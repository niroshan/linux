// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <string.h>
#include <perf/cpumap.h>
#include <perf/evlist.h>
#include "metricgroup.h"
#include "tests.h"
#include "pmu-events/pmu-events.h"
#include "evlist.h"
#include "rblist.h"
#include "debug.h"
#include "expr.h"
#include "stat.h"
#include "pmus.h"

struct value {
	const char	*event;
	u64		 val;
};

static u64 find_value(const char *name, struct value *values)
{
	struct value *v = values;

	while (v->event) {
		if (!strcmp(name, v->event))
			return v->val;
		v++;
	}
	return 0;
}

static void load_runtime_stat(struct evlist *evlist, struct value *vals)
{
	struct evsel *evsel;
	u64 count;

	evlist__alloc_aggr_stats(evlist, 1);
	evlist__for_each_entry(evlist, evsel) {
		count = find_value(evsel->name, vals);
		evsel->supported = true;
		evsel->stats->aggr->counts.val = count;
		if (evsel__name_is(evsel, "duration_time"))
			update_stats(&walltime_nsecs_stats, count);
	}
}

static double compute_single(struct evlist *evlist, const char *name)
{
	struct metric_expr *mexp;
	struct metric_event *me;
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		me = metricgroup__lookup(&evlist->metric_events, evsel, false);
		if (me != NULL) {
			list_for_each_entry (mexp, &me->head, nd) {
				if (strcmp(mexp->metric_name, name))
					continue;
				return test_generic_metric(mexp, 0);
			}
		}
	}
	return 0.;
}

static int __compute_metric(const char *name, struct value *vals,
			    const char *name1, double *ratio1,
			    const char *name2, double *ratio2)
{
	const struct pmu_metrics_table *pme_test;
	struct perf_cpu_map *cpus;
	struct evlist *evlist;
	int err;

	/*
	 * We need to prepare evlist for stat mode running on CPU 0
	 * because that's where all the stats are going to be created.
	 */
	evlist = evlist__new();
	if (!evlist)
		return -ENOMEM;

	cpus = perf_cpu_map__new("0");
	if (!cpus) {
		evlist__delete(evlist);
		return -ENOMEM;
	}

	perf_evlist__set_maps(&evlist->core, cpus, NULL);

	/* Parse the metric into metric_events list. */
	pme_test = find_core_metrics_table("testarch", "testcpu");
	err = metricgroup__parse_groups_test(evlist, pme_test, name);
	if (err)
		goto out;

	err = evlist__alloc_stats(/*config=*/NULL, evlist, /*alloc_raw=*/false);
	if (err)
		goto out;

	/* Load the runtime stats with given numbers for events. */
	load_runtime_stat(evlist, vals);

	/* And execute the metric */
	if (name1 && ratio1)
		*ratio1 = compute_single(evlist, name1);
	if (name2 && ratio2)
		*ratio2 = compute_single(evlist, name2);

out:
	/* ... cleanup. */
	evlist__free_stats(evlist);
	perf_cpu_map__put(cpus);
	evlist__delete(evlist);
	return err;
}

static int compute_metric(const char *name, struct value *vals, double *ratio)
{
	return __compute_metric(name, vals, name, ratio, NULL, NULL);
}

static int compute_metric_group(const char *name, struct value *vals,
				const char *name1, double *ratio1,
				const char *name2, double *ratio2)
{
	return __compute_metric(name, vals, name1, ratio1, name2, ratio2);
}

static int test_ipc(void)
{
	double ratio;
	struct value vals[] = {
		{ .event = "inst_retired.any",        .val = 300 },
		{ .event = "cpu_clk_unhalted.thread", .val = 200 },
		{ .event = NULL, },
	};

	TEST_ASSERT_VAL("failed to compute metric",
			compute_metric("IPC", vals, &ratio) == 0);

	TEST_ASSERT_VAL("IPC failed, wrong ratio",
			ratio == 1.5);
	return 0;
}

static int test_frontend(void)
{
	double ratio;
	struct value vals[] = {
		{ .event = "idq_uops_not_delivered.core",        .val = 300 },
		{ .event = "cpu_clk_unhalted.thread",            .val = 200 },
		{ .event = "cpu_clk_unhalted.one_thread_active", .val = 400 },
		{ .event = "cpu_clk_unhalted.ref_xclk",          .val = 600 },
		{ .event = NULL, },
	};

	TEST_ASSERT_VAL("failed to compute metric",
			compute_metric("Frontend_Bound_SMT", vals, &ratio) == 0);

	TEST_ASSERT_VAL("Frontend_Bound_SMT failed, wrong ratio",
			ratio == 0.45);
	return 0;
}

static int test_cache_miss_cycles(void)
{
	double ratio;
	struct value vals[] = {
		{ .event = "l1d-loads-misses",  .val = 300 },
		{ .event = "l1i-loads-misses",  .val = 200 },
		{ .event = "inst_retired.any",  .val = 400 },
		{ .event = NULL, },
	};

	TEST_ASSERT_VAL("failed to compute metric",
			compute_metric("cache_miss_cycles", vals, &ratio) == 0);

	TEST_ASSERT_VAL("cache_miss_cycles failed, wrong ratio",
			ratio == 1.25);
	return 0;
}


/*
 * DCache_L2_All_Hits = l2_rqsts.demand_data_rd_hit + l2_rqsts.pf_hit + l2_rqsts.rfo_hi
 * DCache_L2_All_Miss = max(l2_rqsts.all_demand_data_rd - l2_rqsts.demand_data_rd_hit, 0) +
 *                      l2_rqsts.pf_miss + l2_rqsts.rfo_miss
 * DCache_L2_All      = dcache_l2_all_hits + dcache_l2_all_miss
 * DCache_L2_Hits     = d_ratio(dcache_l2_all_hits, dcache_l2_all)
 * DCache_L2_Misses   = d_ratio(dcache_l2_all_miss, dcache_l2_all)
 *
 * l2_rqsts.demand_data_rd_hit = 100
 * l2_rqsts.pf_hit             = 200
 * l2_rqsts.rfo_hi             = 300
 * l2_rqsts.all_demand_data_rd = 400
 * l2_rqsts.pf_miss            = 500
 * l2_rqsts.rfo_miss           = 600
 *
 * DCache_L2_All_Hits = 600
 * DCache_L2_All_Miss = MAX(400 - 100, 0) + 500 + 600 = 1400
 * DCache_L2_All      = 600 + 1400  = 2000
 * DCache_L2_Hits     = 600 / 2000  = 0.3
 * DCache_L2_Misses   = 1400 / 2000 = 0.7
 */
static int test_dcache_l2(void)
{
	double ratio;
	struct value vals[] = {
		{ .event = "l2_rqsts.demand_data_rd_hit", .val = 100 },
		{ .event = "l2_rqsts.pf_hit",             .val = 200 },
		{ .event = "l2_rqsts.rfo_hit",            .val = 300 },
		{ .event = "l2_rqsts.all_demand_data_rd", .val = 400 },
		{ .event = "l2_rqsts.pf_miss",            .val = 500 },
		{ .event = "l2_rqsts.rfo_miss",           .val = 600 },
		{ .event = NULL, },
	};

	TEST_ASSERT_VAL("failed to compute metric",
			compute_metric("DCache_L2_Hits", vals, &ratio) == 0);

	TEST_ASSERT_VAL("DCache_L2_Hits failed, wrong ratio",
			ratio == 0.3);

	TEST_ASSERT_VAL("failed to compute metric",
			compute_metric("DCache_L2_Misses", vals, &ratio) == 0);

	TEST_ASSERT_VAL("DCache_L2_Misses failed, wrong ratio",
			ratio == 0.7);
	return 0;
}

static int test_recursion_fail(void)
{
	double ratio;
	struct value vals[] = {
		{ .event = "inst_retired.any",        .val = 300 },
		{ .event = "cpu_clk_unhalted.thread", .val = 200 },
		{ .event = NULL, },
	};

	TEST_ASSERT_VAL("failed to find recursion",
			compute_metric("M1", vals, &ratio) == -1);

	TEST_ASSERT_VAL("failed to find recursion",
			compute_metric("M3", vals, &ratio) == -1);
	return 0;
}

static int test_memory_bandwidth(void)
{
	double ratio;
	struct value vals[] = {
		{ .event = "l1d.replacement", .val = 4000000 },
		{ .event = "duration_time",  .val = 200000000 },
		{ .event = NULL, },
	};

	TEST_ASSERT_VAL("failed to compute metric",
			compute_metric("L1D_Cache_Fill_BW", vals, &ratio) == 0);
	TEST_ASSERT_VAL("L1D_Cache_Fill_BW, wrong ratio",
			1.28 == ratio);

	return 0;
}

static int test_metric_group(void)
{
	double ratio1, ratio2;
	struct value vals[] = {
		{ .event = "cpu_clk_unhalted.thread", .val = 200 },
		{ .event = "l1d-loads-misses",        .val = 300 },
		{ .event = "l1i-loads-misses",        .val = 200 },
		{ .event = "inst_retired.any",        .val = 400 },
		{ .event = NULL, },
	};

	TEST_ASSERT_VAL("failed to find recursion",
			compute_metric_group("group1", vals,
					     "IPC", &ratio1,
					     "cache_miss_cycles", &ratio2) == 0);

	TEST_ASSERT_VAL("group IPC failed, wrong ratio",
			ratio1 == 2.0);

	TEST_ASSERT_VAL("group cache_miss_cycles failed, wrong ratio",
			ratio2 == 1.25);
	return 0;
}

static int test__parse_metric(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	TEST_ASSERT_VAL("IPC failed", test_ipc() == 0);
	TEST_ASSERT_VAL("frontend failed", test_frontend() == 0);
	TEST_ASSERT_VAL("DCache_L2 failed", test_dcache_l2() == 0);
	TEST_ASSERT_VAL("recursion fail failed", test_recursion_fail() == 0);
	TEST_ASSERT_VAL("Memory bandwidth", test_memory_bandwidth() == 0);
	TEST_ASSERT_VAL("cache_miss_cycles failed", test_cache_miss_cycles() == 0);
	TEST_ASSERT_VAL("test metric group", test_metric_group() == 0);
	return 0;
}

DEFINE_SUITE("Parse and process metrics", parse_metric);

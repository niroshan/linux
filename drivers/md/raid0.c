// SPDX-License-Identifier: GPL-2.0-or-later
/*
   raid0.c : Multiple Devices driver for Linux
	     Copyright (C) 1994-96 Marc ZYNGIER
	     <zyngier@ufr-info-p7.ibp.fr> or
	     <maz@gloups.fdn.fr>
	     Copyright (C) 1999, 2000 Ingo Molnar, Red Hat

   RAID-0 management functions.

*/

#include <linux/blkdev.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <trace/events/block.h>
#include "md.h"
#include "raid0.h"
#include "raid5.h"

static int default_layout = 0;
module_param(default_layout, int, 0644);

#define UNSUPPORTED_MDDEV_FLAGS		\
	((1L << MD_HAS_JOURNAL) |	\
	 (1L << MD_JOURNAL_CLEAN) |	\
	 (1L << MD_FAILFAST_SUPPORTED) |\
	 (1L << MD_HAS_PPL) |		\
	 (1L << MD_HAS_MULTIPLE_PPLS))

/*
 * inform the user of the raid configuration
*/
static void dump_zones(struct mddev *mddev)
{
	int j, k;
	sector_t zone_size = 0;
	sector_t zone_start = 0;
	struct r0conf *conf = mddev->private;
	int raid_disks = conf->strip_zone[0].nb_dev;
	pr_debug("md: RAID0 configuration for %s - %d zone%s\n",
		 mdname(mddev),
		 conf->nr_strip_zones, conf->nr_strip_zones==1?"":"s");
	for (j = 0; j < conf->nr_strip_zones; j++) {
		char line[200];
		int len = 0;

		for (k = 0; k < conf->strip_zone[j].nb_dev; k++)
			len += scnprintf(line+len, 200-len, "%s%pg", k?"/":"",
				conf->devlist[j * raid_disks + k]->bdev);
		pr_debug("md: zone%d=[%s]\n", j, line);

		zone_size  = conf->strip_zone[j].zone_end - zone_start;
		pr_debug("      zone-offset=%10lluKB, device-offset=%10lluKB, size=%10lluKB\n",
			(unsigned long long)zone_start>>1,
			(unsigned long long)conf->strip_zone[j].dev_start>>1,
			(unsigned long long)zone_size>>1);
		zone_start = conf->strip_zone[j].zone_end;
	}
}

static int create_strip_zones(struct mddev *mddev, struct r0conf **private_conf)
{
	int i, c, err;
	sector_t curr_zone_end, sectors;
	struct md_rdev *smallest, *rdev1, *rdev2, *rdev, **dev;
	struct strip_zone *zone;
	int cnt;
	struct r0conf *conf = kzalloc(sizeof(*conf), GFP_KERNEL);
	unsigned blksize = 512;

	*private_conf = ERR_PTR(-ENOMEM);
	if (!conf)
		return -ENOMEM;
	rdev_for_each(rdev1, mddev) {
		pr_debug("md/raid0:%s: looking at %pg\n",
			 mdname(mddev),
			 rdev1->bdev);
		c = 0;

		/* round size to chunk_size */
		sectors = rdev1->sectors;
		sector_div(sectors, mddev->chunk_sectors);
		rdev1->sectors = sectors * mddev->chunk_sectors;

		blksize = max(blksize, queue_logical_block_size(
				      rdev1->bdev->bd_disk->queue));

		rdev_for_each(rdev2, mddev) {
			pr_debug("md/raid0:%s:   comparing %pg(%llu)"
				 " with %pg(%llu)\n",
				 mdname(mddev),
				 rdev1->bdev,
				 (unsigned long long)rdev1->sectors,
				 rdev2->bdev,
				 (unsigned long long)rdev2->sectors);
			if (rdev2 == rdev1) {
				pr_debug("md/raid0:%s:   END\n",
					 mdname(mddev));
				break;
			}
			if (rdev2->sectors == rdev1->sectors) {
				/*
				 * Not unique, don't count it as a new
				 * group
				 */
				pr_debug("md/raid0:%s:   EQUAL\n",
					 mdname(mddev));
				c = 1;
				break;
			}
			pr_debug("md/raid0:%s:   NOT EQUAL\n",
				 mdname(mddev));
		}
		if (!c) {
			pr_debug("md/raid0:%s:   ==> UNIQUE\n",
				 mdname(mddev));
			conf->nr_strip_zones++;
			pr_debug("md/raid0:%s: %d zones\n",
				 mdname(mddev), conf->nr_strip_zones);
		}
	}
	pr_debug("md/raid0:%s: FINAL %d zones\n",
		 mdname(mddev), conf->nr_strip_zones);

	/*
	 * now since we have the hard sector sizes, we can make sure
	 * chunk size is a multiple of that sector size
	 */
	if ((mddev->chunk_sectors << 9) % blksize) {
		pr_warn("md/raid0:%s: chunk_size of %d not multiple of block size %d\n",
			mdname(mddev),
			mddev->chunk_sectors << 9, blksize);
		err = -EINVAL;
		goto abort;
	}

	err = -ENOMEM;
	conf->strip_zone = kcalloc(conf->nr_strip_zones,
				   sizeof(struct strip_zone),
				   GFP_KERNEL);
	if (!conf->strip_zone)
		goto abort;
	conf->devlist = kzalloc(array3_size(sizeof(struct md_rdev *),
					    conf->nr_strip_zones,
					    mddev->raid_disks),
				GFP_KERNEL);
	if (!conf->devlist)
		goto abort;

	/* The first zone must contain all devices, so here we check that
	 * there is a proper alignment of slots to devices and find them all
	 */
	zone = &conf->strip_zone[0];
	cnt = 0;
	smallest = NULL;
	dev = conf->devlist;
	err = -EINVAL;
	rdev_for_each(rdev1, mddev) {
		int j = rdev1->raid_disk;

		if (mddev->level == 10) {
			/* taking over a raid10-n2 array */
			j /= 2;
			rdev1->new_raid_disk = j;
		}

		if (mddev->level == 1) {
			/* taiking over a raid1 array-
			 * we have only one active disk
			 */
			j = 0;
			rdev1->new_raid_disk = j;
		}

		if (j < 0) {
			pr_warn("md/raid0:%s: remove inactive devices before converting to RAID0\n",
				mdname(mddev));
			goto abort;
		}
		if (j >= mddev->raid_disks) {
			pr_warn("md/raid0:%s: bad disk number %d - aborting!\n",
				mdname(mddev), j);
			goto abort;
		}
		if (dev[j]) {
			pr_warn("md/raid0:%s: multiple devices for %d - aborting!\n",
				mdname(mddev), j);
			goto abort;
		}
		dev[j] = rdev1;

		if (!smallest || (rdev1->sectors < smallest->sectors))
			smallest = rdev1;
		cnt++;
	}
	if (cnt != mddev->raid_disks) {
		pr_warn("md/raid0:%s: too few disks (%d of %d) - aborting!\n",
			mdname(mddev), cnt, mddev->raid_disks);
		goto abort;
	}
	zone->nb_dev = cnt;
	zone->zone_end = smallest->sectors * cnt;

	curr_zone_end = zone->zone_end;

	/* now do the other zones */
	for (i = 1; i < conf->nr_strip_zones; i++)
	{
		int j;

		zone = conf->strip_zone + i;
		dev = conf->devlist + i * mddev->raid_disks;

		pr_debug("md/raid0:%s: zone %d\n", mdname(mddev), i);
		zone->dev_start = smallest->sectors;
		smallest = NULL;
		c = 0;

		for (j=0; j<cnt; j++) {
			rdev = conf->devlist[j];
			if (rdev->sectors <= zone->dev_start) {
				pr_debug("md/raid0:%s: checking %pg ... nope\n",
					 mdname(mddev),
					 rdev->bdev);
				continue;
			}
			pr_debug("md/raid0:%s: checking %pg ..."
				 " contained as device %d\n",
				 mdname(mddev),
				 rdev->bdev, c);
			dev[c] = rdev;
			c++;
			if (!smallest || rdev->sectors < smallest->sectors) {
				smallest = rdev;
				pr_debug("md/raid0:%s:  (%llu) is smallest!.\n",
					 mdname(mddev),
					 (unsigned long long)rdev->sectors);
			}
		}

		zone->nb_dev = c;
		sectors = (smallest->sectors - zone->dev_start) * c;
		pr_debug("md/raid0:%s: zone->nb_dev: %d, sectors: %llu\n",
			 mdname(mddev),
			 zone->nb_dev, (unsigned long long)sectors);

		curr_zone_end += sectors;
		zone->zone_end = curr_zone_end;

		pr_debug("md/raid0:%s: current zone start: %llu\n",
			 mdname(mddev),
			 (unsigned long long)smallest->sectors);
	}

	if (conf->nr_strip_zones == 1 || conf->strip_zone[1].nb_dev == 1) {
		conf->layout = RAID0_ORIG_LAYOUT;
	} else if (mddev->layout == RAID0_ORIG_LAYOUT ||
		   mddev->layout == RAID0_ALT_MULTIZONE_LAYOUT) {
		conf->layout = mddev->layout;
	} else if (default_layout == RAID0_ORIG_LAYOUT ||
		   default_layout == RAID0_ALT_MULTIZONE_LAYOUT) {
		conf->layout = default_layout;
	} else {
		pr_err("md/raid0:%s: cannot assemble multi-zone RAID0 with default_layout setting\n",
		       mdname(mddev));
		pr_err("md/raid0: please set raid0.default_layout to 1 or 2\n");
		err = -EOPNOTSUPP;
		goto abort;
	}

	if (conf->layout == RAID0_ORIG_LAYOUT) {
		for (i = 1; i < conf->nr_strip_zones; i++) {
			sector_t first_sector = conf->strip_zone[i-1].zone_end;

			sector_div(first_sector, mddev->chunk_sectors);
			zone = conf->strip_zone + i;
			/* disk_shift is first disk index used in the zone */
			zone->disk_shift = sector_div(first_sector,
						      zone->nb_dev);
		}
	}

	pr_debug("md/raid0:%s: done.\n", mdname(mddev));
	*private_conf = conf;

	return 0;
abort:
	kfree(conf->strip_zone);
	kfree(conf->devlist);
	kfree(conf);
	*private_conf = ERR_PTR(err);
	return err;
}

/* Find the zone which holds a particular offset
 * Update *sectorp to be an offset in that zone
 */
static struct strip_zone *find_zone(struct r0conf *conf,
				    sector_t *sectorp)
{
	int i;
	struct strip_zone *z = conf->strip_zone;
	sector_t sector = *sectorp;

	for (i = 0; i < conf->nr_strip_zones; i++)
		if (sector < z[i].zone_end) {
			if (i)
				*sectorp = sector - z[i-1].zone_end;
			return z + i;
		}
	BUG();
}

/*
 * remaps the bio to the target device. we separate two flows.
 * power 2 flow and a general flow for the sake of performance
*/
static struct md_rdev *map_sector(struct mddev *mddev, struct strip_zone *zone,
				sector_t sector, sector_t *sector_offset)
{
	unsigned int sect_in_chunk;
	sector_t chunk;
	struct r0conf *conf = mddev->private;
	int raid_disks = conf->strip_zone[0].nb_dev;
	unsigned int chunk_sects = mddev->chunk_sectors;

	if (is_power_of_2(chunk_sects)) {
		int chunksect_bits = ffz(~chunk_sects);
		/* find the sector offset inside the chunk */
		sect_in_chunk  = sector & (chunk_sects - 1);
		sector >>= chunksect_bits;
		/* chunk in zone */
		chunk = *sector_offset;
		/* quotient is the chunk in real device*/
		sector_div(chunk, zone->nb_dev << chunksect_bits);
	} else{
		sect_in_chunk = sector_div(sector, chunk_sects);
		chunk = *sector_offset;
		sector_div(chunk, chunk_sects * zone->nb_dev);
	}
	/*
	*  position the bio over the real device
	*  real sector = chunk in device + starting of zone
	*	+ the position in the chunk
	*/
	*sector_offset = (chunk * chunk_sects) + sect_in_chunk;
	return conf->devlist[(zone - conf->strip_zone)*raid_disks
			     + sector_div(sector, zone->nb_dev)];
}

static sector_t raid0_size(struct mddev *mddev, sector_t sectors, int raid_disks)
{
	sector_t array_sectors = 0;
	struct md_rdev *rdev;

	WARN_ONCE(sectors || raid_disks,
		  "%s does not support generic reshape\n", __func__);

	rdev_for_each(rdev, mddev)
		array_sectors += (rdev->sectors &
				  ~(sector_t)(mddev->chunk_sectors-1));

	return array_sectors;
}

static void raid0_free(struct mddev *mddev, void *priv)
{
	struct r0conf *conf = priv;

	kfree(conf->strip_zone);
	kfree(conf->devlist);
	kfree(conf);
}

static int raid0_set_limits(struct mddev *mddev)
{
	struct queue_limits lim;
	int err;

	md_init_stacking_limits(&lim);
	lim.max_hw_sectors = mddev->chunk_sectors;
	lim.max_write_zeroes_sectors = mddev->chunk_sectors;
	lim.io_min = mddev->chunk_sectors << 9;
	lim.io_opt = lim.io_min * mddev->raid_disks;
	lim.chunk_sectors = mddev->chunk_sectors;
	lim.features |= BLK_FEAT_ATOMIC_WRITES;
	err = mddev_stack_rdev_limits(mddev, &lim, MDDEV_STACK_INTEGRITY);
	if (err)
		return err;
	return queue_limits_set(mddev->gendisk->queue, &lim);
}

static int raid0_run(struct mddev *mddev)
{
	struct r0conf *conf;
	int ret;

	if (mddev->chunk_sectors == 0) {
		pr_warn("md/raid0:%s: chunk size must be set.\n", mdname(mddev));
		return -EINVAL;
	}
	if (md_check_no_bitmap(mddev))
		return -EINVAL;

	/* if private is not null, we are here after takeover */
	if (mddev->private == NULL) {
		ret = create_strip_zones(mddev, &conf);
		if (ret < 0)
			return ret;
		mddev->private = conf;
	}
	conf = mddev->private;
	if (!mddev_is_dm(mddev)) {
		ret = raid0_set_limits(mddev);
		if (ret)
			return ret;
	}

	/* calculate array device size */
	md_set_array_sectors(mddev, raid0_size(mddev, 0, 0));

	pr_debug("md/raid0:%s: md_size is %llu sectors.\n",
		 mdname(mddev),
		 (unsigned long long)mddev->array_sectors);

	dump_zones(mddev);

	return md_integrity_register(mddev);
}

/*
 * Convert disk_index to the disk order in which it is read/written.
 *  For example, if we have 4 disks, they are numbered 0,1,2,3. If we
 *  write the disks starting at disk 3, then the read/write order would
 *  be disk 3, then 0, then 1, and then disk 2 and we want map_disk_shift()
 *  to map the disks as follows 0,1,2,3 => 1,2,3,0. So disk 0 would map
 *  to 1, 1 to 2, 2 to 3, and 3 to 0. That way we can compare disks in
 *  that 'output' space to understand the read/write disk ordering.
 */
static int map_disk_shift(int disk_index, int num_disks, int disk_shift)
{
	return ((disk_index + num_disks - disk_shift) % num_disks);
}

static void raid0_handle_discard(struct mddev *mddev, struct bio *bio)
{
	struct r0conf *conf = mddev->private;
	struct strip_zone *zone;
	sector_t start = bio->bi_iter.bi_sector;
	sector_t end;
	unsigned int stripe_size;
	sector_t first_stripe_index, last_stripe_index;
	sector_t start_disk_offset;
	unsigned int start_disk_index;
	sector_t end_disk_offset;
	unsigned int end_disk_index;
	unsigned int disk;
	sector_t orig_start, orig_end;

	orig_start = start;
	zone = find_zone(conf, &start);

	if (bio_end_sector(bio) > zone->zone_end) {
		struct bio *split = bio_split(bio,
			zone->zone_end - bio->bi_iter.bi_sector, GFP_NOIO,
			&mddev->bio_set);

		if (IS_ERR(split)) {
			bio->bi_status = errno_to_blk_status(PTR_ERR(split));
			bio_endio(bio);
			return;
		}
		bio_chain(split, bio);
		submit_bio_noacct(bio);
		bio = split;
		end = zone->zone_end;
	} else
		end = bio_end_sector(bio);

	orig_end = end;
	if (zone != conf->strip_zone)
		end = end - zone[-1].zone_end;

	/* Now start and end is the offset in zone */
	stripe_size = zone->nb_dev * mddev->chunk_sectors;

	first_stripe_index = start;
	sector_div(first_stripe_index, stripe_size);
	last_stripe_index = end;
	sector_div(last_stripe_index, stripe_size);

	/* In the first zone the original and alternate layouts are the same */
	if ((conf->layout == RAID0_ORIG_LAYOUT) && (zone != conf->strip_zone)) {
		sector_div(orig_start, mddev->chunk_sectors);
		start_disk_index = sector_div(orig_start, zone->nb_dev);
		start_disk_index = map_disk_shift(start_disk_index,
						  zone->nb_dev,
						  zone->disk_shift);
		sector_div(orig_end, mddev->chunk_sectors);
		end_disk_index = sector_div(orig_end, zone->nb_dev);
		end_disk_index = map_disk_shift(end_disk_index,
						zone->nb_dev, zone->disk_shift);
	} else {
		start_disk_index = (int)(start - first_stripe_index * stripe_size) /
			mddev->chunk_sectors;
		end_disk_index = (int)(end - last_stripe_index * stripe_size) /
			mddev->chunk_sectors;
	}
	start_disk_offset = ((int)(start - first_stripe_index * stripe_size) %
		mddev->chunk_sectors) +
		first_stripe_index * mddev->chunk_sectors;
	end_disk_offset = ((int)(end - last_stripe_index * stripe_size) %
		mddev->chunk_sectors) +
		last_stripe_index * mddev->chunk_sectors;

	for (disk = 0; disk < zone->nb_dev; disk++) {
		sector_t dev_start, dev_end;
		struct md_rdev *rdev;
		int compare_disk;

		compare_disk = map_disk_shift(disk, zone->nb_dev,
					      zone->disk_shift);

		if (compare_disk < start_disk_index)
			dev_start = (first_stripe_index + 1) *
				mddev->chunk_sectors;
		else if (compare_disk > start_disk_index)
			dev_start = first_stripe_index * mddev->chunk_sectors;
		else
			dev_start = start_disk_offset;

		if (compare_disk < end_disk_index)
			dev_end = (last_stripe_index + 1) * mddev->chunk_sectors;
		else if (compare_disk > end_disk_index)
			dev_end = last_stripe_index * mddev->chunk_sectors;
		else
			dev_end = end_disk_offset;

		if (dev_end <= dev_start)
			continue;

		rdev = conf->devlist[(zone - conf->strip_zone) *
			conf->strip_zone[0].nb_dev + disk];
		md_submit_discard_bio(mddev, rdev, bio,
			dev_start + zone->dev_start + rdev->data_offset,
			dev_end - dev_start);
	}
	bio_endio(bio);
}

static void raid0_map_submit_bio(struct mddev *mddev, struct bio *bio)
{
	struct r0conf *conf = mddev->private;
	struct strip_zone *zone;
	struct md_rdev *tmp_dev;
	sector_t bio_sector = bio->bi_iter.bi_sector;
	sector_t sector = bio_sector;

	md_account_bio(mddev, &bio);

	zone = find_zone(mddev->private, &sector);
	switch (conf->layout) {
	case RAID0_ORIG_LAYOUT:
		tmp_dev = map_sector(mddev, zone, bio_sector, &sector);
		break;
	case RAID0_ALT_MULTIZONE_LAYOUT:
		tmp_dev = map_sector(mddev, zone, sector, &sector);
		break;
	default:
		WARN(1, "md/raid0:%s: Invalid layout\n", mdname(mddev));
		bio_io_error(bio);
		return;
	}

	if (unlikely(is_rdev_broken(tmp_dev))) {
		bio_io_error(bio);
		md_error(mddev, tmp_dev);
		return;
	}

	bio_set_dev(bio, tmp_dev->bdev);
	bio->bi_iter.bi_sector = sector + zone->dev_start +
		tmp_dev->data_offset;
	mddev_trace_remap(mddev, bio, bio_sector);
	mddev_check_write_zeroes(mddev, bio);
	submit_bio_noacct(bio);
}

static bool raid0_make_request(struct mddev *mddev, struct bio *bio)
{
	sector_t sector;
	unsigned chunk_sects;
	unsigned sectors;

	if (unlikely(bio->bi_opf & REQ_PREFLUSH)
	    && md_flush_request(mddev, bio))
		return true;

	if (unlikely((bio_op(bio) == REQ_OP_DISCARD))) {
		raid0_handle_discard(mddev, bio);
		return true;
	}

	sector = bio->bi_iter.bi_sector;
	chunk_sects = mddev->chunk_sectors;

	sectors = chunk_sects -
		(likely(is_power_of_2(chunk_sects))
		 ? (sector & (chunk_sects-1))
		 : sector_div(sector, chunk_sects));

	if (sectors < bio_sectors(bio)) {
		struct bio *split = bio_split(bio, sectors, GFP_NOIO,
					      &mddev->bio_set);

		if (IS_ERR(split)) {
			bio->bi_status = errno_to_blk_status(PTR_ERR(split));
			bio_endio(bio);
			return true;
		}
		bio_chain(split, bio);
		raid0_map_submit_bio(mddev, bio);
		bio = split;
	}

	raid0_map_submit_bio(mddev, bio);
	return true;
}

static void raid0_status(struct seq_file *seq, struct mddev *mddev)
{
	seq_printf(seq, " %dk chunks", mddev->chunk_sectors / 2);
	return;
}

static void raid0_error(struct mddev *mddev, struct md_rdev *rdev)
{
	if (!test_and_set_bit(MD_BROKEN, &mddev->flags)) {
		char *md_name = mdname(mddev);

		pr_crit("md/raid0%s: Disk failure on %pg detected, failing array.\n",
			md_name, rdev->bdev);
	}
}

static void *raid0_takeover_raid45(struct mddev *mddev)
{
	struct md_rdev *rdev;
	struct r0conf *priv_conf;

	if (mddev->degraded != 1) {
		pr_warn("md/raid0:%s: raid5 must be degraded! Degraded disks: %d\n",
			mdname(mddev),
			mddev->degraded);
		return ERR_PTR(-EINVAL);
	}

	rdev_for_each(rdev, mddev) {
		/* check slot number for a disk */
		if (rdev->raid_disk == mddev->raid_disks-1) {
			pr_warn("md/raid0:%s: raid5 must have missing parity disk!\n",
				mdname(mddev));
			return ERR_PTR(-EINVAL);
		}
		rdev->sectors = mddev->dev_sectors;
	}

	/* Set new parameters */
	mddev->new_level = 0;
	mddev->new_layout = 0;
	mddev->new_chunk_sectors = mddev->chunk_sectors;
	mddev->raid_disks--;
	mddev->delta_disks = -1;
	/* make sure it will be not marked as dirty */
	mddev->recovery_cp = MaxSector;
	mddev_clear_unsupported_flags(mddev, UNSUPPORTED_MDDEV_FLAGS);

	create_strip_zones(mddev, &priv_conf);

	return priv_conf;
}

static void *raid0_takeover_raid10(struct mddev *mddev)
{
	struct r0conf *priv_conf;

	/* Check layout:
	 *  - far_copies must be 1
	 *  - near_copies must be 2
	 *  - disks number must be even
	 *  - all mirrors must be already degraded
	 */
	if (mddev->layout != ((1 << 8) + 2)) {
		pr_warn("md/raid0:%s:: Raid0 cannot takeover layout: 0x%x\n",
			mdname(mddev),
			mddev->layout);
		return ERR_PTR(-EINVAL);
	}
	if (mddev->raid_disks & 1) {
		pr_warn("md/raid0:%s: Raid0 cannot takeover Raid10 with odd disk number.\n",
			mdname(mddev));
		return ERR_PTR(-EINVAL);
	}
	if (mddev->degraded != (mddev->raid_disks>>1)) {
		pr_warn("md/raid0:%s: All mirrors must be already degraded!\n",
			mdname(mddev));
		return ERR_PTR(-EINVAL);
	}

	/* Set new parameters */
	mddev->new_level = 0;
	mddev->new_layout = 0;
	mddev->new_chunk_sectors = mddev->chunk_sectors;
	mddev->delta_disks = - mddev->raid_disks / 2;
	mddev->raid_disks += mddev->delta_disks;
	mddev->degraded = 0;
	/* make sure it will be not marked as dirty */
	mddev->recovery_cp = MaxSector;
	mddev_clear_unsupported_flags(mddev, UNSUPPORTED_MDDEV_FLAGS);

	create_strip_zones(mddev, &priv_conf);
	return priv_conf;
}

static void *raid0_takeover_raid1(struct mddev *mddev)
{
	struct r0conf *priv_conf;
	int chunksect;

	/* Check layout:
	 *  - (N - 1) mirror drives must be already faulty
	 */
	if ((mddev->raid_disks - 1) != mddev->degraded) {
		pr_err("md/raid0:%s: (N - 1) mirrors drives must be already faulty!\n",
		       mdname(mddev));
		return ERR_PTR(-EINVAL);
	}

	/*
	 * a raid1 doesn't have the notion of chunk size, so
	 * figure out the largest suitable size we can use.
	 */
	chunksect = 64 * 2; /* 64K by default */

	/* The array must be an exact multiple of chunksize */
	while (chunksect && (mddev->array_sectors & (chunksect - 1)))
		chunksect >>= 1;

	if ((chunksect << 9) < PAGE_SIZE)
		/* array size does not allow a suitable chunk size */
		return ERR_PTR(-EINVAL);

	/* Set new parameters */
	mddev->new_level = 0;
	mddev->new_layout = 0;
	mddev->new_chunk_sectors = chunksect;
	mddev->chunk_sectors = chunksect;
	mddev->delta_disks = 1 - mddev->raid_disks;
	mddev->raid_disks = 1;
	/* make sure it will be not marked as dirty */
	mddev->recovery_cp = MaxSector;
	mddev_clear_unsupported_flags(mddev, UNSUPPORTED_MDDEV_FLAGS);

	create_strip_zones(mddev, &priv_conf);
	return priv_conf;
}

static void *raid0_takeover(struct mddev *mddev)
{
	/* raid0 can take over:
	 *  raid4 - if all data disks are active.
	 *  raid5 - providing it is Raid4 layout and one disk is faulty
	 *  raid10 - assuming we have all necessary active disks
	 *  raid1 - with (N -1) mirror drives faulty
	 */

	if (mddev->bitmap) {
		pr_warn("md/raid0: %s: cannot takeover array with bitmap\n",
			mdname(mddev));
		return ERR_PTR(-EBUSY);
	}
	if (mddev->level == 4)
		return raid0_takeover_raid45(mddev);

	if (mddev->level == 5) {
		if (mddev->layout == ALGORITHM_PARITY_N)
			return raid0_takeover_raid45(mddev);

		pr_warn("md/raid0:%s: Raid can only takeover Raid5 with layout: %d\n",
			mdname(mddev), ALGORITHM_PARITY_N);
	}

	if (mddev->level == 10)
		return raid0_takeover_raid10(mddev);

	if (mddev->level == 1)
		return raid0_takeover_raid1(mddev);

	pr_warn("Takeover from raid%i to raid0 not supported\n",
		mddev->level);

	return ERR_PTR(-EINVAL);
}

static void raid0_quiesce(struct mddev *mddev, int quiesce)
{
}

static struct md_personality raid0_personality=
{
	.head = {
		.type	= MD_PERSONALITY,
		.id	= ID_RAID0,
		.name	= "raid0",
		.owner	= THIS_MODULE,
	},

	.make_request	= raid0_make_request,
	.run		= raid0_run,
	.free		= raid0_free,
	.status		= raid0_status,
	.size		= raid0_size,
	.takeover	= raid0_takeover,
	.quiesce	= raid0_quiesce,
	.error_handler	= raid0_error,
};

static int __init raid0_init(void)
{
	return register_md_submodule(&raid0_personality.head);
}

static void __exit raid0_exit(void)
{
	unregister_md_submodule(&raid0_personality.head);
}

module_init(raid0_init);
module_exit(raid0_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAID0 (striping) personality for MD");
MODULE_ALIAS("md-personality-2"); /* RAID0 */
MODULE_ALIAS("md-raid0");
MODULE_ALIAS("md-level-0");

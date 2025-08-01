// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. */

#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/firmware.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/pci.h>
#include <cxlmem.h>
#include "trace.h"
#include "core.h"

static DECLARE_RWSEM(cxl_memdev_rwsem);

/*
 * An entire PCI topology full of devices should be enough for any
 * config
 */
#define CXL_MEM_MAX_DEVS 65536

static int cxl_mem_major;
static DEFINE_IDA(cxl_memdev_ida);

static void cxl_memdev_release(struct device *dev)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);

	ida_free(&cxl_memdev_ida, cxlmd->id);
	devm_cxl_memdev_edac_release(cxlmd);
	kfree(cxlmd);
}

static char *cxl_memdev_devnode(const struct device *dev, umode_t *mode, kuid_t *uid,
				kgid_t *gid)
{
	return kasprintf(GFP_KERNEL, "cxl/%s", dev_name(dev));
}

static ssize_t firmware_version_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);

	if (!mds)
		return sysfs_emit(buf, "\n");
	return sysfs_emit(buf, "%.16s\n", mds->firmware_version);
}
static DEVICE_ATTR_RO(firmware_version);

static ssize_t payload_max_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);

	if (!mds)
		return sysfs_emit(buf, "\n");
	return sysfs_emit(buf, "%zu\n", cxlds->cxl_mbox.payload_size);
}
static DEVICE_ATTR_RO(payload_max);

static ssize_t label_storage_size_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);

	if (!mds)
		return sysfs_emit(buf, "\n");
	return sysfs_emit(buf, "%zu\n", mds->lsa_size);
}
static DEVICE_ATTR_RO(label_storage_size);

static resource_size_t cxl_ram_size(struct cxl_dev_state *cxlds)
{
	/* Static RAM is only expected at partition 0. */
	if (cxlds->part[0].mode != CXL_PARTMODE_RAM)
		return 0;
	return resource_size(&cxlds->part[0].res);
}

static ssize_t ram_size_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	unsigned long long len = cxl_ram_size(cxlds);

	return sysfs_emit(buf, "%#llx\n", len);
}

static struct device_attribute dev_attr_ram_size =
	__ATTR(size, 0444, ram_size_show, NULL);

static ssize_t pmem_size_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	unsigned long long len = cxl_pmem_size(cxlds);

	return sysfs_emit(buf, "%#llx\n", len);
}

static struct device_attribute dev_attr_pmem_size =
	__ATTR(size, 0444, pmem_size_show, NULL);

static ssize_t serial_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	return sysfs_emit(buf, "%#llx\n", cxlds->serial);
}
static DEVICE_ATTR_RO(serial);

static ssize_t numa_node_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return sysfs_emit(buf, "%d\n", dev_to_node(dev));
}
static DEVICE_ATTR_RO(numa_node);

static ssize_t security_state_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_mailbox *cxl_mbox = &cxlds->cxl_mbox;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	unsigned long state = mds->security.state;
	int rc = 0;

	/* sync with latest submission state */
	mutex_lock(&cxl_mbox->mbox_mutex);
	if (mds->security.sanitize_active)
		rc = sysfs_emit(buf, "sanitize\n");
	mutex_unlock(&cxl_mbox->mbox_mutex);
	if (rc)
		return rc;

	if (!(state & CXL_PMEM_SEC_STATE_USER_PASS_SET))
		return sysfs_emit(buf, "disabled\n");
	if (state & CXL_PMEM_SEC_STATE_FROZEN ||
	    state & CXL_PMEM_SEC_STATE_MASTER_PLIMIT ||
	    state & CXL_PMEM_SEC_STATE_USER_PLIMIT)
		return sysfs_emit(buf, "frozen\n");
	if (state & CXL_PMEM_SEC_STATE_LOCKED)
		return sysfs_emit(buf, "locked\n");

	return sysfs_emit(buf, "unlocked\n");
}
static struct device_attribute dev_attr_security_state =
	__ATTR(state, 0444, security_state_show, NULL);

static ssize_t security_sanitize_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	bool sanitize;
	ssize_t rc;

	if (kstrtobool(buf, &sanitize) || !sanitize)
		return -EINVAL;

	rc = cxl_mem_sanitize(cxlmd, CXL_MBOX_OP_SANITIZE);
	if (rc)
		return rc;

	return len;
}
static struct device_attribute dev_attr_security_sanitize =
	__ATTR(sanitize, 0200, NULL, security_sanitize_store);

static ssize_t security_erase_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	ssize_t rc;
	bool erase;

	if (kstrtobool(buf, &erase) || !erase)
		return -EINVAL;

	rc = cxl_mem_sanitize(cxlmd, CXL_MBOX_OP_SECURE_ERASE);
	if (rc)
		return rc;

	return len;
}
static struct device_attribute dev_attr_security_erase =
	__ATTR(erase, 0200, NULL, security_erase_store);

static int cxl_get_poison_by_memdev(struct cxl_memdev *cxlmd)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	u64 offset, length;
	int rc = 0;

	/* CXL 3.0 Spec 8.2.9.8.4.1 Separate pmem and ram poison requests */
	for (int i = 0; i < cxlds->nr_partitions; i++) {
		const struct resource *res = &cxlds->part[i].res;

		offset = res->start;
		length = resource_size(res);
		rc = cxl_mem_get_poison(cxlmd, offset, length, NULL);
		/*
		 * Invalid Physical Address is not an error for
		 * volatile addresses. Device support is optional.
		 */
		if (rc == -EFAULT && cxlds->part[i].mode == CXL_PARTMODE_RAM)
			rc = 0;
	}
	return rc;
}

int cxl_trigger_poison_list(struct cxl_memdev *cxlmd)
{
	struct cxl_port *port;
	int rc;

	port = cxlmd->endpoint;
	if (!port || !is_cxl_endpoint(port))
		return -EINVAL;

	ACQUIRE(rwsem_read_intr, region_rwsem)(&cxl_rwsem.region);
	if ((rc = ACQUIRE_ERR(rwsem_read_intr, &region_rwsem)))
		return rc;

	ACQUIRE(rwsem_read_intr, dpa_rwsem)(&cxl_rwsem.dpa);
	if ((rc = ACQUIRE_ERR(rwsem_read_intr, &dpa_rwsem)))
		return rc;

	if (cxl_num_decoders_committed(port) == 0) {
		/* No regions mapped to this memdev */
		rc = cxl_get_poison_by_memdev(cxlmd);
	} else {
		/* Regions mapped, collect poison by endpoint */
		rc =  cxl_get_poison_by_endpoint(port);
	}

	return rc;
}
EXPORT_SYMBOL_NS_GPL(cxl_trigger_poison_list, "CXL");

static int cxl_validate_poison_dpa(struct cxl_memdev *cxlmd, u64 dpa)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	if (!IS_ENABLED(CONFIG_DEBUG_FS))
		return 0;

	if (!resource_size(&cxlds->dpa_res)) {
		dev_dbg(cxlds->dev, "device has no dpa resource\n");
		return -EINVAL;
	}
	if (!cxl_resource_contains_addr(&cxlds->dpa_res, dpa)) {
		dev_dbg(cxlds->dev, "dpa:0x%llx not in resource:%pR\n",
			dpa, &cxlds->dpa_res);
		return -EINVAL;
	}
	if (!IS_ALIGNED(dpa, 64)) {
		dev_dbg(cxlds->dev, "dpa:0x%llx is not 64-byte aligned\n", dpa);
		return -EINVAL;
	}

	return 0;
}

int cxl_inject_poison(struct cxl_memdev *cxlmd, u64 dpa)
{
	struct cxl_mailbox *cxl_mbox = &cxlmd->cxlds->cxl_mbox;
	struct cxl_mbox_inject_poison inject;
	struct cxl_poison_record record;
	struct cxl_mbox_cmd mbox_cmd;
	struct cxl_region *cxlr;
	int rc;

	if (!IS_ENABLED(CONFIG_DEBUG_FS))
		return 0;

	ACQUIRE(rwsem_read_intr, region_rwsem)(&cxl_rwsem.region);
	if ((rc = ACQUIRE_ERR(rwsem_read_intr, &region_rwsem)))
		return rc;

	ACQUIRE(rwsem_read_intr, dpa_rwsem)(&cxl_rwsem.dpa);
	if ((rc = ACQUIRE_ERR(rwsem_read_intr, &dpa_rwsem)))
		return rc;

	rc = cxl_validate_poison_dpa(cxlmd, dpa);
	if (rc)
		return rc;

	inject.address = cpu_to_le64(dpa);
	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_INJECT_POISON,
		.size_in = sizeof(inject),
		.payload_in = &inject,
	};
	rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
	if (rc)
		return rc;

	cxlr = cxl_dpa_to_region(cxlmd, dpa);
	if (cxlr)
		dev_warn_once(cxl_mbox->host,
			      "poison inject dpa:%#llx region: %s\n", dpa,
			      dev_name(&cxlr->dev));

	record = (struct cxl_poison_record) {
		.address = cpu_to_le64(dpa),
		.length = cpu_to_le32(1),
	};
	trace_cxl_poison(cxlmd, cxlr, &record, 0, 0, CXL_POISON_TRACE_INJECT);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_inject_poison, "CXL");

int cxl_clear_poison(struct cxl_memdev *cxlmd, u64 dpa)
{
	struct cxl_mailbox *cxl_mbox = &cxlmd->cxlds->cxl_mbox;
	struct cxl_mbox_clear_poison clear;
	struct cxl_poison_record record;
	struct cxl_mbox_cmd mbox_cmd;
	struct cxl_region *cxlr;
	int rc;

	if (!IS_ENABLED(CONFIG_DEBUG_FS))
		return 0;

	ACQUIRE(rwsem_read_intr, region_rwsem)(&cxl_rwsem.region);
	if ((rc = ACQUIRE_ERR(rwsem_read_intr, &region_rwsem)))
		return rc;

	ACQUIRE(rwsem_read_intr, dpa_rwsem)(&cxl_rwsem.dpa);
	if ((rc = ACQUIRE_ERR(rwsem_read_intr, &dpa_rwsem)))
		return rc;

	rc = cxl_validate_poison_dpa(cxlmd, dpa);
	if (rc)
		return rc;

	/*
	 * In CXL 3.0 Spec 8.2.9.8.4.3, the Clear Poison mailbox command
	 * is defined to accept 64 bytes of write-data, along with the
	 * address to clear. This driver uses zeroes as write-data.
	 */
	clear = (struct cxl_mbox_clear_poison) {
		.address = cpu_to_le64(dpa)
	};

	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_CLEAR_POISON,
		.size_in = sizeof(clear),
		.payload_in = &clear,
	};

	rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
	if (rc)
		return rc;

	cxlr = cxl_dpa_to_region(cxlmd, dpa);
	if (cxlr)
		dev_warn_once(cxl_mbox->host,
			      "poison clear dpa:%#llx region: %s\n", dpa,
			      dev_name(&cxlr->dev));

	record = (struct cxl_poison_record) {
		.address = cpu_to_le64(dpa),
		.length = cpu_to_le32(1),
	};
	trace_cxl_poison(cxlmd, cxlr, &record, 0, 0, CXL_POISON_TRACE_CLEAR);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_clear_poison, "CXL");

static struct attribute *cxl_memdev_attributes[] = {
	&dev_attr_serial.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_payload_max.attr,
	&dev_attr_label_storage_size.attr,
	&dev_attr_numa_node.attr,
	NULL,
};

static struct cxl_dpa_perf *to_pmem_perf(struct cxl_dev_state *cxlds)
{
	for (int i = 0; i < cxlds->nr_partitions; i++)
		if (cxlds->part[i].mode == CXL_PARTMODE_PMEM)
			return &cxlds->part[i].perf;
	return NULL;
}

static ssize_t pmem_qos_class_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	return sysfs_emit(buf, "%d\n", to_pmem_perf(cxlds)->qos_class);
}

static struct device_attribute dev_attr_pmem_qos_class =
	__ATTR(qos_class, 0444, pmem_qos_class_show, NULL);

static struct attribute *cxl_memdev_pmem_attributes[] = {
	&dev_attr_pmem_size.attr,
	&dev_attr_pmem_qos_class.attr,
	NULL,
};

static struct cxl_dpa_perf *to_ram_perf(struct cxl_dev_state *cxlds)
{
	if (cxlds->part[0].mode != CXL_PARTMODE_RAM)
		return NULL;
	return &cxlds->part[0].perf;
}

static ssize_t ram_qos_class_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	return sysfs_emit(buf, "%d\n", to_ram_perf(cxlds)->qos_class);
}

static struct device_attribute dev_attr_ram_qos_class =
	__ATTR(qos_class, 0444, ram_qos_class_show, NULL);

static struct attribute *cxl_memdev_ram_attributes[] = {
	&dev_attr_ram_size.attr,
	&dev_attr_ram_qos_class.attr,
	NULL,
};

static struct attribute *cxl_memdev_security_attributes[] = {
	&dev_attr_security_state.attr,
	&dev_attr_security_sanitize.attr,
	&dev_attr_security_erase.attr,
	NULL,
};

static umode_t cxl_memdev_visible(struct kobject *kobj, struct attribute *a,
				  int n)
{
	if (!IS_ENABLED(CONFIG_NUMA) && a == &dev_attr_numa_node.attr)
		return 0;
	return a->mode;
}

static struct attribute_group cxl_memdev_attribute_group = {
	.attrs = cxl_memdev_attributes,
	.is_visible = cxl_memdev_visible,
};

static umode_t cxl_ram_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dpa_perf *perf = to_ram_perf(cxlmd->cxlds);

	if (a == &dev_attr_ram_qos_class.attr &&
	    (!perf || perf->qos_class == CXL_QOS_CLASS_INVALID))
		return 0;

	return a->mode;
}

static struct attribute_group cxl_memdev_ram_attribute_group = {
	.name = "ram",
	.attrs = cxl_memdev_ram_attributes,
	.is_visible = cxl_ram_visible,
};

static umode_t cxl_pmem_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dpa_perf *perf = to_pmem_perf(cxlmd->cxlds);

	if (a == &dev_attr_pmem_qos_class.attr &&
	    (!perf || perf->qos_class == CXL_QOS_CLASS_INVALID))
		return 0;

	return a->mode;
}

static struct attribute_group cxl_memdev_pmem_attribute_group = {
	.name = "pmem",
	.attrs = cxl_memdev_pmem_attributes,
	.is_visible = cxl_pmem_visible,
};

static umode_t cxl_memdev_security_visible(struct kobject *kobj,
					   struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);

	if (a == &dev_attr_security_sanitize.attr &&
	    !test_bit(CXL_SEC_ENABLED_SANITIZE, mds->security.enabled_cmds))
		return 0;

	if (a == &dev_attr_security_erase.attr &&
	    !test_bit(CXL_SEC_ENABLED_SECURE_ERASE, mds->security.enabled_cmds))
		return 0;

	return a->mode;
}

static struct attribute_group cxl_memdev_security_attribute_group = {
	.name = "security",
	.attrs = cxl_memdev_security_attributes,
	.is_visible = cxl_memdev_security_visible,
};

static const struct attribute_group *cxl_memdev_attribute_groups[] = {
	&cxl_memdev_attribute_group,
	&cxl_memdev_ram_attribute_group,
	&cxl_memdev_pmem_attribute_group,
	&cxl_memdev_security_attribute_group,
	NULL,
};

void cxl_memdev_update_perf(struct cxl_memdev *cxlmd)
{
	sysfs_update_group(&cxlmd->dev.kobj, &cxl_memdev_ram_attribute_group);
	sysfs_update_group(&cxlmd->dev.kobj, &cxl_memdev_pmem_attribute_group);
}
EXPORT_SYMBOL_NS_GPL(cxl_memdev_update_perf, "CXL");

static const struct device_type cxl_memdev_type = {
	.name = "cxl_memdev",
	.release = cxl_memdev_release,
	.devnode = cxl_memdev_devnode,
	.groups = cxl_memdev_attribute_groups,
};

bool is_cxl_memdev(const struct device *dev)
{
	return dev->type == &cxl_memdev_type;
}
EXPORT_SYMBOL_NS_GPL(is_cxl_memdev, "CXL");

/**
 * set_exclusive_cxl_commands() - atomically disable user cxl commands
 * @mds: The device state to operate on
 * @cmds: bitmap of commands to mark exclusive
 *
 * Grab the cxl_memdev_rwsem in write mode to flush in-flight
 * invocations of the ioctl path and then disable future execution of
 * commands with the command ids set in @cmds.
 */
void set_exclusive_cxl_commands(struct cxl_memdev_state *mds,
				unsigned long *cmds)
{
	struct cxl_mailbox *cxl_mbox = &mds->cxlds.cxl_mbox;

	guard(rwsem_write)(&cxl_memdev_rwsem);
	bitmap_or(cxl_mbox->exclusive_cmds, cxl_mbox->exclusive_cmds,
		  cmds, CXL_MEM_COMMAND_ID_MAX);
}
EXPORT_SYMBOL_NS_GPL(set_exclusive_cxl_commands, "CXL");

/**
 * clear_exclusive_cxl_commands() - atomically enable user cxl commands
 * @mds: The device state to modify
 * @cmds: bitmap of commands to mark available for userspace
 */
void clear_exclusive_cxl_commands(struct cxl_memdev_state *mds,
				  unsigned long *cmds)
{
	struct cxl_mailbox *cxl_mbox = &mds->cxlds.cxl_mbox;

	guard(rwsem_write)(&cxl_memdev_rwsem);
	bitmap_andnot(cxl_mbox->exclusive_cmds, cxl_mbox->exclusive_cmds,
		      cmds, CXL_MEM_COMMAND_ID_MAX);
}
EXPORT_SYMBOL_NS_GPL(clear_exclusive_cxl_commands, "CXL");

static void cxl_memdev_shutdown(struct device *dev)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);

	guard(rwsem_write)(&cxl_memdev_rwsem);
	cxlmd->cxlds = NULL;
}

static void cxl_memdev_unregister(void *_cxlmd)
{
	struct cxl_memdev *cxlmd = _cxlmd;
	struct device *dev = &cxlmd->dev;

	cdev_device_del(&cxlmd->cdev, dev);
	cxl_memdev_shutdown(dev);
	put_device(dev);
}

static void detach_memdev(struct work_struct *work)
{
	struct cxl_memdev *cxlmd;

	cxlmd = container_of(work, typeof(*cxlmd), detach_work);
	device_release_driver(&cxlmd->dev);
	put_device(&cxlmd->dev);
}

static struct lock_class_key cxl_memdev_key;

static struct cxl_memdev *cxl_memdev_alloc(struct cxl_dev_state *cxlds,
					   const struct file_operations *fops)
{
	struct cxl_memdev *cxlmd;
	struct device *dev;
	struct cdev *cdev;
	int rc;

	cxlmd = kzalloc(sizeof(*cxlmd), GFP_KERNEL);
	if (!cxlmd)
		return ERR_PTR(-ENOMEM);

	rc = ida_alloc_max(&cxl_memdev_ida, CXL_MEM_MAX_DEVS - 1, GFP_KERNEL);
	if (rc < 0)
		goto err;
	cxlmd->id = rc;
	cxlmd->depth = -1;

	dev = &cxlmd->dev;
	device_initialize(dev);
	lockdep_set_class(&dev->mutex, &cxl_memdev_key);
	dev->parent = cxlds->dev;
	dev->bus = &cxl_bus_type;
	dev->devt = MKDEV(cxl_mem_major, cxlmd->id);
	dev->type = &cxl_memdev_type;
	device_set_pm_not_required(dev);
	INIT_WORK(&cxlmd->detach_work, detach_memdev);

	cdev = &cxlmd->cdev;
	cdev_init(cdev, fops);
	return cxlmd;

err:
	kfree(cxlmd);
	return ERR_PTR(rc);
}

static long __cxl_memdev_ioctl(struct cxl_memdev *cxlmd, unsigned int cmd,
			       unsigned long arg)
{
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);
	struct cxl_mailbox *cxl_mbox = &mds->cxlds.cxl_mbox;

	switch (cmd) {
	case CXL_MEM_QUERY_COMMANDS:
		return cxl_query_cmd(cxl_mbox, (void __user *)arg);
	case CXL_MEM_SEND_COMMAND:
		return cxl_send_cmd(cxl_mbox, (void __user *)arg);
	default:
		return -ENOTTY;
	}
}

static long cxl_memdev_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	struct cxl_memdev *cxlmd = file->private_data;
	struct cxl_dev_state *cxlds;

	guard(rwsem_read)(&cxl_memdev_rwsem);
	cxlds = cxlmd->cxlds;
	if (cxlds && cxlds->type == CXL_DEVTYPE_CLASSMEM)
		return __cxl_memdev_ioctl(cxlmd, cmd, arg);

	return -ENXIO;
}

static int cxl_memdev_open(struct inode *inode, struct file *file)
{
	struct cxl_memdev *cxlmd =
		container_of(inode->i_cdev, typeof(*cxlmd), cdev);

	get_device(&cxlmd->dev);
	file->private_data = cxlmd;

	return 0;
}

static int cxl_memdev_release_file(struct inode *inode, struct file *file)
{
	struct cxl_memdev *cxlmd =
		container_of(inode->i_cdev, typeof(*cxlmd), cdev);

	put_device(&cxlmd->dev);

	return 0;
}

/**
 * cxl_mem_get_fw_info - Get Firmware info
 * @mds: The device data for the operation
 *
 * Retrieve firmware info for the device specified.
 *
 * Return: 0 if no error: or the result of the mailbox command.
 *
 * See CXL-3.0 8.2.9.3.1 Get FW Info
 */
static int cxl_mem_get_fw_info(struct cxl_memdev_state *mds)
{
	struct cxl_mailbox *cxl_mbox = &mds->cxlds.cxl_mbox;
	struct cxl_mbox_get_fw_info info;
	struct cxl_mbox_cmd mbox_cmd;
	int rc;

	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_GET_FW_INFO,
		.size_out = sizeof(info),
		.payload_out = &info,
	};

	rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
	if (rc < 0)
		return rc;

	mds->fw.num_slots = info.num_slots;
	mds->fw.cur_slot = FIELD_GET(CXL_FW_INFO_SLOT_INFO_CUR_MASK,
				       info.slot_info);

	return 0;
}

/**
 * cxl_mem_activate_fw - Activate Firmware
 * @mds: The device data for the operation
 * @slot: slot number to activate
 *
 * Activate firmware in a given slot for the device specified.
 *
 * Return: 0 if no error: or the result of the mailbox command.
 *
 * See CXL-3.0 8.2.9.3.3 Activate FW
 */
static int cxl_mem_activate_fw(struct cxl_memdev_state *mds, int slot)
{
	struct cxl_mailbox *cxl_mbox = &mds->cxlds.cxl_mbox;
	struct cxl_mbox_activate_fw activate;
	struct cxl_mbox_cmd mbox_cmd;

	if (slot == 0 || slot > mds->fw.num_slots)
		return -EINVAL;

	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_ACTIVATE_FW,
		.size_in = sizeof(activate),
		.payload_in = &activate,
	};

	/* Only offline activation supported for now */
	activate.action = CXL_FW_ACTIVATE_OFFLINE;
	activate.slot = slot;

	return cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
}

/**
 * cxl_mem_abort_fw_xfer - Abort an in-progress FW transfer
 * @mds: The device data for the operation
 *
 * Abort an in-progress firmware transfer for the device specified.
 *
 * Return: 0 if no error: or the result of the mailbox command.
 *
 * See CXL-3.0 8.2.9.3.2 Transfer FW
 */
static int cxl_mem_abort_fw_xfer(struct cxl_memdev_state *mds)
{
	struct cxl_mailbox *cxl_mbox = &mds->cxlds.cxl_mbox;
	struct cxl_mbox_transfer_fw *transfer;
	struct cxl_mbox_cmd mbox_cmd;
	int rc;

	transfer = kzalloc(struct_size(transfer, data, 0), GFP_KERNEL);
	if (!transfer)
		return -ENOMEM;

	/* Set a 1s poll interval and a total wait time of 30s */
	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_TRANSFER_FW,
		.size_in = sizeof(*transfer),
		.payload_in = transfer,
		.poll_interval_ms = 1000,
		.poll_count = 30,
	};

	transfer->action = CXL_FW_TRANSFER_ACTION_ABORT;

	rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
	kfree(transfer);
	return rc;
}

static void cxl_fw_cleanup(struct fw_upload *fwl)
{
	struct cxl_memdev_state *mds = fwl->dd_handle;

	mds->fw.next_slot = 0;
}

static int cxl_fw_do_cancel(struct fw_upload *fwl)
{
	struct cxl_memdev_state *mds = fwl->dd_handle;
	struct cxl_dev_state *cxlds = &mds->cxlds;
	struct cxl_memdev *cxlmd = cxlds->cxlmd;
	int rc;

	rc = cxl_mem_abort_fw_xfer(mds);
	if (rc < 0)
		dev_err(&cxlmd->dev, "Error aborting FW transfer: %d\n", rc);

	return FW_UPLOAD_ERR_CANCELED;
}

static enum fw_upload_err cxl_fw_prepare(struct fw_upload *fwl, const u8 *data,
					 u32 size)
{
	struct cxl_memdev_state *mds = fwl->dd_handle;
	struct cxl_mbox_transfer_fw *transfer;
	struct cxl_mailbox *cxl_mbox = &mds->cxlds.cxl_mbox;

	if (!size)
		return FW_UPLOAD_ERR_INVALID_SIZE;

	mds->fw.oneshot = struct_size(transfer, data, size) <
			    cxl_mbox->payload_size;

	if (cxl_mem_get_fw_info(mds))
		return FW_UPLOAD_ERR_HW_ERROR;

	/*
	 * So far no state has been changed, hence no other cleanup is
	 * necessary. Simply return the cancelled status.
	 */
	if (test_and_clear_bit(CXL_FW_CANCEL, mds->fw.state))
		return FW_UPLOAD_ERR_CANCELED;

	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err cxl_fw_write(struct fw_upload *fwl, const u8 *data,
				       u32 offset, u32 size, u32 *written)
{
	struct cxl_memdev_state *mds = fwl->dd_handle;
	struct cxl_dev_state *cxlds = &mds->cxlds;
	struct cxl_mailbox *cxl_mbox = &cxlds->cxl_mbox;
	struct cxl_memdev *cxlmd = cxlds->cxlmd;
	struct cxl_mbox_transfer_fw *transfer;
	struct cxl_mbox_cmd mbox_cmd;
	u32 cur_size, remaining;
	size_t size_in;
	int rc;

	*written = 0;

	/* Offset has to be aligned to 128B (CXL-3.0 8.2.9.3.2 Table 8-57) */
	if (!IS_ALIGNED(offset, CXL_FW_TRANSFER_ALIGNMENT)) {
		dev_err(&cxlmd->dev,
			"misaligned offset for FW transfer slice (%u)\n",
			offset);
		return FW_UPLOAD_ERR_RW_ERROR;
	}

	/*
	 * Pick transfer size based on mds->payload_size @size must bw 128-byte
	 * aligned, ->payload_size is a power of 2 starting at 256 bytes, and
	 * sizeof(*transfer) is 128.  These constraints imply that @cur_size
	 * will always be 128b aligned.
	 */
	cur_size = min_t(size_t, size, cxl_mbox->payload_size - sizeof(*transfer));

	remaining = size - cur_size;
	size_in = struct_size(transfer, data, cur_size);

	if (test_and_clear_bit(CXL_FW_CANCEL, mds->fw.state))
		return cxl_fw_do_cancel(fwl);

	/*
	 * Slot numbers are 1-indexed
	 * cur_slot is the 0-indexed next_slot (i.e. 'cur_slot - 1 + 1')
	 * Check for rollover using modulo, and 1-index it by adding 1
	 */
	mds->fw.next_slot = (mds->fw.cur_slot % mds->fw.num_slots) + 1;

	/* Do the transfer via mailbox cmd */
	transfer = kzalloc(size_in, GFP_KERNEL);
	if (!transfer)
		return FW_UPLOAD_ERR_RW_ERROR;

	transfer->offset = cpu_to_le32(offset / CXL_FW_TRANSFER_ALIGNMENT);
	memcpy(transfer->data, data + offset, cur_size);
	if (mds->fw.oneshot) {
		transfer->action = CXL_FW_TRANSFER_ACTION_FULL;
		transfer->slot = mds->fw.next_slot;
	} else {
		if (offset == 0) {
			transfer->action = CXL_FW_TRANSFER_ACTION_INITIATE;
		} else if (remaining == 0) {
			transfer->action = CXL_FW_TRANSFER_ACTION_END;
			transfer->slot = mds->fw.next_slot;
		} else {
			transfer->action = CXL_FW_TRANSFER_ACTION_CONTINUE;
		}
	}

	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_TRANSFER_FW,
		.size_in = size_in,
		.payload_in = transfer,
		.poll_interval_ms = 1000,
		.poll_count = 30,
	};

	rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
	if (rc < 0) {
		rc = FW_UPLOAD_ERR_RW_ERROR;
		goto out_free;
	}

	*written = cur_size;

	/* Activate FW if oneshot or if the last slice was written */
	if (mds->fw.oneshot || remaining == 0) {
		dev_dbg(&cxlmd->dev, "Activating firmware slot: %d\n",
			mds->fw.next_slot);
		rc = cxl_mem_activate_fw(mds, mds->fw.next_slot);
		if (rc < 0) {
			dev_err(&cxlmd->dev, "Error activating firmware: %d\n",
				rc);
			rc = FW_UPLOAD_ERR_HW_ERROR;
			goto out_free;
		}
	}

	rc = FW_UPLOAD_ERR_NONE;

out_free:
	kfree(transfer);
	return rc;
}

static enum fw_upload_err cxl_fw_poll_complete(struct fw_upload *fwl)
{
	struct cxl_memdev_state *mds = fwl->dd_handle;

	/*
	 * cxl_internal_send_cmd() handles background operations synchronously.
	 * No need to wait for completions here - any errors would've been
	 * reported and handled during the ->write() call(s).
	 * Just check if a cancel request was received, and return success.
	 */
	if (test_and_clear_bit(CXL_FW_CANCEL, mds->fw.state))
		return cxl_fw_do_cancel(fwl);

	return FW_UPLOAD_ERR_NONE;
}

static void cxl_fw_cancel(struct fw_upload *fwl)
{
	struct cxl_memdev_state *mds = fwl->dd_handle;

	set_bit(CXL_FW_CANCEL, mds->fw.state);
}

static const struct fw_upload_ops cxl_memdev_fw_ops = {
        .prepare = cxl_fw_prepare,
        .write = cxl_fw_write,
        .poll_complete = cxl_fw_poll_complete,
        .cancel = cxl_fw_cancel,
        .cleanup = cxl_fw_cleanup,
};

static void cxl_remove_fw_upload(void *fwl)
{
	firmware_upload_unregister(fwl);
}

int devm_cxl_setup_fw_upload(struct device *host, struct cxl_memdev_state *mds)
{
	struct cxl_dev_state *cxlds = &mds->cxlds;
	struct cxl_mailbox *cxl_mbox = &cxlds->cxl_mbox;
	struct device *dev = &cxlds->cxlmd->dev;
	struct fw_upload *fwl;

	if (!test_bit(CXL_MEM_COMMAND_ID_GET_FW_INFO, cxl_mbox->enabled_cmds))
		return 0;

	fwl = firmware_upload_register(THIS_MODULE, dev, dev_name(dev),
				       &cxl_memdev_fw_ops, mds);
	if (IS_ERR(fwl))
		return PTR_ERR(fwl);
	return devm_add_action_or_reset(host, cxl_remove_fw_upload, fwl);
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_setup_fw_upload, "CXL");

static const struct file_operations cxl_memdev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = cxl_memdev_ioctl,
	.open = cxl_memdev_open,
	.release = cxl_memdev_release_file,
	.compat_ioctl = compat_ptr_ioctl,
	.llseek = noop_llseek,
};

struct cxl_memdev *devm_cxl_add_memdev(struct device *host,
				       struct cxl_dev_state *cxlds)
{
	struct cxl_memdev *cxlmd;
	struct device *dev;
	struct cdev *cdev;
	int rc;

	cxlmd = cxl_memdev_alloc(cxlds, &cxl_memdev_fops);
	if (IS_ERR(cxlmd))
		return cxlmd;

	dev = &cxlmd->dev;
	rc = dev_set_name(dev, "mem%d", cxlmd->id);
	if (rc)
		goto err;

	/*
	 * Activate ioctl operations, no cxl_memdev_rwsem manipulation
	 * needed as this is ordered with cdev_add() publishing the device.
	 */
	cxlmd->cxlds = cxlds;
	cxlds->cxlmd = cxlmd;

	cdev = &cxlmd->cdev;
	rc = cdev_device_add(cdev, dev);
	if (rc)
		goto err;

	rc = devm_add_action_or_reset(host, cxl_memdev_unregister, cxlmd);
	if (rc)
		return ERR_PTR(rc);
	return cxlmd;

err:
	/*
	 * The cdev was briefly live, shutdown any ioctl operations that
	 * saw that state.
	 */
	cxl_memdev_shutdown(dev);
	put_device(dev);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_add_memdev, "CXL");

static void sanitize_teardown_notifier(void *data)
{
	struct cxl_memdev_state *mds = data;
	struct cxl_mailbox *cxl_mbox = &mds->cxlds.cxl_mbox;
	struct kernfs_node *state;

	/*
	 * Prevent new irq triggered invocations of the workqueue and
	 * flush inflight invocations.
	 */
	mutex_lock(&cxl_mbox->mbox_mutex);
	state = mds->security.sanitize_node;
	mds->security.sanitize_node = NULL;
	mutex_unlock(&cxl_mbox->mbox_mutex);

	cancel_delayed_work_sync(&mds->security.poll_dwork);
	sysfs_put(state);
}

int devm_cxl_sanitize_setup_notifier(struct device *host,
				     struct cxl_memdev *cxlmd)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	struct kernfs_node *sec;

	if (!test_bit(CXL_SEC_ENABLED_SANITIZE, mds->security.enabled_cmds))
		return 0;

	/*
	 * Note, the expectation is that @cxlmd would have failed to be
	 * created if these sysfs_get_dirent calls fail.
	 */
	sec = sysfs_get_dirent(cxlmd->dev.kobj.sd, "security");
	if (!sec)
		return -ENOENT;
	mds->security.sanitize_node = sysfs_get_dirent(sec, "state");
	sysfs_put(sec);
	if (!mds->security.sanitize_node)
		return -ENOENT;

	return devm_add_action_or_reset(host, sanitize_teardown_notifier, mds);
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_sanitize_setup_notifier, "CXL");

__init int cxl_memdev_init(void)
{
	dev_t devt;
	int rc;

	rc = alloc_chrdev_region(&devt, 0, CXL_MEM_MAX_DEVS, "cxl");
	if (rc)
		return rc;

	cxl_mem_major = MAJOR(devt);

	return 0;
}

void cxl_memdev_exit(void)
{
	unregister_chrdev_region(MKDEV(cxl_mem_major, 0), CXL_MEM_MAX_DEVS);
}

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VIRTIO_H
#define _LINUX_VIRTIO_H
/* Everything a virtio driver needs to work with any particular virtio
 * implementation. */
#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/gfp.h>
#include <linux/dma-mapping.h>
#include <linux/completion.h>
#include <linux/virtio_features.h>

/**
 * struct virtqueue - a queue to register buffers for sending or receiving.
 * @list: the chain of virtqueues for this device
 * @callback: the function to call when buffers are consumed (can be NULL).
 * @name: the name of this virtqueue (mainly for debugging)
 * @vdev: the virtio device this queue was created for.
 * @priv: a pointer for the virtqueue implementation to use.
 * @index: the zero-based ordinal number for this queue.
 * @num_free: number of elements we expect to be able to fit.
 * @num_max: the maximum number of elements supported by the device.
 * @reset: vq is in reset state or not.
 *
 * A note on @num_free: with indirect buffers, each buffer needs one
 * element in the queue, otherwise a buffer will need one element per
 * sg element.
 */
struct virtqueue {
	struct list_head list;
	void (*callback)(struct virtqueue *vq);
	const char *name;
	struct virtio_device *vdev;
	unsigned int index;
	unsigned int num_free;
	unsigned int num_max;
	bool reset;
	void *priv;
};

int virtqueue_add_outbuf(struct virtqueue *vq,
			 struct scatterlist sg[], unsigned int num,
			 void *data,
			 gfp_t gfp);

int virtqueue_add_inbuf(struct virtqueue *vq,
			struct scatterlist sg[], unsigned int num,
			void *data,
			gfp_t gfp);

int virtqueue_add_inbuf_ctx(struct virtqueue *vq,
			    struct scatterlist sg[], unsigned int num,
			    void *data,
			    void *ctx,
			    gfp_t gfp);

int virtqueue_add_inbuf_premapped(struct virtqueue *vq,
				  struct scatterlist *sg, unsigned int num,
				  void *data,
				  void *ctx,
				  gfp_t gfp);

int virtqueue_add_outbuf_premapped(struct virtqueue *vq,
				   struct scatterlist *sg, unsigned int num,
				   void *data,
				   gfp_t gfp);

int virtqueue_add_sgs(struct virtqueue *vq,
		      struct scatterlist *sgs[],
		      unsigned int out_sgs,
		      unsigned int in_sgs,
		      void *data,
		      gfp_t gfp);

struct device *virtqueue_dma_dev(struct virtqueue *vq);

bool virtqueue_kick(struct virtqueue *vq);

bool virtqueue_kick_prepare(struct virtqueue *vq);

bool virtqueue_notify(struct virtqueue *vq);

void *virtqueue_get_buf(struct virtqueue *vq, unsigned int *len);

void *virtqueue_get_buf_ctx(struct virtqueue *vq, unsigned int *len,
			    void **ctx);

void virtqueue_disable_cb(struct virtqueue *vq);

bool virtqueue_enable_cb(struct virtqueue *vq);

unsigned virtqueue_enable_cb_prepare(struct virtqueue *vq);

bool virtqueue_poll(struct virtqueue *vq, unsigned);

bool virtqueue_enable_cb_delayed(struct virtqueue *vq);

void *virtqueue_detach_unused_buf(struct virtqueue *vq);

unsigned int virtqueue_get_vring_size(const struct virtqueue *vq);

bool virtqueue_is_broken(const struct virtqueue *vq);

const struct vring *virtqueue_get_vring(const struct virtqueue *vq);
dma_addr_t virtqueue_get_desc_addr(const struct virtqueue *vq);
dma_addr_t virtqueue_get_avail_addr(const struct virtqueue *vq);
dma_addr_t virtqueue_get_used_addr(const struct virtqueue *vq);

int virtqueue_resize(struct virtqueue *vq, u32 num,
		     void (*recycle)(struct virtqueue *vq, void *buf),
		     void (*recycle_done)(struct virtqueue *vq));
int virtqueue_reset(struct virtqueue *vq,
		    void (*recycle)(struct virtqueue *vq, void *buf),
		    void (*recycle_done)(struct virtqueue *vq));

struct virtio_admin_cmd {
	__le16 opcode;
	__le16 group_type;
	__le64 group_member_id;
	struct scatterlist *data_sg;
	struct scatterlist *result_sg;
	struct completion completion;
	u32 result_sg_size;
	int ret;
};

/**
 * struct virtio_device - representation of a device using virtio
 * @index: unique position on the virtio bus
 * @failed: saved value for VIRTIO_CONFIG_S_FAILED bit (for restore)
 * @config_core_enabled: configuration change reporting enabled by core
 * @config_driver_disabled: configuration change reporting disabled by
 *                          a driver
 * @config_change_pending: configuration change reported while disabled
 * @config_lock: protects configuration change reporting
 * @vqs_list_lock: protects @vqs.
 * @dev: underlying device.
 * @id: the device type identification (used to match it with a driver).
 * @config: the configuration ops for this device.
 * @vringh_config: configuration ops for host vrings.
 * @vqs: the list of virtqueues for this device.
 * @features: the 64 lower features supported by both driver and device.
 * @features_array: the full features space supported by both driver and
 *		    device.
 * @priv: private pointer for the driver's use.
 * @debugfs_dir: debugfs directory entry.
 * @debugfs_filter_features: features to be filtered set by debugfs.
 */
struct virtio_device {
	int index;
	bool failed;
	bool config_core_enabled;
	bool config_driver_disabled;
	bool config_change_pending;
	spinlock_t config_lock;
	spinlock_t vqs_list_lock;
	struct device dev;
	struct virtio_device_id id;
	const struct virtio_config_ops *config;
	const struct vringh_config_ops *vringh_config;
	struct list_head vqs;
	VIRTIO_DECLARE_FEATURES(features);
	void *priv;
#ifdef CONFIG_VIRTIO_DEBUG
	struct dentry *debugfs_dir;
	u64 debugfs_filter_features[VIRTIO_FEATURES_DWORDS];
#endif
};

#define dev_to_virtio(_dev)	container_of_const(_dev, struct virtio_device, dev)

void virtio_add_status(struct virtio_device *dev, unsigned int status);
int register_virtio_device(struct virtio_device *dev);
void unregister_virtio_device(struct virtio_device *dev);
bool is_virtio_device(struct device *dev);

void virtio_break_device(struct virtio_device *dev);
void __virtio_unbreak_device(struct virtio_device *dev);

void __virtqueue_break(struct virtqueue *_vq);
void __virtqueue_unbreak(struct virtqueue *_vq);

void virtio_config_changed(struct virtio_device *dev);

void virtio_config_driver_disable(struct virtio_device *dev);
void virtio_config_driver_enable(struct virtio_device *dev);

#ifdef CONFIG_PM_SLEEP
int virtio_device_freeze(struct virtio_device *dev);
int virtio_device_restore(struct virtio_device *dev);
#endif
void virtio_reset_device(struct virtio_device *dev);
int virtio_device_reset_prepare(struct virtio_device *dev);
int virtio_device_reset_done(struct virtio_device *dev);

size_t virtio_max_dma_size(const struct virtio_device *vdev);

#define virtio_device_for_each_vq(vdev, vq) \
	list_for_each_entry(vq, &(vdev)->vqs, list)

/**
 * struct virtio_driver - operations for a virtio I/O driver
 * @driver: underlying device driver (populate name).
 * @id_table: the ids serviced by this driver.
 * @feature_table: an array of feature numbers supported by this driver.
 * @feature_table_size: number of entries in the feature table array.
 * @feature_table_legacy: same as feature_table but when working in legacy mode.
 * @feature_table_size_legacy: number of entries in feature table legacy array.
 * @validate: the function to call to validate features and config space.
 *            Returns 0 or -errno.
 * @probe: the function to call when a device is found.  Returns 0 or -errno.
 * @scan: optional function to call after successful probe; intended
 *    for virtio-scsi to invoke a scan.
 * @remove: the function to call when a device is removed.
 * @config_changed: optional function to call when the device configuration
 *    changes; may be called in interrupt context.
 * @freeze: optional function to call during suspend/hibernation.
 * @restore: optional function to call on resume.
 * @reset_prepare: optional function to call when a transport specific reset
 *    occurs.
 * @reset_done: optional function to call after transport specific reset
 *    operation has finished.
 * @shutdown: synchronize with the device on shutdown. If provided, replaces
 *    the virtio core implementation.
 */
struct virtio_driver {
	struct device_driver driver;
	const struct virtio_device_id *id_table;
	const unsigned int *feature_table;
	unsigned int feature_table_size;
	const unsigned int *feature_table_legacy;
	unsigned int feature_table_size_legacy;
	int (*validate)(struct virtio_device *dev);
	int (*probe)(struct virtio_device *dev);
	void (*scan)(struct virtio_device *dev);
	void (*remove)(struct virtio_device *dev);
	void (*config_changed)(struct virtio_device *dev);
	int (*freeze)(struct virtio_device *dev);
	int (*restore)(struct virtio_device *dev);
	int (*reset_prepare)(struct virtio_device *dev);
	int (*reset_done)(struct virtio_device *dev);
	void (*shutdown)(struct virtio_device *dev);
};

#define drv_to_virtio(__drv)	container_of_const(__drv, struct virtio_driver, driver)

/* use a macro to avoid include chaining to get THIS_MODULE */
#define register_virtio_driver(drv) \
	__register_virtio_driver(drv, THIS_MODULE)
int __register_virtio_driver(struct virtio_driver *drv, struct module *owner);
void unregister_virtio_driver(struct virtio_driver *drv);

/* module_virtio_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#define module_virtio_driver(__virtio_driver) \
	module_driver(__virtio_driver, register_virtio_driver, \
			unregister_virtio_driver)

dma_addr_t virtqueue_dma_map_single_attrs(struct virtqueue *_vq, void *ptr, size_t size,
					  enum dma_data_direction dir, unsigned long attrs);
void virtqueue_dma_unmap_single_attrs(struct virtqueue *_vq, dma_addr_t addr,
				      size_t size, enum dma_data_direction dir,
				      unsigned long attrs);
int virtqueue_dma_mapping_error(struct virtqueue *_vq, dma_addr_t addr);

bool virtqueue_dma_need_sync(struct virtqueue *_vq, dma_addr_t addr);
void virtqueue_dma_sync_single_range_for_cpu(struct virtqueue *_vq, dma_addr_t addr,
					     unsigned long offset, size_t size,
					     enum dma_data_direction dir);
void virtqueue_dma_sync_single_range_for_device(struct virtqueue *_vq, dma_addr_t addr,
						unsigned long offset, size_t size,
						enum dma_data_direction dir);

#ifdef CONFIG_VIRTIO_DEBUG
void virtio_debug_device_init(struct virtio_device *dev);
void virtio_debug_device_exit(struct virtio_device *dev);
void virtio_debug_device_filter_features(struct virtio_device *dev);
void virtio_debug_init(void);
void virtio_debug_exit(void);
#else
static inline void virtio_debug_device_init(struct virtio_device *dev)
{
}

static inline void virtio_debug_device_exit(struct virtio_device *dev)
{
}

static inline void virtio_debug_device_filter_features(struct virtio_device *dev)
{
}

static inline void virtio_debug_init(void)
{
}

static inline void virtio_debug_exit(void)
{
}
#endif

#endif /* _LINUX_VIRTIO_H */

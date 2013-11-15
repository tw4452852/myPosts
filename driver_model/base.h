#include <linux/notifier.h>

/**
 * struct subsys_private - structure to hold the private to the driver core portions of the bus_type/class structure.
 *
 * @subsys - the struct kset that defines this subsystem
 * @devices_kset - the subsystem's 'devices' directory
 * @interfaces - list of subsystem interfaces associated
 * @mutex - protect the devices, and interfaces lists.
 *
 * @drivers_kset - the list of drivers associated
 * @klist_devices - the klist to iterate over the @devices_kset
 * @klist_drivers - the klist to iterate over the @drivers_kset
 * @bus_notifier - the bus notifier list for anything that cares about things
 *                 on this bus.
 * @bus - pointer back to the struct bus_type that this structure is associated
 *        with.
 *
 * @glue_dirs - "glue" directory to put in-between the parent device to
 *              avoid namespace conflicts
 * @class - pointer back to the struct class that this structure is associated
 *          with.
 *
 * This structure is the one that is the actual kobject allowing struct
 * bus_type/class to be statically allocated safely.  Nothing outside of the
 * driver core should ever touch these fields.
 */
struct subsys_private {
	struct kset subsys; // 指向该子系统
	struct kset *devices_kset; // 指向子系统的设备目录
	struct list_head interfaces; // 子系统接口的列表
	struct mutex mutex; // 保护接口列表和设备列表

	struct kset *drivers_kset; // 关联的驱动的列表
	struct klist klist_devices; // 用于遍历设备列表
	struct klist klist_drivers; // 用于遍历驱动列表
	struct blocking_notifier_head bus_notifier;
	unsigned int drivers_autoprobe:1;
	struct bus_type *bus; // 指向包含该结构体的总线类型结构体

	struct kset glue_dirs;
	struct class *class; // 指向包含该结构体的设备类型结构体
};
#define to_subsys_private(obj) container_of(obj, struct subsys_private, subsys.kobj)

struct driver_private {
	struct kobject kobj; // 该驱动对应的kobject
	struct klist klist_devices; // 和该驱动绑定的设备的列表
	struct klist_node knode_bus; // 在所属的bus中位置
	struct module_kobject *mkobj; // 所属的模块
	struct device_driver *driver; // 指向包含该结构体的驱动结构体
};
#define to_driver(obj) container_of(obj, struct driver_private, kobj)

/**
 * struct device_private - structure to hold the private to the driver core portions of the device structure.
 *
 * @klist_children - klist containing all children of this device
 * @knode_parent - node in sibling list
 * @knode_driver - node in driver list
 * @knode_bus - node in bus list
 * @deferred_probe - entry in deferred_probe_list which is used to retry the
 *	binding of drivers which were unable to get all the resources needed by
 *	the device; typically because it depends on another driver getting
 *	probed first.
 * @driver_data - private pointer for driver specific info.  Will turn into a
 * list soon.
 * @device - pointer back to the struct class that this structure is
 * associated with.
 *
 * Nothing outside of the driver core should ever touch these fields.
 */
struct device_private {
	struct klist klist_children; // 该设备的孩子列表
	struct klist_node knode_parent; // 在父节点的孩子列表中的位置
	struct klist_node knode_driver; // 在关联的驱动的设备列表中的位置
	struct klist_node knode_bus; // 在其所属的总线的设备列表中的位置
	struct list_head deferred_probe; // 在延迟探测列表中的位置,主要是因为有些资源暂时还无法获得,必须等待其他驱动的初始化
	void *driver_data; // 驱动相关的信息
	struct device *device; // 指向所属的class
};
#define to_device_private_parent(obj)	\
	container_of(obj, struct device_private, knode_parent)
#define to_device_private_driver(obj)	\
	container_of(obj, struct device_private, knode_driver)
#define to_device_private_bus(obj)	\
	container_of(obj, struct device_private, knode_bus)

extern int device_private_init(struct device *dev);

/* initialisation functions */
extern int devices_init(void);
extern int buses_init(void);
extern int classes_init(void);
extern int firmware_init(void);
#ifdef CONFIG_SYS_HYPERVISOR
extern int hypervisor_init(void);
#else
static inline int hypervisor_init(void) { return 0; }
#endif
extern int platform_bus_init(void);
extern void cpu_dev_init(void);

extern int bus_add_device(struct device *dev);
extern void bus_probe_device(struct device *dev);
extern void bus_remove_device(struct device *dev);

extern int bus_add_driver(struct device_driver *drv);
extern void bus_remove_driver(struct device_driver *drv);

extern void driver_detach(struct device_driver *drv);
extern int driver_probe_device(struct device_driver *drv, struct device *dev);
extern void driver_deferred_probe_del(struct device *dev);
static inline int driver_match_device(struct device_driver *drv,
				      struct device *dev)
{
	return drv->bus->match ? drv->bus->match(dev, drv) : 1;
}

extern char *make_class_name(const char *name, struct kobject *kobj);

extern int devres_release_all(struct device *dev);

/* /sys/devices directory */
extern struct kset *devices_kset;

#if defined(CONFIG_MODULES) && defined(CONFIG_SYSFS)
extern void module_add_driver(struct module *mod, struct device_driver *drv);
extern void module_remove_driver(struct device_driver *drv);
#else
static inline void module_add_driver(struct module *mod,
				     struct device_driver *drv) { }
static inline void module_remove_driver(struct device_driver *drv) { }
#endif

#ifdef CONFIG_DEVTMPFS
extern int devtmpfs_init(void);
#else
static inline int devtmpfs_init(void) { return 0; }
#endif

/* JAKernel - Device and Driver Framework Core */

#ifndef JAKERNEL_KERNEL_DEVICE_H
#define JAKERNEL_KERNEL_DEVICE_H

#include <stdint.h>

/* Forward declarations */
struct device;
struct driver;

/* Device types - categorized by bus/connection type */
enum device_type {
    DEVICE_TYPE_UNKNOWN,
    DEVICE_TYPE_PLATFORM,     /* Platform devices (memory-mapped, fixed) */
    DEVICE_TYPE_ISA,          /* ISA bus devices */
    DEVICE_TYPE_PCI,          /* PCI bus devices */
    DEVICE_TYPE_SERIAL,       /* Serial port devices */
    DEVICE_TYPE_CONSOLE,      /* Console devices */
};

/* Device states */
enum device_state {
    DEVICE_STATE_UNINITIALIZED,   /* Device registered but not probed */
    DEVICE_STATE_PROBED,          /* Driver matched, probe completed */
    DEVICE_STATE_INITIALIZED,     /* Device fully initialized */
    DEVICE_STATE_FAILED,          /* Initialization failed */
};

/* Device structure - represents a physical or virtual device */
struct device {
    const char *name;                 /* Device name */
    enum device_type type;            /* Device type */
    enum device_state state;          /* Current state */
    uint16_t match_id;                /* ID for matching with driver */
    uint8_t init_priority;            /* Initialization order (0=first) */

    /* Hardware resources */
    void *mmio_base;                  /* Memory-mapped I/O base */
    uint32_t mmio_size;               /* Memory-mapped I/O size */
    uint16_t io_port_base;            /* I/O port base */
    uint16_t io_port_count;           /* Number of I/O ports */

    /* Driver association (set during probe) */
    struct driver *bound_driver;      /* Driver that matched this device */

    /* Private driver data */
    void *driver_data;

    /* Linked list for device registry */
    struct device *next;
};

/* Driver structure - represents a device driver */
struct driver {
    const char *name;                 /* Driver name */
    uint16_t match_id;                /* ID for matching with device */
    uint16_t match_mask;              /* Mask for ID matching */

    /* Driver callbacks */
    int (*probe)(struct device *dev);     /* Check if driver can handle device */
    int (*init)(struct device *dev);      /* Initialize the device */
    void (*remove)(struct device *dev);   /* Remove driver from device */

    /* Linked list for driver registry */
    struct driver *next;
};

/* Device Manager API */

/* Register a driver to the system */
int driver_register(struct driver *drv);

/* Unregister a driver from the system */
void driver_unregister(struct driver *drv);

/* Register a device to the system */
int device_register(struct device *dev);

/* Unregister a device from the system */
void device_unregister(struct device *dev);

/* Probe and match devices with registered drivers */
int device_probe_all(void);

/* Initialize all devices in priority order */
int device_init_all(void);

/* Get device by name */
struct device *device_get(const char *name);

/* Set driver private data for a device */
void device_set_drvdata(struct device *dev, void *data);

/* Get driver private data for a device */
void *device_get_drvdata(struct device *dev);

#endif /* JAKERNEL_KERNEL_DEVICE_H */

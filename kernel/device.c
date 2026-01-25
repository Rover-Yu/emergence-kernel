/* JAKernel - Device and Driver Framework Implementation */

#include <stddef.h>
#include "kernel/device.h"

/* Maximum number of devices in the system */
#define MAX_DEVICES    32

/* Global driver registry - singly linked list */
static struct driver *driver_list = NULL;

/* Global device registry - singly linked list */
static struct device *device_list = NULL;

/* Number of registered devices */
static int device_count = 0;

/* ============================================================================
 * Driver Management
 * ============================================================================ */

/**
 * driver_register - Register a driver to the system
 * @drv: Driver to register
 *
 * Returns: 0 on success, negative value on error
 */
int driver_register(struct driver *drv) {
    if (!drv || !drv->name) {
        return -1;  /* Invalid driver */
    }

    /* Add to the front of the driver list */
    drv->next = driver_list;
    driver_list = drv;

    return 0;
}


/* ============================================================================
 * Device Management
 * ============================================================================ */

/**
 * device_register - Register a device to the system
 * @dev: Device to register
 *
 * Returns: 0 on success, negative value on error
 */
int device_register(struct device *dev) {
    if (!dev || !dev->name) {
        return -1;  /* Invalid device */
    }

    if (device_count >= MAX_DEVICES) {
        return -2;  /* Too many devices */
    }

    /* Initialize device state */
    dev->state = DEVICE_STATE_UNINITIALIZED;
    dev->driver_data = NULL;
    dev->bound_driver = NULL;

    /* Add to the front of the device list */
    dev->next = device_list;
    device_list = dev;
    device_count++;

    return 0;
}


/* ============================================================================
 * Device Getters/Setters
 * ============================================================================ */




/* ============================================================================
 * Device Matching and Probing
 * ============================================================================ */

/**
 * match_device_driver - Check if a driver can handle a device
 * @drv: Driver to check
 * @dev: Device to check
 *
 * Returns: 1 if match, 0 if no match
 */
static int match_device_driver(struct driver *drv, struct device *dev) {
    /* Check ID match using mask */
    uint16_t dev_id = dev->match_id;
    uint16_t drv_id = drv->match_id;
    uint16_t mask = drv->match_mask;

    if ((dev_id & mask) == (drv_id & mask)) {
        /* ID matches - let driver's probe function decide */
        if (drv->probe) {
            return drv->probe(dev) == 0;
        }
        return 1;  /* No probe function, assume match */
    }

    return 0;
}

/**
 * device_probe_all - Probe all devices and match with drivers
 *
 * This function iterates through all registered devices and tries to find
 * a matching driver for each one. When a match is found, the driver is
 * bound to the device.
 *
 * Returns: Number of successfully probed devices, negative on error
 */
int device_probe_all(void) {
    struct device *dev;
    int matched_count = 0;

    /* Match devices with drivers */
    dev = device_list;
    while (dev) {
        struct driver *drv = driver_list;

        while (drv) {
            if (match_device_driver(drv, dev)) {
                /* Found a matching driver - bind it to the device */
                dev->bound_driver = drv;
                dev->state = DEVICE_STATE_PROBED;
                matched_count++;
                break;
            }
            drv = drv->next;
        }
        dev = dev->next;
    }

    return matched_count;
}

/* ============================================================================
 * Device Initialization
 * ============================================================================ */

/**
 * device_init_all - Initialize all probed devices in priority order
 *
 * Returns: Number of successfully initialized devices
 */
int device_init_all(void) {
    struct device *devices_to_init[MAX_DEVICES];
    int count = 0;
    int success_count = 0;
    int i;

    /* Phase 1: Collect all probed devices with their bound drivers */
    struct device *dev = device_list;
    while (dev) {
        if (dev->state == DEVICE_STATE_PROBED && dev->bound_driver) {
            devices_to_init[count] = dev;
            count++;
        }
        dev = dev->next;
    }

    /* Phase 2: Sort by priority (bubble sort) */
    for (i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (devices_to_init[j]->init_priority > devices_to_init[j + 1]->init_priority) {
                /* Swap */
                struct device *temp = devices_to_init[j];
                devices_to_init[j] = devices_to_init[j + 1];
                devices_to_init[j + 1] = temp;
            }
        }
    }

    /* Phase 3: Initialize in order */
    for (i = 0; i < count; i++) {
        struct device *d = devices_to_init[i];
        struct driver *drv = d->bound_driver;
        int ret;

        if (drv->init) {
            ret = drv->init(d);
            if (ret == 0) {
                d->state = DEVICE_STATE_INITIALIZED;
                success_count++;
            } else {
                d->state = DEVICE_STATE_FAILED;
            }
        } else {
            /* No init function, mark as initialized */
            d->state = DEVICE_STATE_INITIALIZED;
            success_count++;
        }
    }

    return success_count;
}

/* Emergence Kernel - x86-64 Serial Port Driver (using device framework) */

#include <stdint.h>
#include <stddef.h>
#include "kernel/device.h"
#include "arch/x86_64/io.h"

/* Device type IDs for matching */
#define DEVICE_TYPE_SERIAL_ID    0x0001  /* Serial port device type */

/* Serial port registers offsets */
#define SERIAL_REG_DATA       0   /* Data register (read/write) */
#define SERIAL_REG_INT_EN     1   /* Interrupt enable register */
#define SERIAL_REG_FIFO_CTRL  2   /* FIFO control register */
#define SERIAL_REG_LINE_CTRL  3   /* Line control register */
#define SERIAL_REG_MODEM_CTRL 4   /* Modem control register */
#define SERIAL_REG_LINE_STAT  5   /* Line status register */
#define SERIAL_REG_MODEM_STAT 6   /* Modem status register */

/* Line status register bits */
#define SERIAL_LSR_THRE       0x20  /* Transmit-hold-register empty */

/* Driver match configuration */
#define SERIAL_DRIVER_ID      DEVICE_TYPE_SERIAL_ID  /* Match serial devices */
#define SERIAL_DRIVER_MASK    0xFFFF                  /* Exact match required */

/* Private data for serial device */
struct serial_data {
    uint16_t base_port;
    int initialized;
};

/* Simple spinlock for serial output */
static volatile int serial_lock = 0;

static void serial_lock_acquire(void) {
    while (__sync_lock_test_and_set(&serial_lock, 1)) {
        asm volatile("pause");
    }
}

static void serial_lock_release(void) {
    __sync_lock_release(&serial_lock);
}

/* ============================================================================
 * Driver Operations
 * ============================================================================ */

/**
 * serial_probe - Check if this driver can handle the device
 * @dev: Device to probe
 *
 * Returns: 0 if driver can handle device, negative otherwise
 */
static int serial_probe(struct device *dev) {
    (void)dev;  /* Device already validated by match_id */
    return 0;
}

/**
 * serial_init_device - Initialize the serial port device
 * @dev: Device to initialize
 *
 * Returns: 0 on success, negative on error
 */
static int serial_init_device(struct device *dev) {
    struct serial_data *data;
    uint16_t base;

    /* Get private data */
    data = (struct serial_data *)dev->driver_data;
    if (!data) {
        return -1;
    }

    base = data->base_port;

    /* Initialize serial port to 115200 baud, 8N1 */
    outb(base + SERIAL_REG_INT_EN, 0x00);     /* Disable interrupts */
    outb(base + SERIAL_REG_LINE_CTRL, 0x80);  /* Enable DLAB */
    outb(base + SERIAL_REG_DATA, 0x01);       /* Divisor low byte (115200 baud) */
    outb(base + SERIAL_REG_INT_EN, 0x00);     /* Divisor high byte */
    outb(base + SERIAL_REG_LINE_CTRL, 0x03);  /* 8N1, disable DLAB */
    outb(base + SERIAL_REG_FIFO_CTRL, 0xC7);  /* Enable FIFO, 14-byte threshold */
    outb(base + SERIAL_REG_MODEM_CTRL, 0x0B); /* Enable IRQs, set RTS/DSR */

    data->initialized = 1;

    return 0;
}

/* Driver structure */
static struct driver serial_driver = {
    .name = "serial",
    .match_id = SERIAL_DRIVER_ID,
    .match_mask = SERIAL_DRIVER_MASK,
    .probe = serial_probe,
    .init = serial_init_device,
};

/* ============================================================================
 * Device Registration
 * ============================================================================ */

/* Private data for each serial port */
static struct serial_data com1_data;

/* Device structure for COM1 */
static struct device com1_device = {
    .name = "serial-com1",
    .type = DEVICE_TYPE_SERIAL,
    .match_id = DEVICE_TYPE_SERIAL_ID,  /* Use device type for matching */
    .init_priority = 10,  /* Initialize early (low number = early) */

    /* COM1 I/O port range */
    .io_port_base = 0x3F8,
    .io_port_count = 8,

    /* Private data */
    .driver_data = &com1_data,

    .next = NULL,
};

/* Initialize COM1 private data */
static void com1_data_init(void) {
    com1_data.base_port = 0x3F8;
    com1_data.initialized = 0;
}

/**
 * serial_driver_init - Register serial driver and devices
 *
 * This function should be called during kernel initialization
 * to register the serial port driver and devices with the
 * device manager.
 *
 * Returns: 0 on success, negative on error
 */
int serial_driver_init(void) {
    int ret;

    /* Initialize private data */
    com1_data_init();

    /* Register the driver */
    ret = driver_register(&serial_driver);
    if (ret != 0) {
        return ret;
    }

    /* Register COM1 device */
    ret = device_register(&com1_device);
    if (ret != 0) {
        return ret;
    }

    return 0;
}

/* ============================================================================
 * Public API (wrapper functions)
 * ============================================================================ */

/**
 * serial_putc_unlocked - Write a character to COM1 (no locking)
 * @c: Character to write
 *
 * Internal function - assumes caller holds the lock
 */
static void serial_putc_unlocked(char c) {
    struct serial_data *data = (struct serial_data *)com1_device.driver_data;
    uint16_t base;

    /* Fallback: if device not initialized, try direct I/O */
    if (!data) {
        base = 0x3F8;
    } else {
        base = data->base_port;
    }

    /* Wait for transmit hold register to be empty */
    while ((inb(base + SERIAL_REG_LINE_STAT) & SERIAL_LSR_THRE) == 0)
        ;

    outb(base + SERIAL_REG_DATA, c);
}

/**
 * serial_putc - Write a character to COM1
 * @c: Character to write
 */
void serial_putc(char c) {
    serial_lock_acquire();
    serial_putc_unlocked(c);
    serial_lock_release();
}

/**
 * serial_puts - Write a string to COM1
 * @str: Null-terminated string to write
 */
void serial_puts(const char *str) {
    serial_lock_acquire();
    while (*str) {
        serial_putc_unlocked(*str);
        str++;
    }
    serial_lock_release();
}

/**
 * serial_unlock - Release the serial spinlock (for SMP handoff)
 *
 * This function is used during AP startup to release the serial lock
 * before starting APs, preventing deadlock when AP tries to use serial.
 */
void serial_unlock(void) {
    serial_lock_release();
}

/**
 * serial_put_hex - Write a hexadecimal value to COM1
 * @value: 64-bit value to write in hex
 */
void serial_put_hex(uint64_t value) {
    const char hex_chars[] = "0123456789ABCDEF";
    char buf[17];
    int i;

    if (value == 0) {
        serial_puts("0");
        return;
    }

    for (i = 15; i >= 0; i--) {
        buf[i] = hex_chars[value & 0xF];
        value >>= 4;
        if (value == 0) break;
    }

    while (i < 15 && buf[i] == '0') i++;

    while (i <= 15) {
        serial_putc(buf[i++]);
    }
}

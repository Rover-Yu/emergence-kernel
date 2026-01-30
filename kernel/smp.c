/* Emergence Kernel - SMP implementation */

#include <stddef.h>
#include <stdint.h>
#include "kernel/smp.h"
#include "include/spinlock.h"
#include "include/atomic.h"
#include "include/barrier.h"
#include "arch/x86_64/apic.h"

/* External ACPI functions for getting APIC information */
extern int acpi_get_apic_count(void);
extern uint8_t acpi_get_apic_id_by_index(int index);

/* External serial output functions */
extern void serial_puts(const char *str);
extern void serial_putc(char c);

/* External monitor functions */
extern uint64_t monitor_get_unpriv_cr3(void);
extern void monitor_verify_invariants(void);

/* Stack area for AP CPUs (aligned to 16 bytes) */
static uint8_t ap_stacks[SMP_MAX_CPUS][CPU_STACK_SIZE] __attribute__((aligned(16)));

/* Per-CPU information */
static smp_cpu_info_t cpu_info[SMP_MAX_CPUS];

/* Lock for ready_cpus counter - prevents race conditions during AP startup */
static spinlock_t ready_cpus_lock = SPIN_LOCK_UNLOCKED;

/* Number of CPUs that have completed initialization */
static volatile int ready_cpus = 0;

/* Actual number of CPUs in the system (from ACPI or runtime detection) */
static int actual_cpu_count = 0;

/* CPU ID assignment - atomically incremented by each CPU */
static volatile atomic_int next_cpu_id = 0;

/* BSP initialization complete flag - use the same variable as boot.S */
extern volatile int bsp_init_done;
#define bsp_init_complete bsp_init_done  /* Alias for compatibility */

/* Current CPU index (for each CPU) */
static volatile int current_cpu_index = 0;

/* External symbols */
extern void ap_start(void);

/**
 * smp_get_apic_id - Get current CPU's APIC ID
 *
 * Returns: Local APIC ID from CPU info
 */
uint8_t smp_get_apic_id(void) {
    int idx = smp_get_cpu_index();
    if (idx >= 0 && idx < SMP_MAX_CPUS) {
        return cpu_info[idx].apic_id;
    }
    return 0;
}

/**
 * smp_get_cpu_index - Get current CPU's index
 *
 * Returns: CPU index (0-3)
 *
 * NOTE: Returns the global current_cpu_index variable.
 * This works correctly because the BSP always returns 0, and APs
 * call this before setting current_cpu_index to their own value.
 */
int smp_get_cpu_index(void) {
    return current_cpu_index;
}

/**
 * smp_get_apic_id_by_index - Get APIC ID by CPU index
 * @cpu_index: CPU index
 *
 * Returns: APIC ID for the given CPU index
 */
uint8_t smp_get_apic_id_by_index(int cpu_index) {
    if (cpu_index >= 0 && cpu_index < SMP_MAX_CPUS) {
        return cpu_info[cpu_index].apic_id;
    }
    return 0;
}

/**
 * smp_get_cpu_count - Get the actual number of CPUs in the system
 *
 * Returns: Number of CPUs detected (1 to SMP_MAX_CPUS)
 */
int smp_get_cpu_count(void) {
    return actual_cpu_count > 0 ? actual_cpu_count : 1;
}

/**
 * smp_mark_cpu_ready - Mark CPU as ready
 * @cpu_index: CPU index
 */
void smp_mark_cpu_ready(int cpu_index) {
    irq_flags_t flags;

    if (cpu_index >= 0 && cpu_index < SMP_MAX_CPUS) {
        cpu_info[cpu_index].state = CPU_READY;

        /* Use interrupt-safe lock to prevent deadlock in IPI handler */
        spin_lock_irqsave(&ready_cpus_lock, &flags);
        ready_cpus++;
        spin_unlock_irqrestore(&ready_cpus_lock, &flags);
    }
}

/**
 * smp_init - Initialize SMP subsystem
 */
void smp_init(void) {
    /* BSP is CPU 0 */
    current_cpu_index = 0;
    next_cpu_id = 1;

    ready_cpus = 0;

    /* Get actual CPU count from ACPI, fallback to 1 (BSP only) */
    actual_cpu_count = acpi_get_apic_count();
    if (actual_cpu_count <= 0 || actual_cpu_count > SMP_MAX_CPUS) {
        actual_cpu_count = 1;  /* BSP only when ACPI fails */
    }

    /* Initialize CPU info with real APIC IDs from ACPI */
    for (int i = 0; i < SMP_MAX_CPUS; i++) {
        cpu_info[i].cpu_index = i;
        cpu_info[i].apic_id = acpi_get_apic_id_by_index(i);
        cpu_info[i].state = (i == 0) ? CPU_ONLINE : CPU_OFFLINE;
        cpu_info[i].stack_top = NULL;
    }

    /* Fallback: if ACPI didn't provide APIC IDs, use CPU indices */
    if (cpu_info[0].apic_id == 0 && acpi_get_apic_count() == 0) {
        for (int i = 0; i < SMP_MAX_CPUS; i++) {
            cpu_info[i].apic_id = i;
        }
    }
}

/**
 * smp_start_all_aps - Start all Application Processors
 *
 * Sends real STARTUP IPIs to all APs via the Local APIC.
 * The AP trampoline is at physical address 0x7000.
 */
void smp_start_all_aps(void) {
    extern int ap_startup(uint8_t apic_id, uint32_t startup_addr);
#if CONFIG_SMP_AP_DEBUG
    extern void serial_puts(const char *str);
    extern void serial_putc(char c);
#endif

    /* AP trampoline is at 0x7000 (page 7)
     * Page number for STARTUP IPI = 0x7000 >> 12 = 7 */
    const uint32_t TRAMPOLINE_PAGE = 7;

#if CONFIG_SMP_AP_DEBUG
    serial_puts("SMP: Starting all Application Processors...\n");
#endif

    /* Disable interrupts during AP startup to avoid interference */
    asm volatile ("cli");

    /* Signal APs that BSP initialization is complete BEFORE starting APs
     * This prevents deadlock where:
     * - AP waits for bsp_init_complete before proceeding
     * - BSP waits for AP to become ready before continuing
     * Setting this flag first allows APs to proceed without blocking */
    bsp_init_complete = 1;

    /* Start each AP sequentially using STARTUP IPI
     * Sequential startup prevents multiple APs from executing the trampoline
     * simultaneously, which causes serial output corruption and system instability. */
    for (int i = 1; i < actual_cpu_count; i++) {
        uint8_t apic_id = smp_get_apic_id_by_index(i);

        /* Mark AP as booting - tracks AP initialization state */
        cpu_info[i].state = CPU_BOOTING;

        /* Send STARTUP IPI to AP at physical address TRAMPOLINE_PAGE (0x7000)
         * The AP will begin executing at the trampoline entry point */
        int ret = ap_startup(apic_id, TRAMPOLINE_PAGE);

        if (ret < 0) {
#if CONFIG_SMP_AP_DEBUG
            serial_puts("SMP: AP ");
            serial_putc('0' + i);
            serial_puts(" startup FAILED!\n");
#endif
            cpu_info[i].state = CPU_OFFLINE;
            continue;  /* Skip failed AP and try the next one */
        }

        /* Wait for this AP to complete initialization before starting the next one
         * This prevents multiple APs from:
         * - Competing for serial output (causes garbled debug text)
         * - Simultaneously accessing shared trampoline code/data
         * - Racing during CPU index assignment */
        int timeout = SMP_AP_INIT_TIMEOUT;
        while (cpu_info[i].state != CPU_READY && timeout > 0) {
            asm volatile ("pause");  /* Reduce power consumption during busy-wait */
            timeout--;
        }

        /* Small delay to let the AP fully halt and settle before starting the next
         * This ensures the previous AP has completed all initialization and is
         * in a stable halt state before the next AP begins booting */
        for (volatile int j = 0; j < SMP_AP_SETTLE_DELAY; j++) {
            asm volatile ("pause");
        }
    }

    /* All APs started - report status */
    int ap_ready_count = 0;
    int expected_aps = actual_cpu_count - 1;  /* BSP is not an AP */
    for (int i = 1; i < actual_cpu_count; i++) {
        if (cpu_info[i].state == CPU_READY) {
            ap_ready_count++;
        }
    }

    serial_puts("SMP: All APs startup complete. ");
    serial_putc('0' + ap_ready_count);
    serial_puts("/");
    serial_putc('0' + expected_aps);
    serial_puts(" APs ready\n");

    /* BSP will halt after returning to main.c */
}

/**
 * ap_start - Application Processor (AP) entry point
 *
 * This function is called by the AP trampoline after the AP completes
 * its mode transition (Real Mode → Protected Mode → Long Mode).
 * The AP performs initialization, gets its CPU index, sets up its stack,
 * and then halts.
 *
 * Context: Called with interrupts disabled, running on dedicated AP stack
 */
void ap_start(void) {
    /* Atomically allocate CPU index using fetch-and-add
     * Each AP gets a unique index: 1, 2, 3, ... (BSP is always 0)
     * Atomic operation prevents race conditions when multiple APs boot */
    int my_index = atomic_fetch_add(&next_cpu_id, 1);

    if (my_index <= 0 || my_index >= SMP_MAX_CPUS) {
        serial_puts("[AP] ERROR: Invalid CPU index!\n");
        while (1) { asm volatile ("hlt"); }
    }

    /* Set current CPU index */
    current_cpu_index = my_index;

    /* Set up stack */
    cpu_info[my_index].stack_top = &ap_stacks[my_index][CPU_STACK_SIZE];
    asm volatile ("mov %0, %%rsp" : : "r"(cpu_info[my_index].stack_top));

    /* Switch to unprivileged page tables */
    uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
    if (unpriv_cr3 != 0) {
#if CONFIG_CR0_WP_CONTROL
        /* Enable write protection enforcement for AP */
        /* Set CR0.WP=1 so outer kernel cannot modify read-only PTEs */
        uint64_t cr0;
        asm volatile ("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= (1 << 16);  /* Set CR0.WP bit */
        asm volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
#endif

        asm volatile ("mov %0, %%cr3" : : "r"(unpriv_cr3));
        serial_puts("[AP] CPU");
        serial_putc('0' + my_index);
        serial_puts(" switched to unprivileged mode\n");

        /* Verify Nested Kernel invariants on AP as well */
        monitor_verify_invariants();
    }

    cpu_info[my_index].state = CPU_ONLINE;

    /* Mark CPU as ready FIRST - BSP is waiting for this */
    smp_mark_cpu_ready(my_index);

    /* Small delay to ensure BSP sees we're ready */
    for (volatile int i = 0; i < 1000; i++) {
        asm volatile("pause");
    }

#if CONFIG_SPINLOCK_TESTS
    /* Poll for spin lock test mode - BSP will set this flag */
    extern volatile int spinlock_test_start;
    while (!spinlock_test_start) {
        asm volatile("pause");
    }

    /* Enter test mode - participate in SMP tests */
    extern void spinlock_test_ap_entry(void);
    spinlock_test_ap_entry();
#endif

    /* Halt */
    while (1) { asm volatile ("hlt"); }
}

/* patch_ap_trampoline removed - trampoline now works without runtime patching
 * The trampoline is copied to 0x7000 during BSP boot in boot.S using rep movsb.
 * All addresses are resolved at link time using external symbols. */

/* Emergence Kernel - SMP implementation */

#include <stddef.h>
#include <stdint.h>
#include "arch/x86_64/smp.h"
#include "include/spinlock.h"
#include "include/atomic.h"
#include "include/barrier.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/cr.h"
#include "arch/x86_64/idt.h"
#include "kernel/klog.h"

/* Test wrapper headers */
#include "tests/testcases.h"

/* External ACPI functions for getting APIC information */
extern int acpi_get_apic_count(void);
extern uint8_t acpi_get_apic_id_by_index(int index);

/* External monitor functions */
extern uint64_t monitor_get_unpriv_cr3(void);
extern void monitor_verify_invariants(void);

/* Stack area for outer kernel CPUs (aligned to 16 bytes) */
uint8_t ok_cpu_stacks[SMP_MAX_CPUS][CPU_STACK_SIZE] __attribute__((aligned(16)));

/* Per-CPU information */
static smp_cpu_info_t cpu_info[SMP_MAX_CPUS];

/* Per-CPU data for monitor trampoline (GS-base indexed) */
per_cpu_data_t per_cpu_data[SMP_MAX_CPUS];

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
 * smp_get_irq_nest_depth_ptr - Get pointer to current CPU's interrupt nesting depth
 *
 * Returns: Pointer to irq_nest_depth for current CPU
 */
int* smp_get_irq_nest_depth_ptr(void) {
    int idx = smp_get_cpu_index();
    if (idx >= 0 && idx < SMP_MAX_CPUS) {
        return &cpu_info[idx].irq_nest_depth;
    }
    return NULL;
}

/**
 * smp_mark_cpu_ready - Mark CPU as ready
 * @cpu_index: CPU index
 */
void smp_mark_cpu_ready(int cpu_index) {
    if (cpu_index >= 0 && cpu_index < SMP_MAX_CPUS) {
        cpu_info[cpu_index].state = CPU_READY;

        /* Use interrupt-safe lock to prevent deadlock in IPI handler */
        irq_flags_t flags = spin_lock_irqsave(&ready_cpus_lock);
        ready_cpus++;
        spin_unlock_irqrestore(&ready_cpus_lock, flags);
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
        cpu_info[i].irq_nest_depth = 0;
    }

    /* Fallback: if ACPI didn't provide APIC IDs, use CPU indices */
    if (cpu_info[0].apic_id == 0 && acpi_get_apic_count() == 0) {
        for (int i = 0; i < SMP_MAX_CPUS; i++) {
            cpu_info[i].apic_id = i;
            cpu_info[i].irq_nest_depth = 0;
        }
    }

    /* Initialize per-CPU data for monitor trampoline */
    for (int i = 0; i < SMP_MAX_CPUS; i++) {
        per_cpu_data[i].saved_rsp = 0;
        per_cpu_data[i].saved_cr3 = 0;
        per_cpu_data[i].cpu_index = i;
        per_cpu_data[i].saved_rax = 0;
        per_cpu_data[i].saved_rdx = 0;
    }
}

/**
 * smp_set_gs_base - Set GS segment base to per-CPU data
 * @cpu_data: Pointer to this CPU's per_cpu_data
 *
 * Uses WRMSR to set IA32_GS_BASE MSR (0xC0000101).
 * This allows assembly code to access per_cpu_data via %gs:offset.
 */
void smp_set_gs_base(per_cpu_data_t *cpu_data) {
    uint64_t addr = (uint64_t)cpu_data;
    uint32_t low = (uint32_t)(addr & 0xFFFFFFFF);
    uint32_t high = (uint32_t)(addr >> 32);

    asm volatile ("wrmsr" :
        : "c"(0xC0000101),  /* IA32_GS_BASE MSR */
          "a"(low),
          "d"(high));
}

/**
 * smp_get_per_cpu_data - Get current CPU's per_cpu_data
 *
 * Returns: Pointer to current CPU's per_cpu_data structure
 */
per_cpu_data_t *smp_get_per_cpu_data(void) {
    int idx = smp_get_cpu_index();
    if (idx >= 0 && idx < SMP_MAX_CPUS) {
        return &per_cpu_data[idx];
    }
    return &per_cpu_data[0];  /* Fallback to BSP */
}

/**
 * smp_start_all_aps - Start all Application Processors
 *
 * Sends real STARTUP IPIs to all APs via the Local APIC.
 * The AP trampoline is at physical address 0x7000.
 */
void smp_start_all_aps(void) {
    extern int ap_startup(uint8_t apic_id, uint32_t startup_addr);

    /* AP trampoline is at 0x7000 (page 7)
     * Page number for STARTUP IPI = 0x7000 >> 12 = 7 */
    const uint32_t TRAMPOLINE_PAGE = 7;

    klog_info("SMP", "Starting all Application Processors...");

    /* Disable interrupts during AP startup to avoid interference */
    disable_interrupts();

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
            klog_error("SMP", "AP %d startup FAILED!", i);
            cpu_info[i].state = CPU_OFFLINE;
            continue;  /* Skip failed AP and try the next one */
        }

        /* Wait for this AP to complete initialization before starting the next one
         * This prevents multiple APs from:
         * - Competing for serial output (causes garbled debug text)
         * - Simultaneously accessing shared trampoline code/data
         * - Racing during CPU index assignment */
        int timeout = SMP_AP_INIT_TIMEOUT;
        while (timeout > 0) {
            /* Use acquire memory barrier to see latest state from other CPU */
            smp_mb();
            if (cpu_info[i].state == CPU_READY) {
                break;
            }
            cpu_relax();  /* Reduce power consumption during busy-wait */
            timeout--;
        }

        /* Small delay to let the AP fully halt and settle before starting the next
         * This ensures the previous AP has completed all initialization and is
         * in a stable halt state before the next AP begins booting */
        for (volatile int j = 0; j < SMP_AP_SETTLE_DELAY; j++) {
            cpu_relax();
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

    klog_info("SMP", "All APs startup complete. %d/%d APs ready", ap_ready_count, expected_aps);

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
        klog_error("SMP", "[AP] ERROR: Invalid CPU index!");
        while (1) { arch_halt(); }
    }

    /* Set current CPU index with memory barrier to ensure visibility */
    current_cpu_index = my_index;
    smp_mb();  /* Ensure write is visible before continuing */

    /* Set up stack */
    cpu_info[my_index].stack_top = &ok_cpu_stacks[my_index][CPU_STACK_SIZE];
    asm volatile ("mov %0, %%rsp" : : "r"(cpu_info[my_index].stack_top));

    /* Initialize interrupt nesting depth */
    cpu_info[my_index].irq_nest_depth = 0;

    /* Set GS base to point to this CPU's per_cpu_data
     * This enables the monitor trampoline to use GS-relative addressing */
    smp_set_gs_base(&per_cpu_data[my_index]);

    /* Switch to unprivileged page tables */
    uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
    if (unpriv_cr3 != 0) {
        /* Enable write protection enforcement for AP */
        /* Set CR0.WP=1 so outer kernel cannot modify read-only PTEs */
        uint64_t cr0 = arch_cr0_read();
        cr0 |= (1 << 16);  /* Set CR0.WP bit */
        arch_cr0_write(cr0);

        arch_cr3_write(unpriv_cr3);
        klog_info("SMP", "[AP] CPU%d switched to unprivileged mode", my_index);

        /* Verify NK invariants on AP as well */
        test_nk_invariants_verify_ap();
    }

    cpu_info[my_index].state = CPU_ONLINE;
    /* Release memory barrier to ensure state is visible before marking ready */
    smp_mb();

    /* Mark CPU as ready FIRST - BSP is waiting for this */
    smp_mark_cpu_ready(my_index);

    /* Small delay to ensure BSP sees we're ready */
    for (volatile int i = 0; i < 1000; i++) {
        cpu_relax();
    }

    /* Poll for spin lock test mode - BSP will set this flag
     * The wrapper handles the CONFIG guard internally */
    extern volatile int spinlock_test_start;
    while (!spinlock_test_start) {
        cpu_relax();
    }

    /* Enter test mode - participate in SMP tests
     * The wrapper is an empty stub when CONFIG_TESTS_SPINLOCK=0 */
    spinlock_test_ap_entry();

    /* Poll for SMP monitor stress test mode
     * The wrapper handles the CONFIG guard internally */
    extern volatile int nk_smp_monitor_stress_test_start;
    while (!nk_smp_monitor_stress_test_start) {
        cpu_relax();
    }

    /* Enter stress test mode
     * The wrapper is an empty stub when CONFIG_TESTS_SMP_MONITOR_STRESS=0 */
    nk_smp_monitor_stress_ap_entry();

    /* Halt */
    while (1) { arch_halt(); }
}

/* patch_ap_trampoline removed - trampoline now works without runtime patching
 * The trampoline is copied to 0x7000 during BSP boot in boot.S using rep movsb.
 * All addresses are resolved at link time using external symbols. */

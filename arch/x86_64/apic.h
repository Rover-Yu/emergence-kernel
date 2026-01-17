/* JAKernel - x86-64 Local APIC and SMP support */

#ifndef JAKERNEL_ARCH_X86_64_APIC_H
#define JAKERNEL_ARCH_X86_64_APIC_H

#include <stdint.h>

/* Local APIC MMIO registers (offsets from base address) */
#define LAPIC_ID            0x020   /* Local APIC ID register */
#define LAPIC_VER           0x030   /* Local APIC Version register */
#define LAPIC_TPR           0x080   /* Task Priority Register */
#define LAPIC_APR           0x090   /* Arbitration Priority */
#define LAPIC_PPR           0x0A0   /* Processor Priority */
#define LAPIC_EOI           0x0B0   /* End of Interrupt */
#define LAPIC_LDR           0x0D0   /* Logical Destination */
#define LAPIC_DFR           0x0E0   /* Destination Format */
#define LAPIC_SVR           0x0F0   /* Spurious Interrupt Vector */
#define LAPIC_ISR0          0x100   /* In-Service Register 0 */
#define LAPIC_ISR1          0x110   /* In-Service Register 1 */
#define LAPIC_TMR0          0x180   /* Trigger Mode Register 0 */
#define LAPIC_TMR1          0x190   /* Trigger Mode Register 1 */
#define LAPIC_IRR0          0x200   /* Interrupt Request Register 0 */
#define LAPIC_IRR1          0x210   /* Interrupt Request Register 1 */
#define LAPIC_ESR           0x280   /* Error Status */
#define LAPIC_ICR_LOW       0x300   /* Interrupt Command Register Low */
#define LAPIC_ICR_HIGH      0x310   /* Interrupt Command Register High */
#define LAPIC_TIMER_LVT     0x320   /* Timer LVT */
#define LAPIC_THERM_LVT     0x330   /* Thermal Monitor LVT */
#define LAPIC_PERF_LVT      0x340   /* Performance Counter LVT */
#define LAPIC_LINT0_LVT     0x350   /* LINT0 LVT */
#define LAPIC_LINT1_LVT     0x360   /* LINT1 LVT */
#define LAPIC_ERROR_LVT     0x370   /* Error LVT */
#define LAPIC_TIMER_ICR     0x380   /* Timer Initial Count */
#define LAPIC_TIMER_CCR     0x390   /* Timer Current Count */
#define LAPIC_TIMER_DCR     0x3E0   /* Timer Divide Config */

/* ICR (Interrupt Command Register) bits */
#define LAPIC_ICR_DS        0x1000  /* Delivery Status */
#define LAPIC_ICR_LEVEL     0x8000  /* Level */
#define LAPIC_ICR_ASSERT    0x4000  /* Assert */
#define LAPIC_ICR_PENDING   0x1000  /* Send pending */

/* ICR Delivery Modes (these are shifted by 8 in the ICR register) */
#define LAPIC_ICR_DM_FIXED  (0 << 8)   /* Fixed delivery mode */
#define LAPIC_ICR_DM_LOWPRI (1 << 8)   /* Lowest priority */
#define LAPIC_ICR_DM_SMI    (2 << 8)   /* System Management Interrupt */
#define LAPIC_ICR_DM_NMI    (4 << 8)   /* Non-maskable interrupt */
#define LAPIC_ICR_DM_INIT   (5 << 8)   /* INIT */
#define LAPIC_ICR_DM_STARTUP (6 << 8)  /* Startup */
#define LAPIC_ICR_DM_EXTINT (7 << 8)   /* External interrupt */

/* ICR Destination Modes */
#define LAPIC_ICR_DST_PHYSICAL 0x000 /* Physical destination */
#define LAPIC_ICR_DST_LOGICAL  0x800 /* Logical destination */

/* ICR Destination Shorthand */
#define LAPIC_ICR_DST_NONE   0x0000  /* No shorthand */
#define LAPIC_ICR_DST_SELF   0x4000  /* Self */
#define LAPIC_ICR_DST_ALL    0x8000  /* All including self */
#define LAPIC_ICR_DST_OTHERS 0xC000  /* All excluding self */

/* SVR (Spurious Interrupt Vector Register) bits */
#define LAPIC_SVR_ENABLE     0x100   /* APIC enable bit */

/* Timer interrupt vector */
#define TIMER_VECTOR         32      /* Timer interrupt vector */

/* Default Local APIC base address (will be detected from MSR) */
#define LAPIC_DEFAULT_BASE   0xFEE00000

/* Maximum number of CPUs supported */
#define MAX_CPUS             4

/* CPU states */
typedef enum {
    CPU_STATE_UNINITIALIZED,    /* CPU not yet started */
    CPU_STATE_STARTING,         /* CPU is being started */
    CPU_STATE_RUNNING,          /* CPU is running */
    CPU_STATE_READY             /* CPU completed initialization */
} cpu_state_t;

/* CPU information structure */
typedef struct {
    uint8_t id;                 /* Local APIC ID */
    uint8_t acpi_id;            /* ACPI processor ID */
    cpu_state_t state;          /* Current state */
    void *stack_top;            /* Top of stack for this CPU */
} cpu_info_t;

/* APIC function prototypes */

/* Read/write Local APIC registers */
uint32_t lapic_read(uint32_t offset);
void lapic_write(uint32_t offset, uint32_t value);

/* Get Local APIC base address */
uint64_t lapic_get_base(void);

/* Initialize Local APIC */
void lapic_init(void);

/* Get Local APIC ID of current CPU */
uint8_t lapic_get_id(void);

/* Send IPI (Inter-Processor Interrupt) */
void lapic_send_ipi(uint8_t apic_id, uint32_t delivery_mode, uint8_t vector);

/* Startup Application Processor */
int ap_startup(uint8_t apic_id, uint32_t startup_addr);

/* Wait for IPI delivery */
void lapic_wait_for_ipi(void);

/* BSP (Boot Strap Processor) detection */
int is_bsp(void);

/* Timer functions */
void lapic_timer_init(uint32_t frequency);
void lapic_timer_set_divide(uint32_t divide_value);
void lapic_timer_set_initial_count(uint32_t count);
uint32_t lapic_timer_get_current_count(void);

#endif /* JAKERNEL_ARCH_X86_64_APIC_H */

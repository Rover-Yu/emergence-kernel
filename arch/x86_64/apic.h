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
#define LAPIC_ICR_LEVEL     0x8000  /* Level triggered (for INIT) */
#define LAPIC_ICR_ASSERT    0x4000  /* Assert */
#define LAPIC_ICR_LEVELTRIG 0x8000  /* Level triggered (alias for clarity) */
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

/* Timer interrupt vectors */
#define TIMER_VECTOR         32      /* APIC Timer interrupt vector */
#define RTC_VECTOR           40      /* RTC interrupt vector (IRQ 8) */

/* Default Local APIC base address (will be detected from MSR) */
#define LAPIC_DEFAULT_BASE   0xFEE00000

/* APIC Timer LVT (Local Vector Table) bits */
#define LAPIC_TIMER_LVT_MASK      0x10000  /* Timer mask */
#define LAPIC_TIMER_LVT_PERIODIC  0x20000  /* Periodic mode */
#define LAPIC_TIMER_LVT_ONESHOT   0x00000  /* One-shot mode */
#define LAPIC_TIMER_LVT_TSCDEADLINE 0x40000 /* TSC deadline mode */

/* APIC Timer Divide Configuration */
#define LAPIC_TIMER_DIV_BY_1   0xB    /* Divide by 1 */
#define LAPIC_TIMER_DIV_BY_2   0x0    /* Divide by 2 */
#define LAPIC_TIMER_DIV_BY_4   0x1    /* Divide by 4 */
#define LAPIC_TIMER_DIV_BY_8   0x2    /* Divide by 8 */
#define LAPIC_TIMER_DIV_BY_16  0x3    /* Divide by 16 */
#define LAPIC_TIMER_DIV_BY_32  0x8    /* Divide by 32 */
#define LAPIC_TIMER_DIV_BY_64  0x9    /* Divide by 64 */
#define LAPIC_TIMER_DIV_BY_128 0xA    /* Divide by 128 */

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

/* Wait for IPI delivery (returns 0 on success, -1 on timeout) */
int lapic_wait_for_ipi(void);

/* BSP (Boot Strap Processor) detection */
int is_bsp(void);

/* APIC Timer initialization */
void apic_timer_init(void);

#endif /* JAKERNEL_ARCH_X86_64_APIC_H */

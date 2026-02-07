#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* GDT Segment Descriptor bits */
#define GDT_PRESENT     (1ULL << 47)
#define GDT_DPL_0       (0ULL << 45)   /* Ring 0 */
#define GDT_DPL_3       (3ULL << 45)   /* Ring 3 */
#define GDT_S_SYSTEM    (0ULL << 44)   /* System segment (TSS) */
#define GDT_S_CODE_DATA (1ULL << 44)   /* Code or data segment */
#define GDT_TYPE_CODE   (0xAULL << 40) /* Execute/Read, 64-bit */
#define GDT_TYPE_DATA   (0x2ULL << 40) /* Read/Write */
#define GDT_TYPE_TSS    (0x9ULL << 40) /* 64-bit TSS (Available) */
#define GDT_GRANULARITY (1ULL << 55)   /* 4KB pages */
#define GDT_LONG_MODE   (1ULL << 54)   /* 64-bit code */

/* Segment selectors */
#define GDT_NULL        0x00
#define GDT_KERNEL_CS   0x08
#define GDT_KERNEL_DS   0x10
#define GDT_USER_CS     0x18   /* Index 3, RPL=0 in selector, actual DPL=3 */
#define GDT_USER_DS     0x20   /* Index 4 */
#define GDT_TSS         0x28   /* Index 5 */

/* TSS structure for x86_64 */
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) tss_pointer_t;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss_t;

#endif /* GDT_H */

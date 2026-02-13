# Emergence Kernel 嵌套内核架构设计文档

> **基于**: ASPLOS '15 论文 "Nested Kernel: An Operating System Architecture for Intra-Kernel Privilege Separation"
> **版本**: 1.0
> **日期**: 2026-02-06

---

## 目录

1. [架构概述](#1-架构概述)
2. [内存布局](#2-内存布局)
3. [PCD 系统](#3-pcd-系统)
4. [Monitor 调用接口](#4-monitor-调用接口)
5. [Trampoline 机制](#5-trampoline-机制)
6. [安全不变量](#6-安全不变量)
7. [启动流程](#7-启动流程)
8. [实现验证](#8-实现验证)

---

## 1. 架构概述

### 1.1 设计目标

嵌套内核架构将传统内核分为两个特权域，通过硬件内存保护机制实现内核内部的特权分离：

| 组件 | 职责 | 特权级别 | 代码位置 |
|------|------|----------|----------|
| **Nested Kernel (Monitor)** | • PCD 初始化与维护<br>• 物理页类型状态机管理<br>• 页表映射操作 | 特权模式 (CR0.WP=0) | `/kernel/monitor/` |
| **Outer Kernel** | • CPU 管理 (SMP)<br>• 异常与中断处理<br>• 物理页分配 (通过 monitor) | 非特权模式 (CR0.WP=1) | `/arch/x86_64/`, `/kernel/` |

### 1.2 核心安全机制

1. **双页表视图**: 同一物理内存有两种虚拟映射视图
2. **写保护强制执行**: 通过 CR0.WP 位防止外层内核修改只读页表
3. **受控上下文切换**: 外层内核只能通过 trampoline 进入监视器
4. **物理页所有权**: PCD 系统跟踪每个物理页的所有者

### 1.3 物理页分类

每个物理页必须属于以下四种类型之一：

| 页类型 | PCD 常量 | 描述 | Outer Kernel 访问 |
|--------|----------|------|-------------------|
| **Outer Kernel 页** | `PCD_TYPE_OK_NORMAL` (0) | 外层内核使用的普通页 | 读写 |
| **Nested Kernel 页** | `PCD_TYPE_NK_NORMAL` (1) | 嵌套内核私有页 | 只读 (通过 RO 映射) |
| **页表页** | `PCD_TYPE_NK_PGTABLE` (2) | 页表结构页 | 只读 |
| **共享页** | `PCD_TYPE_NK_IO` (3) | I/O 寄存器映射 | 跟踪但不强制 |

---

## 2. 内存布局

### 2.1 虚拟地址空间布局

```
+----------------------+ 0xFFFFFFFFFFFFFFFF
| 高位规范地址空间       |
| (未使用)              |
+----------------------+ 0xFFFF880000000000
| 只读映射区域          |   NESTED_KERNEL_RO_BASE
| - NK_NORMAL 页         |   - 监视器数据对外层内核可见
| - NK_PGTABLE 页        |   - 强制只读访问
+----------------------+ 0xFFFF800000000000
| 内核地址空间          |   KERNEL_VIRT_BASE
| - 恒等映射             |   - 内核代码和数据
| - 每CPU 栈             |   - APIC 映射
+----------------------+ 0xFEE00000
| APIC 内存映射I/O      |
| (Local APIC)          |
+----------------------+ 0x0000000000000000
| 恒等映射物理内存      |
| - 前 2MB: 内核、栈、页表 |
| - 页表映射为 4KB 页     |
+----------------------+
```

### 2.2 物理内存组织

```
+----------------------+ 0x200000 (2MB)
| 可用内存 (PMM 管理)   |
+----------------------+ 0x110000
| 外层内核 CPU 栈       |
| (ok_cpu_stacks)       |
+----------------------+ 0x108000
| 监视器栈              |
| (monitor_stacks)      |
+----------------------+ 0x100000
| 内核代码/数据         |
+----------------------+ 0x11000 - 0x20000
| 页表区域              |
| - boot_pml4, pdpt, pd |
| - monitor_pml4, pdpt, pd |
| - unpriv_pml4, pdpt, pd |
| - monitor_pt_0_2mb    |
| - unpriv_pt_0_2mb     |
+----------------------+ 0x7000
| AP Trampoline         |
+----------------------+ 0x0
| Boot 栈               |
+----------------------+
```

### 2.3 双页表视图设计

```
┌────────────────────────────────────────────────────────────┐
│ monitor_pml4 (特权视图)                                    │
│                                                             │
│ PML4[0] → monitor_pdpt → monitor_pd → monitor_pt_0_2mb   │
│   - 所有 PTE: PRESENT + WRITABLE                          │
│   - Monitor 可以修改任何 PTE                               │
│   - 仅当 CR3 = monitor_pml4_phys 时使用                   │
└────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────┐
│ unpriv_pml4 (非特权视图)                                   │
│                                                             │
│ PML4[0] → unpriv_pdpt → unpriv_pd → unpriv_pt_0_2mb      │
│   - 页表 PTE: 仅 PRESENT (无 WRITABLE) ← 保护关键         │
│   - 栈 PTE: PRESENT + WRITABLE                             │
│   - 代码 PTE: PRESENT + WRITABLE                           │
│   - 当 CR3 = unpriv_pml4_phys 时使用                      │
└────────────────────────────────────────────────────────────┘
```

**关键差异**: `unpriv_pt_0_2mb` 中页表页的 PTE 缺少 `WRITABLE` 位，强制执行写保护。

---

## 3. PCD 系统

### 3.1 PCD 结构定义

**文件**: `/opt/workbench/os/gckernel/claude/kernel/pcd.h`

```c
/* PCD 页类型 */
#define PCD_TYPE_OK_NORMAL   0  /* 外层内核普通页 */
#define PCD_TYPE_NK_NORMAL   1  /* Monitor 私有页 */
#define PCD_TYPE_NK_PGTABLE  2  /* 页表页 */
#define PCD_TYPE_NK_IO       3  /* I/O 寄存器映射 */

/* PCD 结构 - 每页元数据 (8 字节) */
typedef struct __attribute__((packed)) {
    uint8_t  type;           /* 页类型 (PCD_TYPE_*) */
    uint8_t  flags;          /* 额外标志 */
    uint16_t reserved;       /* 预留扩展 */
    uint32_t refcount;       /* 共享页引用计数 */
} pcd_t;

/* PCD 系统状态 */
typedef struct {
    pcd_t    *pages;          /* PCD 数组 */
    uint64_t  max_pages;      /* 管理的最大页数 */
    uint64_t  base_page;      /* 起始物理页号 */
    spinlock_t lock;          /* 保护 PCD 访问 */
    bool      initialized;    /* 初始化标志 */
} pcd_state_t;
```

### 3.2 页类型状态机

```
                    初始状态
                        |
                        v
                    NK_NORMAL
                  (Monitor 拥有)
                        |
          +-------------+-------------+
          |             |             |
          v             v             v
    [分配]          [标记为]        [标记为]
          |           PGTABLE          IO
          v               |               |
      OK_NORMAL          |               |
   (转移给外层内核)       |               |
          |               |               |
          +---------------+---------------+
                        |
                   [释放]
                        |
                        v
                    NK_NORMAL
```

**状态转换规则**:

| 转换 | 触发条件 | 执行者 | PCD 操作 |
|------|----------|--------|----------|
| NK_NORMAL → OK_NORMAL | 外层内核分配 | Monitor | `monitor_pmm_alloc()` |
| NK_NORMAL → NK_PGTABLE | 分配页表页 | Monitor | `pcd_set_type(phys, NK_PGTABLE)` |
| NK_NORMAL → NK_IO | 标记 I/O 区域 | Monitor | `pcd_mark_region(base, size, NK_IO)` |
| 任意类型 → NK_NORMAL | 释放物理页 | Monitor | `monitor_pmm_free()` |

### 3.3 PCD API

**文件**: `/opt/workbench/os/gckernel/claude/kernel/pcd.c`

```c
/* 初始化 PCD 系统
 * 分配 PCD 数组并初始化所有页为 NK_NORMAL
 */
void pcd_init(void);

/* 设置页类型 (Monitor 专用) */
void pcd_set_type(uint64_t phys_addr, uint8_t type);

/* 查询页类型 (外层内核只读) */
uint8_t pcd_get_type(uint64_t phys_addr);

/* 标记内存区域 */
void pcd_mark_region(uint64_t base, uint64_t size, uint8_t type);

/* 获取 PCD 统计信息 */
void pcd_dump_stats(void);
```

### 3.4 初始化流程

```c
void pcd_init(void) {
    // 1. 分配 PCD 数组
    size_t pcd_size = max_pages * sizeof(pcd_t);
    pcd_state.pages = pmm_alloc_order(order);

    // 2. 初始化所有页为 NK_NORMAL
    for (uint64_t i = 0; i < max_pages; i++) {
        pcd_state.pages[i].type = PCD_TYPE_NK_NORMAL;
        pcd_state.pages[i].refcount = 0;
    }

    // 3. 标记内核代码区域为 NK_NORMAL
    pcd_mark_region(0x100000, 0x8000, PCD_TYPE_NK_NORMAL);

    // 4. 标记外层内核栈为 OK_NORMAL
    pcd_mark_region((uint64_t)ok_cpu_stacks, stack_size, PCD_TYPE_OK_NORMAL);
}
```

---

## 4. Monitor 调用接口

### 4.1 Monitor 调用类型

**文件**: `/opt/workbench/os/gckernel/claude/kernel/monitor/monitor.h`

```c
typedef enum {
    MONITOR_CALL_ALLOC_PHYS,     /* 分配物理内存 */
    MONITOR_CALL_FREE_PHYS,      /* 释放物理内存 */
    MONITOR_CALL_SET_PAGE_TYPE,  /* 设置 PCD 类型 (Monitor 专用) */
    MONITOR_CALL_GET_PAGE_TYPE,  /* 查询 PCD 类型 */
    MONITOR_CALL_MAP_PAGE,       /* 映射页面 (带验证) */
    MONITOR_CALL_UNMAP_PAGE,     /* 解除页面映射 */
    MONITOR_CALL_ALLOC_PGTABLE,  /* 分配页表页 (自动标记为 NK_PGTABLE) */
} monitor_call_t;

typedef struct {
    uint64_t result;
    int error;
} monitor_ret_t;
```

### 4.2 PMM 包装函数

```c
/* 通过 Monitor 分配物理内存
 * 副作用: 将页面标记为 OK_NORMAL (转移所有权)
 */
void *monitor_pmm_alloc(uint8_t order);

/* 通过 Monitor 释放物理内存
 * 副作用: 将页面标记为 NK_NORMAL (归还所有权给 Monitor)
 */
void monitor_pmm_free(void *addr, uint8_t order);
```

### 4.3 页表操作

```c
/* 分配页表页 (自动标记为 NK_PGTABLE) */
void *monitor_alloc_pgtable(uint8_t order);

/* 映射页面 (带 PCD 验证) */
int monitor_map_page(uint64_t phys_addr, uint64_t virt_addr, uint64_t flags);

/* 解除页面映射 */
int monitor_unmap_page(uint64_t virt_addr);

/* 刷新页表 TLB 条目 */
void monitor_invalidate_page(uint64_t virt_addr);
```

---

## 5. Trampoline 机制

### 5.1 安全入口/出口设计

**文件**: `/opt/workbench/os/gckernel/claude/arch/x86_64/monitor/monitor_call.S`

```
┌─────────────────────────────────────────────────────────────┐
│ 外层内核 (非特权模式)                                       │
│ - CR3 = unpriv_pml4_phys                                   │
│ - CR0.WP = 1 (写保护强制执行)                              │
│ - 页表只读                                                 │
└─────────────────────────────────────────────────────────────┘
                          │
                          │ monitor_call()
                          │
                          v
┌─────────────────────────────────────────────────────────────┐
│ nk_entry_trampoline (汇编存根)                              │
│                                                             │
│ 1. 保存所有寄存器 (r15-rbx, rsp)                           │
│ 2. 保存当前 CR3                                            │
│ 3. 切换 CR3 → monitor_pml4_phys                            │
│ 4. 清除 CR0.WP (位16) - 允许 Monitor 修改 PTE              │
│ 5. 调用 monitor_call_handler()                             │
│ 6. 设置 CR0.WP - 恢复写保护                                │
│ 7. 恢复 CR3 (unpriv_pml4_phys)                             │
│ 8. 恢复寄存器                                              │
│ 9. 返回调用者                                              │
└─────────────────────────────────────────────────────────────┘
                          │
                          v
┌─────────────────────────────────────────────────────────────┐
│ 嵌套内核 (特权模式)                                         │
│ - CR3 = monitor_pml4_phys                                  │
│ - CR0.WP = 0 (可修改 PTE)                                  │
│ - 完全访问页表和 PCD                                       │
└─────────────────────────────────────────────────────────────┘
```

### 5.2 Trampoline 实现

```assembly
/* nk_entry_trampoline - 安全入口点 */
.global nk_entry_trampoline
nk_entry_trampoline:
    /* 保存所有通用寄存器 */
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15

    /* 保存当前状态 */
    mov %rsp, %r8          /* 保存栈指针 */
    mov %cr3, %r9          /* 保存当前 CR3 */

    /* 切换到特权页表 */
    lea monitor_pml4_phys(%rip), %r10
    mov (%r10), %r10
    mov %r10, %cr3

    /* 清除 CR0.WP 以进入 Monitor */
    mov %cr0, %r11
    btr $16, %r11          /* 清除 WP 位 (位 16) */
    mov %r11, %cr0

    /* 调用 C Monitor 处理函数 */
    call monitor_call_handler

    /* 设置 CR0.WP 以退出 Monitor */
    mov %cr0, %r11
    bts $16, %r11          /* 设置 WP 位 */
    mov %r11, %cr0

    /* 恢复非特权 CR3 */
    mov %r9, %cr3
    mov %r8, %rsp

    /* 恢复寄存器 */
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx

    ret
```

---

## 6. 安全不变量

### 6.1 六大不变量

基于 ASPLOS '15 论文，嵌套内核强制执行以下不变量：

| 不变量 | 描述 | 强制机制 | 实现位置 |
|--------|------|----------|----------|
| **Inv 1** | 受保护数据（页表页）在外层内核中只读 | 在 unpriv_pt_0_2mb 中清除 WRITABLE 位 | `monitor_protect_state()` |
| **Inv 2** | 写保护权限被强制执行 | 在外层内核中设置 CR0.WP = 1 | `main.c:189`, `smp.c:274` |
| **Inv 3** | 全局映射在两个视图中都可访问 | PML4 条目匹配（除 PML4[0]） | `monitor_init()` |
| **Inv 4** | 上下文切换机制可用 | nk_entry_trampoline 可调用 | `monitor_call.S` |
| **Inv 5** | 所有页表页在嵌套内核中可写 | monitor_pt_0_2mb 设置 WRITABLE 位 | `monitor_init()` |
| **Inv 6** | CR3 只加载预声明的页表 | 仅加载 monitor_pml4_phys 或 unpriv_pml4_phys | 启动流程强制 |

### 6.2 不变量验证

**文件**: `/opt/workbench/os/gckernel/claude/kernel/monitor/monitor.c`

```c
void monitor_verify_invariants(void) {
    bool all_passed = true;

    // Inv 1: 页表页在外层内核中只读
    for (each page_table_page) {
        pte = unpriv_pt[pt_index];
        if (pte & WRITABLE) {
            printk("Inv 1 FAIL: page table page is writable in unprivileged view\n");
            all_passed = false;
        }
    }

    // Inv 2: CR0.WP 被设置
    uint64_t cr0 = read_cr0();
    if (!(cr0 & CR0_WP)) {
        printk("Inv 2 FAIL: CR0.WP is not set\n");
        all_passed = false;
    }

    // Inv 3: 全局映射匹配
    for (i = 256; i < 512; i++) {
        if (monitor_pml4[i] != unpriv_pml4[i]) {
            printk("Inv 3 FAIL: PML4[%d] mismatch\n", i);
            all_passed = false;
        }
    }

    // Inv 4: 上下文切换机制可用
    if (!nk_entry_trampoline) {
        printk("Inv 4 FAIL: trampoline not available\n");
        all_passed = false;
    }

    // Inv 5: 页表页在监视器中可写
    for (each page_table_page) {
        pte = monitor_pt[pt_index];
        if (!(pte & WRITABLE)) {
            printk("Inv 5 FAIL: page table page is not writable in monitor view\n");
            all_passed = false;
        }
    }

    // Inv 6: CR3 只包含预声明的页表
    uint64_t cr3 = read_cr3();
    if (cr3 != monitor_pml4_phys && cr3 != unpriv_pml4_phys) {
        printk("Inv 6 FAIL: CR3 has unexpected value\n");
        all_passed = false;
    }

    if (all_passed) {
        printk("[CPU %d] Nested Kernel invariants: PASS\n", cpu_index);
    }
}
```

---

## 7. 启动流程

### 7.1 BSP 启动序列

**文件**: `/opt/workbench/os/gckernel/claude/arch/x86_64/main.c`

```
_start (boot.S)
    │
    ├─> 启用 Long Mode
    ├─> 设置页表 (boot_pml4)
    ├─> long_mode_start
    │       │
    │       ├─> 修补 AP trampoline 占位符
    │       └─> kernel_main (main.c)
    │               │
    │               ├─> 初始化驱动
    │               ├─> pmm_init()
    │               ├─> pcd_init()               [标记页为 NK_NORMAL]
    │               ├─> monitor_init()           [设置双页表]
    │               │       │
    │               │       ├─> 分配 monitor_pml4, unpriv_pml4
    │               │       ├─> 复制 boot_pml4 映射
    │               │       ├─> 为前 2MB 设置 4KB 页
    │               │       ├─> monitor_protect_state()
    │               │       └─> monitor_create_ro_mappings()
    │               │
    │               ├─> 切换 CR3 → unpriv_pml4_phys
    │               ├─> 设置 CR0.WP = 1
    │               ├─> monitor_verify_invariants()
    │               ├─> smp_start_all_aps()
    │               └─> system_shutdown()
```

**关键代码** (`main.c:176-209`):

```c
void kernel_main(void) {
    /* ... 驱动初始化 ... */

    /* 初始化 PCD 系统 */
    pcd_init();

    /* 初始化嵌套内核 */
    monitor_init();

    /* 获取非特权页表地址 */
    uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();

    /* 设置写保护 */
    uint64_t cr0 = read_cr0();
    cr0 |= CR0_WP;          // 设置位 16
    write_cr0(cr0);

    /* 切换到非特权页表 */
    write_cr3(unpriv_cr3);

    /* 验证不变量 */
    #if CONFIG_WRITE_PROTECTION_VERIFY
    monitor_verify_invariants();
    #endif

    /* 启动 AP */
    smp_start_all_aps();
}
```

### 7.2 AP 启动序列

**文件**: `/opt/workbench/os/gckernel/claude/kernel/smp.c`

```
_start (boot.S)
    │
    ├─> 等待 bsp_init_done 标志
    ├─> ap_entry
    │       │
    │       ├─> long_mode_start
    │       └─> ap_start (smp.c)
    │               │
    │               ├─> 分配 CPU 索引
    │               ├─> 设置栈 (ok_cpu_stacks)
    │               ├─> 切换 CR3 → unpriv_pml4_phys
    │               ├─> 设置 CR0.WP = 1
    │               ├─> monitor_verify_invariants()
    │               └─> 停机
```

**关键代码** (`smp.c:268-285`):

```c
void ap_start(void) {
    /* 分配 CPU 索引 */
    uint32_t cpu_index = smp_get_cpu_index();

    /* 设置外层内核栈 */
    set_stack(ok_cpu_stacks[cpu_index]);

    /* 嵌套内核切换 */
    uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();

    /* 设置写保护 */
    uint64_t cr0 = read_cr0();
    cr0 |= CR0_WP;
    write_cr0(cr0);

    /* 切换到非特权页表 */
    write_cr3(unpriv_cr3);

    /* 验证不变量 */
    #if CONFIG_WRITE_PROTECTION_VERIFY
    monitor_verify_invariants();
    #endif

    /* 标记 CPU 就绪 */
    smp_mark_cpu_ready(cpu_index);
}
```

---

## 8. 实现验证

### 8.1 测试矩阵

| 测试 | 状态 | 验证内容 |
|------|------|----------|
| Boot Test | ✅ 通过 | 基本启动和不变量验证 |
| PCD Test | ✅ 通过 | PCD 系统初始化和页类型标记 |
| Nested Kernel Invariants | ✅ 通过 | 六大不变量强制执行 |
| Read-Only Visibility | ✅ 通过 | 只读映射创建 |
| SMP Boot | ✅ 通过 | 多 CPU 嵌套内核切换 |
| APIC Timer | ✅ 通过 | 定时器中断处理 |
| NK Protection | ⚠️ 跳过 | CONFIG_NK_PROTECTION_TESTS=0 |

### 8.2 验证命令

```bash
# 编译内核
make clean && make

# 运行基本启动测试 (1 CPU)
make test-boot

# 运行不变量测试 (2 CPU)
make test-nested-kernel

# 运行所有测试
make test-all

# 交互式调试
make run-debug
# 在另一个终端:
# gdb -x .gdbinit
```

### 8.3 预期输出

成功启动后应看到:
```
MONITOR: Initializing nested kernel architecture
MONITOR: Enforcing Nested Kernel invariants
MONITOR: Nested Kernel invariants enforced
[CPU 0] Nested Kernel invariants: PASS
```

多 CPU 启动时:
```
[CPU 0] Nested Kernel invariants: PASS
[CPU 1] Nested Kernel invariants: PASS
```

---

## 9. 配置选项

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `CONFIG_WRITE_PROTECTION_VERIFY` | 1 | 在所有 CPU 上验证不变量 |
| `CONFIG_INVARIANTS_VERBOSE` | 0 | 详细验证输出 |
| `CONFIG_PCD_STATS` | 0 | 显示 PCD 统计信息 |
| `CONFIG_NK_PROTECTION_TESTS` | 0 | 启用保护测试 (会导致故障) |

---

## 10. 参考资料

**论文**: "Nested Kernel: An Operating System Architecture for Intra-Kernel Privilege Separation"
- 作者: Nathan Dautenhahn, Theodoros Kasampalis, Will Dietz, John Criswell, Vikram Adve
- 会议: ASPLOS '15
- 网站: [nestedkernel.github.io](http://nestedkernel.github.io/)

---

*本文档由 Claude Code 生成，基于 Emergence Kernel 的嵌套内核实现。*

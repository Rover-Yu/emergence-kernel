# 嵌套内核监控 API (Nested Kernel Monitor API)

本文档介绍 Emergence Kernel 的嵌套内核监控 API，实现了论文《Nested Kernel: An Operating System Architecture for Intra-Kernel Privilege Separation》(ASPLOS '15)中的架构。

## 概述

嵌套内核架构将内核分为两个特权域：
- **监控器 (Monitor / Nested Kernel)**: 特权模式，可访问所有内存和页表
- **外部内核 (Outer Kernel)**: 非特权模式，受嵌套内核不变量限制

## 核心概念

### 页表视图

监控器维护两个独立的页表视图：

| 视图 | CR3 值 | 用途 |
|------|---------|------|
| **特权模式** | `monitor_pml4_phys` | 完整系统访问，页表可写 |
| **非特权模式** | `unpriv_pml4_phys` | 受限访问，页表只读 |

### 嵌套内核不变量

监控器强制执行 ASPLOS '15 论文中的 6 个不变量：

| 不变量 | 描述 |
|--------|------|
| **不变量 1** | 受保护数据（页表页）在外部内核中只读 |
| **不变量 2** | 写保护权限强制执行 (CR0.WP=1) |
| **不变量 3** | 全局映射在两个视图中都可访问 |
| **不变量 4** | 上下文切换机制可用 |
| **不变量 5** | 所有页表页在嵌套内核中可写 |
| **不变量 6** | CR3 只加载预声明的页表根 |

## API 参考

### 初始化

#### `monitor_init(void)`

初始化嵌套内核监控器。

**调用位置：** 仅 BSP，内核初始化期间

**副作用：**
- 分配 6 个页表页（3 个用于特权视图，3 个用于非特权视图）
- 将启动页表映射复制到两个视图
- 在非特权视图中保护页表页（不变量 1 和 5）
- 设置 `monitor_pml4_phys` 和 `unpriv_pml4_phys`

**示例：**
```c
extern void monitor_init(void);

void kernel_main(void) {
    // ...
    monitor_init();
    // ...
}
```

---

### 验证

#### `monitor_verify_invariants(void)`

验证所有 6 个嵌套内核不变量是否正确执行。

**调用位置：** 切换到非特权模式后（BSP 和 AP）

**输出行为：**
- **安静模式** (`CONFIG_INVARIANTS_VERBOSE=0`)：仅显示最终结果
  ```
  [CPU 0] Nested Kernel invariants: PASS
  ```
- **详细模式** (`CONFIG_INVARIANTS_VERBOSE=1`)：显示每个不变量的详细信息

**示例：**
```c
extern void monitor_verify_invariants(void);

// 切换到非特权页表后
uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
asm volatile ("mov %0, %%cr3" : : "r"(unpriv_cr3) : "memory");
monitor_verify_invariants();
```

---

### 页表切换

#### `monitor_get_unpriv_cr3(void)`

获取非特权页表根的物理地址。

**返回：** 非特权 PML4 的物理地址

**示例：**
```c
extern uint64_t monitor_get_unpriv_cr3(void);

uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
if (unpriv_cr3 != 0) {
    asm volatile ("mov %0, %%cr3" : : "r"(unpriv_cr3) : "memory");
}
```

---

### 特权模式检测

#### `monitor_is_privileged(void)`

检查当前是否运行在特权（监控器）模式。

**返回：** 特权模式下为 `true`，非特权模式下为 `false`

**示例：**
```c
extern bool monitor_is_privileged(void);

if (monitor_is_privileged()) {
    // 可以直接修改页表页
} else {
    // 必须使用 monitor_call() 执行特权操作
}
```

---

### 监控调用

#### `monitor_call(monitor_call_t, arg1, arg2, arg3)`

从非特权模式调用特权监控器操作。

**参数：**
- `call`: 监控调用类型
- `arg1`, `arg2`, `arg3`: 调用特定参数

**返回：** `monitor_ret_t` 结构，包含 `result` 和 `error` 字段

**监控调用类型：**

| 调用 | 描述 | arg1 | arg2 | arg3 |
|------|------|------|------|------|
| `MONITOR_CALL_ALLOC_PHYS` | 分配物理内存 | 阶数 (0-9) | - | - |
| `MONITOR_CALL_FREE_PHYS` | 释放物理内存 | 物理地址 | 阶数 | - |

**示例：**
```c
extern monitor_ret_t monitor_call(monitor_call_t, uint64_t, uint64_t, uint64_t);

// 分配 2 页（阶数 1）
monitor_ret_t ret = monitor_call(MONITOR_CALL_ALLOC_PHYS, 1, 0, 0);
if (ret.error != 0) {
    // 处理错误
}
void *ptr = (void *)ret.result;
```

---

### PMM 封装函数

#### `monitor_pmm_alloc(uint8_t order)`

通过监控器分配物理内存。

**参数：** `order` - 分配阶数 (0 = 4KB, 1 = 8KB, 等等)

**返回：** 物理地址，失败时返回 NULL

#### `monitor_pmm_free(void *addr, uint8_t order)`

通过监控器释放物理内存。

**参数：**
- `addr`: 要释放的物理地址
- `order`: 分配阶数

**示例：**
```c
extern void *monitor_pmm_alloc(uint8_t order);
extern void monitor_pmm_free(void *addr, uint8_t order);

// 分配 16KB (阶数 2)
void *ptr = monitor_pmm_alloc(2);
if (ptr) {
    monitor_pmm_free(ptr, 2);
}
```

---

## 页表常量

来自 `arch/x86_64/paging.h`：

### PTE 标志位

| 位 | 名称 | 描述 |
|-----|------|------|
| 0 | `X86_PTE_PRESENT` | 页存在位 |
| 1 | `X86_PTE_WRITABLE` | 读写位 (0 = 只读) |
| 2 | `X86_PTE_USER` | 用户/监督模式位 |
| 7 | `X86_PTE_PS` | 页大小位 (1 = 2MB 页) |

### 页标志

| 标志 | 值 | 描述 |
|------|-----|------|
| `X86_PD_FLAGS_2MB` | `0x183` | 标准 2MB 页 (存在 + 可写 + PS) |
| `X86_PD_FLAGS_2MB_RO` | `0x181` | 只读 2MB 页 (仅存在 + PS) |

---

## 配置选项

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `CONFIG_WRITE_PROTECTION_VERIFY` | 1 | 在所有 CPU 上验证不变量 |
| `CONFIG_INVARIANTS_VERBOSE` | 0 | 显示详细验证输出 |
| `CONFIG_CR0_WP_CONTROL` | 1 | 启用二级保护 (PTE + CR0.WP) |

---

## 使用模式

### 切换到非特权模式

```c
// 1. 初始化监控器（仅 BSP）
monitor_init();

// 2. 切换到非特权页表
uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
if (unpriv_cr3 != 0) {
    // 启用 CR0.WP 强制执行
    uint64_t cr0;
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1 << 16);  // 设置 CR0.WP 位
    asm volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");

    // 切换到非特权视图
    asm volatile ("mov %0, %%cr3" : : "r"(unpriv_cr3) : "memory");

    // 验证不变量
    monitor_verify_invariants();
}
```

### 从非特权模式分配内存

```c
// 必须使用监控调用进行内存操作
void *ptr = monitor_pmm_alloc(2);  // 分配 16KB
if (!ptr) {
    serial_puts("分配失败\n");
}

// 使用内存
// ...

// 完成后释放
monitor_pmm_free(ptr, 2);
```

---

## 参考文献

**论文：** "Nested Kernel: An Operating System Architecture for Intra-Kernel Privilege Separation"
- 作者： Nathan Dautenhahn, Theodoros Kasampalis, Will Dietz, John Criswell, Vikram Adve
- 会议： ASPLOS '15
- 网站：[nestedkernel.github.io](http://nestedkernel.github.io/)

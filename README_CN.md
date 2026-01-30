# Emergence Kernel

> "我们没有编写一行代码。我们只是定义了创世的规则。"

> **Emergence Kernel** 是一个完全由 LLM（Claude）通过 Prompts 编写的实验性 x86_64 操作系统内核，**零手写代码**。

这是一个研究性内核项目，旨在探索大语言模型在操作系统内核开发领域的潜力边界。所有代码均通过自然语言对话生成，包括底层汇编、内存管理、多处理器支持等核心功能。

---

## 特性

### 当前已实现功能

#### 启动与模式切换
- **Multiboot2 引导** - 通过 GRUB 加载内核，解析内存布局
- **Real Mode → Protected Mode → Long Mode** - 完整的模式转换流程
- **页表设置** - Identity mapping 第 2MB 内存
- **GDT 配置** - 64 位全局描述符表
- **启动横幅** - 启动时显示内核标识信息

#### 多处理器支持 (SMP)
- **BSP/AP 识别** - 通过原子计数器区分启动处理器和应用处理器
- **AP 启动导引** (Trampoline) - 从实模式跳转到长模式
- **IPI 通信** - 处理器间中断支持
- **每 CPU 栈** - 独立的栈空间（16 KiB）
- **CPU 状态管理** - OFFLINE、BOOTING、ONLINE、READY 状态

#### 同步原语
- **自旋锁** - 基础 spin_lock/spin_unlock 操作
- **中断安全锁** - spin_lock_irqsave/spin_unlock_irqrestore 用于中断上下文
- **读写锁** - 多读者单写者访问模式
- **完整测试** - 10 个测试用例，包括单核和 SMP 多核场景

#### 物理内存管理
- **伙伴系统分配器** - 高效的 2 的幂次块分配
- **Multiboot2 内存映射** - 解析可用/保留内存区域
- **基于阶的分配** - 支持 0-9 阶（4KB 到 2MB 块）
- **自动合并** - 相邻空闲块自动合并
- **统计跟踪** - 空闲和总页数统计

#### 中断与异常处理
- **IDT 设置** - 完整的中断描述符表
- **异常处理器** - 除零、页错误等异常处理
- **ISR 桩架** - 汇编中断服务例程
- **APIC 定时器** - 高频定时器中断（数学名言输出）

#### 设备驱动框架
- **Linux 风格驱动模型** - probe/init/remove 模式
- **设备类型** - PLATFORM, ISA, PCI, SERIAL, CONSOLE
- **优先级初始化** - 支持驱动初始化顺序控制
- **匹配机制** - match_id/match_mask 设备匹配

#### APIC (Advanced Programmable Interrupt Controller)
- **Local APIC** - 每核中断控制器初始化
- **I/O APIC** - 外部中断路由
- **IPI 支持** - 处理器间中断发送/接收
- **定时器支持** - APIC 定时器用于调度和时间管理

#### 控制台输出
- **VGA 文本模式** - 80x25 字符显示
- **串口输出** (COM1) - 调试信息输出
- **标准化日志** - 所有子系统使用统一的日志前缀

#### 电源管理
- **系统关机** - SMP 初始化和测试完成后优雅关机

---

## 编译与运行

### 环境要求
- GCC (支持 freestanding)
- GNU binutils (as, ld)
- GRUB 工具 (grub-mkrescue)
- QEMU 系统模拟器

### 快速开始

```bash
# 编译内核和 ISO（使用默认配置）
make

# 运行 (2 CPUs)
make run

# 运行 (4 CPUs)
./run-qemu.sh

# 清理构建文件
make clean
```

### 配置选项

内核支持多种可配置选项，通过 `kernel.config` 文件或命令行参数控制：

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `CONFIG_SPINLOCK_TESTS` | 0 | 启用自旋锁测试套件 |
| `CONFIG_PMM_TESTS` | 0 | 启用物理内存管理测试 |
| `CONFIG_APIC_TIMER_TEST` | 1 | 启用 APIC 定时器测试（数学名言输出） |
| `CONFIG_SMP_AP_DEBUG` | 0 | 启用 AP 启动调试标记（串口输出 H→G→3→A→P→L→X→D→S→Q→A→I→L→T→W） |

**配置优先级：**
1. 命令行参数：`make CONFIG_SPINLOCK_TESTS=1`
2. 本地覆盖：`.config` 文件（不提交到 git）
3. 默认配置：`kernel.config` 文件

**示例：**
```bash
# 启用自旋锁测试
make CONFIG_SPINLOCK_TESTS=1

# 启用 AP 调试标记
make CONFIG_SMP_AP_DEBUG=1

# 创建本地配置文件（永久覆盖）
cp kernel.config .config
nano .config  # 编辑配置
```

### GDB 调试

终端 1：
```bash
make run-debug
```

终端 2：
```bash
gdb -x .gdbinit
```

---

## 项目结构

```
Emergence-Kernel/
├── arch/x86_64/          # 架构相关代码
│   ├── boot.S           # 启动代码 (32-bit → 64-bit)
│   ├── ap_trampoline.S  # AP 启动导引 (Real Mode → Long Mode)
│   ├── apic.c           # Local APIC / I/O APIC
│   ├── idt.c            # 中断描述符表
│   ├── isr.S            # 中断服务例程桩架
│   ├── vga.c            # VGA 文本模式驱动
│   ├── serial_driver.c  # 串口驱动
│   ├── acpi.c           # ACPI 解析
│   ├── timer.c          # 定时器框架
│   ├── ipi.c            # 处理器间中断
│   ├── power.c          # 电源管理（关机）
│   └── spinlock_arch.h  # x86_64 自旋锁实现
├── kernel/              # 架构无关代码
│   ├── main.c           # 内核主函数
│   ├── smp.c            # 多处理器支持
│   ├── device.c         # 设备驱动框架
│   ├── pmm.c            # 物理内存管理（伙伴系统）
│   ├── multiboot2.c     # Multiboot2 解析
│   └── list.h           # 双向链表
├── include/             # 公共头文件
│   ├── spinlock.h       # 自旋锁公共接口
│   ├── atomic.h         # 原子操作
│   ├── barrier.h        # 内存屏障
│   └── smp.h            # SMP 接口
├── tests/               # 测试代码
│   ├── boot/            # 启动集成测试
│   ├── smp/             # SMP 集成测试
│   ├── timer/           # 定时器集成测试
│   ├── spinlock/        # 内核测试代码
│   └── lib/             # 测试框架库
├── kernel.config        # 内核配置文件（默认）
├── .config              # 本地配置覆盖（不提交）
├── Makefile             # 构建系统
└── CLAUDE.md            # Claude Code 项目指南
```

---

## 启动流程

### BSP (Bootstrap Processor) 流程

1. **`_start`** (arch/x86_64/boot.S) - 32 位入口点
2. **BSP 识别** - 原子递增 `cpu_boot_counter`
3. **页表设置** - 配置 4 级页表结构
4. **GDT 加载** - 设置 64 位全局描述符表
5. **启用长模式** - 设置 CR4.PAE, IA32_EFER.LME, CR0.PG
6. **跳转到 64 位** - far jump 到 kernel_main()
7. **AP 启动** - 复制 trampoline 到 0x7000，发送 STARTUP IPI

### AP (Application Processor) 流程

1. **STARTUP IPI** - 接收启动向量
2. **Real Mode** - 从 0x7000 开始执行
3. **Protected Mode** - 加载 GDT32
4. **Long Mode** - 启用 PAE，加载 CR3，启用分页
5. **C 代码** - 跳转到 `ap_start()`
6. **初始化完成** - 输出 "[AP] CPU X initialized successfully"

---

## 当前状态

| 功能 | 状态 | 说明 |
|------|------|------|
| BSP 启动 | ✅ 完成 | 单核正常启动，带启动横幅 |
| AP 启动 | ✅ 完成 | 多核成功启动，状态管理完整 |
| 中断处理 | ✅ 完成 | IDT 和异常处理正常 |
| APIC 初始化 | ✅ 完成 | Local APIC 和 I/O APIC |
| APIC 定时器 | ✅ 完成 | 高频定时器中断，数学名言输出 |
| 设备驱动框架 | ✅ 完成 | probe/init/remove 模式 |
| 物理内存管理 | ✅ 完成 | 伙伴系统分配器，支持 Multiboot2 |
| 同步原语 | ✅ 完成 | 自旋锁、读写锁、IRQ 安全锁 |
| 测试框架 | ✅ 完成 | PMM 和自旋锁测试套件 |
| ACPI 解析 | ⚠️ 部分 | 暂时使用默认 APIC ID |
| RTC 定时器 | ❌ 已移除 | 因 QEMU 重置问题移除 |

---

## 技术亮点

### AP Trampoline 设计
AP 启动导引使用位置无关代码 (PIC)，通过 GOT 实现符号解析：
- **16-bit** → **32-bit** 使用 `data32 ljmp` 远跳转
- **32-bit** → **64-bit** 使用 `retf` 技巧
- 无需运行时修补，链接器填充 GOT
- **条件编译调试标记**：通过 `CONFIG_SMP_AP_DEBUG` 控制 H→G→3→A→P→L→X→D→S→Q→A→I→L→T→W 输出

### 伙伴系统内存分配器
基于 Linux 内核的 buddy 算法：
- **按阶分配** - 支持 2^0 到 2^9 页（4KB - 2MB）
- **自动合并** - 释放时自动合并相邻空闲块
- **分割分配** - 分配时自动分割大块
- **O(1) 操作** - 每个阶维护空闲链表

### 自旋锁系统
完整的 SMP 同步原语：
- **基础锁** - 使用 `xchg` 指令的 TAS 实现
- **IRQ 安全锁** - 保存/恢复 RFLAGS，防止中断死锁
- **读写锁** - 原子计数器实现，支持多读者
- **PAUSE 指令** - 降低自旋功耗
- **内存屏障修复**：Test 6 修复了 BSP 读取 test_counter 的内存可见性问题

### 汇编助记符优化
```assembly
/* 之前：硬编码字节 */
.byte 0x66
.byte 0xEA
.long 0x703D
.word 0x0008

/* 现在：可读的助记符 */
data32 ljmp $PM_CODE_SELECTOR, $pm_entry
```

### 设备驱动框架
Linux 风格的三阶段初始化：
1. **注册驱动** - `driver_register()`
2. **探测设备** - `device_probe()` (匹配 match_id/match_mask)
3. **初始化设备** - 按 `init_priority` 排序执行

### 配置系统
灵活的构建配置：
- **kernel.config** - 默认配置（提交到 git）
- **.config** - 本地覆盖（不提交）
- **命令行** - 临时覆盖 `make CONFIG_XXX=1`
- **条件编译** - 所有测试和调试功能可独立控制

---

## 关于 LLM 生成

本项目所有代码均通过 **Claude Code** (claude.ai/code) 生成：

- **零手写代码** - 所有 C/汇编代码均通过对话生成
- **自然语言驱动** - 使用中文描述需求，LLM 生成代码
- **迭代开发** - 通过调试输出逐步完善功能
- **探索性学习** - 理解操作系统原理与 LLM 辅助开发的结合

---

## 调试输出示例

```
▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓
░░░                                      ░░░
▓▓▓  [ Emergence Kernel ]  v0.1  ▓▓▓
░░░   > Learning with Every Boot   ░░░
▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓

PMM: Initializing...
BSP: Initializing...
APIC: LAPIC_VER = 0x00050014
APIC: APIC version=D maxlvt=5
APIC: Local APIC initialized
BSP: Initialization complete
CPU 0 (APIC ID 0): Successfully booted
SMP: Starting APs...
SMP: All APs startup complete. 1/1 APs ready
[ APIC tests ] 1. Mathematics is queen of sciences. - Gauss
[ APIC tests ] 2. Pure math is poetry of logic. - Einstein
[ APIC tests ] 3. Math reveals secrets to lovers. - Cantor
[ APIC tests ] 4. Proposing questions exceeds solving. - Cantor
[ APIC tests ] 5. God created natural numbers. - Kronecker
system is shutting down
SHUTDOWN: Port I/O failed, halting...
```

---

## 许可证

MIT License

---

## 参考资料

- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) - x86_64 架构手册
- [OSDev Wiki](https://wiki.osdev.org/) - 操作系统开发文档
- [Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html) - GRUB 引导协议

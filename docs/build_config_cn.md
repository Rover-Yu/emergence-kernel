# 构建配置系统

## 概述

Emergence Kernel 使用灵活的配置系统，允许您在编译时控制启用哪些功能。该系统支持：

- **默认配置** (`kernel.config`) - 版本控制的默认值
- **本地覆盖** (`.config`) - 每个开发者的设置（不提交到 git）
- **命令行** - 单次构建的临时覆盖

## 配置文件

### kernel.config

此文件包含默认配置值，并提交到版本控制。它作为所有构建的基线。

位置: `/opt/workbench/os/gckernel/claude/kernel.config`

### .config

此可选文件允许您为本地开发环境覆盖默认值。它**不会**提交到 git（参见 `.gitignore`）。

创建本地覆盖：
```bash
cp kernel.config .config
# 使用您偏好的设置编辑 .config
```

### 配置优先级

构建时，配置值按以下顺序解析（后面的值覆盖前面的值）：

1. **kernel.config** - 默认值
2. **.config** - 本地覆盖值
3. **命令行** - `make CONFIG_XXX=value` - 临时覆盖

示例：
```bash
# kernel.config 中: CONFIG_SPINLOCK_TESTS ?= 0
# .config 中: CONFIG_SPINLOCK_TESTS ?= 1
# 命令行: make CONFIG_SPINLOCK_TESTS=0
# 结果: 0（命令行获胜）
```

## 配置选项

### 测试配置

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `CONFIG_SPINLOCK_TESTS` | 0 | 启用完整的自旋锁测试套件 |
| `CONFIG_PMM_TESTS` | 0 | 启用物理内存管理器分配测试 |
| `CONFIG_APIC_TIMER_TEST` | 1 | 启用 APIC 定时器测试（输出数学名言） |

#### CONFIG_SPINLOCK_TESTS

启用在内核启动期间运行的自旋锁测试套件。启用后，内核将：

- 运行 10 个不同的自旋锁测试
- 测试单核和多核场景
- 验证基础锁、中断安全锁和读写锁
- 输出详细的测试结果

**启用时的输出：**
```
[ Spin lock tests ] Starting spin lock test suite...
[ Spin lock tests ] Test 1: Basic lock operations... PASSED
[ Spin lock tests ] Test 2: Trylock behavior... PASSED
...
[ Spin lock tests ] Result: ALL TESTS PASSED
```

#### CONFIG_PMM_TESTS

启用物理内存管理器测试套件。启用时，测试：

- 单页分配
- 多页（基于阶）分配
- 释放时的内存合并
- 统计准确性

**启用时的输出：**
```
[ PMM tests ] Running allocation tests...
[ PMM tests ] Allocated page1 at 0x200000, page2 at 0x201000
[ PMM tests ] Allocated 32KB block at 0x208000
[ PMM tests ] Tests complete
```

#### CONFIG_APIC_TIMER_TEST

启用通过周期性中断输出著名数学家名言的 APIC 定时器测试。

**启用时的输出：**
```
[ APIC tests ] 1. Mathematics is queen of sciences. - Gauss
[ APIC tests ] 2. Pure math is poetry of logic. - Einstein
[ APIC tests ] 3. Math reveals secrets to lovers. - Cantor
[ APIC tests ] 4. Proposing questions exceeds solving. - Cantor
[ APIC tests ] 5. God created natural numbers. - Kronecker
```

### 调试配置

| 选项 | 默认值 | 说明 |
|--------|---------|-------------|
| `CONFIG_SMP_AP_DEBUG` | 0 | 在串口输出上启用 AP 启动调试标记 |

#### CONFIG_SMP_AP_DEBUG

启用后，在 AP（应用处理器）启动期间输出调试标记。每个标记代表启动过程的一个阶段：

| 标记 | 阶段 | 说明 |
|------|------|------|
| H | 实模式 | 导引导入入口，栈设置完成 |
| G | GDT32 | 加载 32 位 GDT |
| 3 | 保护模式 | 进入 32 位保护模式 |
| A | PAE | 启用物理地址扩展 |
| P | 页表 | 加载 CR3，刷新 TLB |
| L | 长模式 | 设置 IA32_EFER.LME |
| X | 分页 | 设置 CR0.PG（长模式激活） |
| D | GDT64 | 加载 64 位 GDT |
| S | 桩架 | 到达 64 位桩架入口 |
| Q | 栈 | AP 栈配置完成 |
| A | 段寄存器 | 清除段寄存器 |
| I | IDT 加载 | IDT 加载前 |
| L | IDT 完成 | IDT 加载后 |
| T | 跳转 | 即将跳转到 ap_start |
| W | 桥接 | 到达 test_ap_start 桥接函数 |

**示例输出：** `HG3APLXDSQAILTW`

## 使用示例

### 使用默认值构建

```bash
make
# 使用 kernel.config 中的所有值
```

### 使用自定义选项构建

```bash
# 仅为此构建启用自旋锁测试
make CONFIG_SPINLOCK_TESTS=1

# 启用 PMM 测试和 AP 调试标记
make CONFIG_PMM_TESTS=1 CONFIG_SMP_AP_DEBUG=1

# 禁用 APIC 定时器测试
make CONFIG_APIC_TIMER_TEST=0
```

### 创建持久本地配置

```bash
# 复制默认配置
cp kernel.config .config

# 使用您偏好的设置编辑
nano .config
```

示例 `.config`：
```makefile
# 我的开发配置
CONFIG_SPINLOCK_TESTS ?= 1
CONFIG_PMM_TESTS ?= 1
CONFIG_APIC_TIMER_TEST ?= 0
CONFIG_SMP_AP_DEBUG ?= 1
```

### 检查活动配置

查看构建中使用的配置值：

```bash
make clean
make 2>&1 | grep "CONFIG_"
```

示例输出：
```
gcc ... -DCONFIG_SPINLOCK_TESTS=0 -DCONFIG_PMM_TESTS=0 -DCONFIG_SMP_AP_DEBUG=0 -DCONFIG_APIC_TIMER_TEST=1 -c ...
```

## 高级用法

### 代码中的条件编译

配置系统使用 C 预处理器条件。在您的代码中：

```c
#if CONFIG_SPINLOCK_TESTS
    // 仅当 CONFIG_SPINLOCK_TESTS=1 时编译此代码
    run_spinlock_tests();
#endif

#if CONFIG_SMP_AP_DEBUG
    serial_puts("调试: AP 到达阶段 X\n");
#endif
```

### 添加新的配置选项

添加新的配置选项：

1. **添加到 `kernel.config`：**
   ```makefile
   # 设置为 1 启用我的功能，0 禁用
   CONFIG_MY_FEATURE ?= 0
   ```

2. **添加到 `Makefile` CFLAGS：**
   ```makefile
   CFLAGS += -DCONFIG_MY_FEATURE=$(CONFIG_MY_FEATURE)
   ```

3. **在代码中使用条件编译：**
   ```c
   #if CONFIG_MY_FEATURE
       my_feature_code();
   #endif
   ```

4. **在此文件和 README 中记录**

## 测试

### 验证配置更改

修改配置后，验证更改生效：

```bash
# 清理构建以确保所有文件重新编译
make clean

# 构建并检查编译器标志
make 2>&1 | grep -E "CONFIG_"

# 运行并验证预期行为
make run
```

### 推荐的测试配置

**最小化（最快启动，无测试）：**
```bash
# .config
CONFIG_SPINLOCK_TESTS ?= 0
CONFIG_PMM_TESTS ?= 0
CONFIG_APIC_TIMER_TEST ?= 0
CONFIG_SMP_AP_DEBUG ?= 0
```

**开发（启用大多数测试）：**
```bash
# .config
CONFIG_SPINLOCK_TESTS ?= 1
CONFIG_PMM_TESTS ?= 1
CONFIG_APIC_TIMER_TEST ?= 1
CONFIG_SMP_AP_DEBUG ?= 0
```

**调试（完整调试输出）：**
```bash
# .config
CONFIG_SPINLOCK_TESTS ?= 1
CONFIG_PMM_TESTS ?= 1
CONFIG_APIC_TIMER_TEST ?= 1
CONFIG_SMP_AP_DEBUG ?= 1
```

## 故障排除

### 配置未生效

如果配置更改似乎不起作用：

1. **清理构建：**
   ```bash
   make clean
   make
   ```

2. **检查编译器标志：**
   ```bash
   make 2>&1 | grep "CONFIG_"
   ```

3. **验证文件优先级：**
   - 检查 `.config` 是否存在并包含您的更改
   - 检查命令行是否覆盖了您的设置

4. **检查语法：**
   - 使用 `?=` 进行延迟赋值
   - 不要在 Makefile 中的 `=` 周围使用空格

### 意外的测试输出

如果您看到测试输出但未启用测试：

1. 检查过时的 `.config` 文件：
   ```bash
   cat .config
   rm .config  # 如有需要则删除
   ```

2. 验证 `kernel.config` 值：
   ```bash
   cat kernel.config
   ```

3. 从干净状态重新构建：
   ```bash
   make clean
   make
   ```

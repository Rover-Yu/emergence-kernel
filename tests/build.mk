# Emergence Kernel - Test Build Configuration
#
# This file contains all test source definitions and compilation rules.
# Included from the main Makefile.
#
# Directory structure:
#   tests/spinlock/          - Spinlock tests
#   tests/slab/              - Slab allocator tests
#   tests/pmm/               - Physical memory manager tests
#   tests/timer/             - APIC timer tests
#   tests/boot/              - Boot verification tests
#   tests/smp/               - SMP tests
#   tests/pcd/               - Page Control Data tests
#   tests/minilibc/          - Minilibc tests
#   tests/usermode/          - User mode tests
#   tests/kmap/              - KMAP memory region tracking tests
#   tests/nested-kernel/     - All Nested Kernel tests

TESTS_DIR := tests

# ============================================================================
# Test Source Definitions
# ============================================================================

# Spinlock test (always compiled - provides stubs when disabled)
SPINLOCK_TEST_SRC := tests/spinlock/spinlock_test.c
SPINLOCK_TEST_OBJ := $(BUILD_DIR)/kernel_spinlock_test.o

# Slab allocator test (conditionally compiled)
SLAB_TEST_SRC := tests/slab/slab_test.c
SLAB_TEST_OBJ := $(BUILD_DIR)/kernel_slab_test.o

# PMM test (conditionally compiled)
PMM_TEST_SRC := tests/pmm/pmm_test.c
PMM_TEST_OBJ := $(BUILD_DIR)/kernel_pmm_test.o

# APIC timer test (conditionally compiled)
TIMER_TEST_SRC := tests/timer/timer_test.c
TIMER_TEST_OBJ := $(BUILD_DIR)/kernel_timer_test.o

# Boot test (conditionally compiled)
BOOT_TEST_SRC := tests/boot/boot_test.c
BOOT_TEST_OBJ := $(BUILD_DIR)/kernel_boot_test.o

# SMP boot test (conditionally compiled)
SMP_TEST_SRC := tests/smp/smp_boot_test.c
SMP_TEST_OBJ := $(BUILD_DIR)/kernel_smp_test.o

# PCD test (conditionally compiled)
PCD_TEST_SRC := tests/pcd/pcd_test.c
PCD_TEST_OBJ := $(BUILD_DIR)/kernel_pcd_test.o

# Minilibc test (always compiled - provides stubs when disabled)
MINILIBC_TEST_SRC := tests/minilibc/minilibc_test.c
MINILIBC_TEST_OBJ := $(BUILD_DIR)/minilibc_test.o

# User mode test (always compiled - provides stubs when disabled)
USERMODE_TEST_SRC := tests/usermode/usermode_test.c
USERMODE_TEST_OBJ := $(BUILD_DIR)/kernel_usermode_test.o

# Syscall test (always compiled - provides stubs when disabled)
SYSCALL_TEST_SRC := $(ARCH_DIR)/syscall_test.S
SYSCALL_TEST_OBJ := $(BUILD_DIR)/syscall_test.o

# ============================================================================
# Nested Kernel Test Sources (tests/nested-kernel/)
# ============================================================================

# NK fault injection test (always compiled - provides stubs when disabled)
NK_FAULT_INJECTION_TEST_SRC := tests/nested-kernel/nk_fault_injection_test.c
NK_FAULT_INJECTION_TEST_OBJ := $(BUILD_DIR)/nk_fault_injection_test.o

# NK invariants test (conditionally compiled)
NK_INVARIANTS_TEST_SRC := tests/nested-kernel/nk_invariants_test.c
NK_INVARIANTS_TEST_OBJ := $(BUILD_DIR)/kernel_nk_invariants_test.o

# NK read-only visibility test (always compiled - provides stubs when disabled)
NK_READONLY_VISIBILITY_TEST_SRC := tests/nested-kernel/nk_readonly_visibility_test.c
NK_READONLY_VISIBILITY_TEST_OBJ := $(BUILD_DIR)/kernel_readonly_visibility_test.o

# NK SMP monitor stress test (always compiled - provides stubs when disabled)
NK_SMP_MONITOR_STRESS_TEST_SRC := tests/nested-kernel/nk_smp_monitor_stress_test.c
NK_SMP_MONITOR_STRESS_TEST_OBJ := $(BUILD_DIR)/nk_smp_monitor_stress_test.o

# NK monitor trampoline test (always compiled - provides stubs when disabled)
NK_MONITOR_TRAMPOLINE_TEST_SRC := tests/nested-kernel/nk_monitor_trampoline_test.c
NK_MONITOR_TRAMPOLINE_TEST_OBJ := $(BUILD_DIR)/nk_monitor_trampoline_test.o

# NK invariants verify test (always compiled - provides stubs when disabled)
NK_INVARIANTS_VERIFY_TEST_SRC := tests/nested-kernel/nk_invariants_verify_test.c
NK_INVARIANTS_VERIFY_TEST_OBJ := $(BUILD_DIR)/nk_invariants_verify_test.o

# Scheduler test (conditionally compiled)
SCHED_TEST_SRC := tests/sched/sched_test.c
SCHED_TEST_OBJ := $(BUILD_DIR)/kernel_sched_test.o

# Syscall test (conditionally compiled)
SYSCALL_C_TEST_SRC := tests/syscall/syscall_test.c
SYSCALL_C_TEST_OBJ := $(BUILD_DIR)/kernel_syscall_test.o

# KMAP test (conditionally compiled)
KMAP_TEST_SRC := tests/kmap/kmap_test.c
KMAP_TEST_OBJ := $(BUILD_DIR)/kernel_kmap_test.o

# ============================================================================
# Test Objects Assembly
# ============================================================================

# Always-compiled tests (provide stubs when disabled)
TESTS_OBJS := $(SPINLOCK_TEST_OBJ)
TESTS_OBJS += $(NK_FAULT_INJECTION_TEST_OBJ)
TESTS_OBJS += $(NK_READONLY_VISIBILITY_TEST_OBJ)
TESTS_OBJS += $(NK_SMP_MONITOR_STRESS_TEST_OBJ)
TESTS_OBJS += $(NK_MONITOR_TRAMPOLINE_TEST_OBJ)
TESTS_OBJS += $(NK_INVARIANTS_VERIFY_TEST_OBJ)
TESTS_OBJS += $(USERMODE_TEST_OBJ)
TESTS_OBJS += $(MINILIBC_TEST_OBJ)
TESTS_OBJS += $(SYSCALL_TEST_OBJ)


# Conditionally compiled tests
ifeq ($(CONFIG_TESTS_PMM),1)
TESTS_OBJS += $(PMM_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_SLAB),1)
TESTS_OBJS += $(SLAB_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_APIC_TIMER),1)
TESTS_OBJS += $(TIMER_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_BOOT),1)
TESTS_OBJS += $(BOOT_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_SMP),1)
TESTS_OBJS += $(SMP_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_PCD),1)
TESTS_OBJS += $(PCD_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_NK_INVARIANTS),1)
TESTS_OBJS += $(NK_INVARIANTS_TEST_OBJ)
endif

ifeq ($(CONFIG_TESTS_SCHED),1)
TESTS_OBJS += $(SCHED_TEST_OBJ)
endif

ifeq ($(CONFIG_TESTS_SYSCALL),1)
TESTS_OBJS += $(SYSCALL_C_TEST_OBJ)
endif

ifeq ($(CONFIG_TESTS_KMAP),1)
TESTS_OBJS += $(KMAP_TEST_OBJ)
endif

# ============================================================================
# Test Compilation Rules
# ============================================================================

# Always-compiled tests (provide stubs when disabled)
$(SPINLOCK_TEST_OBJ): $(SPINLOCK_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(NK_FAULT_INJECTION_TEST_OBJ): $(NK_FAULT_INJECTION_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(NK_READONLY_VISIBILITY_TEST_OBJ): $(NK_READONLY_VISIBILITY_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(NK_SMP_MONITOR_STRESS_TEST_OBJ): $(NK_SMP_MONITOR_STRESS_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(NK_MONITOR_TRAMPOLINE_TEST_OBJ): $(NK_MONITOR_TRAMPOLINE_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(NK_INVARIANTS_VERIFY_TEST_OBJ): $(NK_INVARIANTS_VERIFY_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(USERMODE_TEST_OBJ): $(USERMODE_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(MINILIBC_TEST_OBJ): $(MINILIBC_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Syscall test program compilation rule (assembly file)
$(SYSCALL_TEST_OBJ): $(SYSCALL_TEST_SRC) | $(BUILD_DIR)
	@echo "  AS      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@


# Conditionally compiled tests
ifeq ($(CONFIG_TESTS_PMM),1)
$(PMM_TEST_OBJ): $(PMM_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

ifeq ($(CONFIG_TESTS_SLAB),1)
$(SLAB_TEST_OBJ): $(SLAB_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

ifeq ($(CONFIG_TESTS_APIC_TIMER),1)
$(TIMER_TEST_OBJ): $(TIMER_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

ifeq ($(CONFIG_TESTS_BOOT),1)
$(BOOT_TEST_OBJ): $(BOOT_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

ifeq ($(CONFIG_TESTS_SMP),1)
$(SMP_TEST_OBJ): $(SMP_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

ifeq ($(CONFIG_TESTS_PCD),1)
$(PCD_TEST_OBJ): $(PCD_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

ifeq ($(CONFIG_TESTS_NK_INVARIANTS),1)
$(NK_INVARIANTS_TEST_OBJ): $(NK_INVARIANTS_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

ifeq ($(CONFIG_TESTS_SCHED),1)
$(SCHED_TEST_OBJ): $(SCHED_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

ifeq ($(CONFIG_TESTS_SYSCALL),1)
$(SYSCALL_C_TEST_OBJ): $(SYSCALL_C_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

ifeq ($(CONFIG_TESTS_KMAP),1)
$(KMAP_TEST_OBJ): $(KMAP_TEST_SRC) $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

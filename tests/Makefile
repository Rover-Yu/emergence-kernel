# Emergence Kernel Test Targets
#
# This Makefile contains all test-related targets.
# Included from the main Makefile.
#
# Usage: make tests-<name>
#
# Available tests:
#   tests                 - Run all enabled tests
#   tests-boot            - Basic kernel boot verification
#   tests-apic-timer      - APIC timer interrupt tests
#   tests-smp             - SMP startup and multi-CPU verification
#   tests-pcd             - Page Control Data tests
#   tests-slab            - Slab allocator tests
#   tests-nk              - Run all Nested Kernel tests
#   tests-nk-invariants   - Nested Kernel invariants (ASPLOS '15)
#   tests-nk-fault-injection - Nested Kernel fault injection (destructive)
#   tests-nk-readonly-visibility - Read-only mapping visibility
#   tests-nk-smp-monitor-stress - SMP monitor stress test
#   tests-usermode        - User mode syscall and ring 3 execution
#   tests-multiboot       - Multiboot2 header verification
#   tests-minilibc        - Minilibc string/memory functions

# Test targets use KERNEL_CMDLINE to select which tests to run
# The 'tests=' parameter is parsed by kernel/test.c
# Since this file is included from the main Makefile, we run in the project root

.PHONY: tests tests-boot tests-apic-timer tests-smp tests-pcd tests-slab \
        tests-nk tests-nk-invariants tests-nk-fault-injection tests-nk-readonly-visibility \
        tests-nk-smp-monitor-stress tests-usermode tests-multiboot tests-minilibc \
        test test-boot test-apic-timer test-smp test-pcd test-slab \
        test-nk test-nk-invariants test-nk-fault-injection test-nk-readonly-visibility \
        test-nk-smp-monitor-stress test-usermode test-multiboot test-minilibc

# Backward-compatible aliases (singular form)
test: tests
test-boot: tests-boot
test-apic-timer: tests-apic-timer
test-smp: tests-smp
test-pcd: tests-pcd
test-slab: tests-slab
test-nk: tests-nk
test-nk-invariants: tests-nk-invariants
test-nk-fault-injection: tests-nk-fault-injection
test-nk-readonly-visibility: tests-nk-readonly-visibility
test-nk-smp-monitor-stress: tests-nk-smp-monitor-stress
test-usermode: tests-usermode
test-multiboot: tests-multiboot
test-minilibc: tests-minilibc

# Default target: run all tests
tests:
	@echo "Running Emergence Kernel test suite..."
	@$(MAKE) KERNEL_CMDLINE="tests=all" all run

# Basic tests
tests-boot:
	@echo "Running Basic Kernel Boot Test..."
	@$(MAKE) KERNEL_CMDLINE="tests=boot" all run

tests-apic-timer:
	@echo "Running APIC Timer Test..."
	@$(MAKE) KERNEL_CMDLINE="tests=timer" all run

tests-smp:
	@echo "Running SMP Boot Test..."
	@$(MAKE) KERNEL_CMDLINE="tests=smp" all run

tests-pcd:
	@echo "Running Page Control Data (PCD) Test..."
	@$(MAKE) KERNEL_CMDLINE="tests=pcd" all run

tests-slab:
	@echo "Running Slab Allocator Test..."
	@$(MAKE) KERNEL_CMDLINE="tests=slab" all run

# Nested Kernel tests (all use nk_ prefix)
tests-nk:
	@echo "Running all Nested Kernel tests..."
	@$(MAKE) \
		CONFIG_TESTS_NK_FAULT_INJECTION=1 \
		CONFIG_TESTS_NK_READONLY_VISIBILITY=1 \
		CONFIG_TESTS_NK_TRAMPOLINE=1 \
		KERNEL_CMDLINE="tests=nk_invariants,nk_fault_injection,nk_readonly_visibility,nk_trampoline" \
		all run

tests-nk-invariants:
	@echo "Running Nested Kernel Invariants Test..."
	@$(MAKE) KERNEL_CMDLINE="tests=nk_invariants" all run

tests-nk-fault-injection:
	@echo "Running Nested Kernel Fault Injection Test..."
	@$(MAKE) CONFIG_TESTS_NK_FAULT_INJECTION=1 KERNEL_CMDLINE="tests=nk_fault_injection" all run

tests-nk-readonly-visibility:
	@echo "Running NK Read-Only Visibility Test..."
	@$(MAKE) CONFIG_TESTS_NK_READONLY_VISIBILITY=1 KERNEL_CMDLINE="tests=nk_readonly_visibility" all run

tests-nk-smp-monitor-stress:
	@echo "Running NK SMP Monitor Stress Test..."
	@$(MAKE) CONFIG_TESTS_SMP_MONITOR_STRESS=1 KERNEL_CMDLINE="tests=nk_smp_monitor_stress" all run

# Other tests
tests-usermode:
	@echo "Running User Mode Syscall Test..."
	@$(MAKE) KERNEL_CMDLINE="tests=usermode" all run

tests-multiboot:
	@echo "Running Multiboot2 Header Test..."
	@$(MAKE) all
	@python3 tests/multiboot/multiboot_header_test.py

tests-minilibc:
	@echo "Running Minilibc String Library Test..."
	@$(MAKE) KERNEL_CMDLINE="tests=minilibc" all run

/* Emergence Kernel - Test Cases Header
 *
 * Centralized header including all test wrapper declarations.
 * Source files that invoke test wrappers should include this header.
 */

#ifndef TESTCASES_H
#define TESTCASES_H

/* Core tests */
#include "tests/boot/test_boot.h"
#include "tests/pmm/test_pmm.h"
#include "tests/slab/test_slab.h"
#include "tests/spinlock/test_spinlock.h"
#include "tests/timer/test_timer.h"
#include "tests/smp/test_smp.h"
#include "tests/pcd/test_pcd.h"
#include "tests/minilibc/test_minilibc.h"
#include "tests/usermode/test_usermode.h"
#include "tests/syscall/test_syscall.h"
#include "tests/kmap/test_kmap.h"

/* Nested Kernel tests */
#include "tests/nested-kernel/test_nk_invariants.h"
#include "tests/nested-kernel/test_nk_fault_injection.h"
#include "tests/nested-kernel/test_nk_readonly_visibility.h"
#include "tests/nested-kernel/test_nk_smp_monitor_stress.h"
#include "tests/nested-kernel/test_nk_monitor_trampoline.h"
#include "tests/nested-kernel/test_nk_invariants_verify.h"

#endif /* TESTCASES_H */

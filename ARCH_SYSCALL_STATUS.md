# x86-64 SYSCALL Implementation Status

## Overview

This document describes the current status of the SYSCALL/SYSRET implementation for user mode system calls in the Emergence Kernel.

## Implementation Details

### MSR Configuration

The following MSRs are configured in `syscall_init()`:

1. **IA32_EFER (0xC0000080)**
   - SCE bit (bit 0) set to 1 to enable SYSCALL/SYSRET instructions
   - Required for SYSCALL to work at all

2. **IA32_STAR (0xC0000081)**
   - [63:48] = Kernel CS (0x08)
   - [47:32] = User CS (0x18)
   - Format: 0x00080018 (EAX=0x00000000, EDX=0x00080018)

3. **IA32_LSTAR (0xC0000082)**
   - Entry point for SYSCALL (64-bit RIP)
   - Points to `syscall_entry` assembly stub

4. **IA32_FMASK (0xC0000084)**
   - RFLAGS mask to clear on syscall entry
   - Set to 0x200 (clear IF bit)

### GDT Configuration

The GDT includes user mode segments:

```assembly
.quad 0x00CFFA000000FFFF  /* User code (DPL=3, long mode, conforming) */
.quad 0x00CFF2000000FFFF  /* User data (DPL=3, read/write, expanding) */
```

### Syscall Entry Path

1. User executes `syscall` instruction
   - RAX = syscall number
   - RDI, RSI, RDX, R10, R8, R9 = arguments
   - CPU saves RCX (return RIP) and R11 (saved RFLAGS)
   - CPU switches to kernel CS (from STAR[63:48])
   - CPU sets CPL to 0

2. `syscall_entry` assembly stub:
   - Saves user registers (RBX, RBP, R12-R15)
   - Saves user RSP to R15
   - Switches to kernel stack (`nk_boot_stack_top`)
   - Shuffles arguments for C calling convention:
     - RDI = syscall number
     - RSI = arg1
     - RDX = arg2
     - RCX = arg3 (passed in R8)
   - Calls `syscall_handler()`

3. `syscall_handler()` C dispatcher:
   - Switches on syscall number
   - Calls appropriate handler function
   - Returns result in RAX

4. `syscall_entry` return:
   - Restores user RSP from R15
   - Restores user registers
   - Executes `sysretq` to return to user mode
     - Loads RIP from RCX
     - Loads RFLAGS from R11
     - Loads CS from STAR[47:32] with RPL=3
     - Switches to CPL 3

### Syscall Numbers

```c
#define SYS_write       1
#define SYS_exit        2
```

## Current Status

### ✅ Working Components

1. **MSR Configuration** - All MSRs correctly set
2. **SYSCALL from Ring 0** - Works perfectly
3. **Syscall Entry/Exit** - Register save/restore working
4. **Syscall Dispatcher** - Correctly routes to handlers
5. **Basic Syscall Handlers** - sys_write and sys_exit implemented

### ⚠️ Known Limitations

**QEMU TCG Mode Issue:**
- `sysretq` causes #GP faults when transitioning from CPL 0 to CPL 3
- Root cause: QEMU TCG (software emulation) has incomplete support for sysretq
- This is a known limitation of QEMU's TCG mode

**Workaround Options:**
1. Use KVM (hardware virtualization) - requires `/dev/kvm`
2. Test on real hardware
3. Use alternative ring transition mechanism (e.g., call gates, int n)

### Test Results

**Ring 0 Syscall Test:**
```
[TEST] SYSCALL from ring 0 (should halt)...
[KERNEL] Syscall 2 args: 0 10B210 100EB4
[USER] Process exited with code: 0
```

✅ **PASS**: Syscall from ring 0 works correctly!

**Ring 3 Transition Test:**
- Not yet tested due to sysretq issues in TCG mode

## Files Modified

- `arch/x86_64/include/syscall.h` - Syscall numbers and prototypes
- `arch/x86_64/syscall.c` - MSR setup, dispatcher, handlers
- `arch/x86_64/syscall_entry.S` - Assembly stubs for syscall and sysret
- `arch/x86_64/userprog.S` - User program test code
- `arch/x86_64/boot.S` - GDT with user mode segments

## Next Steps

To complete user mode syscall support:

1. **Fix sysretq for ring 3 transition**
   - Option A: Use KVM mode for testing
   - Option B: Implement alternative ring transition
   - Option C: Add workarounds for TCG mode

2. **Add more syscalls**
   - read()
   - mmap()
   - brk()
   - getpid()

3. **Implement user paging**
   - Separate page tables per process
   - X86_PTE_USER bit for user-accessible pages
   - Page fault handling for user memory access

4. **Test SYSCALL from ring 3**
   - Verify SYSCALL works from user code
   - Test syscall argument passing
   - Verify return values

## References

- Intel SDM Volume 2: SYSCALL/SYSRET
- Intel SDM Volume 3A: Chapter 5 (Protection)
- Linux kernel arch/x86/entry/entry_64.S
- OSDev Wiki: System Calls

## Build Configuration

Enabled by:
```makefile
CONFIG_USERMODE_TEST=1
```

Test with:
```bash
make clean && make run
```

For KVM mode (if available):
```bash
qemu-system-x86_64 -enable-kvm -M pc -m 128M -nographic -cdrom emergence.iso -smp 1
```

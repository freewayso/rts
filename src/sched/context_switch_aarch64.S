#
# AArch64 (ARM64) context switch for Permafrost fiber scheduler.
# ABI: AAPCS64 — x0=save, x1=restore, x2=retval, x3=arg
#
# Layout of struct context (must match sched.c AArch64 branch):
#   0x00 x19, 0x08 x20, 0x10 x21, 0x18 x22
#   0x20 x23, 0x28 x24, 0x30 x25, 0x38 x26
#   0x40 x27, 0x48 x28, 0x50 x29(fp), 0x58 x30(lr)
#   0x60 sp
#

#if defined(__aarch64__)

.text
.global sched_switch_ctx
.global sched_task_exit_trampoline
.type sched_switch_ctx, %function
.type sched_task_exit_trampoline, %function

sched_switch_ctx:
    /* save callee-saved + fp/lr/sp into x0 */
    stp x19, x20, [x0, #0x00]
    stp x21, x22, [x0, #0x10]
    stp x23, x24, [x0, #0x20]
    stp x25, x26, [x0, #0x30]
    stp x27, x28, [x0, #0x40]
    stp x29, x30, [x0, #0x50]
    mov x4, sp
    str x4, [x0, #0x60]

    /* load from x1 */
    ldp x19, x20, [x1, #0x00]
    ldp x21, x22, [x1, #0x10]
    ldp x23, x24, [x1, #0x20]
    ldp x25, x26, [x1, #0x30]
    ldp x27, x28, [x1, #0x40]
    ldp x29, x30, [x1, #0x50]
    ldr x4, [x1, #0x60]
    mov sp, x4

    /* return value in x0; arg for trampoline conventions in x1 */
    mov x0, x2
    mov x1, x3
    ret

sched_task_exit_trampoline:
    /* x0 holds task result pointer (same idea as x86_64 SysV) */
    b sched_task_exit

#endif /* __aarch64__ */

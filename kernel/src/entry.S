
     .section .text.entry
     .globl _start
_start:
    li t0, 7
    bgeu a0, t0, .Lhalt
    la sp, boot_stack_lower_bound
    addi t0, a0, 1
    slli t0, t0, 14
    add sp, sp, t0
    call kernel_entry

.Lhalt:
    wfi
    j .Lhalt

    .section .bss.stack
    .globl boot_stack_lower_bound
boot_stack_lower_bound:
    .space 16384 * 7
    .globl boot_stack_top
boot_stack_top:

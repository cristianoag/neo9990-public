/* SPDX-License-Identifier: GPL-2.0-only */
.section .text.boot, "ax"
.global _start
_start:
    b boot
    .long 0
    .quad 0x02000000
    .quad __image_end - _start
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .long 0x644d5241
    .long 0
boot:
    msr daifset, #15
    mov x20, x0
    mrs x1, CurrentEL
    cmp x1, #8
    b.ne el1_check
    mov x1, #(1 << 31)
    msr hcr_el2, x1
    mov x1, #3
    msr cnthctl_el2, x1
    msr cntvoff_el2, xzr
    msr cptr_el2, xzr
    ldr x1, =0x30d00800
    msr sctlr_el1, x1
    ldr x1, =__stack_top
    msr sp_el1, x1
    adr x1, el1_entry
    msr elr_el2, x1
    mov x1, #0x3c5
    msr spsr_el2, x1
    eret
el1_check:
    cmp x1, #4
    b.ne park
el1_entry:
    mrs x1, sctlr_el1
    tst x1, #1
    b.ne park
    tst x1, #4
    b.ne park
    msr spsel, #1
    ldr x1, =__stack_top
    mov sp, x1
    adr x1, vectors
    msr vbar_el1, x1
    isb
    ldr x1, =__bss_start
    ldr x2, =__bss_end
zero_bss:
    cmp x1, x2
    b.hs enter_c
    str xzr, [x1], #8
    b zero_bss
enter_c:
    mov x0, x20
    bl neo_kernel
park:
    wfe
    b park

.section .vectors, "ax"
.balign 2048
vectors:
.rept 16
    b exception
    .balign 128
.endr
exception:
    mrs x0, esr_el1
    mrs x1, elr_el1
    mrs x2, far_el1
    bl neo_exception
    b park

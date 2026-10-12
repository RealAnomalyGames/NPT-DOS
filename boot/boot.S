
.section .multiboot
.align 4

.set MB_MAGIC,    0x1BADB002
.set MB_FLAGS,    0x00000003
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text.boot
.global _start
.type _start, @function

.extern kernel_main
.extern __bss_start
.extern __bss_end

_start:
    cli
    cld

    /*
     * Clear BSS before using the stack.
     * This is required when booting the installed
     * kernel directly, without GRUB.
     */
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb

    /* QEMU diagnostic: kernel startup and BSS clear completed. */
    movb $'K', %al
    outb %al, $0xE9

    /* Initialize the kernel stack after clearing BSS. */
    movl $stack_top, %esp
    xorl %ebp, %ebp

    call kernel_main

halt_loop:
    cli
    hlt
    jmp halt_loop

.size _start, . - _start

.section .note.GNU-stack,"",@progbits

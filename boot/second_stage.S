
/*
 * NPT-DOS Build 014-E
 * BIOS second-stage loader
 *
 * Loaded at physical address 0x8000.
 *
 * Reads 256 sectors (128 KiB) from LBA 64
 * to physical address 0x10000.
 *
 * Copies the flat kernel image to 0x100000
 * and transfers control to its entry point.
 *
 * Requirements:
 *   - BIOS INT 13h extensions
 *   - Flat kernel linked at 1 MiB
 *   - Kernel image padded to 128 KiB
 *   - Boot sector loads this stage at 0000:8000
 */

.section .text
.code16
.global stage2_start

stage2_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    sti

    movb $16, sectors_remaining
    movl $64, current_lba
    movw $0x1000, destination_segment

load_kernel:
    /*
     * Read 16 sectors per request.
     * Advance the destination by 8192 bytes.
     */
    movw destination_segment, %ax
    movw %ax, dap_segment

    movl current_lba, %eax
    movl %eax, dap_lba

    movb $0x42, %ah
    movb boot_drive, %dl
    movw $dap, %si
    int $0x13
    jc disk_error

    addl $16, current_lba
    addw $0x0200, destination_segment

    decb sectors_remaining
    jnz load_kernel

    /*
     * Enable A20 through the fast A20 gate.
     */
    inb $0x92, %al
    orb $0x02, %al
    andb $0xFE, %al
    outb %al, $0x92

    /*
     * Enter 32-bit protected mode.
     */
    cli
    lgdt gdt_descriptor

    movl %cr0, %eax
    orl $0x00000001, %eax
    movl %eax, %cr0

    ljmpl $0x08, $protected_mode_entry

disk_error:
    movw $error_message, %si
    call print_string

halt:
    cli
    hlt
    jmp halt

print_string:
    lodsb
    testb %al, %al
    jz print_done
    movb $0x0E, %ah
    movb $0x07, %bh
    int $0x10
    jmp print_string

print_done:
    ret

boot_drive:
    .byte 0

sectors_remaining:
    .byte 0

.align 2
current_lba:
    .long 0

destination_segment:
    .word 0

error_message:
    .asciz "NPT-DOS: Kernel read failed. Halted."

.align 4
dap:
    .byte 0x10
    .byte 0x00
    .word 16
    .word 0x0000
dap_segment:
    .word 0x1000
dap_lba:
    .quad 64

.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF

gdt_end:

gdt_descriptor:
    .word gdt_end - gdt - 1
    .long gdt

.code32
protected_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    movl $0x90000, %esp
    cld

    /*
     * Copy 128 KiB from 0x10000 to 0x100000.
     * 32768 dwords = 131072 bytes.
     */
    movl $0x10000, %esi
    movl $0x100000, %edi
    movl $32768, %ecx
    rep movsl

    /*
     * The flat kernel's entry point is linked at 1 MiB.
     */
    movl $0x100000, %eax
    jmp *%eax

stage2_halt:
    cli
    hlt
    jmp stage2_halt

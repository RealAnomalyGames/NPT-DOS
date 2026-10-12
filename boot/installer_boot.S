/*
 * NPT-DOS Build 014-E
 * BIOS boot sector
 *
 * Loaded by BIOS at physical address 0x7C00.
 * Loads 16 sectors from LBA 32 to physical 0x8000.
 *
 * Handoff contract:
 *   Physical address 0x0500 contains the BIOS boot drive.
 */

.section .text
.code16
.global _start

_start:
    cli

    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7000, %sp

    /* Normalize CS to 0000 before using absolute addresses. */
    ljmp $0x0000, $normalized

normalized:
    cld

    /* Preserve the BIOS-provided drive number at physical 0x0500. */
    movb %dl, 0x0500

    movw $loading_message, %si
    call print_string

    /* BIOS INT 13h extensions: read LBA 32, 16 sectors to 0000:8000. */
    movw $dap, %si
    movb 0x0500, %dl
    movb $0x42, %ah
    int $0x13
    jc disk_error

    /* Continue execution in the second stage. */
    ljmp $0x0000, $0x8000

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

loading_message:
    .ascii "NPT-DOS: Loading system..."
    .byte 13, 10, 0

error_message:
    .ascii "NPT-DOS: Disk read failed."
    .byte 13, 10, 0

.align 4
dap:
    .byte 0x10
    .byte 0x00
    .word 16
    .word 0x8000
    .word 0x0000
    .quad 32

.org 510
.word 0xAA55

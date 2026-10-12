/*
 * NPT-DOS Build 014-E
 * BIOS hard-disk boot sector
 *
 * Build as a raw 512-byte boot sector.
 * Second-stage loader: LBA 32, 16 sectors.
 *
 * IMPORTANT:
 * This is a boot-sector source file, not yet an
 * integrated or tested installer.
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
    movw $0x7C00, %sp
    sti

    /* Preserve BIOS boot drive. */
    movb %dl, boot_drive

    /* Normalize CS with a far jump. */
    ljmp $0x0000, $normalized

normalized:
    movw $0x0000, %ax
    movw %ax, %ds
    movw %ax, %es

    movw $loading_message, %si
    call print_string

    /* Read 16 sectors from LBA 32 to 0000:8000. */
    movb boot_drive, %dl
    movw $dap, %si
    movb $0x42, %ah
    int $0x13
    jc disk_error

    /* Transfer control to the second-stage loader. */
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

boot_drive:
    .byte 0

loading_message:
    .asciz "NPT-DOS: Loading system...\r\n"

error_message:
    .asciz "NPT-DOS: Disk read failed.\r\n"

.align 4
dap:
    .byte 0x10       /* DAP size */
    .byte 0x00       /* Reserved */
    .word 16         /* Sector count */
    .word 0x8000     /* Destination offset */
    .word 0x0000     /* Destination segment */
    .quad 32         /* Starting LBA */

.org 510
.word 0xAA55

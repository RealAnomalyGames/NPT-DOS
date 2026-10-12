.section .rodata.installer_assets,"a",@progbits
.align 16

.global installer_boot_start
.global installer_boot_end
installer_boot_start:
.incbin "build/installer_boot.bin"
installer_boot_end:

.align 16
.global installer_stage2_start
.global installer_stage2_end
installer_stage2_start:
.incbin "build/second_stage.bin"
installer_stage2_end:

.align 16
.global installer_kernel_start
.global installer_kernel_end
installer_kernel_start:
.incbin "build/installed_kernel.bin"
installer_kernel_end:

.section .note.GNU-stack,"",@progbits

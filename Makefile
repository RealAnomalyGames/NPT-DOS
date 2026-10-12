BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/iso
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/npt-dos-build-014.iso

CC := gcc
LD := ld

CFLAGS := -m32 -std=gnu11 -O2 -Wall -Wextra \
          -ffreestanding -fno-pie -fno-stack-protector \
          -fno-builtin

LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

.PHONY: all clean check

all: $(ISO)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/boot.o: boot/boot.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.o: kernel/kernel.c kernel/keyboard.h kernel/terminal.h kernel/fs.h kernel/ata.h kernel/timer.h kernel/rtc.h kernel/setup.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/terminal.o: kernel/terminal.c kernel/terminal.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/keyboard.o: kernel/keyboard.c kernel/keyboard.h kernel/timer.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fs.o: kernel/fs.c kernel/fs.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ata.o: kernel/ata.c kernel/ata.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/timer.o: kernel/timer.c kernel/timer.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rtc.o: kernel/rtc.c kernel/rtc.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/setup.o: kernel/setup.c kernel/setup.h kernel/keyboard.h kernel/terminal.h kernel/ata.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/terminal.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/fs.o $(BUILD_DIR)/ata.o $(BUILD_DIR)/timer.o $(BUILD_DIR)/rtc.o $(BUILD_DIR)/setup.o linker.ld
	$(LD) $(LDFLAGS) -o $@ \
		$(BUILD_DIR)/boot.o \
		$(BUILD_DIR)/kernel.o \
		$(BUILD_DIR)/terminal.o \
		$(BUILD_DIR)/keyboard.o \
		$(BUILD_DIR)/fs.o \
		$(BUILD_DIR)/ata.o \
		$(BUILD_DIR)/timer.o \
		$(BUILD_DIR)/rtc.o \
		$(BUILD_DIR)/setup.o

check: $(KERNEL)
	grub-file --is-x86-multiboot $(KERNEL)

$(ISO): $(KERNEL) grub/grub.cfg
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/kernel.elf
	cp grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR)

clean:
	rm -rf $(BUILD_DIR)


# Build 014-E: BIOS hard-disk installation assets

AS := as
OBJCOPY := objcopy

INSTALLER_BOOT_OBJ := $(BUILD_DIR)/installer_boot.o
INSTALLER_BOOT_BIN := $(BUILD_DIR)/installer_boot.bin

SECOND_STAGE_OBJ := $(BUILD_DIR)/second_stage.o
SECOND_STAGE_RAW := $(BUILD_DIR)/second_stage.raw
SECOND_STAGE_BIN := $(BUILD_DIR)/second_stage.bin

INSTALLED_KERNEL_RAW := $(BUILD_DIR)/installed_kernel.raw
INSTALLED_KERNEL_BIN := $(BUILD_DIR)/installed_kernel.bin

BOOT_SECTOR_SIZE := 512
SECOND_STAGE_SIZE := 8192
INSTALLED_KERNEL_SIZE := 131072

.PHONY: installer-assets check-installer-assets

$(INSTALLER_BOOT_OBJ): boot/installer_boot.S | $(BUILD_DIR)
	$(AS) --32 $< -o $@

$(INSTALLER_BOOT_BIN): $(INSTALLER_BOOT_OBJ)
	$(LD) -m elf_i386 -Ttext 0x7C00 \
		--oformat binary -e _start -o $@ $<
	test "$$(wc -c < $@)" -eq $(BOOT_SECTOR_SIZE)
	test "$$(od -An -tx1 -j510 -N2 $@ | tr -d ' \n')" = "55aa"

$(SECOND_STAGE_OBJ): boot/second_stage.S | $(BUILD_DIR)
	$(AS) --32 $< -o $@

$(SECOND_STAGE_RAW): $(SECOND_STAGE_OBJ)
	$(LD) -m elf_i386 -Ttext 0x8000 \
		--oformat binary -e stage2_start -o $@ $<

$(SECOND_STAGE_BIN): $(SECOND_STAGE_RAW)
	test "$$(wc -c < $<)" -le $(SECOND_STAGE_SIZE)
	dd if=/dev/zero of=$@ bs=$(SECOND_STAGE_SIZE) count=1 status=none
	dd if=$< of=$@ conv=notrunc status=none
	test "$$(wc -c < $@)" -eq $(SECOND_STAGE_SIZE)

$(INSTALLED_KERNEL_RAW): $(KERNEL)
	$(OBJCOPY) -O binary $< $@

$(INSTALLED_KERNEL_BIN): $(INSTALLED_KERNEL_RAW)
	test "$$(wc -c < $<)" -le $(INSTALLED_KERNEL_SIZE)
	dd if=/dev/zero of=$@ bs=$(INSTALLED_KERNEL_SIZE) count=1 status=none
	dd if=$< of=$@ conv=notrunc status=none
	test "$$(wc -c < $@)" -eq $(INSTALLED_KERNEL_SIZE)

installer-assets: $(INSTALLER_BOOT_BIN) $(SECOND_STAGE_BIN) $(INSTALLED_KERNEL_BIN)

check-installer-assets: installer-assets
	file $(INSTALLER_BOOT_BIN) $(SECOND_STAGE_BIN) $(INSTALLED_KERNEL_BIN)
	sha256sum $(INSTALLER_BOOT_BIN) $(SECOND_STAGE_BIN) $(INSTALLED_KERNEL_BIN)

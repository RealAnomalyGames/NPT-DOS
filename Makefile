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

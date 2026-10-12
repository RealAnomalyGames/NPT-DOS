
#!/usr/bin/env python3

import pathlib
import struct
import subprocess
import sys

BUILD = pathlib.Path("build")

BOOT = BUILD / "installer_boot.bin"
STAGE2 = BUILD / "second_stage.bin"
KERNEL_RAW = BUILD / "installed_kernel.bin"
BASE_ELF = BUILD / "kernel-base.elf"
INSTALLER_ELF = BUILD / "kernel.elf"

EXPECTED_BOOT_SIZE = 512
EXPECTED_STAGE2_SIZE = 8192
EXPECTED_KERNEL_SIZE = 131072
KERNEL_LOAD_ADDRESS = 0x100000
MULTIBOOT_MAGIC = struct.pack("<I", 0x1BADB002)


def fail(message):
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def require_file(path):
    if not path.is_file():
        fail(f"Missing required file: {path}")


def symbol_table(path):
    output = subprocess.check_output(
        ["nm", "-n", str(path)],
        text=True,
    )
    symbols = {}

    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3:
            try:
                symbols[parts[2]] = int(parts[0], 16)
            except ValueError:
                pass

    return symbols


def check_size(path, expected):
    actual = path.stat().st_size
    if actual != expected:
        fail(f"{path}: expected {expected} bytes, found {actual}")
    print(f"PASS: {path} is {expected} bytes")


def main():
    for path in (BOOT, STAGE2, KERNEL_RAW, BASE_ELF, INSTALLER_ELF):
        require_file(path)

    check_size(BOOT, EXPECTED_BOOT_SIZE)
    check_size(STAGE2, EXPECTED_STAGE2_SIZE)
    check_size(KERNEL_RAW, EXPECTED_KERNEL_SIZE)

    boot = BOOT.read_bytes()
    if boot[510:512] != b"\x55\xaa":
        fail("BIOS boot signature is not 55 AA")
    print("PASS: BIOS boot signature is 55 AA")

    raw = KERNEL_RAW.read_bytes()
    if raw[:4] == MULTIBOOT_MAGIC:
        fail("Raw kernel begins with the Multiboot header, not _start")

    base_symbols = symbol_table(BASE_ELF)
    if "_start" not in base_symbols:
        fail("Base kernel is missing _start")
    if base_symbols["_start"] != KERNEL_LOAD_ADDRESS:
        fail(
            f"_start is at 0x{base_symbols['_start']:X}, "
            f"expected 0x{KERNEL_LOAD_ADDRESS:X}"
        )
    print("PASS: _start is linked at physical 0x100000")

    if "__bss_start" not in base_symbols or "__bss_end" not in base_symbols:
        fail("Kernel is missing BSS boundary symbols")
    if base_symbols["__bss_end"] < base_symbols["__bss_start"]:
        fail("Invalid BSS boundaries")
    print("PASS: Kernel BSS boundary symbols exist")

    if MULTIBOOT_MAGIC not in raw[:8192]:
        fail("Multiboot header magic not found within first 8 KiB of raw kernel")
    print("PASS: Multiboot magic remains within the first 8 KiB")

    installer_symbols = symbol_table(INSTALLER_ELF)
    embedded_assets = (
        ("installer_boot_start", "installer_boot_end", EXPECTED_BOOT_SIZE),
        ("installer_stage2_start", "installer_stage2_end", EXPECTED_STAGE2_SIZE),
        ("installer_kernel_start", "installer_kernel_end", EXPECTED_KERNEL_SIZE),
    )

    for start_name, end_name, expected_size in embedded_assets:
        if start_name not in installer_symbols or end_name not in installer_symbols:
            fail(f"Missing embedded asset symbols: {start_name}/{end_name}")

        actual_size = installer_symbols[end_name] - installer_symbols[start_name]
        if actual_size != expected_size:
            fail(
                f"{start_name}: expected {expected_size} bytes, "
                f"found {actual_size}"
            )
        print(f"PASS: {start_name} embedded size is {actual_size} bytes")

    print("All installer layout checks passed.")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys


def find_partition_tool() -> str:
    candidates = glob.glob(
        os.path.expanduser(
            "~/.platformio/packages/framework-espidf/components/partition_table/gen_esp32part.py"
        )
    )
    if not candidates:
        raise RuntimeError("Could not locate gen_esp32part.py in PlatformIO framework package.")
    return candidates[0]


def parse_filesystem_offset(partitions_bin: str) -> str:
    tool = find_partition_tool()
    out = subprocess.check_output([sys.executable, tool, partitions_bin], text=True)
    for line in out.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        cols = [c.strip() for c in line.split(",")]
        if len(cols) < 5:
            continue
        name = cols[0].lower()
        if name in ("spiffs", "littlefs"):
            return cols[3]
    raise RuntimeError("Filesystem partition (spiffs/littlefs) not found in partition table.")


def parse_image_offsets(build_dir: str) -> dict:
    flash_args = os.path.join(build_dir, "flash_args")
    if not os.path.exists(flash_args):
        raise FileNotFoundError(flash_args)

    offsets = {
        "bootloader": None,
        "partitions": None,
        "firmware": None,
    }

    with open(flash_args, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            m = re.match(r"^(0x[0-9a-fA-F]+)\s+(.+)$", line)
            if not m:
                continue
            off, path = m.group(1), m.group(2).strip()
            low = path.lower()
            base = os.path.basename(low)
            if base == "bootloader.bin":
                offsets["bootloader"] = off
                continue
            if "partition-table.bin" in low or base == "partitions.bin":
                offsets["partitions"] = off
                continue
            if base.endswith(".bin"):
                offsets["firmware"] = off

    missing = [k for k, v in offsets.items() if v is None]
    if missing:
        raise RuntimeError(f"Could not parse offsets from flash_args: missing {', '.join(missing)}")
    return offsets


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--env", required=True)
    parser.add_argument("--chip", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    build_dir = os.path.join(".pio", "build", args.env)
    os.makedirs(args.out, exist_ok=True)

    bootloader_src = os.path.join(build_dir, "bootloader.bin")
    partitions_src = os.path.join(build_dir, "partitions.bin")
    firmware_src = os.path.join(build_dir, "firmware.bin")
    fs_src = os.path.join(build_dir, "spiffs.bin")

    for p in (bootloader_src, partitions_src, firmware_src, fs_src):
        if not os.path.exists(p):
            raise FileNotFoundError(p)

    prefix = f"rc-controller-{args.env}"
    bootloader_name = f"{prefix}-bootloader.bin"
    partitions_name = f"{prefix}-partitions.bin"
    firmware_name = f"{prefix}-firmware.bin"
    fs_name = f"{prefix}-spiffs.bin"

    shutil.copy2(bootloader_src, os.path.join(args.out, bootloader_name))
    shutil.copy2(partitions_src, os.path.join(args.out, partitions_name))
    shutil.copy2(firmware_src, os.path.join(args.out, firmware_name))
    shutil.copy2(fs_src, os.path.join(args.out, fs_name))

    image_offsets = parse_image_offsets(build_dir)
    fs_offset = parse_filesystem_offset(partitions_src)
    meta = {
        "env": args.env,
        "chip_family": args.chip,
        "offsets": {
            "bootloader": image_offsets["bootloader"],
            "partitions": image_offsets["partitions"],
            "firmware": image_offsets["firmware"],
            "filesystem": fs_offset,
        },
        "files": {
            "bootloader": bootloader_name,
            "partitions": partitions_name,
            "firmware": firmware_name,
            "filesystem": fs_name,
        },
    }
    with open(os.path.join(args.out, f"{prefix}-meta.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)


if __name__ == "__main__":
    main()

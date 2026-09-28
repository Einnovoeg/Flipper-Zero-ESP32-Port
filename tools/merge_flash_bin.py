#!/usr/bin/env python3
"""
merge_flash_bin.py — Combine bootloader + partition table + app into one flashable image.

Offsets are the ESP-IDF defaults used by this project (see tools/flash_linux.sh):
  0x00000  bootloader/bootloader.bin
  0x08000  partition_table/partition-table.bin
  0x10000  furi_esp32.bin

Produces a single image for:  esptool write_flash 0x0 merged-*.bin
Useful for launchers (M5Launcher etc.) that only handle one .bin at 0x0.

Usage:
  python tools/merge_flash_bin.py --build-dir build_t_embed --chip esp32s3 --flash-size 16MB -o merged.bin
  python tools/merge_flash_bin.py --dry-run
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


def find_esptool() -> list[str]:
    if shutil.which("esptool"):
        return ["esptool"]
    if shutil.which("esptool.py"):
        return ["esptool.py"]
    for mod in ("esptool",):
        try:
            subprocess.run(
                [sys.executable, "-m", mod, "version"],
                capture_output=True,
                check=True,
            )
            return [sys.executable, "-m", mod]
        except Exception:
            continue
    raise SystemExit("esptool not found. Install with: pip install esptool")


def autodetect_build_dir() -> Path:
    for cand in ("build_t_embed", "build_s3", "build", "build_waveshare_c6"):
        p = Path(cand)
        if (p / "furi_esp32.bin").exists():
            return p
    raise SystemExit(
        "No build dir found (looked for build_t_embed, build_s3, build, "
        "build_waveshare_c6). Pass --build-dir explicitly after building."
    )


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Merge ESP32 flash images into one .bin")
    ap.add_argument("--build-dir", default=None, help="IDF build dir (default: autodetect)")
    ap.add_argument("--chip", default="esp32s3", help="esptool chip (default: esp32s3)")
    ap.add_argument("--flash-size", default="16MB", help="flash size (default: 16MB)")
    ap.add_argument("--output", "-o", default=None, help="output .bin path")
    ap.add_argument("--dry-run", action="store_true", help="print esptool cmd without running")
    return ap.parse_args()


def main() -> None:
    args = parse_args()
    build = Path(args.build_dir) if args.build_dir else autodetect_build_dir()

    bootloader = build / "bootloader" / "bootloader.bin"
    partitions = build / "partition_table" / "partition-table.bin"
    firmware = build / "furi_esp32.bin"

    # Try flasher_args.json for authoritative offsets, fall back to defaults.
    offsets = {"bootloader": "0x0", "partitions": "0x8000", "app": "0x10000"}
    flasher_json = build / "flasher_args.json"
    if flasher_json.exists():
        try:
            data = json.loads(flasher_json.read_text())
            # flasher_args.json format varies; only override if keys look right.
            flash_files = data.get("flash_files", {})
            for k, v in flash_files.items():
                kl = k.lower()
                if "bootloader" in kl:
                    offsets["bootloader"] = k
                elif "partition" in kl:
                    offsets["partitions"] = k
                elif "furi_esp32" in kl or k == "0x10000":
                    offsets["app"] = k
        except Exception as e:
            print(f"warn: could not parse {flasher_json}: {e}", file=sys.stderr)

    missing = [p for p in (bootloader, partitions, firmware) if not p.exists()]
    if missing:
        raise SystemExit(
            f"Missing build outputs in {build}: {', '.join(str(m) for m in missing)}\n"
            f"Run a build first, e.g.: ./build.sh --board t_embed --build-only"
        )

    out = Path(args.output) if args.output else build / f"merged-flash-{args.chip}.bin"
    cmd = find_esptool() + [
        "--chip", args.chip, "merge_bin",
        "--flash-mode", "dio",
        "--flash-freq", "80m",
        "--flash-size", args.flash_size,
        "--output", str(out),
        offsets["bootloader"], str(bootloader),
        offsets["partitions"], str(partitions),
        offsets["app"], str(firmware),
    ]
    print(" ".join(cmd))
    if args.dry_run:
        return
    subprocess.run(cmd, check=True)
    print(f"Wrote {out} ({out.stat().st_size / 1024 / 1024:.1f} MB)")


if __name__ == "__main__":
    main()

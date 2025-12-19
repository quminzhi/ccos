#!/usr/bin/env python3
import argparse
import subprocess
import sys


def parse_args():
    parser = argparse.ArgumentParser(
        description="Compute fw_jump _start_warm offset from base address."
    )
    parser.add_argument(
        "--nm",
        required=True,
        help="Path to nm (e.g. riscv64-unknown-linux-gnu-nm)",
    )
    parser.add_argument("--elf", required=True, help="Path to fw_jump.elf")
    parser.add_argument("--base", required=True, help="Base address (hex)")
    return parser.parse_args()


def main():
    args = parse_args()
    try:
        base = int(args.base, 16)
    except ValueError:
        print(f"ERROR: invalid base address: {args.base}", file=sys.stderr)
        return 1

    try:
        nm_out = subprocess.check_output([args.nm, "-n", args.elf], text=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: failed to run nm: {exc}", file=sys.stderr)
        return 1

    start_warm = None
    for line in nm_out.splitlines():
        # Format: "0000000080000328 T _start_warm"
        parts = line.split()
        if len(parts) >= 3 and parts[2] == "_start_warm":
            try:
                start_warm = int(parts[0], 16)
                break
            except ValueError:
                continue

    if start_warm is None:
        print("ERROR: _start_warm not found", file=sys.stderr)
        return 1

    offset = start_warm - base
    if offset < 0:
        print("ERROR: computed negative offset", file=sys.stderr)
        return 1

    print(f"FW_JUMP_WARM_ENTRY_OFFSET=0x{offset:x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

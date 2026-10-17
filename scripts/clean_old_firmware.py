#!/usr/bin/env python3
"""Remove outdated firmware binaries from the repository.

`firmware/` should only contain the binaries of the current release (see AGENTS.md).
GitHub Releases keep the history of all versions.

The current version is read from the `-D BUILDVER=<x.y.z>` define in `platformio.ini`.

Usage:
    python scripts/clean_old_firmware.py                    # dry run, list what would be removed
    python scripts/clean_old_firmware.py --apply            # delete outdated files
    python scripts/clean_old_firmware.py --apply --git-rm   # delete and stage with `git rm`
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
FIRMWARE_DIR = REPO_ROOT / "firmware"
PLATFORMIO_INI = REPO_ROOT / "platformio.ini"

FIRMWARE_PATTERN = re.compile(r"^FW_.+_v(?P<version>\d+\.\d+\.\d+)\.bin$")
BUILDVER_PATTERN = re.compile(r"-D\s+BUILDVER=(?P<version>\d+\.\d+\.\d+)")


def read_current_version(ini_path=PLATFORMIO_INI):
    """Return the BUILDVER define from platformio.ini (e.g. "1.3.13")."""
    try:
        content = ini_path.read_text(encoding="utf-8")
    except OSError as exc:
        raise SystemExit(f"ERROR: cannot read {ini_path}: {exc}")

    match = BUILDVER_PATTERN.search(content)
    if not match:
        raise SystemExit(f"ERROR: no '-D BUILDVER=<x.y.z>' found in {ini_path}")
    return match.group("version")


def split_firmware(firmware_dir, current_version):
    """Split firmware/*.bin into (keep, outdated) lists based on the version in the file name."""
    keep, outdated = [], []
    for path in sorted(firmware_dir.glob("FW_*.bin")):
        match = FIRMWARE_PATTERN.match(path.name)
        if not match:
            print(f"WARNING: skipping unexpected firmware name: {path.name}")
            continue
        if match.group("version") == current_version:
            keep.append(path)
        else:
            outdated.append(path)
    return keep, outdated


def remove_file(path, use_git_rm):
    """Delete a firmware file, optionally through `git rm` so the removal is staged."""
    if use_git_rm:
        subprocess.run(
            ["git", "rm", "--quiet", str(path.relative_to(REPO_ROOT))],
            cwd=REPO_ROOT,
            check=True,
        )
    else:
        path.unlink()


def main():
    parser = argparse.ArgumentParser(
        description="Remove outdated FW_*_v<version>.bin files from the firmware directory."
    )
    parser.add_argument("--apply", action="store_true", help="actually remove files (default: dry run)")
    parser.add_argument("--git-rm", action="store_true", help="use 'git rm' instead of deleting directly")
    parser.add_argument("--version", help="override the current version (default: BUILDVER from platformio.ini)")
    parser.add_argument("--firmware-dir", default=str(FIRMWARE_DIR), help="firmware directory (default: ./firmware)")
    args = parser.parse_args()

    current_version = args.version or read_current_version()
    firmware_dir = Path(args.firmware_dir)
    if not firmware_dir.is_dir():
        raise SystemExit(f"ERROR: firmware directory not found: {firmware_dir}")

    keep, outdated = split_firmware(firmware_dir, current_version)

    print(f"Current version: {current_version}")
    print(f"Keep:   {len(keep)} file(s)")
    for path in keep:
        print(f"  + {path.name}")
    print(f"Remove: {len(outdated)} file(s)")
    for path in outdated:
        print(f"  - {path.name}")

    if not keep:
        print(f"WARNING: no firmware binaries for version {current_version} found in {firmware_dir}")

    if not outdated:
        return 0

    if not args.apply:
        print("\nDry run - nothing removed. Re-run with --apply to delete these files.")
        return 0

    for path in outdated:
        remove_file(path, args.git_rm)

    print(f"\nRemoved {len(outdated)} file(s).")
    if args.git_rm:
        print("Removals are staged - commit them together with the new binaries.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

# Build a single "full" image (bootloader/partitions + app + FS) for flashing at 0x0.
# - Cross-platform: uses PlatformIO's Python and `python -m esptool` (no deprecated esptool.py)
# - Independent of FLASH_EXTRA_IMAGES being mutated by buildfs
# - Parses the partitions CSV to discover app + FS offsets
# - Includes SPIFFS/LittleFS image in the merged binary
# - Runs buildfs ONLY for the current PIO environment (-e <env_name>)
# - Robustly normalizes FLASH_EXTRA_IMAGES (tuples, flat lists, or strings)

import os
import sys
import csv
import shlex
import subprocess
import re

Import("env")
platform     = env.PioPlatform()
BOARD_CFG    = env.BoardConfig()
PROJECT_DIR  = env.subst("$PROJECT_DIR")
PYTHON       = env.subst("$PYTHONEXE")

# --- esptool (module) ---------------------------------------------------------
def _py_out(args):
    """Run a subprocess with the penv Python and return stripped stdout or ''."""
    try:
        out = subprocess.check_output(args, stderr=subprocess.STDOUT, text=True)
        return out.strip()
    except Exception:
        return ""

def get_esptool_version():
    # Query the *same* Python we'll use to run esptool
    mod_path = _py_out([PYTHON, "-c", "import esptool; import os; print(getattr(esptool,'__file__',''))"])
    cli_path = _py_out([PYTHON, "-c", "import shutil; print(shutil.which('esptool') or '')"])

    print(f"[unified] esptool module path: {mod_path or '(unknown)'}")
    print(f"[unified] esptool CLI path:    {cli_path or '(unknown)'}")

    ver_out = _py_out([PYTHON, "-m", "esptool", "version"])
    m = re.search(r"v?(\d+\.\d+(?:\.\d+)*)", ver_out or "")
    return m.group(1) if m else "unknown"

print(f"[unified] Using Python:        {PYTHON}")
print(f"[unified] esptool ver:         {get_esptool_version()}")

# --- Helpers ------------------------------------------------------------------
def run(cmd):
    # Be resilient to non-strings sneaking in
    safe_cmd = [str(x) for x in cmd]
    print("[unified]", " ".join(shlex.quote(x) for x in safe_cmd))
    proc = subprocess.run(safe_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(proc.stdout)
    if proc.returncode != 0:
        sys.exit(proc.returncode)

def run_esptool(args):
    # Use the module entrypoint to avoid esptool.py deprecation
    cmd = [PYTHON, "-m", "esptool"] + [str(x) for x in args]
    run(cmd)

def run_esptool_with_fallback(args):
    """
    Run esptool with compatibility handling for parameter syntax changes.
    Tries new syntax (merge-bin) first, falls back to old syntax (merge_bin) if needed.
    """
    # Try new syntax first (esptool v5.x+)
    try:
        cmd = [PYTHON, "-m", "esptool"] + [str(x) for x in args]
        print("[unified]", " ".join(shlex.quote(x) for x in cmd))
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        print(proc.stdout)
        if proc.returncode == 0:
            return
        # Check if error is related to unknown command (parameter syntax)
        error_output = proc.stdout.lower()
        if ("invalid choice: 'merge-bin'" in error_output or 
            "no such option" in error_output or 
            "unknown command" in error_output):
            print("[unified] New syntax failed, trying old syntax...")
        else:
            # Different error, don't try fallback
            print(f"[unified] Command failed with non-syntax error, exit code: {proc.returncode}")
            sys.exit(proc.returncode)
    except Exception as e:
        print(f"[unified] Exception with new syntax: {e}")
    
    # Fallback to old syntax (esptool v4.x and earlier)
    # Convert merge-bin to merge_bin and --flash-xyz to --flash_xyz
    old_args = []
    for arg in args:
        arg_str = str(arg)
        if arg_str == "merge-bin":
            old_args.append("merge_bin")
        elif arg_str == "--flash-mode":
            old_args.append("--flash_mode")
        elif arg_str == "--flash-freq":
            old_args.append("--flash_freq")
        elif arg_str == "--flash-size":
            old_args.append("--flash_size")
        else:
            old_args.append(arg_str)
    
    cmd = [PYTHON, "-m", "esptool"] + old_args
    print("[unified] Fallback:", " ".join(shlex.quote(x) for x in cmd))
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(proc.stdout)
    if proc.returncode != 0:
        print(f"[unified] Fallback also failed with exit code: {proc.returncode}")
        sys.exit(proc.returncode)

def _read_partitions_csv():
    parts_rel = BOARD_CFG.get("build.partitions")
    if not parts_rel:
        raise FileNotFoundError(
            "board_build.partitions not set; please set a partitions CSV in platformio.ini"
        )
    csv_path = parts_rel if os.path.isabs(parts_rel) else os.path.join(PROJECT_DIR, parts_rel)
    if not os.path.exists(csv_path):
        raise FileNotFoundError(f"Partitions CSV not found: {csv_path}")

    rows = []
    # utf-8-sig handles potential BOM on Windows-edited CSVs
    with open(csv_path, newline="", encoding="utf-8-sig") as f:
        for row in csv.reader(f):
            if not row or row[0].strip().startswith("#"):
                continue
            r = row + [""] * (6 - len(row))
            rows.append([c.strip() for c in r[:6]])
    return rows

def _hexify(v):
    v = str(v).lower().strip()
    if v.startswith("0x"):
        return v
    try:
        return hex(int(v, 0))
    except Exception:
        return v

def find_offsets_from_csv():
    rows = _read_partitions_csv()
    app_offset = None
    fs_offset  = None
    fs_kind    = None

    for name, ptype, subtype, offset, size, flags in rows:
        if ptype.lower() == "app" and subtype.lower() in ("factory", "app0"):
            app_offset = _hexify(offset)
        if ptype.lower() == "data" and subtype.lower() in ("spiffs", "littlefs", "fatfs"):
            fs_offset = _hexify(offset)
            fs_kind   = subtype.lower()

    if not app_offset:
        app_offsets = [_hexify(o) for n,t,s,o,sz,fl in rows if t.lower() == "app" and o]
        if app_offsets and all(x.startswith("0x") for x in app_offsets):
            app_offset = sorted(app_offsets, key=lambda x: int(x, 16))[0]
        elif app_offsets:
            app_offset = app_offsets[0]

    return app_offset, fs_offset, fs_kind

def resolve_fs_image(fs_kind, build_dir):
    # PlatformIO emits <build_dir>/spiffs.bin or <build_dir>/littlefs.bin
    candidates = []
    if fs_kind == "spiffs":
        candidates.append(os.path.join(build_dir, "spiffs.bin"))
    elif fs_kind == "littlefs":
        candidates.append(os.path.join(build_dir, "littlefs.bin"))
    else:
        candidates += [
            os.path.join(build_dir, "spiffs.bin"),
            os.path.join(build_dir, "littlefs.bin"),
        ]
    for p in candidates:
        if os.path.exists(p):
            return p
    return None

def normalize_extra_images(raw):
    """
    Normalize FLASH_EXTRA_IMAGES into a list of (offset, file) strings.
    Accepts:
      - list/tuple of strings (flat): ["0x1000","bootloader.bin","0x8000","partitions.bin"]
      - list/tuple of 2-tuples: [("0x1000","bootloader.bin"), ("0x8000","partitions.bin")]
      - space-joined string: "0x1000 bootloader.bin 0x8000 partitions.bin"
      - list of "offset file" strings: ["0x1000 bootloader.bin", "0x8000 partitions.bin"]
    """
    pairs = []
    if not raw:
        return pairs

    # Case 1: string
    if isinstance(raw, str):
        toks = raw.split()
        for i in range(0, len(toks) - 1, 2):
            pairs.append((str(toks[i]), str(toks[i + 1])))
        return pairs

    # Case 2: list/tuple
    seq = list(raw)
    if not seq:
        return pairs

    # Already 2-tuples?
    if all(isinstance(x, (list, tuple)) and len(x) == 2 for x in seq):
        for off, path in seq:
            pairs.append((str(off), str(path)))
        return pairs

    # List of strings (some may contain spaces)
    if all(isinstance(x, str) for x in seq):
        flat = []
        for x in seq:
            xs = x.split()
            if len(xs) == 2:
                pairs.append((str(xs[0]), str(xs[1])))
            else:
                flat.extend(xs)
        if flat:
            for i in range(0, len(flat) - 1, 2):
                pairs.append((str(flat[i]), str(flat[i + 1])))
        return pairs

    # Fallback: flatten everything
    flat = []
    for x in seq:
        if isinstance(x, (list, tuple)):
            flat.extend([str(y) for y in x])
        else:
            flat.append(str(x))
    for i in range(0, len(flat) - 1, 2):
        pairs.append((flat[i], flat[i + 1]))
    return pairs

# --- Build FS image (spiffs/littlefs) ----------------------------------------
# IMPORTANT: Signature must match what SCons passes: (target, source, env)
def pio_run_buildfs(target, source, env):
    print("[unified] Building filesystem image (buildfs) for current env…")
    env_name = env.subst("$PIOENV")
    if not env_name:
        print("[unified][WARN] $PIOENV is empty; running buildfs without -e may affect all envs.")
        run([PYTHON, "-m", "platformio", "run", "--target", "buildfs"])
    else:
        run([PYTHON, "-m", "platformio", "run", "-e", env_name, "--target", "buildfs"])

# --- esptool merge step -------------------------------------------------------
# IMPORTANT: Signature must match what SCons passes: (target, source, env)
def esp32_create_combined_bin(target, source, env):
    print("[unified] Generating combined firmware for current env…")

    build_dir  = env.subst("$BUILD_DIR")
    chip       = env.get("BOARD_MCU")
    flash_size = env.BoardConfig().get("upload.flash_size", "4MB")
    flash_mode = env["__get_board_flash_mode"](env)
    flash_freq = env["__get_board_f_flash"](env)

    firmware_app = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    output_path  = env.subst("$BUILD_DIR/FW_${PROGNAME}.bin")

    app_offset, fs_offset, fs_kind = find_offsets_from_csv()
    if not app_offset:
        print("[unified][ERROR] Could not determine app offset from partitions CSV.")
        sys.exit(1)

    # Collect extra sections if provided by PlatformIO (bootloader/partitions, etc.)
    sections = []
    raw = env.get("FLASH_EXTRA_IMAGES")
    sections += normalize_extra_images(raw)

    # Main app
    sections.append((str(app_offset), str(firmware_app)))

    # Filesystem image (from buildfs), independent of FLASH_EXTRA_IMAGES
    if fs_offset:
        fs_img = resolve_fs_image(fs_kind, build_dir)
        if fs_img:
            sections.append((str(fs_offset), str(fs_img)))
        else:
            print(f"[unified][WARN] FS image not found in {build_dir} "
                  f"(expected spiffs.bin/littlefs.bin); excluding FS.")

    print("[unified] Merge plan (offset | file):")
    for off, f in sections:
        print(f"  - {off} | {f}")

    args = [
        "--chip", str(chip),
        "merge-bin",
        "-o", str(output_path),
        "--flash-mode", str(flash_mode),
        "--flash-freq", str(flash_freq),
        "--flash-size", str(flash_size),
    ]
    for off, f in sections:
        args += [str(off), str(f)]

    run_esptool_with_fallback(args)
    print(f"[unified] Combined image written: {output_path}")

# --- Register post-build ------------------------------------------------------
my_flags = env.ParseFlags(env['BUILD_FLAGS'])
defines = {}
for b in my_flags.get("CPPDEFINES", []):
    if isinstance(b, list):
        defines[b[0]] = b[1]
    else:
        defines[b] = b

if defines.get("CREATE_FIRMWAREFILE"):
    print("[unified] CREATE_FIRMWAREFILE defined - registering post-buildprog action")
    env.AddPostAction("buildprog", [pio_run_buildfs, esp32_create_combined_bin])
else:
    print("[unified] CREATE_FIRMWAREFILE not defined - skipping")

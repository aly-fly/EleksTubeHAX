# Combines separate bin files with their respective offsets into a single file
# This single file must then be flashed to an ESP32 node with 0x0 offset.
import esptool
from os.path import join
import sys
import subprocess
Import("env")
platform = env.PioPlatform()


env.Execute("$PYTHONEXE -m pip install intelhex")
sys.path.append(join(platform.get_package_dir("tool-esptoolpy")))


def pio_run_buildfs(source, target, env):
    print("script_build_unified_binary.py: Building SPIFFS filesystem image...")

    # Build the SPIFFS filesystem
    buildfs_cmd = [
        env.subst("$PYTHONEXE"),
        "-m",
        "platformio",
        "run",
        "--target",
        "buildfs"
    ]

    result = subprocess.run(
        buildfs_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    if result.returncode != 0:
        print("script_build_unified_binary.py: Failed to build SPIFFS filesystem image.")
        print(result.stderr)
        sys.exit(1)
    else:
        print("script_build_unified_binary.py: Successfully built SPIFFS filesystem image.")


def esp32_create_combined_bin(source, target, env):
    print("script_build_unified_binary.py: Generating combined binary for serial flashing...")

    # The offset from begin of the file where the app0 partition starts
    # This is defined in the partition .csv file
    app_offset = env.get("ESP32_APP_OFFSET")

    object_file_name = env.subst("$BUILD_DIR/FW_${PROGNAME}.bin")
    sections = env.subst(env.get("FLASH_EXTRA_IMAGES"))
    firmware_name = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    chip = env.get("BOARD_MCU")
    flash_size = env.BoardConfig().get("upload.flash_size", "4MB")
    flash_mode = env["__get_board_flash_mode"](env)
    flash_freq = env["__get_board_f_flash"](env)

    cmd = [
        "--chip",
        chip,
        "merge-bin",
        "-o",
        object_file_name,
        "--flash-mode",
        flash_mode,
        "--flash-freq",
        flash_freq,
        "--flash-size",
        flash_size,
    ]

    print("    Offset | File")
    for section in sections:
        sect_adr, sect_file = section.split(" ", 1)
        print(f" -  {sect_adr} | {sect_file}")
        cmd += [sect_adr, sect_file]

    print(f" - {app_offset} | {firmware_name}")
    cmd += [app_offset, firmware_name]

    print('script_build_unified_binary.py: Using esptool.py arguments: %s' %
          ' '.join(cmd))

    esptool.main(cmd)


my_flags = env.ParseFlags(env['BUILD_FLAGS'])

# import pprint
# print("rename_firmware.py: Parsed BUILD_FLAGS:")
# pprint.pprint(my_flags)

defines = dict()
for b in my_flags.get("CPPDEFINES"):
    if isinstance(b, list):
        defines[b[0]] = b[1]
    else:
        defines[b] = b

if defines.get("CREATE_FIRMWAREFILE"):
    print("[Post-Build] CREATE_FIRMWAREFILE is defined. Registering post-build actions.")
    env.AddPostAction(
        "buildprog", [pio_run_buildfs, esp32_create_combined_bin])
else:
    print("[Post-Build] CREATE_FIRMWAREFILE is not defined. Skipping firmware merge.")

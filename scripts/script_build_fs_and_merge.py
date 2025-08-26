# script_build_fs_and_merge.py

import os
import subprocess
import sys
import csv
import re
from os import getenv
from SCons.Script import Import, DefaultEnvironment

def is_create_firmwarefile_defined():
    for define in cpp_defines:
        if isinstance(define, str) and define == 'CREATE_FIRMWAREFILE':
            return True
        elif isinstance(define, tuple) and define[0] == 'CREATE_FIRMWAREFILE':
            return True
    return False

def parse_partition_table(partition_csv_path):
    """
    Parses the partition table CSV file and extracts offsets for bootloader, partition table,
    app (firmware), and spiffs.

    :param partition_csv_path: Path to the partition table CSV file.
    :return: Dictionary with offsets for 'bootloader', 'partition_table', 'app0' or 'factory', and 'spiffs'.
    """
    offsets = {}

    # Standard offsets for bootloader and partition table
    offsets['bootloader'] = '0x1000'
    offsets['partition_table'] = '0x8000'

    if not os.path.isfile(partition_csv_path):
        print(f"[Error] Partition table file not found: {partition_csv_path}")
        env.Exit(1)

    with open(partition_csv_path, newline='') as csvfile:
        reader = csv.reader(csvfile, delimiter=',')
        for row in reader:
            # Skip comments and empty lines
            if not row or row[0].startswith('#'):
                continue
            if len(row) < 5:
                continue  # Not enough columns
            name = row[0].strip()
            offset = row[3].strip()
            # Store offsets for 'app0', 'factory', and 'spiffs'
            if name.lower() in ['app0', 'factory', 'spiffs']:
                # Convert offset from hex or decimal string to integer
                try:
                    offset_int = int(offset, 0)
                    offsets[name.lower()] = hex(offset_int)
                except ValueError:
                    print(f"[Error] Invalid offset value for partition '{name}': {offset}")
                    env.Exit(1)

    # Verify that required partitions were found
    required_partitions = ['app0', 'factory', 'spiffs']
    if not any(partition in offsets for partition in ['app0', 'factory']):
        print("[Error] Required partition 'app0' or 'factory' not found in partition table.")
        env.Exit(1)
    if 'spiffs' not in offsets:
        print("[Error] Required partition 'spiffs' not found in partition table.")
        env.Exit(1)

    return offsets

def extract_hardware_name(user_defines_path):
    """
    Extracts the active hardware name from the _User_defines.h file.

    :param user_defines_path: Path to the _User_defines.h file.
    :return: Hardware name as a string.
    """
    if not os.path.isfile(user_defines_path):
        print(f"[Error] _User_defines.h file not found: {user_defines_path}")
        env.Exit(1)

    with open(user_defines_path, 'r') as f:
        lines = f.readlines()

    in_hardware_section = False
    hardware_name = None

    for line in lines:
        stripped_line = line.strip()

        # Check if we've reached the "Type of the clock hardware" section
        if 'Type of the clock hardware' in stripped_line:
            in_hardware_section = True
            continue  # Move to the next line

        if in_hardware_section:
            # If we encounter an empty line or a comment that marks the end of the section, exit the section
            if stripped_line == '' or 'End of hardware types' in stripped_line:
                break

            # Ignore commented lines (single-line comments)
            if stripped_line.startswith('//') or stripped_line.startswith('/*') or stripped_line.startswith('*'):
                continue

            # Ignore lines that have inline comments
            line_without_comments = line.split('//')[0].split('/*')[0].strip()

            # Match the #define statement
            define_match = re.match(r'#define\s+(HARDWARE_\w+)', line_without_comments)
            if define_match:
                hardware_name = define_match.group(1)
                break  # Found the active hardware define

    if hardware_name:
        return hardware_name
    else:
        print("[Error] Active hardware define not found in the 'Type of the clock hardware' section.")
        env.Exit(1)

def run_buildfs(source, target, env):
    print("\n[Post-Build] Starting SPIFFS build...")
    
    # Build the SPIFFS filesystem
    buildfs_cmd = [
        env.subst("$PYTHONEXE"),
        "-m",
        "platformio",
        "run",
        "--target",
        "buildfs"
    ]
    
    result = subprocess.run(buildfs_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    
    if result.returncode != 0:
        print("[Post-Build] Failed to build SPIFFS filesystem.")
        print(result.stderr)
        sys.exit(1)
    else:
        print("[Post-Build] Successfully built SPIFFS filesystem.")

def run_merge_bins(source, target, env):
    print("\n[Post-Build] Starting binary merge...")

    platform = env.PioPlatform()

    # Paths to binaries
    firmware_bin = os.path.join(build_dir, "firmware.bin")
    spiffs_bin = os.path.join(build_dir, "spiffs.bin")
    combined_bin = os.path.join(build_dir, "combined.bin")
    bootloader_bin = os.path.join(build_dir, "bootloader.bin")
    partition_bin = os.path.join(build_dir, "partitions.bin")

    # Path to the partition table CSV
    partition_csv = env.GetProjectOption("board_build.partitions")
    if not partition_csv:
        print("[Error] 'board_build.partitions' not defined in platformio.ini.")
        env.Exit(1)

    partition_csv_path = partition_csv
    if not os.path.isabs(partition_csv_path):
        partition_csv_path = os.path.join(env.subst("$PROJECT_DIR"), partition_csv_path)

    # Parse the partition table to get offsets
    offsets = parse_partition_table(partition_csv_path)
    bootloader_offset = offsets.get('bootloader', '0x1000')
    partition_table_offset = offsets.get('partition_table', '0x8000')
    app_partition_name = 'app0' if 'app0' in offsets else 'factory'
    app_offset = offsets[app_partition_name]
    spiffs_offset = offsets['spiffs']

    # Extract hardware name for renaming the combined binary
    user_defines_path = os.path.join(env.subst("$PROJECT_SRC_DIR"), "_USER_DEFINES.h")
    hardware_name = extract_hardware_name(user_defines_path)
    print(f"[Post-Build] Hardware name before santization: {hardware_name}")
    if hardware_name:
        # Create a sanitized hardware name for the filename
        hardware_name_sanitized = hardware_name.replace("HARDWARE_", "")
        hardware_name_sanitized = re.sub(r'\W+', '_', hardware_name_sanitized)
    else:
        hardware_name_sanitized = "UnknownHardware"

    print(f"[Post-Build] Hardware name: {hardware_name_sanitized}")

    # Define the output merged binary path with the hardware name
    combined_bin_name = f"{hardware_name_sanitized}_combined.bin"
    combined_bin = os.path.join(build_dir, combined_bin_name)
    print(f"[Post-Build] Combined binary path: {combined_bin}")

    # Check if all binaries exist
    binaries = [
        (bootloader_bin, "Bootloader binary"),
        (partition_bin, "Partition table binary"),
        (firmware_bin, "Firmware binary"),
        (spiffs_bin, "SPIFFS binary")
    ]

    for bin_path, description in binaries:
        if not os.path.isfile(bin_path):
            print(f"[Error] {description} not found: {bin_path}")
            env.Exit(1)

    # Locate esptool.py
    esptool_dir = platform.get_package_dir("tool-esptoolpy")
    esptool_py = os.path.join(esptool_dir, "esptool.py")

    if not os.path.isfile(esptool_py):
        print(f"[Error] esptool.py not found at: {esptool_py}")
        env.Exit(1)
    else:
        print(f"[Post-Build] Found esptool.py at: {esptool_py}")

    # Merge binaries using esptool.py with offsets
    merge_cmd = [
        env.subst("$PYTHONEXE"),
        esptool_py,
        "--chip", "esp32",
        "merge_bin",
        "-o", combined_bin,
        bootloader_offset, bootloader_bin,
        partition_table_offset, partition_bin,
        app_offset, firmware_bin,
        spiffs_offset, spiffs_bin
    ]

    print(f"[Post-Build] Merging binaries into {combined_bin}...")
    result = subprocess.run(merge_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    if result.returncode != 0:
        print("[Error] Failed to merge binaries.")
        print(result.stderr)
        env.Exit(1)
    else:
        print(f"[Post-Build] Successfully created {combined_bin}")

# Initialize the build environment
env = DefaultEnvironment()

# Retrieve the build defines
cpp_defines = env.get('CPPDEFINES', [])

runscript = False
runscript = is_create_firmwarefile_defined()

if runscript:
    print("===== Firmware file creation =====")
    print("[Post-Build] CREATE_FIRMWAREFILE is defined. Registering post-build actions.")
    # Retrieve build directory and environment name
    build_dir = env.subst("$BUILD_DIR")
    env_name = env.subst("$PIOENV")
    # Register the post-build actions
    env.AddPostAction("buildprog", [run_buildfs, run_merge_bins])
# else:
#     print("===== Firmware file creation =====")
#     print("[Post-Build] CREATE_FIRMWAREFILE is not defined. Skipping firmware merge.")
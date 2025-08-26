# EleksTubeHAX_pio/script_modify_user_defines.py

import sys
import os
import re

def modify_user_defines(hardware_name, user_defines_path):
    with open(user_defines_path, 'r') as file:
        lines = file.readlines()

    new_lines = []
    in_hardware_section = False

    for line in lines:
        stripped_line = line.strip()

        # Detect the start of the hardware section
        if stripped_line.startswith('// ************* Type of the clock hardware'):
            in_hardware_section = True

        if in_hardware_section:
            # Comment out all hardware defines
            if re.match(r'^#define\s+HARDWARE_', stripped_line):
                if not line.lstrip().startswith('//'):
                    line = '//' + line
                    # Update stripped_line after modifying line
                    stripped_line = line.strip()

            # Uncomment the specific hardware define
            hardware_define_pattern = rf'^//\s*#define\s+HARDWARE_{re.escape(hardware_name)}\b'
            if re.match(hardware_define_pattern, stripped_line):
                line = line.replace('//', '', 1)
                # Update stripped_line after modifying line
                stripped_line = line.strip()
                in_hardware_section = False  # Exit after uncommenting the hardware define

            # End of hardware section
            if stripped_line.endswith('Type of the clock hardware *************'):
                in_hardware_section = False

        new_lines.append(line)

    with open(user_defines_path, 'w') as file:
        file.writelines(new_lines)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script_modify_user_defines.py <HARDWARE_NAME>")
        sys.exit(1)

    hardware_name = sys.argv[1]
    script_dir = os.path.dirname(os.path.realpath(__file__))
    user_defines_path = os.path.join(script_dir, 'src', '_USER_DEFINES.h')
    modify_user_defines(hardware_name, user_defines_path)
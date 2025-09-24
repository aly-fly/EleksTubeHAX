# In pure Python environment, the script looks broken!
# Import from SCons.Script is already available, if the PlatformIO build environment is used to call it.
# from SCons.Script import Import

# To have access to the PIO build environment variables, we need to import the env module from SCons.Script
import shutil
Import("env")

print("script_configure_tft_lib.py: Copying TFT config files...")

# Get the environment name
environmentname = env.subst("$PIOENV")
# Define target directory with the name of the env in the path
targetDir = ".pio/libdeps/" + environmentname + "/TFT_eSPI/"
# Define target file
targetFile = targetDir + "User_Setup.h"

# Copy using Python libraries.
#   shutil.copy() changes file timestamp -> lib is always recompiled.
#   shutil.copy2() keeps file timestamp -> lib is compiled once (until changed define files).
ret = shutil.copy2('./include/_USER_DEFINES.h', targetDir)
print("script_configure_tft_lib.py: Copied {ret}".format(**locals()))
ret2 = shutil.copy2('./include/GLOBAL_DEFINES.h', targetFile)
print("script_configure_tft_lib.py: Copied {ret2}".format(**locals()))

# Sanitize: ensure no HARDWARE_* defines remain set inside the library copy to avoid redefinition via -D flags,
# and rewrite header guards to avoid clashing with the project's GLOBAL_DEFINES_H_ include guard.
try:
	with open(targetFile, 'r', encoding='utf-8') as f:
		content = f.read()
	# Comment out any accidental HARDWARE_* defines that might have been uncommented
	import re
	content = re.sub(r"^(\s*)#\s*define\s+HARDWARE_([A-Z0-9_]+)\b", r"\1// #define HARDWARE_\2", content, flags=re.MULTILINE)
	# Rewrite header guards to a unique symbol so this file is not skipped when
	# the project has already included GLOBAL_DEFINES.h
	content = content.replace("GLOBAL_DEFINES_H_", "TFT_ESPI_USER_SETUP_H_")
	with open(targetFile, 'w', encoding='utf-8') as f:
		f.write(content)
	print("script_configure_tft_lib.py: Sanitized HARDWARE_* defines in User_Setup.h")
except Exception as e:
	print(f"script_configure_tft_lib.py: Warning: could not sanitize User_Setup.h: {e}")

print("script_configure_tft_lib.py: Done copying TFT config files!")

# # Patch the library header to silence a noisy preprocessor warning that is harmless in our setup
# try:
# 	tft_header = targetDir + "TFT_eSPI.h"
# 	with open(tft_header, 'r', encoding='utf-8') as f:
# 		hdr = f.read()
# 	# Only comment the exact warning line, leave logic intact
# 	if "#warning TFT_MISO set to -1" in hdr:
# 		hdr = hdr.replace("#warning TFT_MISO set to -1", "// #warning TFT_MISO set to -1 (silenced by EleksTubeHAX build)")
# 		with open(tft_header, 'w', encoding='utf-8') as f:
# 			f.write(hdr)
# 		print("script_configure_tft_lib.py: Silenced TFT_MISO warning in TFT_eSPI.h")
# except Exception as e:
# 	print(f"script_configure_tft_lib.py: Warning: could not patch TFT_eSPI.h: {e}")

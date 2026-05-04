# In pure Python environment, the script looks broken!
# Import from SCons.Script is already available, if the PlatformIO build environment is used to call it.
# from SCons.Script import Import

# To have access to the PIO build environment variables, we need to import the env module from SCons.Script
import os
import shutil
Import("env")

print("script_configure_tft_lib.py: Copying TFT config files...")

# Get the environment name
environmentname = env.subst("$PIOENV")
# Define target directory: prefer local modified lib, fall back to PIO libdeps
localLibDir = "lib/modified_TFT_eSPI/"
if os.path.exists(localLibDir):
    targetDir = localLibDir
else:
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

print("script_configure_tft_lib.py: Done copying TFT config files!")

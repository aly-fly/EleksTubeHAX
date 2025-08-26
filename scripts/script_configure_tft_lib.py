# In pure Python environment, the script looks broken!
# Import from SCons.Script is already available, if the PlatformIO build environment is used to call it.
#from SCons.Script import Import

#To have access to the PIO build environment variables, we need to import the env modul from SCons.Script
Import("env")

import shutil

print("===== copying TFT config files ===== ")

# https://stackoverflow.com/questions/123198/how-to-copy-files

# source file location: EleksTubeHAX_pio\src\GLOBAL_DEFINES.h  &  _USER_DEFINES.h
# target file location: EleksTubeHAX_pio\.pio\libdeps\<PIOBOARDNAME>\TFT_eSPI\User_Setup.h

# Get the board type from the used environment
#board = env.GetProjectOption("board")
# Get the environment name
environmentname = env.subst("$PIOENV")

# define target directory with the name of the board in the path
#targetDir = "./.pio/libdeps/" + board + "/TFT_eSPI"

# define target directory with the name of the env in the path
targetDir = "./.pio/libdeps/" + environmentname + "/TFT_eSPI"

# define target file
targetFile = targetDir + "/User_Setup.h"

# copy using Python libraries
# "copy" changes file timestamp -> lib is always recompiled.
# "copy2" keeps file timestamp -> lib is compiled once (until changed define files)
ret = shutil.copy2('./src/_USER_DEFINES.h', targetDir)
print("Copied {ret}".format(**locals()))
ret2 = shutil.copy2('./src/GLOBAL_DEFINES.h', targetFile)
print("Copied {ret2}".format(**locals()))

# copy using Windows command line
# native "copy" command keeps file timestamp -> lib is compiled once
# env.Execute("copy .\\src\\_USER_DEFINES.h .\\.pio\\libdeps\\<PIOBOARDNAME>\\TFT_eSPI")
# env.Execute("copy .\\src\\GLOBAL_DEFINES.h .\\.pio\\libdeps\\<PIOBOARDNAME>\\TFT_eSPI\\User_Setup.h")

# command1 = "copy .\\src\\_USER_DEFINES.h " + targetDir.replace("/", "\\")
# command2 = "copy .\\src\\GLOBAL_DEFINES.h " + targetFile.replace("/", "\\")
# env.Execute(command1)
# env.Execute(command2)


print("Done copying TFT config files!")

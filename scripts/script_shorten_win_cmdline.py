# Workaround for the Windows command line limit of 32767 characters.
#
# The Arduino framework packages for ESP32/ESP32-S2/ESP32-S3 add the include directories of
# all ESP-IDF components to every compile command (more than 200 "-I" flags). On ESP32-S3
# the resulting command line gets so long that spawning the actual compiler process
# (cc1plus) fails. The compiler then reports a misleading error:
#   xtensa-esp32s3-elf-g++: error: CreateProcess: No such file or directory
#
# This script removes the include directories of ESP-IDF components that are not used by
# this firmware, the Arduino core or its bundled libraries. That keeps every command line
# well below the limit. The build is left untouched on non-Windows systems.

import sys

Import("env")

# ESP-IDF components that are neither used by EleksTubeHAX nor by the Arduino core/libraries.
# See https://github.com/espressif/esp-idf - only their example sketches reference these headers.
UNUSED_SDK_COMPONENTS = (
    "espressif__esp-dsp",
    "esp-dl",
    "esp32-camera",
    "fb_gfx",
)

if sys.platform != "win32":
    print("script_shorten_win_cmdline.py: Not Windows, keeping all SDK include paths.")
else:
    # Imported here only: the SCons modules do not exist outside of a PlatformIO build.
    from SCons.Environment import Base as SConsEnvironmentBase

    originalAppend = SConsEnvironmentBase.Append
    droppedPaths = []

    def withoutUnusedSdkPaths(paths):
        if isinstance(paths, str):
            return paths
        keptPaths = []
        for path in paths:
            normalizedPath = str(path).replace("\\", "/")
            if any("/" + component + "/" in normalizedPath for component in UNUSED_SDK_COMPONENTS):
                droppedPaths.append(normalizedPath)
                continue
            keptPaths.append(path)
        return keptPaths

    def filterKeywords(keywords):
        if isinstance(keywords, dict) and "CPPPATH" in keywords:
            keywords = dict(keywords)
            keywords["CPPPATH"] = withoutUnusedSdkPaths(keywords["CPPPATH"])
        return keywords

    reported = []

    def appendWithoutUnusedSdkPaths(self, *args, **keywords):
        keywords = filterKeywords(keywords)
        args = tuple(filterKeywords(arg) for arg in args)
        if droppedPaths and not reported:
            reported.append(True)
            print("script_shorten_win_cmdline.py: Dropped %d unused ESP-IDF include paths "
                  "(Windows command line limit)." % len(droppedPaths))
        return originalAppend(self, *args, **keywords)

    # Patch on class level: SCons clones the environment for the Arduino core and library
    # builds, and a clone only inherits methods that are defined on the class.
    SConsEnvironmentBase.Append = appendWithoutUnusedSdkPaths

    print("script_shorten_win_cmdline.py: Windows command line workaround active.")

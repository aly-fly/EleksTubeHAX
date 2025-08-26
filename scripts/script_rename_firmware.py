Import("env")

my_flags = env.ParseFlags(env['BUILD_FLAGS'])

#import pprint
#print("rename_firmware.py: Parsed BUILD_FLAGS:")
#pprint.pprint(my_flags)

defines = dict()
for b in my_flags.get("CPPDEFINES"):
    if isinstance(b, list):
        defines[b[0]] = b[1]
    else:
        defines[b] = b

env.Replace(PROGNAME="%s_v%s" % (
    env["PIOENV"],
    defines.get("BUILDVER")
))
print("script_rename_firmware.py: Firmware binary name prefix is:", env['PROGNAME'])

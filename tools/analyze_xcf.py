#!/usr/bin/env python3
"""Analyze XCF file structure to find broken UTF-8 strings."""
import struct
import sys

def analyze_xcf(path):
    with open(path, 'rb') as fh:
        f = fh.read()

    magic = f[:14]
    print(f"Magic: {magic}")
    version_str = magic[9:13].replace(b'v', b'').replace(b'\x00', b'').decode('ascii')
    version_num = int(version_str)
    use_64bit = version_num >= 11  # v011+ uses 8-byte offsets
    print(f"Version: {version_num} (64-bit offsets: {use_64bit})")

    pos = 14
    width = struct.unpack('>I', f[pos:pos+4])[0]; pos += 4
    height = struct.unpack('>I', f[pos:pos+4])[0]; pos += 4
    ctype = struct.unpack('>I', f[pos:pos+4])[0]; pos += 4

    # v011+ has precision field
    if use_64bit:
        precision = struct.unpack('>I', f[pos:pos+4])[0]; pos += 4
    else:
        precision = 0

    print(f"Image: {width}x{height}, type={ctype}, precision={precision}")

    # Read properties
    PROP_END = 0
    while pos < len(f) - 8:
        ptype = struct.unpack('>I', f[pos:pos+4])[0]
        psize = struct.unpack('>I', f[pos+4:pos+8])[0]
        if ptype == PROP_END:
            print(f"  PROP_END at offset {pos}")
            pos += 8
            break
        print(f"  Property type={ptype} size={psize} at offset {pos}")
        pos += 8 + psize

    # Read layer offsets
    layer_offsets = []
    if use_64bit:
        while pos < len(f) - 8:
            off = struct.unpack('>Q', f[pos:pos+8])[0]
            pos += 8
            if off == 0:
                break
            layer_offsets.append(off)
    else:
        while pos < len(f) - 4:
            off = struct.unpack('>I', f[pos:pos+4])[0]
            pos += 4
            if off == 0:
                break
            layer_offsets.append(off)

    print(f"\n{len(layer_offsets)} layers found:")

    for i, loff in enumerate(layer_offsets):
        p = loff
        lw = struct.unpack('>I', f[p:p+4])[0]; p += 4
        lh = struct.unpack('>I', f[p:p+4])[0]; p += 4
        lt = struct.unpack('>I', f[p:p+4])[0]; p += 4
        nlen = struct.unpack('>I', f[p:p+4])[0]; p += 4
        name_raw = f[p:p+nlen-1]  # exclude null terminator
        try:
            decoded = name_raw.decode('utf-8')
            print(f"  Layer {i:2d}: \"{decoded}\" ({lw}x{lh})")
        except UnicodeDecodeError as e:
            print(f"  Layer {i:2d}: *** BROKEN UTF-8 *** raw={name_raw[:60]} err={e}")

if __name__ == '__main__':
    for path in sys.argv[1:]:
        print(f"\n=== {path} ===")
        analyze_xcf(path)

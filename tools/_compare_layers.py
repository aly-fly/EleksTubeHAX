#!/usr/bin/env python3
"""Compare layer structure between two XCF files."""
import struct, sys

def u32(d, p): return struct.unpack('>I', d[p:p+4])[0]
def u64(d, p): return struct.unpack('>Q', d[p:p+8])[0]

def skip_props(d, pos):
    while pos + 8 <= len(d):
        pt = u32(d, pos); ps = u32(d, pos + 4); pos += 8
        if pt == 0: break
        pos += ps
    return pos

def get_layers(path):
    d = open(path, 'rb').read()
    pos = 14 + 4 + 4 + 4 + 4
    pos = skip_props(d, pos)
    layers = []
    while pos + 8 <= len(d):
        off = u64(d, pos); pos += 8
        if off == 0: break
        w = u32(d, off); h = u32(d, off + 4); lt = u32(d, off + 8); nlen = u32(d, off + 12)
        name = d[off + 16:off + 16 + nlen - 1].decode('utf-8', 'replace')
        layers.append((off, w, h, name))
    return layers

print("=== CLEAN (12 layers) ===")
for i, (off, w, h, name) in enumerate(get_layers(sys.argv[1])):
    print(f"  {i}: \"{name}\" ({w}x{h}) at 0x{off:X}")

print()
print("=== RESTORED v7 (13 layers) ===")
for i, (off, w, h, name) in enumerate(get_layers(sys.argv[2])):
    print(f"  {i}: \"{name}\" ({w}x{h}) at 0x{off:X}")

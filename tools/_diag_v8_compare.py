#!/usr/bin/env python3
"""Compare v7 and v8 at byte level to find structural differences."""
import struct, sys

def u32(d, p): return struct.unpack('>I', d[p:p+4])[0]
def u64(d, p): return struct.unpack('>Q', d[p:p+8])[0]

v7 = open(sys.argv[1], 'rb').read()
v8 = open(sys.argv[2], 'rb').read()

# Check header + image properties are identical
print("=== Header compare ===")
# Image properties end at 0x16A6 (PROP_END at 0x169E + 8 = 0x16A6)
header_len = 0x16A6
if v7[:header_len] == v8[:header_len]:
    print(f"  First 0x{header_len:X} bytes (header+props): IDENTICAL")
else:
    for i in range(header_len):
        if v7[i] != v8[i]:
            print(f"  FIRST DIFF at 0x{i:X}: v7=0x{v7[i]:02X} v8=0x{v8[i]:02X}")
            break

# Layer table comparison
print("\n=== Layer table compare ===")
lt_start = 0x16A6
for li in range(13):
    off = lt_start + li * 8
    v7_off = u64(v7, off)
    v8_off = u64(v8, off)
    print(f"  Layer {li:2d}: v7=0x{v7_off:08X} v8=0x{v8_off:08X} {'SAME' if v7_off == v8_off else 'DIFF'}")

# Compare layer 0 structure byte-by-byte
print("\n=== Layer 0 detailed compare ===")
v7_l0 = u64(v7, lt_start)
v8_l0 = u64(v8, lt_start)

# Read 256 bytes from each layer start
N = 256
print(f"V7 layer 0 at 0x{v7_l0:X}, V8 layer 0 at 0x{v8_l0:X}")
v7_chunk = v7[v7_l0:v7_l0+N]
v8_chunk = v8[v8_l0:v8_l0+N]

if v7_chunk == v8_chunk:
    print("  First 256 bytes: IDENTICAL")
else:
    diffs = 0
    for i in range(min(len(v7_chunk), len(v8_chunk))):
        if v7_chunk[i] != v8_chunk[i]:
            if diffs < 10:
                print(f"  DIFF at offset+{i} (v7=0x{v7_l0+i:X} v8=0x{v8_l0+i:X}): "
                      f"v7=0x{v7_chunk[i]:02X} v8=0x{v8_chunk[i]:02X}")
            diffs += 1
    print(f"  Total diffs in first {N} bytes: {diffs}")

# Check image properties parasites
print("\n=== Image parasites check ===")
pos = 0x1E  # image properties start
while pos + 8 <= min(len(v7), len(v8)):
    pt = u32(v7, pos)
    ps = u32(v7, pos + 4)
    if pt == 0:
        break
    if pt == 21:  # PROP_PARASITES
        data_start = pos + 8
        data_end = data_start + ps
        print(f"  PROP_PARASITES at 0x{pos:X}, size={ps}")
        # Parse parasites
        pp = data_start
        pi = 0
        while pp < data_end:
            if pp + 4 > data_end: break
            pname_len = u32(v7, pp); pp += 4
            if pname_len == 0 or pname_len > 1000:
                print(f"    Parasite {pi}: BAD name_len={pname_len} at 0x{pp-4:X}")
                break
            pname = v7[pp:pp+pname_len]
            pp += pname_len
            if pp + 8 > data_end: break
            pflags = u32(v7, pp); pp += 4
            pdata_len = u32(v7, pp); pp += 4
            pp += pdata_len
            try:
                name_str = pname.rstrip(b'\x00').decode('utf-8')
                print(f"    Parasite {pi}: \"{name_str}\" flags={pflags} data_len={pdata_len}")
            except UnicodeDecodeError as e:
                print(f"    Parasite {pi}: INVALID UTF-8! raw={pname.hex()} err={e}")
            pi += 1
    pos += 8 + ps

# Try to validate v8 as GIMP would
print("\n=== V8 string validation ===")
def skip_props_v(d, pos, label=""):
    while pos + 8 <= len(d):
        pt = u32(d, pos); ps = u32(d, pos + 4)
        pos += 8
        if pt == 0:
            break
        if pt == 21:  # parasites
            # validate parasite strings
            pp = pos
            pe = pos + ps
            pi = 0
            while pp < pe:
                if pp + 4 > pe: break
                pnl = u32(d, pp); pp += 4
                if pnl == 0 or pnl > 1000: break
                pn = d[pp:pp+pnl]; pp += pnl
                if pp + 8 > pe: break
                pp += 4  # flags
                pdl = u32(d, pp); pp += 4
                pp += pdl
                try:
                    pn.rstrip(b'\x00').decode('utf-8')
                except:
                    print(f"  {label} parasite {pi}: INVALID UTF-8: {pn.hex()}")
                pi += 1
        pos += ps
    return pos

# Check image properties
p = 0x1E
p = skip_props_v(v8, p, "img")

# skip layer table
while p + 8 <= len(v8):
    off = u64(v8, p); p += 8
    if off == 0: break
while p + 8 <= len(v8):
    off = u64(v8, p); p += 8
    if off == 0: break

# Check each layer
lt_start = 0x16A6
for li in range(13):
    loff = u64(v8, lt_start + li * 8)
    if loff + 20 > len(v8): continue
    w = u32(v8, loff); h = u32(v8, loff+4); lt = u32(v8, loff+8)
    nlen = u32(v8, loff+12)
    if nlen == 0 or nlen > 10000:
        print(f"  Layer {li}: BAD nlen={nlen} at 0x{loff:X}")
        continue
    name_raw = v8[loff+16:loff+16+nlen]
    try:
        name = name_raw[:-1].decode('utf-8')
    except UnicodeDecodeError as e:
        print(f"  Layer {li}: INVALID UTF-8 in name! raw={name_raw.hex()} err={e}")
        continue
    
    header_end = loff + 16 + nlen
    props_end = skip_props_v(v8, header_end, f"layer{li}")
    
    # Check hierarchy
    if props_end + 16 > len(v8): continue
    hier_off = u64(v8, props_end)
    mask_off = u64(v8, props_end + 8)
    
    if hier_off == 0 or hier_off >= len(v8):
        print(f"  Layer {li}: BAD hier_off=0x{hier_off:X}")
        continue
    
    hw = u32(v8, hier_off); hh = u32(v8, hier_off+4); bp = u32(v8, hier_off+8)
    if hw != w or hh != h:
        print(f"  Layer {li}: Hierarchy w/h mismatch! layer={w}x{h} hier={hw}x{hh}")
    if bp not in (1,2,3,4):
        print(f"  Layer {li}: BAD bpp={bp}")
    
    print(f"  Layer {li} \"{name}\": OK (hier=0x{hier_off:X} mask=0x{mask_off:X})")

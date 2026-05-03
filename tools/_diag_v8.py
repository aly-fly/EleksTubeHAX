#!/usr/bin/env python3
"""Diagnose v8 vs v7 structural differences."""
import struct, sys

def u32(d, p): return struct.unpack('>I', d[p:p+4])[0]
def u64(d, p): return struct.unpack('>Q', d[p:p+8])[0]

def skip_props(d, pos):
    props = []
    while pos + 8 <= len(d):
        pt = u32(d, pos); ps = u32(d, pos + 4)
        props.append((pt, ps, pos))
        pos += 8
        if pt == 0:
            break
        if pos + ps > len(d):
            print(f"    TRUNCATED prop type={pt} size={ps} at 0x{pos-8:X}")
            break
        pos += ps
    return props, pos

def dump_header(path, label):
    d = open(path, 'rb').read()
    print(f"\n{'='*60}")
    print(f"{label}: {path}")
    print(f"Size: {len(d):,} bytes")
    print(f"Magic: {d[:14]}")
    
    pos = 14
    cw = u32(d, pos); ch = u32(d, pos+4); bt = u32(d, pos+8)
    print(f"Canvas: {cw}x{ch}, type={bt}")
    pos += 12
    
    # Check version for precision field
    ver = int(d[9:13].replace(b'v', b'').replace(b'\x00', b'').decode())
    if ver >= 4:
        prec = u32(d, pos); pos += 4
        print(f"Precision: {prec}")
    
    # Image properties
    print(f"\nImage properties at 0x{pos:X}:")
    props, pos = skip_props(d, pos)
    for pt, ps, pp in props:
        if pt == 0:
            print(f"  PROP_END at 0x{pp:X}")
        else:
            print(f"  type={pt} size={ps} at 0x{pp:X}")
    
    # Layer table
    print(f"\nLayer table at 0x{pos:X}:")
    layers = []
    while pos + 8 <= len(d):
        off = u64(d, pos); pos += 8
        if off == 0: break
        layers.append(off)
        print(f"  [{len(layers)-1}] 0x{off:08X}")
    print(f"Layer table end at 0x{pos:X}")
    
    # Channel table
    print(f"\nChannel table at 0x{pos:X}:")
    channels = []
    while pos + 8 <= len(d):
        off = u64(d, pos); pos += 8
        if off == 0: break
        channels.append(off)
        print(f"  [{len(channels)-1}] 0x{off:08X}")
    print(f"Channel table end at 0x{pos:X}")
    
    # Parse each layer
    for li, loff in enumerate(layers):
        print(f"\n--- Layer {li} at 0x{loff:X} ---")
        if loff + 20 > len(d):
            print("  OUT OF BOUNDS")
            continue
        w = u32(d, loff); h = u32(d, loff+4); lt = u32(d, loff+8)
        nlen = u32(d, loff+12)
        if nlen == 0 or nlen > 10000:
            print(f"  BAD nlen={nlen}")
            # Show raw bytes around this offset
            start = max(0, loff-8)
            end = min(len(d), loff+32)
            print(f"  Raw bytes [{start:X}..{end:X}]: {d[start:end].hex()}")
            continue
        name_raw = d[loff+16:loff+16+nlen]
        try:
            name = name_raw[:-1].decode('utf-8')
        except:
            name = f"<BAD UTF-8: {name_raw[:20].hex()}>"
        print(f"  {w}x{h} type={lt} name({nlen})=\"{name}\"")
        
        header_end = loff + 16 + nlen
        
        # Properties
        print(f"  Properties at 0x{header_end:X}:")
        lprops, lprops_end = skip_props(d, header_end)
        for pt, ps, pp in lprops:
            if pt == 0:
                print(f"    PROP_END at 0x{pp:X}")
            else:
                print(f"    type={pt} size={ps} at 0x{pp:X}")
        
        # Hierarchy + mask offsets
        if lprops_end + 16 > len(d):
            print("  TRUNCATED after properties")
            continue
        hier_off = u64(d, lprops_end)
        mask_off = u64(d, lprops_end + 8)
        print(f"  Hierarchy offset: 0x{hier_off:X}")
        print(f"  Mask offset: 0x{mask_off:X}")
        
        if hier_off == 0 or hier_off >= len(d) - 12:
            print("  BAD hierarchy offset")
            continue
        
        hw = u32(d, hier_off); hh = u32(d, hier_off+4); bpp = u32(d, hier_off+8)
        print(f"  Hierarchy: {hw}x{hh} bpp={bpp}")
        
        # Level table
        hp = hier_off + 12
        level_offs = []
        while hp + 8 <= len(d):
            lo = u64(d, hp); hp += 8
            if lo == 0: break
            level_offs.append(lo)
        print(f"  {len(level_offs)} level(s)")
        
        if level_offs:
            lvl0 = level_offs[0]
            if lvl0 + 8 <= len(d):
                lw = u32(d, lvl0); lh = u32(d, lvl0+4)
                print(f"  Level 0 at 0x{lvl0:X}: {lw}x{lh}")
                
                # Count tiles
                tp = lvl0 + 8
                tc = 0
                first_tile = 0
                while tp + 8 <= len(d):
                    to = u64(d, tp); tp += 8
                    if to == 0: break
                    if tc == 0: first_tile = to
                    tc += 1
                print(f"  {tc} tiles, first at 0x{first_tile:X}")
        
        if li >= 3:
            break  # enough for diagnosis

for path, label in [(sys.argv[1], "V7"), (sys.argv[2], "V8")]:
    dump_header(path, label)

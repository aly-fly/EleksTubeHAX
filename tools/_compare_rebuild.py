#!/usr/bin/env python3
"""Compare original clean XCF vs rebuilt clean XCF at structural level.
Find exactly where and why they diverge."""
import struct, sys

def u32(d, p): return struct.unpack('>I', d[p:p+4])[0]
def u64(d, p): return struct.unpack('>Q', d[p:p+8])[0]

def skip_props(d, pos):
    while pos + 8 <= len(d):
        pt = u32(d, pos); ps = u32(d, pos + 4); pos += 8
        if pt == 0: break
        pos += ps
    return pos

def walk_xcf(d, label):
    """Walk through XCF structure, returning a list of (offset, type, description)."""
    events = []
    ver = int(d[9:13].replace(b'v', b'').replace(b'\x00', b'').decode())
    
    pos = 14
    events.append((pos, 'canvas_w', u32(d, pos))); pos += 4
    events.append((pos, 'canvas_h', u32(d, pos))); pos += 4
    events.append((pos, 'base_type', u32(d, pos))); pos += 4
    if ver >= 4:
        events.append((pos, 'precision', u32(d, pos))); pos += 4
    
    # Properties
    prop_start = pos
    while pos + 8 <= len(d):
        pt = u32(d, pos); ps = u32(d, pos + 4)
        events.append((pos, f'img_prop', f'type={pt} size={ps}'))
        pos += 8
        if pt == 0: break
        pos += ps
    events.append((pos, 'layer_table_start', ''))
    
    # Layer offsets
    layer_offsets = []
    while pos + 8 <= len(d):
        off = u64(d, pos)
        events.append((pos, 'layer_ptr', f'-> 0x{off:X}'))
        pos += 8
        if off == 0: break
        layer_offsets.append(off)
    
    events.append((pos, 'channel_table_start', ''))
    channel_offsets = []
    while pos + 8 <= len(d):
        off = u64(d, pos)
        events.append((pos, 'channel_ptr', f'-> 0x{off:X}'))
        pos += 8
        if off == 0: break
        channel_offsets.append(off)
    
    events.append((pos, 'tables_end', ''))
    
    # Walk each layer
    for li, loff in enumerate(layer_offsets):
        events.append((loff, f'layer[{li}]_start', ''))
        w = u32(d, loff); h = u32(d, loff+4); lt = u32(d, loff+8)
        nlen = u32(d, loff+12)
        name = d[loff+16:loff+16+nlen-1].decode('utf-8', errors='replace')
        events.append((loff, f'layer[{li}]_header', f'{w}x{h} type={lt} "{name}"'))
        
        p = loff + 16 + nlen
        # Properties
        while p + 8 <= len(d):
            pt = u32(d, p); ps = u32(d, p + 4)
            events.append((p, f'layer[{li}]_prop', f'type={pt} size={ps}'))
            p += 8
            if pt == 0: break
            p += ps
        
        hier_off = u64(d, p)
        mask_off = u64(d, p + 8)
        events.append((p, f'layer[{li}]_hier_ptr', f'-> 0x{hier_off:X}'))
        events.append((p+8, f'layer[{li}]_mask_ptr', f'-> 0x{mask_off:X}'))
        p += 16
        
        # Check what's at position p
        events.append((p, f'layer[{li}]_after_ptrs', f'next bytes: {d[p:p+16].hex()}'))
        
        # Hierarchy
        events.append((hier_off, f'layer[{li}]_hier', f'{u32(d,hier_off)}x{u32(d,hier_off+4)} bpp={u32(d,hier_off+8)}'))
        bpp = u32(d, hier_off+8)
        
        hp = hier_off + 12
        level_offsets = []
        while hp + 8 <= len(d):
            lo = u64(d, hp)
            events.append((hp, f'layer[{li}]_level_ptr', f'-> 0x{lo:X}'))
            hp += 8
            if lo == 0: break
            level_offsets.append(lo)
        
        # Level 0
        if level_offsets:
            lvl0 = level_offsets[0]
            lw = u32(d, lvl0); lh = u32(d, lvl0+4)
            events.append((lvl0, f'layer[{li}]_level0', f'{lw}x{lh}'))
            
            tp = lvl0 + 8
            tc = 0
            while tp + 8 <= len(d):
                to = u64(d, tp)
                if tc < 3:
                    events.append((tp, f'layer[{li}]_tile_ptr[{tc}]', f'-> 0x{to:X}'))
                tp += 8
                if to == 0:
                    events.append((tp-8, f'layer[{li}]_tile_end', f'{tc} tiles total'))
                    break
                tc += 1
            
            events.append((tp, f'layer[{li}]_after_tiles', f'next bytes: {d[tp:tp+16].hex()}'))
        
        if li >= 1:
            break  # Just check first 2 layers
    
    return sorted(events, key=lambda x: x[0])

orig = open(sys.argv[1], 'rb').read()
rebuilt = open(sys.argv[2], 'rb').read()

print(f"Original: {len(orig):,} bytes")
print(f"Rebuilt:  {len(rebuilt):,} bytes")
print(f"Diff:     {len(orig) - len(rebuilt):,} bytes\n")

# Walk both
orig_events = walk_xcf(orig, "ORIG")
rebuilt_events = walk_xcf(rebuilt, "REBUILT")

# Show side by side
print(f"{'ORIG':>12s} {'REBUILT':>12s}  {'Type':30s}  Details")
print("-" * 100)

oi = 0
ri = 0
while oi < len(orig_events) and ri < len(rebuilt_events):
    oe = orig_events[oi]
    re = rebuilt_events[ri]
    
    # Match by event type
    if oe[1] == re[1]:
        marker = " " if oe[2] == re[2] else "*"
        print(f"  0x{oe[0]:08X}   0x{re[0]:08X}  {oe[1]:30s}  {marker} {oe[2]}{'  vs  ' + str(re[2]) if oe[2] != re[2] else ''}")
        oi += 1
        ri += 1
    else:
        print(f"  0x{oe[0]:08X}   {'---':>10s}  {oe[1]:30s}    {oe[2]}")
        oi += 1

#!/usr/bin/env python3
"""Compare corrupted and restored XCF byte-by-byte, and do a detailed GIMP-like parse."""
import struct, sys, os
os.environ.setdefault('PYTHONIOENCODING', 'utf-8')
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def u32(d, pos): return struct.unpack('>I', d[pos:pos+4])[0]
def u64(d, pos): return struct.unpack('>Q', d[pos:pos+8])[0]

def skip_properties(d, pos):
    while pos + 8 <= len(d):
        pt = u32(d, pos); ps = u32(d, pos + 4); pos += 8
        if pt == 0: break
        if pos + ps > len(d): break
        pos += ps
    return pos

def main():
    corrupt_path = sys.argv[1]
    restored_path = sys.argv[2]
    
    with open(corrupt_path, 'rb') as f: c = f.read()
    with open(restored_path, 'rb') as f: r = f.read()
    
    print(f"Corrupted: {len(c):,} bytes")
    print(f"Restored:  {len(r):,} bytes")
    
    if len(c) != len(r):
        print("SIZE MISMATCH!")
        return
    
    # Find all differences
    diffs = []
    for i in range(len(c)):
        if c[i] != r[i]:
            diffs.append(i)
    
    print(f"\nTotal differing bytes: {len(diffs)}")
    if not diffs:
        print("Files are identical!")
        return
    
    print(f"First diff at: 0x{diffs[0]:X}")
    print(f"Last diff at:  0x{diffs[-1]:X}")
    
    # Group diffs into ranges
    ranges = []
    start = diffs[0]
    end = diffs[0]
    for d_pos in diffs[1:]:
        if d_pos <= end + 16:  # within 16 bytes = same range
            end = d_pos
        else:
            ranges.append((start, end))
            start = d_pos
            end = d_pos
    ranges.append((start, end))
    
    print(f"\n{len(ranges)} diff ranges:")
    for s, e in ranges[:50]:
        print(f"  0x{s:08X} - 0x{e:08X}  ({e-s+1} bytes)  "
              f"corrupt: {c[s:min(s+8,e+1)].hex()} -> restored: {r[s:min(s+8,e+1)].hex()}")
    if len(ranges) > 50:
        print(f"  ... and {len(ranges)-50} more ranges")
    
    # Now parse the restored file as GIMP would
    print("\n\n=== GIMP-style parse of RESTORED file ===")
    d = r
    
    assert d[:9] == b'gimp xcf '
    ver = d[9:13]
    print(f"Version: {ver}")
    
    pos = 14
    canvas_w = u32(d, pos); pos += 4
    canvas_h = u32(d, pos); pos += 4
    base_type = u32(d, pos); pos += 4
    precision = u32(d, pos); pos += 4
    print(f"Canvas: {canvas_w}x{canvas_h}, type={base_type}, precision={precision}")
    
    pos = skip_properties(d, pos)
    print(f"After image properties: 0x{pos:X}")
    
    # Read layer offset table
    layer_offsets = []
    table_start = pos
    while pos + 8 <= len(d):
        off = u64(d, pos)
        pos += 8
        if off == 0: break
        layer_offsets.append((pos - 8, off))
    print(f"Layer table: {len(layer_offsets)} entries at 0x{table_start:X}-0x{pos:X}")
    
    # Read channel offset table
    chan_start = pos
    channel_offsets = []
    while pos + 8 <= len(d):
        off = u64(d, pos)
        pos += 8
        if off == 0: break
        channel_offsets.append((pos - 8, off))
    print(f"Channel table: {len(channel_offsets)} entries at 0x{chan_start:X}-0x{pos:X}")
    
    # Parse each layer as GIMP would
    for li, (tpos, loff) in enumerate(layer_offsets):
        print(f"\n--- Layer {li} (table entry at 0x{tpos:X} -> 0x{loff:X}) ---")
        
        if loff >= len(d) - 20:
            print(f"  OFFSET OUT OF BOUNDS!")
            continue
        
        w = u32(d, loff)
        h = u32(d, loff + 4)
        lt = u32(d, loff + 8)
        nlen = u32(d, loff + 12)
        
        if w == 0 or w > 65536 or h == 0 or h > 65536:
            print(f"  BAD dimensions: {w}x{h}")
            continue
        if nlen == 0 or nlen > 10000:
            print(f"  BAD nlen: {nlen}")
            continue
        
        name = d[loff+16:loff+16+nlen-1].decode('utf-8', errors='replace')
        print(f"  \"{name}\" {w}x{h} type={lt}")
        
        # Skip to after properties
        prop_start = loff + 16 + nlen
        prop_end = skip_properties(d, prop_start)
        
        if prop_end + 16 > len(d):
            print(f"  CAN'T READ hier/mask offsets (prop_end=0x{prop_end:X})")
            continue
        
        hier_off = u64(d, prop_end)
        mask_off = u64(d, prop_end + 8)
        print(f"  hier_off=0x{hier_off:X}, mask_off=0x{mask_off:X}")
        
        # Check hierarchy
        if hier_off == 0 or hier_off >= len(d) - 12:
            print(f"  HIERARCHY OFFSET INVALID!")
            continue
        
        hw = u32(d, hier_off)
        hh = u32(d, hier_off + 4)
        hbpp = u32(d, hier_off + 8)
        print(f"  hierarchy: {hw}x{hh} bpp={hbpp}")
        
        if hw != w or hh != h:
            print(f"  HIERARCHY W/H MISMATCH! (expected {w}x{h})")
            continue
        if hbpp not in (1, 2, 3, 4):
            print(f"  HIERARCHY BAD BPP!")
            continue
        
        # Read level offsets from hierarchy
        hp = hier_off + 12
        level_offsets_here = []
        while hp + 8 <= len(d):
            lo = u64(d, hp)
            hp += 8
            if lo == 0: break
            level_offsets_here.append(lo)
        
        print(f"  {len(level_offsets_here)} level offsets")
        
        # Check level 0
        if not level_offsets_here:
            print(f"  NO LEVELS!")
            continue
        
        level0_off = level_offsets_here[0]
        if level0_off >= len(d) - 8:
            print(f"  LEVEL 0 OFFSET OUT OF BOUNDS: 0x{level0_off:X}")
            continue
        
        lw = u32(d, level0_off)
        lh = u32(d, level0_off + 4)
        print(f"  level 0: {lw}x{lh} at 0x{level0_off:X}")
        
        if lw != w or lh != h:
            print(f"  LEVEL 0 W/H MISMATCH!")
            continue
        
        # Read tile offsets
        tp = level0_off + 8
        tile_offsets = []
        while tp + 8 <= len(d):
            to = u64(d, tp)
            tp += 8
            if to == 0: break
            tile_offsets.append(to)
        
        tiles_x = (w + 63) // 64
        tiles_y = (h + 63) // 64
        expected = tiles_x * tiles_y
        
        in_bounds = sum(1 for t in tile_offsets if 0 < t < len(d))
        print(f"  {len(tile_offsets)} tiles (expected {expected}), {in_bounds} in-bounds")
        
        if tile_offsets:
            print(f"  first tile: 0x{tile_offsets[0]:X}, last tile: 0x{tile_offsets[-1]:X}")
            # Check if tiles are sequential and reasonable
            if len(tile_offsets) >= 2:
                sizes = [tile_offsets[i+1] - tile_offsets[i] for i in range(min(5, len(tile_offsets)-1))]
                print(f"  first 5 tile sizes: {sizes}")
        
        # Check stub levels
        for sli, soff in enumerate(level_offsets_here[1:], 1):
            if soff >= len(d) - 8:
                print(f"  level {sli}: OFFSET OUT OF BOUNDS 0x{soff:X}")
                continue
            sw = u32(d, soff)
            sh = u32(d, soff + 4)
            exp_w = max(1, w >> sli)
            exp_h = max(1, h >> sli)
            ok = "OK" if (sw == exp_w and sh == exp_h) else f"MISMATCH (expected {exp_w}x{exp_h})"
            print(f"  level {sli}: {sw}x{sh} at 0x{soff:X} {ok}")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <corrupted.xcf> <restored.xcf>")
        sys.exit(1)
    main()

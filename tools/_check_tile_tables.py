#!/usr/bin/env python3
"""Check if tile offset tables in the corrupted file have CR corruption."""
import struct, sys, os
os.environ.setdefault('PYTHONIOENCODING', 'utf-8')
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def u32(d, pos): return struct.unpack('>I', d[pos:pos+4])[0]
def u64(d, pos): return struct.unpack('>Q', d[pos:pos+8])[0]
def p32(v): return struct.pack('>I', v)

def skip_properties(d, pos):
    while pos + 8 <= len(d):
        pt = u32(d, pos); ps = u32(d, pos + 4); pos += 8
        if pt == 0: break
        if pos + ps > len(d): break
        pos += ps
    return pos

def find_hierarchy(d, stored_off, w, h, after_pos, max_delta=1000):
    target = p32(w) + p32(h)
    for delta in range(0, max_delta + 1):
        for sign in [0, -1, 1]:
            if delta == 0 and sign != 0: continue
            pos = stored_off + delta * (sign if sign else 1)
            if delta == 0: pos = stored_off
            if pos < after_pos or pos + 20 >= len(d): continue
            if d[pos:pos+8] == target:
                bpp = u32(d, pos + 8)
                if bpp in (1,2,3,4):
                    if pos + 20 <= len(d):
                        lo = u64(d, pos + 12)
                        if 0 < lo < len(d):
                            return pos, bpp
    return None, None

def find_level(d, stored_off, w, h, after_pos, max_delta=1000):
    target = p32(w) + p32(h)
    for delta in range(0, max_delta + 1):
        for sign in [0, -1, 1]:
            if delta == 0 and sign != 0: continue
            pos = stored_off + delta * (sign if sign else 1)
            if delta == 0: pos = stored_off
            if pos < after_pos or pos + 16 >= len(d): continue
            if d[pos:pos+8] == target:
                if pos + 16 <= len(d):
                    to = u64(d, pos + 8)
                    if 0 < to < len(d):
                        return pos
    return None

def check_layer_header(d, pos):
    if pos + 16 >= len(d): return None
    w = u32(d, pos); h = u32(d, pos+4); lt = u32(d, pos+8); nlen = u32(d, pos+12)
    if w == 0 or h == 0 or w > 65536 or h > 65536: return None
    if lt > 10: return None
    if nlen == 0 or nlen > 500: return None
    if pos + 16 + nlen > len(d): return None
    name = d[pos+16:pos+16+nlen]
    if name[-1:] != b'\x00': return None
    try: ns = name[:-1].decode('utf-8')
    except: return None
    if not all(32 <= b < 127 or b > 127 for b in name[:-1]): return None
    return {'w': w, 'h': h, 'type': lt, 'nlen': nlen, 'name': ns}

def find_layer_header(d, stored_off, max_delta=1000):
    for delta in range(0, max_delta + 1):
        for sign in [0, -1, 1]:
            if delta == 0 and sign != 0: continue
            pos = stored_off + delta * (sign if sign else 1)
            if delta == 0: pos = stored_off
            if pos < 0 or pos + 20 >= len(d): continue
            info = check_layer_header(d, pos)
            if info: return pos, info
    return None, None

def main():
    path = sys.argv[1]
    with open(path, 'rb') as f:
        d = f.read()
    
    # Parse header
    pos = 14
    canvas_w = u32(d, pos); pos += 4
    canvas_h = u32(d, pos); pos += 4
    base_type = u32(d, pos); pos += 4
    pos += 4  # precision
    pos = skip_properties(d, pos)
    
    # Layer offsets
    stored_layer_offsets = []
    while pos + 8 <= len(d):
        off = u64(d, pos); pos += 8
        if off == 0: break
        stored_layer_offsets.append(off)
    while pos + 8 <= len(d):
        off = u64(d, pos); pos += 8
        if off == 0: break
    
    prev_delta = 0
    for i, stored in enumerate(stored_layer_offsets):
        max_d = max(abs(prev_delta) + 200, 1000)
        actual, info = find_layer_header(d, stored, max_delta=max_d)
        if not actual: continue
        delta = actual - stored; prev_delta = delta
        
        lp = actual + 16 + info['nlen']
        lp = skip_properties(d, lp)
        if lp + 16 > len(d): continue
        
        stored_hier = u64(d, lp)
        hier_pos, bpp = find_hierarchy(d, stored_hier, info['w'], info['h'], actual, max_d+200)
        if not hier_pos: continue
        
        stored_loff = u64(d, hier_pos + 12)
        level_pos = find_level(d, stored_loff, info['w'], info['h'], hier_pos, max_d+200)
        if not level_pos: continue
        lvl_delta = level_pos - stored_loff
        
        # Read tile offset table
        tp = level_pos + 8
        tile_table_start = tp
        tile_offsets = []
        while tp + 8 <= len(d):
            to = u64(d, tp); tp += 8
            if to == 0: break
            tile_offsets.append(to)
        tile_table_end = tp
        
        tiles_x = (info['w'] + 63) // 64
        tiles_y = (info['h'] + 63) // 64
        expected = tiles_x * tiles_y
        
        print(f"\nLayer {i}: \"{info['name']}\" ({info['w']}x{info['h']}) delta={delta} lvl_delta={lvl_delta}")
        print(f"  Table: 0x{tile_table_start:X}-0x{tile_table_end:X} ({len(tile_offsets)}/{expected} tiles)")
        
        if len(tile_offsets) == 0: continue
        
        # Check for 0x0D 0x0A in raw tile table bytes
        raw = d[tile_table_start:tile_table_end]
        crlf_positions = []
        for j in range(len(raw) - 1):
            if raw[j] == 0x0D and raw[j+1] == 0x0A:
                crlf_positions.append(tile_table_start + j)
        
        # Check for 0x0A bytes (potential sites where 0x0D was removed)
        lf_positions = [tile_table_start + j for j in range(len(raw)) if raw[j] == 0x0A]
        
        print(f"  Raw table: {len(crlf_positions)} CRLF pairs, {len(lf_positions)} lone 0x0A bytes")
        
        # Check monotonicity
        non_mono = 0
        big_gaps = 0
        for j in range(1, len(tile_offsets)):
            diff = tile_offsets[j] - tile_offsets[j-1]
            if diff <= 0:
                non_mono += 1
                if non_mono <= 3:
                    print(f"  NON-MONOTONIC at [{j}]: 0x{tile_offsets[j-1]:X} -> 0x{tile_offsets[j]:X} (diff={diff})")
            elif diff > 100000:
                big_gaps += 1
                if big_gaps <= 3:
                    print(f"  BIG GAP at [{j}]: 0x{tile_offsets[j-1]:X} -> 0x{tile_offsets[j]:X} (diff={diff})")
        
        if non_mono == 0 and big_gaps == 0:
            print(f"  Table CONSISTENT (monotonic, no big gaps)")
        else:
            print(f"  TABLE ISSUES: {non_mono} non-monotonic, {big_gaps} big gaps")
        
        # Check first and last tile positions
        first_tile = tile_offsets[0]
        last_tile = tile_offsets[-1]
        # Expected first tile: right after the tile table's original position
        expected_first_actual = tile_table_end  # actual pos of first tile
        first_actual = first_tile + lvl_delta
        print(f"  First tile: stored=0x{first_tile:X}, actual=0x{first_actual:X}, table_end=0x{tile_table_end:X}")
        if abs(first_actual - expected_first_actual) > 10:
            print(f"  *** FIRST TILE OFFSET MISMATCH: expected near 0x{expected_first_actual:X}, got 0x{first_actual:X} (diff={first_actual-expected_first_actual})")
        
        # Check if tile sizes are reasonable
        sizes = [tile_offsets[j+1] - tile_offsets[j] for j in range(min(10, len(tile_offsets)-1))]
        print(f"  First 10 tile sizes (from stored offsets): {sizes}")


if __name__ == '__main__':
    main()

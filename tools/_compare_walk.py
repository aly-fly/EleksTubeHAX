#!/usr/bin/env python3
"""Compare tile offsets between v7 output and corrupted file for a specific layer."""
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

def decode_rle_channel(d, pos, npixels):
    start = pos
    decoded = 0
    while decoded < npixels:
        if pos >= len(d): return pos - start, decoded
        n = d[pos]; pos += 1
        if n <= 126:
            if pos >= len(d): return pos - start, decoded
            pos += 1; decoded += n + 1
        elif n == 127:
            if pos + 3 > len(d): return pos - start, decoded
            count = (d[pos] << 8) | d[pos+1]; pos += 2; pos += 1; decoded += count
        elif n == 128:
            if pos + 2 > len(d): return pos - start, decoded
            count = (d[pos] << 8) | d[pos+1]; pos += 2
            if pos + count > len(d): return pos - start, decoded
            pos += count; decoded += count
        else:
            count = 256 - n
            if pos + count > len(d): return pos - start, decoded
            pos += count; decoded += count
    return pos - start, decoded

def try_decode_tile(d, pos, tw, th, bpp):
    npixels = tw * th
    total = 0
    for ch in range(bpp):
        consumed, decoded = decode_rle_channel(d, pos + total, npixels)
        total += consumed
        if decoded != npixels:
            return total, False, f"ch{ch}:{decoded}/{npixels}"
    return total, True, "OK"

def main():
    corrupt_path = sys.argv[1]
    restored_path = sys.argv[2]
    target_layer = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    
    with open(corrupt_path, 'rb') as f: cd = f.read()
    with open(restored_path, 'rb') as f: rd = f.read()
    
    # Parse restored file to find layer 8's tile offsets
    pos = 14 + 4 + 4 + 4 + 4  # skip header
    pos = skip_properties(rd, pos)
    
    layer_offsets = []
    while pos + 8 <= len(rd):
        off = u64(rd, pos); pos += 8
        if off == 0: break
        layer_offsets.append(off)
    while pos + 8 <= len(rd):
        off = u64(rd, pos); pos += 8
        if off == 0: break
    
    loff = layer_offsets[target_layer]
    w = u32(rd, loff); h = u32(rd, loff+4); nlen = u32(rd, loff+12)
    name = rd[loff+16:loff+16+nlen-1].decode('utf-8', errors='replace')
    print(f"Layer {target_layer}: \"{name}\" ({w}x{h}) at 0x{loff:X}")
    
    prop_end = skip_properties(rd, loff + 16 + nlen)
    hier_off = u64(rd, prop_end)
    bpp = u32(rd, hier_off + 8)
    lvl0_off = u64(rd, hier_off + 12)
    print(f"Hierarchy at 0x{hier_off:X}, bpp={bpp}, Level 0 at 0x{lvl0_off:X}")
    
    # Read tile offsets from RESTORED file
    tp = lvl0_off + 8
    r_tiles = []
    while tp + 8 <= len(rd):
        to = u64(rd, tp); tp += 8
        if to == 0: break
        r_tiles.append(to)
    
    # Read tile offsets from CORRUPTED file (same level position in corrupted data)
    # Need to find the same level in corrupted file
    tp = lvl0_off + 8
    c_tiles = []
    while tp + 8 <= len(cd):
        to = u64(cd, tp); tp += 8
        if to == 0: break
        c_tiles.append(to)
    
    tiles_x = (w + 63) // 64
    tiles_y = (h + 63) // 64
    print(f"Tiles: {len(r_tiles)} in restored, {len(c_tiles)} in corrupted")
    print(f"Expected: {tiles_x * tiles_y} ({tiles_x}x{tiles_y})")
    
    # Now simulate the walk and compare
    print(f"\n--- Walk simulation vs actual output (first 200 tiles, then around failures) ---")
    
    # Get lvl_delta from stored vs actual level position
    # We read from hier_off + 12 in RESTORED for the actual level pos
    # And from hier_off + 12 in CORRUPTED for the stored level off
    stored_lvl = u64(cd, hier_off + 12)
    actual_lvl = u64(rd, hier_off + 12)  # = lvl0_off
    lvl_delta = actual_lvl - stored_lvl
    print(f"Stored level: 0x{stored_lvl:X}, actual: 0x{actual_lvl:X}, lvl_delta={lvl_delta}")
    
    # Walk-simulate: start from stored[0] + lvl_delta using corrupted data
    if c_tiles:
        first_stored = c_tiles[0]
        walk_pos = first_stored + lvl_delta
        print(f"Walk start: stored[0]=0x{first_stored:X} + delta={lvl_delta} = 0x{walk_pos:X}")
        print(f"Restored [0] = 0x{r_tiles[0]:X}")
        print()
        
        # Walk through tiles
        mismatches = 0
        for i in range(min(len(c_tiles), len(r_tiles))):
            tx = i % tiles_x
            ty = i // tiles_x
            tw = min(64, w - tx * 64)
            th = min(64, h - ty * 64)
            
            # Decode from CORRUPTED file at walk position
            consumed_c, ok_c, msg_c = try_decode_tile(cd, walk_pos, tw, th, bpp)
            # Decode from RESTORED file at the stored offset
            consumed_r, ok_r, msg_r = try_decode_tile(rd, r_tiles[i], tw, th, bpp)
            
            show = (i < 5) or (i >= 148 and i <= 155) or (walk_pos != r_tiles[i]) or (ok_c != ok_r)
            
            if walk_pos != r_tiles[i]:
                mismatches += 1
            
            if show:
                match = "MATCH" if walk_pos == r_tiles[i] else "MISMATCH"
                print(f"  tile[{i:4d}] walk=0x{walk_pos:08X} out=0x{r_tiles[i]:08X} {match}  "
                      f"walk_decode={ok_c}({consumed_c})  out_decode={ok_r}({consumed_r})  {msg_c} / {msg_r}")
            
            if ok_c:
                walk_pos += consumed_c
            else:
                # Mirror the recovery logic
                if i + 1 < len(c_tiles):
                    orig_size = c_tiles[i+1] - c_tiles[i]
                    found = False
                    for cr in range(0, 5):
                        cand = walk_pos + orig_size - cr
                        if cand <= walk_pos: continue
                        ntx = (i+1) % tiles_x
                        nty = (i+1) // tiles_x
                        ntw = min(64, w - ntx * 64)
                        nth = min(64, h - nty * 64)
                        _, nok, _ = try_decode_tile(cd, cand, ntw, nth, bpp)
                        if nok:
                            walk_pos = cand
                            found = True
                            break
                    if not found:
                        walk_pos += orig_size
                else:
                    walk_pos += consumed_c
        
        print(f"\n  Total mismatches: {mismatches}/{min(len(c_tiles), len(r_tiles))}")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <corrupted.xcf> <restored-v7.xcf> [layer_index]")
        sys.exit(1)
    main()

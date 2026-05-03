#!/usr/bin/env python3
"""
Restore corrupted XCF v7 — structural repair with RLE-walk tile offset fixing.

Same structural finding as v6, but tile offsets are fixed by walking forward
through tiles using RLE decoding to determine each tile's actual size, rather
than using a uniform level delta.
"""
import struct, sys, os
os.environ.setdefault('PYTHONIOENCODING', 'utf-8')
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def u32(d, pos): return struct.unpack('>I', d[pos:pos+4])[0]
def u64(d, pos): return struct.unpack('>Q', d[pos:pos+8])[0]
def p32(v): return struct.pack('>I', v)
def p64(v): return struct.pack('>Q', v)

# ── Structural element finders (from v6) ──

def find_layer_header(d, stored_off, max_delta=1000):
    for delta in range(0, max_delta + 1):
        for sign in [0, -1, 1]:
            if delta == 0 and sign != 0: continue
            pos = stored_off + delta * (sign if sign else 1)
            if delta == 0: pos = stored_off
            if pos < 0 or pos + 20 >= len(d): continue
            info = check_layer_header(d, pos)
            if info:
                return pos, info
    return None, None

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
                if bpp in (1, 2, 3, 4):
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

def skip_properties(d, pos):
    while pos + 8 <= len(d):
        pt = u32(d, pos); ps = u32(d, pos + 4); pos += 8
        if pt == 0: break
        if pos + ps > len(d): break
        pos += ps
    return pos

# ── RLE decoder ──

def decode_rle_channel(d, pos, npixels):
    """Decode one RLE channel. Returns (bytes_consumed, decoded_pixel_count)."""
    start = pos
    decoded = 0
    while decoded < npixels:
        if pos >= len(d): return pos - start, decoded
        n = d[pos]; pos += 1
        if n <= 126:
            if pos >= len(d): return pos - start, decoded
            pos += 1
            decoded += n + 1
        elif n == 127:
            if pos + 3 > len(d): return pos - start, decoded
            count = (d[pos] << 8) | d[pos+1]; pos += 2
            pos += 1
            decoded += count
        elif n == 128:
            if pos + 2 > len(d): return pos - start, decoded
            count = (d[pos] << 8) | d[pos+1]; pos += 2
            if pos + count > len(d): return pos - start, decoded
            pos += count
            decoded += count
        else:
            count = 256 - n
            if pos + count > len(d): return pos - start, decoded
            pos += count
            decoded += count
    return pos - start, decoded

def try_decode_tile(d, pos, tw, th, bpp):
    """Try to decode a tile at pos. Returns (total_consumed, success)."""
    npixels = tw * th
    total = 0
    for ch in range(bpp):
        consumed, decoded = decode_rle_channel(d, pos + total, npixels)
        total += consumed
        if decoded != npixels:
            return total, False
    return total, True

# ── Tile offset fixer with RLE walk ──

def encode_rle_channel_zeros(npixels):
    """Encode a channel of all-zero pixels as RLE. Returns bytes."""
    result = bytearray()
    remaining = npixels
    while remaining > 0:
        if remaining > 127:
            # Long run: opcode 127, 2-byte count, value
            run = min(remaining, 65535)
            result.append(127)
            result.append((run >> 8) & 0xFF)
            result.append(run & 0xFF)
            result.append(0)
            remaining -= run
        else:
            # Short run: opcode n-1, value
            result.append(remaining - 1)
            result.append(0)
            remaining = 0
    return bytes(result)


def make_transparent_tile(tw, th, bpp):
    """Create valid RLE data for a transparent (all-zero) tile."""
    npixels = tw * th
    channel_data = encode_rle_channel_zeros(npixels)
    return channel_data * bpp


def try_repair_tile_cr(d, pos, tile_size_corrupt, orig_size, tw, th, bpp):
    """
    Try to repair a tile by reinserting 0x0D bytes that were removed before 0x0A.
    
    Returns repaired tile data (bytes) if successful, None if not.
    The repaired data will be orig_size bytes (larger than tile_size_corrupt).
    """
    n_missing = orig_size - tile_size_corrupt
    if n_missing <= 0 or n_missing > 10:
        return None  # Too many missing CRs or something else is wrong
    
    tile_data = d[pos:pos + tile_size_corrupt]
    
    # Find all 0x0A positions (candidate sites for 0x0D reinsertion)
    lf_positions = [j for j in range(len(tile_data)) if tile_data[j] == 0x0A]
    
    if len(lf_positions) < n_missing:
        return None  # Not enough 0x0A to account for the missing CRs
    
    npixels = tw * th
    
    if n_missing == 1:
        # Try each single 0x0A position
        for lf_pos in lf_positions:
            candidate = tile_data[:lf_pos] + b'\x0D' + tile_data[lf_pos:]
            ok = _validate_rle(candidate, npixels, bpp)
            if ok:
                return bytes(candidate)
    elif n_missing == 2:
        # Try pairs — optimize by trying sequential pairs first
        for j in range(len(lf_positions)):
            for k in range(j + 1, len(lf_positions)):
                p1, p2 = lf_positions[j], lf_positions[k]
                candidate = tile_data[:p1] + b'\x0D' + tile_data[p1:p2] + b'\x0D' + tile_data[p2:]
                ok = _validate_rle(candidate, npixels, bpp)
                if ok:
                    return bytes(candidate)
            # Bail out early if too many combinations
            if len(lf_positions) > 100 and j > 20:
                break
    
    return None  # Could not repair


def _validate_rle(data, npixels, bpp):
    """Check if data decodes as valid RLE for a tile."""
    pos = 0
    for ch in range(bpp):
        decoded = 0
        while decoded < npixels:
            if pos >= len(data): return False
            n = data[pos]; pos += 1
            if n <= 126:
                if pos >= len(data): return False
                pos += 1; decoded += n + 1
            elif n == 127:
                if pos + 3 > len(data): return False
                count = (data[pos] << 8) | data[pos+1]; pos += 2
                pos += 1; decoded += count
            elif n == 128:
                if pos + 2 > len(data): return False
                count = (data[pos] << 8) | data[pos+1]; pos += 2
                if pos + count > len(data): return False
                pos += count; decoded += count
            else:
                count = 256 - n
                if pos + count > len(data): return False
                pos += count; decoded += count
        if decoded != npixels:
            return False
    return True


def tile_dims(i, w, h, tiles_x):
    """Return (tw, th) for tile index i."""
    tx = i % tiles_x
    ty = i // tiles_x
    return min(64, w - tx * 64), min(64, h - ty * 64)


def fix_tile_offsets_rle_walk(d, out, stored_offsets, table_positions, lvl_delta, bpp, w, h):
    """
    Fix tile offsets by walking forward through tiles using RLE decoding.
    
    Strategy:
    1. First tile is at stored_offsets[0] + lvl_delta (known correct from level finding)
    2. For each tile, try to decode RLE at current position
    3. If decode OK: advance by consumed bytes (exact tile size in corrupted file)
    4. If decode fails (tile has internal CR corruption): 
       look ahead up to 50 tiles to find the next decodable tile, 
       trying multiple CR counts for the gap. This prevents error accumulation
       when consecutive tiles have internal corruption.
    """
    n = len(stored_offsets)
    if n == 0:
        return 0
    
    tiles_x = (w + 63) // 64
    tiles_y = (h + 63) // 64
    
    running_pos = stored_offsets[0] + lvl_delta
    patched = 0
    crs_found = 0
    i = 0
    
    while i < n:
        tw, th = tile_dims(i, w, h, tiles_x)
        
        if running_pos < 0: running_pos = 0
        if running_pos >= len(d): running_pos = len(d) - 1
        
        # Patch this tile's offset
        out[table_positions[i]:table_positions[i]+8] = p64(running_pos)
        patched += 1
        
        # Try to decode the tile
        consumed, ok = try_decode_tile(d, running_pos, tw, th, bpp)
        
        if ok:
            running_pos += consumed
            i += 1
        else:
            # Tile has internal CR corruption. Look ahead to find next good tile.
            found_anchor = False
            max_lookahead = min(50, n - i)
            
            for la in range(1, max_lookahead):
                target_i = i + la
                if target_i >= n:
                    break
                
                la_tw, la_th = tile_dims(target_i, w, h, tiles_x)
                
                # Gap in original file from tile i to tile target_i
                orig_gap = stored_offsets[target_i] - stored_offsets[i]
                
                # Try different numbers of total CRs in the gap
                for total_crs in range(0, la * 3 + 5):
                    candidate = running_pos + orig_gap - total_crs
                    if candidate <= running_pos or candidate >= len(d):
                        continue
                    
                    _, anchor_ok = try_decode_tile(d, candidate, la_tw, la_th, bpp)
                    if anchor_ok:
                        # Found a good anchor at target_i!
                        # Set intermediate tile offsets proportionally
                        for mid in range(i + 1, target_i):
                            # Proportional position between running_pos and candidate
                            mid_orig_off = stored_offsets[mid] - stored_offsets[i]
                            frac = mid_orig_off / orig_gap if orig_gap > 0 else 0
                            mid_pos = int(running_pos + frac * (candidate - running_pos))
                            if mid_pos < 0: mid_pos = 0
                            if mid_pos >= len(d): mid_pos = len(d) - 1
                            out[table_positions[mid]:table_positions[mid]+8] = p64(mid_pos)
                            patched += 1
                        
                        crs_found += total_crs
                        running_pos = candidate
                        i = target_i  # Skip to the anchor tile
                        found_anchor = True
                        break
                
                if found_anchor:
                    break
            
            if not found_anchor:
                # Couldn't find any good anchor tile. Use original sizes as fallback.
                if i + 1 < n:
                    orig_size = stored_offsets[i + 1] - stored_offsets[i]
                    running_pos += orig_size
                else:
                    running_pos += consumed
                i += 1
    
    return patched, crs_found

def find_channel_near(d, stored_off, expected_delta):
    for delta in range(0, 2000):
        for sign in [0, -1, 1]:
            if delta == 0 and sign != 0: continue
            pos = stored_off + expected_delta + delta * (sign if sign else 1)
            if delta == 0: pos = stored_off + expected_delta
            if pos < 0 or pos + 16 >= len(d): continue
            w = u32(d, pos); h = u32(d, pos + 4); nlen = u32(d, pos + 8)
            if 0 < w <= 65536 and 0 < h <= 65536 and 0 < nlen < 500:
                name = d[pos + 12:pos + 12 + nlen]
                if name[-1:] == b'\x00':
                    try:
                        name[:-1].decode('utf-8')
                        return pos
                    except: pass
    return None


# ── Main ──

def main():
    corrupt_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else corrupt_path.replace('.xcf', '-RESTORED-v7.xcf')
    
    print(f"Input:  {corrupt_path}")
    print(f"Output: {output_path}")
    
    with open(corrupt_path, 'rb') as f:
        d = f.read()
    
    print(f"Size: {len(d):,} bytes")
    
    # Parse file header
    assert d[:9] == b'gimp xcf '
    ver = int(d[9:13].replace(b'v', b'').replace(b'\x00', b'').decode())
    print(f"Version: v{ver:03d}")
    
    pos = 14
    canvas_w = u32(d, pos); pos += 4
    canvas_h = u32(d, pos); pos += 4
    base_type = u32(d, pos); pos += 4
    if ver >= 4: pos += 4
    print(f"Canvas: {canvas_w}x{canvas_h}")
    
    pos = skip_properties(d, pos)
    
    # Layer offset table
    layer_table_pos = pos
    stored_layer_offsets = []
    while pos + 8 <= len(d):
        off = u64(d, pos); pos += 8
        if off == 0: break
        stored_layer_offsets.append(off)
    layer_table_end = pos
    
    # Channel offset table
    channel_table_pos = pos
    stored_channel_offsets = []
    while pos + 8 <= len(d):
        off = u64(d, pos); pos += 8
        if off == 0: break
        stored_channel_offsets.append(off)
    
    print(f"\n{len(stored_layer_offsets)} layers, {len(stored_channel_offsets)} channels")
    
    out = bytearray(d)
    patch_count = 0
    
    def patch_offset(pos, new_val):
        nonlocal patch_count, out
        out[pos:pos+8] = p64(new_val)
        patch_count += 1
    
    # Process each layer
    prev_delta = 0
    for i, stored in enumerate(stored_layer_offsets):
        max_d = max(abs(prev_delta) + 200, 1000)
        
        actual, info = find_layer_header(d, stored, max_delta=max_d)
        if actual is None:
            print(f"\n  Layer {i}: STORED=0x{stored:X} NOT FOUND")
            continue
        
        delta = actual - stored
        prev_delta = delta
        print(f"\n  Layer {i}: \"{info['name']}\" ({info['w']}x{info['h']}) "
              f"stored=0x{stored:X} actual=0x{actual:X} delta={delta}")
        
        # Patch layer table
        patch_offset(layer_table_pos + i * 8, actual)
        
        lp = actual + 16 + info['nlen']
        lp = skip_properties(d, lp)
        
        if lp + 16 > len(d): continue
        
        stored_hier = u64(d, lp)
        stored_mask = u64(d, lp + 8)
        
        hier_pos, bpp = find_hierarchy(d, stored_hier, info['w'], info['h'],
                                       actual, max_delta=max_d + 200)
        if hier_pos is None:
            print(f"    Hierarchy NOT FOUND (stored=0x{stored_hier:X})")
            continue
        
        hier_delta = hier_pos - stored_hier
        print(f"    Hierarchy: stored=0x{stored_hier:X} actual=0x{hier_pos:X} delta={hier_delta} bpp={bpp}")
        
        patch_offset(lp, hier_pos)
        
        if stored_mask != 0:
            actual_mask = find_channel_near(d, stored_mask, delta)
            if actual_mask:
                patch_offset(lp + 8, actual_mask)
        
        # Parse hierarchy level table
        hp = hier_pos + 12
        stored_level_offsets = []
        level_table_positions = []
        while hp + 8 <= len(d):
            level_table_positions.append(hp)
            lo = u64(d, hp); hp += 8
            if lo == 0: break
            stored_level_offsets.append(lo)
        
        # Find and patch each level
        for li, stored_loff in enumerate(stored_level_offsets):
            lw = info['w'] >> li
            lh = info['h'] >> li
            if lw < 1: lw = 1
            if lh < 1: lh = 1
            
            level_pos = find_level(d, stored_loff, lw, lh, hier_pos,
                                   max_delta=max_d + 200)
            if level_pos is None:
                if li == 0:
                    print(f"    Level {li}: NOT FOUND ({lw}x{lh})")
                continue
            
            lvl_delta = level_pos - stored_loff
            patch_offset(level_table_positions[li], level_pos)
            
            if li == 0:
                print(f"    Level 0: stored=0x{stored_loff:X} actual=0x{level_pos:X} delta={lvl_delta}")
            
            # Parse tile offset table
            tp = level_pos + 8
            tile_count = 0
            tile_stored_offsets = []
            tile_table_positions = []
            while tp + 8 <= len(d):
                tile_table_positions.append(tp)
                to = u64(d, tp); tp += 8
                if to == 0: break
                tile_stored_offsets.append(to)
                tile_count += 1
            
            # Validate tile table
            tiles_x = (lw + 63) // 64
            tiles_y = (lh + 63) // 64
            expected_tiles = tiles_x * tiles_y
            
            valid_tiles = all(0 < t < len(d) + 1000 for t in tile_stored_offsets)
            valid_count = (tile_count == expected_tiles) or \
                          (tile_count > 0 and abs(tile_count - expected_tiles) <= 1)
            
            if tile_stored_offsets and valid_count and valid_tiles:
                if li == 0:
                    # Level 0: use RLE walk for precise tile offsets
                    patched, crs = fix_tile_offsets_rle_walk(
                        d, out, tile_stored_offsets, tile_table_positions,
                        lvl_delta, bpp, lw, lh)
                    print(f"    Level 0: {tile_count} tiles, RLE-walked ({crs} CRs found)")
                else:
                    # Stub levels: just use lvl_delta (GIMP doesn't read their tiles)
                    for ti, (stored_t, tpos) in enumerate(zip(tile_stored_offsets, tile_table_positions)):
                        actual_t = stored_t + lvl_delta
                        if actual_t < 0: actual_t = 0
                        if actual_t >= len(d): actual_t = len(d) - 1
                        out[tpos:tpos+8] = p64(actual_t)
    
    # Patch channel offsets
    for ci, stored in enumerate(stored_channel_offsets):
        actual = find_channel_near(d, stored, prev_delta)
        if actual:
            patch_offset(channel_table_pos + ci * 8, actual)
    
    print(f"\nTotal patches: {patch_count}")
    
    # ── Second pass: repair corrupt tile RLE data ──
    print("\n=== Tile data repair pass ===")
    repair_count = 0
    replace_count = 0
    
    # Re-parse the output to find all tile offsets and validate them
    opos = 14 + 4 + 4 + 4 + 4  # skip header
    opos = skip_properties(out, opos)
    
    o_layer_offsets = []
    while opos + 8 <= len(out):
        off = u64(out, opos); opos += 8
        if off == 0: break
        o_layer_offsets.append(off)
    while opos + 8 <= len(out):
        off = u64(out, opos); opos += 8
        if off == 0: break
    
    for li, loff in enumerate(o_layer_offsets):
        if loff >= len(out) - 20: continue
        w_l = u32(out, loff); h_l = u32(out, loff+4)
        nlen = u32(out, loff+12)
        if nlen == 0 or nlen > 10000: continue
        name = out[loff+16:loff+16+nlen-1].decode('utf-8', errors='replace')
        
        prop_end = skip_properties(out, loff + 16 + nlen)
        if prop_end + 16 > len(out): continue
        hier_off = u64(out, prop_end)
        if hier_off == 0 or hier_off >= len(out) - 12: continue
        
        bpp_l = u32(out, hier_off + 8)
        lvl0_off = u64(out, hier_off + 12)
        if lvl0_off == 0 or lvl0_off >= len(out) - 8: continue
        
        # Read tile offsets from output
        tp = lvl0_off + 8
        tiles = []
        tile_tpos = []
        while tp + 8 <= len(out):
            tile_tpos.append(tp)
            to = u64(out, tp); tp += 8
            if to == 0: break
            tiles.append(to)
        
        tiles_x = (w_l + 63) // 64
        tiles_y = (h_l + 63) // 64
        expected = tiles_x * tiles_y
        if len(tiles) != expected: continue
        
        # Also need the STORED (original) offsets from the corrupted input for size info
        # Read from corrupted file at the same level position
        c_tiles = []
        c_tp = lvl0_off + 8
        while c_tp + 8 <= len(d):
            c_to = u64(d, c_tp); c_tp += 8
            if c_to == 0: break
            c_tiles.append(c_to)
        
        layer_repaired = 0
        layer_replaced = 0
        
        # Track end position of last transparent write to detect overlaps.
        # When a corrupt tile is replaced with transparent RLE, the write
        # may extend past the next tile's offset (in proportional interpolation
        # zones where tiles are spaced only 12-14 bytes apart but transparent
        # RLE needs 16 bytes). We detect this overlap and move subsequent
        # tiles forward, replacing them too.
        write_end = 0
        
        for ti in range(len(tiles)):
            toff = tiles[ti]
            tw, th = tile_dims(ti, w_l, h_l, tiles_x)
            
            need_replace = False
            
            # Check if previous transparent write overlapped this tile
            if write_end > toff:
                # This tile's data was (partially) overwritten. Move it
                # forward past the previous write and replace it.
                toff = write_end
                out[tile_tpos[ti]:tile_tpos[ti]+8] = p64(toff)
                tiles[ti] = toff
                need_replace = True
            
            if not need_replace:
                consumed, ok = try_decode_tile(out, toff, tw, th, bpp_l)
                if ok:
                    continue  # Tile is fine
            
            # Tile has corrupt RLE data. Try to repair it (only if not
            # already forced by overlap — overlapped tiles have garbage data).
            repaired = False
            if not need_replace:
                if ti < len(c_tiles) - 1 and len(c_tiles) == len(tiles):
                    orig_size = c_tiles[ti + 1] - c_tiles[ti]
                    if ti + 1 < len(tiles):
                        actual_size = tiles[ti + 1] - toff
                    else:
                        actual_size = consumed
                    
                    if actual_size > 0 and orig_size > actual_size and orig_size - actual_size <= 5:
                        repaired_data = try_repair_tile_cr(
                            bytes(out), toff, actual_size, orig_size, tw, th, bpp_l)
                        if repaired_data is not None:
                            if len(repaired_data) <= actual_size:
                                out[toff:toff+len(repaired_data)] = repaired_data
                                repaired = True
            
            if not repaired:
                # Replace with valid transparent tile RLE
                transparent = make_transparent_tile(tw, th, bpp_l)
                if toff + len(transparent) <= len(out):
                    out[toff:toff+len(transparent)] = transparent
                    write_end = toff + len(transparent)
                    layer_replaced += 1
                    replace_count += 1
            else:
                layer_repaired += 1
                repair_count += 1
        
        if layer_repaired > 0 or layer_replaced > 0:
            print(f"  Layer {li} \"{name}\": {layer_repaired} repaired, {layer_replaced} replaced with transparent")
    
    print(f"  Total: {repair_count} tiles repaired, {replace_count} tiles replaced with transparent")
    
    with open(output_path, 'wb') as f:
        f.write(out)
    
    # Verify
    print("\n=== Verification ===")
    verify_xcf(bytes(out))
    print(f"\nSaved: {output_path}")


def verify_xcf(data):
    if data[:9] != b'gimp xcf ':
        print("  ERROR: Bad magic"); return
    
    ver = int(data[9:13].replace(b'v',b'').replace(b'\x00',b'').decode())
    pos = 14 + 4 + 4 + 4
    if ver >= 4: pos += 4
    
    while pos + 8 <= len(data):
        pt = u32(data, pos); ps = u32(data, pos+4); pos += 8
        if pt == 0: break
        pos += ps
    
    count = 0; valid = 0
    while pos + 8 <= len(data):
        off = u64(data, pos); pos += 8
        if off == 0: break
        count += 1
        if off >= len(data) - 20:
            print(f"  Layer {count}: 0x{off:X} OUT OF BOUNDS"); continue
        
        w = u32(data, off); h = u32(data, off+4); nlen = u32(data, off+12)
        if nlen == 0 or nlen > 10000 or off + 16 + nlen > len(data):
            print(f"  Layer {count}: bad header"); continue
        name = data[off+16:off+16+nlen-1].decode('utf-8', errors='replace')
        
        p = off + 16 + nlen
        while p + 8 <= len(data):
            pt = u32(data, p); ps = u32(data, p+4); p += 8
            if pt == 0: break
            if p + ps > len(data): p = len(data); break
            p += ps
        
        hier_ok = False; tile_ok = False
        if p + 16 <= len(data):
            hier_off = u64(data, p)
            if 0 < hier_off < len(data) - 12:
                hw = u32(data, hier_off); hh = u32(data, hier_off+4)
                hbpp = u32(data, hier_off+8)
                if hw == w and hh == h and hbpp in (1,2,3,4):
                    hier_ok = True
                    if hier_off + 20 <= len(data):
                        lo = u64(data, hier_off + 12)
                        if 0 < lo < len(data) - 8:
                            lw = u32(data, lo); lh = u32(data, lo + 4)
                            if lw == w and lh == h:
                                tile_ok = True
        
        status = "OK" if hier_ok and tile_ok else ("HIER_OK" if hier_ok else "BROKEN")
        print(f"  Layer {count}: \"{name}\" ({w}x{h}) [{status}]")
        if hier_ok: valid += 1
    
    print(f"  Total: {count} layers, {valid} structurally valid")


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <corrupted.xcf> [output.xcf]")
        sys.exit(1)
    main()

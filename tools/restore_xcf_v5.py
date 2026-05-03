#!/usr/bin/env python3
"""
Reconstruct a valid XCF file from the corrupted Kopie.xcf.

Strategy: 
Git stripped all 0x0D bytes preceding 0x0A in the binary XCF.
This broke all file offsets because:
1. The layer table stores absolute offsets into the file
2. Each layer stores offsets to its hierarchy/channels
3. Each hierarchy stores offsets to levels
4. Each level stores offsets to tiles
5. After byte removal, all offsets past the first removed CR are WRONG

The key insight: all DATA (property values, tile pixels) is intact except for
missing 0x0D bytes. The structural POINTERS are wrong.

Approach: Parse the corrupted file using the CORRECTED offsets (found by
searching for layer name strings), extract all components, and write them
into a new file with recomputed offsets.

For tile data: we leave it as-is (some pixels may have wrong values where
0x0D was removed before 0x0A in the RLE stream, causing minor visual artifacts).
This is unavoidable without the original file.
"""
import struct
import sys
import os

os.environ.setdefault('PYTHONIOENCODING', 'utf-8')
sys.stdout.reconfigure(encoding='utf-8', errors='replace')


def u32(d, pos):
    return struct.unpack('>I', d[pos:pos+4])[0]

def u64(d, pos):
    return struct.unpack('>Q', d[pos:pos+8])[0]

def p32(val):
    return struct.pack('>I', val)

def p64(val):
    return struct.pack('>Q', val)


def is_valid_layer_header(d, pos):
    """Check if position looks like a layer header."""
    if pos < 0 or pos + 20 >= len(d):
        return None
    w = u32(d, pos)
    h = u32(d, pos+4)
    lt = u32(d, pos+8)
    nlen = u32(d, pos+12)
    if w == 0 or h == 0 or w > 65536 or h > 65536:
        return None
    if lt > 10:
        return None
    if nlen == 0 or nlen > 500:
        return None
    if pos + 16 + nlen > len(d):
        return None
    name_bytes = d[pos+16:pos+16+nlen]
    if name_bytes[-1] != 0:
        return None
    try:
        name_bytes[:-1].decode('utf-8')
    except:
        return None
    if not all(32 <= b < 127 or b > 127 for b in name_bytes[:-1]):
        return None
    return {'w': w, 'h': h, 'type': lt, 'nlen': nlen,
            'name': name_bytes[:-1].decode('utf-8')}


def find_actual_layer_offset(d, stored_off, search_range=200000):
    """Find actual layer header position near the stored offset."""
    best = None
    for delta in range(-search_range, search_range + 1):
        pos = stored_off + delta
        info = is_valid_layer_header(d, pos)
        if info:
            if best is None or abs(delta) < abs(best[1]):
                best = (pos, delta, info)
    return best


def parse_properties(d, pos):
    """Parse property list, return (end_pos, raw bytes of props including terminator)."""
    start = pos
    while pos < len(d) - 8:
        pt = u32(d, pos)
        ps = u32(d, pos + 4)
        pos += 8
        if pt == 0:
            break
        pos += ps
    return pos, d[start:pos]


def parse_layer(d, actual_off, stored_off, cr_delta):
    """
    Parse a layer at actual_off and extract all its components.
    
    Returns a dict with all the layer's data needed for reconstruction.
    """
    pos = actual_off
    w = u32(d, pos)
    h = u32(d, pos + 4)
    lt = u32(d, pos + 8)
    nlen = u32(d, pos + 12)
    name = d[pos + 16:pos + 16 + nlen - 1].decode('utf-8', 'replace')
    
    # Layer header bytes
    header = d[pos:pos + 16 + nlen]
    pos += 16 + nlen
    
    # Properties
    props_pos, props_bytes = parse_properties(d, pos)
    pos = props_pos
    
    if pos + 16 > len(d):
        return None
    
    # Hierarchy and mask offset (stored values are wrong, need to find actual)
    stored_hier_off = u64(d, pos)
    stored_mask_off = u64(d, pos + 8)
    pos += 16
    
    # The hierarchy offset stored is wrong (shifted). 
    # The actual hierarchy should be right after the layer header + props + 16 bytes for the offsets.
    # Actually no — the hierarchy is pointed to by the offset, it could be anywhere.
    # BUT in practice, GIMP writes it sequentially right after the mask offset.
    # Let's try: actual_hier_off = stored_hier_off + cr_delta
    
    # Actually, we need to figure out the actual hier offset.
    # The stored_hier_off was correct in the original file. The actual position
    # in the corrupted file is stored_hier_off + (delta at that position).
    # But we don't know the exact delta at the hier position.
    
    # Better approach: search for the hierarchy header near the expected position.
    # The hierarchy has: w(4) + h(4) + bpp(4) — same w,h as the layer.
    actual_hier_off = find_hierarchy(d, stored_hier_off, w, h, actual_off)
    
    if actual_hier_off is None:
        print(f"  WARNING: Can't find hierarchy for \"{name}\" (stored=0x{stored_hier_off:X})")
        return {'name': name, 'w': w, 'h': h, 'type': lt,
                'header': header, 'props': props_bytes,
                'hier_off_raw': stored_hier_off, 'mask_off_raw': stored_mask_off,
                'hierarchy': None, 'mask': None}
    
    # Parse hierarchy
    hier = parse_hierarchy(d, actual_hier_off, w, h)
    
    # Parse mask if present
    mask = None
    if stored_mask_off != 0:
        # Try to find actual mask offset
        # Mask is a channel: w(4) + h(4) + name_len(4) + name
        actual_mask_off = find_channel(d, stored_mask_off, actual_off)
        if actual_mask_off is not None:
            mask = parse_channel(d, actual_mask_off)
    
    return {'name': name, 'w': w, 'h': h, 'type': lt,
            'header': header, 'props': props_bytes,
            'hier_off_raw': stored_hier_off, 'mask_off_raw': stored_mask_off,
            'hierarchy': hier, 'mask': mask}


def find_hierarchy(d, stored_off, w, h, layer_off):
    """Find actual hierarchy position by searching for w,h,bpp pattern."""
    target = p32(w) + p32(h)
    
    # Search around stored_off with increasing delta
    # The hierarchy is usually close after the layer header
    for search_range in [2000, 20000, 200000]:
        for delta in range(-search_range, search_range + 1):
            pos = stored_off + delta
            if pos < layer_off or pos + 12 >= len(d):
                continue
            if d[pos:pos+8] == target:
                bpp = u32(d, pos + 8)
                if bpp in (1, 2, 3, 4):
                    # Verify: after hierarchy header, there should be level offsets
                    lpos = pos + 12
                    lo = u64(d, lpos)
                    # First level offset should be reasonable
                    if 0 < lo < len(d):
                        return pos
        if delta == search_range:
            continue  # Try wider range
    
    return None


def find_channel(d, stored_off, ref_off):
    """Find a channel/mask near stored_off."""
    for delta in range(-10000, 10000):
        pos = stored_off + delta
        if pos < 0 or pos + 16 >= len(d):
            continue
        w = u32(d, pos)
        h = u32(d, pos + 4)
        nlen = u32(d, pos + 8)
        if 0 < w <= 65536 and 0 < h <= 65536 and 0 < nlen < 500:
            name = d[pos + 12:pos + 12 + nlen]
            if name[-1:] == b'\x00':
                try:
                    name[:-1].decode('utf-8')
                    return pos
                except:
                    pass
    return None


def parse_hierarchy(d, hier_off, w, h):
    """Parse hierarchy structure, extract all tile data."""
    hw = u32(d, hier_off)
    hh = u32(d, hier_off + 4)
    bpp = u32(d, hier_off + 8)
    
    # Collect level offsets
    pos = hier_off + 12
    stored_level_offsets = []
    while pos + 8 <= len(d):
        lo = u64(d, pos)
        pos += 8
        if lo == 0:
            break
        stored_level_offsets.append(lo)
    
    level_table_end = pos  # Position after the 0-terminator
    
    # Parse each level
    levels = []
    for li, stored_loff in enumerate(stored_level_offsets):
        level = find_and_parse_level(d, stored_loff, hier_off, w, h, bpp, li)
        levels.append(level)
    
    return {
        'w': hw, 'h': hh, 'bpp': bpp,
        'stored_level_offsets': stored_level_offsets,
        'level_table_end': level_table_end,
        'levels': levels,
        'hier_off': hier_off,
    }


def find_and_parse_level(d, stored_off, hier_off, w, h, bpp, level_idx):
    """Find and parse a level."""
    # Level header: w(4) + h(4) followed by tile offset table
    
    # For level 0, w and h match the layer. For higher levels, halved.
    lw = w >> level_idx
    lh = h >> level_idx
    if lw < 1: lw = 1
    if lh < 1: lh = 1
    
    target = p32(lw) + p32(lh)
    
    actual_off = None
    for delta in range(-10000, 10000):
        pos = stored_off + delta
        if pos < hier_off or pos + 16 >= len(d):
            continue
        if d[pos:pos+8] == target:
            # Verify: tile offsets follow
            tp = pos + 8
            if tp + 8 <= len(d):
                to = u64(d, tp)
                if 0 < to < len(d):
                    actual_off = pos
                    break
    
    if actual_off is None:
        return {'w': lw, 'h': lh, 'actual_off': None, 'tiles': []}
    
    # Parse tile offset table
    tp = actual_off + 8
    stored_tile_offsets = []
    while tp + 8 <= len(d):
        to = u64(d, tp)
        tp += 8
        if to == 0:
            break
        stored_tile_offsets.append(to)
    
    tile_table_end = tp
    
    # Expected tile count
    tiles_x = (lw + 63) // 64
    tiles_y = (lh + 63) // 64
    expected_tiles = tiles_x * tiles_y
    
    # Find actual tile data offsets
    # Tiles are stored sequentially. We need to find the first tile's actual position.
    tiles = []
    if stored_tile_offsets:
        tiles = find_tiles(d, stored_tile_offsets, actual_off, bpp)
    
    return {
        'w': lw, 'h': lh,
        'actual_off': actual_off,
        'stored_tile_offsets': stored_tile_offsets,
        'tile_table_end': tile_table_end,
        'tiles': tiles,
        'expected_tiles': expected_tiles,
    }


def find_tiles(d, stored_tile_offsets, level_off, bpp):
    """
    Find actual tile data. Tiles are stored sequentially.
    The first tile offset helps us find the actual start position.
    Then each subsequent tile follows the previous.
    """
    tiles = []
    
    if not stored_tile_offsets:
        return tiles
    
    # Find first tile: search near the stored offset
    first_stored = stored_tile_offsets[0]
    
    # The tile data is usually right after the tile offset table of the level.
    # But the stored offsets are shifted. We need to figure out the actual position.
    
    # For tiles, we can't easily verify them since they're just RLE byte streams.
    # But we know the delta should be monotonically increasing (more CRs removed
    # as we go further into the file).
    
    # Simple approach: find delta from the level header, apply to tile offsets.
    # Actually: we know the level's actual_off. The tile offset table is right after
    # the level header. The stored tile offsets should be close to actual positions.
    
    # For now, just store the stored offsets and we'll fix them during writing.
    for i, sto in enumerate(stored_tile_offsets):
        tiles.append({'stored_off': sto})
    
    return tiles


def parse_channel(d, actual_off):
    """Parse a channel (mask)."""
    w = u32(d, actual_off)
    h = u32(d, actual_off + 4)
    nlen = u32(d, actual_off + 8)
    name = d[actual_off + 12:actual_off + 12 + nlen]
    
    pos = actual_off + 12 + nlen
    props_pos, props_bytes = parse_properties(d, pos)
    
    stored_hier_off = u64(d, props_pos)
    
    return {
        'w': w, 'h': h, 'nlen': nlen, 'name': name,
        'props': props_bytes,
        'stored_hier_off': stored_hier_off,
        'actual_off': actual_off,
    }


def write_xcf(corrupt, layers_info, actual_offsets, stored_offsets, output_path):
    """
    Write a reconstructed XCF file.
    
    Instead of rewriting from scratch (which would require perfectly
    reconstructing every property value), we take a smarter approach:
    
    1. Copy the corrupted file as-is
    2. Build a mapping: for each stored offset in the file, what's the correction?
    3. Patch every offset field to point to the corrected position
    
    The tile DATA stays as-is (with missing CRs — some pixel values will be 
    slightly wrong). But the STRUCTURE will be correct.
    """
    out = bytearray(corrupt)
    patches = []
    
    # First, build the cumulative CR removal map.
    # We know the delta for each layer header position.
    # delta[i] = actual_pos - stored_pos (always <= 0)
    # This means at position stored_pos, the cumulative CRs removed = -delta
    
    # Build calibration points: (stored_pos, actual_pos, delta)
    cal_points = []
    for i in range(len(stored_offsets)):
        cal_points.append((stored_offsets[i], actual_offsets[i], 
                          actual_offsets[i] - stored_offsets[i]))
    
    # Sort by stored position
    cal_points.sort(key=lambda x: x[0])
    
    print("\nCalibration points (offset corrections):")
    for sp, ap, delta in cal_points:
        print(f"  stored=0x{sp:X} actual=0x{ap:X} delta={delta}")
    
    # For any stored offset, we can interpolate the delta
    # Between calibration points, delta changes linearly (each CR removal is -1)
    
    def correct_offset(stored_off):
        """Given a stored (original) offset, find the actual position in corrupt file."""
        if stored_off == 0:
            return 0
        
        # Find surrounding calibration points
        prev = None
        for sp, ap, delta in cal_points:
            if sp <= stored_off:
                prev = (sp, ap, delta)
            else:
                break
        
        if prev is None:
            return stored_off  # Before first cal point, no correction
        
        # Use the closest known delta
        # The actual exact delta between cal points depends on how many CRs
        # are in that region, which we don't know exactly. But the delta
        # changes monotonically (only gets more negative).
        # Use the closest preceding calibration point's delta.
        return stored_off + prev[2]
    
    # Now patch all offsets in the file
    
    # 1. Layer table offsets
    # Parse the file header to find the layer table
    pos = 14 + 4 + 4 + 4  # magic + w + h + base_type
    ver_str = corrupt[9:13].replace(b'v', b'').replace(b'\x00', b'')
    ver = int(ver_str.decode())
    if ver >= 4:
        pos += 4
    
    # Skip image properties
    while pos < len(corrupt) - 8:
        pt = u32(corrupt, pos)
        ps = u32(corrupt, pos + 4)
        pos += 8
        if pt == 0: break
        pos += ps
    
    # Patch layer offset table
    layer_table_pos = pos
    for i in range(len(stored_offsets)):
        actual = actual_offsets[i]
        patches.append((pos, actual, f"layer {i} offset"))
        out[pos:pos+8] = p64(actual)
        pos += 8
    pos += 8  # skip 0 terminator
    
    # Patch channel offset table (if any)
    while pos < len(corrupt) - 8:
        off = u64(corrupt, pos)
        if off == 0:
            pos += 8
            break
        actual = correct_offset(off)
        patches.append((pos, actual, "channel offset"))
        out[pos:pos+8] = p64(actual)
        pos += 8
    
    # 2. For each layer, patch hierarchy and mask offsets
    patched_layers = 0
    for i, layer in enumerate(layers_info):
        if layer is None:
            continue
        
        actual_loff = actual_offsets[i]
        nlen = len(layer['header']) - 16
        
        # Skip layer header + props to find hier/mask offsets
        lp = actual_loff + len(layer['header'])
        
        # Skip properties
        while lp < len(corrupt) - 8:
            pt = u32(corrupt, lp)
            ps = u32(corrupt, lp + 4)
            lp += 8
            if pt == 0: break
            lp += ps
        
        if lp + 16 > len(corrupt):
            print(f"  Layer {i} \"{layer['name']}\": can't reach hier/mask offsets")
            continue
        
        # Patch hierarchy offset
        stored_hier = u64(corrupt, lp)
        if layer['hierarchy'] and layer['hierarchy']['hier_off']:
            actual_hier = layer['hierarchy']['hier_off']
            patches.append((lp, actual_hier, f"layer {i} hier"))
            out[lp:lp+8] = p64(actual_hier)
        else:
            actual_hier = correct_offset(stored_hier)
            out[lp:lp+8] = p64(actual_hier)
        
        # Patch mask offset
        stored_mask = u64(corrupt, lp + 8)
        if stored_mask != 0:
            actual_mask = correct_offset(stored_mask)
            out[lp+8:lp+16] = p64(actual_mask)
        
        # 3. Patch hierarchy level offsets and tile offsets
        if layer['hierarchy']:
            hier = layer['hierarchy']
            hier_off = hier['hier_off']
            
            # Level offset table starts at hier_off + 12
            hp = hier_off + 12
            for li, level in enumerate(hier['levels']):
                if level['actual_off'] is not None:
                    out[hp:hp+8] = p64(level['actual_off'])
                    patches.append((hp, level['actual_off'], f"layer {i} level {li}"))
                hp += 8
            # Already has 0-terminator
            
            # Patch tile offsets within each level
            for li, level in enumerate(hier['levels']):
                if level['actual_off'] is None:
                    continue
                
                # Tile offset table starts at level_off + 8
                tp = level['actual_off'] + 8
                for ti, tile in enumerate(level['tiles']):
                    stored_tile_off = tile['stored_off']
                    actual_tile_off = correct_offset(stored_tile_off)
                    out[tp:tp+8] = p64(actual_tile_off)
                    tp += 8
        
        patched_layers += 1
    
    print(f"\nPatched {len(patches)} offsets across {patched_layers} layers")
    
    # Write output
    with open(output_path, 'wb') as f:
        f.write(out)
    
    return bytes(out)


def verify_xcf(data, label=""):
    """Verify XCF structure."""
    if data[:9] != b'gimp xcf ':
        print(f"  {label}ERROR: Bad magic")
        return 0
    
    ver = int(data[9:13].replace(b'v', b'').replace(b'\x00', b'').decode())
    
    pos = 14 + 4 + 4 + 4
    if ver >= 4: pos += 4
    
    while pos < len(data) - 8:
        pt = u32(data, pos); ps = u32(data, pos+4); pos += 8
        if pt == 0: break
        pos += ps
    
    count = 0
    valid = 0
    while pos < len(data) - 8:
        off = u64(data, pos); pos += 8
        if off == 0: break
        count += 1
        
        if off >= len(data) - 20:
            print(f"  {label}Layer {count}: 0x{off:X} OUT OF BOUNDS")
            continue
        
        w = u32(data, off); h = u32(data, off+4); nlen = u32(data, off+12)
        
        if nlen == 0 or nlen > 10000 or off + 16 + nlen > len(data):
            print(f"  {label}Layer {count}: 0x{off:X} bad nlen={nlen}")
            continue
        
        name = data[off+16:off+16+nlen-1].decode('utf-8', errors='replace')
        
        p = off + 16 + nlen
        while p < len(data) - 8:
            pt = u32(data, p); ps = u32(data, p+4); p += 8
            if pt == 0: break
            p += ps
        
        hier_ok = False
        if p + 16 <= len(data):
            hier_off = u64(data, p)
            if 0 < hier_off < len(data) - 12:
                hw = u32(data, hier_off); hh = u32(data, hier_off+4); hbpp = u32(data, hier_off+8)
                if hw == w and hh == h and hbpp in (1,2,3,4):
                    # Check level offset
                    lo = u64(data, hier_off + 12)
                    if 0 < lo < len(data) - 8:
                        lw = u32(data, lo); lh = u32(data, lo + 4)
                        if lw == w and lh == h:
                            hier_ok = True
        
        status = "OK" if hier_ok else "BROKEN"
        print(f"  {label}Layer {count}: \"{name}\" ({w}x{h}) [{status}]")
        if hier_ok:
            valid += 1
    
    print(f"  {label}Total: {count} layers, {valid} valid")
    return valid


def main():
    clean_path = sys.argv[1]
    corrupt_path = sys.argv[2]
    output_path = sys.argv[3] if len(sys.argv) > 3 else corrupt_path.replace('.xcf', '-RESTORED-v5.xcf')
    
    print(f"Clean:     {clean_path}")
    print(f"Corrupted: {corrupt_path}")
    print(f"Output:    {output_path}")
    
    with open(clean_path, 'rb') as f:
        clean = f.read()
    with open(corrupt_path, 'rb') as f:
        corrupt = f.read()
    
    print(f"\nClean:     {len(clean):,} bytes")
    print(f"Corrupted: {len(corrupt):,} bytes")
    
    # Parse layer table from corrupted file
    pos = 14 + 4 + 4 + 4
    ver_str = corrupt[9:13].replace(b'v', b'').replace(b'\x00', b'')
    ver = int(ver_str.decode())
    if ver >= 4: pos += 4
    while pos < len(corrupt) - 8:
        pt = u32(corrupt, pos); ps = u32(corrupt, pos+4); pos += 8
        if pt == 0: break
        pos += ps
    
    stored_offsets = []
    while pos < len(corrupt) - 8:
        off = u64(corrupt, pos); pos += 8
        if off == 0: break
        stored_offsets.append(off)
    
    print(f"\n{len(stored_offsets)} layers in table")
    
    # Find actual positions
    print("\nFinding actual layer positions...")
    actual_offsets = []
    layers_info = []
    
    for i, stored in enumerate(stored_offsets):
        result = find_actual_layer_offset(corrupt, stored)
        if result:
            actual_pos, delta, info = result
            actual_offsets.append(actual_pos)
            print(f"  Layer {i}: \"{info['name']}\" stored=0x{stored:X} actual=0x{actual_pos:X} delta={delta}")
        else:
            print(f"  Layer {i}: NOT FOUND near 0x{stored:X}")
            actual_offsets.append(stored)  # fallback
    
    # Parse each layer
    print("\nParsing layers...")
    for i in range(len(stored_offsets)):
        delta = actual_offsets[i] - stored_offsets[i]
        layer = parse_layer(corrupt, actual_offsets[i], stored_offsets[i], delta)
        layers_info.append(layer)
        if layer:
            hier = layer.get('hierarchy')
            if hier:
                total_tiles = sum(len(l['tiles']) for l in hier['levels'])
                print(f"  Layer {i}: \"{layer['name']}\" hier_at=0x{hier['hier_off']:X} "
                      f"bpp={hier['bpp']} levels={len(hier['levels'])} tiles={total_tiles}")
            else:
                print(f"  Layer {i}: \"{layer['name']}\" NO HIERARCHY")
        else:
            print(f"  Layer {i}: PARSE FAILED")
    
    # Reconstruct
    print("\nReconstructing XCF...")
    restored = write_xcf(corrupt, layers_info, actual_offsets, stored_offsets, output_path)
    
    print(f"\nRestored: {len(restored):,} bytes")
    
    # Verify
    print("\nStructural verification:")
    verify_xcf(restored)
    
    print(f"\nSaved: {output_path}")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <clean.xcf> <corrupted.xcf> [output.xcf]")
        sys.exit(1)
    main()

#!/usr/bin/env python3
"""
Restore a corrupted XCF by reinserting removed 0x0D bytes.

CORRECT approach: use the clean file's known CRLF positions to guide reinsertion.
For matched regions: exact reinsertion.
For diverged regions: DON'T blindly reinsert in structural data.

KEY INSIGHT: The layer table offsets in the corrupted file point to correct
positions IN THE CORRUPTED DATA. When we reinsert CRs, those offsets become
wrong because data shifts. So we must:
1. Parse the corrupted file completely first
2. Fix tile data by reinserting CRs (using parallel walk with clean)
3. Rewrite structural pointers to match the new positions

Simpler approach tried here: parse corrupted, identify all structural fields
(offsets, property ints), reinsert CRs ONLY in non-structural regions, then
patch structural offsets.

Actually simplest correct approach: 
- For every byte in the corrupted file, decide: was there a 0x0D before this byte
  in the original (pre-corruption) file?
- Use the clean file's parallel walk to determine this for unchanged regions.
- For changed regions, use structural parsing of the CORRUPTED file to identify
  which bytes are part of integer fields (don't reinsert) vs part of tile data (reinsert).
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


def strip_cr(data):
    result = bytearray()
    for i in range(len(data)):
        if data[i] == 0x0D and i + 1 < len(data) and data[i+1] == 0x0A:
            continue
        result.append(data[i])
    return bytes(result)


def skip_properties_return_ranges(d, pos):
    """Skip props, return (end_pos, list of byte ranges for each prop)."""
    ranges = []
    start = pos
    while pos < len(d) - 8:
        pt = u32(d, pos); pos += 4
        ps = u32(d, pos); pos += 4
        if pt == 0:
            break
        ranges.append((pos - 8, pos + ps))  # property header + data
        pos += ps
    return pos, ranges


def classify_corrupted_bytes(d):
    """
    Parse the corrupted file's own structure and classify each byte as:
    - 'S' (structural: integers, headers, property lists, offset tables)
    - 'T' (tile data: RLE compressed pixels)
    - 'U' (unknown / unclassified)
    
    Returns a bytearray same length as d with S=0, T=1, U=2.
    """
    S, T, U = 0, 1, 2
    bmap = bytearray([U] * len(d))
    
    if d[:9] != b'gimp xcf ':
        return bmap
    
    # File header is structural
    mark(bmap, 0, 14, S)  # magic "gimp xcf vXXX\0"
    
    pos = 14
    mark(bmap, pos, pos + 4, S)  # width
    w = u32(d, pos); pos += 4
    mark(bmap, pos, pos + 4, S)  # height
    h = u32(d, pos); pos += 4
    mark(bmap, pos, pos + 4, S)  # base_type
    pos += 4
    
    ver_str = d[9:13].replace(b'v', b'').replace(b'\x00', b'')
    ver = int(ver_str.decode())
    if ver >= 4:
        mark(bmap, pos, pos + 4, S)  # precision
        pos += 4
    
    # Image properties
    pos = classify_properties(d, pos, bmap, S)
    
    # Layer offset table
    layer_offsets = []
    table_start = pos
    while pos < len(d) - 8:
        mark(bmap, pos, pos + 8, S)  # each 8-byte offset
        off = u64(d, pos); pos += 8
        if off == 0:
            break
        layer_offsets.append(off)
    
    # Channel offset table
    while pos < len(d) - 8:
        mark(bmap, pos, pos + 8, S)
        off = u64(d, pos); pos += 8
        if off == 0:
            break
    
    print(f"  {len(layer_offsets)} layers found in layer table")
    
    # Parse each layer
    for li, loff in enumerate(layer_offsets):
        try:
            classify_layer(d, loff, bmap, S, T, li)
        except Exception as e:
            print(f"  Layer {li}: classify error at 0x{loff:X}: {e}")
    
    # Stats
    s_count = sum(1 for b in bmap if b == S)
    t_count = sum(1 for b in bmap if b == T)
    u_count = sum(1 for b in bmap if b == U)
    print(f"  Classified: Structural={s_count:,} Tile={t_count:,} Unknown={u_count:,}")
    
    return bmap


def mark(bmap, start, end, val):
    for i in range(max(0, start), min(end, len(bmap))):
        bmap[i] = val


def classify_properties(d, pos, bmap, S):
    """Classify property list bytes as structural. Return end position."""
    while pos < len(d) - 8:
        mark(bmap, pos, pos + 8, S)  # prop_type + payload_size
        pt = u32(d, pos); pos += 4
        ps = u32(d, pos); pos += 4
        if pt == 0:
            break
        mark(bmap, pos, pos + ps, S)  # property data is structural
        pos += ps
    return pos


def classify_layer(d, loff, bmap, S, T, layer_idx):
    """Classify layer bytes."""
    if loff >= len(d) - 20:
        return
    
    # Layer header: w(4) + h(4) + type(4) + name_len(4) + name(nlen)
    mark(bmap, loff, loff + 16, S)
    w = u32(d, loff)
    h = u32(d, loff + 4)
    lt = u32(d, loff + 8)
    nlen = u32(d, loff + 12)
    
    if nlen == 0 or nlen > 10000 or loff + 16 + nlen > len(d):
        if 0 < w <= 65536 and 0 < h <= 65536:
            print(f"  Layer {layer_idx}: bad nlen={nlen} at 0x{loff:X}, skipping layer body")
        return
    
    mark(bmap, loff + 16, loff + 16 + nlen, S)  # name string (incl null-term)
    name = d[loff+16:loff+16+nlen-1].decode('utf-8', errors='replace')
    
    p = loff + 16 + nlen
    
    # Layer properties
    p = classify_properties(d, p, bmap, S)
    
    if p + 16 > len(d):
        return
    
    # Hierarchy and mask offsets
    mark(bmap, p, p + 16, S)
    hier_off = u64(d, p)
    mask_off = u64(d, p + 8)
    
    tiles = 0
    if 0 < hier_off < len(d) - 12:
        tiles = classify_hierarchy(d, hier_off, w, h, bmap, S, T)
    
    print(f"  Layer {layer_idx}: \"{name}\" ({w}x{h}) hier=0x{hier_off:X} tiles={tiles}")


def classify_hierarchy(d, hier_off, w, h, bmap, S, T):
    """Classify hierarchy structure. Returns tile count."""
    if hier_off + 12 >= len(d):
        return 0
    
    # Hierarchy header: w(4) + h(4) + bpp(4)
    mark(bmap, hier_off, hier_off + 12, S)
    hw = u32(d, hier_off)
    hh = u32(d, hier_off + 4)
    hbpp = u32(d, hier_off + 8)
    
    if hw != w or hh != h or hbpp not in (1, 2, 3, 4):
        return 0
    
    # Level offset table
    p = hier_off + 12
    level_offsets = []
    while p < len(d) - 8:
        mark(bmap, p, p + 8, S)
        lo = u64(d, p); p += 8
        if lo == 0:
            break
        level_offsets.append(lo)
    
    total_tiles = 0
    for lvl_off in level_offsets:
        total_tiles += classify_level(d, lvl_off, w, h, hbpp, bmap, S, T)
    
    return total_tiles


def classify_level(d, lvl_off, w, h, bpp, bmap, S, T):
    """Classify level structure and tile data."""
    if lvl_off >= len(d) - 8:
        return 0
    
    # Level header: w(4) + h(4)
    mark(bmap, lvl_off, lvl_off + 8, S)
    
    # Tile offset table
    tp = lvl_off + 8
    tile_offsets = []
    while tp < len(d) - 8:
        mark(bmap, tp, tp + 8, S)
        to = u64(d, tp); tp += 8
        if to == 0:
            break
        tile_offsets.append(to)
    
    # Tile data
    for i in range(len(tile_offsets)):
        start = tile_offsets[i]
        if i + 1 < len(tile_offsets):
            end = tile_offsets[i + 1]
        else:
            end = min(start + 64 * 64 * bpp * 2, len(d))
        
        if 0 < start < len(d) and end <= len(d) and end > start:
            mark(bmap, start, end, T)
    
    return len(tile_offsets)


def parallel_walk_restore(clean, corrupt, corrupt_bmap):
    """
    Walk clean and corrupted in parallel, reinserting CRs.
    
    For matched regions: standard CRLF detection.
    For diverged regions: check corrupt_bmap to decide.
    """
    S, T, U = 0, 1, 2
    result = bytearray()
    ci = 0
    xi = 0
    
    stats = {
        'matched_cr': 0,
        'edited_tile_cr': 0,
        'edited_struct_kept': 0,
        'edited_unknown_cr': 0,
        'matched_bytes': 0,
        'div_events': 0,
        'div_bytes': 0,
    }
    
    while ci < len(clean) and xi < len(corrupt):
        # Check for CRLF in clean matching LF in corrupted
        if (ci + 1 < len(clean) and 
            clean[ci] == 0x0D and clean[ci+1] == 0x0A and
            corrupt[xi] == 0x0A):
            result.append(0x0D)
            result.append(0x0A)
            ci += 2
            xi += 1
            stats['matched_cr'] += 1
            continue
        
        if clean[ci] == corrupt[xi]:
            result.append(corrupt[xi])
            ci += 1
            xi += 1
            stats['matched_bytes'] += 1
            continue
        
        # Divergence — find resync
        resync = find_resync(clean, corrupt, ci, xi)
        stats['div_events'] += 1
        
        if resync is not None:
            new_ci, new_xi = resync
            stats['div_bytes'] += (new_xi - xi)
            
            for xp in range(xi, new_xi):
                b = corrupt[xp]
                if b == 0x0A:
                    btype = corrupt_bmap[xp] if xp < len(corrupt_bmap) else U
                    if btype == T:
                        result.append(0x0D)
                        result.append(0x0A)
                        stats['edited_tile_cr'] += 1
                    elif btype == S:
                        result.append(0x0A)
                        stats['edited_struct_kept'] += 1
                    else:
                        # Unknown region — DON'T reinsert (safer than reinserting)
                        result.append(0x0A)
                        stats['edited_unknown_cr'] += 1
                else:
                    result.append(b)
            
            ci = new_ci
            xi = new_xi
        else:
            # No resync
            stats['div_bytes'] += (len(corrupt) - xi)
            for xp in range(xi, len(corrupt)):
                b = corrupt[xp]
                if b == 0x0A:
                    btype = corrupt_bmap[xp] if xp < len(corrupt_bmap) else U
                    if btype == T:
                        result.append(0x0D)
                        result.append(0x0A)
                        stats['edited_tile_cr'] += 1
                    else:
                        result.append(0x0A)
                        stats['edited_struct_kept'] += 1 if btype == S else 0
                        stats['edited_unknown_cr'] += 1 if btype != S else 0
                else:
                    result.append(b)
            
            ci = len(clean)
            xi = len(corrupt)
            break
    
    # Remaining bytes
    if xi < len(corrupt):
        for xp in range(xi, len(corrupt)):
            b = corrupt[xp]
            if b == 0x0A:
                btype = corrupt_bmap[xp] if xp < len(corrupt_bmap) else U
                if btype == T:
                    result.append(0x0D)
                    result.append(0x0A)
                    stats['edited_tile_cr'] += 1
                else:
                    result.append(0x0A)
            else:
                result.append(b)
    
    print(f"\n  Matched bytes:          {stats['matched_bytes']:,}")
    print(f"  CRs reinserted (match): {stats['matched_cr']}")
    print(f"  CRs reinserted (tile):  {stats['edited_tile_cr']}")
    print(f"  0x0A kept (structural): {stats['edited_struct_kept']}")
    print(f"  0x0A kept (unknown):    {stats['edited_unknown_cr']}")
    print(f"  Divergence events:      {stats['div_events']}")
    print(f"  Divergence bytes:       {stats['div_bytes']:,}")
    
    return bytes(result)


def find_resync(clean, corrupt, ci, xi, max_search=200000, min_match=16):
    """Find next resynchronization point."""
    for xi_try in range(xi + 1, min(xi + max_search, len(corrupt) - min_match)):
        chunk = corrupt[xi_try:xi_try + min_match]
        
        ci_start = max(0, ci - 1000)
        ci_end = min(len(clean), ci + max_search + 1000)
        
        pos = ci_start
        while True:
            found = clean.find(chunk, pos, ci_end)
            if found == -1:
                break
            
            # Verify extended match
            ext = 0
            tc, tx = found, xi_try
            while tc < len(clean) and tx < len(corrupt) and ext < 64:
                if clean[tc] == corrupt[tx]:
                    ext += 1; tc += 1; tx += 1
                elif (tc + 1 < len(clean) and 
                      clean[tc] == 0x0D and clean[tc+1] == 0x0A and
                      corrupt[tx] == 0x0A):
                    ext += 1; tc += 2; tx += 1
                else:
                    break
            
            if ext >= min_match:
                return (found, xi_try)
            pos = found + 1
    
    return None


def fix_offsets_after_reinsertion(data, old_data):
    """
    After reinserting CRs, all stored offsets are wrong because data shifted.
    Build a mapping from old positions to new positions and patch all offsets.
    
    Actually: this is the hardest part. We need to know which bytes in the
    restored file are at offset X where previously they were at offset Y.
    
    Since we tracked the correspondence during the parallel walk, we'd need
    to build this mapping there. For now, skip offset patching — just do
    basic structural verification.
    """
    pass  # TODO: this is why the approach is fundamentally hard


def verify_xcf(data, label=""):
    """Verify XCF structure."""
    if data[:9] != b'gimp xcf ':
        print(f"  {label}ERROR: Bad magic")
        return False
    
    ver = int(data[9:13].replace(b'v', b'').replace(b'\x00', b'').decode())
    print(f"  {label}Version: v{ver:03d}")
    
    pos = 14
    w = u32(data, pos); h = u32(data, pos+4); pos += 12
    print(f"  {label}Canvas: {w}x{h}")
    if ver >= 4:
        pos += 4
    
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
        
        lw = u32(data, off)
        lh = u32(data, off+4)
        nlen = u32(data, off+12)
        
        if nlen == 0 or nlen > 10000 or off + 16 + nlen > len(data):
            print(f"  {label}Layer {count}: 0x{off:X} bad nlen={nlen}")
            continue
        
        name = data[off+16:off+16+nlen-1].decode('utf-8', errors='replace')
        
        # Check hierarchy
        p = off + 16 + nlen
        while p < len(data) - 8:
            pt = u32(data, p); ps = u32(data, p+4); p += 8
            if pt == 0: break
            p += ps
        
        hier_ok = False
        if p + 16 <= len(data):
            hier_off = u64(data, p)
            if 0 < hier_off < len(data)-12:
                hw = u32(data, hier_off); hh = u32(data, hier_off+4); hbpp = u32(data, hier_off+8)
                if hw == lw and hh == lh and hbpp in (1,2,3,4):
                    hier_ok = True
        
        status = "OK" if hier_ok else "BROKEN"
        print(f"  {label}Layer {count}: \"{name}\" ({lw}x{lh}) [{status}]")
        if hier_ok:
            valid += 1
    
    print(f"  {label}Total: {count} layers, {valid} valid")
    return valid > 0


def main():
    clean_path = sys.argv[1]
    corrupt_path = sys.argv[2]
    output_path = sys.argv[3] if len(sys.argv) > 3 else corrupt_path.replace('.xcf', '-RESTORED.xcf')
    
    print(f"Clean:     {clean_path}")
    print(f"Corrupted: {corrupt_path}")
    print(f"Output:    {output_path}")
    
    with open(clean_path, 'rb') as f:
        clean = f.read()
    with open(corrupt_path, 'rb') as f:
        corrupt = f.read()
    
    print(f"\nClean:     {len(clean):,} bytes")
    print(f"Corrupted: {len(corrupt):,} bytes")
    
    # Step 1: Classify corrupted file's bytes
    print("\nClassifying corrupted file bytes...")
    corrupt_bmap = classify_corrupted_bytes(corrupt)
    
    # Step 2: Parallel walk with structural guidance
    print("\nRestoring CRs...")
    restored = parallel_walk_restore(clean, corrupt, corrupt_bmap)
    
    print(f"\nRestored: {len(restored):,} bytes")
    
    # Roundtrip check
    rs = strip_cr(restored)
    if rs == corrupt:
        print("ROUNDTRIP OK: strip_cr(restored) == corrupted")
    else:
        print(f"ROUNDTRIP FAIL: {len(rs):,} vs {len(corrupt):,}")
    
    # Verify
    print("\nStructural verification:")
    verify_xcf(restored, "  ")
    
    with open(output_path, 'wb') as f:
        f.write(restored)
    print(f"\nSaved: {output_path}")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <clean.xcf> <corrupted.xcf> [output.xcf]")
        sys.exit(1)
    main()

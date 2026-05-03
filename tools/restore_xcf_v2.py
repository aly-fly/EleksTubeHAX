#!/usr/bin/env python3
"""
Restore a corrupted XCF file by reinserting 0x0D bytes that git removed.

Strategy:
1. Parse the corrupted file to identify tile data regions
2. Walk clean+corrupted in parallel; at matching regions, use CRLF from clean
3. In diverged regions that are tile data: reinsert 0x0D before every 0x0A
4. In diverged regions that are structural: do NOT reinsert (0x0A is a real value)

Also handles new layers that don't exist in clean file.
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
    i = 0
    while i < len(data):
        if i + 1 < len(data) and data[i] == 0x0D and data[i+1] == 0x0A:
            i += 1
        else:
            result.append(data[i])
            i += 1
    return bytes(result)


def skip_properties(d, pos):
    """Skip property list, return position after terminal prop."""
    while pos < len(d) - 8:
        pt = u32(d, pos); pos += 4
        ps = u32(d, pos); pos += 4
        if pt == 0:
            break
        pos += ps
    return pos


def parse_xcf_tile_ranges(d):
    """
    Parse the corrupted XCF and return a sorted list of 
    (start, end) byte ranges that contain tile data.
    Everything else is structural.
    """
    tile_ranges = []
    
    if d[:9] != b'gimp xcf ':
        print("ERROR: Not a valid XCF file")
        return tile_ranges
    
    ver_str = d[9:13].replace(b'v', b'').replace(b'\x00', b'')
    ver = int(ver_str.decode())
    
    pos = 14
    pos += 4 + 4 + 4  # width, height, base_type
    if ver >= 4:
        pos += 4  # precision
    pos = skip_properties(d, pos)
    
    # Read layer offsets
    layer_offsets = []
    while pos < len(d) - 8:
        off = u64(d, pos); pos += 8
        if off == 0:
            break
        layer_offsets.append(off)
    
    # Also read channel offsets (after layers)
    channel_offsets = []
    while pos < len(d) - 8:
        off = u64(d, pos); pos += 8
        if off == 0:
            break
        channel_offsets.append(off)
    
    print(f"  Found {len(layer_offsets)} layer offsets, {len(channel_offsets)} channel offsets")
    
    # Parse each layer
    for li, loff in enumerate(layer_offsets):
        if loff >= len(d) - 20:
            print(f"  Layer {li}: offset 0x{loff:X} out of bounds, skipping")
            continue
        
        try:
            ranges = parse_layer_tiles(d, loff, li)
            tile_ranges.extend(ranges)
        except Exception as e:
            print(f"  Layer {li}: parse error: {e}")
    
    # Parse channels too
    for ci, coff in enumerate(channel_offsets):
        if coff >= len(d) - 20:
            continue
        try:
            ranges = parse_channel_tiles(d, coff)
            tile_ranges.extend(ranges)
        except:
            pass
    
    # Sort and merge
    tile_ranges.sort()
    merged = []
    for start, end in tile_ranges:
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    
    return merged


def parse_layer_tiles(d, loff, layer_idx):
    """Parse a layer and return tile data ranges."""
    ranges = []
    
    w = u32(d, loff)
    h = u32(d, loff + 4)
    lt = u32(d, loff + 8)
    nlen = u32(d, loff + 12)
    
    if nlen == 0 or nlen > 10000:
        return ranges
    
    name = d[loff+16:loff+16+nlen-1].decode('utf-8', errors='replace')
    
    p = loff + 16 + nlen
    p = skip_properties(d, p)
    
    if p + 16 > len(d):
        return ranges
    
    hier_off = u64(d, p)
    mask_off = u64(d, p + 8)
    
    if hier_off > 0 and hier_off < len(d) - 12:
        hr = parse_hierarchy_tiles(d, hier_off, w, h, name)
        ranges.extend(hr)
    
    if mask_off > 0 and mask_off < len(d) - 20:
        try:
            # Mask is like a channel: w, h, name, props, hier_off
            mw = u32(d, mask_off)
            mh = u32(d, mask_off + 4)
            mnlen = u32(d, mask_off + 8)
            if 0 < mnlen < 1000 and mask_off + 12 + mnlen < len(d):
                mp = mask_off + 12 + mnlen
                mp = skip_properties(d, mp)
                if mp + 8 <= len(d):
                    mhier = u64(d, mp)
                    if 0 < mhier < len(d):
                        mr = parse_hierarchy_tiles(d, mhier, mw, mh, f"{name}/mask")
                        ranges.extend(mr)
        except:
            pass
    
    print(f"  Layer {layer_idx}: \"{name}\" ({w}x{h}) -> {len(ranges)} tile ranges")
    return ranges


def parse_channel_tiles(d, coff):
    """Parse a channel and return tile data ranges."""
    ranges = []
    w = u32(d, coff)
    h = u32(d, coff + 4)
    nlen = u32(d, coff + 8)
    if nlen == 0 or nlen > 10000:
        return ranges
    p = coff + 12 + nlen
    p = skip_properties(d, p)
    if p + 8 > len(d):
        return ranges
    hier_off = u64(d, p)
    if hier_off > 0 and hier_off < len(d) - 12:
        ranges = parse_hierarchy_tiles(d, hier_off, w, h, "channel")
    return ranges


def parse_hierarchy_tiles(d, hier_off, w, h, label=""):
    """Parse hierarchy structure and return tile data ranges."""
    ranges = []
    
    hw = u32(d, hier_off)
    hh = u32(d, hier_off + 4)
    hbpp = u32(d, hier_off + 8)
    
    if hw != w or hh != h or hbpp not in (1, 2, 3, 4):
        return ranges
    
    # Read level offsets
    p = hier_off + 12
    level_offsets = []
    while p < len(d) - 8:
        lo = u64(d, p); p += 8
        if lo == 0:
            break
        level_offsets.append(lo)
    
    if not level_offsets:
        return ranges
    
    # Only parse level 0 (full resolution)
    lvl0 = level_offsets[0]
    if lvl0 >= len(d) - 8:
        return ranges
    
    lw = u32(d, lvl0)
    lh = u32(d, lvl0 + 4)
    
    if lw != w or lh != h:
        return ranges
    
    # Read tile offsets
    tp = lvl0 + 8
    tile_offsets = []
    while tp < len(d) - 8:
        to = u64(d, tp); tp += 8
        if to == 0:
            break
        tile_offsets.append(to)
    
    if not tile_offsets:
        return ranges
    
    # Mark tile data ranges
    for i in range(len(tile_offsets)):
        start = tile_offsets[i]
        if i + 1 < len(tile_offsets):
            end = tile_offsets[i + 1]
        else:
            # Last tile — estimate size
            # Maximum tile data = 64*64*bpp + overhead for RLE
            end = min(start + 64 * 64 * hbpp * 2, len(d))
        
        if 0 < start < len(d) and 0 < end <= len(d) and end > start:
            ranges.append((start, end))
    
    return ranges


def is_in_tile_data(pos, tile_ranges):
    """Binary search to check if pos falls within a tile data range."""
    lo, hi = 0, len(tile_ranges) - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        start, end = tile_ranges[mid]
        if pos < start:
            hi = mid - 1
        elif pos >= end:
            lo = mid + 1
        else:
            return True
    return False


def find_resync(clean, corrupt, ci, xi, max_search=200000, min_match=16):
    """Find next position where clean and corrupt resynchronize."""
    search_chunk_size = min_match
    
    for xi_try in range(xi + 1, min(xi + max_search, len(corrupt) - search_chunk_size)):
        chunk = corrupt[xi_try:xi_try + search_chunk_size]
        
        ci_search_start = max(0, ci - 1000)
        ci_search_end = min(len(clean), ci + max_search + 1000)
        
        search_pos = ci_search_start
        while True:
            found = clean.find(chunk, search_pos, ci_search_end)
            if found == -1:
                break
            
            # Verify extended match
            extended_match = 0
            tc = found
            tx = xi_try
            while tc < len(clean) and tx < len(corrupt) and extended_match < 64:
                if clean[tc] == corrupt[tx]:
                    extended_match += 1
                    tc += 1
                    tx += 1
                elif (tc + 1 < len(clean) and 
                      clean[tc] == 0x0D and clean[tc+1] == 0x0A and
                      corrupt[tx] == 0x0A):
                    extended_match += 1
                    tc += 2
                    tx += 1
                else:
                    break
            
            if extended_match >= min_match:
                return (found, xi_try)
            
            search_pos = found + 1
    
    return None


def restore_with_structure_awareness(clean, corrupt, tile_ranges):
    """
    Walk clean and corrupted in parallel, reinserting CRs.
    In diverged regions, only reinsert CRs for bytes within tile data ranges.
    """
    result = bytearray()
    ci = 0
    xi = 0
    
    stats = {
        'matched_crs': 0,
        'tile_crs': 0,
        'struct_0a_kept': 0,
        'resyncs': 0,
        'no_resync_bytes': 0,
    }
    
    while ci < len(clean) and xi < len(corrupt):
        # Check for CRLF match
        if (ci + 1 < len(clean) and 
            clean[ci] == 0x0D and clean[ci+1] == 0x0A and
            corrupt[xi] == 0x0A):
            result.append(0x0D)
            result.append(0x0A)
            ci += 2
            xi += 1
            stats['matched_crs'] += 1
            continue
        
        if clean[ci] == corrupt[xi]:
            result.append(corrupt[xi])
            ci += 1
            xi += 1
            continue
        
        # Divergence — find resync
        best_resync = find_resync(clean, corrupt, ci, xi)
        
        if best_resync is not None:
            new_ci, new_xi = best_resync
            stats['resyncs'] += 1
            
            # Process the diverged chunk from corrupted file
            for xp in range(xi, new_xi):
                b = corrupt[xp]
                if b == 0x0A and is_in_tile_data(xp, tile_ranges):
                    result.append(0x0D)
                    result.append(0x0A)
                    stats['tile_crs'] += 1
                elif b == 0x0A:
                    result.append(0x0A)
                    stats['struct_0a_kept'] += 1
                else:
                    result.append(b)
            
            ci = new_ci
            xi = new_xi
        else:
            # No resync — process rest of corrupted file
            stats['no_resync_bytes'] += len(corrupt) - xi
            for xp in range(xi, len(corrupt)):
                b = corrupt[xp]
                if b == 0x0A and is_in_tile_data(xp, tile_ranges):
                    result.append(0x0D)
                    result.append(0x0A)
                    stats['tile_crs'] += 1
                elif b == 0x0A:
                    result.append(0x0A)
                    stats['struct_0a_kept'] += 1
                else:
                    result.append(b)
            ci = len(clean)
            xi = len(corrupt)
            break
    
    # Remaining corrupted bytes
    if xi < len(corrupt):
        for xp in range(xi, len(corrupt)):
            b = corrupt[xp]
            if b == 0x0A and is_in_tile_data(xp, tile_ranges):
                result.append(0x0D)
                result.append(0x0A)
                stats['tile_crs'] += 1
            elif b == 0x0A:
                result.append(0x0A)
                stats['struct_0a_kept'] += 1
            else:
                result.append(b)
    
    print(f"  CRs reinserted at known (matching) positions: {stats['matched_crs']}")
    print(f"  CRs reinserted in tile data regions: {stats['tile_crs']}")
    print(f"  0x0A kept unchanged in structural regions: {stats['struct_0a_kept']}")
    print(f"  Resync points found: {stats['resyncs']}")
    if stats['no_resync_bytes']:
        print(f"  Bytes processed without resync: {stats['no_resync_bytes']}")
    
    return bytes(result)


def verify_structure(data, label=""):
    """Quick structural check of the restored XCF."""
    if data[:9] != b'gimp xcf ':
        print(f"  {label} ERROR: Not a valid XCF file")
        return False
    
    ver_str = data[9:13].replace(b'v', b'').replace(b'\x00', b'')
    ver = int(ver_str.decode())
    print(f"  {label} XCF version: v{ver:03d}")
    
    pos = 14
    w = u32(data, pos); pos += 4
    h = u32(data, pos); pos += 4
    bt = u32(data, pos); pos += 4
    print(f"  {label} Canvas: {w}x{h}, base_type={bt}")
    
    if ver >= 4:
        pos += 4
    
    while pos < len(data) - 8:
        pt = u32(data, pos); pos += 4
        ps = u32(data, pos); pos += 4
        if pt == 0:
            break
        pos += ps
    
    layer_count = 0
    valid_layers = 0
    while pos < len(data) - 8:
        off = u64(data, pos); pos += 8
        if off == 0:
            break
        layer_count += 1
        
        if off < len(data) - 20:
            lw = u32(data, off)
            lh = u32(data, off + 4)
            lt = u32(data, off + 8)
            nlen = u32(data, off + 12)
            if 0 < nlen < 1000 and off + 16 + nlen <= len(data) and 0 < lw <= 65536 and 0 < lh <= 65536:
                name = data[off+16:off+16+nlen-1].decode('utf-8', errors='replace')
                
                # Try to parse hierarchy
                hp = off + 16 + nlen
                hp = skip_properties(data, hp)
                hier_ok = False
                if hp + 16 <= len(data):
                    hier_off = u64(data, hp)
                    if 0 < hier_off < len(data) - 12:
                        hw = u32(data, hier_off)
                        hh = u32(data, hier_off + 4)
                        hbpp = u32(data, hier_off + 8)
                        if hw == lw and hh == lh and hbpp in (1, 2, 3, 4):
                            hier_ok = True
                
                status = "OK" if hier_ok else "hierarchy BROKEN"
                print(f"  {label}   Layer {layer_count}: \"{name}\" ({lw}x{lh}) [{status}]")
                if hier_ok:
                    valid_layers += 1
            else:
                print(f"  {label}   Layer {layer_count}: @ 0x{off:X} (parse failed, nlen={nlen})")
        else:
            print(f"  {label}   Layer {layer_count}: @ 0x{off:X} (out of bounds)")
    
    print(f"  {label} Total layers: {layer_count}, valid: {valid_layers}")
    return valid_layers > 0


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
    
    print(f"\nClean size:     {len(clean):,} bytes")
    print(f"Corrupted size: {len(corrupt):,} bytes")
    
    clean_stripped = strip_cr(clean)
    print(f"Clean stripped:  {len(clean_stripped):,} bytes ({len(clean) - len(clean_stripped)} CRs removed)")
    
    # Step 1: Parse corrupted file for tile data regions
    print(f"\nParsing corrupted file structure...")
    tile_ranges = parse_xcf_tile_ranges(corrupt)
    total_tile_bytes = sum(e - s for s, e in tile_ranges)
    print(f"Found {len(tile_ranges)} tile data regions, total {total_tile_bytes:,} bytes "
          f"({100*total_tile_bytes/len(corrupt):.1f}% of file)")
    
    # Count 0x0A bytes in tile vs structural regions
    tile_0a = 0
    struct_0a = 0
    for i in range(len(corrupt)):
        if corrupt[i] == 0x0A:
            if is_in_tile_data(i, tile_ranges):
                tile_0a += 1
            else:
                struct_0a += 1
    print(f"0x0A bytes in tile data: {tile_0a}")
    print(f"0x0A bytes in structural data: {struct_0a}")
    
    # Step 2: Restore
    print(f"\nRestoring CRs...")
    restored = restore_with_structure_awareness(clean, corrupt, tile_ranges)
    
    print(f"\nRestored size:  {len(restored):,} bytes")
    
    # Verification: strip_cr(restored) should equal corrupted
    restored_stripped = strip_cr(restored)
    if restored_stripped == corrupt:
        print("ROUNDTRIP VERIFIED: strip_cr(restored) == corrupted")
    else:
        print(f"ROUNDTRIP MISMATCH: {len(restored_stripped)} vs {len(corrupt)}")
        for i in range(min(len(restored_stripped), len(corrupt))):
            if restored_stripped[i] != corrupt[i]:
                print(f"  First diff at byte {i}: 0x{restored_stripped[i]:02X} vs 0x{corrupt[i]:02X}")
                break
    
    # Step 3: Structural verification
    print(f"\nStructural verification of restored file:")
    verify_structure(restored, "RESTORED")
    
    # Also verify corrupted for comparison
    print(f"\nStructural verification of corrupted file:")
    verify_structure(corrupt, "CORRUPT ")
    
    with open(output_path, 'wb') as f:
        f.write(restored)
    print(f"\nSaved to: {output_path} ({len(restored):,} bytes)")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <clean.xcf> <corrupted.xcf> [output.xcf]")
        sys.exit(1)
    main()

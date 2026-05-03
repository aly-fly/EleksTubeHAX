#!/usr/bin/env python3
"""
Restore corrupted XCF by reinserting CRs using clean file as structural guide.

Approach: build a mapping from clean file positions to byte types (structural vs
tile data), then walk both files in parallel. When a CR needs to be reinserted:
- If the clean position says "tile data" -> reinsert CR
- If "structural" -> don't reinsert (0x0A is a real integer value)

For new/edited regions not in clean, we use the surrounding context to decide.
"""
import struct
import sys
import os
import bisect

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


def skip_properties(d, pos):
    while pos < len(d) - 8:
        pt = u32(d, pos); pos += 4
        ps = u32(d, pos); pos += 4
        if pt == 0:
            break
        pos += ps
    return pos


def mark_range(bitmap, start, end, val):
    """Set bitmap[start:end] to val."""
    for i in range(start, min(end, len(bitmap))):
        bitmap[i] = val


# Byte type constants
STRUCT = 0  # Structural data (integers, properties, headers)
TILE = 1    # RLE tile data (compressed pixels)


def classify_clean_bytes(d):
    """
    Parse clean XCF structure and classify every byte as STRUCT or TILE.
    Returns a bytearray the same length as d.
    """
    bmap = bytearray(len(d))  # default = STRUCT (0)
    
    if d[:9] != b'gimp xcf ':
        return bmap
    
    ver_str = d[9:13].replace(b'v', b'').replace(b'\x00', b'')
    ver = int(ver_str.decode())
    
    pos = 14
    w_canvas = u32(d, pos); pos += 4
    h_canvas = u32(d, pos); pos += 4
    bt = u32(d, pos); pos += 4
    if ver >= 4:
        pos += 4
    pos = skip_properties(d, pos)
    
    # Layer offsets
    layer_offsets = []
    while pos < len(d) - 8:
        off = u64(d, pos); pos += 8
        if off == 0:
            break
        layer_offsets.append(off)
    
    # Channel offsets
    while pos < len(d) - 8:
        off = u64(d, pos); pos += 8
        if off == 0:
            break
    
    for loff in layer_offsets:
        classify_layer(d, loff, bmap)
    
    return bmap


def classify_layer(d, loff, bmap):
    """Classify bytes of a layer."""
    if loff >= len(d) - 20:
        return
    
    w = u32(d, loff)
    h = u32(d, loff + 4)
    nlen = u32(d, loff + 12)
    if nlen == 0 or nlen > 10000 or loff + 16 + nlen > len(d):
        return
    
    p = loff + 16 + nlen
    p = skip_properties(d, p)
    
    if p + 16 > len(d):
        return
    
    hier_off = u64(d, p)
    mask_off = u64(d, p + 8)
    
    if 0 < hier_off < len(d):
        classify_hierarchy(d, hier_off, w, h, bmap)
    
    if 0 < mask_off < len(d) - 20:
        try:
            mw = u32(d, mask_off)
            mh = u32(d, mask_off + 4)
            mnlen = u32(d, mask_off + 8)
            if 0 < mnlen < 1000 and mask_off + 12 + mnlen < len(d):
                mp = mask_off + 12 + mnlen
                mp = skip_properties(d, mp)
                if mp + 8 <= len(d):
                    mhier = u64(d, mp)
                    if 0 < mhier < len(d):
                        classify_hierarchy(d, mhier, mw, mh, bmap)
        except:
            pass


def classify_hierarchy(d, hier_off, w, h, bmap):
    """Classify hierarchy tile bytes."""
    if hier_off + 12 >= len(d):
        return
    
    hw = u32(d, hier_off)
    hh = u32(d, hier_off + 4)
    hbpp = u32(d, hier_off + 8)
    
    if hw != w or hh != h or hbpp not in (1, 2, 3, 4):
        return
    
    p = hier_off + 12
    level_offsets = []
    while p < len(d) - 8:
        lo = u64(d, p); p += 8
        if lo == 0:
            break
        level_offsets.append(lo)
    
    # Process all levels (level 0 = full res, others = mipmaps)
    for lvl_off in level_offsets:
        if lvl_off >= len(d) - 8:
            continue
        lw = u32(d, lvl_off)
        lh = u32(d, lvl_off + 4)
        
        tp = lvl_off + 8
        tile_offsets = []
        while tp < len(d) - 8:
            to = u64(d, tp); tp += 8
            if to == 0:
                break
            tile_offsets.append(to)
        
        # Mark tile data
        for i in range(len(tile_offsets)):
            start = tile_offsets[i]
            if i + 1 < len(tile_offsets):
                end = tile_offsets[i + 1]
            else:
                end = min(start + 64 * 64 * hbpp * 2, len(d))
            mark_range(bmap, start, end, TILE)


def build_crlf_positions(clean):
    """Get sorted list of positions where CR appears before LF in clean file."""
    positions = []
    i = 0
    while i < len(clean) - 1:
        if clean[i] == 0x0D and clean[i+1] == 0x0A:
            positions.append(i)
            i += 2
        else:
            i += 1
    return positions


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
    
    # Step 1: Classify clean file bytes
    print("\nClassifying clean file bytes (structural vs tile data)...")
    bmap = classify_clean_bytes(clean)
    tile_bytes = sum(1 for b in bmap if b == TILE)
    struct_bytes = len(bmap) - tile_bytes
    print(f"  Tile data: {tile_bytes:,} bytes ({100*tile_bytes/len(clean):.1f}%)")
    print(f"  Structural: {struct_bytes:,} bytes ({100*struct_bytes/len(clean):.1f}%)")
    
    # Count CRLFs by type
    crlf_positions = build_crlf_positions(clean)
    crlf_in_tile = sum(1 for p in crlf_positions if bmap[p] == TILE)
    crlf_in_struct = sum(1 for p in crlf_positions if bmap[p] == STRUCT)
    print(f"  CRLFs in tile data: {crlf_in_tile}")
    print(f"  CRLFs in structural data: {crlf_in_struct}")
    
    # Step 2: Walk files in parallel with structure-aware CR reinsertion
    print("\nRestoring via parallel walk with structural guidance...")
    result = bytearray()
    ci = 0  # clean index
    xi = 0  # corrupt index
    
    stats = {
        'matched_cr': 0,      # CR reinserted because clean has CRLF at this position
        'edited_tile_cr': 0,  # CR reinserted in edited tile data region
        'edited_struct_keep': 0,  # 0x0A kept in edited structural region
        'matched_bytes': 0,
        'div_bytes': 0,
        'div_events': 0,
    }
    
    in_tile_context = False  # Whether we're in a tile data region
    last_clean_tile = False  # Whether the last matching clean position was tile data
    
    def process_diverged_byte(b, corrupt_pos):
        """Process a byte from a diverged (edited) region."""
        nonlocal result
        if b == 0x0A:
            # In diverged regions, use the context from where we diverged
            if last_clean_tile:
                # We were in tile data — reinsert CR
                result.append(0x0D)
                result.append(0x0A)
                stats['edited_tile_cr'] += 1
            else:
                # We were in structural data — keep as-is
                result.append(0x0A)
                stats['edited_struct_keep'] += 1
        else:
            result.append(b)
    
    while ci < len(clean) and xi < len(corrupt):
        # Track tile context
        if ci < len(bmap):
            last_clean_tile = (bmap[ci] == TILE)
        
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
        
        if resync is not None:
            new_ci, new_xi = resync
            stats['div_events'] += 1
            stats['div_bytes'] += (new_xi - xi)
            
            for xp in range(xi, new_xi):
                process_diverged_byte(corrupt[xp], xp)
            
            ci = new_ci
            xi = new_xi
        else:
            # No resync — process remaining
            stats['div_events'] += 1
            stats['div_bytes'] += (len(corrupt) - xi)
            
            for xp in range(xi, len(corrupt)):
                process_diverged_byte(corrupt[xp], xp)
            
            ci = len(clean)
            xi = len(corrupt)
            break
    
    # Remaining corrupted bytes (new content at end of file)
    if xi < len(corrupt):
        for xp in range(xi, len(corrupt)):
            process_diverged_byte(corrupt[xp], xp)
    
    restored = bytes(result)
    
    print(f"\n  Matching bytes: {stats['matched_bytes']:,}")
    print(f"  CRs reinserted (matched): {stats['matched_cr']}")
    print(f"  CRs reinserted (edited tile regions): {stats['edited_tile_cr']}")
    print(f"  0x0A kept (edited structural regions): {stats['edited_struct_keep']}")
    print(f"  Divergence events: {stats['div_events']}")
    print(f"  Divergence bytes: {stats['div_bytes']:,}")
    
    print(f"\nRestored size: {len(restored):,} bytes")
    
    # Roundtrip verification
    restored_stripped = strip_cr(restored)
    if restored_stripped == corrupt:
        print("ROUNDTRIP VERIFIED: strip_cr(restored) == corrupted")
    else:
        print(f"ROUNDTRIP MISMATCH: {len(restored_stripped)} vs {len(corrupt)}")
    
    # Structural verification
    print(f"\nStructural verification:")
    verify_structure(restored)
    
    with open(output_path, 'wb') as f:
        f.write(restored)
    print(f"\nSaved: {output_path} ({len(restored):,} bytes)")


def find_resync(clean, corrupt, ci, xi, max_search=200000, min_match=16):
    """Find next resynchronization point."""
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


def verify_structure(data):
    """Check restored XCF structure."""
    if data[:9] != b'gimp xcf ':
        print("  ERROR: Bad magic")
        return
    
    ver = int(data[9:13].replace(b'v', b'').replace(b'\x00', b'').decode())
    print(f"  Version: v{ver:03d}")
    
    pos = 14
    w = u32(data, pos); pos += 4
    h = u32(data, pos); pos += 4
    bt = u32(data, pos); pos += 4
    print(f"  Canvas: {w}x{h}, type={bt}")
    
    if ver >= 4:
        pos += 4
    pos = skip_properties(data, pos)
    
    count = 0
    valid = 0
    while pos < len(data) - 8:
        off = u64(data, pos); pos += 8
        if off == 0:
            break
        count += 1
        
        if off >= len(data) - 20:
            print(f"  Layer {count}: @ 0x{off:X} OUT OF BOUNDS")
            continue
        
        lw = u32(data, off)
        lh = u32(data, off + 4)
        lt = u32(data, off + 8)
        nlen = u32(data, off + 12)
        
        if nlen == 0 or nlen > 10000 or off + 16 + nlen > len(data):
            print(f"  Layer {count}: @ 0x{off:X} bad nlen={nlen}")
            continue
        
        name = data[off+16:off+16+nlen-1].decode('utf-8', errors='replace')
        
        # Check hierarchy
        p = off + 16 + nlen
        p = skip_properties(data, p)
        hier_ok = False
        tiles = 0
        if p + 16 <= len(data):
            hier_off = u64(data, p)
            if 0 < hier_off < len(data) - 12:
                hw = u32(data, hier_off)
                hh = u32(data, hier_off + 4)
                hbpp = u32(data, hier_off + 8)
                if hw == lw and hh == lh and hbpp in (1, 2, 3, 4):
                    hier_ok = True
                    # Count tiles
                    hp = hier_off + 12
                    if hp + 8 <= len(data):
                        lvl0 = u64(data, hp)
                        if 0 < lvl0 < len(data) - 8:
                            if u32(data, lvl0) == lw and u32(data, lvl0+4) == lh:
                                tp = lvl0 + 8
                                while tp < len(data) - 8:
                                    to = u64(data, tp); tp += 8
                                    if to == 0:
                                        break
                                    tiles += 1
        
        status = f"OK, {tiles} tiles" if hier_ok else "BROKEN"
        print(f"  Layer {count}: \"{name}\" ({lw}x{lh}) [{status}]")
        if hier_ok:
            valid += 1
    
    print(f"  Total: {count} layers, {valid} valid")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <clean.xcf> <corrupted.xcf> [output.xcf]")
        sys.exit(1)
    main()

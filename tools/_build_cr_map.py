#!/usr/bin/env python3
"""
Build a precise offset mapping between the corrupted file and what the 
original (pre-corruption) file would have looked like.

The key insight: git removed every 0x0D that preceded 0x0A.
So if the ORIGINAL file had byte B at position P, and there were N bytes
of 0x0D-before-0x0A removed before position P, then B is at position P-N
in the corrupted file.

We build this mapping using the clean file (which shares most of its content
with the original) and the corrupted file.

There are two types of offsets we need to fix:
1. Position P_orig in the original → position P_corrupt = P_orig - CR_count(P_orig)
2. Position P_corrupt in corrupted → what was P_orig? P_orig = P_corrupt + CR_count(P_orig)

We need a map: orig_pos → corrupt_pos for all structural offset values.

Since the stored offsets in the corrupted file ARE the original positions
(GIMP wrote them correctly, then git munged the file), we just need:
for each stored offset value V, find V's corresponding corrupt position.

Approach: scan through the corrupted file counting all 0x0A bytes that
WERE preceded by 0x0D in the original. We know which ones from the 
parallel walk with the clean file. For edited regions, we use the cumulative
count from surrounding known regions.
"""
import struct, sys, os, bisect
os.environ.setdefault('PYTHONIOENCODING', 'utf-8')
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def u32(d, pos): return struct.unpack('>I', d[pos:pos+4])[0]
def u64(d, pos): return struct.unpack('>Q', d[pos:pos+8])[0]
def p32(v): return struct.pack('>I', v)
def p64(v): return struct.pack('>Q', v)


def build_cr_map(clean, corrupt):
    """
    Build a mapping from original file positions to corrupted file positions.
    
    Walk through both files in parallel. When clean has 0x0D 0x0A and corrupt
    has just 0x0A, we know a CR was removed at that original position.
    
    Returns: list of (orig_pos, corrupt_pos) tuples at each CR removal point.
    These define a piecewise linear function: between consecutive removals,
    the mapping is simply corrupt_pos = orig_pos - removals_so_far.
    """
    cr_removals = []  # list of orig_pos where CR was removed
    ci = 0  # clean index (closest to original positions)
    xi = 0  # corrupt index
    
    matched_crs = 0
    
    while ci < len(clean) and xi < len(corrupt):
        if (ci + 1 < len(clean) and 
            clean[ci] == 0x0D and clean[ci+1] == 0x0A and
            corrupt[xi] == 0x0A):
            # CR removal at original position ci
            # At this point: orig_pos ci → corrupt_pos xi
            # (the 0x0D was at orig_pos ci, 0x0A at ci+1, in corrupt it's xi)
            cr_removals.append(ci)  # orig pos of the 0x0D that was removed
            ci += 2
            xi += 1
            matched_crs += 1
            continue
        
        if clean[ci] == corrupt[xi]:
            ci += 1
            xi += 1
            continue
        
        # Divergence — find resync
        resync = find_resync(clean, corrupt, ci, xi)
        if resync:
            new_ci, new_xi = resync
            # In the diverged region, we DON'T know where CRs were.
            # But we can count the total: the total shift from before to after
            # divergence must account for all CRs (both known and unknown).
            # For now, skip — we'll interpolate later.
            ci = new_ci
            xi = new_xi
        else:
            break
    
    print(f"  Built CR map: {len(cr_removals)} known CR positions from parallel walk")
    return cr_removals


def find_resync(clean, corrupt, ci, xi, max_search=200000, min_match=16):
    for xi_try in range(xi + 1, min(xi + max_search, len(corrupt) - min_match)):
        chunk = corrupt[xi_try:xi_try + min_match]
        ci_start = max(0, ci - 1000)
        ci_end = min(len(clean), ci + max_search + 1000)
        pos = ci_start
        while True:
            found = clean.find(chunk, pos, ci_end)
            if found == -1: break
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


def orig_to_corrupt(orig_pos, cr_removals):
    """
    Given an original file position, return the corrupted file position.
    
    Each CR removal at orig_pos P means everything at P+1 and beyond shifts -1.
    So corrupt_pos = orig_pos - count_of_removals_before_or_at(orig_pos)
    """
    # Number of removals at or before orig_pos
    idx = bisect.bisect_right(cr_removals, orig_pos)
    return orig_pos - idx


def find_layer_actual_offset(d, stored_off, search_range=200000):
    """Find actual layer header position near stored offset."""
    best = None
    for delta in range(-search_range, search_range + 1):
        pos = stored_off + delta
        if pos < 0 or pos + 20 >= len(d):
            continue
        w = u32(d, pos)
        h = u32(d, pos+4)
        lt = u32(d, pos+8)
        nlen = u32(d, pos+12)
        if w == 0 or h == 0 or w > 65536 or h > 65536: continue
        if lt > 10: continue
        if nlen == 0 or nlen > 500: continue
        if pos + 16 + nlen > len(d): continue
        name = d[pos+16:pos+16+nlen]
        if name[-1:] != b'\x00': continue
        try: name[:-1].decode('utf-8')
        except: continue
        if not all(32 <= b < 127 or b > 127 for b in name[:-1]): continue
        if best is None or abs(delta) < abs(best[0]):
            best = (delta, pos, name[:-1].decode('utf-8'))
    return best


def main():
    clean_path = sys.argv[1]
    corrupt_path = sys.argv[2]
    output_path = sys.argv[3] if len(sys.argv) > 3 else corrupt_path.replace('.xcf', '-RESTORED-v6.xcf')
    
    print(f"Clean:     {clean_path}")
    print(f"Corrupted: {corrupt_path}")
    print(f"Output:    {output_path}")
    
    with open(clean_path, 'rb') as f: clean = f.read()
    with open(corrupt_path, 'rb') as f: corrupt = f.read()
    
    print(f"\nClean:     {len(clean):,} bytes")
    print(f"Corrupted: {len(corrupt):,} bytes")
    
    # Step 1: Build CR map from parallel walk
    print("\nBuilding CR removal map...")
    cr_removals = build_cr_map(clean, corrupt)
    
    # Step 2: Add CR positions from edited regions
    # We know from layer analysis:
    # - Between layers, there are additional CRs not found by parallel walk
    # - Total ~849 CRs removed by end of file
    # For edited (diverged) regions, we need to find CRs there too.
    # 
    # Strategy: scan the corrupted file for 0x0A bytes. For each one,
    # check if the original file would have had 0x0D before it.
    # In matched regions, we know. In edited regions, we DON'T know
    # for tile data, but we DO know for structural data (integers don't
    # coincidentally have 0x0D0A patterns — well, they CAN, but it's
    # the data GIMP wrote).
    
    # Actually, let's first check how many CRs the parallel walk found,
    # vs. how many total CRs the ORIGINAL file would have had.
    # The original file had MORE CRs than the clean file (because it's
    # a different file with different content). We know from the layer
    # offset analysis that 849 CRs were removed total (delta at last layer).
    
    # But 849 is the delta at the LAST layer header. The total CRs in the
    # file could be more (there's content after the last layer header).
    
    # Let's use the CR map to correct known offsets and see how well it works.
    
    print(f"\n  Known CRs from parallel walk: {len(cr_removals)}")
    
    # Verify against known layer positions
    print("\nVerifying CR map against known layer offsets...")
    
    # Parse layer table
    pos = 14 + 4 + 4 + 4
    ver = int(corrupt[9:13].replace(b'v',b'').replace(b'\x00',b'').decode())
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
    
    for i, stored in enumerate(stored_offsets):
        # Find actual position
        result = find_layer_actual_offset(corrupt, stored)
        if result:
            delta, actual, name = result
            # What does our CR map predict?
            predicted = orig_to_corrupt(stored, cr_removals)
            map_delta = predicted - stored
            print(f"  Layer {i:2d}: stored=0x{stored:X} actual=0x{actual:X} "
                  f"(delta={delta:+d}) predicted=0x{predicted:X} "
                  f"(map_delta={map_delta:+d}) "
                  f"{'OK' if predicted == actual else f'OFF by {actual-predicted}'} "
                  f"\"{name}\"")
        else:
            print(f"  Layer {i:2d}: stored=0x{stored:X} NOT FOUND")
    
    # Step 3: Build a more complete CR position list by also scanning
    # the corrupted file for ALL 0x0A bytes and finding which ones had 0x0D.
    # This is only possible for matched regions (from parallel walk).
    # For edited regions, we need a different approach.
    
    # For now, let's just count how many CRs are in edited regions.
    # We know there are divergence events. Let's track them.
    
    # Actually, let's try a different approach: since we can find all layer
    # headers, hierarchies, levels, and tile offsets by searching for known
    # patterns, we can build a comprehensive calibration map with HUNDREDS
    # of calibration points, giving us much better interpolation.
    
    print("\nBuilding comprehensive calibration from ALL structure elements...")
    
    # Use all found structural elements as calibration points
    cal_points = []  # (stored_pos, actual_pos)
    
    for i, stored in enumerate(stored_offsets):
        result = find_layer_actual_offset(corrupt, stored)
        if result:
            delta, actual, name = result
            cal_points.append((stored, actual))
    
    # Also find hierarchy offsets for each layer
    for i, stored in enumerate(stored_offsets):
        result = find_layer_actual_offset(corrupt, stored)
        if not result: continue
        _, actual_loff, name = result
        
        # Parse to hierarchy offset
        nlen = u32(corrupt, actual_loff + 12)
        p = actual_loff + 16 + nlen
        while p < len(corrupt) - 8:
            pt = u32(corrupt, p); ps = u32(corrupt, p+4); p += 8
            if pt == 0: break
            p += ps
        if p + 8 <= len(corrupt):
            stored_hier = u64(corrupt, p)
            # Find actual hierarchy
            w = u32(corrupt, actual_loff)
            h = u32(corrupt, actual_loff + 4)
            actual_hier = find_hierarchy_offset(corrupt, stored_hier, w, h, actual_loff)
            if actual_hier is not None:
                cal_points.append((stored_hier, actual_hier))
                
                # Parse hierarchy for level offsets
                bpp = u32(corrupt, actual_hier + 8)
                hp = actual_hier + 12
                level_offsets = []
                while hp + 8 <= len(corrupt):
                    lo = u64(corrupt, hp); hp += 8
                    if lo == 0: break
                    level_offsets.append(lo)
                
                for li, stored_lo in enumerate(level_offsets):
                    lw = w >> li
                    lh = h >> li
                    if lw < 1: lw = 1
                    if lh < 1: lh = 1
                    actual_lo = find_level_offset(corrupt, stored_lo, actual_hier, lw, lh)
                    if actual_lo is not None:
                        cal_points.append((stored_lo, actual_lo))
                        
                        # Parse level for tile offsets
                        tp = actual_lo + 8
                        tile_offs = []
                        while tp + 8 <= len(corrupt):
                            to = u64(corrupt, tp); tp += 8
                            if to == 0: break
                            tile_offs.append((to, tp - 8))  # (stored_val, table_pos)
                        
                        # Tiles are sequential. If we can find the first tile's
                        # actual position, we can derive the rest.
                        # For now, just collect the stored offsets.
    
    cal_points.sort(key=lambda x: x[0])
    # Remove duplicates
    cal_points = list(dict(cal_points).items())
    cal_points.sort(key=lambda x: x[0])
    
    print(f"  {len(cal_points)} calibration points")
    
    # Show the calibration curve
    print("\n  Stored       Actual       Delta")
    for stored, actual in cal_points:
        delta = actual - stored
        print(f"  0x{stored:08X}  0x{actual:08X}  {delta:+d}")


def find_hierarchy_offset(d, stored_off, w, h, layer_off):
    target = p32(w) + p32(h)
    for delta in range(-10000, 10000):
        pos = stored_off + delta
        if pos < layer_off or pos + 12 >= len(d): continue
        if d[pos:pos+8] == target:
            bpp = u32(d, pos + 8)
            if bpp in (1,2,3,4):
                if pos + 20 <= len(d):
                    lo = u64(d, pos + 12)
                    if 0 < lo < len(d):
                        return pos
    return None


def find_level_offset(d, stored_off, hier_off, w, h):
    target = p32(w) + p32(h)
    for delta in range(-10000, 10000):
        pos = stored_off + delta
        if pos < hier_off or pos + 16 >= len(d): continue
        if d[pos:pos+8] == target:
            tp = pos + 8
            if tp + 8 <= len(d):
                to = u64(d, tp)
                if 0 < to < len(d):
                    return pos
    return None


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <clean.xcf> <corrupted.xcf> [output.xcf]")
        sys.exit(1)
    main()

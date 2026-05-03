#!/usr/bin/env python3
"""
Repair XCF files damaged by Git's CRLF→LF conversion.
Corruption: Git removed 0x0D bytes before 0x0A in binary data regions.
Strategy: Parse XCF structure, identify binary vs text regions,
          re-insert 0x0D before 0x0A in binary regions.
"""
import struct
import sys
import os

def read_u32(data, pos):
    return struct.unpack('>I', data[pos:pos+4])[0], pos + 4

def read_u64(data, pos):
    return struct.unpack('>Q', data[pos:pos+8])[0], pos + 8

def write_u32(val):
    return struct.pack('>I', val)

def write_u64(val):
    return struct.pack('>Q', val)

def analyze_xcf(path):
    """Phase 1: Analyze structure and find corruption points."""
    with open(path, 'rb') as fh:
        f = fh.read()
    
    print(f"File: {path}")
    print(f"Size: {len(f)} bytes")
    
    # Header
    magic = f[:14]
    print(f"Magic: {magic}")
    ver_str = magic[9:13].replace(b'v', b'').replace(b'\x00', b'').decode()
    ver = int(ver_str)
    use64 = ver >= 11
    print(f"Version: {ver} ({'64-bit' if use64 else '32-bit'} offsets)")
    
    pos = 14
    width, pos = read_u32(f, pos)
    height, pos = read_u32(f, pos)
    img_type, pos = read_u32(f, pos)
    if use64:
        precision, pos = read_u32(f, pos)
    else:
        precision = 0
    print(f"Image: {width}x{height} type={img_type} precision={precision}")
    
    # Properties
    compression = 0
    props_ranges = []  # (start, end) of each property payload
    parasite_ranges = []  # text regions to NOT repair
    
    while pos < len(f) - 8:
        prop_start = pos
        ptype, pos = read_u32(f, pos)
        psize, pos = read_u32(f, pos)
        if ptype == 0:  # PROP_END
            print(f"  PROP_END at {prop_start}")
            break
        payload_start = pos
        payload_end = pos + psize
        
        if ptype == 17:  # PROP_COMPRESSION
            compression = f[pos]
            comp_names = {0: 'none', 1: 'RLE', 2: 'zlib'}
            print(f"  Compression: {comp_names.get(compression, 'unknown')} ({compression})")
        
        if ptype == 21:  # PROP_PARASITES - contains text (XML, etc.)
            parasite_ranges.append((payload_start, payload_end))
            print(f"  PROP_PARASITES at {payload_start}-{payload_end} ({psize} bytes)")
        else:
            print(f"  Property type={ptype} size={psize} at {prop_start}")
        
        props_ranges.append((payload_start, payload_end))
        pos = payload_end
    
    # Layer offsets
    layer_offsets = []
    layer_offsets_pos = pos  # where the offset table starts
    if use64:
        while pos < len(f) - 8:
            off, pos = read_u64(f, pos)
            if off == 0:
                break
            layer_offsets.append(off)
    else:
        while pos < len(f) - 4:
            off, pos = read_u32(f, pos)
            if off == 0:
                break
            layer_offsets.append(off)
    
    # Channel offsets
    channel_offsets = []
    if use64:
        while pos < len(f) - 8:
            off, pos = read_u64(f, pos)
            if off == 0:
                break
            channel_offsets.append(off)
    else:
        while pos < len(f) - 4:
            off, pos = read_u32(f, pos)
            if off == 0:
                break
            channel_offsets.append(off)
    
    data_start = pos  # after all header pointers
    
    print(f"\n{len(layer_offsets)} layer offsets: {[hex(o) for o in layer_offsets]}")
    print(f"{len(channel_offsets)} channel offsets: {[hex(o) for o in channel_offsets]}")
    print(f"Data starts at: {pos} (0x{pos:X})")
    
    # Count 0x0A bytes in the header region (before first layer data)
    first_layer_off = min(layer_offsets) if layer_offsets else len(f)
    header_region = f[:first_layer_off]
    header_lf = header_region.count(b'\x0a')
    header_crlf = header_region.count(b'\x0d\x0a')
    print(f"\nHeader region (0 to {first_layer_off}): {header_lf} LF, {header_crlf} CRLF")
    
    # Analyze each layer
    print(f"\n=== Layer Analysis ===")
    cumulative_shift = 0
    
    for i, off in enumerate(layer_offsets):
        if off >= len(f):
            print(f"  Layer {i}: offset 0x{off:X} beyond file end!")
            continue
        
        # Try reading layer header at the stored offset
        try:
            w, _ = read_u32(f, off)
            h, _ = read_u32(f, off + 4)
            t, _ = read_u32(f, off + 8)
            nlen, _ = read_u32(f, off + 12)
        except:
            print(f"  Layer {i}: offset 0x{off:X} - can't read header")
            continue
        
        valid = (0 < w <= 100000 and 0 < h <= 100000 and t <= 6 and 0 < nlen < 10000)
        
        if valid:
            name = f[off+16:off+16+nlen-1]
            try:
                name_str = name.decode('utf-8')
                print(f"  Layer {i}: offset 0x{off:X} OK - \"{name_str}\" ({w}x{h})")
            except:
                print(f"  Layer {i}: offset 0x{off:X} VALID structure but broken name ({w}x{h})")
        else:
            # Try to find the layer by searching nearby
            print(f"  Layer {i}: offset 0x{off:X} INVALID (w={w} h={h} t={t} nlen={nlen})")
            
            # Search backwards from the stored offset for valid layer header
            found = False
            search_range = 5000  # search up to 5000 bytes around
            for delta in range(0, search_range):
                for try_off in [off - delta, off + delta]:
                    if try_off < 0 or try_off + 20 >= len(f):
                        continue
                    try:
                        tw, _ = read_u32(f, try_off)
                        th, _ = read_u32(f, try_off + 4)
                        tt, _ = read_u32(f, try_off + 8)
                        tnlen, _ = read_u32(f, try_off + 12)
                        if (0 < tw <= 100000 and 0 < th <= 100000 and 
                            tt <= 6 and 0 < tnlen < 10000):
                            name_raw = f[try_off+16:try_off+16+min(tnlen-1, 200)]
                            try:
                                name_str = name_raw.decode('utf-8')
                                shift = off - try_off
                                print(f"          FOUND at 0x{try_off:X} (shift={shift}) "
                                      f"\"{name_str}\" ({tw}x{th})")
                                found = True
                                break
                            except:
                                pass
                    except:
                        pass
                if found:
                    break
    
    # Summary stats
    print(f"\n=== Byte Statistics ===")
    total_lf = f.count(b'\x0a')
    total_crlf = f.count(b'\x0d\x0a')
    total_cr = f.count(b'\x0d')
    print(f"0x0A (LF):  {total_lf}")
    print(f"0x0D (CR):  {total_cr}")
    print(f"CRLF pairs: {total_crlf}")
    print(f"Standalone LF: {total_lf - total_crlf}")
    print(f"Standalone CR: {total_cr - total_crlf}")
    
    return {
        'data': f,
        'version': ver,
        'use64': use64,
        'width': width,
        'height': height,
        'compression': compression,
        'layer_offsets': layer_offsets,
        'layer_offsets_pos': layer_offsets_pos,
        'channel_offsets': channel_offsets,
        'parasite_ranges': parasite_ranges,
    }


def find_all_layer_positions(info):
    """Search the file for all valid layer headers to build a shift map."""
    f = info['data']
    width = info['width']
    height = info['height']
    
    print(f"\n=== Searching for valid layer headers ===")
    
    # Known valid dimensions for this image
    # Layers can be image-size or smaller (pasted layers)
    candidates = []
    
    # Search entire file for 4-byte patterns that could be valid width+height
    for pos in range(0, len(f) - 20, 1):
        try:
            w, _ = read_u32(f, pos)
            h, _ = read_u32(f, pos + 4)
            t, _ = read_u32(f, pos + 8)
            nlen, _ = read_u32(f, pos + 12)
        except:
            continue
        
        # Filter: reasonable dimensions, valid type, reasonable name length
        if not (0 < w <= 65536 and 0 < h <= 65536 and t <= 6 and 0 < nlen < 500):
            continue
        
        # Try to read the name
        if pos + 16 + nlen > len(f):
            continue
        name_raw = f[pos+16:pos+16+nlen-1]
        try:
            name_str = name_raw.decode('utf-8')
            # Check it's printable
            if all(c.isprintable() or c in '\n\r\t' for c in name_str):
                candidates.append((pos, w, h, t, name_str))
        except:
            continue
    
    print(f"Found {len(candidates)} candidate layer headers:")
    for pos, w, h, t, name in candidates:
        print(f"  0x{pos:08X}: \"{name}\" ({w}x{h} type={t})")
    
    return candidates


def compute_repair_map(info, candidates):
    """Compare stored layer offsets with found positions to compute byte shifts."""
    offsets = info['layer_offsets']
    
    print(f"\n=== Computing repair map ===")
    
    # Match each stored offset to the nearest candidate
    repair_map = []  # List of (stored_offset, actual_offset, shift)
    used_candidates = set()
    
    for i, stored_off in enumerate(offsets):
        best_match = None
        best_dist = float('inf')
        
        for j, (pos, w, h, t, name) in enumerate(candidates):
            if j in used_candidates:
                continue
            dist = abs(stored_off - pos)
            if dist < best_dist:
                best_dist = dist
                best_match = (j, pos, w, h, t, name)
        
        if best_match and best_dist < 50000:
            j, pos, w, h, t, name = best_match
            shift = stored_off - pos
            repair_map.append((stored_off, pos, shift, name))
            used_candidates.add(j)
            print(f"  Layer {i}: stored=0x{stored_off:X} actual=0x{pos:X} "
                  f"shift={shift} bytes \"{name}\"")
        else:
            print(f"  Layer {i}: stored=0x{stored_off:X} NO MATCH FOUND")
            repair_map.append((stored_off, None, None, None))
    
    return repair_map


def repair_xcf(info, repair_map, output_path):
    """
    Repair the XCF file by re-inserting 0x0D before 0x0A in binary regions.
    
    Strategy: 
    - We know the exact cumulative byte shift at each layer boundary
    - For each region between layers, we compute how many 0x0D to re-insert
    - We insert 0x0D before standalone 0x0A bytes (not already CRLF)
    - We distribute insertions evenly across the region's candidate positions
    - Finally, update the layer offset table in the header
    """
    f = info['data']
    use64 = info['use64']
    
    # Build ordered shift points from repair map
    shift_points = []  # (actual_pos_in_file, stored_offset, cumulative_shift)
    for stored_off, actual_off, shift, name in repair_map:
        if actual_off is not None and shift is not None:
            shift_points.append((actual_off, stored_off, shift))
    
    shift_points.sort(key=lambda x: x[0])
    
    # Add file end as final boundary
    shift_points.append((len(f), len(f), shift_points[-1][2] if shift_points else 0))
    
    print(f"\n=== Repair Plan ===")
    
    # Process file region by region
    result = bytearray()
    parasite_set = set()
    for ps, pe in info['parasite_ranges']:
        for i in range(ps, pe):
            parasite_set.add(i)
    
    prev_shift = 0
    prev_pos = 0
    
    for actual_pos, stored_off, cum_shift in shift_points:
        region_start = prev_pos
        region_end = actual_pos
        needed = cum_shift - prev_shift
        
        if region_end <= region_start:
            prev_shift = cum_shift
            prev_pos = actual_pos
            continue
        
        region = f[region_start:region_end]
        
        if needed <= 0:
            result.extend(region)
            prev_shift = cum_shift
            prev_pos = actual_pos
            continue
        
        # Find standalone 0x0A positions in this region (not in text, not already CRLF)
        candidates = []
        for j in range(len(region)):
            if region[j] == 0x0A:
                abs_pos = region_start + j
                # Skip if in parasite (text) range
                if abs_pos in parasite_set:
                    continue
                # Skip if already preceded by 0x0D (CRLF)
                if j > 0 and region[j-1] == 0x0D:
                    continue
                candidates.append(j)
        
        print(f"  Region 0x{region_start:X}–0x{region_end:X} ({len(region)} bytes): "
              f"need {needed} insertions, {len(candidates)} candidates")
        
        if len(candidates) < needed:
            print(f"  WARNING: Only {len(candidates)} candidates for {needed} insertions!")
            # Insert what we can
            needed = len(candidates)
        
        # Distribute insertions evenly across candidates
        # This gives the best chance of correct placement since we don't know
        # which specific 0x0A bytes lost their 0x0D
        if needed > 0 and len(candidates) > 0:
            step = len(candidates) / needed
            selected = set()
            for i in range(needed):
                idx = int(i * step)
                if idx >= len(candidates):
                    idx = len(candidates) - 1
                selected.add(candidates[idx])
        else:
            selected = set()
        
        # Build repaired region
        for j in range(len(region)):
            if j in selected:
                result.append(0x0D)
            result.append(region[j])
        
        prev_shift = cum_shift
        prev_pos = actual_pos
    
    # Now we need to update the layer offset table
    # The header up to and including the offset table is in the first part of the file
    # Since layers 0-5 had shift=0, the header region is unaffected
    # But the offsets for layers 6+ need to be updated to their stored values
    # (because we re-inserted the missing bytes, the layers are now at their original offsets)
    
    # Verify: check if the stored offsets now point to valid data in the repaired file
    print(f"\nRepaired file size: {len(result)} (original: {len(f)}, added: {len(result) - len(f)} bytes)")
    
    # Validate before writing
    print("\n=== Validating repaired file ===")
    good = validate_xcf(bytes(result), info)
    
    with open(output_path, 'wb') as fh:
        fh.write(result)
    print(f"\nWritten to: {output_path}")
    
    return True


def validate_xcf(data, original_info):
    """Quick validation of repaired XCF structure."""
    f = bytes(data)
    use64 = original_info['use64']
    
    pos = 14
    width, pos = read_u32(f, pos)
    height, pos = read_u32(f, pos)
    _, pos = read_u32(f, pos)
    if use64:
        _, pos = read_u32(f, pos)
    
    # Skip properties
    while pos < len(f) - 8:
        ptype, pos = read_u32(f, pos)
        psize, pos = read_u32(f, pos)
        if ptype == 0:
            break
        pos += psize
    
    # Read layer offsets
    offsets = []
    if use64:
        while pos < len(f) - 8:
            off, pos = read_u64(f, pos)
            if off == 0:
                break
            offsets.append(off)
    
    all_ok = True
    print(f"  {len(offsets)} layers found")
    for i, off in enumerate(offsets):
        if off >= len(f):
            print(f"  Layer {i}: offset 0x{off:X} BEYOND FILE END")
            all_ok = False
            continue
        try:
            w, _ = read_u32(f, off)
            h, _ = read_u32(f, off + 4)
            t, _ = read_u32(f, off + 8)
            nlen, _ = read_u32(f, off + 12)
            valid = (0 < w <= 100000 and 0 < h <= 100000 and t <= 6 and 0 < nlen < 10000)
            if valid:
                name = f[off+16:off+16+nlen-1].decode('utf-8', errors='replace')
                print(f"  Layer {i}: \"{name}\" ({w}x{h}) - OK")
            else:
                print(f"  Layer {i}: INVALID (w={w} h={h} t={t} nlen={nlen})")
                all_ok = False
        except Exception as e:
            print(f"  Layer {i}: ERROR - {e}")
            all_ok = False
    
    return all_ok


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <input.xcf> [output.xcf]")
        print(f"  Phase 1: Analyze only (no output path)")
        print(f"  Phase 2: Analyze + repair (with output path)")
        sys.exit(1)
    
    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else None
    
    # Phase 1: Analyze
    info = analyze_xcf(input_path)
    
    # Phase 2: Find all layer positions
    candidates = find_all_layer_positions(info)
    
    # Phase 3: Compute repair map
    repair_map = compute_repair_map(info, candidates)
    
    # Phase 4: Repair (if output path given)
    if output_path:
        repair_xcf(info, repair_map, output_path)
    else:
        print("\nNo output path given. Run with output path to attempt repair.")


if __name__ == '__main__':
    main()

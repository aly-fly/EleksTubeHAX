#!/usr/bin/env python3
"""
Compare two XCF files layer-by-layer to identify changes.
Handles the case where one file is corrupted (shifted offsets) by
scanning for layer headers instead of trusting the offset table.

Compares:
- Layer names, dimensions, types
- Layer properties (visibility, opacity, offsets, etc.)
- Actual pixel data (tile-by-tile comparison)
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

def s32(d, pos):
    return struct.unpack('>i', d[pos:pos+4])[0]


def skip_properties(d, pos):
    """Skip properties, return position after END marker."""
    while pos < len(d) - 8:
        pt = u32(d, pos); pos += 4
        ps = u32(d, pos); pos += 4
        if pt == 0:
            break
        pos += ps
    return pos


def read_properties(d, pos):
    """Read properties into a dict, return (props_dict, pos_after)."""
    props = {}
    while pos < len(d) - 8:
        pt = u32(d, pos); pos += 4
        ps = u32(d, pos); pos += 4
        if pt == 0:
            break
        prop_data = bytes(d[pos:pos+ps])
        props[pt] = prop_data
        pos += ps
    return props, pos


PROP_NAMES = {
    6: "OPACITY", 7: "MODE", 8: "VISIBLE", 11: "LINKED",
    15: "OFFSETS", 20: "COLOR_TAG", 21: "PARASITES",
    28: "LOCK_CONTENT", 32: "LOCK_POSITION",
    33: "COMPOSITE_MODE", 34: "COMPOSITE_SPACE",
    35: "BLEND_SPACE",
}


def find_all_layers(d):
    """Scan file for valid XCF layer headers."""
    results = []
    for pos in range(0, len(d) - 20):
        w = u32(d, pos)
        h = u32(d, pos+4)
        t = u32(d, pos+8)
        nlen = u32(d, pos+12)
        if not (0 < w <= 65536 and 0 < h <= 65536 and t <= 6 and 0 < nlen < 500):
            continue
        if pos + 16 + nlen > len(d):
            continue
        name_raw = d[pos+16:pos+16+nlen-1]
        try:
            name_str = name_raw.decode('utf-8')
            if all(c.isprintable() or c in '\n\r\t' for c in name_str) and len(name_str) > 0:
                results.append((pos, w, h, t, name_str))
        except:
            continue
    return results


def parse_layer_full(d, pos, name_hint=""):
    """Parse a layer at the given position, return structured info."""
    info = {}
    info['pos'] = pos
    info['width'] = u32(d, pos)
    info['height'] = u32(d, pos+4)
    info['type'] = u32(d, pos+8)
    nlen = u32(d, pos+12)
    info['name'] = d[pos+16:pos+16+nlen-1].decode('utf-8', errors='replace')

    p = pos + 16 + nlen
    props, p = read_properties(d, p)
    info['properties'] = props

    # Decode common properties
    if 6 in props and len(props[6]) >= 4:
        info['opacity'] = struct.unpack('>I', props[6][:4])[0]
    if 8 in props and len(props[8]) >= 4:
        info['visible'] = struct.unpack('>I', props[8][:4])[0]
    if 15 in props and len(props[15]) >= 8:
        info['offset_x'] = struct.unpack('>i', props[15][:4])[0]
        info['offset_y'] = struct.unpack('>i', props[15][4:8])[0]
    if 7 in props and len(props[7]) >= 4:
        info['mode'] = struct.unpack('>I', props[7][:4])[0]

    # Hierarchy and mask offsets
    if p + 16 <= len(d):
        info['hier_off'] = u64(d, p)
        info['mask_off'] = u64(d, p+8)
    else:
        info['hier_off'] = 0
        info['mask_off'] = 0

    return info


def find_hierarchy(d, stored_off, w, h, search_range=10000):
    """Find actual hierarchy position near stored offset."""
    for delta in range(0, search_range):
        for try_off in [stored_off - delta, stored_off + delta]:
            if try_off < 0 or try_off + 12 >= len(d):
                continue
            hw = u32(d, try_off)
            hh = u32(d, try_off+4)
            hbpp = u32(d, try_off+8)
            if hw == w and hh == h and hbpp in (1, 2, 3, 4):
                return try_off, hbpp
    return None, None


def get_tile_data(d, tile_offsets):
    """Extract raw tile data between consecutive offsets."""
    tiles = []
    for i in range(len(tile_offsets)):
        start = tile_offsets[i]
        if i + 1 < len(tile_offsets):
            end = tile_offsets[i+1]
        else:
            # Last tile - read up to 64*64*4 bytes max
            end = min(start + 64*64*4 + 1000, len(d))
        if 0 < start < len(d) and 0 < end <= len(d) and end > start:
            tiles.append(bytes(d[start:end]))
        else:
            tiles.append(None)
    return tiles


def get_layer_tiles(d, layer_info, search=False):
    """Get level 0 tile offsets and data for a layer."""
    w = layer_info['width']
    h = layer_info['height']
    hier_off = layer_info.get('hier_off', 0)

    if hier_off == 0 or hier_off >= len(d):
        return None

    # Find actual hierarchy
    if search:
        actual_hier, bpp = find_hierarchy(d, hier_off, w, h)
    else:
        # Trust the offset
        if hier_off + 12 >= len(d):
            return None
        hw = u32(d, hier_off)
        hh = u32(d, hier_off+4)
        hbpp = u32(d, hier_off+8)
        if hw == w and hh == h and hbpp in (1, 2, 3, 4):
            actual_hier, bpp = hier_off, hbpp
        else:
            actual_hier, bpp = find_hierarchy(d, hier_off, w, h)

    if actual_hier is None:
        return None

    # Level 0 offset
    hp = actual_hier + 12
    if hp + 8 > len(d):
        return None
    lvl0_off = u64(d, hp)

    if lvl0_off == 0 or lvl0_off + 8 >= len(d):
        return None

    # Verify level 0
    l0w = u32(d, lvl0_off)
    l0h = u32(d, lvl0_off+4)

    if l0w != w or l0h != h:
        # Search for it
        for delta in range(0, 5000):
            for try_off in [lvl0_off - delta, lvl0_off + delta]:
                if 0 < try_off < len(d) - 8:
                    if u32(d, try_off) == w and u32(d, try_off+4) == h:
                        lvl0_off = try_off
                        l0w, l0h = w, h
                        break
            if l0w == w and l0h == h:
                break

    if l0w != w or l0h != h:
        return None

    # Read tile offsets
    tp = lvl0_off + 8
    tile_offsets = []
    while tp < len(d) - 8:
        to = u64(d, tp); tp += 8
        if to == 0:
            break
        tile_offsets.append(to)

    return {
        'bpp': bpp,
        'hier_off': actual_hier,
        'lvl0_off': lvl0_off,
        'tile_offsets': tile_offsets,
    }


def compare_xcf(clean_path, corrupted_path):
    with open(clean_path, 'rb') as f:
        clean = f.read()
    with open(corrupted_path, 'rb') as f:
        corrupt = f.read()

    print(f"Clean:     {clean_path} ({len(clean)} bytes)")
    print(f"Corrupted: {corrupted_path} ({len(corrupt)} bytes)")
    print(f"Size diff: {len(corrupt) - len(clean)} bytes")

    # Parse clean file using offset table (trusted)
    pos = 14
    c_width = u32(clean, pos); pos += 4
    c_height = u32(clean, pos); pos += 4
    pos += 4  # base_type
    ver = int(clean[9:13].replace(b'v', b'').replace(b'\x00', b'').decode())
    if ver >= 4:
        pos += 4  # precision
    pos = skip_properties(clean, pos)

    clean_layer_offsets = []
    while pos < len(clean) - 8:
        off = u64(clean, pos); pos += 8
        if off == 0:
            break
        clean_layer_offsets.append(off)

    print(f"\nClean file: {len(clean_layer_offsets)} layers")
    clean_layers = []
    for i, off in enumerate(clean_layer_offsets):
        info = parse_layer_full(clean, off)
        clean_layers.append(info)
        vis = info.get('visible', '?')
        ox = info.get('offset_x', 0)
        oy = info.get('offset_y', 0)
        print(f"  [{i}] \"{info['name']}\" {info['width']}x{info['height']} "
              f"vis={vis} offset=({ox},{oy})")

    # Parse corrupted file by scanning for layer headers
    print(f"\nScanning corrupted file for layer headers...")
    corrupt_candidates = find_all_layers(corrupt)
    print(f"Found {len(corrupt_candidates)} candidates:")
    for cpos, w, h, t, name in corrupt_candidates:
        print(f"  0x{cpos:X}: \"{name}\" {w}x{h}")

    # Match layers by name
    print(f"\n{'='*60}")
    print(f"=== Layer-by-layer comparison ===")
    print(f"{'='*60}")

    clean_by_name = {}
    for i, info in enumerate(clean_layers):
        clean_by_name[info['name']] = (i, info)

    corrupt_by_name = {}
    for cpos, w, h, t, name in corrupt_candidates:
        info = parse_layer_full(corrupt, cpos)
        corrupt_by_name[name] = info

    # Check for new layers in corrupted (= layers added after clean version)
    new_layers = []
    for name in corrupt_by_name:
        if name not in clean_by_name:
            new_layers.append(name)

    # Check for removed layers
    removed_layers = []
    for name in clean_by_name:
        if name not in corrupt_by_name:
            removed_layers.append(name)

    if new_layers:
        print(f"\n--- New layers (added after clean version) ---")
        for name in new_layers:
            info = corrupt_by_name[name]
            print(f"  + \"{name}\" {info['width']}x{info['height']}")

    if removed_layers:
        print(f"\n--- Removed layers ---")
        for name in removed_layers:
            idx, info = clean_by_name[name]
            print(f"  - [{idx}] \"{name}\" {info['width']}x{info['height']}")

    # Compare matching layers
    print(f"\n--- Matching layers ---")
    for name in clean_by_name:
        if name not in corrupt_by_name:
            continue

        idx, c_info = clean_by_name[name]
        x_info = corrupt_by_name[name]

        diffs = []

        # Dimension check
        if c_info['width'] != x_info['width'] or c_info['height'] != x_info['height']:
            diffs.append(f"SIZE: {c_info['width']}x{c_info['height']} → "
                        f"{x_info['width']}x{x_info['height']}")

        # Property comparison
        for pt in set(list(c_info['properties'].keys()) + list(x_info['properties'].keys())):
            if pt == 21:  # Skip PARASITES (contains EXIF etc, affected by CR removal)
                continue
            c_val = c_info['properties'].get(pt)
            x_val = x_info['properties'].get(pt)
            pname = PROP_NAMES.get(pt, f"prop_{pt}")

            if c_val is None and x_val is not None:
                diffs.append(f"+{pname}")
            elif c_val is not None and x_val is None:
                diffs.append(f"-{pname}")
            elif c_val != x_val:
                # Decode for readable output
                if pt == 8:  # VISIBLE
                    cv = struct.unpack('>I', c_val[:4])[0] if c_val else '?'
                    xv = struct.unpack('>I', x_val[:4])[0] if x_val else '?'
                    diffs.append(f"VISIBLE: {cv} → {xv}")
                elif pt == 6:  # OPACITY
                    cv = struct.unpack('>I', c_val[:4])[0] if c_val else '?'
                    xv = struct.unpack('>I', x_val[:4])[0] if x_val else '?'
                    diffs.append(f"OPACITY: {cv} → {xv}")
                elif pt == 15:  # OFFSETS
                    if c_val and len(c_val) >= 8:
                        cox = struct.unpack('>i', c_val[:4])[0]
                        coy = struct.unpack('>i', c_val[4:8])[0]
                    else:
                        cox, coy = '?', '?'
                    if x_val and len(x_val) >= 8:
                        xox = struct.unpack('>i', x_val[:4])[0]
                        xoy = struct.unpack('>i', x_val[4:8])[0]
                    else:
                        xox, xoy = '?', '?'
                    diffs.append(f"OFFSETS: ({cox},{coy}) → ({xox},{xoy})")
                else:
                    diffs.append(f"{pname}: changed ({len(c_val or b'')}→{len(x_val or b'')} bytes)")

        # Tile data comparison (pixel data)
        c_tiles = get_layer_tiles(clean, c_info, search=False)
        x_tiles = get_layer_tiles(corrupt, x_info, search=True)

        if c_tiles and x_tiles:
            ct = c_tiles['tile_offsets']
            xt = x_tiles['tile_offsets']

            if len(ct) != len(xt):
                diffs.append(f"TILE_COUNT: {len(ct)} → {len(xt)}")
            else:
                # Compare actual tile data
                c_data = get_tile_data(clean, ct)
                x_data = get_tile_data(corrupt, xt)
                changed_tiles = 0
                for ti in range(len(c_data)):
                    if c_data[ti] != x_data[ti]:
                        changed_tiles += 1
                if changed_tiles > 0:
                    diffs.append(f"PIXELS: {changed_tiles}/{len(ct)} tiles changed")
                else:
                    pass  # pixel data identical
        elif c_tiles and not x_tiles:
            diffs.append("TILES: corrupted (can't read)")
        elif not c_tiles and not x_tiles:
            diffs.append("TILES: both unreadable")

        if diffs:
            print(f"\n  [{idx}] \"{name}\" — CHANGED:")
            for d in diffs:
                print(f"        {d}")
        else:
            print(f"  [{idx}] \"{name}\" — identical")

    # Layer order comparison
    print(f"\n--- Layer order ---")
    # Read corrupted file's offset table
    pos2 = 14
    pos2 += 4 + 4 + 4  # w, h, base_type
    if ver >= 4:
        pos2 += 4
    pos2 = skip_properties(corrupt, pos2)

    corrupt_layer_offsets = []
    while pos2 < len(corrupt) - 8:
        off = u64(corrupt, pos2); pos2 += 8
        if off == 0:
            break
        corrupt_layer_offsets.append(off)

    print(f"  Clean order ({len(clean_layer_offsets)} layers):")
    for i, off in enumerate(clean_layer_offsets):
        info = clean_layers[i]
        print(f"    [{i}] \"{info['name']}\"")

    # Try to determine corrupted order from offset table
    print(f"  Corrupted offset table ({len(corrupt_layer_offsets)} entries):")
    for i, off in enumerate(corrupt_layer_offsets):
        # Find nearest candidate
        best = None
        best_dist = float('inf')
        for cpos, w, h, t, name in corrupt_candidates:
            dist = abs(off - cpos)
            if dist < best_dist:
                best_dist = dist
                best = (cpos, name)
        if best and best_dist < 50000:
            print(f"    [{i}] 0x{off:X} → \"{best[1]}\" (shift={off-best[0]})")
        else:
            print(f"    [{i}] 0x{off:X} → NO MATCH (dist={best_dist})")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <clean.xcf> <corrupted.xcf>")
        sys.exit(1)
    compare_xcf(sys.argv[1], sys.argv[2])

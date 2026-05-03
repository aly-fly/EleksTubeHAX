#!/usr/bin/env python3
"""
CR-aware XCF tile comparison.

Compares a clean XCF file with a corrupted one (CR bytes removed by git).
For each tile pair, determines if the difference is:
  a) Only CR removal (strip_cr(clean_tile) == corrupt_tile)
  b) A real pixel edit

Also decodes RLE tiles to pixel data for visual diff of actual edits.
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


def skip_properties(d, pos):
    while pos < len(d) - 8:
        pt = u32(d, pos); pos += 4
        ps = u32(d, pos); pos += 4
        if pt == 0:
            break
        pos += ps
    return pos


def read_properties(d, pos):
    props = {}
    while pos < len(d) - 8:
        pt = u32(d, pos); pos += 4
        ps = u32(d, pos); pos += 4
        if pt == 0:
            break
        props[pt] = bytes(d[pos:pos+ps])
        pos += ps
    return props, pos


def strip_cr(data):
    """Remove all CR before LF — same transform git applied."""
    result = bytearray()
    i = 0
    while i < len(data):
        if i + 1 < len(data) and data[i] == 0x0D and data[i+1] == 0x0A:
            i += 1  # skip CR, LF will be added by next iteration
        else:
            result.append(data[i])
            i += 1
    return bytes(result)


def find_hierarchy(d, stored_off, w, h, search_range=10000):
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


def find_level(d, stored_off, w, h, search_range=10000):
    for delta in range(0, search_range):
        for try_off in [stored_off - delta, stored_off + delta]:
            if try_off < 0 or try_off + 8 >= len(d):
                continue
            if u32(d, try_off) == w and u32(d, try_off+4) == h:
                return try_off
    return None


def get_layer_info(d, pos):
    """Parse layer at pos, return dict with structure info."""
    try:
        w = u32(d, pos)
        h = u32(d, pos+4)
        lt = u32(d, pos+8)
        nlen = u32(d, pos+12)
        name = d[pos+16:pos+16+nlen-1].decode('utf-8', errors='replace')

        p = pos + 16 + nlen
        props, p = read_properties(d, p)

        if p + 16 > len(d):
            return {
                'pos': pos, 'width': w, 'height': h, 'type': lt, 'name': name,
                'properties': props, 'hier_off': 0, 'mask_off': 0
            }

        hier_off = u64(d, p)
        mask_off = u64(d, p+8)

        return {
            'pos': pos, 'width': w, 'height': h, 'type': lt, 'name': name,
            'properties': props, 'hier_off': hier_off, 'mask_off': mask_off
        }
    except Exception:
        return {
            'pos': pos, 'width': 0, 'height': 0, 'type': 0, 'name': '???',
            'properties': {}, 'hier_off': 0, 'mask_off': 0
        }


def get_tile_offsets(d, layer_info, search=False):
    """Get level 0 tile offsets for a layer.
    If search=True, search near stored offsets for actual structure positions.
    """
    w = layer_info['width']
    h = layer_info['height']
    hier_off = layer_info['hier_off']

    if hier_off == 0 or hier_off >= len(d):
        return None, None

    if search:
        actual_hier, bpp = find_hierarchy(d, hier_off, w, h)
    else:
        actual_hier = hier_off
        bpp = u32(d, hier_off + 8) if hier_off + 12 < len(d) else None
        if bpp not in (1, 2, 3, 4):
            actual_hier, bpp = find_hierarchy(d, hier_off, w, h)

    if actual_hier is None:
        return None, None

    # Level 0 offset
    hp = actual_hier + 12
    lvl0_off = u64(d, hp)
    if lvl0_off == 0:
        return None, bpp

    if search:
        actual_lvl = find_level(d, lvl0_off, w, h)
    else:
        actual_lvl = lvl0_off
        if u32(d, lvl0_off) != w or u32(d, lvl0_off+4) != h:
            actual_lvl = find_level(d, lvl0_off, w, h)

    if actual_lvl is None:
        return None, bpp

    # Read tile offsets
    tp = actual_lvl + 8
    offsets = []
    while tp < len(d) - 8:
        to = u64(d, tp); tp += 8
        if to == 0:
            break
        offsets.append(to)

    return offsets, bpp


def get_tile_data(d, tile_offsets, max_tile_size=64*64*4+2000):
    """Get raw tile data between consecutive offsets."""
    tiles = []
    for i in range(len(tile_offsets)):
        start = tile_offsets[i]
        if i + 1 < len(tile_offsets):
            end = tile_offsets[i+1]
        else:
            end = min(start + max_tile_size, len(d))
        if 0 < start < len(d) and end <= len(d) and end > start:
            tiles.append(bytes(d[start:end]))
        else:
            tiles.append(None)
    return tiles


def rle_decode_tile(data, width, height, bpp):
    """Decode one RLE-compressed XCF tile. Returns pixels as flat bytes."""
    total = width * height
    channels = []

    pos = 0
    for ch in range(bpp):
        decoded = bytearray()
        while len(decoded) < total and pos < len(data):
            byte = data[pos]; pos += 1

            if byte <= 126:
                # Short run: repeat next byte (byte+1) times
                count = byte + 1
                if pos < len(data):
                    val = data[pos]; pos += 1
                    decoded.extend([val] * count)
                else:
                    break
            elif byte == 127:
                # Long run: 2 bytes count, then value
                if pos + 2 < len(data):
                    count = (data[pos] << 8) | data[pos+1]; pos += 2
                    val = data[pos]; pos += 1
                    decoded.extend([val] * count)
                else:
                    break
            elif byte == 128:
                # Long raw: 2 bytes count, then that many bytes
                if pos + 1 < len(data):
                    count = (data[pos] << 8) | data[pos+1]; pos += 2
                    decoded.extend(data[pos:pos+count])
                    pos += count
                else:
                    break
            else:
                # Short raw: (256-byte) literal bytes
                count = 256 - byte
                decoded.extend(data[pos:pos+count])
                pos += count

        if len(decoded) < total:
            decoded.extend([0] * (total - len(decoded)))
        channels.append(bytes(decoded[:total]))

    return channels


def compare_layers(clean, corrupt, clean_layer, corrupt_layer, layer_name):
    """Compare tiles of matching layers, distinguishing CR-only from real edits."""

    w = clean_layer['width']
    h = clean_layer['height']

    # Get tile offsets from clean (trusted) and corrupted (search near stored)
    c_offsets, c_bpp = get_tile_offsets(clean, clean_layer, search=False)
    x_offsets, x_bpp = get_tile_offsets(corrupt, corrupt_layer, search=True)

    if c_offsets is None:
        print(f"    Cannot read clean tiles")
        return
    if x_offsets is None:
        print(f"    Cannot read corrupted tiles")
        return

    if len(c_offsets) != len(x_offsets):
        print(f"    Tile count mismatch: {len(c_offsets)} vs {len(x_offsets)}")
        return

    c_tiles = get_tile_data(clean, c_offsets)
    x_tiles = get_tile_data(corrupt, x_offsets)

    tiles_x = (w + 63) // 64
    tiles_y = (h + 63) // 64

    identical = 0
    cr_only = 0
    real_edit = 0
    unreadable = 0
    real_edit_coords = []

    for ti in range(len(c_tiles)):
        ct = c_tiles[ti]
        xt = x_tiles[ti]

        if ct is None or xt is None:
            unreadable += 1
            continue

        if ct == xt:
            identical += 1
            continue

        # Check if difference is ONLY CR removal
        ct_stripped = strip_cr(ct)
        if ct_stripped == xt:
            cr_only += 1
            continue

        # Real difference - but could be CR removal + real edit
        # Try decomposing: does stripping CR make them closer?
        size_diff_before = len(ct) - len(xt)
        size_diff_after = len(ct_stripped) - len(xt)

        # Calculate tile coordinates
        tx = ti % tiles_x
        ty = ti // tiles_x
        px = tx * 64
        py = ty * 64

        real_edit += 1
        real_edit_coords.append((ti, tx, ty, px, py, size_diff_before, size_diff_after))

    print(f"    Tiles: {len(c_tiles)} total")
    print(f"      Identical:    {identical}")
    print(f"      CR-only diff: {cr_only}")
    print(f"      Real edits:   {real_edit}")
    if unreadable:
        print(f"      Unreadable:   {unreadable}")

    if real_edit_coords:
        print(f"    Edited tile positions:")
        for ti, tx, ty, px, py, sdb, sda in real_edit_coords:
            print(f"      tile[{ti}] grid({tx},{ty}) → pixel({px},{py})-({px+63},{py+63}) "
                  f"size_diff={sdb} after_strip={sda}")

    return {
        'identical': identical,
        'cr_only': cr_only,
        'real_edit': real_edit,
        'real_edit_coords': real_edit_coords,
    }


def find_all_layers(d):
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


def main(clean_path, corrupt_path):
    with open(clean_path, 'rb') as f:
        clean = f.read()
    with open(corrupt_path, 'rb') as f:
        corrupt = f.read()

    print(f"Clean:     {clean_path} ({len(clean)} bytes)")
    print(f"Corrupted: {corrupt_path} ({len(corrupt)} bytes)")
    print(f"Removed CRs total (approx): {len(corrupt) - len(clean) + (len(corrupt) - len(clean))}")
    # Actually: clean_orig_size = corrupt_size + removed_crs
    # But corrupted was EDITED (larger), so we can't just subtract.

    # Parse clean file
    pos = 14
    pos += 4 + 4 + 4  # width, height, base_type
    ver = int(clean[9:13].replace(b'v', b'').replace(b'\x00', b'').decode())
    if ver >= 4:
        pos += 4
    pos = skip_properties(clean, pos)

    clean_offsets = []
    while pos < len(clean) - 8:
        off = u64(clean, pos); pos += 8
        if off == 0:
            break
        clean_offsets.append(off)

    clean_layers = {}
    for off in clean_offsets:
        info = get_layer_info(clean, off)
        clean_layers[info['name']] = info

    # Parse corrupted file - scan for layer headers
    corrupt_candidates = find_all_layers(corrupt)
    corrupt_layers = {}
    for cpos, w, h, t, name in corrupt_candidates:
        # If name appears multiple times, use first occurrence
        if name not in corrupt_layers:
            corrupt_layers[name] = get_layer_info(corrupt, cpos)

    # Compare matching layers
    print(f"\n{'='*60}")
    print(f"CR-aware tile comparison")
    print(f"{'='*60}")

    for name in clean_layers:
        if name not in corrupt_layers:
            print(f"\n  \"{name}\": MISSING in corrupted file")
            continue

        c_info = clean_layers[name]
        x_info = corrupt_layers[name]

        print(f"\n  \"{name}\" ({c_info['width']}x{c_info['height']}):")

        if c_info['width'] != x_info['width'] or c_info['height'] != x_info['height']:
            print(f"    Dimension mismatch: {c_info['width']}x{c_info['height']} vs "
                  f"{x_info['width']}x{x_info['height']}")
            continue

        result = compare_layers(clean, corrupt, c_info, x_info, name)

    # Show new layers
    for name in corrupt_layers:
        if name not in clean_layers:
            info = corrupt_layers[name]
            print(f"\n  + NEW: \"{name}\" ({info['width']}x{info['height']})")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <clean.xcf> <corrupted.xcf>")
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])

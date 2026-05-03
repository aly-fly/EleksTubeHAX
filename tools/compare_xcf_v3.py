#!/usr/bin/env python3
"""
CR-adjusted XCF tile comparison (v3) - Sequential RLE walking.

Parses BOTH files independently. For the corrupted file, uses the RLE tile decoder
to walk through tiles sequentially from the found level structure, bypassing the
broken stored tile offsets.

For each tile pair, determines:
  a) Identical (byte-for-byte match)
  b) CR-only (strip_cr(clean) == corrupt)
  c) Real pixel edit
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
            i += 1  # skip CR
        else:
            result.append(data[i])
            i += 1
    return bytes(result)


def get_layer_info(d, pos):
    """Parse layer at pos."""
    w = u32(d, pos)
    h = u32(d, pos+4)
    lt = u32(d, pos+8)
    nlen = u32(d, pos+12)
    name = d[pos+16:pos+16+nlen-1].decode('utf-8', errors='replace')
    p = pos + 16 + nlen
    props, p = read_properties(d, p)
    hier_off = u64(d, p) if p + 16 <= len(d) else 0
    mask_off = u64(d, p+8) if p + 16 <= len(d) else 0
    return {
        'pos': pos, 'width': w, 'height': h, 'type': lt, 'name': name,
        'properties': props, 'hier_off': hier_off, 'mask_off': mask_off
    }


def find_hierarchy(d, stored_off, w, h, search_range=50000):
    """Search near stored offset for a valid hierarchy structure."""
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


def find_level(d, stored_off, w, h, search_range=50000):
    """Search near stored offset for a valid level structure."""
    for delta in range(0, search_range):
        for try_off in [stored_off - delta, stored_off + delta]:
            if try_off < 0 or try_off + 8 >= len(d):
                continue
            if u32(d, try_off) == w and u32(d, try_off+4) == h:
                return try_off
    return None


def read_tile_offset_table(d, level_pos):
    """Read tile offset table from level structure. Returns (offsets, table_end_pos)."""
    tp = level_pos + 8  # skip width, height
    offsets = []
    while tp < len(d) - 8:
        to = u64(d, tp); tp += 8
        if to == 0:
            break
        offsets.append(to)
    return offsets, tp


def rle_consume_tile(data, pos, width, height, bpp):
    """Walk RLE data starting at pos, consuming exactly one tile.
    Returns (decoded_channels, end_pos) or (None, pos) on failure."""
    total = width * height
    channels = []
    orig_pos = pos

    for ch in range(bpp):
        decoded = bytearray()
        while len(decoded) < total and pos < len(data):
            byte = data[pos]; pos += 1
            if byte <= 126:
                count = byte + 1
                if pos < len(data):
                    val = data[pos]; pos += 1
                    decoded.extend([val] * count)
                else:
                    return None, orig_pos
            elif byte == 127:
                if pos + 2 < len(data):
                    count = (data[pos] << 8) | data[pos+1]; pos += 2
                    val = data[pos]; pos += 1
                    decoded.extend([val] * count)
                else:
                    return None, orig_pos
            elif byte == 128:
                if pos + 1 < len(data):
                    count = (data[pos] << 8) | data[pos+1]; pos += 2
                    decoded.extend(data[pos:pos+count])
                    pos += count
                else:
                    return None, orig_pos
            else:
                count = 256 - byte
                decoded.extend(data[pos:pos+count])
                pos += count

        if len(decoded) < total:
            # Pad if short
            decoded.extend([0] * (total - len(decoded)))
        channels.append(bytes(decoded[:total]))

    return channels, pos


def get_tiles_via_offsets(d, layer_info):
    """Standard approach: read tiles using offset table directly.
    Works for regions with shift=0."""
    w = layer_info['width']
    h = layer_info['height']
    hier_off = layer_info['hier_off']
    if hier_off == 0 or hier_off + 12 >= len(d):
        return None, None, None

    bpp = u32(d, hier_off + 8)
    if bpp not in (1, 2, 3, 4):
        return None, None, None

    lvl0_off = u64(d, hier_off + 12)
    if lvl0_off == 0 or lvl0_off + 8 >= len(d):
        return None, bpp, None

    if u32(d, lvl0_off) != w or u32(d, lvl0_off+4) != h:
        return None, bpp, None

    offsets, _ = read_tile_offset_table(d, lvl0_off)

    tiles = []
    for i in range(len(offsets)):
        start = offsets[i]
        if i + 1 < len(offsets):
            end = offsets[i+1]
        else:
            end = min(start + 64*64*4+4000, len(d))
        if 0 < start < len(d) and end <= len(d) and end > start:
            tiles.append(bytes(d[start:end]))
        else:
            tiles.append(None)

    return tiles, bpp, offsets


def get_tiles_via_rle_walk(d, layer_info, search_range=50000):
    """Walk tiles sequentially using RLE decoder.
    Finds hierarchy/level by searching near stored offsets, then walks tiles."""
    w = layer_info['width']
    h = layer_info['height']
    hier_off = layer_info['hier_off']

    if hier_off == 0 or hier_off >= len(d):
        return None, None

    actual_hier, bpp = find_hierarchy(d, hier_off, w, h, search_range)
    if actual_hier is None:
        return None, None

    lvl0_stored = u64(d, actual_hier + 12)
    if lvl0_stored == 0:
        return None, bpp

    actual_lvl = find_level(d, lvl0_stored, w, h, search_range)
    if actual_lvl is None:
        return None, bpp

    # Read tile offset table from found level
    offsets, table_end = read_tile_offset_table(d, actual_lvl)
    num_tiles = len(offsets)
    if num_tiles == 0:
        return None, bpp

    # The tile data should start right after the offset table
    # OR at the minimum stored offset (which in the original file points here)
    # In the corrupted file, tile_data_start is right after the table
    # because tiles are stored consecutively after the level structure.
    #
    # Verify: the first stored offset should be close to (actual_lvl + 8 + num_tiles*8 + 8)
    expected_tile_start = actual_lvl + 8 + num_tiles * 8 + 8
    # But stored offsets may point elsewhere. Check if they're near expected_tile_start.
    first_stored = offsets[0]
    # The shift = first_stored - expected_tile_start (approx)

    # Use expected_tile_start as the actual start of tile data
    tile_data_pos = expected_tile_start

    tiles_x = (w + 63) // 64

    tiles = []
    for ti in range(num_tiles):
        tx = ti % tiles_x
        ty = ti // tiles_x
        tw = min(64, w - tx * 64)
        th = min(64, h - ty * 64)

        tile_start = tile_data_pos
        channels, tile_data_pos = rle_consume_tile(d, tile_data_pos, tw, th, bpp)
        if channels is None:
            # RLE decode failed - CR corruption in the stream
            # Try reading a few more bytes in case a CR was removed
            tiles.append(None)
            # Attempt recovery: try advancing 1 byte and retrying
            # (simple heuristic for single CR removal)
            recovered = False
            for skip in range(1, 5):
                channels2, pos2 = rle_consume_tile(d, tile_start + skip, tw, th, bpp)
                if channels2 is not None:
                    tile_data_pos = pos2
                    tiles[-1] = bytes(d[tile_start:pos2])
                    recovered = True
                    break
            if not recovered:
                # Give up on this and subsequent tiles
                while len(tiles) < num_tiles:
                    tiles.append(None)
                break
        else:
            tiles.append(bytes(d[tile_start:tile_data_pos]))

    return tiles, bpp


def find_all_layers(d):
    """Scan for layer headers in the file."""
    results = []
    pos = 0
    while pos < len(d) - 20:
        w = u32(d, pos)
        h = u32(d, pos+4)
        t = u32(d, pos+8)
        nlen = u32(d, pos+12)
        if (0 < w <= 65536 and 0 < h <= 65536 and t <= 6 and 0 < nlen < 500
                and pos + 16 + nlen <= len(d)):
            name_raw = d[pos+16:pos+16+nlen-1]
            try:
                name_str = name_raw.decode('utf-8')
                if all(c.isprintable() or c in '\n\r\t' for c in name_str) and len(name_str) > 0:
                    results.append((pos, w, h, t, name_str))
                    pos += 16 + nlen  # skip past this header
                    continue
            except:
                pass
        pos += 1
    return results


def compare_tile_pixels(c_channels, x_channels, tw, th, bpp):
    """Compare decoded pixel channels. Return count of differing pixels."""
    diff_count = 0
    for y in range(th):
        for x in range(tw):
            idx = y * tw + x
            for ch in range(bpp):
                if c_channels[ch][idx] != x_channels[ch][idx]:
                    diff_count += 1
                    break
    return diff_count


def main(clean_path, corrupt_path):
    print("Loading files...", flush=True)
    with open(clean_path, 'rb') as f:
        clean = f.read()
    with open(corrupt_path, 'rb') as f:
        corrupt = f.read()

    print(f"Clean:     {clean_path} ({len(clean)} bytes)")
    print(f"Corrupted: {corrupt_path} ({len(corrupt)} bytes)")

    # === Parse CLEAN file ===
    print("\nParsing clean file...", flush=True)
    pos = 14
    pos += 4 + 4 + 4  # w, h, base_type
    ver_str = clean[9:13].replace(b'v', b'').replace(b'\x00', b'')
    ver = int(ver_str.decode())
    if ver >= 4:
        pos += 4
    pos = skip_properties(clean, pos)

    clean_layer_offsets = []
    while pos < len(clean) - 8:
        off = u64(clean, pos); pos += 8
        if off == 0:
            break
        clean_layer_offsets.append(off)

    clean_layers = []
    for off in clean_layer_offsets:
        info = get_layer_info(clean, off)
        tiles, bpp, tile_offs = get_tiles_via_offsets(clean, info)
        info['tiles'] = tiles
        info['bpp'] = bpp
        info['tile_offsets_raw'] = tile_offs
        clean_layers.append(info)
        print(f"  Clean: \"{info['name']}\" ({info['width']}x{info['height']}) "
              f"tiles={len(tiles) if tiles else 0}, bpp={bpp}")

    # === Parse CORRUPTED file - scan for layer headers ===
    print("\nScanning corrupted file for layer headers...", flush=True)
    corrupt_candidates = find_all_layers(corrupt)
    corrupt_layer_map = {}
    for cpos, w, h, t, name in corrupt_candidates:
        if name not in corrupt_layer_map:
            corrupt_layer_map[name] = get_layer_info(corrupt, cpos)
            print(f"  Found: \"{name}\" ({w}x{h}) @ 0x{cpos:X}")

    # === Compare matching layers ===
    print(f"\n{'='*60}")
    print(f"Tile comparison (RLE-walk for corrupted file)")
    print(f"{'='*60}")

    for cl in clean_layers:
        name = cl['name']
        w = cl['width']
        h = cl['height']
        c_tiles = cl['tiles']
        c_bpp = cl['bpp']

        if name not in corrupt_layer_map:
            print(f"\n  \"{name}\": NOT FOUND in corrupted file")
            continue

        xl = corrupt_layer_map[name]
        if xl['width'] != w or xl['height'] != h:
            print(f"\n  \"{name}\": Dimension mismatch {w}x{h} vs {xl['width']}x{xl['height']}")
            continue

        print(f"\n  \"{name}\" ({w}x{h}):", flush=True)

        if c_tiles is None:
            print(f"    Cannot read clean tiles")
            continue

        # Try direct offset reading first (works for shift=0 layers)
        x_tiles_direct, x_bpp_direct, _ = get_tiles_via_offsets(corrupt, xl)

        # Check if direct approach gives reasonable results
        use_direct = False
        if x_tiles_direct and len(x_tiles_direct) == len(c_tiles):
            # Quick test: count identical tiles
            ident_count = sum(1 for i in range(min(10, len(c_tiles)))
                           if c_tiles[i] and x_tiles_direct[i] and c_tiles[i] == x_tiles_direct[i])
            if ident_count > 0:
                use_direct = True
                print(f"    [Direct offset reading OK, {ident_count}/10 first tiles identical]")

        if use_direct:
            x_tiles = x_tiles_direct
            x_bpp = x_bpp_direct
        else:
            # Fall back to RLE walk
            print(f"    [Using RLE sequential walk]", flush=True)
            x_tiles, x_bpp = get_tiles_via_rle_walk(corrupt, xl)

        if x_tiles is None:
            print(f"    Cannot read corrupted tiles (RLE walk also failed)")
            continue

        if len(x_tiles) != len(c_tiles):
            print(f"    Tile count mismatch: clean={len(c_tiles)} corrupt={len(x_tiles)}")
            continue

        tiles_x = (w + 63) // 64

        identical = 0
        cr_only = 0
        real_edit = 0
        unreadable = 0
        real_edit_info = []

        for ti in range(len(c_tiles)):
            ct = c_tiles[ti]
            xt = x_tiles[ti]

            if ct is None or xt is None:
                unreadable += 1
                continue

            if ct == xt:
                identical += 1
                continue

            ct_stripped = strip_cr(ct)
            if ct_stripped == xt:
                cr_only += 1
                continue

            # Real pixel edit
            tx = ti % tiles_x
            ty = ti // tiles_x
            px = tx * 64
            py = ty * 64
            tw = min(64, w - px)
            th = min(64, h - py)

            # Try pixel-level comparison
            pixel_diff_count = None
            c_decoded = rle_consume_tile(clean, cl['tile_offsets_raw'][ti], tw, th, c_bpp)[0] if cl['tile_offsets_raw'] else None
            x_decoded = rle_consume_tile(corrupt, 0, tw, th, x_bpp or c_bpp)[0] if False else None
            # ^ skip pixel comparison for now - just report tile-level diffs

            size_diff = len(ct) - len(xt)
            strip_diff = len(ct_stripped) - len(xt)

            real_edit += 1
            real_edit_info.append((ti, tx, ty, px, py, tw, th, size_diff, strip_diff))

        print(f"    Tiles: {len(c_tiles)} total")
        print(f"      Identical:    {identical}")
        print(f"      CR-only diff: {cr_only}")
        print(f"      Real edits:   {real_edit}")
        if unreadable:
            print(f"      Unreadable:   {unreadable}")

        if real_edit_info:
            # Only print first 50 + summary
            printed = 0
            for ti, tx, ty, px, py, tw, th, sd, ssd in real_edit_info:
                if printed < 50:
                    print(f"      tile[{ti}] grid({tx},{ty}) pixel({px},{py})-({px+tw-1},{py+th-1}) "
                          f"size_diff={sd} strip_diff={ssd}")
                    printed += 1
            if len(real_edit_info) > 50:
                print(f"      ... and {len(real_edit_info) - 50} more tiles")

    # New layers in corrupted
    for name in corrupt_layer_map:
        found = any(cl['name'] == name for cl in clean_layers)
        if not found:
            info = corrupt_layer_map[name]
            print(f"\n  + NEW LAYER: \"{name}\" ({info['width']}x{info['height']})")

    print(f"\n{'='*60}")
    print("Done.")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <clean.xcf> <corrupted.xcf>")
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])

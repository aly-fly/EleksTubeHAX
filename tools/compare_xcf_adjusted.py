#!/usr/bin/env python3
"""
CR-adjusted XCF tile comparison (v2).

Uses the clean file's exact structure and offsets as the authoritative reference.
Adjusts read positions in the corrupted file using a cumulative CR removal map
derived from the clean file (every 0x0D 0x0A pair = one removed CR).

For each tile: clean_offset O -> read corrupted at O - crc(O)
where crc(O) = number of 0x0D 0x0A pairs in clean_data[0:O].
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


def build_cr_map(clean_data):
    """Build sorted list of all 0x0D 0x0A positions in clean file.
    Returns list of positions where 0x0D appears before 0x0A.
    crc(pos) = bisect_left(cr_positions, pos) gives the count of CRs removed before pos.
    """
    cr_positions = []
    i = clean_data.find(b'\r\n', 0)
    while i != -1:
        cr_positions.append(i)
        i = clean_data.find(b'\r\n', i + 1)
    return cr_positions


def crc(cr_positions, pos):
    """Number of CRs removed before position pos in the clean file."""
    return bisect.bisect_left(cr_positions, pos)


def clean_to_corrupt_offset(cr_positions, clean_off):
    """Convert a clean-file offset to the corresponding corrupted-file position."""
    return clean_off - crc(cr_positions, clean_off)


def get_layer_info(d, pos):
    """Parse layer at pos, return dict with structure info."""
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


def get_tile_offsets_from_clean(d, layer_info):
    """Get level 0 tile offsets from the clean (trusted) file."""
    w = layer_info['width']
    h = layer_info['height']
    hier_off = layer_info['hier_off']

    if hier_off == 0 or hier_off >= len(d):
        return None, None

    bpp = u32(d, hier_off + 8)
    if bpp not in (1, 2, 3, 4):
        return None, None

    # Level 0 pointer
    hp = hier_off + 12
    lvl0_off = u64(d, hp)
    if lvl0_off == 0 or lvl0_off >= len(d):
        return None, bpp

    # Verify level 0
    if u32(d, lvl0_off) != w or u32(d, lvl0_off+4) != h:
        return None, bpp

    # Read tile offsets
    tp = lvl0_off + 8
    offsets = []
    while tp < len(d) - 8:
        to = u64(d, tp); tp += 8
        if to == 0:
            break
        offsets.append(to)

    return offsets, bpp


def get_tile_data_at_offsets(d, offsets, max_tile_size=64*64*4+4000):
    """Get raw tile data using the given offsets (already adjusted for the target file)."""
    tiles = []
    for i in range(len(offsets)):
        start = offsets[i]
        if i + 1 < len(offsets):
            end = offsets[i+1]
        else:
            end = min(start + max_tile_size, len(d))
        if 0 < start < len(d) and end <= len(d) and end > start:
            tiles.append(bytes(d[start:end]))
        else:
            tiles.append(None)
    return tiles


def rle_decode_tile(data, width, height, bpp):
    """Decode one RLE-compressed XCF tile. Returns pixel channels or None on error."""
    total = width * height
    channels = []
    pos = 0

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
                    return None
            elif byte == 127:
                if pos + 2 < len(data):
                    count = (data[pos] << 8) | data[pos+1]; pos += 2
                    val = data[pos]; pos += 1
                    decoded.extend([val] * count)
                else:
                    return None
            elif byte == 128:
                if pos + 1 < len(data):
                    count = (data[pos] << 8) | data[pos+1]; pos += 2
                    decoded.extend(data[pos:pos+count])
                    pos += count
                else:
                    return None
            else:
                count = 256 - byte
                decoded.extend(data[pos:pos+count])
                pos += count

        if len(decoded) < total:
            decoded.extend([0] * (total - len(decoded)))
        channels.append(bytes(decoded[:total]))

    return channels


def compare_tile_pixels(clean_channels, corrupt_channels, tw, th, bpp):
    """Compare decoded pixel data. Returns list of (x,y,clean_rgba,corrupt_rgba) diffs."""
    diffs = []
    for y in range(th):
        for x in range(tw):
            idx = y * tw + x
            c_pixel = tuple(clean_channels[ch][idx] for ch in range(bpp))
            x_pixel = tuple(corrupt_channels[ch][idx] for ch in range(bpp))
            if c_pixel != x_pixel:
                diffs.append((x, y, c_pixel, x_pixel))
    return diffs


def main(clean_path, corrupt_path):
    print("Loading files...", flush=True)
    with open(clean_path, 'rb') as f:
        clean = f.read()
    with open(corrupt_path, 'rb') as f:
        corrupt = f.read()

    print(f"Clean:     {clean_path} ({len(clean)} bytes)")
    print(f"Corrupted: {corrupt_path} ({len(corrupt)} bytes)")

    # Build CR removal map from clean file
    print("Building CR position map from clean file...", flush=True)
    cr_positions = build_cr_map(clean)
    total_crs = len(cr_positions)
    print(f"Found {total_crs} 0x0D 0x0A sequences in clean file")
    expected_corrupt_size = len(clean) - total_crs
    # Note: corrupted file may be different size due to actual edits
    print(f"Expected corrupted size (if no edits): {expected_corrupt_size}")
    print(f"Actual corrupted size: {len(corrupt)}")
    edit_size_delta = len(corrupt) - expected_corrupt_size
    print(f"Size delta from edits: {edit_size_delta:+d} bytes")

    # Parse clean file layer table
    pos = 14
    pos += 4 + 4 + 4  # width, height, base_type
    ver_str = clean[9:13].replace(b'v', b'').replace(b'\x00', b'')
    ver = int(ver_str.decode())
    if ver >= 4:
        pos += 4  # precision
    pos = skip_properties(clean, pos)

    clean_layer_offsets = []
    while pos < len(clean) - 8:
        off = u64(clean, pos); pos += 8
        if off == 0:
            break
        clean_layer_offsets.append(off)

    print(f"\nFound {len(clean_layer_offsets)} layers in clean file")

    # Parse each layer from clean file
    layers = []
    for off in clean_layer_offsets:
        info = get_layer_info(clean, off)
        tile_offsets, bpp = get_tile_offsets_from_clean(clean, info)
        info['tile_offsets'] = tile_offsets
        info['bpp'] = bpp
        layers.append(info)
        shift = crc(cr_positions, off)
        print(f"  Layer \"{info['name']}\" ({info['width']}x{info['height']}) "
              f"@ 0x{off:X}, shift={shift}, tiles={len(tile_offsets) if tile_offsets else 0}")

    print(f"\n{'='*60}")
    print(f"CR-adjusted tile comparison")
    print(f"{'='*60}")

    for layer in layers:
        name = layer['name']
        w = layer['width']
        h = layer['height']
        bpp = layer['bpp']
        c_tile_offsets = layer['tile_offsets']

        if c_tile_offsets is None:
            print(f"\n  \"{name}\": Cannot read tiles from clean file")
            continue

        # Build adjusted offsets for corrupted file
        x_tile_offsets = []
        for off in c_tile_offsets:
            x_tile_offsets.append(clean_to_corrupt_offset(cr_positions, off))

        # Read tile data from both files
        c_tiles = get_tile_data_at_offsets(clean, c_tile_offsets)
        x_tiles = get_tile_data_at_offsets(corrupt, x_tile_offsets)

        tiles_x = (w + 63) // 64
        tiles_y = (h + 63) // 64

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

            # Check if difference is only CR removal
            ct_stripped = strip_cr(ct)
            if ct_stripped == xt:
                cr_only += 1
                continue

            # Real pixel edit
            tx = ti % tiles_x
            ty = ti // tiles_x
            px = tx * 64
            py = ty * 64

            # Determine actual tile dimensions (edge tiles may be smaller)
            tw = min(64, w - px)
            th = min(64, h - py)

            # Try RLE decode for pixel-level comparison
            pixel_diff_count = None
            c_decoded = rle_decode_tile(ct, tw, th, bpp)
            x_decoded = rle_decode_tile(xt, tw, th, bpp)
            if c_decoded and x_decoded:
                diffs = compare_tile_pixels(c_decoded, x_decoded, tw, th, bpp)
                pixel_diff_count = len(diffs)

            real_edit += 1
            size_diff = len(ct) - len(xt)
            strip_diff = len(ct_stripped) - len(xt)
            real_edit_info.append((ti, tx, ty, px, py, tw, th, size_diff, strip_diff, pixel_diff_count))

        print(f"\n  \"{name}\" ({w}x{h}):")
        print(f"    Tiles: {len(c_tiles)} total")
        print(f"      Identical:    {identical}")
        print(f"      CR-only diff: {cr_only}")
        print(f"      Real edits:   {real_edit}")
        if unreadable:
            print(f"      Unreadable:   {unreadable}")

        if real_edit_info:
            print(f"    Edited tiles:")
            for ti, tx, ty, px, py, tw, th, sd, ssd, pdc in real_edit_info:
                pdc_str = f", {pdc} pixels changed" if pdc is not None else ", RLE decode failed"
                print(f"      tile[{ti}] grid({tx},{ty}) pixel({px},{py})-({px+tw-1},{py+th-1}) "
                      f"size_diff={sd} strip_diff={ssd}{pdc_str}")

    print(f"\n{'='*60}")
    print("Done.")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <clean.xcf> <corrupted.xcf>")
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])

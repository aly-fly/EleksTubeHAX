#!/usr/bin/env python3
"""
Restore XCF v8 — append clean tile data onto a working v7 base.

Instead of rebuilding the entire XCF (which misses format details like the
v022 effect pointer), this script:
1. Takes the v7 file as-is (correct structure, opens in GIMP)
2. Parses both v7 and clean file to find matching layers/tiles
3. Appends clean tile data at the end of the v7 file
4. Redirects tile offset pointers to the new locations

This preserves ALL structural bytes from v7 exactly.
"""
import struct, sys, os

os.environ.setdefault('PYTHONIOENCODING', 'utf-8')
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def u32(d, pos): return struct.unpack('>I', d[pos:pos+4])[0]
def u64(d, pos): return struct.unpack('>Q', d[pos:pos+8])[0]
def p64(v): return struct.pack('>Q', v)


def skip_properties(d, pos):
    while pos + 8 <= len(d):
        pt = u32(d, pos); ps = u32(d, pos + 4); pos += 8
        if pt == 0: break
        pos += ps
    return pos


def decode_rle_channel(d, pos, npixels):
    start = pos
    decoded = 0
    while decoded < npixels:
        if pos >= len(d): return pos - start, decoded
        n = d[pos]; pos += 1
        if n <= 126:
            if pos >= len(d): return pos - start, decoded
            pos += 1; decoded += n + 1
        elif n == 127:
            if pos + 3 > len(d): return pos - start, decoded
            count = (d[pos] << 8) | d[pos + 1]; pos += 2
            pos += 1; decoded += count
        elif n == 128:
            if pos + 2 > len(d): return pos - start, decoded
            count = (d[pos] << 8) | d[pos + 1]; pos += 2
            if pos + count > len(d): return pos - start, decoded
            pos += count; decoded += count
        else:
            count = 256 - n
            if pos + count > len(d): return pos - start, decoded
            pos += count; decoded += count
    return pos - start, decoded


def try_decode_tile(d, pos, tw, th, bpp):
    npixels = tw * th
    total = 0
    for ch in range(bpp):
        consumed, decoded = decode_rle_channel(d, pos + total, npixels)
        total += consumed
        if decoded != npixels:
            return total, False
    return total, True


def tile_dims(i, w, h, tiles_x):
    tx = i % tiles_x
    ty = i // tiles_x
    return min(64, w - tx * 64), min(64, h - ty * 64)


def parse_layer_tiles(d, layer_off):
    """Parse layer structure and return tile info for level 0."""
    w = u32(d, layer_off)
    h = u32(d, layer_off + 4)
    nlen = u32(d, layer_off + 12)
    name = d[layer_off + 16:layer_off + 16 + nlen - 1].decode('utf-8', errors='replace')

    header_end = layer_off + 16 + nlen
    props_end = skip_properties(d, header_end)

    hier_off = u64(d, props_end)
    if hier_off == 0 or hier_off >= len(d) - 12:
        return None

    bpp = u32(d, hier_off + 8)

    # Find level 0
    hp = hier_off + 12
    levels = []
    while hp + 8 <= len(d):
        lo = u64(d, hp); hp += 8
        if lo == 0: break
        levels.append(lo)

    if not levels:
        return None

    lvl0_off = levels[0]

    # Tile offset table
    tp = lvl0_off + 8
    tile_offsets = []
    tile_table_positions = []
    while tp + 8 <= len(d):
        tile_table_positions.append(tp)
        to = u64(d, tp); tp += 8
        if to == 0: break
        tile_offsets.append(to)

    tiles_x = (w + 63) // 64

    return {
        'name': name, 'w': w, 'h': h, 'bpp': bpp,
        'tiles_x': tiles_x,
        'tile_offsets': tile_offsets,
        'tile_table_positions': tile_table_positions,
    }


def extract_tile_data(d, layer_info):
    """Extract all tile data bytes from a layer."""
    tiles = []
    for ti, toff in enumerate(layer_info['tile_offsets']):
        tw, th = tile_dims(ti, layer_info['w'], layer_info['h'], layer_info['tiles_x'])
        consumed, ok = try_decode_tile(d, toff, tw, th, layer_info['bpp'])
        if ok:
            tiles.append(d[toff:toff + consumed])
        else:
            tiles.append(None)
    return tiles


def main():
    if len(sys.argv) < 3:
        print("Usage: restore_xcf_v8.py <v7-restored.xcf> <clean.xcf> [output.xcf]")
        sys.exit(1)

    v7_path = sys.argv[1]
    clean_path = sys.argv[2]
    output_path = sys.argv[3] if len(sys.argv) > 3 else v7_path.replace('-v7.xcf', '-v8.xcf')

    print(f"V7 restored: {v7_path}")
    print(f"Clean file:  {clean_path}")
    print(f"Output:      {output_path}")

    v7_data = open(v7_path, 'rb').read()
    clean_data = open(clean_path, 'rb').read()
    print(f"V7 size:    {len(v7_data):,} bytes")
    print(f"Clean size: {len(clean_data):,} bytes")

    # Parse v7 layer table
    ver = int(v7_data[9:13].replace(b'v', b'').replace(b'\x00', b'').decode())
    pos = 14 + 4 + 4 + 4
    if ver >= 4: pos += 4
    pos = skip_properties(v7_data, pos)

    v7_layer_offsets = []
    while pos + 8 <= len(v7_data):
        off = u64(v7_data, pos); pos += 8
        if off == 0: break
        v7_layer_offsets.append(off)

    # Parse clean layer table
    c_pos = 14 + 4 + 4 + 4
    if ver >= 4: c_pos += 4
    c_pos = skip_properties(clean_data, c_pos)

    clean_layer_offsets = []
    while c_pos + 8 <= len(clean_data):
        off = u64(clean_data, c_pos); c_pos += 8
        if off == 0: break
        clean_layer_offsets.append(off)

    # Parse all layers
    print(f"\n{len(v7_layer_offsets)} v7 layers, {len(clean_layer_offsets)} clean layers")

    v7_layers = []
    for loff in v7_layer_offsets:
        info = parse_layer_tiles(v7_data, loff)
        v7_layers.append(info)

    clean_layers = {}
    for loff in clean_layer_offsets:
        info = parse_layer_tiles(clean_data, loff)
        if info:
            tiles = extract_tile_data(clean_data, info)
            n_ok = sum(1 for t in tiles if t is not None)
            clean_layers[info['name']] = (info, tiles)
            print(f"  Clean: \"{info['name']}\" {len(tiles)} tiles ({n_ok} valid)")

    # Build output: start with v7 verbatim, append clean tile data
    out = bytearray(v7_data)
    append_pos = len(out)

    total_redirected = 0
    total_kept = 0

    for li, v7_layer in enumerate(v7_layers):
        if v7_layer is None:
            print(f"\n  Layer {li}: SKIP (parse error)")
            continue

        name = v7_layer['name']
        clean_entry = clean_layers.get(name)

        if clean_entry is None:
            n_tiles = len(v7_layer['tile_offsets'])
            print(f"\n  Layer {li} \"{name}\": no clean source, keeping {n_tiles} v7 tiles")
            total_kept += n_tiles
            continue

        clean_info, clean_tiles = clean_entry

        if clean_info['w'] != v7_layer['w'] or clean_info['h'] != v7_layer['h']:
            print(f"\n  Layer {li} \"{name}\": DIMENSION MISMATCH, skipping")
            total_kept += len(v7_layer['tile_offsets'])
            continue

        n_tiles = len(v7_layer['tile_offsets'])
        n_clean = len(clean_tiles)
        if n_tiles != n_clean:
            print(f"\n  Layer {li} \"{name}\": tile count mismatch (v7={n_tiles} clean={n_clean}), skipping")
            total_kept += n_tiles
            continue

        redirected = 0
        kept = 0

        for ti in range(n_tiles):
            clean_tile = clean_tiles[ti] if ti < len(clean_tiles) else None
            if clean_tile is not None:
                new_offset = append_pos
                out.extend(clean_tile)
                append_pos += len(clean_tile)

                tpos = v7_layer['tile_table_positions'][ti]
                out[tpos:tpos + 8] = p64(new_offset)
                redirected += 1
            else:
                kept += 1

        total_redirected += redirected
        total_kept += kept
        print(f"\n  Layer {li} \"{name}\": {redirected} tiles from clean, {kept} kept from v7")

    print(f"\n=== Summary ===")
    print(f"  Tiles redirected to clean data: {total_redirected}")
    print(f"  Tiles kept from v7:             {total_kept}")
    print(f"  V7 base size:  {len(v7_data):,} bytes")
    print(f"  Output size:   {len(out):,} bytes")
    print(f"  Appended data: {len(out) - len(v7_data):,} bytes")

    # Verify all tiles decode
    print(f"\n=== Verification ===")
    out_bytes = bytes(out)
    vpos = 14 + 4 + 4 + 4
    if ver >= 4: vpos += 4
    vpos = skip_properties(out_bytes, vpos)

    out_layer_offsets = []
    while vpos + 8 <= len(out_bytes):
        off = u64(out_bytes, vpos); vpos += 8
        if off == 0: break
        out_layer_offsets.append(off)

    all_ok = True
    for li, loff in enumerate(out_layer_offsets):
        info = parse_layer_tiles(out_bytes, loff)
        if info is None:
            print(f"  Layer {li}: PARSE ERROR")
            all_ok = False
            continue

        n_ok = 0
        n_fail = 0
        for ti, toff in enumerate(info['tile_offsets']):
            tw, th = tile_dims(ti, info['w'], info['h'], info['tiles_x'])
            _, ok = try_decode_tile(out_bytes, toff, tw, th, info['bpp'])
            if ok:
                n_ok += 1
            else:
                n_fail += 1

        status = "OK" if n_fail == 0 else f"FAIL ({n_fail} bad)"
        print(f"  Layer {li} \"{info['name']}\": {n_ok + n_fail} tiles [{status}]")
        if n_fail > 0:
            all_ok = False

    if all_ok:
        print("\n  ALL TILES VALID")

    with open(output_path, 'wb') as f:
        f.write(out)
    print(f"\nSaved: {output_path}")


if __name__ == '__main__':
    main()

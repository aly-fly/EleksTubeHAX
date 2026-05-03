#!/usr/bin/env python3
"""
Restore XCF v8 — merge clean tile data from undamaged file into v7's structure.

Takes the v7-restored file (with correct structure but 459 transparent tiles)
and the clean file (12 layers, perfect tile data). For each layer that exists
in both files, copies ALL tile data from the clean file. For the extra layer
(only in v7), keeps the v7 tile data.

The output is a valid XCF with correct pixel data for 12/13 layers.
"""
import struct, sys, os

os.environ.setdefault('PYTHONIOENCODING', 'utf-8')
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def u32(d, pos): return struct.unpack('>I', d[pos:pos+4])[0]
def u64(d, pos): return struct.unpack('>Q', d[pos:pos+8])[0]
def p32(v): return struct.pack('>I', v)
def p64(v): return struct.pack('>Q', v)


def skip_properties(d, pos):
    """Skip property list, return position after PROP_END."""
    while pos + 8 <= len(d):
        pt = u32(d, pos); ps = u32(d, pos + 4); pos += 8
        if pt == 0:
            break
        pos += ps
    return pos


def read_properties_raw(d, pos):
    """Read raw bytes of properties section (including the PROP_END)."""
    start = pos
    while pos + 8 <= len(d):
        pt = u32(d, pos); ps = u32(d, pos + 4); pos += 8
        if pt == 0:
            break
        pos += ps
    return d[start:pos], pos


def decode_rle_channel(d, pos, npixels):
    """Decode one RLE channel. Returns (bytes_consumed, decoded_pixel_count)."""
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
    """Try to decode a tile. Returns (total_consumed, success)."""
    npixels = tw * th
    total = 0
    for ch in range(bpp):
        consumed, decoded = decode_rle_channel(d, pos + total, npixels)
        total += consumed
        if decoded != npixels:
            return total, False
    return total, True


def tile_dims(i, w, h, tiles_x):
    """Return (tw, th) for tile index i."""
    tx = i % tiles_x
    ty = i // tiles_x
    return min(64, w - tx * 64), min(64, h - ty * 64)


def parse_layer(d, layer_off):
    """Parse a layer and return its structure."""
    w = u32(d, layer_off)
    h = u32(d, layer_off + 4)
    ltype = u32(d, layer_off + 8)
    nlen = u32(d, layer_off + 12)
    name = d[layer_off + 16:layer_off + 16 + nlen - 1].decode('utf-8', errors='replace')

    # Header raw bytes: w(4) + h(4) + type(4) + nlen(4) + name(nlen)
    header_end = layer_off + 16 + nlen

    # Properties
    props_raw, props_end = read_properties_raw(d, header_end)

    # Hierarchy + mask pointers
    hier_off = u64(d, props_end)
    mask_off = u64(d, props_end + 8)

    # Parse hierarchy
    hier_w = u32(d, hier_off)
    hier_h = u32(d, hier_off + 4)
    bpp = u32(d, hier_off + 8)

    # Level pointer table
    lp = hier_off + 12
    levels = []
    while lp + 8 <= len(d):
        lo = u64(d, lp); lp += 8
        if lo == 0:
            break
        levels.append(lo)

    # Parse level 0
    level0_off = levels[0] if levels else 0
    lvl_w = u32(d, level0_off)
    lvl_h = u32(d, level0_off + 4)

    # Tile offset table
    tp = level0_off + 8
    tile_offsets = []
    while tp + 8 <= len(d):
        to = u64(d, tp); tp += 8
        if to == 0:
            break
        tile_offsets.append(to)

    tiles_x = (w + 63) // 64

    # Extract tile data
    tiles_data = []
    for ti, toff in enumerate(tile_offsets):
        tw, th_ = tile_dims(ti, w, h, tiles_x)
        consumed, ok = try_decode_tile(d, toff, tw, th_, bpp)
        if ok:
            tiles_data.append(d[toff:toff + consumed])
        else:
            tiles_data.append(None)  # corrupt tile

    # Parse stub levels (1+)
    stub_levels = []
    for li in range(1, len(levels)):
        sl_off = levels[li]
        if sl_off + 8 > len(d):
            break
        sl_w = u32(d, sl_off)
        sl_h = u32(d, sl_off + 4)
        if sl_w == 0 or sl_h == 0 or sl_w > 65536 or sl_h > 65536:
            break
        sl_tp = sl_off + 8
        sl_tile_offsets = []
        while sl_tp + 8 <= len(d):
            sto = u64(d, sl_tp); sl_tp += 8
            if sto == 0:
                break
            if sto >= len(d):
                break
            sl_tile_offsets.append(sto)

        sl_tiles_x = (sl_w + 63) // 64
        sl_tiles_data = []
        for sti, stoff in enumerate(sl_tile_offsets):
            stw, sth = tile_dims(sti, sl_w, sl_h, sl_tiles_x)
            sc, sok = try_decode_tile(d, stoff, stw, sth, bpp)
            if sok:
                sl_tiles_data.append(d[stoff:stoff + sc])
            else:
                sl_tiles_data.append(None)
        stub_levels.append({
            'w': sl_w, 'h': sl_h,
            'tiles_data': sl_tiles_data,
        })

    # Parse layer mask if present
    mask_data = None
    if mask_off != 0:
        mask_data = parse_channel(d, mask_off)

    return {
        'name': name, 'w': w, 'h': h, 'type': ltype, 'nlen': nlen,
        'header_raw': d[layer_off:header_end],
        'props_raw': props_raw,
        'bpp': bpp,
        'tiles_x': tiles_x,
        'tiles_data': tiles_data,
        'stub_levels': stub_levels,
        'mask': mask_data,
        'has_mask': mask_off != 0,
    }


def parse_channel(d, ch_off):
    """Parse a channel structure."""
    w = u32(d, ch_off)
    h = u32(d, ch_off + 4)
    nlen = u32(d, ch_off + 8)
    name = d[ch_off + 12:ch_off + 12 + nlen - 1].decode('utf-8', errors='replace')
    header_end = ch_off + 12 + nlen

    props_raw, props_end = read_properties_raw(d, header_end)
    hier_off = u64(d, props_end)

    # Parse hierarchy
    hier_w = u32(d, hier_off)
    hier_h = u32(d, hier_off + 4)
    bpp = u32(d, hier_off + 8)

    lp = hier_off + 12
    levels = []
    while lp + 8 <= len(d):
        lo = u64(d, lp); lp += 8
        if lo == 0:
            break
        levels.append(lo)

    level0_off = levels[0] if levels else 0
    tp = level0_off + 8
    tile_offsets = []
    while tp + 8 <= len(d):
        to = u64(d, tp); tp += 8
        if to == 0:
            break
        tile_offsets.append(to)

    tiles_x = (w + 63) // 64
    tiles_data = []
    for ti, toff in enumerate(tile_offsets):
        tw, th_ = tile_dims(ti, w, h, tiles_x)
        consumed, ok = try_decode_tile(d, toff, tw, th_, bpp)
        if ok:
            tiles_data.append(d[toff:toff + consumed])
        else:
            tiles_data.append(None)

    # Stub levels
    stub_levels = []
    for li in range(1, len(levels)):
        sl_off = levels[li]
        if sl_off + 8 > len(d):
            break
        sl_w = u32(d, sl_off)
        sl_h = u32(d, sl_off + 4)
        if sl_w == 0 or sl_h == 0 or sl_w > 65536 or sl_h > 65536:
            break
        sl_tp = sl_off + 8
        sl_tile_offsets = []
        while sl_tp + 8 <= len(d):
            sto = u64(d, sl_tp); sl_tp += 8
            if sto == 0:
                break
            if sto >= len(d):
                break
            sl_tile_offsets.append(sto)
        sl_tiles_x = (sl_w + 63) // 64
        sl_tiles_data = []
        for sti, stoff in enumerate(sl_tile_offsets):
            stw, sth = tile_dims(sti, sl_w, sl_h, sl_tiles_x)
            sc, sok = try_decode_tile(d, stoff, stw, sth, bpp)
            if sok:
                sl_tiles_data.append(d[stoff:stoff + sc])
            else:
                sl_tiles_data.append(None)
        stub_levels.append({'w': sl_w, 'h': sl_h, 'tiles_data': sl_tiles_data})

    return {
        'name': name, 'w': w, 'h': h, 'nlen': nlen, 'bpp': bpp,
        'header_raw': d[ch_off:ch_off + 12 + nlen],
        'props_raw': props_raw,
        'tiles_data': tiles_data,
        'tiles_x': tiles_x,
        'stub_levels': stub_levels,
    }


def encode_rle_channel_zeros(npixels):
    """Encode a channel of all-zero pixels as RLE."""
    result = bytearray()
    remaining = npixels
    while remaining > 0:
        if remaining > 127:
            run = min(remaining, 65535)
            result.append(127)
            result.append((run >> 8) & 0xFF)
            result.append(run & 0xFF)
            result.append(0)
            remaining -= run
        else:
            result.append(remaining - 1)
            result.append(0)
            remaining = 0
    return bytes(result)


def make_transparent_tile(tw, th, bpp):
    """Create valid RLE data for a transparent (all-zero) tile."""
    npixels = tw * th
    channel_data = encode_rle_channel_zeros(npixels)
    return channel_data * bpp


class XcfWriter:
    """Builds an XCF file sequentially, tracking positions for fixups."""

    def __init__(self):
        self.buf = bytearray()
        self.fixups = []  # list of (position, value) to write as u64

    def pos(self):
        return len(self.buf)

    def write(self, data):
        self.buf.extend(data)

    def write_u32(self, v):
        self.buf.extend(p32(v))

    def write_u64(self, v):
        self.buf.extend(p64(v))

    def reserve_u64(self):
        """Reserve 8 bytes, return the position for later fixup."""
        pos = self.pos()
        self.buf.extend(b'\x00' * 8)
        return pos

    def fixup_u64(self, pos, value):
        """Write a u64 value at a previously reserved position."""
        self.buf[pos:pos + 8] = p64(value)

    def write_tiles(self, layer_struct, clean_layer=None):
        """Write level 0 tile data, using clean tiles where available.
        Returns list of tile data positions."""
        w, h, bpp = layer_struct['w'], layer_struct['h'], layer_struct['bpp']
        tiles_x = layer_struct['tiles_x']
        n_tiles = len(layer_struct['tiles_data'])

        tile_positions = []
        clean_used = 0
        v7_used = 0
        transparent_used = 0

        for ti in range(n_tiles):
            tw, th_ = tile_dims(ti, w, h, tiles_x)
            tile_data = None

            # Try clean tile first
            if clean_layer and ti < len(clean_layer['tiles_data']):
                tile_data = clean_layer['tiles_data'][ti]
                if tile_data is not None:
                    clean_used += 1

            # Fall back to v7 tile
            if tile_data is None:
                tile_data = layer_struct['tiles_data'][ti]
                if tile_data is not None:
                    v7_used += 1

            # Last resort: transparent
            if tile_data is None:
                tile_data = make_transparent_tile(tw, th_, bpp)
                transparent_used += 1

            tile_positions.append(self.pos())
            self.write(tile_data)

        return tile_positions, clean_used, v7_used, transparent_used

    def write_stub_tiles(self, stub, bpp):
        """Write stub level tile data. Returns list of tile data positions."""
        tile_positions = []
        for ti, td in enumerate(stub['tiles_data']):
            if td is None:
                tiles_x = (stub['w'] + 63) // 64
                tw, th_ = tile_dims(ti, stub['w'], stub['h'], tiles_x)
                td = make_transparent_tile(tw, th_, bpp)
            tile_positions.append(self.pos())
            self.write(td)
        return tile_positions


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

    # ── Parse v7 file header ──
    assert v7_data[:9] == b'gimp xcf '
    ver = int(v7_data[9:13].replace(b'v', b'').replace(b'\x00', b'').decode())
    print(f"Version: v{ver:03d}")

    pos = 14
    canvas_w = u32(v7_data, pos); pos += 4
    canvas_h = u32(v7_data, pos); pos += 4
    base_type = u32(v7_data, pos); pos += 4
    precision = u32(v7_data, pos) if ver >= 4 else None
    if ver >= 4: pos += 4
    print(f"Canvas: {canvas_w}x{canvas_h}, type={base_type}")

    # Image properties
    img_props_raw, props_end = read_properties_raw(v7_data, pos)

    # Layer offset table
    lpos = props_end
    v7_layer_offsets = []
    while lpos + 8 <= len(v7_data):
        off = u64(v7_data, lpos); lpos += 8
        if off == 0: break
        v7_layer_offsets.append(off)
    # skip terminator zero, already consumed

    # Channel offset table
    v7_channel_offsets = []
    while lpos + 8 <= len(v7_data):
        off = u64(v7_data, lpos); lpos += 8
        if off == 0: break
        v7_channel_offsets.append(off)

    print(f"{len(v7_layer_offsets)} layers, {len(v7_channel_offsets)} channels")

    # ── Parse all layers from both files ──
    print("\nParsing v7 layers...")
    v7_layers = []
    for i, loff in enumerate(v7_layer_offsets):
        layer = parse_layer(v7_data, loff)
        v7_layers.append(layer)
        print(f"  {i}: \"{layer['name']}\" ({layer['w']}x{layer['h']}) "
              f"{len(layer['tiles_data'])} tiles, bpp={layer['bpp']}, "
              f"mask={'yes' if layer['has_mask'] else 'no'}")

    print("\nParsing clean file...")
    # Parse clean file header
    c_pos = 14 + 4 + 4 + 4
    if ver >= 4: c_pos += 4
    c_pos = skip_properties(clean_data, c_pos)
    clean_layer_offsets = []
    while c_pos + 8 <= len(clean_data):
        off = u64(clean_data, c_pos); c_pos += 8
        if off == 0: break
        clean_layer_offsets.append(off)

    clean_layers = {}
    for i, loff in enumerate(clean_layer_offsets):
        layer = parse_layer(clean_data, loff)
        clean_layers[layer['name']] = layer
        n_ok = sum(1 for t in layer['tiles_data'] if t is not None)
        print(f"  {i}: \"{layer['name']}\" ({layer['w']}x{layer['h']}) "
              f"{len(layer['tiles_data'])} tiles ({n_ok} valid), bpp={layer['bpp']}")

    # ── Build new file ──
    print("\n=== Building v8 output ===")
    w = XcfWriter()

    # File header
    w.write(v7_data[:14])  # magic + version
    w.write_u32(canvas_w)
    w.write_u32(canvas_h)
    w.write_u32(base_type)
    if ver >= 4:
        w.write_u32(precision)

    # Image properties
    w.write(img_props_raw)

    # Layer offset table (placeholders)
    layer_table_positions = []
    for _ in v7_layers:
        layer_table_positions.append(w.reserve_u64())
    w.write_u64(0)  # terminator

    # Channel offset table (placeholders)
    channel_table_positions = []
    for _ in v7_channel_offsets:
        channel_table_positions.append(w.reserve_u64())
    w.write_u64(0)  # terminator

    # ── Write each layer ──
    for li, v7_layer in enumerate(v7_layers):
        layer_pos = w.pos()
        w.fixup_u64(layer_table_positions[li], layer_pos)

        clean_layer = clean_layers.get(v7_layer['name'])
        if clean_layer:
            # Verify dimensions match
            if clean_layer['w'] != v7_layer['w'] or clean_layer['h'] != v7_layer['h']:
                print(f"  WARNING: Layer \"{v7_layer['name']}\" dimensions mismatch! "
                      f"v7={v7_layer['w']}x{v7_layer['h']} clean={clean_layer['w']}x{clean_layer['h']}")
                clean_layer = None

        # Layer header (w, h, type, nlen, name)
        w.write(v7_layer['header_raw'])

        # Layer properties
        w.write(v7_layer['props_raw'])

        # Hierarchy + mask offset placeholders
        hier_off_pos = w.reserve_u64()
        mask_off_pos = w.reserve_u64()

        # Hierarchy header
        hier_pos = w.pos()
        w.fixup_u64(hier_off_pos, hier_pos)
        w.write_u32(v7_layer['w'])
        w.write_u32(v7_layer['h'])
        w.write_u32(v7_layer['bpp'])

        # Level offset table (placeholders)
        n_levels = 1 + len(v7_layer['stub_levels'])
        level_table_positions = []
        for _ in range(n_levels):
            level_table_positions.append(w.reserve_u64())
        w.write_u64(0)  # terminator

        # Level 0
        level0_pos = w.pos()
        w.fixup_u64(level_table_positions[0], level0_pos)
        w.write_u32(v7_layer['w'])
        w.write_u32(v7_layer['h'])

        # Tile offset table (placeholders)
        n_tiles = len(v7_layer['tiles_data'])
        tile_table_positions = []
        for _ in range(n_tiles):
            tile_table_positions.append(w.reserve_u64())
        w.write_u64(0)  # terminator

        # Write tile data
        tile_positions, c_used, v_used, t_used = w.write_tiles(v7_layer, clean_layer)

        # Fixup tile offsets
        for ti, tpos in enumerate(tile_positions):
            w.fixup_u64(tile_table_positions[ti], tpos)

        src = f"clean={c_used}" if c_used else ""
        if v_used: src += f" v7={v_used}"
        if t_used: src += f" transparent={t_used}"
        print(f"  Layer {li} \"{v7_layer['name']}\": {n_tiles} tiles ({src.strip()})")

        # Stub levels
        for sli, stub in enumerate(v7_layer['stub_levels']):
            sl_pos = w.pos()
            w.fixup_u64(level_table_positions[1 + sli], sl_pos)
            w.write_u32(stub['w'])
            w.write_u32(stub['h'])

            sl_tile_positions_list = []
            for _ in stub['tiles_data']:
                sl_tile_positions_list.append(w.reserve_u64())
            w.write_u64(0)

            sl_tile_pos = w.write_stub_tiles(stub, v7_layer['bpp'])
            for sti, stp in enumerate(sl_tile_pos):
                w.fixup_u64(sl_tile_positions_list[sti], stp)

        # Layer mask
        if v7_layer['has_mask'] and v7_layer['mask']:
            mask = v7_layer['mask']
            mask_pos = w.pos()
            w.fixup_u64(mask_off_pos, mask_pos)

            # Channel header + props
            w.write(mask['header_raw'])
            w.write(mask['props_raw'])

            # Hierarchy offset placeholder
            m_hier_off_pos = w.reserve_u64()

            # Hierarchy
            m_hier_pos = w.pos()
            w.fixup_u64(m_hier_off_pos, m_hier_pos)
            w.write_u32(mask['w'])
            w.write_u32(mask['h'])
            w.write_u32(mask['bpp'])

            # Level offset table
            m_n_levels = 1 + len(mask['stub_levels'])
            m_level_positions = []
            for _ in range(m_n_levels):
                m_level_positions.append(w.reserve_u64())
            w.write_u64(0)

            # Level 0
            m_level0_pos = w.pos()
            w.fixup_u64(m_level_positions[0], m_level0_pos)
            w.write_u32(mask['w'])
            w.write_u32(mask['h'])

            m_tile_positions_list = []
            for _ in mask['tiles_data']:
                m_tile_positions_list.append(w.reserve_u64())
            w.write_u64(0)

            for mti, mtd in enumerate(mask['tiles_data']):
                if mtd is None:
                    tw, th_ = tile_dims(mti, mask['w'], mask['h'], mask['tiles_x'])
                    mtd = make_transparent_tile(tw, th_, mask['bpp'])
                mtp = w.pos()
                w.write(mtd)
                w.fixup_u64(m_tile_positions_list[mti], mtp)

            for msli, mstub in enumerate(mask['stub_levels']):
                msl_pos = w.pos()
                w.fixup_u64(m_level_positions[1 + msli], msl_pos)
                w.write_u32(mstub['w'])
                w.write_u32(mstub['h'])
                msl_tile_pos_list = []
                for _ in mstub['tiles_data']:
                    msl_tile_pos_list.append(w.reserve_u64())
                w.write_u64(0)
                msl_tps = w.write_stub_tiles(mstub, mask['bpp'])
                for msti, mstp in enumerate(msl_tps):
                    w.fixup_u64(msl_tile_pos_list[msti], mstp)
        else:
            w.fixup_u64(mask_off_pos, 0)

    # ── Write channels ──
    for ci, ch_off in enumerate(v7_channel_offsets):
        ch = parse_channel(v7_data, ch_off)
        ch_pos = w.pos()
        w.fixup_u64(channel_table_positions[ci], ch_pos)

        w.write(ch['header_raw'])
        w.write(ch['props_raw'])

        ch_hier_off_pos = w.reserve_u64()

        ch_hier_pos = w.pos()
        w.fixup_u64(ch_hier_off_pos, ch_hier_pos)
        w.write_u32(ch['w'])
        w.write_u32(ch['h'])
        w.write_u32(ch['bpp'])

        ch_n_levels = 1 + len(ch['stub_levels'])
        ch_level_positions = []
        for _ in range(ch_n_levels):
            ch_level_positions.append(w.reserve_u64())
        w.write_u64(0)

        ch_level0_pos = w.pos()
        w.fixup_u64(ch_level_positions[0], ch_level0_pos)
        w.write_u32(ch['w'])
        w.write_u32(ch['h'])

        ch_tile_positions_list = []
        for _ in ch['tiles_data']:
            ch_tile_positions_list.append(w.reserve_u64())
        w.write_u64(0)

        for cti, ctd in enumerate(ch['tiles_data']):
            if ctd is None:
                tw, th_ = tile_dims(cti, ch['w'], ch['h'], ch['tiles_x'])
                ctd = make_transparent_tile(tw, th_, ch['bpp'])
            ctp = w.pos()
            w.write(ctd)
            w.fixup_u64(ch_tile_positions_list[cti], ctp)

        for csli, cstub in enumerate(ch['stub_levels']):
            csl_pos = w.pos()
            w.fixup_u64(ch_level_positions[1 + csli], csl_pos)
            w.write_u32(cstub['w'])
            w.write_u32(cstub['h'])
            csl_tile_pos_list = []
            for _ in cstub['tiles_data']:
                csl_tile_pos_list.append(w.reserve_u64())
            w.write_u64(0)
            csl_tps = w.write_stub_tiles(cstub, ch['bpp'])
            for csti, cstp in enumerate(csl_tps):
                w.fixup_u64(csl_tile_pos_list[csti], cstp)

    # ── Save ──
    output = bytes(w.buf)
    print(f"\nOutput size: {len(output):,} bytes")

    with open(output_path, 'wb') as f:
        f.write(output)

    # ── Verify ──
    print("\n=== Verification ===")
    verify_pos = 14 + 4 + 4 + 4
    if ver >= 4: verify_pos += 4
    verify_pos = skip_properties(output, verify_pos)

    out_layer_offsets = []
    while verify_pos + 8 <= len(output):
        off = u64(output, verify_pos); verify_pos += 8
        if off == 0: break
        out_layer_offsets.append(off)

    for i, loff in enumerate(out_layer_offsets):
        w_ = u32(output, loff)
        h_ = u32(output, loff + 4)
        nlen = u32(output, loff + 12)
        name = output[loff + 16:loff + 16 + nlen - 1].decode('utf-8', errors='replace')

        prop_end = skip_properties(output, loff + 16 + nlen)
        hier_off = u64(output, prop_end)
        bpp = u32(output, hier_off + 8)
        lvl0_off = u64(output, hier_off + 12)

        tp = lvl0_off + 8
        tiles = []
        while tp + 8 <= len(output):
            to = u64(output, tp); tp += 8
            if to == 0: break
            tiles.append(to)

        tiles_x = (w_ + 63) // 64
        n_ok = 0
        n_fail = 0
        for ti, toff in enumerate(tiles):
            tw, th_ = tile_dims(ti, w_, h_, tiles_x)
            _, ok = try_decode_tile(output, toff, tw, th_, bpp)
            if ok:
                n_ok += 1
            else:
                n_fail += 1

        status = "OK" if n_fail == 0 else f"FAIL ({n_fail} bad tiles)"
        print(f"  Layer {i}: \"{name}\" ({w_}x{h_}) {len(tiles)} tiles [{status}]")

    print(f"\nSaved: {output_path}")


if __name__ == '__main__':
    main()

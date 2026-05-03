#!/usr/bin/env python3
"""Test: rebuild a valid XCF from scratch to check if the rebuild logic is correct."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from restore_xcf_v8 import (
    u32, u64, p32, p64, skip_properties, read_properties_raw,
    parse_layer, parse_channel, tile_dims, try_decode_tile,
    make_transparent_tile, XcfWriter, encode_rle_channel_zeros
)

def main():
    src_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else src_path.replace('.xcf', '-REBUILT.xcf')

    d = open(src_path, 'rb').read()
    print(f"Input:  {src_path} ({len(d):,} bytes)")
    print(f"Output: {out_path}")

    ver = int(d[9:13].replace(b'v', b'').replace(b'\x00', b'').decode())
    pos = 14
    canvas_w = u32(d, pos); pos += 4
    canvas_h = u32(d, pos); pos += 4
    base_type = u32(d, pos); pos += 4
    precision = u32(d, pos) if ver >= 4 else None
    if ver >= 4: pos += 4

    img_props_raw, props_end = read_properties_raw(d, pos)

    # Layer table
    lpos = props_end
    layer_offsets = []
    while lpos + 8 <= len(d):
        off = u64(d, lpos); lpos += 8
        if off == 0: break
        layer_offsets.append(off)

    # Channel table
    channel_offsets = []
    while lpos + 8 <= len(d):
        off = u64(d, lpos); lpos += 8
        if off == 0: break
        channel_offsets.append(off)

    print(f"{len(layer_offsets)} layers, {len(channel_offsets)} channels")

    # Parse layers
    layers = []
    for i, loff in enumerate(layer_offsets):
        layer = parse_layer(d, loff)
        n_ok = sum(1 for t in layer['tiles_data'] if t is not None)
        print(f"  Layer {i}: \"{layer['name']}\" ({layer['w']}x{layer['h']}) "
              f"{len(layer['tiles_data'])} tiles ({n_ok} ok), {len(layer['stub_levels'])} stubs")
        layers.append(layer)

    # Rebuild
    w = XcfWriter()
    w.write(d[:14])
    w.write_u32(canvas_w)
    w.write_u32(canvas_h)
    w.write_u32(base_type)
    if ver >= 4:
        w.write_u32(precision)
    w.write(img_props_raw)

    layer_table_pos = []
    for _ in layers:
        layer_table_pos.append(w.reserve_u64())
    w.write_u64(0)

    ch_table_pos = []
    for _ in channel_offsets:
        ch_table_pos.append(w.reserve_u64())
    w.write_u64(0)

    for li, layer in enumerate(layers):
        layer_pos = w.pos()
        w.fixup_u64(layer_table_pos[li], layer_pos)

        w.write(layer['header_raw'])
        w.write(layer['props_raw'])

        hier_off_pos = w.reserve_u64()
        mask_off_pos = w.reserve_u64()

        hier_pos = w.pos()
        w.fixup_u64(hier_off_pos, hier_pos)
        w.write_u32(layer['w'])
        w.write_u32(layer['h'])
        w.write_u32(layer['bpp'])

        n_levels = 1 + len(layer['stub_levels'])
        level_tps = []
        for _ in range(n_levels):
            level_tps.append(w.reserve_u64())
        w.write_u64(0)

        # Level 0
        l0_pos = w.pos()
        w.fixup_u64(level_tps[0], l0_pos)
        w.write_u32(layer['w'])
        w.write_u32(layer['h'])

        tile_tps = []
        for _ in layer['tiles_data']:
            tile_tps.append(w.reserve_u64())
        w.write_u64(0)

        tile_positions, c, v, t = w.write_tiles(layer)
        for ti, tp in enumerate(tile_positions):
            w.fixup_u64(tile_tps[ti], tp)

        # Stub levels
        for sli, stub in enumerate(layer['stub_levels']):
            sl_pos = w.pos()
            w.fixup_u64(level_tps[1 + sli], sl_pos)
            w.write_u32(stub['w'])
            w.write_u32(stub['h'])
            sl_tile_tps = []
            for _ in stub['tiles_data']:
                sl_tile_tps.append(w.reserve_u64())
            w.write_u64(0)
            sl_tile_pos = w.write_stub_tiles(stub, layer['bpp'])
            for sti, stp in enumerate(sl_tile_pos):
                w.fixup_u64(sl_tile_tps[sti], stp)

        # Mask
        if layer['has_mask'] and layer['mask']:
            mask = layer['mask']
            mask_pos = w.pos()
            w.fixup_u64(mask_off_pos, mask_pos)
            w.write(mask['header_raw'])
            w.write(mask['props_raw'])
            m_hier_pos_ref = w.reserve_u64()
            m_hier_pos = w.pos()
            w.fixup_u64(m_hier_pos_ref, m_hier_pos)
            w.write_u32(mask['w'])
            w.write_u32(mask['h'])
            w.write_u32(mask['bpp'])
            m_n_levels = 1 + len(mask['stub_levels'])
            m_lvl_tps = []
            for _ in range(m_n_levels):
                m_lvl_tps.append(w.reserve_u64())
            w.write_u64(0)
            m_l0_pos = w.pos()
            w.fixup_u64(m_lvl_tps[0], m_l0_pos)
            w.write_u32(mask['w'])
            w.write_u32(mask['h'])
            m_tile_tps = []
            for _ in mask['tiles_data']:
                m_tile_tps.append(w.reserve_u64())
            w.write_u64(0)
            for mti, mtd in enumerate(mask['tiles_data']):
                if mtd is None:
                    tw, th = tile_dims(mti, mask['w'], mask['h'], mask['tiles_x'])
                    mtd = make_transparent_tile(tw, th, mask['bpp'])
                mtp = w.pos()
                w.write(mtd)
                w.fixup_u64(m_tile_tps[mti], mtp)
            for msli, mstub in enumerate(mask['stub_levels']):
                msl_pos = w.pos()
                w.fixup_u64(m_lvl_tps[1 + msli], msl_pos)
                w.write_u32(mstub['w'])
                w.write_u32(mstub['h'])
                msl_tile_tps = []
                for _ in mstub['tiles_data']:
                    msl_tile_tps.append(w.reserve_u64())
                w.write_u64(0)
                msl_tps = w.write_stub_tiles(mstub, mask['bpp'])
                for msti, mstp in enumerate(msl_tps):
                    w.fixup_u64(msl_tile_tps[msti], mstp)
        else:
            w.fixup_u64(mask_off_pos, 0)

    # Channels
    for ci, ch_off in enumerate(channel_offsets):
        ch = parse_channel(d, ch_off)
        ch_pos = w.pos()
        w.fixup_u64(ch_table_pos[ci], ch_pos)
        w.write(ch['header_raw'])
        w.write(ch['props_raw'])
        ch_hier_ref = w.reserve_u64()
        ch_hier_pos = w.pos()
        w.fixup_u64(ch_hier_ref, ch_hier_pos)
        w.write_u32(ch['w'])
        w.write_u32(ch['h'])
        w.write_u32(ch['bpp'])
        ch_n_levels = 1 + len(ch['stub_levels'])
        ch_lvl_tps = []
        for _ in range(ch_n_levels):
            ch_lvl_tps.append(w.reserve_u64())
        w.write_u64(0)
        ch_l0_pos = w.pos()
        w.fixup_u64(ch_lvl_tps[0], ch_l0_pos)
        w.write_u32(ch['w'])
        w.write_u32(ch['h'])
        ch_tile_tps = []
        for _ in ch['tiles_data']:
            ch_tile_tps.append(w.reserve_u64())
        w.write_u64(0)
        for cti, ctd in enumerate(ch['tiles_data']):
            if ctd is None:
                tw, th = tile_dims(cti, ch['w'], ch['h'], ch['tiles_x'])
                ctd = make_transparent_tile(tw, th, ch['bpp'])
            ctp = w.pos()
            w.write(ctd)
            w.fixup_u64(ch_tile_tps[cti], ctp)
        for csli, cstub in enumerate(ch['stub_levels']):
            csl_pos = w.pos()
            w.fixup_u64(ch_lvl_tps[1 + csli], csl_pos)
            w.write_u32(cstub['w'])
            w.write_u32(cstub['h'])
            csl_tile_tps = []
            for _ in cstub['tiles_data']:
                csl_tile_tps.append(w.reserve_u64())
            w.write_u64(0)
            csl_tps = w.write_stub_tiles(cstub, ch['bpp'])
            for csti, cstp in enumerate(csl_tps):
                w.fixup_u64(csl_tile_tps[csti], cstp)

    output = bytes(w.buf)
    print(f"\nOutput: {len(output):,} bytes (orig: {len(d):,})")
    with open(out_path, 'wb') as f:
        f.write(output)
    print(f"Saved: {out_path}")

if __name__ == '__main__':
    main()

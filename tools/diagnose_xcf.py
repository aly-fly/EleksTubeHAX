#!/usr/bin/env python3
"""
Diagnose an XCF file by following the exact same path GIMP would.
Reports every structural element and where parsing fails.
"""
import struct
import sys


def u32(data, pos):
    return struct.unpack('>I', data[pos:pos+4])[0]

def u64(data, pos):
    return struct.unpack('>Q', data[pos:pos+8])[0]


def diagnose(filepath):
    with open(filepath, 'rb') as fh:
        d = fh.read()

    print(f"File: {filepath}")
    print(f"Size: {len(d)} bytes (0x{len(d):X})")
    print(f"Magic: {d[:14]}")

    pos = 14
    width = u32(d, pos); pos += 4
    height = u32(d, pos); pos += 4
    base_type = u32(d, pos); pos += 4

    # Detect version for precision field
    ver_str = d[9:13].replace(b'v', b'').replace(b'\x00', b'').decode()
    ver = int(ver_str)
    if ver >= 4:
        precision = u32(d, pos); pos += 4
    else:
        precision = 0

    print(f"Version: {ver}, Image: {width}x{height}, "
          f"base_type={base_type}, precision={precision}")

    # Image properties
    print(f"\n=== Image Properties (at 0x{pos:X}) ===")
    img_compression = 0
    while pos < len(d) - 8:
        pt = u32(d, pos); pos += 4
        ps = u32(d, pos); pos += 4
        if pt == 0:
            break
        if pt == 17:  # PROP_COMPRESSION
            img_compression = u32(d, pos)
            print(f"  PROP_COMPRESSION = {img_compression} "
                  f"({'none' if img_compression == 0 else 'rle' if img_compression == 1 else 'zlib' if img_compression == 2 else '?'})")
        elif pt == 19:  # PROP_RESOLUTION
            print(f"  PROP_RESOLUTION = {struct.unpack('>ff', d[pos:pos+8])}")
        elif pt == 1:  # PROP_COLORMAP
            print(f"  PROP_COLORMAP (size={ps})")
        else:
            print(f"  prop type={pt} size={ps}")
        pos += ps

    # Layer offsets
    print(f"\n=== Layer Offset Table (at 0x{pos:X}) ===")
    layer_offsets = []
    while pos < len(d) - 8:
        off = u64(d, pos); pos += 8
        if off == 0:
            break
        layer_offsets.append(off)
    print(f"  {len(layer_offsets)} layers")
    for i, off in enumerate(layer_offsets):
        in_bounds = "OK" if off < len(d) else "BEYOND END!"
        print(f"  [{i}] offset=0x{off:X} {in_bounds}")

    # Channel offsets
    print(f"\n=== Channel Offset Table (at 0x{pos:X}) ===")
    channel_offsets = []
    while pos < len(d) - 8:
        off = u64(d, pos); pos += 8
        if off == 0:
            break
        channel_offsets.append(off)
    print(f"  {len(channel_offsets)} channels")

    # Diagnose each layer
    for li, loff in enumerate(layer_offsets):
        print(f"\n{'='*60}")
        print(f"=== Layer {li} at 0x{loff:X} ===")
        if loff + 16 >= len(d):
            print(f"  FATAL: offset beyond file")
            continue

        p = loff
        lw = u32(d, p); p += 4
        lh = u32(d, p); p += 4
        lt = u32(d, p); p += 4
        nlen = u32(d, p); p += 4

        if lw == 0 or lw > 65536 or lh == 0 or lh > 65536 or lt > 6 or nlen == 0 or nlen > 10000:
            print(f"  FATAL: invalid header: w={lw} h={lh} type={lt} nlen={nlen}")
            continue

        if p + nlen > len(d):
            print(f"  FATAL: name extends beyond file")
            continue

        name = d[p:p+nlen-1].decode('utf-8', errors='replace')
        p += nlen
        print(f"  \"{name}\" ({lw}x{lh}, type={lt})")

        # Layer properties
        print(f"  Properties (at 0x{p:X}):")
        prop_count = 0
        prop_error = False
        while p < len(d) - 8:
            pt = u32(d, p); p += 4
            ps = u32(d, p); p += 4
            if pt == 0:
                break
            prop_count += 1
            if p + ps > len(d):
                print(f"    FATAL: prop type={pt} size={ps} at 0x{p-8:X} extends beyond file!")
                prop_error = True
                break
            if ps > 10_000_000:  # suspiciously large
                print(f"    WARNING: prop type={pt} size={ps} (very large!)")
            if pt == 8:  # VISIBLE
                vis = u32(d, p)
                print(f"    VISIBLE = {vis}")
            elif pt == 6:  # OPACITY
                print(f"    OPACITY = {u32(d, p)}")
            elif pt == 21:  # PARASITES
                print(f"    PARASITES (size={ps})")
                # Parse parasites to check
                pp = p
                while pp < p + ps:
                    if pp + 4 > p + ps:
                        print(f"      WARN: parasite name truncated at 0x{pp:X}")
                        break
                    pnlen = u32(d, pp); pp += 4
                    if pnlen == 0 or pnlen > 1000 or pp + pnlen > p + ps:
                        print(f"      WARN: parasite name_len={pnlen} at 0x{pp-4:X}")
                        break
                    pname = d[pp:pp+pnlen-1].decode('utf-8', errors='replace')
                    pp += pnlen
                    if pp + 8 > p + ps:
                        print(f"      WARN: parasite header truncated for '{pname}'")
                        break
                    pflags = u32(d, pp); pp += 4
                    pdsize = u32(d, pp); pp += 4
                    if pp + pdsize > p + ps:
                        print(f"      WARN: parasite '{pname}' data size={pdsize} truncated")
                        break
                    print(f"      parasite '{pname}' flags={pflags} data_size={pdsize}")
                    pp += pdsize
            p += ps

        print(f"    {prop_count} properties total")
        if prop_error:
            continue

        # Hierarchy and mask offsets
        if p + 16 > len(d):
            print(f"  FATAL: can't read hier/mask offsets at 0x{p:X}")
            continue

        hier_off = u64(d, p); p += 8
        mask_off = u64(d, p); p += 8
        print(f"  Hierarchy offset: 0x{hier_off:X}")
        print(f"  Mask offset: 0x{mask_off:X}")

        if hier_off == 0 or hier_off >= len(d):
            print(f"  FATAL: invalid hierarchy offset!")
            continue
        if hier_off + 12 >= len(d):
            print(f"  FATAL: hierarchy header truncated")
            continue

        # Hierarchy
        hw = u32(d, hier_off)
        hh = u32(d, hier_off + 4)
        hbpp = u32(d, hier_off + 8)
        print(f"  Hierarchy: {hw}x{hh}, bpp={hbpp}")

        if hw != lw or hh != lh:
            print(f"  FATAL: hierarchy size mismatch! (expected {lw}x{lh})")
            continue
        if hbpp not in (1, 2, 3, 4):
            print(f"  FATAL: invalid bpp {hbpp}")
            continue

        # Level offsets
        hp = hier_off + 12
        level_offsets = []
        while hp < len(d) - 8:
            lo = u64(d, hp); hp += 8
            if lo == 0:
                break
            level_offsets.append(lo)

        print(f"  {len(level_offsets)} levels")

        if not level_offsets:
            print(f"  FATAL: no levels!")
            continue

        # Check level 0
        l0 = level_offsets[0]
        if l0 >= len(d) - 8:
            print(f"  FATAL: level 0 offset 0x{l0:X} beyond file")
            continue

        l0w = u32(d, l0)
        l0h = u32(d, l0 + 4)
        print(f"  Level 0: {l0w}x{l0h} at 0x{l0:X}")

        if l0w != lw or l0h != lh:
            print(f"  FATAL: level 0 size mismatch! (expected {lw}x{lh}, got {l0w}x{l0h})")
            continue

        # Tile offsets
        tp = l0 + 8
        tiles = []
        while tp < len(d) - 8:
            to = u64(d, tp); tp += 8
            if to == 0:
                break
            tiles.append(to)

        expected_tx = (lw + 63) // 64
        expected_ty = (lh + 63) // 64
        expected_tiles = expected_tx * expected_ty

        print(f"  Tiles: {len(tiles)} (expected {expected_tiles}, "
              f"{expected_tx}x{expected_ty})")

        if len(tiles) != expected_tiles:
            print(f"  FATAL: tile count mismatch!")

        # Check tile offsets
        bad_tiles = 0
        out_of_order = 0
        beyond_file = 0
        prev = 0
        for ti, to in enumerate(tiles):
            if to >= len(d):
                beyond_file += 1
                if beyond_file <= 3:
                    print(f"    tile[{ti}] 0x{to:X} BEYOND FILE END")
            if to <= prev and ti > 0:
                out_of_order += 1
                if out_of_order <= 3:
                    print(f"    tile[{ti}] 0x{to:X} <= prev 0x{prev:X} OUT OF ORDER")
            prev = to

        print(f"  Tile range: 0x{tiles[0]:X} to 0x{tiles[-1]:X}")
        if beyond_file:
            print(f"  ERROR: {beyond_file} tiles beyond file end!")
        if out_of_order:
            print(f"  ERROR: {out_of_order} tiles out of order!")

        # Check tile sizes (from consecutive offsets)
        if len(tiles) >= 2:
            sizes = [tiles[i+1] - tiles[i] for i in range(len(tiles)-1)]
            min_s = min(sizes)
            max_s = max(sizes)
            avg_s = sum(sizes) / len(sizes)
            neg_count = sum(1 for s in sizes if s <= 0)
            huge_count = sum(1 for s in sizes if s > 64*64*hbpp*2)
            print(f"  Tile sizes: min={min_s}, max={max_s}, avg={avg_s:.0f}")
            if neg_count:
                print(f"  ERROR: {neg_count} tiles with zero/negative size!")
            if huge_count:
                print(f"  WARNING: {huge_count} tiles suspiciously large (>2x uncompressed)")

        # Try to RLE-decode first tile
        if img_compression == 1 and tiles:  # RLE
            t0_pos = tiles[0]
            tile_w = min(64, lw)
            tile_h = min(64, lh)
            total_pixels = tile_w * tile_h
            print(f"  First tile RLE decode test ({tile_w}x{tile_h}, {hbpp} channels):")
            success = True
            rle_pos = t0_pos
            for ch in range(hbpp):
                decoded = 0
                ch_start = rle_pos
                while decoded < total_pixels and rle_pos < len(d):
                    byte = d[rle_pos]; rle_pos += 1
                    if byte <= 126:
                        count = byte + 1
                        rle_pos += 1  # value byte
                        decoded += count
                    elif byte == 127:
                        if rle_pos + 2 >= len(d):
                            break
                        count = (d[rle_pos] << 8) | d[rle_pos+1]
                        rle_pos += 3  # 2 count + 1 value
                        decoded += count
                    elif byte == 128:
                        if rle_pos + 1 >= len(d):
                            break
                        count = (d[rle_pos] << 8) | d[rle_pos+1]
                        rle_pos += 2 + count
                        decoded += count
                    else:
                        count = 256 - byte
                        rle_pos += count
                        decoded += count

                if decoded != total_pixels:
                    print(f"    Channel {ch}: decoded {decoded}/{total_pixels} - MISMATCH!")
                    success = False
                else:
                    print(f"    Channel {ch}: OK ({rle_pos - ch_start} bytes)")
            if success:
                actual_tile0_end = rle_pos
                if len(tiles) >= 2:
                    expected_tile1 = tiles[1]
                    print(f"    Tile 0 ends at 0x{actual_tile0_end:X}, "
                          f"tile 1 expected at 0x{expected_tile1:X}, "
                          f"diff={expected_tile1 - actual_tile0_end}")

    print(f"\n{'='*60}")
    print(f"Diagnosis complete.")


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.xcf>")
        sys.exit(1)
    diagnose(sys.argv[1])

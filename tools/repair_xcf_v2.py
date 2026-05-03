#!/usr/bin/env python3
"""
Repair XCF files damaged by Git's CRLF→LF conversion.

Strategy: Patch the layer offset table in the header to point to the actual
(shifted) layer positions. This doesn't fix corrupted pixel tiles but restores
structural integrity so GIMP can open the file.

Layers 0-4 (wiring diagrams) are fully intact (no shift).
Layers 5-8 (photos) may have some garbled tiles but their headers are valid.
"""
import struct
import sys
import os


def read_u32(data, pos):
    return struct.unpack('>I', data[pos:pos+4])[0], pos + 4

def read_u64(data, pos):
    return struct.unpack('>Q', data[pos:pos+8])[0], pos + 8

def write_u64(val):
    return struct.pack('>Q', val)


def find_layer_headers(f, max_w=65536, max_h=65536):
    """Scan entire file for valid XCF layer headers."""
    results = []
    for pos in range(0, len(f) - 20):
        try:
            w, _ = read_u32(f, pos)
            h, _ = read_u32(f, pos + 4)
            t, _ = read_u32(f, pos + 8)
            nlen, _ = read_u32(f, pos + 12)
        except:
            continue
        if not (0 < w <= max_w and 0 < h <= max_h and t <= 6 and 0 < nlen < 500):
            continue
        if pos + 16 + nlen > len(f):
            continue
        name_raw = f[pos+16:pos+16+nlen-1]
        try:
            name_str = name_raw.decode('utf-8')
            if all(c.isprintable() or c in '\n\r\t' for c in name_str) and len(name_str) > 0:
                results.append((pos, w, h, t, name_str))
        except:
            continue
    return results


def patch_layer_offsets(input_path, output_path):
    """Read XCF, find actual layer positions, patch offset table."""
    with open(input_path, 'rb') as fh:
        f = bytearray(fh.read())

    print(f"File: {input_path}")
    print(f"Size: {len(f)} bytes")

    # Parse header
    magic = bytes(f[:14])
    ver_str = magic[9:13].replace(b'v', b'').replace(b'\x00', b'').decode()
    ver = int(ver_str)
    use64 = ver >= 11
    print(f"Version: {ver} ({'64-bit' if use64 else '32-bit'} offsets)")

    pos = 14
    width, pos = read_u32(f, pos)
    height, pos = read_u32(f, pos)
    _, pos = read_u32(f, pos)
    if use64:
        _, pos = read_u32(f, pos)
    print(f"Image: {width}x{height}")

    # Skip properties
    while pos < len(f) - 8:
        ptype, pos = read_u32(f, pos)
        psize, pos = read_u32(f, pos)
        if ptype == 0:
            break
        pos += psize

    # Read stored layer offsets and their positions in the file
    layer_entries = []  # (file_position_of_offset, stored_value)
    offset_table_start = pos
    if use64:
        while pos < len(f) - 8:
            off, _ = read_u64(f, pos)
            if off == 0:
                pos += 8
                break
            layer_entries.append((pos, off))
            pos += 8
    else:
        while pos < len(f) - 4:
            off, _ = read_u32(f, pos)
            if off == 0:
                pos += 4
                break
            layer_entries.append((pos, off))
            pos += 4

    print(f"\n{len(layer_entries)} stored layer offsets:")
    for file_pos, stored_off in layer_entries:
        print(f"  Table pos 0x{file_pos:X}: → 0x{stored_off:X}")

    # Find all actual layer headers
    print(f"\nScanning for layer headers...")
    candidates = find_layer_headers(f)
    print(f"Found {len(candidates)} candidates:")
    for cpos, w, h, t, name in candidates:
        print(f"  0x{cpos:08X}: \"{name}\" ({w}x{h} type={t})")

    # Match stored offsets to actual positions
    print(f"\n=== Matching ===")
    patches = []
    used = set()

    for table_pos, stored_off in layer_entries:
        # First check if stored offset is already valid
        best = None
        best_dist = float('inf')

        for j, (cpos, w, h, t, name) in enumerate(candidates):
            if j in used:
                continue
            dist = abs(stored_off - cpos)
            if dist < best_dist:
                best_dist = dist
                best = (j, cpos, name)

        if best:
            j, actual_pos, name = best
            used.add(j)
            shift = stored_off - actual_pos
            if shift == 0:
                print(f"  0x{stored_off:X} → OK (no change needed) \"{name}\"")
            else:
                print(f"  0x{stored_off:X} → 0x{actual_pos:X} (shift={shift}) \"{name}\"")
                patches.append((table_pos, actual_pos))
        else:
            print(f"  0x{stored_off:X} → NO MATCH FOUND")

    if not patches:
        print("\nNo patches needed - all offsets are already correct.")
        return

    # Also need to fix hierarchy offsets within each shifted layer
    # Parse each layer to find internal absolute offsets that need patching
    internal_patches = []
    for table_pos, new_layer_off in patches:
        stored_off = struct.unpack('>Q', f[table_pos:table_pos+8])[0]
        shift = stored_off - new_layer_off
        
        # Parse this layer's structure at its ACTUAL position
        p = new_layer_off
        lw, p = read_u32(f, p)
        lh, p = read_u32(f, p)
        lt, p = read_u32(f, p)
        nlen, p = read_u32(f, p)
        p += nlen  # skip name + null

        # Skip layer properties
        while p < len(f) - 8:
            ptype, p = read_u32(f, p)
            psize, p = read_u32(f, p)
            if ptype == 0:
                break
            p += psize

        # Hierarchy offset (absolute, 8 bytes)
        hier_off_pos = p
        hier_off, p = read_u64(f, p)
        # Layer mask offset
        mask_off, p = read_u64(f, p)

        if hier_off > 0:
            # The hierarchy offset needs adjustment by at least this layer's shift
            actual_hier = hier_off - shift
            if 0 < actual_hier < len(f):
                # Verify: at actual_hier, we should find width(4)+height(4)+bpp(4)
                try:
                    hw, _ = read_u32(f, actual_hier)
                    hh, _ = read_u32(f, actual_hier + 4)
                    hbpp, _ = read_u32(f, actual_hier + 8)
                    if hw == lw and hh == lh and hbpp in (1, 2, 3, 4):
                        print(f"    Hierarchy at 0x{hier_off:X} → 0x{actual_hier:X} "
                              f"({hw}x{hh} bpp={hbpp}) ✓")
                        internal_patches.append((hier_off_pos, actual_hier))

                        # Now parse hierarchy to find level offsets
                        hp = actual_hier + 12  # skip w, h, bpp
                        level_offsets = []
                        while hp < len(f) - 8:
                            loff, hp = read_u64(f, hp)
                            if loff == 0:
                                break
                            level_offsets.append((hp - 8, loff))

                        for lvl_pos, lvl_off in level_offsets:
                            actual_lvl = lvl_off - shift
                            if 0 < actual_lvl < len(f):
                                try:
                                    lv_w, _ = read_u32(f, actual_lvl)
                                    lv_h, _ = read_u32(f, actual_lvl + 4)
                                    if lv_w == lw and lv_h == lh:
                                        print(f"      Level at 0x{lvl_off:X} → 0x{actual_lvl:X} "
                                              f"({lv_w}x{lv_h}) ✓")
                                        internal_patches.append((lvl_pos, actual_lvl))

                                        # Parse tile offset table within the level
                                        tp = actual_lvl + 8  # skip w, h
                                        tile_patches = 0
                                        while tp < len(f) - 8:
                                            toff, _ = read_u64(f, tp)
                                            if toff == 0:
                                                break
                                            actual_tile = toff - shift
                                            if 0 < actual_tile < len(f):
                                                internal_patches.append((tp, actual_tile))
                                                tile_patches += 1
                                            tp += 8
                                        print(f"        {tile_patches} tile offsets patched")
                                    else:
                                        print(f"      Level at 0x{lvl_off:X} → mismatch "
                                              f"({lv_w}x{lv_h} vs expected {lw}x{lh})")
                                except:
                                    print(f"      Level at 0x{lvl_off:X} → read error")
                    else:
                        print(f"    Hierarchy at 0x{hier_off:X} → verify failed "
                              f"(w={hw} h={hh} bpp={hbpp})")
                except:
                    print(f"    Hierarchy at 0x{hier_off:X} → read error")

    # Apply all patches
    all_patches = patches + internal_patches
    print(f"\n=== Applying {len(all_patches)} patches ===")

    for patch_pos, new_value in all_patches:
        f[patch_pos:patch_pos+8] = write_u64(new_value)

    # Write repaired file
    with open(output_path, 'wb') as fh:
        fh.write(f)
    print(f"Written to: {output_path}")

    # Validate
    print(f"\n=== Validation ===")
    validate(f, use64)


def validate(f, use64):
    """Quick structural validation."""
    pos = 14
    _, pos = read_u32(f, pos)  # width
    _, pos = read_u32(f, pos)  # height
    _, pos = read_u32(f, pos)  # type
    if use64:
        _, pos = read_u32(f, pos)  # precision

    while pos < len(f) - 8:
        ptype, pos = read_u32(f, pos)
        psize, pos = read_u32(f, pos)
        if ptype == 0:
            break
        pos += psize

    offsets = []
    while pos < len(f) - 8:
        off, pos = read_u64(f, pos)
        if off == 0:
            break
        offsets.append(off)

    print(f"  {len(offsets)} layers:")
    for i, off in enumerate(offsets):
        if off >= len(f):
            print(f"  Layer {i}: 0x{off:X} BEYOND END")
            continue
        try:
            w, _ = read_u32(f, off)
            h, _ = read_u32(f, off + 4)
            t, _ = read_u32(f, off + 8)
            nlen, _ = read_u32(f, off + 12)
            if 0 < w <= 65536 and 0 < h <= 65536 and t <= 6 and 0 < nlen < 10000:
                name = bytes(f[off+16:off+16+nlen-1]).decode('utf-8', errors='replace')
                print(f"    Layer {i}: \"{name}\" ({w}x{h}) ✓")
            else:
                print(f"    Layer {i}: INVALID structure at 0x{off:X}")
        except Exception as e:
            print(f"    Layer {i}: ERROR at 0x{off:X}: {e}")


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <input.xcf> <output.xcf>")
        sys.exit(1)
    patch_layer_offsets(sys.argv[1], sys.argv[2])


if __name__ == '__main__':
    main()

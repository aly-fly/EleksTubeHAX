#!/usr/bin/env python3
"""Deep XCF validator — decode RLE tiles to find exactly where GIMP would fail."""
import struct, sys, os
os.environ.setdefault('PYTHONIOENCODING', 'utf-8')
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def u32(d, pos): return struct.unpack('>I', d[pos:pos+4])[0]
def u64(d, pos): return struct.unpack('>Q', d[pos:pos+8])[0]

def skip_properties(d, pos):
    while pos + 8 <= len(d):
        pt = u32(d, pos); ps = u32(d, pos + 4); pos += 8
        if pt == 0: break
        if pos + ps > len(d): break
        pos += ps
    return pos


def decode_rle_channel(d, pos, npixels):
    """Decode one channel of RLE data. Returns (bytes_consumed, decoded_count).
    XCF RLE:
      n=0..126:   run of n+1 identical bytes (1 value byte follows)
      n=127:      long run: 2-byte BE count, then 1 value byte 
      n=128:      long literal: 2-byte BE count, then that many literal bytes
      n=129..255: literal run of 256-n bytes
    """
    start = pos
    decoded = 0
    while decoded < npixels:
        if pos >= len(d):
            return pos - start, decoded
        n = d[pos]; pos += 1
        
        if n <= 126:
            # run of n+1 identical bytes
            if pos >= len(d): return pos - start, decoded
            pos += 1  # value byte
            decoded += n + 1
        elif n == 127:
            # long run
            if pos + 2 >= len(d): return pos - start, decoded
            count = (d[pos] << 8) | d[pos+1]; pos += 2
            if pos >= len(d): return pos - start, decoded
            pos += 1  # value byte
            decoded += count
        elif n == 128:
            # long literal
            if pos + 2 >= len(d): return pos - start, decoded
            count = (d[pos] << 8) | d[pos+1]; pos += 2
            if pos + count > len(d): return pos - start, decoded
            pos += count
            decoded += count
        else:
            # literal run of 256-n bytes
            count = 256 - n
            if pos + count > len(d): return pos - start, decoded
            pos += count
            decoded += count
    
    return pos - start, decoded


def decode_tile(d, pos, tw, th, bpp):
    """Decode a full tile (all channels). Returns (bytes_consumed, success)."""
    npixels = tw * th
    total_consumed = 0
    for ch in range(bpp):
        consumed, decoded = decode_rle_channel(d, pos, npixels)
        total_consumed += consumed
        pos += consumed
        if decoded != npixels:
            return total_consumed, False, f"channel {ch}: decoded {decoded} pixels, expected {npixels}"
    return total_consumed, True, "OK"


def main():
    path = sys.argv[1]
    with open(path, 'rb') as f:
        d = f.read()
    
    print(f"File: {path}")
    print(f"Size: {len(d):,} bytes")
    
    # Parse header
    pos = 14
    canvas_w = u32(d, pos); pos += 4
    canvas_h = u32(d, pos); pos += 4
    base_type = u32(d, pos); pos += 4
    precision = u32(d, pos); pos += 4
    pos = skip_properties(d, pos)
    
    # Read layer table
    layer_offsets = []
    while pos + 8 <= len(d):
        off = u64(d, pos); pos += 8
        if off == 0: break
        layer_offsets.append(off)
    
    # Skip channel table
    while pos + 8 <= len(d):
        off = u64(d, pos); pos += 8
        if off == 0: break
    
    print(f"\nRaw layer table ({len(layer_offsets)} entries):")
    for i, off in enumerate(layer_offsets):
        print(f"  [{i:2d}] 0x{off:08X}  first bytes: {d[off:off+20].hex()}")
    
    # Parse each layer and validate tiles
    for li, loff in enumerate(layer_offsets):
        if loff >= len(d) - 20: continue
        
        w = u32(d, loff); h = u32(d, loff+4); lt = u32(d, loff+8); nlen = u32(d, loff+12)
        if nlen == 0 or nlen > 10000 or loff + 16 + nlen > len(d): continue
        name = d[loff+16:loff+16+nlen-1].decode('utf-8', errors='replace')
        
        print(f"\n{'='*60}")
        print(f"Layer {li}: \"{name}\" ({w}x{h}) at 0x{loff:X}")
        
        # Skip to hierarchy offset
        prop_end = skip_properties(d, loff + 16 + nlen)
        if prop_end + 16 > len(d): 
            print("  CAN'T READ hier/mask"); continue
        
        hier_off = u64(d, prop_end)
        if hier_off == 0 or hier_off >= len(d) - 12:
            print(f"  INVALID hier_off=0x{hier_off:X}"); continue
        
        hw = u32(d, hier_off); hh = u32(d, hier_off+4); hbpp = u32(d, hier_off+8)
        if hw != w or hh != h or hbpp not in (1,2,3,4):
            print(f"  HIERARCHY MISMATCH: {hw}x{hh} bpp={hbpp}"); continue
        
        # Read level 0 offset
        lvl0_off = u64(d, hier_off + 12)
        if lvl0_off == 0 or lvl0_off >= len(d) - 8:
            print(f"  INVALID level0 offset: 0x{lvl0_off:X}"); continue
        
        lw = u32(d, lvl0_off); lh = u32(d, lvl0_off+4)
        if lw != w or lh != h:
            print(f"  LEVEL0 W/H MISMATCH: {lw}x{lh}"); continue
        
        # Read tile offsets
        tp = lvl0_off + 8
        tiles = []
        while tp + 8 <= len(d):
            to = u64(d, tp); tp += 8
            if to == 0: break
            tiles.append(to)
        
        tiles_x = (w + 63) // 64
        tiles_y = (h + 63) // 64
        expected = tiles_x * tiles_y
        
        print(f"  {len(tiles)} tiles (expected {expected}), bpp={hbpp}")
        
        if len(tiles) != expected:
            print(f"  TILE COUNT MISMATCH!")
            continue
        
        # Validate each tile by decoding RLE
        fail_count = 0
        first_fail = None
        for ti, toff in enumerate(tiles):
            if toff == 0 or toff >= len(d):
                if first_fail is None:
                    first_fail = (ti, f"offset 0x{toff:X} out of bounds")
                fail_count += 1
                continue
            
            # Compute tile dimensions
            tx = ti % tiles_x
            ty = ti // tiles_x
            tw = min(64, w - tx * 64)
            th = min(64, h - ty * 64)
            
            consumed, ok, msg = decode_tile(d, toff, tw, th, hbpp)
            if not ok:
                fail_count += 1
                if first_fail is None:
                    first_fail = (ti, f"at 0x{toff:X}: {msg} (consumed {consumed} bytes)")
                if fail_count <= 3:
                    print(f"  tile[{ti}] FAIL at 0x{toff:X}: {msg}")
        
        if fail_count == 0:
            print(f"  ALL {len(tiles)} tiles decode OK")
        else:
            print(f"  {fail_count}/{len(tiles)} tiles FAILED")
            if first_fail:
                print(f"  First failure: tile[{first_fail[0]}]: {first_fail[1]}")


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.xcf>")
        sys.exit(1)
    main()

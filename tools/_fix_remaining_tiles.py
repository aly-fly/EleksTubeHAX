#!/usr/bin/env python3
"""Check specific failing tiles and fix them."""
import struct, sys, os
os.environ.setdefault('PYTHONIOENCODING', 'utf-8')

def u32(d, pos): return struct.unpack('>I', d[pos:pos+4])[0]
def u64(d, pos): return struct.unpack('>Q', d[pos:pos+8])[0]
def p64(v): return struct.pack('>Q', v)

def skip_properties(d, pos):
    while pos + 8 <= len(d):
        pt = u32(d, pos); ps = u32(d, pos + 4); pos += 8
        if pt == 0: break
        if pos + ps > len(d): break
        pos += ps
    return pos

def encode_rle_channel_zeros(npixels):
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
    npixels = tw * th
    return encode_rle_channel_zeros(npixels) * bpp

def decode_rle_channel(d, pos, npixels):
    start = pos; decoded = 0
    while decoded < npixels:
        if pos >= len(d): return pos - start, decoded
        n = d[pos]; pos += 1
        if n <= 126:
            if pos >= len(d): return pos - start, decoded
            pos += 1; decoded += n + 1
        elif n == 127:
            if pos + 3 > len(d): return pos - start, decoded
            count = (d[pos] << 8) | d[pos+1]; pos += 2; pos += 1; decoded += count
        elif n == 128:
            if pos + 2 > len(d): return pos - start, decoded
            count = (d[pos] << 8) | d[pos+1]; pos += 2
            if pos + count > len(d): return pos - start, decoded
            pos += count; decoded += count
        else:
            count = 256 - n
            if pos + count > len(d): return pos - start, decoded
            pos += count; decoded += count
    return pos - start, decoded

def try_decode_tile(d, pos, tw, th, bpp):
    npixels = tw * th; total = 0
    for ch in range(bpp):
        consumed, decoded = decode_rle_channel(d, pos + total, npixels)
        total += consumed
        if decoded != npixels:
            return total, False, f"ch{ch}:{decoded}/{npixels}"
    return total, True, "OK"

# Failing tiles from validator:
# Layer 4 tile[449] at 0x71344 (53x53 edge tile? or 2572x757)
# Layer 5 tile[490] at 0x96E80 
# Layer 6 tile[89] at 0x18F8EE
# Layer 6 tile[104] at 0x1B790C  

def main():
    path = sys.argv[1]
    with open(path, 'rb') as f:
        d = bytearray(f.read())
    
    failing = [
        (4, 449, 2572, 757, 4),
        (5, 490, 2572, 757, 4),
        (6, 89, 900, 554, 4),
        (6, 104, 900, 554, 4),
    ]
    
    # Parse to find layers
    pos = 14 + 4 + 4 + 4 + 4
    pos = skip_properties(d, pos)
    layer_offsets = []
    while pos + 8 <= len(d):
        off = u64(d, pos); pos += 8
        if off == 0: break
        layer_offsets.append(off)
    
    for li, ti, w, h, bpp in failing:
        loff = layer_offsets[li]
        nlen = u32(d, loff + 12)
        prop_end = skip_properties(d, loff + 16 + nlen)
        hier_off = u64(d, prop_end)
        lvl0_off = u64(d, hier_off + 12)
        
        # Read tile offset for this tile
        tp = lvl0_off + 8
        tile_offsets = []
        while tp + 8 <= len(d):
            to = u64(d, tp); tp += 8
            if to == 0: break
            tile_offsets.append(to)
        
        tiles_x = (w + 63) // 64
        toff = tile_offsets[ti]
        
        tx = ti % tiles_x
        ty = ti // tiles_x
        tw = min(64, w - tx * 64)
        th = min(64, h - ty * 64)
        
        consumed, ok, msg = try_decode_tile(d, toff, tw, th, bpp)
        print(f"Layer {li} tile[{ti}]: {tw}x{th} at 0x{toff:X}, decode={ok} ({msg}), consumed={consumed}")
        print(f"  first bytes: {d[toff:toff+32].hex()}")
        
        if not ok:
            # Write transparent tile
            transparent = make_transparent_tile(tw, th, bpp)
            print(f"  transparrent tile size: {len(transparent)} bytes")
            d[toff:toff+len(transparent)] = transparent
            
            # Verify
            consumed2, ok2, msg2 = try_decode_tile(d, toff, tw, th, bpp)
            print(f"  after fix: decode={ok2} ({msg2})")
    
    with open(path, 'wb') as f:
        f.write(d)
    print(f"\nPatched and saved: {path}")

if __name__ == '__main__':
    main()

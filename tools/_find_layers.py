"""
Find all 13 layer headers in the corrupted file by:
1. Scanning near each stored offset for a valid layer header
2. Using known layer names from clean file with incremental search
"""
import struct, sys, os
os.environ.setdefault('PYTHONIOENCODING', 'utf-8')
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def u32(d, pos): return struct.unpack('>I', d[pos:pos+4])[0]
def u64(d, pos): return struct.unpack('>Q', d[pos:pos+8])[0]

with open('docs/Hardware MarvelTubes Mini/Kopie-CORRUPTED.xcf','rb') as f:
    corrupt = f.read()

# Layer table entries
offsets = [0x1726, 0x13E15, 0x2423E, 0x3A33C, 0x46600, 0x737E3,
           0x9890A, 0x1F7F89, 0x25C8E7, 0x1DA4BEA, 0x1DD0EFD, 0x1E3366A, 0x207F91C]

def is_valid_layer_header(d, pos):
    """Check if position looks like a layer header."""
    if pos < 0 or pos + 20 >= len(d):
        return None
    w = u32(d, pos)
    h = u32(d, pos+4)
    lt = u32(d, pos+8)
    nlen = u32(d, pos+12)
    
    if w == 0 or h == 0 or w > 65536 or h > 65536:
        return None
    if lt > 10:  # layer type
        return None
    if nlen == 0 or nlen > 500:
        return None
    if pos + 16 + nlen > len(d):
        return None
    
    # Name should be printable + null terminated
    name_bytes = d[pos+16:pos+16+nlen]
    if name_bytes[-1] != 0:
        return None
    name = name_bytes[:-1]
    try:
        name_str = name.decode('utf-8')
    except:
        return None
    
    # Name should be mostly printable
    if not all(32 <= b < 127 or b > 127 for b in name):
        return None
    
    return {
        'w': w, 'h': h, 'type': lt, 'nlen': nlen, 'name': name_str,
        'offset': pos
    }

print("=== SEARCHING NEAR EACH STORED OFFSET ===")
# For each stored offset, search -2000 to +2000 for a valid layer header
found_layers = {}  # index -> layer info

for i, stored_off in enumerate(offsets):
    best = None
    search_range = 2000 if i < 6 else 200000  # Wider range for later layers
    
    for delta in range(-search_range, search_range):
        pos = stored_off + delta
        info = is_valid_layer_header(corrupt, pos)
        if info:
            # Prefer closest match
            if best is None or abs(delta) < abs(best['delta']):
                best = {**info, 'delta': delta}
    
    if best:
        found_layers[i] = best
        print(f"  Entry {i}: stored=0x{stored_off:X} found=0x{best['offset']:X} delta={best['delta']:+d} "
              f"\"{best['name']}\" ({best['w']}x{best['h']})")
    else:
        print(f"  Entry {i}: stored=0x{stored_off:X} NO VALID HEADER FOUND in +/-{search_range}")

# Also do a full scan for ALL valid layer headers
print("\n=== FULL SCAN FOR ALL LAYER HEADERS ===")
all_headers = []
# Layer headers have w,h,type at known canvas size (2572x757) or smaller
# Scan every position is too slow. Instead, search for known patterns.

# The canvas is 2572x757 = 0x0A0C x 0x02F5
# Many layers are this size, so search for the byte pattern
target_w = struct.pack('>I', 2572)  # \x00\x00\x0a\x0c
target_h = struct.pack('>I', 757)   # \x00\x00\x02\xf5

# Search for w+h pattern at aligned positions
start = 0
full_size_headers = []
while start < len(corrupt) - 20:
    pos = corrupt.find(target_w + target_h, start)
    if pos == -1:
        break
    info = is_valid_layer_header(corrupt, pos)
    if info:
        full_size_headers.append(info)
    start = pos + 1

print(f"  Found {len(full_size_headers)} full-canvas-size layer headers:")
for info in full_size_headers:
    print(f"    0x{info['offset']:X}: \"{info['name']}\" ({info['w']}x{info['h']})")

# Also try smaller known sizes
for w, h in [(195, 244), (268, 316), (900, 554), (280, 519), (4274, 2678), 
             (424, 328), (1096, 752), (4328, 2714)]:
    tw = struct.pack('>I', w)
    th = struct.pack('>I', h)
    start = 0
    while start < len(corrupt) - 20:
        pos = corrupt.find(tw + th, start)
        if pos == -1:
            break
        info = is_valid_layer_header(corrupt, pos)
        if info:
            print(f"    0x{info['offset']:X}: \"{info['name']}\" ({info['w']}x{info['h']})")
        start = pos + 1

# Search specifically for the ESP32-C3 layer
print("\n=== SEARCHING FOR ESP32-C3 LAYER ===")
# We know it's at 0x1DA48C7 and has w=267, h=222
for w, h in [(267, 222), (266, 222), (268, 222), (267, 221), (267, 223)]:
    tw = struct.pack('>I', w)
    th = struct.pack('>I', h)
    start = 0
    while start < len(corrupt) - 20:
        pos = corrupt.find(tw + th, start)
        if pos == -1:
            break
        info = is_valid_layer_header(corrupt, pos)
        if info:
            print(f"  0x{info['offset']:X}: \"{info['name']}\" ({info['w']}x{info['h']}) type={info['type']}")
        start = pos + 1

# What about Farbvertauschung?
print("\n=== SEARCHING FOR 'Farbvertauschung' LAYER ===")
# Search for the exact name
search_name = b"Farbvertauschung\x00"
start = 0
while True:
    pos = corrupt.find(search_name, start)
    if pos == -1: break
    # Check if it's preceded by a valid nlen
    if pos >= 4:
        nlen = u32(corrupt, pos - 4)
        if nlen == len(search_name):
            # Check for layer header
            info = is_valid_layer_header(corrupt, pos - 16)
            if info:
                print(f"  Layer header at 0x{pos-16:X}: \"{info['name']}\" ({info['w']}x{info['h']})")
            else:
                print(f"  Name at 0x{pos:X} with valid nlen={nlen} but no valid layer header at 0x{pos-16:X}")
                # Show surrounding bytes
                hdr = corrupt[pos-16:pos+20]
                print(f"    header bytes: {hdr.hex()}")
    start = pos + 1

# Let's also look at what's near the stored offset for entries 6-12
print("\n=== DETAILED LOOK AT ENTRIES 6-12 ===")
for i in range(6, 13):
    off = offsets[i]
    # Show 64 bytes at and around the stored offset
    for delta in [-100, -50, -20, -10, -5, -3, -2, -1, 0, 1, 2]:
        pos = off + delta
        if 0 <= pos < len(corrupt) - 20:
            info = is_valid_layer_header(corrupt, pos)
            if info:
                print(f"  Entry {i}: delta={delta:+d} at 0x{pos:X}: \"{info['name']}\" ({info['w']}x{info['h']})")

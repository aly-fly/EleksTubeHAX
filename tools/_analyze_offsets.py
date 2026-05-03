"""
Analyze the corrupted XCF file to find where each layer's data ACTUALLY is,
then determine the offset corrections needed.

Strategy: the layer table offsets point to ORIGINAL positions. The actual data
was shifted by CR removal. We can find the real positions by searching for
the layer name strings (which don't contain 0x0D0A sequences).
"""
import struct, sys, os
os.environ.setdefault('PYTHONIOENCODING', 'utf-8')
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def u32(d, pos): return struct.unpack('>I', d[pos:pos+4])[0]
def u64(d, pos): return struct.unpack('>Q', d[pos:pos+8])[0]

with open('docs/Hardware MarvelTubes Mini/Kopie-CORRUPTED.xcf','rb') as f:
    corrupt = f.read()
with open('docs/Hardware MarvelTubes Mini/MarvelTubesMini - Front and back - Kopie.xcf','rb') as f:
    clean = f.read()

print("=== CLEAN FILE LAYER TABLE ===")
# Parse clean
pos = 14
pos += 4 + 4 + 4  # w, h, base_type
ver_str = clean[9:13].replace(b'v',b'').replace(b'\x00',b'')
ver = int(ver_str.decode())
if ver >= 4: pos += 4
while pos < len(clean)-8:
    pt = u32(clean, pos); ps = u32(clean, pos+4); pos += 8
    if pt == 0: break
    pos += ps
clean_layers = []
while pos < len(clean)-8:
    off = u64(clean, pos); pos += 8
    if off == 0: break
    clean_layers.append(off)
    w = u32(clean, off); h = u32(clean, off+4); nlen = u32(clean, off+12)
    name = clean[off+16:off+16+nlen-1].decode('utf-8','replace')
    print(f"  Layer at 0x{off:X}: {w}x{h} nlen={nlen} \"{name}\"")

print(f"\n=== CORRUPTED FILE LAYER TABLE ===")
pos = 14 + 4 + 4 + 4
ver_str = corrupt[9:13].replace(b'v',b'').replace(b'\x00',b'')
ver = int(ver_str.decode())
if ver >= 4: pos += 4
while pos < len(corrupt)-8:
    pt = u32(corrupt, pos); ps = u32(corrupt, pos+4); pos += 8
    if pt == 0: break
    pos += ps
corrupt_layer_offsets = []
while pos < len(corrupt)-8:
    off = u64(corrupt, pos); pos += 8
    if off == 0: break
    corrupt_layer_offsets.append(off)

print(f"  Table starts at 0x{pos - len(corrupt_layer_offsets)*8 - 8:X}")
for i, off in enumerate(corrupt_layer_offsets):
    print(f"  Entry {i}: offset 0x{off:X}")

# Known layer names from the full edited file
# From the clean file we know some names, and from partial corrupt parsing we got more
known_names = [
    b"ESP32 Diagramm small\x00",
    b"ESP32 Diagramm big\x00",
    b"Wiring - pins\x00",
    b"Wiring - ASSumes\x00",
    b"Wiring - top\x00",
    b"Wiring - bottom\x00",
    b"Farbvertauschung\x00",  # new layer (added in edits)
    b"Photo\x00",
    b"Photo - Pin names\x00",
    b"Photo - color\x00",
    b"Photo - cropped\x00",
    b"Photo - undistorted\x00",
    b"Background\x00",
    b"ESP32-C3-Mini-1",  # possible new layer name
]

print("\n=== SEARCHING FOR LAYER NAMES IN CORRUPTED FILE ===")
for name in known_names:
    positions = []
    start = 0
    while True:
        pos = corrupt.find(name, start)
        if pos == -1: break
        positions.append(pos)
        start = pos + 1
    if positions:
        for p in positions:
            # Check if this looks like a layer header (name is preceded by nlen = len(name))
            if p >= 16:
                nlen_pos = p - 16 + 12  # nlen is at offset+12, name at offset+16
                # Actually, layer header is: w(4) h(4) type(4) nlen(4) name(nlen)
                # So name starts at offset+16. nlen = len(name). 
                # nlen_pos is at p - 4
                nlen_at = u32(corrupt, p - 4)
                w_at = u32(corrupt, p - 16)
                h_at = u32(corrupt, p - 12)
                type_at = u32(corrupt, p - 8)
                
                is_header = (nlen_at == len(name) and 
                             0 < w_at <= 65536 and 0 < h_at <= 65536 and
                             type_at in (0, 1, 2, 3, 4))
                
                if is_header:
                    real_off = p - 16
                    print(f"  \"{name[:-1].decode()}\" found at 0x{p:X}, layer header at 0x{real_off:X} [{w_at}x{h_at}]")
                else:
                    print(f"  \"{name[:-1].decode()}\" at 0x{p:X} (not a valid layer header: w={w_at} h={h_at} type={type_at} nlen={nlen_at})")
            else:
                print(f"  \"{name[:-1].decode()}\" at 0x{p:X} (too early for header)")

print("\n=== OFFSET CORRECTIONS ===")
# For each layer, compare stored offset vs actual offset
# Search for each name and try to match with the layer table entries

# Build list of actual layer positions by searching
actual_positions = {}
for name in known_names:
    start = 0
    while True:
        pos = corrupt.find(name, start)
        if pos == -1: break
        if pos >= 16:
            nlen_at = u32(corrupt, pos - 4)
            w_at = u32(corrupt, pos - 16)
            h_at = u32(corrupt, pos - 12)
            if nlen_at == len(name) and 0 < w_at <= 65536 and 0 < h_at <= 65536:
                namestr = name[:-1].decode('utf-8', 'replace')
                actual_positions[namestr] = pos - 16
        start = pos + 1

print(f"\nFound {len(actual_positions)} valid layer headers:")
for name, pos in sorted(actual_positions.items(), key=lambda x: x[1]):
    # Find which layer table entry is closest
    best_idx = -1
    best_delta = None
    for i, off in enumerate(corrupt_layer_offsets):
        delta = pos - off  # negative means real pos is before stored offset
        if best_delta is None or abs(delta) < abs(best_delta):
            best_delta = delta
            best_idx = i
    print(f"  \"{name}\" actual=0x{pos:X} closest entry[{best_idx}]=0x{corrupt_layer_offsets[best_idx]:X} delta={best_delta}")

# Also search for the layer name pattern with "ESP32-C3" 
for pattern in [b"ESP32-C3", b"Farbvert"]:
    positions = []
    start = 0
    while True:
        pos = corrupt.find(pattern, start)
        if pos == -1: break
        positions.append(pos)
        start = pos + 1
    print(f"\n  Pattern \"{pattern.decode()}\" found at: {['0x%X' % p for p in positions[:10]]}")

import struct

def u64(d, p):
    return struct.unpack('>Q', d[p:p+8])[0]

with open('docs/Hardware MarvelTubes Mini/Kopie-CORRUPTED.xcf', 'rb') as f:
    c = f.read()
with open('docs/Hardware MarvelTubes Mini/Kopie-RESTORED.xcf', 'rb') as f:
    r = f.read()
with open('docs/Hardware MarvelTubes Mini/MarvelTubesMini - Front and back - Kopie.xcf', 'rb') as f:
    clean = f.read()

# Find first 10 diffs between restored and corrupted
diffs = 0
for i in range(min(len(c), len(r))):
    if r[i] != c[i]:
        ctx_c = c[max(0,i-8):i+8].hex(' ')
        ctx_r = r[max(0,i-8):i+8].hex(' ')
        ctx_cl = clean[max(0,i-8):min(i+8,len(clean))].hex(' ')
        print(f"Diff at byte {i} (0x{i:X}):")
        print(f"  corrupt:  {ctx_c}")
        print(f"  restored: {ctx_r}")
        print(f"  clean:    {ctx_cl}")
        diffs += 1
        if diffs >= 10:
            break

# Show layer table from corrupted
print("\n--- Corrupted layer table ---")
pos = 14 + 4 + 4 + 4 + 4  # header + w + h + type + precision
# Skip properties
while pos < len(c) - 8:
    pt = struct.unpack('>I', c[pos:pos+4])[0]
    ps = struct.unpack('>I', c[pos+4:pos+8])[0]
    pos += 8
    if pt == 0:
        break
    pos += ps

print(f"Layer table starts at 0x{pos:X}")
for i in range(15):
    if pos + 8 > len(c):
        break
    off = u64(c, pos)
    off_bytes = c[pos:pos+8].hex(' ')
    print(f"  0x{pos:X}: {off_bytes} -> 0x{off:X}")
    pos += 8
    if off == 0:
        break

# Show same region in restored
print("\n--- Restored same region ---")
pos = 14 + 4 + 4 + 4 + 4
while pos < len(r) - 8:
    pt = struct.unpack('>I', r[pos:pos+4])[0]
    ps = struct.unpack('>I', r[pos+4:pos+8])[0]
    pos += 8
    if pt == 0:
        break
    pos += ps

print(f"Layer table starts at 0x{pos:X}")
for i in range(15):
    if pos + 8 > len(r):
        break
    off = u64(r, pos)
    off_bytes = r[pos:pos+8].hex(' ')
    print(f"  0x{pos:X}: {off_bytes} -> 0x{off:X}")
    pos += 8
    if off == 0:
        break

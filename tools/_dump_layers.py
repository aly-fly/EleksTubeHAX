import struct, sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def u32(d, pos): return struct.unpack('>I', d[pos:pos+4])[0]
def u64(d, pos): return struct.unpack('>Q', d[pos:pos+8])[0]

with open('docs/Hardware MarvelTubes Mini/Kopie-CORRUPTED.xcf','rb') as f: d=f.read()

# Layer table
pos = 0x16A6
for i in range(14):
    off = u64(d, pos)
    pos += 8
    if off == 0:
        print(f'Layer table entry {i}: 0 (end)')
        break
    w = u32(d, off); h = u32(d, off+4); lt= u32(d, off+8); nlen = u32(d, off+12)
    if nlen > 0 and nlen < 10000:
        name = d[off+16:off+16+nlen-1].decode('utf-8','replace')
    else:
        name = f'(nlen={nlen})'
    print(f'Layer {i}: off=0x{off:X} w={w} h={h} type={lt} nlen={nlen} name="{name}"')
    header = d[off:off+40]
    print(f'  bytes: {header.hex()}')

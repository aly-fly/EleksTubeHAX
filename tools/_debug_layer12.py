import struct, sys, os
os.environ.setdefault('PYTHONIOENCODING', 'utf-8')
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def u32(d,p): return struct.unpack('>I',d[p:p+4])[0]
def u64(d,p): return struct.unpack('>Q',d[p:p+8])[0]

d = open('docs/Hardware MarvelTubes Mini/Kopie-CORRUPTED.xcf','rb').read()

level_pos = 0x207F726
lw = u32(d, level_pos); lh = u32(d, level_pos+4)
print(f'Level: {lw}x{lh}')
tiles_x = (lw + 63) // 64
tiles_y = (lh + 63) // 64
expected = tiles_x * tiles_y
print(f'Expected: {expected} tiles ({tiles_x}x{tiles_y})')

tp = level_pos + 8
count = 0
max_dist = 0
in_file = 0
while tp + 8 <= len(d):
    to = u64(d, tp); tp += 8
    if to == 0: break
    count += 1
    valid = 0 < to < len(d)
    if valid:
        in_file += 1
        dist = abs(to - level_pos)
        if dist > max_dist:
            max_dist = dist
    if count <= 5 or count == expected:
        flag = "OK" if valid and dist < 0x2000000 else "FAR" if valid else "INVALID"
        print(f'  tile[{count-1}] at 0x{to:X} {flag}')

print(f'Total: {count} tiles, expected: {expected}')
print(f'In file: {in_file}/{count}')
print(f'Max distance: 0x{max_dist:X} ({max_dist:,})')
print(f'Tile validation threshold 0x2000000 = {0x2000000:,}')
print(f'Layer "bottom" is 4328x2714 = large, tile data could be 30+MB')

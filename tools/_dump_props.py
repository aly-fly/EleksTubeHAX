#!/usr/bin/env python3
"""Dump all image properties from an XCF file — specifically looking for 
PROP_ITEM_SET and related properties that GIMP 3.x (v022) uses for layer tree."""
import struct, sys, os
os.environ.setdefault('PYTHONIOENCODING', 'utf-8')
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def u32(d, pos): return struct.unpack('>I', d[pos:pos+4])[0]
def u64(d, pos): return struct.unpack('>Q', d[pos:pos+8])[0]

# XCF property types (from GIMP source xcf-private.h)
PROP_NAMES = {
    0: "PROP_END",
    1: "PROP_COLORMAP",
    2: "PROP_ACTIVE_LAYER",
    3: "PROP_ACTIVE_CHANNEL",
    4: "PROP_SELECTION",
    5: "PROP_FLOATING_SELECTION",
    6: "PROP_OPACITY",
    7: "PROP_MODE",
    8: "PROP_VISIBLE",
    9: "PROP_LINKED",
    10: "PROP_LOCK_ALPHA",
    11: "PROP_APPLY_MASK",
    12: "PROP_EDIT_MASK",
    13: "PROP_SHOW_MASK",
    14: "PROP_SHOW_ALL",  # v14+
    15: "PROP_OFFSETS",
    16: "PROP_COLOR",
    17: "PROP_COMPRESSION",
    18: "PROP_GUIDES",
    19: "PROP_RESOLUTION",
    20: "PROP_TATTOO",
    21: "PROP_PARASITES",
    22: "PROP_UNIT",
    23: "PROP_PATHS",
    24: "PROP_USER_UNIT",
    25: "PROP_VECTORS",
    26: "PROP_TEXT_LAYER_FLAGS",
    27: "PROP_OLD_SAMPLE_POINTS",  
    28: "PROP_LOCK_CONTENT",
    29: "PROP_GROUP_ITEM",
    30: "PROP_ITEM_PATH",
    31: "PROP_GROUP_ITEM_FLAGS",
    32: "PROP_LOCK_POSITION",
    33: "PROP_FLOAT_OPACITY",
    34: "PROP_COLOR_TAG",
    35: "PROP_COMPOSITE_MODE",
    36: "PROP_COMPOSITE_SPACE",
    37: "PROP_BLEND_SPACE",
    38: "PROP_FLOAT_COLOR",
    39: "PROP_SAMPLE_POINTS",
    40: "PROP_ITEM_SET",
    41: "PROP_ITEM_SET_ITEM",
}

def dump_properties(d, pos, label=""):
    print(f"\n  --- Properties ({label}) starting at 0x{pos:X} ---")
    prop_count = 0
    while pos + 8 <= len(d):
        pt = u32(d, pos)
        ps = u32(d, pos + 4)
        pname = PROP_NAMES.get(pt, f"UNKNOWN_{pt}")
        
        if pt == 0:
            print(f"  0x{pos:08X}: PROP_END (size={ps})")
            pos += 8
            break
        
        print(f"  0x{pos:08X}: {pname} (type={pt}, size={ps})")
        
        data_start = pos + 8
        data = d[data_start:data_start + min(ps, 200)]
        
        # Decode specific property types
        if pt == 6:  # OPACITY
            if ps >= 4:
                print(f"    opacity = {u32(d, data_start)}")
        elif pt == 7:  # MODE
            if ps >= 4:
                print(f"    mode = {u32(d, data_start)}")
        elif pt == 8:  # VISIBLE
            if ps >= 4:
                print(f"    visible = {u32(d, data_start)}")
        elif pt == 15:  # OFFSETS
            if ps >= 8:
                print(f"    offsets = ({struct.unpack('>ii', d[data_start:data_start+8])})")
        elif pt == 17:  # COMPRESSION
            if ps >= 1:
                print(f"    compression = {d[data_start]}")
        elif pt == 33:  # FLOAT_OPACITY
            if ps >= 4:
                print(f"    float_opacity = {struct.unpack('>f', d[data_start:data_start+4])[0]}")
        elif pt == 40:  # PROP_ITEM_SET
            print(f"    *** ITEM SET DATA ({ps} bytes) ***")
            # Item set: first 4 bytes = set type (0=named, 1=anonymous)
            # If named: followed by name string
            # Then: 4 bytes = number of items, followed by item indices (4 bytes each)
            if ps >= 4:
                set_type = u32(d, data_start)
                print(f"    set_type = {set_type}")
                inner = data_start + 4
                if set_type == 0:  # named
                    nlen = u32(d, inner)
                    inner += 4
                    name = d[inner:inner+nlen-1].decode('utf-8', errors='replace') if nlen > 0 else ""
                    print(f"    name = \"{name}\" (len={nlen})")
                    inner += nlen
                # Number of children
                if inner + 4 <= data_start + ps:
                    n_items = u32(d, inner)
                    print(f"    n_items = {n_items}")
                    inner += 4
                    for ii in range(min(n_items, 50)):
                        if inner + 4 <= data_start + ps:
                            item_idx = u32(d, inner)
                            print(f"    item[{ii}] = {item_idx}")
                            inner += 4
            print(f"    raw hex: {data[:64].hex()}")
        elif pt == 41:  # PROP_ITEM_SET_ITEM
            print(f"    *** ITEM SET ITEM ({ps} bytes) ***")
            if ps >= 4:
                set_idx = u32(d, data_start)
                print(f"    set_index = {set_idx}")
            print(f"    raw hex: {data[:32].hex()}")
        elif pt == 21:  # PARASITES
            print(f"    parasites data ({ps} bytes)")
            # Parse individual parasites
            pp = data_start
            while pp + 4 < data_start + ps:
                pnlen = u32(d, pp)
                pp += 4
                if pnlen == 0 or pnlen > 500 or pp + pnlen > data_start + ps:
                    break
                pname_s = d[pp:pp+pnlen-1].decode('utf-8', errors='replace')
                pp += pnlen
                if pp + 8 > data_start + ps: break
                pflags = u32(d, pp); pp += 4
                pdlen = u32(d, pp); pp += 4
                print(f"    parasite: \"{pname_s}\" flags={pflags} datalen={pdlen}")
                pp += pdlen
        elif pt == 30:  # PROP_ITEM_PATH
            print(f"    *** ITEM PATH ({ps} bytes) ***")
            if ps >= 4:
                n_components = u32(d, data_start)
                print(f"    n_components = {n_components}")
                inner = data_start + 4
                for ci in range(min(n_components, 20)):
                    if inner + 4 <= data_start + ps:
                        comp = u32(d, inner)
                        print(f"    component[{ci}] = {comp}")
                        inner += 4
            print(f"    raw hex: {data[:32].hex()}")
        elif pt == 29:  # PROP_GROUP_ITEM
            print(f"    *** GROUP ITEM flag ***")
        else:
            if ps <= 64:
                print(f"    hex: {data.hex()}")
        
        pos = data_start + ps
        prop_count += 1
        if prop_count > 100:
            print(f"  ... stopping after 100 properties")
            break
    
    return pos


def main():
    for path in sys.argv[1:]:
        print(f"\n{'='*60}")
        print(f"FILE: {path}")
        print(f"{'='*60}")
        
        with open(path, 'rb') as f:
            d = f.read()
        
        pos = 14
        canvas_w = u32(d, pos); pos += 4
        canvas_h = u32(d, pos); pos += 4
        base_type = u32(d, pos); pos += 4
        precision = u32(d, pos); pos += 4
        print(f"Canvas: {canvas_w}x{canvas_h}, type={base_type}, precision={precision}")
        
        pos = dump_properties(d, pos, "IMAGE")
        
        print(f"\nAfter image properties: 0x{pos:X}")
        
        # Read layer offset table
        layer_offsets = []
        while pos + 8 <= len(d):
            off = u64(d, pos)
            pos += 8
            if off == 0: break
            layer_offsets.append(off)
        
        print(f"Layer table: {len(layer_offsets)} entries")
        
        # Skip channel table
        while pos + 8 <= len(d):
            off = u64(d, pos); pos += 8
            if off == 0: break
        
        # Parse each layer's properties
        for li, loff in enumerate(layer_offsets):
            if loff >= len(d) - 20:
                print(f"\nLayer {li}: OFFSET OUT OF BOUNDS 0x{loff:X}")
                continue
            
            w = u32(d, loff)
            h = u32(d, loff + 4)
            lt = u32(d, loff + 8)
            nlen = u32(d, loff + 12)
            if nlen == 0 or nlen > 10000 or loff + 16 + nlen > len(d):
                print(f"\nLayer {li}: BAD HEADER at 0x{loff:X}")
                continue
            
            name = d[loff+16:loff+16+nlen-1].decode('utf-8', errors='replace')
            print(f"\nLayer {li}: \"{name}\" ({w}x{h})")
            
            prop_start = loff + 16 + nlen
            dump_properties(d, prop_start, f"LAYER {li}")


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.xcf> [file2.xcf ...]")
        sys.exit(1)
    main()

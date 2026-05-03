#!/usr/bin/env python3
"""
Restore a corrupted XCF file by reinserting 0x0D bytes that git removed.

Strategy:
- Walk clean and corrupted files in parallel with two pointers
- In matching regions: use clean data directly (has correct CRLFs)
- At CRLF positions in clean where corrupted has just 0x0A: reinsert 0x0D
- In edited regions (where data diverges): use heuristics based on
  XCF RLE structure to determine which 0x0A need 0x0D prepended

The corrupted file = strip_cr(edited_original), so:
  restored = insert_cr_back(corrupted, guided_by=clean)
"""
import struct
import sys
import os

os.environ.setdefault('PYTHONIOENCODING', 'utf-8')
sys.stdout.reconfigure(encoding='utf-8', errors='replace')


def u32(d, pos):
    return struct.unpack('>I', d[pos:pos+4])[0]

def u64(d, pos):
    return struct.unpack('>Q', d[pos:pos+8])[0]


def strip_cr(data):
    """Remove all CR before LF — same transform git applied."""
    result = bytearray()
    i = 0
    while i < len(data):
        if i + 1 < len(data) and data[i] == 0x0D and data[i+1] == 0x0A:
            i += 1  # skip CR
        else:
            result.append(data[i])
            i += 1
    return bytes(result)


def build_cr_set(clean):
    """Build set of positions in clean file where 0x0D precedes 0x0A."""
    positions = set()
    i = 0
    while i < len(clean) - 1:
        if clean[i] == 0x0D and clean[i+1] == 0x0A:
            positions.add(i)
        i += 1
    return positions


def parallel_walk_restore(clean, corrupt):
    """
    Walk clean and corrupted in parallel.
    
    ci = clean index, xi = corrupted index
    When clean[ci] == 0x0D and clean[ci+1] == 0x0A and corrupt[xi] == 0x0A:
      -> This is a CR that was stripped. Output 0x0D 0x0A, advance ci by 2, xi by 1.
    When clean[ci] == corrupt[xi]:
      -> Same byte. Output it, advance both.
    When they differ:
      -> Edited region. Need special handling.
    """
    result = bytearray()
    ci = 0  # clean index
    xi = 0  # corrupt index
    
    total_reinserted = 0
    total_edited_0a = 0  # 0x0A in edited regions where we had to guess
    total_edited_0a_reinserted = 0
    
    match_run = 0
    
    while ci < len(clean) and xi < len(corrupt):
        # Check for CRLF in clean where corrupt has just LF
        if (ci + 1 < len(clean) and 
            clean[ci] == 0x0D and clean[ci+1] == 0x0A and
            corrupt[xi] == 0x0A):
            # Reinsert the CR
            result.append(0x0D)
            result.append(0x0A)
            ci += 2
            xi += 1
            total_reinserted += 1
            match_run = 0
            continue
        
        if clean[ci] == corrupt[xi]:
            # Matching byte
            result.append(corrupt[xi])
            ci += 1
            xi += 1
            match_run += 1
            continue
        
        # --- Divergence: edited region ---
        # Try to find resynchronization point
        # Strategy: scan ahead in both files for a matching run (>= 8 bytes)
        
        # First, collect the divergent chunk from corrupted file
        # Look ahead in clean to find where they resync
        best_resync = find_resync(clean, corrupt, ci, xi)
        
        if best_resync is not None:
            new_ci, new_xi = best_resync
            # Output the corrupted bytes from xi to new_xi, 
            # reinserting 0x0D before 0x0A where appropriate
            chunk = corrupt[xi:new_xi]
            restored_chunk = reinsert_cr_in_edited_region(chunk)
            result.extend(restored_chunk)
            
            edited_0a_count = chunk.count(0x0A)
            reinserted_count = len(restored_chunk) - len(chunk)
            total_edited_0a += edited_0a_count
            total_edited_0a_reinserted += reinserted_count
            
            ci = new_ci
            xi = new_xi
            match_run = 0
        else:
            # No resync found — append rest of corrupted with CR reinsertion
            chunk = corrupt[xi:]
            restored_chunk = reinsert_cr_in_edited_region(chunk)
            result.extend(restored_chunk)
            total_edited_0a += chunk.count(0x0A)
            total_edited_0a_reinserted += len(restored_chunk) - len(chunk)
            ci = len(clean)
            xi = len(corrupt)
            break
    
    # Append any remaining corrupted bytes
    if xi < len(corrupt):
        chunk = corrupt[xi:]
        restored_chunk = reinsert_cr_in_edited_region(chunk)
        result.extend(restored_chunk)
        total_edited_0a += chunk.count(0x0A)
        total_edited_0a_reinserted += len(restored_chunk) - len(chunk)
    
    print(f"  CRs reinserted at known positions: {total_reinserted}")
    print(f"  0x0A bytes in edited regions: {total_edited_0a}")
    print(f"  CRs reinserted in edited regions: {total_edited_0a_reinserted}")
    
    return bytes(result)


def find_resync(clean, corrupt, ci, xi, max_search=200000, min_match=16):
    """
    Find the next position where clean and corrupt resynchronize.
    
    Tries offsets in the clean file (ci+1, ci+2, ...) to find where
    the corrupted data at xi matches the clean data again.
    
    Also considers that some clean bytes might be CRLFs that map to just LF.
    """
    # Strategy: try small windows of corrupted data and search for them in clean
    # We know the divergence is because of edits, so the clean pointer needs
    # to advance past the old content, and corrupt pointer past the new content,
    # until they're back in sync.
    
    # Quick approach: scan corrupted from xi looking for a sequence that matches
    # somewhere in clean near ci.
    search_chunk_size = min_match
    
    for xi_try in range(xi + 1, min(xi + max_search, len(corrupt) - search_chunk_size)):
        chunk = corrupt[xi_try:xi_try + search_chunk_size]
        
        # Search in clean near the expected position
        # The clean pointer might be ahead or behind
        ci_search_start = max(0, ci - 1000)
        ci_search_end = min(len(clean), ci + max_search + 1000)
        
        search_pos = ci_search_start
        while True:
            found = clean.find(chunk, search_pos, ci_search_end)  # bytes.find
            if found == -1:
                break
            
            # Verify it's a real resync: check more bytes match
            extended_match = 0
            tc = found
            tx = xi_try
            while tc < len(clean) and tx < len(corrupt) and extended_match < 64:
                if clean[tc] == corrupt[tx]:
                    extended_match += 1
                    tc += 1
                    tx += 1
                elif (tc + 1 < len(clean) and 
                      clean[tc] == 0x0D and clean[tc+1] == 0x0A and
                      corrupt[tx] == 0x0A):
                    # CRLF vs LF — still in sync
                    extended_match += 1
                    tc += 2
                    tx += 1
                else:
                    break
            
            if extended_match >= min_match:
                return (found, xi_try)
            
            search_pos = found + 1
    
    return None


def reinsert_cr_in_edited_region(chunk):
    """
    In an edited region, every 0x0A that was originally preceded by 0x0D
    needs the CR reinserted.
    
    In XCF RLE tile data, byte values are essentially random (compressed
    pixel data), so EVERY 0x0A was likely preceded by 0x0D in the original.
    
    Exception: XCF structural data (property lists, offsets) uses big-endian
    integers where 0x0A can legitimately stand alone.
    
    Heuristic: reinsert 0x0D before ALL 0x0A bytes in edited regions.
    This is correct for RLE tile data. For structural edits (properties),
    it might occasionally be wrong, but property changes are only a few
    bytes and the probability of a false 0x0A there is low.
    """
    result = bytearray()
    for b in chunk:
        if b == 0x0A:
            result.append(0x0D)
        result.append(b)
    return bytes(result)


def verify_structure(data, label=""):
    """Quick structural check of the restored XCF."""
    if data[:9] != b'gimp xcf ':
        print(f"  {label} ERROR: Not a valid XCF file (bad magic)")
        return False
    
    ver_str = data[9:13].replace(b'v', b'').replace(b'\x00', b'')
    try:
        ver = int(ver_str.decode())
    except:
        print(f"  {label} ERROR: Cannot parse version")
        return False
    
    print(f"  {label} XCF version: v{ver:03d}")
    
    pos = 14
    w = u32(data, pos); pos += 4
    h = u32(data, pos); pos += 4
    bt = u32(data, pos); pos += 4
    print(f"  {label} Canvas: {w}x{h}, base_type={bt}")
    
    if ver >= 4:
        pos += 4  # precision
    
    # Skip properties
    while pos < len(data) - 8:
        pt = u32(data, pos); pos += 4
        ps = u32(data, pos); pos += 4
        if pt == 0:
            break
        pos += ps
    
    # Read layer offsets
    layer_count = 0
    while pos < len(data) - 8:
        off = u64(data, pos); pos += 8
        if off == 0:
            break
        layer_count += 1
        
        if off < len(data) - 20:
            lw = u32(data, off)
            lh = u32(data, off + 4)
            lt = u32(data, off + 8)
            nlen = u32(data, off + 12)
            if 0 < nlen < 500 and off + 16 + nlen <= len(data):
                name = data[off+16:off+16+nlen-1].decode('utf-8', errors='replace')
                print(f"  {label}   Layer {layer_count}: \"{name}\" ({lw}x{lh})")
            else:
                print(f"  {label}   Layer {layer_count}: @ 0x{off:X} (name parse failed, nlen={nlen})")
        else:
            print(f"  {label}   Layer {layer_count}: @ 0x{off:X} (out of bounds!)")
    
    print(f"  {label} Total layers: {layer_count}")
    return layer_count > 0


def main():
    clean_path = sys.argv[1]
    corrupt_path = sys.argv[2]
    output_path = sys.argv[3] if len(sys.argv) > 3 else corrupt_path.replace('.xcf', '-RESTORED.xcf')
    
    print(f"Clean:     {clean_path}")
    print(f"Corrupted: {corrupt_path}")
    print(f"Output:    {output_path}")
    
    with open(clean_path, 'rb') as f:
        clean = f.read()
    with open(corrupt_path, 'rb') as f:
        corrupt = f.read()
    
    print(f"\nClean size:     {len(clean):,} bytes")
    print(f"Corrupted size: {len(corrupt):,} bytes")
    
    # Verify: strip_cr(clean) should produce something shorter
    clean_stripped = strip_cr(clean)
    print(f"Clean stripped:  {len(clean_stripped):,} bytes ({len(clean) - len(clean_stripped)} CRs)")
    
    print(f"\nRestoring CRs via parallel walk...")
    restored = parallel_walk_restore(clean, corrupt)
    
    print(f"\nRestored size:  {len(restored):,} bytes")
    
    # Verify: strip_cr(restored) should equal corrupted
    restored_stripped = strip_cr(restored)
    if restored_stripped == corrupt:
        print("VERIFICATION PASSED: strip_cr(restored) == corrupted")
    else:
        print(f"VERIFICATION FAILED: strip_cr(restored) has {len(restored_stripped)} bytes, "
              f"corrupted has {len(corrupt)} bytes")
        # Find first difference
        for i in range(min(len(restored_stripped), len(corrupt))):
            if restored_stripped[i] != corrupt[i]:
                print(f"  First diff at byte {i}: restored_stripped=0x{restored_stripped[i]:02X} "
                      f"corrupt=0x{corrupt[i]:02X}")
                break
    
    print(f"\nStructural verification:")
    verify_structure(restored, "RESTORED")
    
    with open(output_path, 'wb') as f:
        f.write(restored)
    print(f"\nSaved to: {output_path} ({len(restored):,} bytes)")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <clean.xcf> <corrupted.xcf> [output.xcf]")
        sys.exit(1)
    main()

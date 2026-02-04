# astc_writer.py
#
# Minimal ASTC writer that mirrors the C/C++ write_astc_file() logic from example_capi.c.
# Writes a valid single-slice 2D ASTC texture file (no array slices, no 3D, no mips).
#
# Usage:
#     from astc_writer import write_astc_file
#     write_astc_file("output.astc", blocks, block_width, block_height, width, height)
#
# "blocks" must be a bytes-like object containing the full ASTC block data
# using 16 bytes per block (standard ASTC block size).


def write_astc_file(
    filename: str,
    blocks: bytes,
    block_width: int,
    block_height: int,
    width: int,
    height: int
) -> None:
    """
    Write an ASTC file to disk.

    Parameters:
        filename      : Output filename ("something.astc")
        blocks        : Bytes-like object containing ASTC blocks (16 bytes per block)
        block_width   : ASTC block width  (e.g. 4-12)
        block_height  : ASTC block height (e.g. 4-12)
        width         : Original image width  in pixels
        height        : Original image height in pixels

    Notes:
        - ASTC files use 2D blocks; depth is always 1.
        - Block layout goes row-major: (num_blocks_y  num_blocks_x) blocks.
        - No mipmaps are stored in this format.
    """

    # Validate block dimensions
    if block_width < 4 or block_width > 12:
        raise ValueError(f"ASTC block_width {block_width} out of range (412)")
    if block_height < 4 or block_height > 12:
        raise ValueError(f"ASTC block_height {block_height} out of range (412)")

    # Compute block grid
    num_blocks_x = (width  + block_width  - 1) // block_width
    num_blocks_y = (height + block_height - 1) // block_height
    total_blocks = num_blocks_x * num_blocks_y
    expected_size = total_blocks * 16  # 16 bytes per ASTC block (always)

    if len(blocks) != expected_size:
        raise ValueError(
            f"ASTC block buffer incorrect size: expected {expected_size}, got {len(blocks)}"
        )

    # Write file
    with open(filename, "wb") as f:
        # ASTC magic number (0x13AB A15C)
        f.write(bytes([0x13, 0xAB, 0xA1, 0x5C]))

        # Block dims: x, y, z (z=1)
        f.write(bytes([
            block_width & 0xFF,
            block_height & 0xFF,
            1
        ]))

        # ASTC stores width/height/depth as 24-bit LE
        def write_24bit_le(v: int):
            f.write(bytes([
                v & 0xFF,
                (v >> 8) & 0xFF,
                (v >> 16) & 0xFF
            ]))

        write_24bit_le(width)
        write_24bit_le(height)
        write_24bit_le(1)  # depth

        # Write actual block payload
        f.write(blocks)

    print(f"[ASTC Writer] Wrote: {filename} ({width}x{height}, {block_width}x{block_height} blocks)")

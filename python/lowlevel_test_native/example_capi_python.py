#!/usr/bin/env python3
# example_capi_python.py
#
# Simple Python port of example_capi.c using native C++ pybind11 bindings:
#   - basisu_python           (encoder)
#   - basisu_transcoder_python (transcoder)
#
# Requires:
#   basisu_py/basisu_python*.so
#   basisu_py/basisu_transcoder_python*.so
#   basisu_py/constants.py

import sys
import os
import math
import ctypes

# Make sure Python can see the native .so's and the shared constants
sys.path.append("basisu_py")

import basisu_python as bu
import basisu_transcoder_python as bt
from constants import BasisTexFormat, BasisFlags
from constants import TranscoderTextureFormat as TF
from constants import TranscodeDecodeFlags as DF

TRUE = 1
FALSE = 0

# ------------------------------------------------------------
# Utility: write raw bytes to a file
# ------------------------------------------------------------

def write_blob_to_file(filename: str, data: bytes) -> int:
    print(f"write_blob_to_file: writing {len(data)} bytes to {filename!r}")
    if not filename or data is None:
        print("  ERROR: invalid filename or data")
        return FALSE

    try:
        with open(filename, "wb") as f:
            f.write(data)
        print("  OK")
        return TRUE
    except OSError as e:
        print("  ERROR:", e)
        return FALSE

# ------------------------------------------------------------
# TGA writer (24/32bpp) - port of write_tga_image()
# ------------------------------------------------------------

def write_tga_image(filename: str, w: int, h: int, has_alpha: bool, pixels_rgba_ptr: int) -> int:
    """
    filename: path to TGA file
    w, h: image dimensions
    has_alpha: True for 32bpp, False for 24bpp
    pixels_rgba_ptr: C pointer (uint64) to RGBA or RGB data in native heap
    """
    print(f"write_tga_image: {filename!r}, {w}x{h}, has_alpha={has_alpha}, ptr=0x{pixels_rgba_ptr:x}")
    if not filename or pixels_rgba_ptr == 0 or w <= 0 or h <= 0:
        print("  ERROR: invalid args")
        return -1

    bytes_per_pixel = 4 if has_alpha else 3
    row_bytes = w * bytes_per_pixel
    total_bytes = row_bytes * h

    # Create a ctypes buffer that views the native memory
    SrcArrayType = ctypes.c_ubyte * total_bytes
    src = SrcArrayType.from_address(pixels_rgba_ptr)

    try:
        with open(filename, "wb") as f:
            header = bytearray(18)
            header[2] = 2  # uncompressed true-color
            header[12] = w & 0xFF
            header[13] = (w >> 8) & 0xFF
            header[14] = h & 0xFF
            header[15] = (h >> 8) & 0xFF
            header[16] = 32 if has_alpha else 24
            header[17] = 8 if has_alpha else 0  # bottom-left origin (with or without alpha)

            f.write(header)

            # temp row buffer for BGRA/BGR
            row_buf = bytearray(row_bytes)

            # TGA expects rows bottom-to-top
            for y in range(h):
                src_y = h - 1 - y
                row_start = src_y * row_bytes
                src_row = src[row_start:row_start + row_bytes]

                if has_alpha:
                    # RGBA -> BGRA
                    for x in range(w):
                        si = x*4
                        di = x*4
                        row_buf[di + 0] = src_row[si + 2]  # B
                        row_buf[di + 1] = src_row[si + 1]  # G
                        row_buf[di + 2] = src_row[si + 0]  # R
                        row_buf[di + 3] = src_row[si + 3]  # A
                else:
                    # RGB -> BGR
                    for x in range(w):
                        si = x*3
                        di = x*3
                        row_buf[di + 0] = src_row[si + 2]  # B
                        row_buf[di + 1] = src_row[si + 1]  # G
                        row_buf[di + 2] = src_row[si + 0]  # R

                f.write(row_buf)

        print("  Wrote TGA:", filename)
        return 0
    except OSError as e:
        print("  ERROR writing TGA:", e)
        return -2

# ------------------------------------------------------------
# ASTC writer - port of write_astc_file()
# ------------------------------------------------------------

def write_astc_file(filename: str,
                    blocks_ptr: int,
                    block_width: int,
                    block_height: int,
                    dim_x: int,
                    dim_y: int) -> int:
    print(f"write_astc_file: {filename!r}, block={block_width}x{block_height}, dim={dim_x}x{dim_y}, ptr=0x{blocks_ptr:x}")
    if not filename or blocks_ptr == 0:
        print("  ERROR: invalid filename or pointer")
        return 0

    assert dim_x > 0 and dim_y > 0
    assert 4 <= block_width <= 12
    assert 4 <= block_height <= 12

    num_blocks_x = (dim_x + block_width - 1) // block_width
    num_blocks_y = (dim_y + block_height - 1) // block_height
    total_blocks = num_blocks_x * num_blocks_y
    total_bytes = total_blocks * 16  # 16 bytes per ASTC block

    print(f"  num_blocks_x={num_blocks_x}, num_blocks_y={num_blocks_y}, total_blocks={total_blocks}, total_bytes={total_bytes}")

    # View native memory
    BlockArray = ctypes.c_ubyte * total_bytes
    src = BlockArray.from_address(blocks_ptr)

    try:
        with open(filename, "wb") as f:
            # Magic
            f.write(bytes([0x13, 0xAB, 0xA1, 0x5C]))

            # Block dimensions x,y,z (=1)
            f.write(bytes([block_width & 0xFF, block_height & 0xFF, 1]))

            # dim_x (24-bit LE)
            f.write(bytes([dim_x & 0xFF, (dim_x >> 8) & 0xFF, (dim_x >> 16) & 0xFF]))

            # dim_y (24-bit LE)
            f.write(bytes([dim_y & 0xFF, (dim_y >> 8) & 0xFF, (dim_y >> 16) & 0xFF]))

            # dim_z = 1 (24-bit LE)
            f.write(bytes([1, 0, 0]))

            # Block data
            f.write(bytes(src))

        print("  Wrote ASTC:", filename)
        return 1
    except OSError as e:
        print("  ERROR writing ASTC:", e)
        return 0

# ------------------------------------------------------------
# Procedural RGBA pattern (ported & fixed version)
# ------------------------------------------------------------

def create_pretty_rgba_pattern(w: int, h: int) -> bytes:
    print(f"create_pretty_rgba_pattern: {w}x{h}")
    if w <= 0 or h <= 0:
        return None

    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            fx = x / float(w)
            fy = y / float(h)

            # Colorful plasma-type formula
            v = math.sin(fx * 12.0 + fy * 4.0)
            v += math.sin(fy * 9.0 - fx * 6.0)
            v += math.sin((fx + fy) * 7.0)
            v = v * 0.25 + 0.5  # scale 0..1

            L = 1.5

            r = int(round(255.0 * math.sin(v * 6.28) * L))
            g = int(round(255.0 * (1.0 - v) * L))
            b = int(round(255.0 * v * L))

            if r < 0: r = 0
            elif r > 255: r = 255
            if g < 0: g = 0
            elif g > 255: g = 255
            if b < 0: b = 0
            elif b > 255: b = 255

            i = (y * w + x) * 4
            out[i+0] = r
            out[i+1] = g
            out[i+2] = b
            out[i+3] = 255

    return bytes(out)

# ------------------------------------------------------------
# Transcode a KTX2 blob (ported from transcode_ktx2_file)
# ------------------------------------------------------------

def transcode_ktx2_file(ktx2_data: bytes) -> int:
    if not ktx2_data:
        print("transcode_ktx2_file: empty data")
        return FALSE

    size = len(ktx2_data)
    print(f"transcode_ktx2_file: size={size} bytes")

    if size > 0xFFFFFFFF:
        print("  ERROR: size too large for 32-bit length")
        return FALSE

    # Allocate memory in transcoder heap and copy KTX2 data
    ktx2_data_ofs = bt.alloc(size)
    if not ktx2_data_ofs:
        print("  ERROR: bt.alloc failed")
        return FALSE

    print(f"  KTX2 data allocated at 0x{ktx2_data_ofs:x}")
    ctypes.memmove(ktx2_data_ofs, ktx2_data, size)

    # Open KTX2
    ktx2_handle = bt.ktx2_open(ktx2_data_ofs, size)
    if not ktx2_handle:
        print("  ERROR: bt.ktx2_open failed")
        bt.free(ktx2_data_ofs)
        return FALSE

    print(f"  KTX2 handle = 0x{ktx2_handle:x}")

    if not bt.ktx2_is_ldr(ktx2_handle):
        print("  ERROR: This sample only handles LDR KTX2 files")
        bt.ktx2_close(ktx2_handle)
        bt.free(ktx2_data_ofs)
        return FALSE

    if not bt.ktx2_start_transcoding(ktx2_handle):
        print("  ERROR: bt.ktx2_start_transcoding failed")
        bt.ktx2_close(ktx2_handle)
        bt.free(ktx2_data_ofs)
        return FALSE

    width  = bt.ktx2_get_width(ktx2_handle)
    height = bt.ktx2_get_height(ktx2_handle)
    levels = bt.ktx2_get_levels(ktx2_handle)
    faces  = bt.ktx2_get_faces(ktx2_handle)
    layers = bt.ktx2_get_layers(ktx2_handle)

    basis_tex_format = bt.ktx2_get_basis_tex_format(ktx2_handle)
    block_width  = bt.ktx2_get_block_width(ktx2_handle)
    block_height = bt.ktx2_get_block_height(ktx2_handle)
    is_srgb = bt.ktx2_is_srgb(ktx2_handle)

    print(f"KTX2 Dimensions: {width}x{height}, Levels={levels}, Faces={faces}, Layers={layers}")
    print(f"basis_tex_format: {basis_tex_format}")
    print(f"Block dimensions: {block_width}x{block_height}")
    print(f"is sRGB: {is_srgb}")

    if layers < 1:
        layers = 1

    assert width >= 1 and height >= 1
    assert levels >= 1
    assert faces in (1, 6)

    # Optional: separate transcode state (thread-local)
    trans_state = bt.ktx2_create_transcode_state()
    print(f"trans_state handle = 0x{trans_state:x}")

    for level_index in range(levels):
        for layer_index in range(layers):
            for face_index in range(faces):
                print(f"- Level {level_index}, layer {layer_index}, face {face_index}")
                ow = bt.ktx2_get_level_orig_width(ktx2_handle, level_index, layer_index, face_index)
                oh = bt.ktx2_get_level_orig_height(ktx2_handle, level_index, layer_index, face_index)
                aw = bt.ktx2_get_level_actual_width(ktx2_handle, level_index, layer_index, face_index)
                ah = bt.ktx2_get_level_actual_height(ktx2_handle, level_index, layer_index, face_index)
                nbx = bt.ktx2_get_level_num_blocks_x(ktx2_handle, level_index, layer_index, face_index)
                nby = bt.ktx2_get_level_num_blocks_y(ktx2_handle, level_index, layer_index, face_index)
                tblocks = bt.ktx2_get_level_total_blocks(ktx2_handle, level_index, layer_index, face_index)
                alpha_flag = bt.ktx2_get_level_alpha_flag(ktx2_handle, level_index, layer_index, face_index)
                iframe_flag = bt.ktx2_get_level_iframe_flag(ktx2_handle, level_index, layer_index, face_index)

                print(f"  Orig dimensions: {ow}x{oh}, actual: {aw}x{ah}")
                print(f"  Block dims: {nbx}x{nby}, total blocks: {tblocks}")
                print(f"  Alpha={alpha_flag}, I-frame={iframe_flag}")

                # 1) Transcode to RGBA32 and write TGA
                tga_name = f"transcoded_{level_index}_{layer_index}_{face_index}.tga"
                trans_size_rgba = bt.basis_compute_transcoded_image_size_in_bytes(TF.TF_RGBA32, ow, oh)
                assert trans_size_rgba > 0
                rgba_ofs = bt.alloc(trans_size_rgba)
                print(f"  RGBA buf ofs=0x{rgba_ofs:x}, size={trans_size_rgba}")

                decode_flags = 0
                ok = bt.ktx2_transcode_image_level(
                    ktx2_handle,
                    level_index, layer_index, face_index,
                    rgba_ofs,
                    trans_size_rgba,
                    TF.TF_RGBA32,
                    decode_flags,
                    0, 0, -1, -1,
                    trans_state
                )
                print("  ktx2_transcode_image_level(RGBA32):", ok)
                if not ok:
                    bt.free(rgba_ofs)
                    bt.ktx2_destroy_transcode_state(trans_state)
                    bt.ktx2_close(ktx2_handle)
                    bt.free(ktx2_data_ofs)
                    return FALSE

                write_tga_image(tga_name, ow, oh, True, rgba_ofs)
                bt.free(rgba_ofs)

                # 2) Transcode to ASTC and write .astc file
                astc_name = f"transcoded_{level_index}_{layer_index}_{face_index}.astc"
                target_tf = bt.basis_get_transcoder_texture_format_from_basis_tex_format(basis_tex_format)
                print(f"  Target ASTC TF={target_tf}")

                trans_size_astc = bt.basis_compute_transcoded_image_size_in_bytes(target_tf, ow, oh)
                assert trans_size_astc > 0
                astc_ofs = bt.alloc(trans_size_astc)
                print(f"  ASTC buf ofs=0x{astc_ofs:x}, size={trans_size_astc}")

                ok = bt.ktx2_transcode_image_level(
                    ktx2_handle,
                    level_index, layer_index, face_index,
                    astc_ofs,
                    trans_size_astc,
                    target_tf,
                    0, 0, 0, -1, -1,
                    trans_state
                )
                print("  ktx2_transcode_image_level(ASTC):", ok)
                if not ok:
                    bt.free(astc_ofs)
                    bt.ktx2_destroy_transcode_state(trans_state)
                    bt.ktx2_close(ktx2_handle)
                    bt.free(ktx2_data_ofs)
                    return FALSE

                write_astc_file(astc_name, astc_ofs, block_width, block_height, ow, oh)
                bt.free(astc_ofs)

    bt.ktx2_destroy_transcode_state(trans_state)
    bt.ktx2_close(ktx2_handle)
    bt.free(ktx2_data_ofs)

    print("transcode_ktx2_file: success")
    return TRUE

# ------------------------------------------------------------
# main() equivalent
# ------------------------------------------------------------

def main():
    print("example_capi_python:")

    # Init encoder (which initializes transcoder)
    print("Calling bu.init() ...")
    bu.init()

    print("Calling bt.init() ...")
    bt.init()

    # Optional debug control if bound
    if hasattr(bu, "enable_debug_printf"):
        print("Disabling debug printf from encoder")
        bu.enable_debug_printf(False)

    # Generate test image
    W, H = 512, 512
    src_image = create_pretty_rgba_pattern(W, H)
    if src_image is None:
        print("ERROR: create_pretty_rgba_pattern failed")
        return 1

    # Save test image for inspection
    print("Writing test_image.tga ...")
    # use Python-level TGA writer by allocating a temporary native buffer
    tmp_ofs = bt.alloc(len(src_image))
    ctypes.memmove(tmp_ofs, src_image, len(src_image))
    write_tga_image("test_image.tga", W, H, True, tmp_ofs)
    bt.free(tmp_ofs)

    # Compress to KTX2
    print("Creating comp_params ...")
    comp_params = bu.new_params()
    print("  comp_params handle:", comp_params)

    img_ofs = bu.alloc(W * H * 4)
    print(f"Allocated encoder image buffer at 0x{img_ofs:x}")
    ctypes.memmove(img_ofs, src_image, W * H * 4)

    print("Calling bu.comp_params_set_image_rgba32(...)")
    ok = bu.set_image_rgba32(comp_params, 0, img_ofs, W, H, W * 4)
    print("  set_image_rgba32:", ok)
    if not ok:
        print("ERROR: bu_comp_params_set_image_rgba32 failed")
        return 1

    bu.free(img_ofs)

    print("Compressing to XUASTC LDR 8x5 KTX2 ...")
    basis_tex_format = BasisTexFormat.cXUASTC_LDR_8x5
    quality_level = 85
    effort_level = 2
    flags = (BasisFlags.KTX2_OUTPUT |
             BasisFlags.SRGB |
             BasisFlags.THREADED |
             BasisFlags.GEN_MIPS_CLAMP |
             BasisFlags.PRINT_STATS |
             BasisFlags.PRINT_STATUS)

    ok = bu.compress(comp_params,
                     tex_format=basis_tex_format,
                     quality=quality_level,
                     effort=effort_level,
                     flags=flags,
                     rdo_quality=0.0)
    print("  bu.compress:", ok)
    if not ok:
        print("ERROR: bu_compress_texture failed")
        return 1

    comp_size = bu.get_comp_data_size(comp_params)
    print("Compressed size:", comp_size)
    if comp_size == 0:
        print("ERROR: bu_comp_params_get_comp_data_size failed")
        return 1

    comp_ofs = bu.get_comp_data_ofs(comp_params)
    print(f"Compressed data ptr=0x{comp_ofs:x}")

    # Copy compressed data into Python bytes
    CompArray = ctypes.c_ubyte * comp_size
    comp_buf = CompArray.from_address(comp_ofs)
    comp_bytes = bytes(comp_buf)

    print("Writing test.ktx2 ...")
    if not write_blob_to_file("test.ktx2", comp_bytes):
        print("ERROR: write_blob_to_file failed")
        return 1

    # Transcode using the native transcoder API
    print("Now transcoding test.ktx2 via C API ...")
    if not transcode_ktx2_file(comp_bytes):
        print("ERROR: transcode_ktx2_file failed")
        return 1

    bu.delete_params(comp_params)

    print("Success")
    return 0

if __name__ == "__main__":
    sys.exit(main())

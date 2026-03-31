#!/usr/bin/env python3
"""
explode_ktx2_file.py
FULL LDR/HDR KTX2 EXPLODER + FULL API INTROSPECTION + ASTC + BC7/BC6H OUTPUT

Usage:
    python3 explode_ktx2_file.py input.ktx2
    python3 explode_ktx2_file.py input.ktx2 --info-only
"""

# Python Dependencies (beyond basisu_py):
#     numpy
#     pillow
#     imageio (v3+)
#     wasmtime
#
# System Dependencies:
#     OpenImageIO ("oiiotool")  -- required for EXR output
#
# Install Python deps:
#     pip install numpy pillow imageio wasmtime
#
# On Ubuntu:
#     sudo apt install openimageio-tools
#
# On macOS (Homebrew):
#     brew install openimageio

import sys
import os
import numpy as np
import subprocess
import tempfile
import imageio.v3 as iio
from PIL import Image

from basisu_py import Transcoder
from basisu_py.constants import TranscoderTextureFormat as TF

# Writers located in same directory as this script
from astc_writer import write_astc_file
from dds_writer import DDSWriter


# ============================================================================
# File-writing helpers
# ============================================================================
def save_exr(path, rgba32f):
    """
    Save float32 RGBA as EXR if possible.
    If oiiotool is not available, save TIFF instead (Windows-safe).
    """
    import numpy as np
    import imageio.v3 as iio
    import subprocess, tempfile, os

    # Write temp TIFF
    with tempfile.NamedTemporaryFile(suffix=".tiff", delete=False) as tmp:
        temp_path = tmp.name

    iio.imwrite(temp_path, rgba32f.astype(np.float32))

    # Try EXR via oiiotool
    try:
        subprocess.run(["oiiotool", temp_path, "-o", path], check=True)
        os.remove(temp_path)
        print("  Wrote EXR:", path)
        return

    except Exception:
        # --- FALLBACK: save TIFF ---
        fallback_path = path + ".tiff"

        # Windows cannot overwrite files via rename(), so remove first
        if os.path.exists(fallback_path):
            os.remove(fallback_path)

        # os.replace() always overwrites
        os.replace(temp_path, fallback_path)

        print("  [Fallback] Wrote TIFF instead:", fallback_path)


def save_png(path, rgba8):
    img = Image.fromarray(rgba8, mode="RGBA")
    img.save(path)
    print(f"  PNG saved: {path}")


# ============================================================================
# Pretty header
# ============================================================================
def print_header(title):
    print("\n" + "=" * 90)
    print(title)
    print("=" * 90)


# ============================================================================
# Full top-level metadata dump (ALL API)
# ============================================================================
def dump_all_top_level(t, h):
    print_header("TOP-LEVEL KTX2 METADATA  FULL API")

    print("Backend                  :", t.backend_name)
    print("Version                  :", t.get_version())
    print("Width                    :", t.get_width(h))
    print("Height                   :", t.get_height(h))
    print("Levels                   :", t.get_levels(h))
    print("Faces                    :", t.get_faces(h))

    layers = t.get_layers(h)
    eff_layers = layers if layers > 0 else 1
    print("Layers (raw)             :", layers)
    print("Layers (effective)       :", eff_layers)

    fmt = t.get_basis_tex_format(h)
    print("\nBasisTexFormat           :", fmt)

    print("\nKTX2 Format Flags:")
    print("  is_etc1s               :", t.is_etc1s(h))
    print("  is_uastc_ldr_4x4       :", t.is_uastc_ldr_4x4(h))
    print("  is_xuastc_ldr          :", t.is_xuastc_ldr(h))
    print("  is_astc_ldr            :", t.is_astc_ldr(h))
    print("  is_hdr                 :", t.is_hdr(h))
    print("  is_hdr_4x4             :", t.is_hdr_4x4(h))
    print("  is_hdr_6x6             :", t.is_hdr_6x6(h))
    print("  is_ldr                 :", t.is_ldr(h))
    print("  is_srgb                :", t.is_srgb(h))
    print("  is_video               :", t.is_video(h))
    print("  has_alpha              :", t.has_alpha(h))

    print("\nBlock Info:")
    print("  block_width            :", t.get_block_width(h))
    print("  block_height           :", t.get_block_height(h))

    print("\nDFD Info:")
    print("  color_model            :", t.get_dfd_color_model(h))
    print("  color_primaries        :", t.get_dfd_color_primaries(h))
    print("  transfer_func          :", t.get_dfd_transfer_func(h))
    print("  flags                  :", t.get_dfd_flags(h))
    print("  total_samples          :", t.get_dfd_total_samples(h))
    print("  channel_id0            :", t.get_dfd_channel_id0(h))
    print("  channel_id1            :", t.get_dfd_channel_id1(h))

    if t.is_hdr(h):
        print("  hdr_nit_multiplier     :", t.get_ldr_hdr_upconversion_nit_multiplier(h))


# ============================================================================
# BasisTexFormat helpers
# ============================================================================
def dump_basis_tex_format_helpers(t, h):
    print_header("BasisTexFormat HELPERS (FULL)")

    fmt = t.get_basis_tex_format(h)
    print("basis_tex_format:", fmt)

    print("is_xuastc_ldr     :", t.basis_tex_format_is_xuastc_ldr(fmt))
    print("is_astc_ldr       :", t.basis_tex_format_is_astc_ldr(fmt))
    print("block width       :", t.basis_tex_format_get_block_width(fmt))
    print("block height      :", t.basis_tex_format_get_block_height(fmt))
    print("is_hdr            :", t.basis_tex_format_is_hdr(fmt))
    print("is_ldr            :", t.basis_tex_format_is_ldr(fmt))


# ============================================================================
# Level / Layer / Face metadata dump
# ============================================================================
def dump_per_level_info(t, h):
    print_header("PER-LEVEL / PER-LAYER / PER-FACE METADATA")

    levels = t.get_levels(h)
    faces  = t.get_faces(h)
    layers = t.get_layers(h)
    if layers == 0:
        layers = 1

    for level in range(levels):
        for layer in range(layers):
            for face in range(faces):
                print(f"\nLevel={level}, Layer={layer}, Face={face}")
                print("  orig_width   :", t.get_level_orig_width(h, level, layer, face))
                print("  orig_height  :", t.get_level_orig_height(h, level, layer, face))
                print("  actual_width :", t.get_level_actual_width(h, level, layer, face))
                print("  actual_height:", t.get_level_actual_height(h, level, layer, face))
                print("  blocks_x     :", t.get_level_num_blocks_x(h, level, layer, face))
                print("  blocks_y     :", t.get_level_num_blocks_y(h, level, layer, face))
                print("  total_blocks :", t.get_level_total_blocks(h, level, layer, face))
                print("  alpha_flag   :", t.get_level_alpha_flag(h, level, layer, face))
                print("  iframe_flag  :", t.get_level_iframe_flag(h, level, layer, face))


# ============================================================================
# ASTC Selection
# ============================================================================
def choose_astc_format(t, h):
    fmt = t.get_basis_tex_format(h)
    tfmt = t.basis_get_transcoder_texture_format_from_basis_tex_format(fmt)
    bw = t.basis_get_block_width(tfmt)
    bh = t.basis_get_block_height(tfmt)

    print_header("ASTC SELECTION")
    print("ASTC TF:", tfmt)
    print(f"Block dims: {bw}x{bh}")
    return tfmt, bw, bh


# ============================================================================
# BC Format Selection
# ============================================================================
def choose_bc_format(t, h):
    if t.is_hdr(h):
        print_header("HDR -> BC6H")
        return TF.TF_BC6H, 8, 95   # DXGI_FORMAT_BC6H_UF16
    else:
        print_header("LDR -> BC7")
        return TF.TF_BC7_RGBA, 8, 98   # DXGI_FORMAT_BC7_UNORM


# ============================================================================
# Full explode transcoding (using handle API + per-level dims)
# ============================================================================
def explode_transcode(t, h):
    levels = t.get_levels(h)
    faces  = t.get_faces(h)
    layers = t.get_layers(h)
    if layers == 0:
        layers = 1

    astc_tfmt, astc_bw, astc_bh = choose_astc_format(t, h)
    bc_tfmt, bc_bpp, bc_dxgi    = choose_bc_format(t, h)

    ddsw = DDSWriter()
    print_header("BEGIN EXPLODE TRANSCODING (handle API)")

    for level in range(levels):
        for layer in range(layers):
            for face in range(faces):

                print(f"\n- Level={level} Layer={layer} Face={face}")

                ow = t.get_level_orig_width(h, level, layer, face)
                oh = t.get_level_orig_height(h, level, layer, face)
                print(f"  Level orig dims: {ow}x{oh}")

                # ASTC
                astc_blocks = t.transcode_tfmt_handle(
                    h, astc_tfmt,
                    level=level, layer=layer, face=face,
                    decode_flags=0, channel0=-1, channel1=-1
                )
                astc_name = f"astc_L{level}_Y{layer}_F{face}.astc"
                write_astc_file(astc_name, astc_blocks, astc_bw, astc_bh, ow, oh)
                print("  ASTC saved:", astc_name)

                # BC6H / BC7
                bc_blocks = t.transcode_tfmt_handle(
                    h, bc_tfmt,
                    level=level, layer=layer, face=face,
                    decode_flags=0, channel0=-1, channel1=-1
                )
                if t.is_hdr(h):
                    dds_name = f"bc6h_L{level}_Y{layer}_F{face}.dds"
                else:
                    dds_name = f"bc7_L{level}_Y{layer}_F{face}.dds"

                ddsw.save_dds(
                    dds_name,
                    width=ow, height=oh,
                    blocks=bc_blocks,
                    pixel_format_bpp=bc_bpp,
                    dxgi_format=bc_dxgi,
                    srgb=False,
                    force_dx10_header=True,
                )
                print("  DDS saved :", dds_name)

    print_header("EXPLODE TRANSCODING COMPLETE")


# ============================================================================
# Decode each (Level, Layer, Face) to PNG or EXR
# ============================================================================
def explode_decode_images(t, h):
    print_header("BEGIN EXPLODE IMAGE DECODE (PNG/EXR)")

    levels = t.get_levels(h)
    faces  = t.get_faces(h)
    layers = t.get_layers(h)
    if layers == 0:
        layers = 1

    hdr = t.is_hdr(h)

    for level in range(levels):
        for layer in range(layers):
            for face in range(faces):

                print(f"\n- Decode Level={level} Layer={layer} Face={face}")

                ow = t.get_level_orig_width(h, level, layer, face)
                oh = t.get_level_orig_height(h, level, layer, face)

                if hdr:
                    rgba32f = t.decode_rgba_hdr_handle(h, level, layer, face)
                    outname = f"exr_L{level}_Y{layer}_F{face}.exr"
                    save_exr(outname, rgba32f)
                else:
                    rgba8 = t.decode_rgba_handle(h, level, layer, face)
                    outname = f"png_L{level}_Y{layer}_F{face}.png"
                    save_png(outname, rgba8)

    print_header("IMAGE DECODE COMPLETE")

def dump_transcoder_texture_format_helpers(t):
    print_header("TranscoderTextureFormat HELPERS (FULL)")

    test_formats = [
        # uncompressed
        TF.TF_RGBA32, TF.TF_RGB565, TF.TF_BGR565,
        TF.TF_RGBA4444, TF.TF_RGB_HALF, TF.TF_RGBA_HALF, TF.TF_RGB_9E5,

        # basic compressed
        TF.TF_ETC1_RGB, TF.TF_ETC2_RGBA,
        TF.TF_BC1_RGB, TF.TF_BC3_RGBA,
        TF.TF_BC4_R, TF.TF_BC5_RG,
        TF.TF_BC7_RGBA, TF.TF_BC6H,
        TF.TF_ETC2_EAC_R11, TF.TF_ETC2_EAC_RG11,
        TF.TF_FXT1_RGB,
        TF.TF_PVRTC1_4_RGB, TF.TF_PVRTC1_4_RGBA,
        TF.TF_PVRTC2_4_RGB, TF.TF_PVRTC2_4_RGBA,
        TF.TF_ATC_RGB, TF.TF_ATC_RGBA,

        # HDR ASTC
        TF.TF_ASTC_HDR_4X4_RGBA,
        TF.TF_ASTC_HDR_6X6_RGBA,

        # LDR ASTC
        TF.TF_ASTC_LDR_4X4_RGBA,
        TF.TF_ASTC_LDR_5X4_RGBA, TF.TF_ASTC_LDR_5X5_RGBA,
        TF.TF_ASTC_LDR_6X5_RGBA, TF.TF_ASTC_LDR_6X6_RGBA,
        TF.TF_ASTC_LDR_8X5_RGBA, TF.TF_ASTC_LDR_8X6_RGBA,
        TF.TF_ASTC_LDR_10X5_RGBA, TF.TF_ASTC_LDR_10X6_RGBA,
        TF.TF_ASTC_LDR_8X8_RGBA, TF.TF_ASTC_LDR_10X8_RGBA,
        TF.TF_ASTC_LDR_10X10_RGBA, TF.TF_ASTC_LDR_12X10_RGBA,
        TF.TF_ASTC_LDR_12X12_RGBA,
    ]

    for tfmt in test_formats:
        print(f"\nTF={tfmt}")
        print("  has_alpha        :", t.basis_transcoder_format_has_alpha(tfmt))
        print("  is_hdr           :", t.basis_transcoder_format_is_hdr(tfmt))
        print("  is_ldr           :", t.basis_transcoder_format_is_ldr(tfmt))
        print("  is_astc          :", t.basis_transcoder_texture_format_is_astc(tfmt))
        print("  is_uncompressed  :", t.basis_transcoder_format_is_uncompressed(tfmt))
        print("  bytes/block      :", t.basis_get_bytes_per_block_or_pixel(tfmt))
        print("  block_width      :", t.basis_get_block_width(tfmt))
        print("  block_height     :", t.basis_get_block_height(tfmt))


def main():
    if len(sys.argv) < 2:
        print("Usage: python explode_ktx2_file.py input.ktx2 [--info-only] [--print-tf]")
        return 1

    args = sys.argv[1:]
    info_only = "--info-only" in args
    print_tf   = "--print-tf" in args or "--transcoder-formats" in args

    # Determine input filename
    input_file = None
    for a in args:
        if not a.startswith("--"):
            input_file = a
            break

    if input_file is None:
        print("Error: No input file provided.")
        return 1

    ktx_bytes = open(input_file, "rb").read()

    t = Transcoder()
    h = t.open(ktx_bytes)
    t.start_transcoding(h)

    # Full metadata
    dump_all_top_level(t, h)
    dump_basis_tex_format_helpers(t, h)
    dump_per_level_info(t, h)

    # Optional TF helpers
    if print_tf:
        dump_transcoder_texture_format_helpers(t)

    if info_only:
        print_header("INFO-ONLY MODE  NO FILES WRITTEN")
        t.close(h)
        return 0

    # Full output
    explode_transcode(t, h)
    explode_decode_images(t, h)

    t.close(h)
    print("Success")
    return 0

if __name__ == "__main__":
    sys.exit(main())



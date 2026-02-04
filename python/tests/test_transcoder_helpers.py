#!/usr/bin/env python3
import sys
import numpy as np

from basisu_py.transcoder import Transcoder, TranscoderBackend
from basisu_py.constants import BasisTexFormat, TranscoderTextureFormat

print("========== TESTING TRANSCODER HELPERS & METADATA ==========\n")

# ----------------------------------------------------------------------------
# Load test KTX2 file
# ----------------------------------------------------------------------------
try:
    ktx2_bytes = open("test.ktx2", "rb").read()
    print("[INFO] Loaded test.ktx2")
except FileNotFoundError:
    print("[ERROR] test.ktx2 not found. Run encoder tests first.")
    sys.exit(1)


# ----------------------------------------------------------------------------
# Utility: run helper tests on a given backend
# ----------------------------------------------------------------------------
def test_backend(name, backend):
    print(f"\n=== Testing {name} backend ===")

    try:
        t = Transcoder(backend)
    except Exception as e:
        print(f"[FAIL] Could not initialize {name} backend:", e)
        return

    print(f"[OK] {name} backend loaded")

    # Version
    try:
        ver = t.get_version()
        print(f"  version = {ver}")
    except Exception as e:
        print("  [FAIL] get_version() error:", e)
        return
        
    # enable_debug_printf
    try:
        t.enable_debug_printf(True)
    except Exception as e:
        print("  [FAIL] enable_debug_printf() failed")
        return
                
    # Open KTX2
    try:
        raw = t.open(ktx2_bytes)
        print("  [OK] open() success")
    except Exception as e:
        print("  [FAIL] open() failed:", e)
        return

    # ----------------------------------------------------------------------
    # KTX2 top-level metadata
    # ----------------------------------------------------------------------
    try:
        w = t.get_width(raw)
        h = t.get_height(raw)
        lv = t.get_levels(raw)
        fc = t.get_faces(raw)
        la = t.get_layers(raw)
        fmt = t.get_basis_tex_format(raw)

        print(f"  Width  = {w}")
        print(f"  Height = {h}")
        print(f"  Levels = {lv}")
        print(f"  Faces  = {fc}")
        print(f"  Layers = {la}")
        print(f"  basis_tex_format = {fmt}")
        print(f"  has_alpha = {t.has_alpha(raw)}")
        print(f"  is_hdr    = {t.is_hdr(raw)}")
        print(f"  is_ldr    = {t.is_ldr(raw)}")
        print(f"  is_srgb   = {t.is_srgb(raw)}")
        print(f"  is_etc1s  = {t.is_etc1s(raw)}")
        print(f"  is_uastc_ldr_4x4  = {t.is_uastc_ldr_4x4(raw)}")
        print(f"  is_xuastc_ldr = {t.is_xuastc_ldr(raw)}")
        print(f"  is_astc_ldr   = {t.is_astc_ldr(raw)}")
        print(f"  block dims = {t.get_block_width(raw)} x {t.get_block_height(raw)}")

    except Exception as e:
        print("  [FAIL] get_* metadata error:", e)
        t.close(raw)
        return

    # ----------------------------------------------------------------------
    # Per-level metadata for each mipmap
    # ----------------------------------------------------------------------
    print("\n  -- Level Metadata --")
    for level in range(lv):
        try:
            ow = t.get_level_orig_width(raw, level)
            oh = t.get_level_orig_height(raw, level)
            nbx = t.get_level_num_blocks_x(raw, level)
            nby = t.get_level_num_blocks_y(raw, level)
            tb = t.get_level_total_blocks(raw, level)
            af = t.get_level_alpha_flag(raw, level)
            ff = t.get_level_iframe_flag(raw, level)

            print(f"   Level {level}: orig={ow}x{oh}, blocks={nbx}x{nby}, total={tb}, alpha={af}, iframe={ff}")
        except Exception as e:
            print(f"   [FAIL] Level {level} metadata error:", e)

    # ----------------------------------------------------------------------
    # Test ALL basis_tex_format helpers on the file's format
    # ----------------------------------------------------------------------
    print("\n  -- basis_tex_format helpers --")

    try:
        print(f"    is_xuastc_ldr = {t.basis_tex_format_is_xuastc_ldr(fmt)}")
        print(f"    is_astc_ldr   = {t.basis_tex_format_is_astc_ldr(fmt)}")
        print(f"    block W/H     = {t.basis_tex_format_get_block_width(fmt)} x "
              f"{t.basis_tex_format_get_block_height(fmt)}")
        print(f"    is_hdr        = {t.basis_tex_format_is_hdr(fmt)}")
        print(f"    is_ldr        = {t.basis_tex_format_is_ldr(fmt)}")
    except Exception as e:
        print("    [FAIL] basis_tex_format_* error:", e)

    # ----------------------------------------------------------------------
    # Test transcoder_texture_format helpers using a few common formats
    # ----------------------------------------------------------------------
    print("\n  -- transcoder_texture_format helpers --")

    test_formats = [
        TranscoderTextureFormat.TF_RGBA32,
        TranscoderTextureFormat.TF_RGBA_HALF,
        TranscoderTextureFormat.TF_BC7_RGBA,
        TranscoderTextureFormat.TF_ETC1_RGB,
    ]

    for tfmt in test_formats:
        try:
            print(f"    Format {tfmt}: hdr={t.basis_transcoder_format_is_hdr(tfmt)}, "
                  f"ldr={t.basis_transcoder_format_is_ldr(tfmt)}, "
                  f"has_alpha={t.basis_transcoder_format_has_alpha(tfmt)}, "
                  f"uncompressed={t.basis_transcoder_format_is_uncompressed(tfmt)}, "
                  f"bytes/pixel or block={t.basis_get_bytes_per_block_or_pixel(tfmt)}")
        except Exception as e:
            print("    [FAIL] transcoder_texture_format_* error:", e)

    # ----------------------------------------------------------------------
    # Compute transcode buffer sizes
    # ----------------------------------------------------------------------
    print("\n  -- compute_transcoded_image_size_in_bytes --")
    try:
        for tfmt in test_formats:
            sz = t.basis_compute_transcoded_image_size_in_bytes(tfmt, w, h)
            print(f"    Format {tfmt}: size = {sz}")
    except Exception as e:
        print("    [FAIL] size computation error:", e)

    # ----------------------------------------------------------------------
    # Decode RGBA (LDR)
    # ----------------------------------------------------------------------
    print("\n  -- decode_rgba --")
    try:
        img_rgba = t.decode_rgba(ktx2_bytes)
        print(f"    decode_rgba: shape={img_rgba.shape}, dtype={img_rgba.dtype}")
    except Exception as e:
        print("    [FAIL] decode_rgba error:", e)

    # ----------------------------------------------------------------------
    # Decode HDR if applicable
    # ----------------------------------------------------------------------
    if t.is_hdr(raw):
        print("\n  -- decode_rgba_hdr --")
        try:
            img_hdr = t.decode_rgba_hdr(ktx2_bytes)
            print(f"    decode_rgba_hdr: shape={img_hdr.shape}, dtype={img_hdr.dtype}")
        except Exception as e:
            print("    [FAIL] decode_rgba_hdr error:", e)
    else:
        print("  Texture is LDR; skipping decode_rgba_hdr().")

    # Cleanup
    t.close(raw)
    print(f"\n=== {name} backend OK ===\n")


# ----------------------------------------------------------------------------
# Run tests for both backends
# ----------------------------------------------------------------------------
test_backend("NATIVE", TranscoderBackend.NATIVE)
test_backend("WASM",   TranscoderBackend.WASM)

print("\n========== DONE ==========\n")

#!/usr/bin/env python3
"""
HDR End-to-End Compression & Transcoding Test
Works on all platforms:
 - native if available
 - WASM fallback otherwise
"""

import numpy as np
from math import sin, cos, atan2, hypot
from PIL import Image
import subprocess
import tempfile
import os
import imageio.v3 as iio

from basisu_py.codec import Encoder, EncoderBackend
from basisu_py.transcoder import Transcoder, TranscoderBackend
from basisu_py.constants import (
    BasisTexFormat,
    BasisQuality,
    BasisEffort,
    BasisFlags
)


# -------------------------------------------------------------------
# Save EXR using TIFF temp + oiiotool (as required)
# -------------------------------------------------------------------
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

# -------------------------------------------------------------------
# Generate HDR swirl image (float32)
# -------------------------------------------------------------------
def make_swirl_hdr(w=256, h=256):
    arr = np.zeros((h, w, 4), dtype=np.float32)
    cx, cy = w / 2.0, h / 2.0

    for y in range(h):
        for x in range(w):
            dx, dy = x - cx, y - cy
            dist = hypot(dx, dy)
            angle = atan2(dy, dx)

            # HDR values range up to about 4.0
            r = (sin(dist * 0.08) * 0.5 + 0.5) * 4.0
            g = (sin(angle * 2.0) * 0.5 + 0.5) * 4.0
            b = (cos(dist * 0.06 + angle * 1.5) * 0.5 + 0.5) * 4.0

            arr[y, x] = (r, g, b, 1.0)

    return arr


# -------------------------------------------------------------------
# Try loading a transcoder backend
# -------------------------------------------------------------------
def try_transcoder(name, backend):
    try:
        t = Transcoder(backend)
        print(f"[OK] Loaded transcoder backend '{name}' ({t.backend_name})")
        return t
    except Exception as e:
        print(f"[SKIP] Backend '{name}' unavailable:", e)
        return None


# -------------------------------------------------------------------
# MAIN
# -------------------------------------------------------------------
if __name__ == "__main__":
    print("========== HDR End-to-End Compression & Transcoding Test ==========")

    # -------------------------------------------------------
    # Create HDR test image
    # -------------------------------------------------------
    img_hdr = make_swirl_hdr(256, 256)
    print("[HDR] swirl:", img_hdr.shape, img_hdr.dtype)

    # -------------------------------------------------------
    # ENCODE using AUTO backend (native ? or WASM)
    # -------------------------------------------------------
    try:
        enc = Encoder(EncoderBackend.AUTO)
        print(f"[HDR] Encoder backend = {enc.backend_name}")
    except Exception as e:
        print("[FATAL] Could not create encoder:", e)
        exit(1)

    try:
        print("[HDR] Compressing HDR swirl -> test_hdr.ktx2...")
        ktx2_blob = enc.compress(
            img_hdr,
            format=-1,                # auto-select HDR format
            quality=BasisQuality.MAX,
            effort=BasisEffort.DEFAULT,
            flags=BasisFlags.KTX2_OUTPUT
        )
        print("  KTX2 size:", len(ktx2_blob))
        open("test_hdr.ktx2", "wb").write(ktx2_blob)
        print("  Wrote test_hdr.ktx2")
    except Exception as e:
        print("[FATAL] Encoding failed:", e)
        exit(1)

    # -------------------------------------------------------
    # DECODE using AUTO (native ? or WASM)
    # -------------------------------------------------------
    t_auto = try_transcoder("AUTO", TranscoderBackend.AUTO)
    if t_auto:
        try:
            hdr = t_auto.decode_rgba_hdr(ktx2_blob)
            print("  AUTO decoded:", hdr.shape, hdr.dtype)
            save_exr("decoded_auto_hdr.exr", hdr)
        except Exception as e:
            print("  [FAIL] AUTO decode failed:", e)

    # -------------------------------------------------------
    # DECODE using NATIVE if available
    # -------------------------------------------------------
    t_native = try_transcoder("NATIVE", TranscoderBackend.NATIVE)
    if t_native:
        try:
            hdr_n = t_native.decode_rgba_hdr(ktx2_blob)
            print("  Native decoded:", hdr_n.shape, hdr_n.dtype)
            save_exr("decoded_native_hdr.exr", hdr_n)
        except Exception as e:
            print("  [FAIL] Native decode failed:", e)

    # -------------------------------------------------------
    # DECODE using WASM if available
    # -------------------------------------------------------
    t_wasm = try_transcoder("WASM", TranscoderBackend.WASM)
    if t_wasm:
        try:
            hdr_w = t_wasm.decode_rgba_hdr(ktx2_blob)
            print("  WASM decoded:", hdr_w.shape, hdr_w.dtype)
            save_exr("decoded_wasm_hdr.exr", hdr_w)
        except Exception as e:
            print("  [FAIL] WASM decode failed:", e)

    print("\n========== DONE ==========")

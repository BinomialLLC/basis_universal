#!/usr/bin/env python3
"""
Full end-to-end transcoder test with automatic fallback.

- Generates a swirl image
- Compresses it using native OR WASM (AUTO mode)
- Writes test.ktx2
- Decodes it using whichever backends are available:
    * AUTO (native if present, otherwise WASM)
    * Native (if available)
    * WASM   (if available)
- Produces PNG outputs for all successful backends
"""

import numpy as np
from math import sin, cos, atan2, hypot
from PIL import Image
import sys

from basisu_py.codec import Encoder, EncoderBackend
from basisu_py.transcoder import Transcoder, TranscoderBackend
from basisu_py.constants import (
    BasisTexFormat,
    BasisQuality,
    BasisEffort,
    BasisFlags,
)


# -------------------------------------------------------------------
# Create an RGBA swirl test image
# -------------------------------------------------------------------
def make_swirl(w=256, h=256):
    arr = np.zeros((h, w, 4), dtype=np.uint8)

    cx, cy = w / 2.0, h / 2.0

    for y in range(h):
        for x in range(w):
            dx, dy = x - cx, y - cy
            dist = hypot(dx, dy)
            angle = atan2(dy, dx)

            r = int((sin(dist * 0.15) * 0.5 + 0.5) * 255)
            g = int((sin(angle * 3.0)   * 0.5 + 0.5) * 255)
            b = int((cos(dist * 0.10 + angle * 2.0) * 0.5 + 0.5) * 255)

            arr[y, x] = (r, g, b, 255)

    return arr


# -------------------------------------------------------------------
# Try loading transcoder with a backend, return (success, transcoder)
# -------------------------------------------------------------------
def try_transcoder(backend):
    try:
        t = Transcoder(backend)
        print(f"[OK] Loaded transcoder backend '{backend}' ({t.backend_name})")
        return True, t
    except Exception as e:
        print(f"[SKIP] Backend '{backend}' unavailable:", e)
        return False, None


# -------------------------------------------------------------------
# Try loading encoder with a backend, return blob or None
# -------------------------------------------------------------------
def try_encoder(backend, img):
    try:
        enc = Encoder(backend)
        print(f"[OK] Loaded encoder backend '{backend}' ({enc.backend_name})")
    except Exception as e:
        print(f"[SKIP] Encoder backend '{backend}' unavailable:", e)
        return None

    try:
        print(f"[Test] Compressing swirl -> KTX2 using {enc.backend_name}...")
        blob = enc.compress(
            img,
            format=-1,
            quality=BasisQuality.MAX,
            effort=BasisEffort.DEFAULT,
            flags=BasisFlags.KTX2_OUTPUT | BasisFlags.SRGB
        )
        return blob
    except Exception as e:
        print(f"[FAIL] Compression failed on backend '{backend}':", e)
        return None


# -------------------------------------------------------------------
# Decode blob with a given transcoder
# -------------------------------------------------------------------
def decode_with_backend(name, t, blob):
    try:
        rgba = t.decode_rgba(blob)
        outname = f"decoded_{name}.png"
        Image.fromarray(rgba, mode="RGBA").save(outname)
        print(f"  --> {name}: decoded successfully, wrote {outname}")
    except Exception as e:
        print(f"  [FAIL] decode_rgba on backend '{name}':", e)


# -------------------------------------------------------------------
# Main test
# -------------------------------------------------------------------
if __name__ == "__main__":
    print("========== BasisU End-to-End Compression & Transcoding Test ==========")

    # -------------------------------------------------------
    # Generate swirl test
    # -------------------------------------------------------
    img = make_swirl(256, 256)
    print("[Test] Generated swirl:", img.shape)

    # -------------------------------------------------------
    # Try AUTO encoder (native if available, else WASM)
    # -------------------------------------------------------
    blob = try_encoder(EncoderBackend.AUTO, img)
    if blob is None:
        print("[FAIL] Could not encode using AUTO backend; aborting.")
        sys.exit(1)

    # Save test.ktx2
    with open("test.ktx2", "wb") as f:
        f.write(blob)
    print("[Test] Wrote: test.ktx2")

    # -------------------------------------------------------
    # Test transcoding using AUTO
    # -------------------------------------------------------
    print("\n[Test] Decoding via AUTO backend...")
    ok_auto, t_auto = try_transcoder(TranscoderBackend.AUTO)
    if ok_auto:
        decode_with_backend("auto", t_auto, blob)

    # -------------------------------------------------------
    # Test NATIVE explicitly (if available)
    # -------------------------------------------------------
    print("\n[Test] Decoding via NATIVE backend...")
    ok_native, t_native = try_transcoder(TranscoderBackend.NATIVE)
    if ok_native:
        decode_with_backend("native", t_native, blob)

    # -------------------------------------------------------
    # Test WASM explicitly (if available)
    # -------------------------------------------------------
    print("\n[Test] Decoding via WASM backend...")
    ok_wasm, t_wasm = try_transcoder(TranscoderBackend.WASM)
    if ok_wasm:
        decode_with_backend("wasm", t_wasm, blob)

    print("\n========== DONE ==========")

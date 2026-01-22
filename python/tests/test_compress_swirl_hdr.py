#!/usr/bin/env python3
import numpy as np
from math import sin, cos, atan2, hypot
from basisu_py.codec import Encoder, EncoderBackend
from basisu_py.constants import BasisTexFormat, BasisQuality, BasisEffort, BasisFlags


# --------------------------------------------------------------
# Procedural HDR swirl pattern (float32 RGBA)
# --------------------------------------------------------------
def make_hdr_swirl_image(w=256, h=256):
    arr = np.zeros((h, w, 4), dtype=np.float32)

    cx = w / 2.0
    cy = h / 2.0

    for y in range(h):
        for x in range(w):
            dx = x - cx
            dy = y - cy
            dist = hypot(dx, dy)
            angle = atan2(dy, dx)

            r = (sin(dist * 0.15) * 0.5 + 0.5)
            g = (sin(angle * 3.0)   * 0.5 + 0.5)
            b = (cos(dist * 0.10 + angle * 2.0) * 0.5 + 0.5)

            arr[y, x] = (r, g, b, 1.0)  # full alpha

    return arr


# --------------------------------------------------------------
# Test encode using a given backend
# --------------------------------------------------------------
def compress_hdr_swirl(backend, outfile):
    print(f"\n========== Testing HDR {backend} backend ==========")

    hdr = make_hdr_swirl_image(256, 256)
    print("Generated HDR swirl image:", hdr.shape, hdr.dtype)

    enc = Encoder(backend=backend)

    blob = enc.compress(
        hdr,
        format=-1,  # auto-select HDR (UASTC_HDR_4x4)
        quality=BasisQuality.MAX,
        effort=BasisEffort.DEFAULT,
        flags=BasisFlags.KTX2_OUTPUT | BasisFlags.SRGB
    )

    print(f"Compressed blob size: {len(blob)} bytes")

    with open(outfile, "wb") as f:
        f.write(blob)

    print(f"Wrote: {outfile}")
    print("==============================================")


# --------------------------------------------------------------
# Main
# --------------------------------------------------------------
if __name__ == "__main__":
    # Native backend
    try:
        compress_hdr_swirl(EncoderBackend.NATIVE, "hdr_swirl_native.ktx2")
    except Exception as e:
        print("Native HDR backend ERROR:", e)

    # WASM backend
    try:
        compress_hdr_swirl(EncoderBackend.WASM, "hdr_swirl_wasm.ktx2")
    except Exception as e:
        print("WASM HDR backend ERROR:", e)

#!/usr/bin/env python3
import numpy as np
from PIL import Image
from math import sin, cos, atan2, hypot

from basisu_py.codec import Encoder, EncoderBackend
from basisu_py.constants import BasisTexFormat, BasisQuality, BasisEffort, BasisFlags


# --------------------------------------------------------------
# Procedural swirl pattern (RGBA8)
# --------------------------------------------------------------
def make_swirl_image(w=256, h=256):
    arr = np.zeros((h, w, 4), dtype=np.uint8)

    cx = w / 2.0
    cy = h / 2.0

    for y in range(h):
        for x in range(w):
            dx = x - cx
            dy = y - cy

            dist = hypot(dx, dy)
            angle = atan2(dy, dx)

            r = int((sin(dist * 0.15) * 0.5 + 0.5) * 255)
            g = int((sin(angle * 3.0)   * 0.5 + 0.5) * 255)
            b = int((cos(dist * 0.10 + angle * 2.0) * 0.5 + 0.5) * 255)

            arr[y, x] = (r, g, b, 255)

    return arr


# --------------------------------------------------------------
# Test encode using a given backend
# --------------------------------------------------------------
def compress_swirl(backend, outfile):
    print(f"\n========== Testing {backend} backend ==========")

    # Build procedural image
    swirl = make_swirl_image(256, 256)
    print("Generated swirl image:", swirl.shape)

    # Create encoder
    enc = Encoder(backend=backend)

    # Compress
    blob = enc.compress(
        swirl,
        format=BasisTexFormat.cUASTC_LDR_4x4,
        quality=BasisQuality.MAX,
        effort=BasisEffort.DEFAULT,
        flags=BasisFlags.KTX2_OUTPUT | BasisFlags.SRGB
    )

    print(f"Compressed blob size: {len(blob)} bytes")

    # Save output
    with open(outfile, "wb") as f:
        f.write(blob)

    print(f"Wrote: {outfile}")
    print("==============================================")


# --------------------------------------------------------------
# Main
# --------------------------------------------------------------
if __name__ == "__main__":
    # Test native backend
    try:
        compress_swirl(EncoderBackend.NATIVE, "swirl_native.ktx2")
    except Exception as e:
        print("Native backend ERROR:", e)

    # Test WASM backend
    try:
        compress_swirl(EncoderBackend.WASM, "swirl_wasm.ktx2")
    except Exception as e:
        print("WASM backend ERROR:", e)

# basic_test.py
import sys
sys.path.append("basisu_py")   # make sure Python can load the .so

import basisu_python as bu
from constants import *

import ctypes
import math

def generate_swirl_rgba8(width, height):
    """
    Generate a smooth colorful swirl procedural RGBA8 test image.
    Returns: a ctypes array of type (c_ubyte * (width * height * 4))
    """
    pixel_count = width * height * 4
    img = (ctypes.c_ubyte * pixel_count)()

    for y in range(height):
        for x in range(width):
            i = (y * width + x) * 4

            dx = x - width / 2
            dy = y - height / 2

            dist = math.hypot(dx, dy)
            angle = math.atan2(dy, dx)

            # Color swirl pattern
            r = int((math.sin(dist * 0.15) * 0.5 + 0.5) * 255)
            g = int((math.sin(angle * 3.0) * 0.5 + 0.5) * 255)
            b = int((math.cos(dist * 0.10 + angle * 2.0) * 0.5 + 0.5) * 255)

            img[i + 0] = r & 255
            img[i + 1] = g & 255
            img[i + 2] = b & 255
            img[i + 3] = 255

    return img

def generate_test_pattern_rgba8(width, height):
    """
    Generate a simple deterministic RGBA8 test pattern:
    R = x
    G = y
    B = x^y
    A = 255
    """
    import ctypes

    pixel_count = width * height * 4
    img = (ctypes.c_ubyte * pixel_count)()

    for y in range(height):
        for x in range(width):
            i = (y * width + x) * 4

            img[i + 0] = x & 0xFF
            img[i + 1] = y & 0xFF
            img[i + 2] = (x ^ y) & 0xFF
            img[i + 3] = 255

    return img
    
# ------------------------------------------------------------
# BasisU compression test (NATIVE C++)
# ------------------------------------------------------------

print("Native BasisU version:", bu.get_version())
bu.init()

# Create comp params
params = bu.new_params()
print("Params handle:", params)

# Create RGBA8 swirl (64 x 64)
W, H = 512, 512
pixel_count = W * H * 4

# Generate swirl image in PYTHON memory

img = generate_swirl_rgba8(W, H)
#img = generate_test_pattern_rgba8(W, H)

# Allocate memory inside NATIVE C++ heap
img_ptr = bu.alloc(pixel_count)

# Copy Python swirl image ? C++ heap buffer
ctypes.memmove(img_ptr, img, pixel_count)

# Set into BasisU
pitch = W * 4
ok = bu.set_image_rgba32(params, 0, img_ptr, W, H, pitch)
print("Set image:", ok)

# Compress (UASTC LDR 4x4 = 1)
ok = bu.compress(
    params,
    BasisTexFormat.cASTC_LDR_4x4, # basis_tex_format
     BasisQuality.MAX,    # quality
    BasisEffort.DEFAULT,      # effort
    BasisFlags.KTX2_OUTPUT | BasisFlags.SRGB | BasisFlags.THREADED | BasisFlags.DEBUG_OUTPUT | BasisFlags.VERBOSE,      # flags
    0.0     # rdo
)
print("Compress:", ok)

# Retrieve compressed data
size = bu.get_comp_data_size(params)
ofs  = bu.get_comp_data_ofs(params)

print("Output size =", size, "ptr =", ofs)

# Copy bytes out of native memory
byte_ptr = ctypes.cast(ofs, ctypes.POINTER(ctypes.c_ubyte))
blob = bytes(byte_ptr[i] for i in range(size))

print("First 16 bytes:", blob[:16])

# Save to KTX2
with open("out_native.ktx2", "wb") as f:
    f.write(blob)

print("Saved out_native.ktx2")

# Cleanup
bu.delete_params(params)
bu.free(img_ptr)

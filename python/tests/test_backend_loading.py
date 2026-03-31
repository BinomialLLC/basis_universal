#!/usr/bin/env python3
import numpy as np
from PIL import Image

from basisu_py.codec import Encoder, EncoderBackend
from basisu_py.constants import BasisTexFormat

print("========== BACKEND LOADING TEST ==========\n")

# --------------------------------------------------------------
# 1. Test native backend (if available)
# --------------------------------------------------------------
print("Testing native backend...")

try:
    enc_native = Encoder(backend=EncoderBackend.NATIVE)
    print("  [OK] Native backend loaded")
except Exception as e:
    print("  [FAIL] Native backend failed to load:", e)
    enc_native = None

# If native loaded, test very basic functionality
if enc_native:
    try:
        version = enc_native._native.get_version()
        print(f"  Native get_version() ? {version}")

        ptr = enc_native._native.alloc(16)
        print(f"  Native alloc() returned ptr = {ptr}")

        enc_native._native.free(ptr)
        print(f"  Native free() OK")

        print("  [OK] Native basic operations working.\n")
    except Exception as e:
        print("  [FAIL] Native operations error:", e)
else:
    print("  Skipping native basic operations.\n")

# --------------------------------------------------------------
# 2. Test WASM backend
# --------------------------------------------------------------
print("\nTesting WASM backend...")

try:
    enc_wasm = Encoder(backend=EncoderBackend.WASM)
    print("  [OK] WASM backend loaded")
except Exception as e:
    print("  [FAIL] WASM backend failed to load:", e)
    enc_wasm = None

# If WASM loaded, test basic methods
if enc_wasm and enc_wasm._wasm is not None:
    try:
        version = enc_wasm._wasm.get_version()
        print(f"  WASM get_version() ? {version}")

        ptr = enc_wasm._wasm.alloc(16)
        print(f"  WASM alloc() returned ptr = {ptr}")

        enc_wasm._wasm.free(ptr)
        print(f"  WASM free() OK")

        print("  [OK] WASM basic operations working.\n")
    except Exception as e:
        print("  [FAIL] WASM operations error:", e)
else:
    print("  Skipping WASM basic operations.\n")

print("\n========== DONE ==========\n")

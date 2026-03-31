#!/usr/bin/env python3
import sys
from basisu_py.transcoder import Transcoder, TranscoderBackend
from basisu_py.constants import BasisTexFormat

print("========== TESTING TRANSCODER BACKENDS ==========\n")

# Load some test data (ensure test.ktx2 exists)
try:
    test_data = open("test.ktx2", "rb").read()
    print("[INFO] Loaded test.ktx2")
except FileNotFoundError:
    print("[ERROR] test.ktx2 not found. Create one first via encoder tests.")
    sys.exit(1)


# -------------------------------------------------------------------
# 1. Test NATIVE backend
# -------------------------------------------------------------------
print("\n--- Testing NATIVE transcoder backend ---")

try:
    t_native = Transcoder(TranscoderBackend.NATIVE)
    print("  [OK] Native backend loaded")

    version = t_native.get_version()
    print(f"  Native get_version() = {version}")

    # Open KTX2
    raw = t_native.open(test_data)
    print("  [OK] Opened KTX2 (native)")

    # Query some basic properties
    print("   Width :", t_native.get_width(raw))
    print("   Height:", t_native.get_height(raw))
    print("   Levels:", t_native.get_levels(raw))

    # Cleanup
    t_native.close(raw)
    print("  [OK] Native transcoder basic operations working.")

except Exception as e:
    print("  [FAIL] Native transcoder error:", e)


# -------------------------------------------------------------------
# 2. Test WASM backend
# -------------------------------------------------------------------
print("\n--- Testing WASM transcoder backend ---")

try:
    t_wasm = Transcoder(TranscoderBackend.WASM)
    print("  [OK] WASM backend loaded")

    version = t_wasm.get_version()
    print(f"  WASM get_version() = {version}")

    raw = t_wasm.open(test_data)
    print("  [OK] Opened KTX2 (wasm)")

    print("   Width :", t_wasm.get_width(raw))
    print("   Height:", t_wasm.get_height(raw))
    print("   Levels:", t_wasm.get_levels(raw))

    t_wasm.close(raw)
    print("  [OK] WASM transcoder basic operations working.")

except Exception as e:
    print("  [FAIL] WASM transcoder error:", e)


print("\n========== DONE ==========")

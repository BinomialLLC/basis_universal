# test_transcoder_basic.py
import sys
import os

# Make sure Python can find the .so file
sys.path.append("basisu_py")   # Adjust if needed

try:
    import basisu_transcoder_python as bt
except ImportError as e:
    print("Failed to import basisu_transcoder_python:", e)
    raise

print("Successfully loaded basisu_transcoder_python")

# Call bt_get_version() via the pybind11 binding
try:
    version = bt.get_version()
    print("Transcoder version:", version)
except Exception as e:
    print("Error calling bt_get_version:", e)
    raise

print("Basic transcoder test complete.")

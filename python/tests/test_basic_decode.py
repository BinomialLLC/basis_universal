from basisu_py import Transcoder
from PIL import Image
import numpy as np

# Load input file
with open("test.ktx2", "rb") as f:
    data = f.read()

# Decode (AUTO backend)
t = Transcoder()
rgba = t.decode_rgba(data)   # returns HxWx4 uint8 NumPy array

print("Decoded:", rgba.shape, rgba.dtype)

# Convert to Pillow Image and save
img = Image.fromarray(rgba, mode="RGBA")
img.save("decoded.png")

print("Wrote decoded.png")
# compress_test.py
from .basisu_wasm import *

# === Load WASM ===
codec = BasisuWasm("basisu_py/wasm/basisu_module_st.wasm")
codec.load()

print("Version =", codec.version())

# === Build test image ===
W, H = 256, 256
BYTES_PER_PIXEL = 4
pitch = W * BYTES_PER_PIXEL

img = bytearray(W * H * 4)

for y in range(H):
    for x in range(W):
        i = (y * W + x) * 4
        img[i + 0] = x & 0xFF          # R
        img[i + 1] = y & 0xFF          # G
        img[i + 2] = (x ^ y) & 0xFF    # B
        img[i + 3] = 255               # A

# === Upload image to WASM memory ===
img_ptr = codec.alloc(len(img))
codec.write_bytes(img_ptr, img)

# === Create comp_params ===
params = codec.new_params()

# === Set image into comp_params ===
ok = codec.set_image_rgba32(params, 0, img_ptr, W, H, pitch)
print("Set image:", ok)

# === Compress ===
ok = codec.compress(
    params,
    tex_format=BasisTexFormat.cUASTC_LDR_4x4,
    quality=100,
    effort=BasisEffort.DEFAULT,
    flags=BasisFlags.KTX2_OUTPUT | BasisFlags.SRGB,
    rdo_quality=0.0
)
print("Compress result:", ok)

# === Retrieve compressed blob ===
ofs  = codec.get_comp_data_ofs(params)
size = codec.get_comp_data_size(params)
print("Output size =", size)

comp_data = codec.read_bytes(ofs, size)
print("First 16 bytes:", comp_data[:16])

# === Save to KTX2 ===
with open("test.ktx2", "wb") as f:
    f.write(comp_data)

print("File written: test.ktx2")

# === Cleanup ===
codec.delete_params(params)
codec.free(img_ptr)

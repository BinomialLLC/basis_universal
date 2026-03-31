# compress_test_float.py

from .basisu_wasm import BasisuWasm, BasisTexFormat, BasisEffort, BasisFlags, BasisQuality
import struct   # for packing floats

# === Load WASM ===
codec = BasisuWasm("basisu_py/wasm/basisu_module_st.wasm")
codec.load()

print("Version =", codec.version())

# === Build a 256x256 FLOAT RGBA image ===
W, H = 256, 256
BYTES_PER_PIXEL = 16  # float32 * 4
pitch = W * BYTES_PER_PIXEL

# Float image stored as bytearray of packed floats
img = bytearray(W * H * BYTES_PER_PIXEL)

for y in range(H):
    for x in range(W):
        # Create some float HDR gradient pattern
        r = float(x) / W               # 0.0 ? 1.0
        g = float(y) / H               # 0.0 ? 1.0
        b = float(x ^ y) / 255.0       # quirky pattern
        a = 1.0

        i = (y * W + x) * 4

        # pack into img bytearray
        struct.pack_into("ffff", img, i*4, r, g, b, a)

print("Created FLOAT RGBA image.")

# === Upload to WASM memory ===
img_ptr = codec.alloc(len(img))
codec.write_bytes(img_ptr, img)
print("Copied float image into WASM heap at", img_ptr)

# === Create params ===
params = codec.new_params()

# === Set FLOAT RGBA image ===
ok = codec.set_image_float_rgba(params, 0, img_ptr, W, H, pitch)
print("Set float RGBA:", ok)

# === Compress using HDR UASTC 4x4 ===
ok = codec.compress(
    params,
    tex_format=BasisTexFormat.cUASTC_HDR_4x4,
    quality=BasisQuality.MAX,
    effort=BasisEffort.DEFAULT,
    flags=BasisFlags.KTX2_OUTPUT | BasisFlags.REC2020,  # optional: HDR color space
    rdo_quality=0.0
)

print("Compression result:", ok)

# === Retrieve compressed HDR KTX2 ===
ofs  = codec.get_comp_data_ofs(params)
size = codec.get_comp_data_size(params)

print("Output size =", size)
data = codec.read_bytes(ofs, size)

print("First 16 bytes:", data[:16])

# === Save to test_hdr.ktx2 ===
with open("test_hdr.ktx2", "wb") as f:
    f.write(data)

print("Wrote test_hdr.ktx2")

# === Cleanup ===
codec.delete_params(params)
codec.free(img_ptr)

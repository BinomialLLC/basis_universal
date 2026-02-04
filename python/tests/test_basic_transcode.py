from basisu_py import Transcoder

with open("test.ktx2", "rb") as f:
    data = f.read()

t = Transcoder()    # AUTO backend
img = t.decode_rgba(data)

print("Decoded shape:", img.shape)
print("dtype:", img.dtype)

from basisu_py import Transcoder
from basisu_py.transcoder import TranscoderBackend

t = Transcoder(backend=TranscoderBackend.WASM)
print("Backend:", t.backend_name)
t.decode_rgba(open("test.ktx2","rb").read())

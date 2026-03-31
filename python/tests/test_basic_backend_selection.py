from basisu_py import Encoder

enc = Encoder()    # AUTO mode
print("Encoder backend:", enc.backend)
print("Native loaded:", enc._native is not None)
print("WASM loaded:", enc._wasm is not None)
print("Version:", enc._native.get_version() if enc._native else enc._wasm.get_version())

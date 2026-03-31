# basisu_py/wasm/wasm_encoder.py

import wasmtime
import ctypes

from ..constants import BasisTexFormat, BasisQuality, BasisEffort, BasisFlags


class BasisuWasmEncoder:
    def __init__(self, wasm_path):
        self.wasm_path = wasm_path
        self.engine = None
        self.store = None
        self.memory = None
        self.exports = None

    # ------------------------------------------------------
    # Initialize WASM + WASI
    # ------------------------------------------------------
    def _init_engine(self):
        self.engine = wasmtime.Engine()
        self.store = wasmtime.Store(self.engine)

        wasi = wasmtime.WasiConfig()
        wasi.argv = ["basisu-wasm"]
        wasi.inherit_stdout()
        wasi.inherit_stderr()
        self.store.set_wasi(wasi)

    def load(self):
        self._init_engine()

        module = wasmtime.Module.from_file(self.engine, self.wasm_path)
        linker = wasmtime.Linker(self.engine)
        linker.define_wasi()

        instance = linker.instantiate(self.store, module)
        self.exports = instance.exports(self.store)
        self.memory = self.exports["memory"]

        # Initialize if present
        if "bu_init" in self.exports:
            self.exports["bu_init"](self.store)

        print("[WASM Encoder] Loaded:", self.wasm_path)

    # ------------------------------------------------------
    # Access raw linear memory buffer
    # ------------------------------------------------------
    def _buf(self):
        raw_ptr = self.memory.data_ptr(self.store)
        size = self.memory.data_len(self.store)
        addr = ctypes.addressof(raw_ptr.contents)
        return (ctypes.c_ubyte * size).from_address(addr)

    # ------------------------------------------------------
    # Version
    # ------------------------------------------------------
    def get_version(self):
        return self.exports["bu_get_version"](self.store)

    # ------------------------------------------------------
    # Memory alloc/free
    # ------------------------------------------------------
    def alloc(self, size):
        return self.exports["bu_alloc"](self.store, size)

    def free(self, ptr):
        self.exports["bu_free"](self.store, ptr)

    # ------------------------------------------------------
    # Params
    # ------------------------------------------------------
    def new_params(self):
        return self.exports["bu_new_comp_params"](self.store)

    def delete_params(self, params):
        return self.exports["bu_delete_comp_params"](self.store, params)

    # ------------------------------------------------------
    # Image input
    # ------------------------------------------------------
    def set_image_rgba32(self, params, index, ptr, w, h, pitch):
        return self.exports["bu_comp_params_set_image_rgba32"](
            self.store, params, index, ptr, w, h, pitch
        )

    def set_image_float_rgba(self, params, index, ptr, w, h, pitch):
        return self.exports["bu_comp_params_set_image_float_rgba"](
            self.store, params, index, ptr, w, h, pitch
        )

    # ------------------------------------------------------
    # Compression
    # ------------------------------------------------------
    def compress(self, params, fmt, quality, effort, flags, rdo):
        return bool(self.exports["bu_compress_texture"](
            self.store, params, fmt, quality, effort, flags, rdo
        ))

    # ------------------------------------------------------
    # Output blob
    # ------------------------------------------------------
    def get_comp_data_size(self, params):
        return self.exports["bu_comp_params_get_comp_data_size"](self.store, params)

    def get_comp_data_ofs(self, params):
        return self.exports["bu_comp_params_get_comp_data_ofs"](self.store, params)

    # ------------------------------------------------------
    # Raw memory I/O
    # ------------------------------------------------------
    def write_bytes(self, ptr, data):
        buf = self._buf()
        buf[ptr:ptr + len(data)] = data

    def read_bytes(self, ptr, size):
        buf = self._buf()
        return bytes(buf[ptr:ptr + size])
        
    # NEW unified names:
    def write_memory(self, ptr, data):
        self.write_bytes(ptr, data)

    def read_memory(self, ptr, size):
        return self.read_bytes(ptr, size)

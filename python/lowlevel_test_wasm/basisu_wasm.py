# basisu_wasm.py
import wasmtime
import ctypes
import sys

sys.path.append("basisu_py") # our shared .py files

from constants import *

class BasisuWasm:
    def __init__(self, path):
        self.path = path
        self.engine = None
        self.store = None
        self.memory = None
        self.exports = None

    # -----------------------------------------------
    # Internal helper: build WASI + Wasmtime engine
    # -----------------------------------------------
    def _init_engine(self):
        self.engine = wasmtime.Engine()
        self.store = wasmtime.Store(self.engine)

        wasi = wasmtime.WasiConfig()
        wasi.argv = ["basisu"]
        wasi.inherit_stdout()
        wasi.inherit_stderr()
        self.store.set_wasi(wasi)

        return wasi
    
    # -----------------------------------------------
    # Create linker and instantiate WASM module
    # -----------------------------------------------
    def load(self):
        self._init_engine()

        module = wasmtime.Module.from_file(self.engine, self.path)
        linker = wasmtime.Linker(self.engine)
        linker.define_wasi()

        instance = linker.instantiate(self.store, module)

        self.exports = instance.exports(self.store)
        self.memory = self.exports["memory"]
        
        if "bu_init" in self.exports:
            self.exports["bu_init"](self.store)

        print("WASM loaded:", self.path)

    # -----------------------------------------------
    # Read/write WASM linear memory via ctypes
    # -----------------------------------------------
    def _wasm_buf(self):
        raw_ptr = self.memory.data_ptr(self.store)
        length = self.memory.data_len(self.store)
        addr = ctypes.addressof(raw_ptr.contents)
        return (ctypes.c_ubyte * length).from_address(addr)

    # -----------------------------------------------
    # Exported API accessors
    # -----------------------------------------------
    def init(self):
        return self.exports["bu_init"](self.store)
        
    def version(self):
        return self.exports["bu_get_version"](self.store)

    def alloc(self, size):
        return self.exports["bu_alloc"](self.store, size)

    def free(self, ptr):
        return self.exports["bu_free"](self.store, ptr)

    def new_params(self):
        return self.exports["bu_new_comp_params"](self.store)

    def delete_params(self, ptr):
        return self.exports["bu_delete_comp_params"](self.store, ptr)

    def set_image_rgba32(self, params, image_index, img_ptr, w, h, pitch):
        return self.exports["bu_comp_params_set_image_rgba32"](
            self.store, params, image_index, img_ptr, w, h, pitch
        )
        
    def set_image_float_rgba(self, params, image_index, img_ptr, w, h, pitch):
        return self.exports["bu_comp_params_set_image_float_rgba"](
            self.store, params, image_index, img_ptr, w, h, pitch
        )

    # Normally quality_level controls the quality. 
    # If quality_level==-1, then rdo_quality (a low-level parameter) directly 
    # controls each codec's quality setting. Normally set to 0.
        
    def compress_texture_lowlevel(self, params,
                              tex_format,
                              quality_level,
                              effort_level,
                              flags_and_quality,
                              rdo_quality):
                              
        return self.exports["bu_compress_texture"](
            self.store,
            params,
            tex_format,
            quality_level,
            effort_level,
            flags_and_quality,
            rdo_quality
        )

    def compress(self, params,
             tex_format=BasisTexFormat.cUASTC_LDR_4x4,
             quality=BasisQuality.MAX,
             effort=BasisEffort.DEFAULT,
             flags=BasisFlags.NONE,
             rdo_quality=0.0):
             
        return bool(self.compress_texture_lowlevel(
            params,
            tex_format,
            quality,
            effort,
            flags,
            rdo_quality
        ))

    def get_comp_data_ofs(self, params):
        return self.exports["bu_comp_params_get_comp_data_ofs"](self.store, params)

    def get_comp_data_size(self, params):
        return self.exports["bu_comp_params_get_comp_data_size"](self.store, params)

    # -----------------------------------------------
    # Copy bytes into WASM memory
    # -----------------------------------------------
    def write_bytes(self, wasm_ptr, data: bytes):
        buf = self._wasm_buf()
        buf[wasm_ptr:wasm_ptr+len(data)] = data

    # -----------------------------------------------
    # Read bytes from WASM memory
    # -----------------------------------------------
    def read_bytes(self, wasm_ptr, size):
        buf = self._wasm_buf()
        return bytes(buf[wasm_ptr:wasm_ptr+size])

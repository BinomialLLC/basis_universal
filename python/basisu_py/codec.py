# basisu_py/codec.py

import importlib
import numpy as np
from PIL import Image
import ctypes

from .constants import BasisTexFormat, BasisQuality, BasisEffort, BasisFlags
from pathlib import Path

class EncoderBackend:
    NATIVE = "native"
    WASM = "wasm"
    AUTO = "auto"

class Encoder:

    def __init__(self, backend=EncoderBackend.AUTO):
        self.backend = backend
        self._native = None
        self._wasm = None
        self.backend_name = None

        # ------------------------------------------------------------------
        # Try native first (AUTO or NATIVE modes)
        # ------------------------------------------------------------------
        if backend in (EncoderBackend.AUTO, EncoderBackend.NATIVE):
            try:
                import basisu_py.basisu_python as native_encoder
                native_encoder.init()

                self._native = native_encoder
                self._wasm = None
                self.backend_name = "NATIVE"

                print("[Encoder] Using native backend")
                return

            except Exception as e:
                if backend == EncoderBackend.NATIVE:
                    raise RuntimeError(
                        f"[Encoder] Native backend requested but unavailable: {e}"
                    )
                print("[Encoder] Native unavailable; falling back to WASM:", e)

        # ------------------------------------------------------------------
        # Fallback to WASM (AUTO or explicitly WASM)
        # ------------------------------------------------------------------
        try:
            from basisu_py.wasm.wasm_encoder import BasisuWasmEncoder
        except Exception as e:
            raise RuntimeError(
                f"[Encoder] WASM backend cannot be imported: {e}\n"
                "Make sure wasmtime is installed and basisu_py/wasm/*.wasm exist."
            )

        wasm_path = Path(__file__).parent / "wasm" / "basisu_module_st.wasm"
        self._wasm = BasisuWasmEncoder(str(wasm_path))
        self._wasm.load()
        self._native = None
        self.backend_name = "WASM"

        print("[Encoder] Using WASM backend")           
            

    # ------------------------------------------------------
    # Public API
    # ------------------------------------------------------
    def compress(self,
                 image,
                 format=-1,
                 quality=BasisQuality.MAX,
                 effort=BasisEffort.DEFAULT,
                 flags=BasisFlags.KTX2_OUTPUT | BasisFlags.SRGB | BasisFlags.THREADED | BasisFlags.XUASTC_LDR_FULL_ZSTD):
                
        rgba_bytes, w, h, is_hdr = self._convert_input_to_rgba_bytes(image)
        
        # Auto-select format if user passed -1
        if format == -1:
            if is_hdr:
                format = BasisTexFormat.cUASTC_HDR_6x6
            else:
                format = BasisTexFormat.cXUASTC_LDR_6x6

        if self._native:
            return self._compress_native(rgba_bytes, w, h, format, quality, effort, flags, is_hdr)
        else:
            return self._compress_wasm(rgba_bytes, w, h, format, quality, effort, flags, is_hdr)
            
    def compress_float32(self, arr, **kwargs):
        if not isinstance(arr, np.ndarray) or arr.dtype != np.float32:
            raise ValueError("compress_float32 requires float32 NumPy HxWx4 array")

        return self.compress(arr, **kwargs)

    # ------------------------------------------------------
    # Native backend
    # ------------------------------------------------------
    def _compress_native(self, bytes_data, w, h, fmt, quality, effort, flags, is_hdr=False):
        enc = self._native

        params = enc.new_params()

        try:
            buf_ptr = enc.alloc(len(bytes_data))

            # Write raw bytes (uint8 or float32)
            ctypes.memmove(buf_ptr, bytes_data, len(bytes_data))

            if is_hdr:
                ok = enc.set_image_float_rgba(params, 0, buf_ptr, w, h, w * 16)  # 4 floats = 16 bytes per pixel
            else:
                ok = enc.set_image_rgba32(params, 0, buf_ptr, w, h, w * 4)

            if not ok:
                raise RuntimeError("Native encoder: set_image failed (HDR or LDR)")

            ok = enc.compress(params, fmt, quality, effort, flags, 0.0)
            if not ok:
                raise RuntimeError("Native encoder: compress() failed")

            size = enc.get_comp_data_size(params)
            ofs = enc.get_comp_data_ofs(params)
            blob = enc.read_memory(ofs, size)
            return blob

        finally:
            enc.delete_params(params)
            if buf_ptr:
                enc.free(buf_ptr)

    # ------------------------------------------------------
    # WASM backend
    # ------------------------------------------------------
    def _compress_wasm(self, bytes_data, w, h, fmt, quality, effort, flags, is_hdr=False):
        enc = self._wasm

        params = enc.new_params()

        try:
            buf_ptr = enc.alloc(len(bytes_data))
            enc.write_bytes(buf_ptr, bytes_data)

            if is_hdr:
                ok = enc.set_image_float_rgba(params, 0, buf_ptr, w, h, w * 16)
            else:
                ok = enc.set_image_rgba32(params, 0, buf_ptr, w, h, w * 4)

            if not ok:
                raise RuntimeError("WASM encoder: set_image failed (HDR or LDR)")

            ok = enc.compress(params, fmt, quality, effort, flags, 0.0)
            if not ok:
                raise RuntimeError("WASM encoder: compress() failed")

            size = enc.get_comp_data_size(params)
            ofs = enc.get_comp_data_ofs(params)
            blob = enc.read_bytes(ofs, size)
            return blob

        finally:
            enc.delete_params(params)
            if buf_ptr:
                enc.free(buf_ptr)

    # ------------------------------------------------------
    # Image conversion
    # ------------------------------------------------------
    def _convert_input_to_rgba_bytes(self, image):
        """
        Accept:
          - Pillow Image (LDR) -> returns uint8 bytes
          - NumPy uint8 LDR -> returns uint8 bytes
          - NumPy float32 HDR -> returns float32 bytes
        Returns (bytes, width, height, is_hdr)
        """

        # Pillow image -> LDR
        if isinstance(image, Image.Image):
            image = image.convert("RGBA")
            arr = np.array(image, dtype=np.uint8)
            h, w = arr.shape[:2]
            return arr.tobytes(), w, h, False

        # NumPy array
        elif isinstance(image, np.ndarray):

            # HDR float32 image
            if image.dtype == np.float32:
                if image.ndim != 3 or image.shape[2] not in (3,4):
                    raise ValueError("HDR NumPy image must be HxWx3 or HxWx4 float32")

                h, w, c = image.shape

                # Expand RGB -> RGBA if needed
                if c == 3:
                    alpha = np.ones((h, w, 1), dtype=np.float32)
                    arr = np.concatenate([image, alpha], axis=2)
                else:
                    arr = image

                return arr.tobytes(), w, h, True

            # LDR uint8 image
            if image.dtype == np.uint8:
                if image.ndim != 3 or image.shape[2] not in (3,4):
                    raise ValueError("LDR NumPy image must be HxWx3 or HxWx4 uint8")

                h, w, c = image.shape

                if c == 3:
                    alpha = np.full((h, w, 1), 255, dtype=np.uint8)
                    arr = np.concatenate([image, alpha], axis=2)
                else:
                    arr = image

                return arr.tobytes(), w, h, False

            raise ValueError("NumPy image must be uint8 (LDR) or float32 (HDR)")

        else:
            raise TypeError("compress() expects Pillow Image or NumPy array")

# basisu_py/transcoder.py
import numpy as np
from dataclasses import dataclass
from pathlib import Path

from basisu_py.constants import (
    TranscoderTextureFormat,
)

import importlib
import ctypes


# ---------------------------------------------------------------------------
# Enum to select backend
# ---------------------------------------------------------------------------
class TranscoderBackend:
    NATIVE = "native"
    WASM = "wasm"
    AUTO = "auto"


# ---------------------------------------------------------------------------
# Wrapper class storing pointer+handle
# ---------------------------------------------------------------------------
@dataclass
class KTX2Handle:
    ptr: int
    handle: int


# ---------------------------------------------------------------------------
# Main Transcoder class
# ---------------------------------------------------------------------------
class Transcoder:
    def __init__(self, backend=TranscoderBackend.AUTO):
        self._native = None
        self._wasm = None
        self.backend_name = None
        self.backend = None

        use_native = False

        # ------------------------------------------------------------------
        # Try native backend first if AUTO or NATIVE
        # ------------------------------------------------------------------
        if backend in (TranscoderBackend.AUTO, TranscoderBackend.NATIVE):
            try:
                native_mod = importlib.import_module("basisu_py.basisu_transcoder_python")
                native_mod.init()
                self._native = native_mod
                self.backend = native_mod
                self.backend_name = "NATIVE"
                use_native = True
                print("[Transcoder] Using native backend")
            except Exception as e:
                if backend == TranscoderBackend.NATIVE:
                    # Caller explicitly requested native - fail hard
                    raise RuntimeError(f"Native transcoder backend failed: {e}")
                print("[Transcoder] Native backend unavailable, reason:", e)
                self._native = None

        # ------------------------------------------------------------------
        # Fallback to WASM if native is not being used
        # ------------------------------------------------------------------
        if not use_native:
            try:
                from basisu_py.wasm.wasm_transcoder import BasisuWasmTranscoder
            except Exception as e:
                raise RuntimeError(
                    f"WASM backend cannot be imported: {e}\n"
                    "Ensure that:\n"
                    " - 'wasmtime' is installed\n"
                    " - basisu_py/wasm/*.wasm files are present in the install\n"
                )

            wasm_path = Path(__file__).parent / "wasm" / "basisu_transcoder_module_st.wasm"
            self._wasm = BasisuWasmTranscoder(str(wasm_path))
            self._wasm.load()
            self.backend = self._wasm
            self.backend_name = "WASM"
            print("[Transcoder] Using WASM backend")

        # Finally, bind the unified API to whichever backend we chose
        self._bind_backend(self.backend)

    # -----------------------------------------------------------------------
    # Unified backend binding (native or wasm)
    # -----------------------------------------------------------------------
    def _bind_backend(self, b):
        self.backend = b

        # ------------------ memory operations ------------------
        memory_mapping = [
            ("_alloc",  "alloc"),
            ("_free",   "free"),
            ("_write",  "write_memory"),
            ("_read",   "read_memory"),
        ]

        # ------------------ KTX2 core ------------------
        basis_mapping = [
            # basis_tex_format helpers
            ("basis_tex_format_is_xuastc_ldr", "basis_tex_format_is_xuastc_ldr"),
            ("basis_tex_format_is_astc_ldr",   "basis_tex_format_is_astc_ldr"),
            ("basis_tex_format_get_block_width",  "basis_tex_format_get_block_width"),
            ("basis_tex_format_get_block_height", "basis_tex_format_get_block_height"),
            ("basis_tex_format_is_hdr", "basis_tex_format_is_hdr"),
            ("basis_tex_format_is_ldr", "basis_tex_format_is_ldr"),

            # transcoder_texture_format helpers
            ("basis_get_bytes_per_block_or_pixel",          "basis_get_bytes_per_block_or_pixel"),
            ("basis_transcoder_format_has_alpha",           "basis_transcoder_format_has_alpha"),
            ("basis_transcoder_format_is_hdr",              "basis_transcoder_format_is_hdr"),
            ("basis_transcoder_format_is_ldr",              "basis_transcoder_format_is_ldr"),
            ("basis_transcoder_texture_format_is_astc",     "basis_transcoder_texture_format_is_astc"),
            ("basis_transcoder_format_is_uncompressed",     "basis_transcoder_format_is_uncompressed"),
            ("basis_get_uncompressed_bytes_per_pixel",      "basis_get_uncompressed_bytes_per_pixel"),
            ("basis_get_block_width",                       "basis_get_block_width"),
            ("basis_get_block_height",                      "basis_get_block_height"),
            ("basis_get_transcoder_texture_format_from_basis_tex_format","basis_get_transcoder_texture_format_from_basis_tex_format"),
            ("basis_is_format_supported",                   "basis_is_format_supported"),
            ("basis_compute_transcoded_image_size_in_bytes","basis_compute_transcoded_image_size_in_bytes"),
        ]
        
        ktx2_mapping = [
        
            ("ktx2_open",  "ktx2_open"),
            ("ktx2_close", "ktx2_close"),

            ("ktx2_get_width",   "ktx2_get_width"),
            ("ktx2_get_height",  "ktx2_get_height"),
            ("ktx2_get_levels",  "ktx2_get_levels"),
            ("ktx2_get_faces",   "ktx2_get_faces"),
            ("ktx2_get_layers",  "ktx2_get_layers"),

            ("ktx2_get_basis_tex_format", "ktx2_get_basis_tex_format"),

            ("ktx2_get_block_width",  "ktx2_get_block_width"),
            ("ktx2_get_block_height", "ktx2_get_block_height"),
            
            ("ktx2_has_alpha",        "ktx2_has_alpha"),

            # flags
            ("ktx2_is_hdr",            "ktx2_is_hdr"),
            ("ktx2_is_hdr_4x4",        "ktx2_is_hdr_4x4"),
            ("ktx2_is_hdr_6x6",        "ktx2_is_hdr_6x6"),
            ("ktx2_is_ldr",            "ktx2_is_ldr"),
            ("ktx2_is_srgb",           "ktx2_is_srgb"),
            ("ktx2_is_etc1s",          "ktx2_is_etc1s"),
            ("ktx2_is_uastc_ldr_4x4",  "ktx2_is_uastc_ldr_4x4"),
            ("ktx2_is_xuastc_ldr",     "ktx2_is_xuastc_ldr"),
            ("ktx2_is_astc_ldr",       "ktx2_is_astc_ldr"),
            ("ktx2_is_video",          "ktx2_is_video"),
            ("ktx2_get_ldr_hdr_upconversion_nit_multiplier", "ktx2_get_ldr_hdr_upconversion_nit_multiplier"),
            
            # DFD access
            ("ktx2_get_dfd_flags", "ktx2_get_dfd_flags"),
            ("ktx2_get_dfd_total_samples", "ktx2_get_dfd_total_samples"),
            ("ktx2_get_dfd_channel_id0", "ktx2_get_dfd_channel_id0"),
            ("ktx2_get_dfd_channel_id1", "ktx2_get_dfd_channel_id1"),
            ("ktx2_get_dfd_color_model", "ktx2_get_dfd_color_model"),
            ("ktx2_get_dfd_color_primaries", "ktx2_get_dfd_color_primaries"),
            ("ktx2_get_dfd_transfer_func", "ktx2_get_dfd_transfer_func"),

            # per-level info
            ("ktx2_get_level_orig_width",    "ktx2_get_level_orig_width"),
            ("ktx2_get_level_orig_height",   "ktx2_get_level_orig_height"),
            ("ktx2_get_level_actual_width",  "ktx2_get_level_actual_width"),
            ("ktx2_get_level_actual_height", "ktx2_get_level_actual_height"),

            ("ktx2_get_level_num_blocks_x", "ktx2_get_level_num_blocks_x"),
            ("ktx2_get_level_num_blocks_y", "ktx2_get_level_num_blocks_y"),
            ("ktx2_get_level_total_blocks", "ktx2_get_level_total_blocks"),

            ("ktx2_get_level_alpha_flag",  "ktx2_get_level_alpha_flag"),
            ("ktx2_get_level_iframe_flag", "ktx2_get_level_iframe_flag"),
                        
            # transcoding
            ("ktx2_start_transcoding",   "ktx2_start_transcoding"),
            ("ktx2_transcode_image_level", "ktx2_transcode_image_level"),
            
            # version
            ("get_version_fn", "get_version"),
        ]

        # Apply all mappings
        for public_name, backend_name in (memory_mapping + ktx2_mapping + basis_mapping):
            setattr(self, public_name, getattr(b, backend_name))

    # -----------------------------------------------------------------------
    # Public version query
    # -----------------------------------------------------------------------
    def get_version(self):
        return self.get_version_fn()

    # -----------------------------------------------------------------------
    # Enable library debug printing to stdout (also set BASISU_FORCE_DEVEL_MESSAGES to 1 in transcoder/basisu.h)
    # -----------------------------------------------------------------------        
    def enable_debug_printf(self, flag: bool = True):
        return self.backend.enable_debug_printf(flag)

    # -----------------------------------------------------------------------
    # KTX2 Handle API: open/close + all queries
    # -----------------------------------------------------------------------
    def open(self, ktx2_bytes: bytes) -> KTX2Handle:
        ptr = self._alloc(len(ktx2_bytes))
        self._write(ptr, ktx2_bytes)
        handle = self.ktx2_open(ptr, len(ktx2_bytes))
        return KTX2Handle(ptr, handle)

    def close(self, ktx2_handle: KTX2Handle):
        self.ktx2_close(ktx2_handle.handle)
        self._free(ktx2_handle.ptr)

    # ---- Basic queries ----
    def get_width(self, ktx2_handle: KTX2Handle):
        return self.ktx2_get_width(ktx2_handle.handle)

    def get_height(self, ktx2_handle: KTX2Handle):
        return self.ktx2_get_height(ktx2_handle.handle)

    def get_levels(self, ktx2_handle: KTX2Handle):
        return self.ktx2_get_levels(ktx2_handle.handle)

    def get_faces(self, ktx2_handle: KTX2Handle):
        return self.ktx2_get_faces(ktx2_handle.handle)

    def get_layers(self, ktx2_handle: KTX2Handle):
        return self.ktx2_get_layers(ktx2_handle.handle)

    def get_basis_tex_format(self, ktx2_handle: KTX2Handle):
        return self.ktx2_get_basis_tex_format(ktx2_handle.handle)
        
    def has_alpha(self, ktx2_handle: KTX2Handle) -> bool:
        """
        Return true if the KTX2 container has alpha.
        """
        return bool(self.ktx2_has_alpha(ktx2_handle.handle))

    # ---- Format flags ----
    def is_hdr(self, ktx2_handle):    return bool(self.ktx2_is_hdr(ktx2_handle.handle))
    def is_hdr_4x4(self, ktx2_handle):    return bool(self.ktx2_is_hdr_4x4(ktx2_handle.handle))
    def is_hdr_6x6(self, ktx2_handle):    return bool(self.ktx2_is_hdr_6x6(ktx2_handle.handle))
    def is_ldr(self, ktx2_handle):    return bool(self.ktx2_is_ldr(ktx2_handle.handle))
    def is_srgb(self, ktx2_handle):   return bool(self.ktx2_is_srgb(ktx2_handle.handle))
    def is_video(self, ktx2_handle):   return bool(self.ktx2_is_video(ktx2_handle.handle))
    def get_ldr_hdr_upconversion_nit_multiplier(self, ktx2_handle):   return self.ktx2_get_ldr_hdr_upconversion_nit_multiplier(ktx2_handle.handle)
    def is_etc1s(self, ktx2_handle):  return bool(self.ktx2_is_etc1s(ktx2_handle.handle))
    def is_uastc_ldr_4x4(self, ktx2_handle):  return bool(self.ktx2_is_uastc_ldr_4x4(ktx2_handle.handle))
    def is_xuastc_ldr(self, ktx2_handle): return bool(self.ktx2_is_xuastc_ldr(ktx2_handle.handle))
    def is_astc_ldr(self, ktx2_handle):   return bool(self.ktx2_is_astc_ldr(ktx2_handle.handle))
    
    # ---- DFD access
    def get_dfd_flags(self, ktx2_handle):   return self.ktx2_get_dfd_flags(ktx2_handle.handle)
    def get_dfd_total_samples(self, ktx2_handle):   return self.ktx2_get_dfd_total_samples(ktx2_handle.handle)
    def get_dfd_color_model(self, ktx2_handle):   return self.ktx2_get_dfd_color_model(ktx2_handle.handle)
    def get_dfd_color_primaries(self, ktx2_handle):   return self.ktx2_get_dfd_color_primaries(ktx2_handle.handle)
    def get_dfd_transfer_func(self, ktx2_handle):   return self.ktx2_get_dfd_transfer_func(ktx2_handle.handle)
    def get_dfd_channel_id0(self, ktx2_handle):   return self.ktx2_get_dfd_channel_id0(ktx2_handle.handle)
    def get_dfd_channel_id1(self, ktx2_handle):   return self.ktx2_get_dfd_channel_id1(ktx2_handle.handle)

    # ---- Block dimensions ----
    def get_block_width(self, ktx2_handle):  return self.ktx2_get_block_width(ktx2_handle.handle)
    def get_block_height(self, ktx2_handle): return self.ktx2_get_block_height(ktx2_handle.handle)
    
    # -----------------------------------------------------------------------
    # Explicit: start transcoding on an already-open KTX2 file
    # -----------------------------------------------------------------------
    def start_transcoding(self, ktx2_handle: KTX2Handle):
        """
        Must be called before per-level iframe flags become valid.
        """
        ok = self.ktx2_start_transcoding(ktx2_handle.handle)
        if not ok:
            raise RuntimeError("start_transcoding() failed")
        return True

    # ---- Level info ----
    def get_level_orig_width(self, ktx2_handle, level, layer=0, face=0):
        return self.ktx2_get_level_orig_width(ktx2_handle.handle, level, layer, face)

    def get_level_orig_height(self, ktx2_handle, level, layer=0, face=0):
        return self.ktx2_get_level_orig_height(ktx2_handle.handle, level, layer, face)
        
    def get_level_actual_width(self, ktx2_handle, level, layer=0, face=0):
        return self.ktx2_get_level_actual_width(ktx2_handle.handle, level, layer, face)

    def get_level_actual_height(self, ktx2_handle, level, layer=0, face=0):
        return self.ktx2_get_level_actual_height(ktx2_handle.handle, level, layer, face)

    def get_level_num_blocks_x(self, ktx2_handle, level, layer=0, face=0):
        return self.ktx2_get_level_num_blocks_x(ktx2_handle.handle, level, layer, face)

    def get_level_num_blocks_y(self, ktx2_handle, level, layer=0, face=0):
        return self.ktx2_get_level_num_blocks_y(ktx2_handle.handle, level, layer, face)

    def get_level_total_blocks(self, ktx2_handle, level, layer=0, face=0):
        return self.ktx2_get_level_total_blocks(ktx2_handle.handle, level, layer, face)

    def get_level_alpha_flag(self, ktx2_handle, level, layer=0, face=0):
        return bool(self.ktx2_get_level_alpha_flag(ktx2_handle.handle, level, layer, face))

    def get_level_iframe_flag(self, ktx2_handle, level, layer=0, face=0):
        return bool(self.ktx2_get_level_iframe_flag(ktx2_handle.handle, level, layer, face))

    # -----------------------------------------------------------------------
    # Low-level: Decode RGBA8 from an already-open KTX2 handle
    # -----------------------------------------------------------------------
    def decode_rgba_handle(self, ktx2_handle: KTX2Handle, level=0, layer=0, face=0):
        """
        Low-level fast decode. Requires an already-open KTX2Handle.
        Returns HxWx4 uint8 NumPy array.
        """
        w = self.ktx2_get_level_orig_width(ktx2_handle.handle, level, layer, face)
        h = self.ktx2_get_level_orig_height(ktx2_handle.handle, level, layer, face)

        out_size = w * h * 4
        out_ptr = self._alloc(out_size)

        # MUST start transcoding before ANY decode
        ok = self.ktx2_start_transcoding(ktx2_handle.handle)
        if not ok:
            self._free(out_ptr)
            raise RuntimeError("start_transcoding failed")

        ok = self.ktx2_transcode_image_level(
            ktx2_handle.handle,
            level, layer, face,
            out_ptr,
            out_size,
            TranscoderTextureFormat.TF_RGBA32,
            0, 0, 0, -1, -1, 0
        )
        if not ok:
            self._free(out_ptr)
            raise RuntimeError("transcode_image_level failed")

        raw_bytes = self._read(out_ptr, out_size)
        self._free(out_ptr)

        arr = np.frombuffer(raw_bytes, dtype=np.uint8)
        return arr.reshape((h, w, 4))

    # -----------------------------------------------------------------------
    # High-level: Decode RGBA8 directly from KTX2 file data
    # -----------------------------------------------------------------------       
    def decode_rgba(self, ktx2_bytes: bytes, level=0, layer=0, face=0):
        """
        High-level convenience decode. Opens the KTX2 file bytes for you.
        """
        ktx2_handle = self.open(ktx2_bytes)
        try:
            return self.decode_rgba_handle(ktx2_handle, level, layer, face)
        finally:
            self.close(ktx2_handle)     
            
    # -----------------------------------------------------------------------
    # Low-level: Decode HDR (RGBA float32) from open KTX2
    # -----------------------------------------------------------------------
    def decode_rgba_hdr_handle(self, ktx2_handle: KTX2Handle, level=0, layer=0, face=0):
        """
        Low-level HDR decode. Returns HxWx4 float32 NumPy array.
        """
        w = self.ktx2_get_level_orig_width(ktx2_handle.handle, level, layer, face)
        h = self.ktx2_get_level_orig_height(ktx2_handle.handle, level, layer, face)

        bytes_per_pixel = 8   # 4 * half-float
        out_size = w * h * bytes_per_pixel
        out_ptr = self._alloc(out_size)

        ok = self.ktx2_start_transcoding(ktx2_handle.handle)
        if not ok:
            self._free(out_ptr)
            raise RuntimeError("start_transcoding failed")

        ok = self.ktx2_transcode_image_level(
            ktx2_handle.handle,
            level, layer, face,
            out_ptr,
            out_size,
            TranscoderTextureFormat.TF_RGBA_HALF,
            0, 0, 0, -1, -1, 0
        )
        if not ok:
            self._free(out_ptr)
            raise RuntimeError("transcode_image_level failed")

        raw_bytes = self._read(out_ptr, out_size)
        self._free(out_ptr)

        arr = np.frombuffer(raw_bytes, dtype=np.float16).astype(np.float32)
        return arr.reshape((h, w, 4))

    # -----------------------------------------------------------------------
    # High-level: Decode HDR (RGBA float32) from KTX2 file data
    # -----------------------------------------------------------------------       
    def decode_rgba_hdr(self, ktx2_bytes: bytes, level=0, layer=0, face=0):
        """
        High-level convenience HDR decode. Opens the KTX2 file bytes for you.
        """
        ktx2_handle = self.open(ktx2_bytes)
        try:
            return self.decode_rgba_hdr_handle(ktx2_handle, level, layer, face)
        finally:
            self.close(ktx2_handle)     

    # -----------------------------------------------------------------------
    # Low-level: General-purpose transcode using a chosen TranscoderTextureFormat format
    # -----------------------------------------------------------------------
    def transcode_tfmt_handle(self, ktx2_handle: KTX2Handle, tfmt: int,
                      level=0, layer=0, face=0, decode_flags=0,
                      channel0=-1, channel1=-1):
        """
        Low-level direct transcoding from an already-open KTX2 handle.

        Parameters:
            ktx2_handle: KTX2Handle        -> already-open KTX2
            tfmt: int              -> TranscoderTextureFormat to transcode to (for ASTC: block size and LDR/HDR MUST match the KTX2 file, for HDR: must be a HDR texture format)
            level/layer/face: int  -> which image slice to decode
            decode_flags: int      -> basist::decode_flags
            row_pitch, rows_in_pixels, channel0, channel1 -> advanced options

        Returns: bytes (transcoded GPU texture data or uncompressed image)
        """

        # Determine actual output size in bytes
        ow = self.ktx2_get_level_orig_width(ktx2_handle.handle, level, layer, face)
        oh = self.ktx2_get_level_orig_height(ktx2_handle.handle, level, layer, face)

        out_size = self.basis_compute_transcoded_image_size_in_bytes(tfmt, ow, oh)
        if out_size == 0:
            raise RuntimeError("basis_compute_transcoded_image_size_in_bytes returned 0")
            
        # print(f"*** ow={ow}, oh={oh}, out_size={out_size}")

        out_ptr = self._alloc(out_size)

        # Call transcoder
        ok = self.ktx2_start_transcoding(ktx2_handle.handle)
        if not ok:
            self._free(out_ptr)
            raise RuntimeError("start_transcoding failed")

        ok = self.ktx2_transcode_image_level(
            ktx2_handle.handle,
            level, layer, face,
            out_ptr,
            out_size,
            tfmt,
            decode_flags,
            0,
            0,
            channel0, channel1,
            0  # no per-thread state object
        )
        if not ok:
            self._free(out_ptr)
            raise RuntimeError("ktx2_transcode_image_level failed")

        # Extract bytes
        raw_bytes = self._read(out_ptr, out_size)

        self._free(out_ptr)
        return raw_bytes

    # -----------------------------------------------------------------------
    # High-level: General-purpose transcode (opens the KTX2 for you)
    # tfmt: the TranscoderTextureFormat to transcode too
    # -----------------------------------------------------------------------
    def transcode_tfmt(self, ktx2_bytes: bytes, tfmt: int,
                  level=0, layer=0, face=0, decode_flags=0,
                  channel0=-1, channel1=-1):
        """
        High-level convenience wrapper for transcode_tfmt_handle().
        Automatically opens/closes the KTX2 file.
        """
        ktx2_handle = self.open(ktx2_bytes)
        try:
            return self.transcode_tfmt_handle(
                ktx2_handle, tfmt,
                level=level,
                layer=layer,
                face=face,
                decode_flags=decode_flags,
                channel0=channel0,
                channel1=channel1
            )
        finally:
            self.close(ktx2_handle)

    # -----------------------------------------------------------------------
    # Low-level: choose a specific transcoder_texture_format from a family string
    # -----------------------------------------------------------------------
    def choose_transcoder_format(self, ktx2_handle: KTX2Handle, family: str) -> int:
        """
        Given an already-opened KTX2 and a desired family string, choose a concrete
        TranscoderTextureFormat enum.

        family: one of:
          "ASTC", "BC1", "BC3", "BC4", "BC5", "BC6H", "BC7",
          "PVRTC1", "PVRTC2",
          "ETC1", "ETC2", "ETC2_EAC_R11", "ETC2_EAC_RG11",
          "ATC", "FXT1",
          "RGBA32", "RGB_HALF", "RGBA_HALF", "RGB_FLOAT", "RGBA_FLOAT",
          "RGB_9E5"

        Returns:
          int: TranscoderTextureFormat value
        """

        s = family.strip().upper().replace(" ", "")
        hdr_tex = self.is_hdr(ktx2_handle)
        has_alpha = self.has_alpha(ktx2_handle)
        basis_fmt = self.get_basis_tex_format(ktx2_handle)

        tfmt = None

        # -------------------------------------------------------------------
        # Uncompressed families
        # -------------------------------------------------------------------
        if s in ("RGBA32", "RGBA8", "UNCOMPRESSED"):
            tfmt = TranscoderTextureFormat.TF_RGBA32

        elif s in ("RGBHALF", "RGB16F", "RGB_FLOAT", "RGBFLOAT"):
            tfmt = TranscoderTextureFormat.TF_RGB_HALF

        elif s in ("RGBAHALF", "RGBA16F", "RGBA_FLOAT", "RGBAFLOAT"):
            tfmt = TranscoderTextureFormat.TF_RGBA_HALF

        elif s in ("RGB9E5", "RGB_9E5"):
            tfmt = TranscoderTextureFormat.TF_RGB_9E5

        # -------------------------------------------------------------------
        # BC families
        # -------------------------------------------------------------------
        elif s == "BC1":
            tfmt = TranscoderTextureFormat.TF_BC1_RGB
        elif s == "BC3":
            tfmt = TranscoderTextureFormat.TF_BC3_RGBA
        elif s == "BC4":
            tfmt = TranscoderTextureFormat.TF_BC4_R
        elif s == "BC5":
            tfmt = TranscoderTextureFormat.TF_BC5_RG
        elif s == "BC6H":
            tfmt = TranscoderTextureFormat.TF_BC6H
        elif s == "BC7":
            tfmt = TranscoderTextureFormat.TF_BC7_RGBA

        # -------------------------------------------------------------------
        # PVRTC families
        # -------------------------------------------------------------------
        elif s == "PVRTC1":
            tfmt = (TranscoderTextureFormat.TF_PVRTC1_4_RGBA
                    if has_alpha else TranscoderTextureFormat.TF_PVRTC1_4_RGB)

        elif s == "PVRTC2":
            tfmt = (TranscoderTextureFormat.TF_PVRTC2_4_RGBA
                    if has_alpha else TranscoderTextureFormat.TF_PVRTC2_4_RGB)

        # -------------------------------------------------------------------
        # ETC / EAC families
        # -------------------------------------------------------------------
        elif s == "ETC1":
            tfmt = TranscoderTextureFormat.TF_ETC1_RGB

        elif s == "ETC2":
            tfmt = TranscoderTextureFormat.TF_ETC2_RGBA

        elif s in ("ETC2_EAC_R11", "EAC_R11"):
            tfmt = TranscoderTextureFormat.TF_ETC2_EAC_R11

        elif s in ("ETC2_EAC_RG11", "EAC_RG11"):
            tfmt = TranscoderTextureFormat.TF_ETC2_EAC_RG11

        # -------------------------------------------------------------------
        # ATC / FXT
        # -------------------------------------------------------------------
        elif s == "ATC":
            tfmt = (TranscoderTextureFormat.TF_ATC_RGBA
                    if has_alpha else TranscoderTextureFormat.TF_ATC_RGB)

        elif s == "FXT1":
            tfmt = TranscoderTextureFormat.TF_FXT1_RGB

        # -------------------------------------------------------------------
        # ASTC family
        # -------------------------------------------------------------------
        elif s == "ASTC":
            # Let BasisU decide correct ASTC format (block size + LDR/HDR)
            tfmt = self.basis_get_transcoder_texture_format_from_basis_tex_format(basis_fmt)

        else:
            # Unknown family: choose a safe uncompressed default
            if hdr_tex:
                tfmt = TranscoderTextureFormat.TF_RGBA_HALF
            else:
                tfmt = TranscoderTextureFormat.TF_RGBA32

        # -------------------------------------------------------------------
        # Validate HDR/LDR compatibility (optional but recommended)
        # -------------------------------------------------------------------
        # Use helpers to ensure we don't do HDR->LDR or LDR->HDR accidentally.
        is_tfmt_hdr = self.basis_transcoder_format_is_hdr(tfmt)
        if hdr_tex and not is_tfmt_hdr:
            raise ValueError(f"Requested {family} (LDR transcoder format) for HDR KTX2.")
        if not hdr_tex and is_tfmt_hdr:
            raise ValueError(f"Requested {family} (HDR transcoder format) for LDR KTX2.")

        return tfmt

    # -----------------------------------------------------------------------
    # Low-level: General-purpose transcode using a family string
    # from an already opened ktx2 file.
    # Returns:
    #   (data_bytes, chosen_tfmt, block_width, block_height)
    # -----------------------------------------------------------------------
    def transcode_handle(
        self,
        ktx2_handle: KTX2Handle,
        family: str,
        level=0,
        layer=0,
        face=0,
        decode_flags=0,
        channel0=-1,
        channel1=-1
    ):
        """
        Low-level direct transcoding from an already-open KTX2 handle,
        using a high-level family string such as:
            "BC7", "BC3", "BC1", "ETC1", "ETC2", "ASTC", "PVRTC1",
            "RGBA32", "RGB_HALF", "RGBA_HALF", "RGB_9E5", etc.
        See choose_transcoder_format().
        Returns:
            (data_bytes, tfmt, block_width, block_height)
        """

        # Decide the exact transcoder format (BC1/BC7/etc.)
        tfmt = self.choose_transcoder_format(ktx2_handle, family)

        # Get original dims of the requested slice
        ow = self.get_level_orig_width(ktx2_handle, level, layer, face)
        oh = self.get_level_orig_height(ktx2_handle, level, layer, face)

        # Compute correct output size for the chosen format
        out_size = self.basis_compute_transcoded_image_size_in_bytes(tfmt, ow, oh)
        if out_size == 0:
            raise RuntimeError(
                f"Computed output size is 0 for tfmt={tfmt}, dims={ow}x{oh}"
            )

        # Allocate output buffer
        out_ptr = self._alloc(out_size)

        # Ensure transcoding tables are ready
        ok = self.ktx2_start_transcoding(ktx2_handle.handle)
        if not ok:
            self._free(out_ptr)
            raise RuntimeError("start_transcoding failed")

        # Perform the transcode
        ok = self.ktx2_transcode_image_level(
            ktx2_handle.handle,
            level, layer, face,
            out_ptr,
            out_size,
            tfmt,
            decode_flags,
            0,  # row_pitch_in_blocks_or_pixels
            0,  # rows_in_pixels
            channel0,
            channel1,
            0   # no thread-local state
        )
        if not ok:
            self._free(out_ptr)
            raise RuntimeError("ktx2_transcode_image_level failed")

        # Extract bytes from native/WASM memory
        data_bytes = self._read(out_ptr, out_size)

        # Free the output buffer
        self._free(out_ptr)

        # Determine block dims for this texture format
        if self.basis_transcoder_format_is_uncompressed(tfmt):
            bw = None
            bh = None
        else:
            bw = self.basis_get_block_width(tfmt)
            bh = self.basis_get_block_height(tfmt)

        return data_bytes, tfmt, bw, bh

    # -----------------------------------------------------------------------
    # High-level: one-shot transcode using a family string
    # directly from ktx2 file data. (Slower if you're transcoding multiple 
    # levels/faces/layers.)
    # -----------------------------------------------------------------------
    def transcode(
        self,
        ktx2_bytes: bytes,
        family: str,
        level=0,
        layer=0,
        face=0,
        decode_flags=0,
        channel0=-1,
        channel1=-1
    ):
        """
        High-level version of transcode_handle().
        Calls transcode_handle() internally.

        Returns:
            (data_bytes, tfmt, block_width, block_height)
        """
        ktx2_handle = self.open(ktx2_bytes)
        try:
            return self.transcode_handle(
                ktx2_handle,
                family,
                level=level,
                layer=layer,
                face=face,
                decode_flags=decode_flags,
                channel0=channel0,
                channel1=channel1
            )
        finally:
            self.close(ktx2_handle)

    def tfmt_name(self, tfmt: int):
        return TranscoderTextureFormat(tfmt).name

# basisu_py/wasm/wasm_transcoder.py

import wasmtime
import ctypes


class BasisuWasmTranscoder:
    """
    Lowest-level WASM transcoder wrapper.
    Direct mapping to basisu_wasm_transcoder_api.h/.cpp

    NOTE:
      - This layer does NOT interpret formats or block sizes.
      - It only wraps the raw C API (bt_* and basis_* exports).
      - Higher-level logic (TranscoderCore, Transcoder) will build on top.
    """

    def __init__(self, wasm_path: str):
        self.wasm_path = wasm_path
        self.engine = None
        self.store = None
        self.memory = None
        self.exports = None

    # ------------------------------------------------------
    # Internal: initialize WASM + WASI
    # ------------------------------------------------------
    def _init_engine(self):
        self.engine = wasmtime.Engine()
        self.store = wasmtime.Store(self.engine)

        wasi = wasmtime.WasiConfig()
        wasi.argv = ["basisu-transcoder"]
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

        # Mandatory transcoder init
        if "bt_init" in self.exports:
            self.exports["bt_init"](self.store)

        print("[WASM Transcoder] Loaded:", self.wasm_path)

    # ------------------------------------------------------
    # Linear memory access helpers
    # ------------------------------------------------------
    def _buf(self):
        raw_ptr = self.memory.data_ptr(self.store)
        size = self.memory.data_len(self.store)
        addr = ctypes.addressof(raw_ptr.contents)
        return (ctypes.c_ubyte * size).from_address(addr)

    def write_bytes(self, ptr: int, data: bytes):
        buf = self._buf()
        buf[ptr:ptr + len(data)] = data

    def read_bytes(self, ptr: int, num: int) -> bytes:
        buf = self._buf()
        return bytes(buf[ptr:ptr + num])
        
    # NEW unified names:
    def write_memory(self, ptr, data):
        self.write_bytes(ptr, data)

    def read_memory(self, ptr, size):
        return self.read_bytes(ptr, size)

    # ------------------------------------------------------
    # Memory alloc/free
    # ------------------------------------------------------
    def alloc(self, size: int) -> int:
        return self.exports["bt_alloc"](self.store, size)

    def free(self, ptr: int):
        return self.exports["bt_free"](self.store, ptr)

    # ------------------------------------------------------
    # High-level functions: version, init, debug
    # ------------------------------------------------------
    def get_version(self) -> int:
        return self.exports["bt_get_version"](self.store)

    def enable_debug_printf(self, flag: bool = True):
        return self.exports["bt_enable_debug_printf"](self.store, 1 if flag else 0)

    # ------------------------------------------------------
    # basis_tex_format helpers
    # ------------------------------------------------------
    def basis_tex_format_is_xuastc_ldr(self, basis_tex_fmt_u32: int) -> bool:
        return bool(self.exports["bt_basis_tex_format_is_xuastc_ldr"](self.store, basis_tex_fmt_u32))

    def basis_tex_format_is_astc_ldr(self, basis_tex_fmt_u32: int) -> bool:
        return bool(self.exports["bt_basis_tex_format_is_astc_ldr"](self.store, basis_tex_fmt_u32))

    def basis_tex_format_get_block_width(self, basis_tex_fmt_u32: int) -> int:
        return self.exports["bt_basis_tex_format_get_block_width"](self.store, basis_tex_fmt_u32)

    def basis_tex_format_get_block_height(self, basis_tex_fmt_u32: int) -> int:
        return self.exports["bt_basis_tex_format_get_block_height"](self.store, basis_tex_fmt_u32)

    def basis_tex_format_is_hdr(self, basis_tex_fmt_u32: int) -> bool:
        return bool(self.exports["bt_basis_tex_format_is_hdr"](self.store, basis_tex_fmt_u32))

    def basis_tex_format_is_ldr(self, basis_tex_fmt_u32: int) -> bool:
        return bool(self.exports["bt_basis_tex_format_is_ldr"](self.store, basis_tex_fmt_u32))

    # ------------------------------------------------------
    # transcoder_texture_format helpers
    # ------------------------------------------------------
    def basis_get_bytes_per_block_or_pixel(self, tfmt_u32: int) -> int:
        return self.exports["bt_basis_get_bytes_per_block_or_pixel"](self.store, tfmt_u32)

    def basis_transcoder_format_has_alpha(self, tfmt_u32: int) -> bool:
        return bool(self.exports["bt_basis_transcoder_format_has_alpha"](self.store, tfmt_u32))

    def basis_transcoder_format_is_hdr(self, tfmt_u32: int) -> bool:
        return bool(self.exports["bt_basis_transcoder_format_is_hdr"](self.store, tfmt_u32))

    def basis_transcoder_format_is_ldr(self, tfmt_u32: int) -> bool:
        return bool(self.exports["bt_basis_transcoder_format_is_ldr"](self.store, tfmt_u32))

    def basis_transcoder_texture_format_is_astc(self, tfmt_u32: int) -> bool:
        return bool(self.exports["bt_basis_transcoder_texture_format_is_astc"](self.store, tfmt_u32))

    def basis_transcoder_format_is_uncompressed(self, tfmt_u32: int) -> bool:
        return bool(self.exports["bt_basis_transcoder_format_is_uncompressed"](self.store, tfmt_u32))

    def basis_get_uncompressed_bytes_per_pixel(self, tfmt_u32: int) -> int:
        return self.exports["bt_basis_get_uncompressed_bytes_per_pixel"](self.store, tfmt_u32)

    def basis_get_block_width(self, tfmt_u32: int) -> int:
        return self.exports["bt_basis_get_block_width"](self.store, tfmt_u32)

    def basis_get_block_height(self, tfmt_u32: int) -> int:
        return self.exports["bt_basis_get_block_height"](self.store, tfmt_u32)

    def basis_get_transcoder_texture_format_from_basis_tex_format(self, basis_tex_fmt_u32: int) -> int:
        return self.exports["bt_basis_get_transcoder_texture_format_from_basis_tex_format"](self.store, basis_tex_fmt_u32)

    def basis_is_format_supported(self, tfmt_u32: int, basis_tex_fmt_u32: int) -> bool:
        return bool(self.exports["bt_basis_is_format_supported"](self.store, tfmt_u32, basis_tex_fmt_u32))

    def basis_compute_transcoded_image_size_in_bytes(self, tfmt_u32: int, orig_width: int, orig_height: int) -> int:
        return self.exports["bt_basis_compute_transcoded_image_size_in_bytes"](
            self.store, tfmt_u32, orig_width, orig_height
        )

    # ------------------------------------------------------
    # KTX2 handle management
    # ------------------------------------------------------
    def ktx2_open(self, data_ptr: int, data_len: int) -> int:
        return self.exports["bt_ktx2_open"](self.store, data_ptr, data_len)

    def ktx2_close(self, handle: int):
        return self.exports["bt_ktx2_close"](self.store, handle)

    # ------------------------------------------------------
    # Basic KTX2 metadata
    # ------------------------------------------------------
    def ktx2_get_width(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_width"](self.store, handle)

    def ktx2_get_height(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_height"](self.store, handle)

    def ktx2_get_levels(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_levels"](self.store, handle)

    def ktx2_get_faces(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_faces"](self.store, handle)

    def ktx2_get_layers(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_layers"](self.store, handle)

    def ktx2_get_basis_tex_format(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_basis_tex_format"](self.store, handle)

    # KTX2 format checks
    def ktx2_is_etc1s(self, handle: int) -> bool:
        return bool(self.exports["bt_ktx2_is_etc1s"](self.store, handle))

    def ktx2_is_uastc_ldr_4x4(self, handle: int) -> bool:
        return bool(self.exports["bt_ktx2_is_uastc_ldr_4x4"](self.store, handle))

    def ktx2_is_hdr(self, handle: int) -> bool:
        return bool(self.exports["bt_ktx2_is_hdr"](self.store, handle))

    def ktx2_is_hdr_4x4(self, handle: int) -> bool:
        return bool(self.exports["bt_ktx2_is_hdr_4x4"](self.store, handle))

    def ktx2_is_hdr_6x6(self, handle: int) -> bool:
        return bool(self.exports["bt_ktx2_is_hdr_6x6"](self.store, handle))

    def ktx2_is_ldr(self, handle: int) -> bool:
        return bool(self.exports["bt_ktx2_is_ldr"](self.store, handle))

    def ktx2_is_astc_ldr(self, handle: int) -> bool:
        return bool(self.exports["bt_ktx2_is_astc_ldr"](self.store, handle))

    def ktx2_is_xuastc_ldr(self, handle: int) -> bool:
        return bool(self.exports["bt_ktx2_is_xuastc_ldr"](self.store, handle))

    def ktx2_get_block_width(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_block_width"](self.store, handle)

    def ktx2_get_block_height(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_block_height"](self.store, handle)

    def ktx2_has_alpha(self, handle: int) -> bool:
        return bool(self.exports["bt_ktx2_has_alpha"](self.store, handle))

    def ktx2_get_dfd_color_model(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_dfd_color_model"](self.store, handle)

    def ktx2_get_dfd_color_primaries(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_dfd_color_primaries"](self.store, handle)

    def ktx2_get_dfd_transfer_func(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_dfd_transfer_func"](self.store, handle)

    def ktx2_is_srgb(self, handle: int) -> bool:
        return bool(self.exports["bt_ktx2_is_srgb"](self.store, handle))

    def ktx2_get_dfd_flags(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_dfd_flags"](self.store, handle)

    def ktx2_get_dfd_total_samples(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_dfd_total_samples"](self.store, handle)

    def ktx2_get_dfd_channel_id0(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_dfd_channel_id0"](self.store, handle)

    def ktx2_get_dfd_channel_id1(self, handle: int) -> int:
        return self.exports["bt_ktx2_get_dfd_channel_id1"](self.store, handle)

    def ktx2_is_video(self, handle: int) -> bool:
        return bool(self.exports["bt_ktx2_is_video"](self.store, handle))

    def ktx2_get_ldr_hdr_upconversion_nit_multiplier(self, handle: int) -> float:
        return self.exports["bt_ktx2_get_ldr_hdr_upconversion_nit_multiplier"](self.store, handle)

    # ------------------------------------------------------
    # Per-level metadata
    # ------------------------------------------------------
    def ktx2_get_level_orig_width(self, h, lvl, layer, face) -> int:
        return self.exports["bt_ktx2_get_level_orig_width"](self.store, h, lvl, layer, face)

    def ktx2_get_level_orig_height(self, h, lvl, layer, face) -> int:
        return self.exports["bt_ktx2_get_level_orig_height"](self.store, h, lvl, layer, face)

    def ktx2_get_level_actual_width(self, h, lvl, layer, face) -> int:
        return self.exports["bt_ktx2_get_level_actual_width"](self.store, h, lvl, layer, face)

    def ktx2_get_level_actual_height(self, h, lvl, layer, face) -> int:
        return self.exports["bt_ktx2_get_level_actual_height"](self.store, h, lvl, layer, face)

    def ktx2_get_level_num_blocks_x(self, h, lvl, layer, face) -> int:
        return self.exports["bt_ktx2_get_level_num_blocks_x"](self.store, h, lvl, layer, face)

    def ktx2_get_level_num_blocks_y(self, h, lvl, layer, face) -> int:
        return self.exports["bt_ktx2_get_level_num_blocks_y"](self.store, h, lvl, layer, face)

    def ktx2_get_level_total_blocks(self, h, lvl, layer, face) -> int:
        return self.exports["bt_ktx2_get_level_total_blocks"](self.store, h, lvl, layer, face)

    def ktx2_get_level_alpha_flag(self, h, lvl, layer, face) -> bool:
        return bool(self.exports["bt_ktx2_get_level_alpha_flag"](self.store, h, lvl, layer, face))

    def ktx2_get_level_iframe_flag(self, h, lvl, layer, face) -> bool:
        return bool(self.exports["bt_ktx2_get_level_iframe_flag"](self.store, h, lvl, layer, face))

    # ------------------------------------------------------
    # Transcoding control
    # ------------------------------------------------------
    def ktx2_start_transcoding(self, handle: int) -> bool:
        return bool(self.exports["bt_ktx2_start_transcoding"](self.store, handle))

    def ktx2_create_transcode_state(self) -> int:
        return self.exports["bt_ktx2_create_transcode_state"](self.store)

    def ktx2_destroy_transcode_state(self, handle: int):
        return self.exports["bt_ktx2_destroy_transcode_state"](self.store, handle)

    # ------------------------------------------------------
    # Actual transcoding call
    # ------------------------------------------------------
    def ktx2_transcode_image_level(
        self,
        ktx2_handle: int,
        level_index: int,
        layer_index: int,
        face_index: int,
        output_block_mem_ofs: int,
        output_blocks_buf_size_in_blocks_or_pixels: int,
        transcoder_texture_format_u32: int,
        decode_flags: int,
        output_row_pitch_in_blocks_or_pixels: int,
        output_rows_in_pixels: int,
        channel0: int,
        channel1: int,
        state_handle: int,
    ) -> bool:
        return bool(self.exports["bt_ktx2_transcode_image_level"](
            self.store,
            ktx2_handle,
            level_index, layer_index, face_index,
            output_block_mem_ofs,
            output_blocks_buf_size_in_blocks_or_pixels,
            transcoder_texture_format_u32,
            decode_flags,
            output_row_pitch_in_blocks_or_pixels,
            output_rows_in_pixels,
            channel0, channel1,
            state_handle
        ))

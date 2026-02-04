// File: basisu_transcoder_pybind11.cpp
// pybind11 native bindings for the transcoder's pure C API basisu_wasm_transcoder_api.h

#include <pybind11/pybind11.h>
#include <stdint.h>

#include "../encoder/basisu_wasm_transcoder_api.h"

namespace py = pybind11;

// wasm_bool_t is uint32_t — convert to Python bool
static inline bool to_bool(wasm_bool_t v) { return v != 0; }

PYBIND11_MODULE(basisu_transcoder_python, m) {
    m.doc() = "Native Basis Universal transcoder (pybind11 binding over basisu_wasm_transcoder_api)";

    // ------------------------------------------------------------------------
    // High-level functions
    // ------------------------------------------------------------------------
    m.def("get_version", &bt_get_version,
          "Get BasisU transcoder version");

    m.def("enable_debug_printf",
          [](bool flag) { bt_enable_debug_printf(flag ? 1u : 0u); },
          "Enable or disable debug printf output");

    m.def("init", &bt_init,
          "Initialize transcoder library");

    m.def("alloc", &bt_alloc,
          "Allocate a buffer, returns uint64 offset/pointer");
    m.def("free", &bt_free,
          "Free a buffer allocated by bt_alloc");


    // ------------------------------------------------------------------------
    // basis_tex_format helpers
    // ------------------------------------------------------------------------
    m.def("basis_tex_format_is_xuastc_ldr",
          [](uint32_t fmt) { return to_bool(bt_basis_tex_format_is_xuastc_ldr(fmt)); });

    m.def("basis_tex_format_is_astc_ldr",
          [](uint32_t fmt) { return to_bool(bt_basis_tex_format_is_astc_ldr(fmt)); });

    m.def("basis_tex_format_get_block_width",
          &bt_basis_tex_format_get_block_width);

    m.def("basis_tex_format_get_block_height",
          &bt_basis_tex_format_get_block_height);

    m.def("basis_tex_format_is_hdr",
          [](uint32_t fmt) { return to_bool(bt_basis_tex_format_is_hdr(fmt)); });

    m.def("basis_tex_format_is_ldr",
          [](uint32_t fmt) { return to_bool(bt_basis_tex_format_is_ldr(fmt)); });


    // ------------------------------------------------------------------------
    // transcoder_texture_format helpers
    // ------------------------------------------------------------------------
    m.def("basis_get_bytes_per_block_or_pixel",
          &bt_basis_get_bytes_per_block_or_pixel);

    m.def("basis_transcoder_format_has_alpha",
          [](uint32_t tfmt) { return to_bool(bt_basis_transcoder_format_has_alpha(tfmt)); });

    m.def("basis_transcoder_format_is_hdr",
          [](uint32_t tfmt) { return to_bool(bt_basis_transcoder_format_is_hdr(tfmt)); });

    m.def("basis_transcoder_format_is_ldr",
          [](uint32_t tfmt) { return to_bool(bt_basis_transcoder_format_is_ldr(tfmt)); });

    m.def("basis_transcoder_texture_format_is_astc",
          [](uint32_t tfmt) { return to_bool(bt_basis_transcoder_texture_format_is_astc(tfmt)); });

    m.def("basis_transcoder_format_is_uncompressed",
          [](uint32_t tfmt) { return to_bool(bt_basis_transcoder_format_is_uncompressed(tfmt)); });

    m.def("basis_get_uncompressed_bytes_per_pixel",
          &bt_basis_get_uncompressed_bytes_per_pixel);

    m.def("basis_get_block_width",
          &bt_basis_get_block_width);

    m.def("basis_get_block_height",
          &bt_basis_get_block_height);

    m.def("basis_get_transcoder_texture_format_from_basis_tex_format",
          &bt_basis_get_transcoder_texture_format_from_basis_tex_format);

    m.def("basis_is_format_supported",
          [](uint32_t tfmt, uint32_t basis_fmt) {
              return to_bool(bt_basis_is_format_supported(tfmt, basis_fmt));
          });

    m.def("basis_compute_transcoded_image_size_in_bytes",
          &bt_basis_compute_transcoded_image_size_in_bytes);


    // ------------------------------------------------------------------------
    // KTX2 open/close & basic info
    // ------------------------------------------------------------------------
    m.def("ktx2_open", &bt_ktx2_open,
          "Open a KTX2 image from memory; returns handle");

    m.def("ktx2_close", &bt_ktx2_close,
          "Close a previously opened KTX2 handle");

    m.def("ktx2_get_width",  &bt_ktx2_get_width);
    m.def("ktx2_get_height", &bt_ktx2_get_height);
    m.def("ktx2_get_levels", &bt_ktx2_get_levels);
    m.def("ktx2_get_faces",  &bt_ktx2_get_faces);
    m.def("ktx2_get_layers", &bt_ktx2_get_layers);

    m.def("ktx2_get_basis_tex_format", &bt_ktx2_get_basis_tex_format);

    m.def("ktx2_is_etc1s",
          [](uint64_t h) { return to_bool(bt_ktx2_is_etc1s(h)); });

    m.def("ktx2_is_uastc_ldr_4x4",
          [](uint64_t h) { return to_bool(bt_ktx2_is_uastc_ldr_4x4(h)); });

    m.def("ktx2_is_hdr",
          [](uint64_t h) { return to_bool(bt_ktx2_is_hdr(h)); });

    m.def("ktx2_is_hdr_4x4",
          [](uint64_t h) { return to_bool(bt_ktx2_is_hdr_4x4(h)); });

    m.def("ktx2_is_hdr_6x6",
          [](uint64_t h) { return to_bool(bt_ktx2_is_hdr_6x6(h)); });

    m.def("ktx2_is_ldr",
          [](uint64_t h) { return to_bool(bt_ktx2_is_ldr(h)); });

    m.def("ktx2_is_astc_ldr",
          [](uint64_t h) { return to_bool(bt_ktx2_is_astc_ldr(h)); });

    m.def("ktx2_is_xuastc_ldr",
          [](uint64_t h) { return to_bool(bt_ktx2_is_xuastc_ldr(h)); });

    m.def("ktx2_get_block_width", &bt_ktx2_get_block_width);
	
    m.def("ktx2_get_block_height", &bt_ktx2_get_block_height);

    m.def("ktx2_has_alpha",
          [](uint64_t h) { return to_bool(bt_ktx2_has_alpha(h)); });

    m.def("ktx2_get_dfd_color_model",      &bt_ktx2_get_dfd_color_model);
    m.def("ktx2_get_dfd_color_primaries",  &bt_ktx2_get_dfd_color_primaries);
    m.def("ktx2_get_dfd_transfer_func",    &bt_ktx2_get_dfd_transfer_func);

    m.def("ktx2_is_srgb",
          [](uint64_t h) { return to_bool(bt_ktx2_is_srgb(h)); });

    m.def("ktx2_get_dfd_flags",            &bt_ktx2_get_dfd_flags);
    m.def("ktx2_get_dfd_total_samples",    &bt_ktx2_get_dfd_total_samples);
    m.def("ktx2_get_dfd_channel_id0",      &bt_ktx2_get_dfd_channel_id0);
    m.def("ktx2_get_dfd_channel_id1",      &bt_ktx2_get_dfd_channel_id1);

    m.def("ktx2_is_video",
          [](uint64_t h) { return to_bool(bt_ktx2_is_video(h)); });

    m.def("ktx2_get_ldr_hdr_upconversion_nit_multiplier",
          &bt_ktx2_get_ldr_hdr_upconversion_nit_multiplier);


    // ------------------------------------------------------------------------
    // KTX2 per-level info
    // ------------------------------------------------------------------------
    m.def("ktx2_get_level_orig_width",
          &bt_ktx2_get_level_orig_width);

    m.def("ktx2_get_level_orig_height",
          &bt_ktx2_get_level_orig_height);

    m.def("ktx2_get_level_actual_width",
          &bt_ktx2_get_level_actual_width);

    m.def("ktx2_get_level_actual_height",
          &bt_ktx2_get_level_actual_height);

    m.def("ktx2_get_level_num_blocks_x",
          &bt_ktx2_get_level_num_blocks_x);

    m.def("ktx2_get_level_num_blocks_y",
          &bt_ktx2_get_level_num_blocks_y);

    m.def("ktx2_get_level_total_blocks",
          &bt_ktx2_get_level_total_blocks);

    m.def("ktx2_get_level_alpha_flag",
          [](uint64_t h, uint32_t level, uint32_t layer, uint32_t face) {
              return to_bool(bt_ktx2_get_level_alpha_flag(h, level, layer, face));
          });

    m.def("ktx2_get_level_iframe_flag",
          [](uint64_t h, uint32_t level, uint32_t layer, uint32_t face) {
              return to_bool(bt_ktx2_get_level_iframe_flag(h, level, layer, face));
          });


    // ------------------------------------------------------------------------
    // Transcoding state and operations
    // ------------------------------------------------------------------------
    m.def("ktx2_start_transcoding",
          [](uint64_t h) { return to_bool(bt_ktx2_start_transcoding(h)); });

    m.def("ktx2_create_transcode_state",
          &bt_ktx2_create_transcode_state);

    m.def("ktx2_destroy_transcode_state",
          &bt_ktx2_destroy_transcode_state);

    m.def("ktx2_transcode_image_level",
          [](uint64_t ktx2_handle,
             uint32_t level_index, uint32_t layer_index, uint32_t face_index,
             uint64_t out_mem_ofs,
             uint32_t out_blocks_or_pixels,
             uint32_t transcoder_texture_format_u32,
             uint32_t decode_flags,
             uint32_t row_pitch_blocks_or_pixels,
             uint32_t rows_in_pixels,
             int channel0, int channel1,
             uint64_t state_handle)
          {
              return to_bool(bt_ktx2_transcode_image_level(
                  ktx2_handle,
                  level_index, layer_index, face_index,
                  out_mem_ofs,
                  out_blocks_or_pixels,
                  transcoder_texture_format_u32,
                  decode_flags,
                  row_pitch_blocks_or_pixels,
                  rows_in_pixels,
                  channel0, channel1,
                  state_handle));
          },
          py::arg("ktx2_handle"),
          py::arg("level_index"),
          py::arg("layer_index"),
          py::arg("face_index"),
          py::arg("output_block_mem_ofs"),
          py::arg("output_blocks_buf_size_in_blocks_or_pixels"),
          py::arg("transcoder_texture_format_u32"),
          py::arg("decode_flags"),
          py::arg("output_row_pitch_in_blocks_or_pixels") = 0,
          py::arg("output_rows_in_pixels") = 0,
          py::arg("channel0") = -1,
          py::arg("channel1") = -1,
          py::arg("state_handle") = 0);

	m.def("read_memory",
	    [](uint64_t ptr, uint32_t size) {
	        return py::bytes((const char*)ptr, size);
	    },
	    "Read `size` bytes starting at native memory address `ptr`");
	
	m.def("write_memory",
	    [](uint64_t dest_ptr, py::buffer src) {
	        py::buffer_info info = src.request();
	        memcpy((void*)dest_ptr, info.ptr, info.size * info.itemsize);
	    },
	    "Write bytes/buffer-like object into native memory at address `ptr`");
}

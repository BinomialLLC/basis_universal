// File: basisu_encoder_pybind11.cpp
// pybind11 native bindings for the compressor's pure C API basisu_wasm_api.h
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <stdint.h>

// include the basisu compression plain C API
#include "../encoder/basisu_wasm_api.h"   

namespace py = pybind11;

// Convert wasm_bool_t (uint32_t) ? Python bool
static inline bool to_bool(uint32_t v) { return v != 0; }

PYBIND11_MODULE(basisu_python, m) {
    m.doc() = "Native Basis Universal encoder (pybind11 binding over basisu_wasm_api)";

    //
    // Initialization / Version
    //
    m.def("init", &bu_init, "Initialize the BasisU codec library");
    m.def("get_version", &bu_get_version, "Return BASISU_LIB_VERSION");

    //
    // Memory allocation helpers
    //
    m.def("alloc", &bu_alloc,
          "Allocate memory inside native heap and return pointer as uint64");
    m.def("free", &bu_free,
          "Free previously allocated pointer");

    //
    // Compression params handles
    //
    m.def("new_params", &bu_new_comp_params,
          "Create a new comp_params struct inside native heap");
    m.def("delete_params",
          [](uint64_t h) { return to_bool(bu_delete_comp_params(h)); },
          "Destroy a comp_params struct");

    m.def("params_clear",
          [](uint64_t h) { return to_bool(bu_comp_params_clear(h)); },
          "Clear comp_params struct");

    //
    // Image upload API
    //
    m.def("set_image_rgba32",
          [](uint64_t params, uint32_t index,
             uint64_t img_ptr, uint32_t w, uint32_t h, uint32_t pitch) {
              return to_bool(bu_comp_params_set_image_rgba32(
                  params, index, img_ptr, w, h, pitch));
          },
          "Set 8-bit RGBA32 image into parameters");

    m.def("set_image_float_rgba",
          [](uint64_t params, uint32_t index,
             uint64_t img_ptr, uint32_t w, uint32_t h, uint32_t pitch) {
              return to_bool(bu_comp_params_set_image_float_rgba(
                  params, index, img_ptr, w, h, pitch));
          },
          "Set float32 RGBA image into parameters");

    //
    // Compression
    //
	m.def("compress",
      [](uint64_t params,
         int tex_format,
         int quality,
         int effort,
         uint64_t flags,
         float rdo_quality)
      {
          return to_bool(bu_compress_texture(
              params, tex_format, quality, effort, flags, rdo_quality));
      },
      py::arg("params"),
      py::arg("tex_format"),
      py::arg("quality"),
      py::arg("effort"),
      py::arg("flags"),
      py::arg("rdo_quality") = 0.0f
	);		  

    //
    // Output blob access
    //
    m.def("get_comp_data_size",
          &bu_comp_params_get_comp_data_size,
          "Return size (bytes) of compressed output");
    m.def("get_comp_data_ofs",
          &bu_comp_params_get_comp_data_ofs,
          "Return pointer (uint64) to compressed output buffer");
		  
	// Memory read/write
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

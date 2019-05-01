# basis_universal
Basis Universal GPU Texture Compression Codec

Basis Universal is a GPU texture compression system that outputs a highly compressed intermediate file format (.basis) that can be quickly transcoded to a wide variety of GPU texture compression formats: PVRTC1 4bpp RGB, BC7 mode 6 RGB, BC1-5, ETC1, and ETC2. We will be adding ASTC RGB or RGBA, BC7 mode 4/5 RGBA, and PVRTC1 4bpp RGBA next. Basis files support non-uniform texture arrays, so cubemaps, volume textures, texture arrays, mipmap levels, video sequences, or arbitrary texture "tiles" can be stored in a single file. The compressor is able to exploit color and pattern correlations across the entire file, so multiple images with mipmaps can be stored very efficiently in a single file.

So far, we've compiled the code using MSVS 2019, under Ubuntu x64 using cmake with either clang 3.8 or gcc 5.4, and emscripten 1.35 to asm.js. (Be sure to use this version of emcc, as earlier versions fail with internal errors/exceptions during compilation.) The compressor uses OpenMP for multithreading, but if you don't have OpenMP it'll still work (just much more slowly). The transcoder is currently single threaded (and doesn't use OpenMP).

### 3rd party code dependencies

The transcoder (in the "transcoder" directory) has no 3rd party code dependencies.

The encoder uses [lodepng](https://lodev.org/lodepng/) for loading and saving PNG images, which is Copyright (c) 2005-2019 Lode Vandevenne. It uses the zlib license.

### Command Line Tool

The command line tool is named "basisu". Run basisu without any parameters for help. 

To compress an sRGB image to .basis:

`basisu -srgb x.png`

Note that basisu defaults to linear colorspace metrics, not sRGB. If the input is a photograph, or a diffuse/albedo/specular/etc. texture, you will definitely want to use sRGB metrics for less artifacts and better rate distortion performance.

To unpack a .basis file to .png/.ktx files:

`basisu x.basis`

The mipmapped .KTX files will be in a variety of GPU formats (PVRTC1 4bpp, ETC1-2, BC1-5, BC7), and to my knowledge there is no single .KTX viewer tool that supports every GPU texture format. BC1-5 and BC7 files are viewable using AMD's Compressonator, ETC1/2 using Mali's Texture Compression Tool, and PVRTC1 using Imagination Tech's PVRTexTool. Links:

[Mali Texture Compression Tool](https://duckduckgo.com/?q=mali+texture+compression+tool&atb=v146-1&ia=web)]

[Compressonator](https://gpuopen.com/gaming-product/compressonator/)

[PVRTexTool](https://www.imgtec.com/developers/powervr-sdk-tools/pvrtextool/)

For the maximum possible achievable quality with the current former and encoder, use (remove -srgb for non-photographic images):

`basisu x.png -srgb -slower -max_endpoint_clusters 8192 -max_selector_clusters 7936 -no_selector_rdo -no_auto_global_sel_pal`

### WebGL test 

The "WebGL" directory contains a very simple WebGL demo that uses the transcoder compiled to asm.js using emscripten. It currently only supports transcoding to the BC1 and BC3 texture formats. The file WebGL/basis_wrappers.cpp contains a simple C-style API that the Javascript code calsl to interface with the C++ Basis transcoder.

On browsers that don't support BC1 (Firefox is one), there's a low-quality fallback code path for opaque textures (but no fallback for BC3 yet). Note that the fallback path only converts to 16-bit RGB images at the moment, so the quality isn't as good as it should be.

Note that I was unable to disable assertions when compiling the transcoder in release (-O2), and I'm not sure why yet. Currently, basisu.h forcefully #undef's the assert() macro, which is ugly but works. Including assert()'s in release will slow transcoding down.

### Transcoder details

To use .basis files in an application, you only need the files in the "transcoder" directory. The entire transcoder lives in a single .cpp file: transcoder/basisu_transcoder.cpp. If compiling with gcc/clang, be sure strict aliasing is disabled when compiling this file, as I have not tested either the encoder or transcoder with strict aliasing enabled: -fno-strict-aliasing (The Linux kernel is also compiled with this option.)

To use the transcoder, #include "transcoder/basisu_transcoder.h". Call `basist::basisu_transcoder_init()` a single time (probably at startup). Also, ideally once at startup, you need to create a single instance of the `basist::etc1_global_selector_codebook` class, like this:

`basist::etc1_global_selector_codebook sel_codebook(basist::g_global_selector_cb_size, basist::g_global_selector_cb);`

Now you can use the transcoder, which is implemented in the "basisu_transcoder" class in transcoder/basisu_transcoder.h. The key methods are `start_decoding()`, `get_total_images()`, `get_image_info()`, `get_image_level_info()`, and `transcode_image_level()`. 

I will be simplifying the transcoder so the caller doesn't need to deal with etc1_global_selector_codebook's next. To get an idea how to use the API, you can check out WebGL/basis_wrappers.cpp.

`transcode_image_level()` and `transcode_slice()` are thread safe, i.e. you can decompress multiple images/slices from multiple threads. 

### Quick Basis file details

Internally, Basis files are composed of a non-uniform texture array of one or more 2D ETC1S texture "slices". ETC1S is a simple subset of the ETC1 texture format popular on Android. ETC1S has no block flips, no 4x2 or 2x4 subblocks, and each block only uses 555 base colors. ETC1S is still 100% standard ETC1, so transcoding to ETC1 or the color block of ETC2 is a no-op. We choose ETC1S because it has the valuable property that it can be quickly transcoded to almost any other GPU texture format at high quality using only simple per-block operations with small 1D lookup tables. Transcoding to PVRTC1 involves only simple block level operations to compute the endpoints, and simple per-pixel scalar operations to compute the modulation values.

Basis files have a single set of compressed global endpoint/selector codebooks in ETC1S format, which all slices utilize. The ETC1S texture data is compressed using vector quantization (VQ) seperately on the endpoints and selectors, followed by DPCM/RLE/psuedo-MTF/canonical Huffman coding. Each ETC1S texture slice may be a different resolution. Mipmaps (if any) are always stored in order from largest to smallest level. The file format supports either storing the selector codebook directly (using DPCM+Huffman), or storing the selector codebook using a hierarchical virtual codebook scheme.

One the codebook and Huffman tables are decompressed, the slices are randomly accessible in any order. Opaque files always have one slice per image, and files with alpha channels always have two slices per image (even if some images in the file don't have alpha channels, i.e. alpha is all or nothing). The transcoder abstracts these details away into a simple "image" API, which is what most callers will use. An image is either a single RGB slice, or two (RGB+A) slices. Internally, alpha slices are also in ETC1S format.

We currently only support CPU transcoding, but GPU assisted transcoding/format conversion is also possible by uploading the decompressed codebooks as textures and using compute shaders to convert the ETC1S data to the desired output format.

### Special thanks
Thanks to a number of companies or groups who have supported Binomial over the years, which helped fund this work: Intel, SpaceX, Netflix, Forgotten Empires, Microsoft, Polystream, Hothead Games, BioDigital, Magic Leap, Activision, and the organizers at CppCon.

Thanks to John Brooks at Blue Shift, Inc. for inspiring this work by showing me his Dreamcast texture compression system around 2002, and for releasing etc2comp. I first saw the subblock flip estimation approach (used in basisu_etc.cpp) in etc2comp.

Thanks to Colt McAnlis, for advertising one of my earlier open source texture compression libraries at GDC.

I first saw precomputed tables for computing optimal encodings of solid color blocks in ryg_dxt.

### Possible improvements
The current max codebook sizes are 8K. 12-16K codebooks would result in higher quality, but changes would be needed to the way delta indices are compressed.

The way the -q (quality) option is converted to codebook sizes is very simple (fixed formulas), and could be improved. 

The compressor's front end doesn't support endpoint tiling, which purposely constrains adjacent ETC1S block to use the same endpoint colors/intensity levels. The file format already has tiling optimizations, but the frontend needs to be modified to better exploit it.

The various Huffman codes could be divided up into groups (like Zstd), for much faster Huffman decoding in the transcoder. Also, larger slices could be divided up into multiple segments, and each segment transcoded using a different thread. Both of these changes would modify the format.

PVRTC1 modulation values could be determined using multiple threads and/or SIMD code.

PVRTC1 2bpp and ATITC support wouldn't be hard to add.

The transcoder's BC7 tables are a bit large, and can be reduced, which would allow the transcoder to be downloaded more quickly.

3-bit selectors for alpha would greatly improve the quality of the alpha, but would break the file format and require extensive additions to the compressor/transcoder.


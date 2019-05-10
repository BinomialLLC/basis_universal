# basis_universal
Basis Universal GPU Texture Compression Codec

Basis Universal is a ["supercompressed"](http://gamma.cs.unc.edu/GST/gst.pdf) GPU texture compression system that outputs a highly compressed intermediate file format (.basis) that can be quickly transcoded to a wide variety of GPU texture compression formats: PVRTC1 4bpp RGB, BC7 mode 6 RGB, BC1-5, ETC1, and ETC2. We will be adding ASTC RGB or RGBA, BC7 mode 4/5 RGBA, and PVRTC1 4bpp RGBA next. Basis files support non-uniform texture arrays, so cubemaps, volume textures, texture arrays, mipmap levels, video sequences, or arbitrary texture "tiles" can be stored in a single file. The compressor is able to exploit color and pattern correlations across the entire file, so multiple images with mipmaps can be stored very efficiently in a single file.

The system's bitrate depends on the quality setting and image content, but common usable bitrates are .3-1.25 bits/texel. .basis files are typically smaller than using RDO texture compression followed by LZ.

The transcoder has been fuzz tested using [zzuf](https://www.linux.com/news/fuzz-testing-zzuf).

So far, we've compiled the code using MSVS 2019, under Ubuntu x64 using cmake with either clang 3.8 or gcc 5.4, and emscripten 1.35 to asm.js. (Be sure to use this version of emcc, as earlier versions fail with internal errors/exceptions during compilation.) The compressor uses OpenMP for multithreading, but if you don't have OpenMP it'll still work (just much more slowly). The transcoder is currently single threaded (and doesn't use OpenMP).

**Important:** I've changed the .basis file format today (4/7/19). The format should now be hopefully stable unless bugs are found.

### 3rd party code dependencies

The transcoder (in the "transcoder" directory) has no 3rd party code dependencies.

The encoder uses [lodepng](https://lodev.org/lodepng/) for loading and saving PNG images, which is Copyright (c) 2005-2019 Lode Vandevenne. It uses the zlib license.

### Command Line Tool

The command line tool is named "basisu". Run basisu without any parameters for help. 

To compress a sRGB image to .basis:

`basisu x.png`

Note that basisu defaults to sRGB colorspace metrics. If the input is a normal map, or some other type of non-sRGB (non-photographic) texture content, be sure to use -linear to avoid extra unnecessary artifacts.

To add automatically generated mipmaps to the .basis file:

`basisu -mipmap x.png`

There are several mipmap options that allow you to change the filter kernel, the smallest mipmap dimension, etc. The tool also supports generating cubemap files, 2D/cubemap texture arrays, etc.

To unpack a .basis file to multiple .png/.ktx files:

`basisu x.basis`

The mipmapped .KTX files will be in a variety of GPU formats (PVRTC1 4bpp, ETC1-2, BC1-5, BC7), and to my knowledge there is no single .KTX viewer tool that supports every GPU texture format that we support. BC1-5 and BC7 files are viewable using AMD's Compressonator, ETC1/2 using Mali's Texture Compression Tool, and PVRTC1 using Imagination Tech's PVRTexTool. Links:

[Mali Texture Compression Tool](https://duckduckgo.com/?q=mali+texture+compression+tool&atb=v146-1&ia=web)]

[Compressonator](https://gpuopen.com/gaming-product/compressonator/)

[PVRTexTool](https://www.imgtec.com/developers/powervr-sdk-tools/pvrtextool/)

For the maximum possible achievable quality with the current former and encoder, use:

`basisu x.png -slower -max_endpoints 16128 -max_selectors 16128 -no_selector_rdo -no_endpoint_rdo`

Note that "-no_selector_rdo -no_endpoint_rdo" are optional. Using them hurts rate distortion performance, but increases quality. An alternative is to use -selector_rdo_thresh X and -endpoint_rdo_thresh, with X ranging from [1,2] (higher=lower quality/better compression - see the tool's help text).

### WebGL test 

The "WebGL" directory contains a very simple WebGL demo that uses the transcoder compiled to asm.js with [emscripten](https://emscripten.org/). It currently only supports transcoding to the BC1 and BC3 texture formats. The file `WebGL/basis_wrappers.cpp` contains a simple C-style API that the Javascript code calls to interface with the C++ Basis transcoder.

On browsers that don't support BC1 (Firefox is one), there's a low-quality fallback code path for opaque textures (but no fallback for BC3 yet). Note that the fallback path only converts to 16-bit RGB images at the moment, so the quality isn't as good as it should be.

Note that I was unable to disable assertions when compiling the transcoder in release (-O2), and I'm not sure why yet. Currently, basisu.h forcefully #undef's the assert() macro, which is ugly but works. Including assert()'s in release will slow transcoding down.

### Transcoder details

The transcoder unpacks .basis files to various GPU texture formats, almost always without needing to decompress entire images at the pixel level (i.e. it only deals with arrays of blocks). The one exception is PVRTC1, where the transcoder needs to recompute the per-pixel selector ("modulation") values, but it does so using simple scalar operations.

To use .basis files in an application, you only need the files in the "transcoder" directory. The entire transcoder lives in a single .cpp file: transcoder/basisu_transcoder.cpp. If compiling with gcc/clang, be sure strict aliasing is disabled when compiling this file, as I have not tested either the encoder or transcoder with strict aliasing enabled: -fno-strict-aliasing (The Linux kernel is also compiled with this option.)

To use the transcoder, #include "transcoder/basisu_transcoder.h". Call `basist::basisu_transcoder_init()` a single time (probably at startup). Also, ideally once at startup, you need to create a single instance of the `basist::etc1_global_selector_codebook` class, like this:

`basist::etc1_global_selector_codebook sel_codebook(basist::g_global_selector_cb_size, basist::g_global_selector_cb);`

Now you can use the transcoder, which is implemented in the "basisu_transcoder" class in transcoder/basisu_transcoder.h. The key methods are `start_decoding()`, `get_total_images()`, `get_image_info()`, `get_image_level_info()`, and `transcode_image_level()`. 

I will be simplifying the transcoder so the caller doesn't need to deal with etc1_global_selector_codebook's next. To get an idea how to use the API, you can check out WebGL/basis_wrappers.cpp.

`transcode_image_level()` and `transcode_slice()` are thread safe, i.e. you can decompress multiple images/slices from multiple threads. 

### Quick Basis file details

Internally, Basis files are composed of a non-uniform texture array of one or more 2D ETC1S texture "slices". ETC1S is a simple subset of the ETC1 texture format popular on Android. ETC1S has no block flips, no 4x2 or 2x4 subblocks, and each block only uses 555 base colors. ETC1S is still 100% standard ETC1, so transcoding to ETC1 or the color block of ETC2 is a no-op. We choose ETC1S because it has the very valuable property that it can be quickly transcoded to almost any other GPU texture format at very high quality using only simple per-block operations with small 1D lookup tables. Transcoding ETC1S to BC1 usually only introduces around .3 dB Y PSNR quality loss, with less loss for ETC1S->BC7. Transcoding to PVRTC1 involves only simple block level operations to compute the endpoints, and simple per-pixel scalar operations to compute the modulation values.

Basis files have a single set of compressed global endpoint/selector codebooks in ETC1S format, which all slices utilize. The ETC1S texture data is compressed using vector quantization (VQ) seperately on the endpoints and selectors, followed by DPCM/RLE/psuedo-MTF/canonical Huffman coding. Each ETC1S texture slice may be a different resolution. Mipmaps (if any) are always stored in order from largest to smallest level. The file format supports either storing the selector codebook directly (using DPCM+Huffman), or storing the selector codebook using a hierarchical virtual codebook scheme. 

Once the codebook and Huffman tables are decompressed, the slices are randomly accessible in any order. Opaque files always have one slice per image mipmap level, and files with alpha channels always have two slices per image mipmap level (even if some images in the file don't have alpha channels, i.e. alpha is all or nothing at the file level). The transcoder abstracts these details away into a simple "image" API, which is what most callers will use. An image is either one or more RGB slices (one per mipmap level), or one or more pairs of RGB/A slices (two per mipmap level). Internally, alpha slices are also stored in ETC1S format, like the color data, so selector correlations across color/alpha can be exploited. This also allows both RGB and alpha slices to be transcoded to opaque-only texture formats like ETC1, BC1, or PVRTC1 with no transparency.

We currently only support CPU transcoding, but GPU assisted transcoding/format conversion is also possible by uploading the decompressed codebooks as textures and using compute shaders to convert the ETC1S codebook block indices to the desired output texture or pixel format.

### Calling the encoder from C/C++

I'm going to provide a simple C-style API to call the encoder directly. For now, you can call the C++ interface in basisu_comp.cpp/.h. See struct basis_compressor_params and class basis_compressor. Almost the entire command line tool's functionality is in basis_compressor. This class does support doing 100% in-memory compression with no file I/O.

### GPU texture format support details

Internally, all ETC1S slices can be converted to any format. The transcoder's image API's supports converting alpha slices to color texture formats, which allows the user to transcode textures with alpha to two ETC1 images, etc.

ETC1 - The internal file format is ETC1S, so outputting ETC1 texture data is a no-op. We only use differential encodings, each subblock uses the same base color (the differential color is always [0,0,0]), and flips are always enabled.

ETC2 - The color block will be ETC1S, and the alpha block is EAC. Conversion from ETC1S->EAC is nearly lossless.

BC1 - ETC1S->BC1 conversion loses approx. .3-.5 dB Y PSNR relative to the source ETC1S data. We don't currently use 3 color (punchthrough) blocks, but we could easy add them. 

BC3 - The color block is BC1, the alpha block is BC4. ETC1S->BC4 is nearly lossless.

BC4 - ETC1S->BC4 conversion is nearly lossless.

BC5 - Two BC4 blocks.

BC7 - Currently we only output mode 6, for opaque only textures. The conversion from ETC1S->BC7 is nearly lossless. We'll soon be adding mode 4 or 5 support for alpha textures.

PVRTC1 4bpp - Currently we only support opaque textures. The conversion from ETC1S->PVRTC1 is a two step process. The first step finds the RGB bounding boxes of each ETC1S block, which is fast (we don't need to process the entire block's pixels, just the 1-4 used block colors). The first pass occurs during ETC1S transcoding. The second pass computes the per-pixel 2-bpp modulation values, which is fast because we can do this in a luma-like colorspace using simple scalar (not full RGB) operations. The second pass is highly optimized, and threading it would be easy. Quality is roughly the same as PVRTexTool's "Normal (Good Quality)" setting. ETC1S->PVRTC1 loses the most quality - several Y dB PSNR. (I'll be adding better statistics here soon.)

Interestingly, the low pass filtering-like artifacts due to PVRTC1's unique block endpoint interpolation help obscure ETC1S chroma artifacts. 

We will be adding PVRTC1 4bpp transparent support soon, and possibly PVRTC1 2bpp.

Currently, the PVRTC1 transcoder requires that the ETC1S texture's dimensions both be a power of two (but non-square is OK, although I believe iOS doesn't support that). We will be adding the ability to transcode non-pow2 ETC1S textures to larger pow2 PVRTC1 textures soon.

ASTC1: 4x4 is definitely coming and will be comparable to BC7's quality. We may also support fast conversion to ASTC 6x6 pixel blocks.

### Improvements vs. our earlier work

Basis supports up to 16K codebooks for both endpoints and selectors for significantly higher quality textures, uses much higher quality codebook generators, the format uses a new prediction scheme for block endpoints (replicate one of 3 neighbors or use DPCM+Huffman from left neighbor), the format uses a selector history buffer, and RLE codes are implemented for all symbol types for high efficiency on simpler textures. The encoder also implements several new rate distortion optimization stages on both endpoint and selectors, and in Basis the encoder backend can call back into the frontend to reoptimize endpoints or selectors after the RDO stages modify the block codebook indices for better rate distortion performance. 

The file format also supports very large non-uniform texture arrays, making the system usable as an RDO backend in specialized block-based video encoders. Internally, the encoder only handles blocks and all later RDO stages which assume a fixed 2D raster order of the blocks can be optionally disabled.

### Special thanks
A big thanks to Google for partnering with us and enabling this code to be open sourced.

Thanks to a number of companies or groups who have supported Binomial over the years: Intel, SpaceX, Netflix, Forgotten Empires, Microsoft, Polystream, Hothead Games, BioDigital, Magic Leap, Activision, the Khronos Group, and the organizers at CppCon.

Thanks to John Brooks at Blue Shift, Inc. for inspiring this work by showing me his Dreamcast texture compression system around 2002, and for releasing etc2comp. I first saw the subblock flip estimation approach (used in basisu_etc.cpp) in etc2comp.

Thanks to Colt McAnlis, for advertising one of my earlier open source texture compression libraries at GDC, and Won Chun, who originally suggested making a universal system.

I first saw using precomputed tables for quickly computing optimal encodings of solid color blocks in ryg_dxt. The method that limits the canonical Huffman codelengths to a maximum codesize was used in Yoshizaki's lharc.

### Possible improvements
The way the -q (quality) option is converted to codebook sizes is very simple (fixed formulas), and could be improved. It has a tendancy to plateue on some files.

The various Huffman codes could be divided up into groups (like Zstd), for much faster Huffman decoding in the transcoder. Also, larger slices could be divided up into multiple segments, and each segment transcoded using a different thread. Both of these changes would modify the format.

PVRTC1 modulation values could be determined using multiple threads and/or SIMD code.

PVRTC1 2bpp and ATITC support wouldn't be hard to add.

The transcoder's BC7 tables are a bit large, and can be reduced, which would allow the transcoder to be downloaded more quickly.

3-bit selectors for alpha would greatly improve the quality of the alpha, but would break the file format and require extensive additions to the compressor/transcoder.

Fast 6x6 ASTC support may be possible.

# basis_universal
Basis Universal Supercompressed GPU Texture Codec

Basis Universal is a ["supercompressed"](http://gamma.cs.unc.edu/GST/gst.pdf) GPU texture compression system that outputs a highly compressed intermediate file format (.basis) that can be quickly transcoded to a [very wide variety](https://github.com/BinomialLLC/basis_universal/wiki/OpenGL-texture-format-enums-table) of GPU compressed and uncompressed pixel formats: ASTC 4x4 L/LA/RGB/RGBA, PVRTC1 4bpp RGB/RGBA, PVRTC2 RGB/RGBA, BC7 mode 6 RGB, BC7 mode 5 RGB/RGBA, BC1-5 RGB/RGBA/X/XY, ETC1 RGB, ETC2 RGBA, ATC RGB/RGBA, ETC2 EAC R11 and RG11, FXT1 RGB, and uncompressed raster image formats 8888/565/4444. 

The system now supports two modes: a high quality mode which is internally based off the [UASTC compressed texture format](https://richg42.blogspot.com/2020/01/uastc-block-format-encoding.html), and the original lower quality mode which is based off a subset of ETC1 called "ETC1S". UASTC is for extremely high quality (similar to BC7 quality) textures, and ETC1S is for very small files. The ETC1S system includes built-in data compression, while the UASTC system includes an optional Rate Distortion Optimization (RDO) post-process stage that conditions the encoded UASTC texture data in the .basis file so it can be more effectively LZ compressed by the end user. More technical details about UASTC integration are [here](https://github.com/BinomialLLC/basis_universal/wiki/UASTC-implementation-details).

Basis files support non-uniform texture arrays, so cubemaps, volume textures, texture arrays, mipmap levels, video sequences, or arbitrary texture "tiles" can be stored in a single file. The compressor is able to exploit color and pattern correlations across the entire file, so multiple images with mipmaps can be stored very efficiently in a single file.

The system's bitrate depends on the quality setting and image content, but common usable ETC1S bitrates are .3-1.25 bits/texel. ETC1S .basis files are typically 10-25% smaller than using RDO texture compression of the internal texture data stored in the .basis file followed by LZMA. For UASTC files, the bitrate is fixed at 8bpp, but with RDO post-processing and user-provided LZ compression on the .basis file the effective bitrate can be as low as 2bpp for video or for individual textures approximately 4-6bpp.

The transcoder has been fuzz tested using [zzuf](https://www.linux.com/news/fuzz-testing-zzuf).

So far, we've compiled the code using MSVS 2019, under Ubuntu x64 using cmake with either clang 3.8 or gcc 5.4, and emscripten 1.35 to asm.js. (Be sure to use this version or later of emcc, as earlier versions fail with internal errors/exceptions during compilation.) The compressor is multithreaded by default, but this can be disabled using the -no_multithreading command line option. The transcoder is currently single threaded.

Basis Universal supports "skip blocks" in ETC1S compressed texture arrays, which makes it useful for basic [compressed texture video](http://gamma.cs.unc.edu/MPTC/) applications. Note that Basis Universal is still at heart a GPU texture compression system, not a dedicated video codec, so bitrates will be larger than even MPEG1.

[Release Notes](https://github.com/BinomialLLC/basis_universal/wiki/Release-Notes)

### Important Usage Notes

Probably the most important concept to understand about Basis Universal before using it: The system supports **two** very different universal texture modes: The original "ETC1S" mode is low/medium quality, but the resulting file sizes are very small because the system has built-in compression for ETC1S texture format files. This is the command line encoding tool's default mode. ETC1S textures work best on images, photos, map data, or albedo/specular/etc. textures, but don't work as well on normal maps. There's the second "UASTC" mode, which is significantly higher quality (near-BC7 grade), and is usable on all texture types including complex normal maps. UASTC mode purposely does not have built-in file compression like ETC1S mode does, so the resulting files are quite large (8-bits/texel - same as BC7) compared to ETC1S mode. The UASTC encoder has an optional Rate Distortion Optimization (RDO) encoding mode (implemented as a post-process over the encoded UASTC texture data), which lowers the output data's entropy in a way that results in better compression when UASTC .basis files are compressed with Deflate/Zstd, etc. In UASTC mode, you must losslessly compress the file yourself.

Basis Universal is not an image compression codec. It's a texture compression codec. It can be used just like an image compression codec, but that's not the only use case. Here's a [good intro](http://renderingpipeline.com/2012/07/texture-compression/) to GPU texture compression. If you're looking to primarily use the system as an image compression codec on sRGB photographic content, use the default ETC1S mode, because it has built-in compression. 

**The "-q X" option controls the output quality in ETC1S mode.** The default is quality level 128. "-q 255" will increase quality quite a bit. If you want even higher quality, try "-max_selectors 16128 -max_endpoints 16128" instead of -q. -q internally tries to set the codebook sizes (or the # of quantization intervals for endpoints/selectors) for you. You need to experiment with the quality level on your content.

For tangent space normal maps, you should separate X into RGB and Y into Alpha, and provide the compressor with 32-bit/pixel input images. Or use the "-separate_rg_to_color_alpha" command line option which does this for you. The internal texture format that Basis Universal uses (ETC1S) doesn't handle tangent space normal maps encoded into RGB well. You need to separate the channels and recover Z in the pixel shader using z=sqrt(1-x^2-y^2).

### 3rd party code dependencies

The stand-alone transcoder (in the "transcoder" directory) is a single .cpp source file library which has no 3rd party code dependencies.

The encoder uses [lodepng](https://lodev.org/lodepng/) for loading and saving PNG images, which is Copyright (c) 2005-2019 Lode Vandevenne. It uses the zlib license. It also uses [apg_bmp](https://github.com/capnramses/apg/tree/master/apg_bmp) for loading BMP images, which is Copyright 2019 Anton Gerdelan. It uses the Apache 2.0 license.

The encoder uses [tcuAstcUtil.cpp](https://chromium.googlesource.com/external/deqp/+/refs/heads/master/framework/common/tcuAstcUtil.cpp), from the [Android drawElements Quality Program (deqp) Testing Suite](https://source.android.com/devices/graphics/deqp-testing), for unpacking the transcoder's ASTC output for testing/validation purposes. This code is Copyright 2016 The Android Open Source Project, and uses the Apache 2.0 license. We have modified the code so it has no external dependencies, and disabled HDR support.

### Legal/IP/license stuff

Basis Universal uses texture compression formats or technologies created by several companies: ARM Holdings, AMD, Ericsson, Microsoft, and Imagination Technologies Limited. All are supported by various open standards or API's from [The Khronos Group](https://www.khronos.org/), such as OpenGL 4.5, OpenGL ES, or Vulkan. 

A few texture formats (such as AMD/ATI's ATC texture format, or PVRTC2) were not documented sufficiently by the originator of the format. In these cases, we relied on open source code references from other authors, or information in published articles/papers to implement support for those texture formats in our transcoder. These references are included here.

ASTC usage falls under ARM's [END USER LICENCE AGREEMENT FOR THE MALI ASTC SPECIFICATION AND SOFTWARE CODEC license agreement](https://github.com/ARM-software/astc-encoder/blob/master/LICENSE.md). 

PVRTC1/2: See the [PVRTC Specification and User Guide](https://www.imgtec.com/downloads/download-info/pvrtc-texture-compression-user-guide-2/). Imagination Technologies Limited, 23 Nov 2018. Also see the [Khronos Data Format Specification](https://www.khronos.org/registry/DataFormat/specs/1.3/dataformat.1.3.html). See [PVR Texture Compression Exploration](https://roartindon.blogspot.com/2014/08/pvr-texture-compression-exploration.html) and [PvrTcCompressor](https://bitbucket.org/jthlim/pvrtccompressor/src). Also see [Texture Compression Techniques](http://sv-journal.org/2014-1/06/en/index.php?lang=en#7-3).

ETC1 and ETC2 EAC: See the [Khronos Data Format Specification](https://www.khronos.org/registry/DataFormat/specs/1.3/dataformat.1.3.html) and the [OpenGL 4.5 Core Profile](https://www.khronos.org/registry/OpenGL/specs/gl/glspec45.core.pdf) Appendix C.

BC1-5,7: Part of Microsoft's Direct3D API technology. See [Texture Block Compression in Direct3D 11](https://docs.microsoft.com/en-us/windows/win32/direct3d11/texture-block-compression-in-direct3d-11). Also see the [squish library](https://github.com/Ethatron/squish-ccr).

ATC: See the OpenGL extension [GL_AMD_compressed_ATC_texture](https://www.khronos.org/registry/OpenGL/extensions/AMD/AMD_compressed_ATC_texture.txt). For low-level ATC texture format information, see [S3TConv](https://github.com/Triang3l/S3TConv) and the paper [A Method for Load-Time Conversion of DXTC Assets to ATC](http://www.guildsoftware.com/papers/2012.Converting.DXTC.to.ATC.pdf).

FXT1: See the OpenGL extension [GL_3DFX_texture_compression_FXT1](https://www.khronos.org/registry/OpenGL/extensions/3DFX/3DFX_texture_compression_FXT1.txt).

Also see [Intel(R) Open Source HD Graphics Programmers' Reference Manual (PRM)](https://01.org/sites/default/files/documentation/intel-gfx-bspec-osrc-chv-bsw-vol05-memory-views.pdf). This reference manual details how to encode FXT1, ETC1, ETC2, EAC, DXT/BC1-3, BC4/5/7, and ASTC.

### Repository Licensing with REUSE

The repository has been updated to be compliant with the REUSE licenese
checking tool (https://reuse.software/). This was done by adding the complete
text of all licenses used under the LICENSES/ directory and adding the
.reuse/dep5 file which specifies licenses for files which don't contain
them in a form which can be automatically parse by the reuse tool. REUSE
does not alter copyrights or licenses, simply captures information about
licensing to ensure the entire repository has explicit licensing information.

To ensure continued REUSE compliance, run `reuse lint` at the root of
a clean, checked-out repository periodically, or run it during CI tests
before any build artifacts have been created.

### Command Line Compression Tool

The command line tool used to create, validate, and transcode/unpack .basis files is named "basisu". Run basisu without any parameters for help. 

To build basisu:

```
cmake CMakeLists.txt
make
```

For Visual Studio 2019, you can now either use the CMakeLists.txt file or the included `basisu.sln` file.

To compress a sRGB PNG/BMP/TGA/JPEG image to an ETC1S .basis file:

`basisu x.png`

To compress a image to a higher quality UASTC .basis file:

`basisu -uastc -uastc_level 2 x.png`

To compress a image to a higher quality UASTC .basis file with RDO post processing, so the .basis file is more compressible:

`basisu -uastc -uastc_level 2 -uastc_rdo_q .75 x.png`

-uastc_level X ranges from 0-4 and controls the UASTC encoder's performance vs. quality tradeoff. Level 0 is very fast, but low quality, level 2 is the default quality, while level 3 is the highest practical quality. Level 4 is impractically slow, but highest quality.

-uastc_rdo_q X controls the rate distortion stage's quality setting. The lower this value, the higher the quality, but the larger the compressed file size. Good values to try are between .2-3.0. The default is 1.0. RDO post-processing is currently pretty slow, but we'll be optimizing it over time.

UASTC texture video is supported and has been tested. In RDO mode with 7zip LZMA, we've seen average bitrates between 1-2 bpp. ETC1S mode is recommended for texture video, which gets bitrates around .25-.3 bpp.

Note that basisu defaults to sRGB colorspace metrics. If the input is a normal map, or some other type of non-sRGB (non-photographic) texture content, be sure to use -linear to avoid extra unnecessary artifacts. (Note: Currently, UASTC mode always uses linear colorspace metrics. sRGB and angulate metrics are comming soon.)

To add automatically generated mipmaps to the .basis file, at a higher than default quality level (which ranges from [1,255]):

`basisu -mipmap -q 190 x.png`

There are several mipmap options that allow you to change the filter kernel, the filter colorspace for the RGB channels (linear vs. sRGB), the smallest mipmap dimension, etc. The tool also supports generating cubemap files, 2D/cubemap texture arrays, etc.

To create a slightly higher quality ETC1S .basis file (one with better codebooks) at the default quality level (128) - note this is much slower to encode:

`basisu -comp_level 2 x.png`

To unpack a .basis file to multiple .png/.ktx files:

`basisu x.basis`

The mipmapped or cubemap .KTX files will be in a wide variety of compressed GPU texture formats (PVRTC1 4bpp, ETC1-2, BC1-5, BC7, etc.), and to my knowledge there is no single .KTX viewer tool that correctly and reliably supports every GPU texture format that we support. BC1-5 and BC7 files are viewable using AMD's Compressonator, ETC1/2 using Mali's Texture Compression Tool, and PVRTC1 using Imagination Tech's PVRTexTool. Links:

[Mali Texture Compression Tool](https://developer.arm.com/tools-and-software/graphics-and-gaming/graphics-development-tools/mali-texture-compression-tool)

[Compressonator](https://gpuopen.com/gaming-product/compressonator/)

[PVRTexTool](https://www.imgtec.com/developers/powervr-sdk-tools/pvrtextool/)

After compression, the compressor transcodes all slices in the output .basis file to validate that the file decompresses correctly. It also validates all header, compressed data, and slice data CRC16's.

For best quality, you must supply basisu with original uncompressed source images. Any other type of lossy compression applied before basisu (including ETC1/BC1-5, BC7, JPEG, etc.) will cause multi-generational artifacts to appear in the final output textures. 

For the maximum possible achievable ETC1S mode quality with the current format and encoder (completely ignoring encoding speed!), use:

`basisu x.png -comp_level 5 -max_endpoints 16128 -max_selectors 16128 -no_selector_rdo -no_endpoint_rdo`

Level 5 is extremely slow, so unless you have a very powerful machine, levels 2-4 are recommended.

Note that "-no_selector_rdo -no_endpoint_rdo" are optional. Using them hurts rate distortion performance, but increases quality. An alternative is to use -selector_rdo_thresh X and -endpoint_rdo_thresh, with X ranging from [1,2] (higher=lower quality/better compression - see the tool's help text).

To compress small video sequences, say using tools like ffmpeg and VirtualDub:

`basisu -comp_level 1 -tex_type video -stats -debug -multifile_printf "pic%04u.png" -multifile_num 200 -multifile_first 1 -max_selectors 16128 -max_endpoints 16128 -endpoint_rdo_thresh 1.05 -selector_rdo_thresh 1.05`

The reference encoder will take a LONG time and a lot of CPU to encode video. The more cores your machine has, the better. Basis is intended for smaller videos of a few dozen seconds or so. If you are very patient and have a Threadripper or Xeon workstation, you should be able to encode up to a few thousand 720P frames. The "webgl_videotest" directory contains a very simple video viewer.

The .basis file will contain multiple images (all using the same global codebooks), which you can retrieve using the transcoder's image API. The system now supports [conditional replenisment](https://en.wikipedia.org/wiki/MPEG-1) (CR, or "skip blocks"). CR can reduce the bitrate of some videos (highly dependent on how dynamic the content is) by over 50%. For videos using CR, the images must be requested from the transcoder in sequence from first to last, and random access is only allowed to I-Frames. 

If you are doing rate distortion comparisons vs. other similar systems, be sure to experiment with increasing the endpoint RDO threshold (-endpoint_rdo_thresh X). This setting controls how aggressively the compressor's backend will combine together nearby blocks so they use the same block endpoint codebook vectors, for better coding efficiency. X defaults to a modest 1.5, which means the backend is allowed to increase the overall color distance by 1.5x while searching for merge candidates. The higher this setting, the better the compression, with the tradeoff of more block artifacts. Settings up to ~2.25 can work well, and make the codec more competitive. "-endpoint_rdo_thresh 1.75" is a good setting on many textures.

For video, level 1 should result in decent results on most clips. For less banding, level 2 can make a big difference. This is still an active area of development, and quality/encoding perf. will improve over time.

To control the ETC1S encoder's quality vs. encoding speed tradeoff, see [ETC1S Compression Effort Levels](https://github.com/BinomialLLC/basis_universal/wiki/ETC1S-Compression-Effort-Levels).

### More detailed examples

`basisu x.png`\
Compress sRGB image x.png to a ETC1S format x.basis file using default settings (multiple filenames OK). ETC1S format files are typically very small on disk (around .5-1.5 bits/texel).

`basisu -uastc x.png`\
Compress image x.png to a UASTC format x.basis file using default settings (multiple filenames OK). UASTC files are the same size as BC7 on disk (8-bpp). Be sure to compress UASTC .basis files yourself using Deflate, zstd, etc. To increase .basis file compressibility (trading off quality for smaller compressed files) use the "-uastc_rdo_q X" command line parameter.

`basisu -q 255 x.png`\
Compress sRGB image x.png to x.basis at max quality level achievable without  manually setting the codebook sizes (multiple filenames OK)

`basisu x.basis`\
Unpack x.basis to PNG/KTX files (multiple filenames OK)

`basisu -validate -file x.basis`\
Validate x.basis (check header, check file CRC's, attempt to transcode all slices)

`basisu -unpack -file x.basis`\
Validates, transcodes and unpacks x.basis to mipmapped .KTX and RGB/A .PNG files (transcodes to all supported GPU texture formats)

`basisu -q 255 -file x.png -mipmap -debug -stats`\
Compress sRGB x.png to x.basis at quality level 255 with compressor debug output/statistics

`basisu -linear -max_endpoints 16128 -max_selectors 16128 -file x.png`\
Compress non-sRGB x.png to x.basis using the largest supported manually specified codebook sizes

`basisu -linear -global_sel_pal -no_hybrid_sel_cb -file x.png`\
Compress a non-sRGB image, use virtual selector codebooks for improved compression (but slower encoding)

`basisu -linear -global_sel_pal -file x.png`\
Compress a non-sRGB image, use hybrid selector codebooks for slightly improved compression (but slower encoding)

`basisu -tex_type video -framerate 20 -multifile_printf "x%02u.png" -multifile_first 1 -multifile_count 20 -selector_rdo_thresh 1.05 -endpoint_rdo_thresh 1.05`\
Compress a 20 sRGB source image video sequence (x01.png, x02.png, x03.png, etc.) to x01.basis

`basisu -comp_level 2 -q 255 -file x.png -mipmap -y_flip`\
Compress a mipmapped x.basis file from an sRGB image named x.png, Y flip each source image, set encoder to level 2 for slightly higher quality (but slower encoding).

### WebGL test 

The "WebGL" directory contains three simple WebGL demos that use the transcoder and compressor compiled to wasm with [emscripten](https://emscripten.org/). See more details [here](webgl/README.md).

![Screenshot of 'texture' example running in a browser.](webgl/texture/preview.png)
![Screenshot of 'gltf' example running in a browser.](webgl/gltf/preview.png)
![Screenshot of 'encode_test' example running in a browser.](webgl/encode_test/preview.png)

### WebAssembly Support Using Emscripten

Both the transcoder and now the compressor (as of 12/17/2020) may be compiled using emscripten to WebAssembly and used on the web. Currently, multithreading is not supported by the compressor when compiled with emscripten. A simple Web compression demo is in webgl/encode_test. All compressor features, including texture video, are supported and fully exposed.

To enable compression support compile the JavaScript wrappers in webgl/transcoding/basis_wrappers.cpp with BASISU_SUPPORT_ENCODING set to 1. See the webgl/encoding directory. 

### Special thanks
A huge thanks to Google for partnering with us and enabling this system to be open sourced.

Thanks to a number of companies or groups who have supported or helped out Binomial over the years: Intel, SpaceX, Netflix, Forgotten Empires, Microsoft, Polystream, Hothead Games, BioDigital, Magic Leap, Blizzard Entertainment, Insomniac Games, Rockstar Games, Facebook, Activision, the Khronos Group, and the organizers at CppCon.

Thanks to Dave Wilkinson (AMD/Khronos) for supporting us and giving us very valuable feedback while we developed Basis Universal.

Thanks to Chris Wein (Netflix), who showed us the path to Texture Video.

Thanks to Mike Dussault (SpaceX) for supporting Binomial in the early days.

Thanks to Graeme Devine at Magic Leap.

Thanks to Matt Pritchard, formerly of Valve Software and Microsoft, for helping me with the computer hardware I used while building this system and its predecessor. 

Thanks to John Brooks at Blue Shift, Inc. for inspiring this work by showing me his Dreamcast texture compression system around 2002, and for releasing etc2comp. I first saw the subblock flip estimation approach (used in basisu_etc.cpp) in etc2comp.

Thanks to Colt McAnlis, for advertising one of my earlier open source texture compression libraries at GDC, and Won Chun, who originally suggested making a universal system.

Thanks to Chas Boyd (Microsoft), for inspiring us to work on texture compression full-time. Chas also gave us great feedback about UASTC before it was released.

I first saw using precomputed tables for quickly computing optimal encodings of solid color blocks in ryg_dxt. The method that limits the canonical Huffman codelengths to a maximum codesize was used in Yoshizaki's lharc. The canonical Huffman codelength compression system is similar to Katz's Deflate method.

### Possible improvements
The codebook generation process is basically a high quality, but slow and brute force reference. It's possible to massively speed up codebook gen in several ways. One way is to not throw away the tree structures constructed during the creation of the initial codebooks. 

The way the -q (quality) option is converted to codebook sizes is very simple (fixed formulas), and could be improved. It has a tendency to plateau on some files.

The various Huffman codes could be divided up into groups (like Zstd), for much faster Huffman decoding in the transcoder. Also, larger slices could be divided up into multiple segments, and each segment transcoded using a different thread. Both of these changes would modify the format.

PVRTC1 modulation values could be determined using multiple threads and/or SIMD code.

PVRTC1 2bpp support wouldn't be hard to add.

The transcoder's BC7 tables are a bit large, and can be reduced, which would allow the transcoder to be downloaded more quickly.

3-bit selectors for alpha would greatly improve the quality of the alpha, but would break the file format and require extensive additions to the compressor/transcoder.

Fast 6x6 ASTC support may be possible.

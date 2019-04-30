# basis_universal
Basis Universal GPU Texture Compression Codec

So far, we've compiled this using MSVS 2019, and under Ubuntu using cmake with either clang 3.8 or gcc 5.4.

The command line tool is named "basisu". Run basisu without any parameters for help. 

To compress an sRGB image to .basis:

basisu -srgb x.png

Note that basisu defaults to linear colorspace metrics, not sRGB. If the input is a photograph, or a diffuse/albedo/specular/etc. texture, you want to use sRGB for much better rate distortion performance. 

To unpack a .basis file to .png/.ktx files:

basisu x.basis

The mipmapped .KTX files will be in a variety of GPU formats (PVRTC1 4bpp, ETC1-2, BC1-5, BC7), and to my knowledge there is no single .KTX viewer tool that supports every GPU texture format. BC1-5 and BC7 files are viewable using AMD's Compressonator, ETC1/2 using Mali's Texture Compression Tool, and PVRTC1 using Imagination Tech's PVRTexTool. Links:

[Mali Texture Compression Tool](https://duckduckgo.com/?q=mali+texture+compression+tool&atb=v146-1&ia=web)]

[Compressonator](https://gpuopen.com/gaming-product/compressonator/)

[PVRTexTool](https://www.imgtec.com/developers/powervr-sdk-tools/pvrtextool/)

### Transcoder details

To use .basis files in an application, you only need the files in the "transcoder" directory. The entire transcoder lives in a single .cpp file: transcoder/basisu_transcoder.cpp. If compiling with gcc/clang, be sure strict aliasing is disabled when compiling this file, as I have not tested either the encoder or transcoder with strict aliasing enabled: -fno-strict-aliasing (The Linux kernel is also compiled with this option.)

To use the transcoder, #include "transcoder/basisu_transcoder.h" and "transcoder/basisu_global_selector_palette.h". Call basist::basisu_transcoder_init() a single time (probably at startup). Also, ideally once at startup, you need to create a single instance of the basist::etc1_global_selector_codebook class, like this:

basist::etc1_global_selector_codebook sel_codebook(basist::g_global_selector_cb_size, basist::g_global_selector_cb);

Now you can use the transcoder, which is implemented in the "basisu_transcoder" class in transcoder/basisu_transcoder.h. The key methods are start_decoding(), get_total_images(), get_image_info(), get_image_level_info(), and transcode_image_level(). 

I will be simplifying the transcoder so the caller doesn't need to deal with etc1_global_selector_codebook's next.

transcode_image_level() and transcode_slice() are thread safe, i.e. you can decompressor multiple images/slices from multiple threads. start_decoding() is not thread safe.

### Quick Basis file details

Internally, Basis files are composed of a non-uniform texture array of one or more 2D ETC1S texture "slices". ETC1S is a simple subset of the ETC1 texture format popular on Android. ETC1S has no block flips, no 4x2 or 2x4 subblocks, and each block only uses 555 base colors. ETC1S is still 100% standard ETC1, so transcoding to ETC1 or ETC2 is a no-op. We choose ETC1S because it has the valuable property that it can be quickly transcoded (converted) to any other GPU texture format at high quality using only simple per-block operations with small 1D lookup tables. 

Basis files have a single set of compressed global endpoint/selector codebooks, which all slices refer to. The ETC1S texture data is compressed using vector quantization (VQ) seperately on the endpoints and selectors, followed by DPCM/RLE/psuedo-MTF/canonical Huffman coding. Each ETC1S texture slice may be a different resolution. Mipmaps (if any) are always stored in order from largest to smallest.

The slices are randomly accessible. Opaque files always have one slice per image, and files with alpha channels always have two slices per image (even if some images in the file don't have alpha channels, i.e. alpha is all or nothing). The transcoder abstracts these details away into a simple "image" API, which is what most people will use. An image may be one slice, or two.

We currently support CPU transcoding to PVRTC1 opaque, BC7 mode 6 opaque, BC1-5, ETC1/2, and PVRTC1 4bpp opaque. The next steps are to add ASTC opaque/alpha, BC7 mode 4 or 5 opaque/alpha, PVRTC1 4bpp transparent, and possibly PVRTC1 2bpp. GPU assisted transcoding/format conversion is also possible.

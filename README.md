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

https://duckduckgo.com/?q=mali+texture+compression+tool&atb=v146-1&ia=web
https://gpuopen.com/gaming-product/compressonator/
https://www.imgtec.com/developers/powervr-sdk-tools/pvrtextool/

To use .basis files in an application, you only need the files in the "transcoder" directory. The entire transcoder lives in a single .cpp file: transcoder/basisu_transcoder.cpp. If compiling with gcc/clang, be sure strict aliasing is disabled when compiling this file: -fno-strict-aliasing

To use the transcoder, #include "transcoder/basisu_transcoder.h" and "transcoder/basisu_global_selector_palette.h". Call basist::basisu_transcoder_init() a single time (probably at startup). Also, probably at startup, you need to create a single instance of the basist::etc1_global_selector_codebook class, like this:

basist::etc1_global_selector_codebook sel_codebook(basist::g_global_selector_cb_size, basist::g_global_selector_cb);

Now you can use the transcoder, which is implemented in the "basisu_transcoder" class in transcoder/basisu_transcoder.h. The key methods are start_decoding(), get_total_images(), get_image_info(), get_image_level_info(), and transcode_image_level(). 

I will be simplifying the transcoder so the caller doesn't need to deal with etc1_global_selector_codebook's next.

transcode_image_level() and transcode_slice() are thread safe, i.e. you can decompressor multiple images/slices from multiple threads. start_decoding() is not thread safe.

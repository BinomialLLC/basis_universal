# basis_universal
Basis Universal GPU Texture Compressor

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


// basisu_tool.cpp
// Copyright (C) 2019-2025 Binomial LLC. All Rights Reserved.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#if _MSC_VER
// For sprintf(), strcpy()
#define _CRT_SECURE_NO_WARNINGS (1)
#pragma warning(disable:4505) //  unreferenced function with internal linkage has been removed
#pragma warning(disable:4189) // local variable is initialized but not referenced
#pragma warning(disable:4100) // unreferenced formal parameter
#endif

#include "transcoder/basisu.h"
#include "transcoder/basisu_transcoder_internal.h"
#include "encoder/basisu_enc.h"
#include "encoder/basisu_etc.h"
#include "encoder/basisu_gpu_texture.h"
#include "encoder/basisu_frontend.h"
#include "encoder/basisu_backend.h"
#include "encoder/basisu_comp.h"
#include "transcoder/basisu_transcoder.h"
#include "encoder/basisu_ssim.h"
#include "encoder/basisu_opencl.h"

#define MINIZ_HEADER_FILE_ONLY
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "encoder/basisu_miniz.h"

#include <queue>
#include <array>

#include "encoder/basisu_resampler.h"
#include "encoder/basisu_resampler_filters.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define CLEAR_WIN32_CONSOLE 0
#endif

// Set BASISU_CATCH_EXCEPTIONS if you want exceptions to crash the app, otherwise main() catches them.
#ifndef BASISU_CATCH_EXCEPTIONS
	#define BASISU_CATCH_EXCEPTIONS 0
#endif

using namespace basisu;
using namespace buminiz;

#define BASISU_TOOL_VERSION "1.60.0"

// Define to lower the -test and -test_hdr tolerances
//#define USE_TIGHTER_TEST_TOLERANCES

// Only enable to verify SAN is working.
//#define FORCE_SAN_FAILURE

enum tool_mode
{
	cDefault,
	cCompress,
	cValidate,
	cInfo,
	cUnpack,
	cCompare,
	cHDRCompare,
	cVersion,
	cBench,
	cCompSize,
	cTestLDR,
	cTestHDR_4x4,
	cTestHDR_6x6,
	cTestHDR_6x6i,
	cCLBench,
	cSplitImage,
	cCombineImages,
	cTonemapImage
};

static void print_usage()
{
	printf("\nUsage: basisu filename [filename ...] <options>\n");

	puts("\n"
		"The default processing mode is compression of one or more .PNG/.BMP/.TGA/.JPG/.QOI/.DDS/.EXR/.HDR files to a LDR or HDR .KTX2 file. Alternate modes:\n"
		" -unpack: Use transcoder to unpack a .basis/.KTX2 file to one or more .KTX/.PNG files\n"
		" -validate: Validate and display information about a .basis/.KTX2 file\n"
		" -info: Display high-level information about a .basis/.KTX2 file\n"
		" -compare: Compare two LDR PNG/BMP/TGA/JPG/QOI images specified with -file, output PSNR and SSIM statistics and RGB/A delta images\n"
		" -compare_hdr: Compare two HDR .EXR/.HDR images specified with -file, output PSNR statistics and RGB delta images\n"
		" -tonemap: Tonemap an HDR or EXR image to PNG at multiple exposures, use -file to specify filename\n"
		" -version: Print version and exit\n"
		"\n"
		"--- Notes:\n"
		"\nUnless an explicit mode is specified, if one or more files have the .basis or .KTX2 extension this tool defaults to unpack mode.\n"
		"\nBy default, the compressor assumes the input is in the sRGB colorspace (like photos/albedo textures).\n"
		"If the input is NOT sRGB (like a normal map), be sure to specify -linear for less artifacts. Depending on the content type, some experimentation may be needed.\n"
		"\n"
		"The TinyEXR library is used to read .EXR images. This library does not support all .EXR compression methods. For unsupported images, you can use ImageMagick to convert them to uncompressed .EXR.\n"
		"\n"
		"For .DDS source files: Mipmapped or not mipmapped 2D textures (but not cubemaps) are supported. Only uncompressed 32-bit RGBA/BGRA, half float RGBA, or float RGBA .DDS files are supported. In -tex_array mode, if a .DDS file is specified, all source files must be in .DDS format.\n"
		"\n"
		"Filenames prefixed with a @ symbol are read as filename listing files. Listing text files specify which actual filenames to process (one filename per line).\n"
		"\n"
		"--- Texture Mode Options:\n"
		" -etc1s: Encode to ETC1S LDR (the default for SDR/LDR inputs). Roughly .8-2.5 bpp.\n"
		" -uastc: Encode to UASTC LDR 4x4. Roughly 5-8 bpp.\n"
		" -hdr/-hdr_4x4: Encode input as UASTC HDR 4x4 (the default if any input file has the .EXR or .HDR extension, or if any .DDS file is HDR). Roughly 5-8 bpp.\n"
		" -hdr_6x6: Encode input as RDO or highest quality UASTC HDR 6x6. Use -lambda X (try 100-20000 or higher) option to enable RDO UASTC HDR 6x6, where x controls the quality vs. size tradeoff. Roughly 1.2-3.2 bpp.\n"
		" -hdr_6x6i: Encode input as UASTC HDR 6x6 intermediate. Use -lambda X (try 100-20000 or higher) option to enable RDO UASTC HDR 6x6, where x controls the quality vs. size tradeoff. Roughly 1-3.2 bpp.\n"
		"\n"
		"--- Options:\n"
		" -ktx2: Write .KTX2 files (the default). By default, UASTC LDR/HDR 4x4 and ASTC 6x6 files will be compressed using Zstandard unless -ktx2_no_zstandard is specified.\n"
		" -basis: Write .basis files instead of .KTX2 files (the previous default).\n"
		" -file filename.png/tga/jpg/qoi/exr/hdr: Input image filename, multiple images are OK, use -file X for each input filename (prefixing input filenames with -file is optional)\n"
		" -alpha_file filename.png/tga/jpg/qoi: Input alpha image filename, multiple images are OK, use -file X for each input filename (must be paired with -file), images converted to REC709 grayscale and used as input alpha\n"
		" -output_file filename: Output .basis/.KTX2 filename\n"
		" -output_path: Output .basis/.KTX2 files to specified directory.\n"
		" -debug or -verbose: Enable codec debug print to stdout (slightly slower).\n"
		" -debug_images: Enable codec debug images (much slower).\n"
		" -stats: Compute and display image quality metrics (slightly to much slower).\n"
		" -individual: Process input images individually and output multiple .basis/.KTX2 files (not as a texture array - this is now the default as of v1.16)\n"
		"\n"
		" -fastest: Set UASTC LDR 4x4 and HDR 4x4/6x6 to fastest but lowest quality encoding mode (same as -uastc_level 0 or -hdr_6x6_level 0)\n"
		" -slower: Set UASTC LDR 4x4 and HDR 4x4/6x6 to slower but a higher quality encoding mode (same as -uastc_level 3 or -hdr_6x6_level 5)\n"
		" -parallel: Compress multiple textures simumtanously (one per thread), instead of one at a time. Compatible with OpenCL mode. This is much faster, but in OpenCL mode the driver is pushed harder, and the CLI output will be jumbled.\n"
		" -linear: Use linear colorspace metrics (instead of the default sRGB or scaled RGB for HDR), and by default linear (not sRGB) mipmap filtering.\n"
		" -tex_type <2d, 2darray, 3d, video, cubemap>: Set Basis file header's texture type field. Cubemap arrays require multiples of 6 images, in X+, X-, Y+, Y-, Z+, Z- order, each image must be the same resolutions.\n"
		"  2d=arbitrary 2D images, 2darray=2D array, 3D=volume texture slices, video=video frames, cubemap=array of faces. For 2darray/3d/cubemaps/video, each source image's dimensions and # of mipmap levels must be the same.\n"
		"  For video, the .basis file will be written with the first frame being an I-Frame, and subsequent frames being P-Frames (using conditional replenishment). Playback must always occur in order from first to last image.\n"
		" -cubemap: same as -tex_type cubemap\n"
		" -tex_array: Process input images as a single texture array and write a single .basis/.KTX2 file (the former default before v1.16)\n"
		" -fuzz_testing: Use with -validate: Disables CRC16 validation of file contents before transcoding\n"
		" -multifile_printf: printf() format strint to use to compose multiple filenames\n"
		" -multifile_first: The index of the first file to process, default is 0 (must specify -multifile_printf and -multifile_num)\n"
		" -multifile_num: The total number of files to process.\n"
		" -opencl: Enable OpenCL usage (currently only accelerates ETC1S encoding)\n"
		" -opencl_serialize: Serialize all calls to the OpenCL driver (to work around buggy drivers, only useful with -parallel)\n"
		"\n"
		"--- ETC1S specific options (-etc1s - the LDR/SDR default):\n"
		" -q X: Set ETC1S quality level, 1-255, default is 128, lower=better compression/lower quality/faster, higher=less compression/higher quality/slower, default is 128. For even higher quality, use -max_endpoints/-max_selectors.\n"
		" -comp_level X: Set ETC1S encoding speed vs. quality tradeoff. Range is 0-6, default is 1. Higher values=MUCH slower, but slightly higher quality. Higher levels intended for videos. Use -q first!\n"
		" -max_endpoints X: ETC1S: Manually set the max number of color endpoint clusters from 1-16128, use instead of -q\n"
		" -max_selectors X: ETC1S: Manually set the max number of color selector clusters from 1-16128, use instead of -q\n"
		"\n"
		"--- UASTC LDR/HDR 4x4 specific options (-uastc):\n"
		" -uastc: Enable UASTC LDR 4x4 texture mode, instead of the default ETC1S mode. Significantly higher texture quality, but much larger (~8bpp) files. (Note that UASTC .basis files must be losslessly compressed by the user.)\n"
		" -uastc_level: Set UASTC LDR/HDR 4x4 encoding level. LDR Range is [0,4], default is 2, higher=slower but higher quality. 0=fastest/lowest quality, 3=slowest practical option, 4=impractically slow/highest achievable quality\n"
		"				UASTC HDR 4x4 range is [0,4] - higher=slower but higher quality. HDR 4x4 default level=1.\n"
		" -uastc_rdo_l X: Enable UASTC LDR 4x4 RDO post-processing and set UASTC LDR 4x4 RDO quality scalar (lambda) to X. Lower values=higher quality/larger LZ\n"
		"                 compressed files, higher values=lower quality/smaller LZ compressed files. Good range to try is [.25-10].\n"
		"                 Note: Previous versons used the -uastc_rdo_q option, which was removed because the RDO algorithm was changed.\n"
		" -uastc_rdo_d X: Set UASTC LDR 4x4 RDO dictionary size in bytes. Default is 4096, max is 65536. Lower values=faster, but less compression.\n"
		" -uastc_rdo_b X: Set UASTC LDR 4x4 RDO max smooth block error scale. Range is [1,300]. Default is 10.0, 1.0=disabled. Larger values suppress more artifacts (and allocate more bits) on smooth blocks.\n"
		" -uastc_rdo_s X: Set UASTC LDR 4x4 RDO max smooth block standard deviation. Range is [.01,65536]. Default is 18.0. Larger values expand the range of blocks considered smooth.\n"
		" -uastc_rdo_f: Don't favor simpler UASTC LDR 4x4 modes in RDO mode.\n"
		" -uastc_rdo_m: Disable RDO multithreading (slightly higher compression, deterministic).\n"
		"\n"
		"--- UASTC HDR 4x4 specific options (-hdr or -hdr_4x4 - the HDR default):\n"
		" -uastc_level X: Sets the UASTC HDR 4x4 compressor's level. Valid range is [0,4] - higher=slower but higher quality. HDR default=1.\n"
		"                 Level 0=fastest/lowest quality, 3=highest practical setting, 4=exhaustive\n"
		" -hdr_uber_mode: Allow the UASTC HDR 4x4 encoder to try varying the CEM 11 selectors more for slightly higher quality (slower). This may negatively impact BC6H quality, however.\n"
		" -hdr_ultra_quant: UASTC HDR 4x4: Try to find better quantized CEM 7/11 endpoint values (slower).\n"
		" -hdr_favor_astc: UASTC HDR 4x4: By default the UASTC HDR 4x4 encoder tries to strike a balance or even slightly favor BC6H quality. If this option is specified, ASTC HDR 4x4 quality is favored instead.\n"
		"\n"
		"--- UASTC HDR 6x6 specific options (-hdr_6x6 or -hdr_6x6i):\n"
		" -lambda X: Enables rate distortion optimization (RDO). The higher this value, the lower the quality, but the smaller the file size. Try 100-20000, or higher values on some images.\n"
		" -hdr_6x6_level X: Sets the codec to 6x6 HDR mode (same as -hdr_6x6) and controls encoder performance vs. max quality tradeoff. X may range from [0,12]. Default level is 2. Higher values result in better quality but slower encoding. Values above 10 are extremely slow.\n"
		" -hdr_6x6i_level X: Sets the codec to 6x6 HDR intermediate mode (same as -hdr_6x6i) and controls encoder performance vs. max quality tradeoff. X may range from [0,12]. Default level is 2.\n"
		" -rec_2020: The input image's gamut is Rec. 2020 vs. the default Rec. 709 - for accurate colorspace error calculations.\n"
		" -hdr_6x6_jnd X, -hdr_6x6_extra_pats, -hdr_6x6_brute_force_pats, -hdr_6x6_comp_levels X Y or -hdr_6x6i_comp_levels X Y: Low-level control over the encoder's configuration.\n"
		"\n"
		"--- SDR/LDR->HDR upconversion options (only used when encoding to HDR formats from an LDR/SDR source image):\n"
		" -hdr_ldr_no_srgb_to_linear: If specified, LDR images will NOT be converted to normalized linear light (via a sRGB->Linear conversion) during SDR->HDR upconversion before compressing as HDR.\n"
		" -hdr_ldr_upconversion_nit_multiplier X: Specify how many nits (candelas per sq. meter) LDR/SDR images are converted to after converting to linear light. Default is 100 nits. Note: Previous builds used 1 nit.\n"
		"\n"
		"--- More options:\n"
		" -test: Run an automated LDR ETC1S/UASTC encoding and transcoding test. Returns EXIT_FAILURE if any failures\n"
		" -test_hdr_4x4/-test_hdr_6x6/-test_hdr_6x6i: Run automated UASTC HDR encoding and transcoding tests. Returns EXIT_FAILURE if any failures\n"
		" -test_dir: Optional directory of test files. Defaults to \"../test_files\".\n"
		" -y_flip: Flip input images vertically before compression\n"
		" -normal_map: Tunes codec parameters for better quality on normal maps (linear colorspace metrics, linear mipmap filtering, no selector RDO, no sRGB)\n"
		" -no_alpha: Always output non-alpha basis files, even if one or more inputs has alpha\n"
		" -force_alpha: Always output alpha basis files, even if no inputs has alpha\n"
		" -separate_rg_to_color_alpha: Separate input R and G channels to RGB and A (for tangent space XY normal maps)\n"
		" -swizzle rgba: Specify swizzle for the 4 input color channels using r, g, b and a (the -separate_rg_to_color_alpha flag is equivalent to rrrg)\n"
		" -renorm: Renormalize each input image before any further processing/compression\n"
		" -no_multithreading: Disable multithreading\n"
		" -max_threads X: Use at most X threads total when multithreading is enabled (this includes the main thread)\n"
		" -no_ktx: Disable KTX writing when unpacking (faster, less output files)\n"
		" -ktx_only: Only write KTX files when unpacking (faster, less output files)\n"
		" -write_out: Write 3dfx OUT files when unpacking FXT1 textures\n"
		" -format_only: Only unpack the specified format, by its numeric code.\n"
		" -etc1_only: Only unpack to ETC1, skipping the other texture formats during -unpack\n"
		" -disable_hierarchical_endpoint_codebooks: Disable hierarchical endpoint codebook usage, slower but higher quality on some compression levels\n"
		" -compare_ssim: Compute and display SSIM of image comparison (slow)\n"
		" -compare_plot: Display histogram plots in -compare mode\n"
		" -bench: UASTC benchmark mode, for development only\n"
		" -resample X Y: Resample all input textures to XxY pixels using a box filter\n"
		" -resample_factor X: Resample all input textures by scale factor X using a box filter\n"
		" -no_sse: Forbid all SSE instruction set usage\n"
		" -validate_etc1s: Validate internal ETC1S compressor's data structures during compression (slower, intended for development).\n"
		" -ktx2_animdata_duration X: Set KTX2animData duration field to integer value X (only valid/useful for -tex_type video, default is 1)\n"
		" -ktx2_animdata_timescale X: Set KTX2animData timescale field to integer value X (only valid/useful for -tex_type video, default is 15)\n"
		" -ktx2_animdata_loopcount X: Set KTX2animData loopcount field to integer value X (only valid/useful for -tex_type video, default is 0)\n"
		" -framerate X: Set framerate in .basis header to X/frames sec.\n"
		" -ktx2_no_zstandard: Don't compress UASTC texture data using Zstandard -- store it uncompressed instead.\n"
		" -ktx2_zstandard_level X: Set ZStandard compression level to X (see Zstandard documentation, default level is 6)\n"
		" -tonemap_dither: Dither tonemapper's 8-bit/component output by adding a small amount of white noise, only used with -tonemap mode\n"
		"\n"
		"--- Mipmap generation options:\n"
		" -mipmap: Generate mipmaps for each source image\n"
		" -mip_srgb: Convert image to linear before filtering, then back to sRGB\n"
		" -mip_linear: Keep image in linear light during mipmap filtering (i.e. do not convert to/from sRGB for filtering purposes)\n"
		" -mip_scale X: Set mipmap filter kernel's scale, lower=sharper, higher=more blurry, default is 1.0\n"
		" -mip_filter X: Set mipmap filter kernel, default is kaiser, filters: box, tent, bell, blackman, catmullrom, mitchell, etc.\n"
		" -mip_renorm: Renormalize normal map to unit length vectors after filtering\n"
		" -mip_clamp: Use clamp addressing on borders, instead of wrapping\n"
		" -mip_fast: Use faster mipmap generation (resample from previous mip, not always first/largest mip level). The default (as of 1/2021)\n"
		" -mip_slow: Always resample each mipmap level starting from the largest mipmap. Higher quality, but slower. Opposite of -mip_fast. Was the prior default before 1/2021.\n"
		" -mip_smallest X: Set smallest pixel dimension for generated mipmaps, default is 1 pixel\n"
		"  By default, textures will be converted from sRGB to linear light before mipmap filtering, then back to sRGB (for the RGB color channels) unless -linear is specified.\n"
		"  You can override this behavior with -mip_srgb/-mip_linear.\n"
		"\n"
		"--- ETC1S backend endpoint/selector RDO codec options:\n"
		" -no_selector_rdo: Disable backend's selector rate distortion optimizations (slightly faster, less noisy output, but lower quality per output bit)\n"
		" -selector_rdo_thresh X: Set selector RDO quality threshold, default is 1.25, lower is higher quality but less quality per output bit (try 1.0-3.0)\n"
		" -no_endpoint_rdo: Disable backend's endpoint rate distortion optimizations (slightly faster, less noisy output, but lower quality per output bit)\n"
		" -endpoint_rdo_thresh X: Set endpoint RDO quality threshold, default is 1.5, lower is higher quality but less quality per output bit (try 1.0-3.0)\n"
		"\n"
		"--- Set various fields in the Basis file header:\n"
		" -userdata0 X: Set 32-bit userdata0 field in Basis file header to X (X is a signed 32-bit int)\n"
		" -userdata1 X: Set 32-bit userdata1 field in Basis file header to X (X is a signed 32-bit int)\n"
		"\n"
		"--- Example LDR ETC1S/UASTC LDR 4x4 command lines:\n"
		" basisu x.png : Compress sRGB image x.png to x.ktx2 using default settings (multiple filenames OK, use -tex_array if you want a tex array vs. multiple output files)\n"
		" basisu -basis x.qoi : Compress sRGB image x.qoi to x.basis (supports 24-bit or 32-bit .QOI files)\n"
		" basisu x.ktx2 : Unpack x.basis to PNG/KTX files (multiple filenames OK)\n"
		" basisu x.basis : Unpack x.basis to PNG/KTX files (multiple filenames OK)\n"
		" basisu -uastc x.png -uastc_rdo_l 2.0 -ktx2 -stats : Compress to a UASTC .KTX2 file with RDO (rate distortion optimization) to reduce .KTX2 compressed file size\n"
		" basisu -file x.png -mipmap -y_flip : Compress a mipmapped x.ktx2 file from an sRGB image named x.png, Y flip each source image\n"
		" basisu -validate -file x.basis : Validate x.basis (check header, check file CRC's, attempt to transcode all slices)\n"
		" basisu -unpack -file x.basis : Validates, transcodes and unpacks x.basis to mipmapped .KTX and RGB/A .PNG files (transcodes to all supported GPU texture formats)\n"
		" basisu -q 255 -file x.png -mipmap -debug -stats : Compress sRGB x.png to x.ktx2 at quality level 255 with compressor debug output/statistics\n"
		" basisu -linear -max_endpoints 16128 -max_selectors 16128 -file x.png : Compress non-sRGB x.png to x.ktx2 using the largest supported manually specified codebook sizes\n"
		" basisu -basis -comp_level 2 -max_selectors 8192 -max_endpoints 8192 -tex_type video -framerate 20 -multifile_printf \"x%02u.png\" -multifile_first 1 -multifile_num 20 : Compress a 20 sRGB source image video sequence (x01.png, x02.png, x03.png, etc.) to x01.basis\n"
		"\n"
		"--- Example UASTC HDR 4x4 command lines:\n"
		" basisu x.exr : Compress a HDR .EXR (or .HDR) image to a UASTC HDR 4x4 .KTX2 file. LDR/SDR images will be upconverted to linear light HDR before compression. See HDR upconversion options, above.\n"
		" basisu -hdr_4x4 x.exr : Compress a HDR .EXR image to a UASTC HDR 4x4 .KTX2 file.\n"
		" basisu x.hdr -uastc_level 0 : Compress a HDR .hdr image to a UASTC HDR 4x4 .KTX2 file, fastest encoding but lowest quality\n"
		" basisu -hdr x.png : Compress a LDR .PNG image to UASTC HDR 4x4 (image is converted from sRGB to linear light first, use -hdr_ldr_no_srgb_to_linear to disable)\n"
		" basisu x.hdr -uastc_level 3 : Compress a HDR .hdr image to UASTC HDR 4x4 at higher quality (-uastc_level 4 is highest quality, but very slow encoding)\n"
		" basisu x.hdr -uastc_level 3 -mipmap -basis -stats -debug -debug_images : Compress a HDR .hdr image to UASTC HDR 4x4, .basis output file, at higher quality, generate mipmaps, output statistics and debug information, and write tone mapped debug images\n"
		" basisu x.hdr -stats -hdr_favor_astc -hdr_uber_mode -uastc_level 4 : Highest achievable ASTC HDR 4x4 quality (very slow encoding, BC6H quality is traded off)\n"
		"\n--- Example UASTC HDR 6x6 command lines:\n"
		" basisu -hdr_6x6 x.exr : Compress a HDR .EXR (or .HDR) image to a UASTC HDR 6x6 .KTX2 file. LDR/SDR images will be upconverted to linear light HDR before compression. See HDR upconversion options, above.\n"
		" basisu -lambda 1000 -hdr_6x6 x.exr : Compress a HDR .EXR (or .HDR) image to a UASTC HDR 6x6 .KTX2 file with rate-distortion optimization (RDO), at lambda level 1000.\n"
		" basisu -hdr_6x6i x.exr : Compress a HDR .EXR image to a compressed intermediate format UASTC HDR 6x6 .KTX2 file.\n"
		" basisu -lambda 1000 -hdr_6x6i x.exr : Compress a HDR .EXR image to a compressed intermediate format UASTC HDR 6x6 .KTX2 file with rate-distortion optimization (RDO), at lambda level 1000.\n"
		"\n"
		"--- Video notes: For video use, it's recommended to encode on a machine with many cores. Use -comp_level 2 or higher for better codebook\n"
		"generation, specify very large codebooks using -max_endpoints and -max_selectors, and reduce the default endpoint RDO threshold\n"
		"(-endpoint_rdo_thresh) to around 1.25. Videos may have mipmaps and alpha channels. Videos must always be played back by the transcoder\n"
		"in first to last image order.\n"
		"Video files currently use I-Frames on the first image, and P-Frames using conditional replenishment on subsequent frames.\n"
		"\nETC1S Compression level (-comp_level X) details. This controls the ETC1S speed vs. quality trandeoff. (Use -q to control the quality vs. compressed size tradeoff.):\n"
		" Level 0: Fastest, but has marginal quality and can be brittle on complex images. Avg. Y dB: 35.45\n"
		" Level 1: Hierarchical codebook searching, faster ETC1S encoding. 36.87 dB, ~1.4x slower vs. level 0. (This is the default setting.)\n"
		" Level 2: Use this or higher for video. Hierarchical codebook searching. 36.87 dB, ~1.4x slower vs. level 0. (This is the v1.12's default setting.)\n"
		" Level 3: Full codebook searching. 37.13 dB, ~1.8x slower vs. level 0. (Equivalent the the initial release's default settings.)\n"
		" Level 4: Hierarchical codebook searching, codebook k-means iterations. 37.15 dB, ~4x slower vs. level 0\n"
		" Level 5: Full codebook searching, codebook k-means iterations. 37.41 dB, ~5.5x slower vs. level 0.\n"
		" Level 6: Full codebook searching, twice as many codebook k-means iterations, best ETC1 endpoint opt. 37.43 dB, ~12x slower vs. level 0\n"
	);
}

static bool load_listing_file(const std::string &f, basisu::vector<std::string> &filenames)
{
	std::string filename(f);
	filename.erase(0, 1);

	FILE *pFile = nullptr;
#ifdef _WIN32
	fopen_s(&pFile, filename.c_str(), "r");
#else
	pFile = fopen(filename.c_str(), "r");
#endif

	if (!pFile)
	{
		error_printf("Failed opening listing file: \"%s\"\n", filename.c_str());
		return false;
	}

	uint32_t total_filenames = 0;

	for ( ; ; )
	{
		char buf[3072];
		buf[0] = '\0';

		char *p = fgets(buf, sizeof(buf), pFile);
		if (!p)
		{
			if (ferror(pFile))
			{
				error_printf("Failed reading from listing file: \"%s\"\n", filename.c_str());

				fclose(pFile);
				return false;
			}
			else
				break;
		}

		std::string read_filename(p);
		while (read_filename.size())
		{
			if (read_filename[0] == ' ')
				read_filename.erase(0, 1);
			else
				break;
		}

		while (read_filename.size())
		{
			const char c = read_filename.back();
			if ((c == ' ') || (c == '\n') || (c == '\r'))
				read_filename.erase(read_filename.size() - 1, 1);
			else
				break;
		}

		if (read_filename.size())
		{
			filenames.push_back(read_filename);
			total_filenames++;
		}
	}

	fclose(pFile);

	printf("Successfully read %u filenames(s) from listing file \"%s\"\n", total_filenames, filename.c_str());

	return true;
}

class command_line_params
{
	BASISU_NO_EQUALS_OR_COPY_CONSTRUCT(command_line_params);

#define REMAINING_ARGS_CHECK(n) if (num_remaining_args < (n)) { error_printf("Error: Expected %u values to follow %s!\n", n, pArg); return false; }

	bool check_for_hdr_options(const char** arg_v, const char* pArg, int arg_index, const int num_remaining_args, int& arg_count)
	{
		if ((strcasecmp(pArg, "-hdr") == 0) || (strcasecmp(pArg, "-hdr_4x4") == 0))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cUASTC_HDR_4x4);
			return true;
		}
		else if (strcasecmp(pArg, "-rec_2020") == 0)
		{
			m_comp_params.m_astc_hdr_6x6_options.m_rec2020_bt2100_color_gamut = true;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_6x6") == 0)
		{
			// max quality (if -lambda=0) or RDO UASTC HDR 6x6
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_HDR_6x6);
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_6x6i") == 0)
		{
			// intermediate format UASTC HDR 6x6
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_HDR_6x6_INTERMEDIATE);
			return true;
		}
		else if (strcasecmp(pArg, "-lambda") == 0)
		{
			REMAINING_ARGS_CHECK(1);

			// Set UASTC HDR 6x6's lambda
			m_comp_params.m_astc_hdr_6x6_options.m_lambda = (float)atof(arg_v[arg_index + 1]);

			if (m_comp_params.m_astc_hdr_6x6_options.m_lambda < 0.0f)
			{
				fmt_error_printf("-lambda: value must be >= 0.0f\n");
				return false;
			}

			// Also set UASTC LDR 4x4's lambda
			m_comp_params.m_rdo_uastc_ldr_4x4_quality_scalar = (float)atof(arg_v[arg_index + 1]);
			m_comp_params.m_rdo_uastc_ldr_4x4 = true;

			arg_count++;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_6x6_jnd") == 0)
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_astc_hdr_6x6_options.m_jnd_optimization = true;
			m_comp_params.m_astc_hdr_6x6_options.m_jnd_delta_itp_thresh = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_6x6_level") == 0)
		{
			REMAINING_ARGS_CHECK(1);
			const int level = atoi(arg_v[arg_index + 1]);
			m_comp_params.m_astc_hdr_6x6_options.set_user_level(level);
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_HDR_6x6);
			arg_count++;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_6x6i_level") == 0)
		{
			REMAINING_ARGS_CHECK(1);
			const int level = atoi(arg_v[arg_index + 1]);
			m_comp_params.m_astc_hdr_6x6_options.set_user_level(level);
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_HDR_6x6_INTERMEDIATE);
			arg_count++;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_6x6_extra_pats") == 0)
		{
			m_comp_params.m_astc_hdr_6x6_options.m_extra_patterns_flag = true;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_6x6_brute_force_pats") == 0)
		{
			m_comp_params.m_astc_hdr_6x6_options.m_brute_force_partition_matching = true;
			return true;
		}
		else if ((strcasecmp(pArg, "-hdr_6x6_comp_levels") == 0) || (strcasecmp(pArg, "-hdr_6x6i_comp_levels") == 0))
		{
			REMAINING_ARGS_CHECK(2);

			const int lo_level = clamp<int>(atoi(arg_v[arg_index + 1]), 0, astc_6x6_hdr::ASTC_HDR_6X6_MAX_COMP_LEVEL);
			const int hi_level = clamp<int>(atoi(arg_v[arg_index + 2]), 0, astc_6x6_hdr::ASTC_HDR_6X6_MAX_COMP_LEVEL);

			m_comp_params.m_astc_hdr_6x6_options.m_master_comp_level = minimum(lo_level, hi_level);
			m_comp_params.m_astc_hdr_6x6_options.m_highest_comp_level = maximum(lo_level, hi_level);

			if (strcasecmp(pArg, "-hdr_6x6_comp_levels") == 0)
				m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_HDR_6x6);
			else
				m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_HDR_6x6_INTERMEDIATE);

			arg_count += 2;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_6x6_no_gaussian") == 0)
		{
			m_comp_params.m_astc_hdr_6x6_options.m_gaussian1_fallback = false;
			m_comp_params.m_astc_hdr_6x6_options.m_gaussian2_fallback = false;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_6x6_gaussian1") == 0)
		{
			m_comp_params.m_astc_hdr_6x6_options.m_gaussian1_strength = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_6x6_gaussian2") == 0)
		{
			m_comp_params.m_astc_hdr_6x6_options.m_gaussian2_strength = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if ((strcasecmp(pArg, "-hdr_ldr_no_srgb_to_linear") == 0) || (strcasecmp(pArg, "-hdr_ldr_upconversion_no_srgb_to_linear") == 0))
		{
			m_comp_params.m_ldr_hdr_upconversion_srgb_to_linear = false;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_ldr_upconversion_black_bias") == 0)
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_ldr_hdr_upconversion_black_bias = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_ldr_upconversion_nit_multiplier") == 0)
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_ldr_hdr_upconversion_nit_multiplier = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_uber_mode") == 0)
		{
			m_comp_params.m_uastc_hdr_4x4_options.m_allow_uber_mode = true;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_ultra_quant") == 0)
		{
			m_comp_params.m_uastc_hdr_4x4_options.m_ultra_quant = true;
			return true;
		}
		else if (strcasecmp(pArg, "-hdr_favor_astc") == 0)
		{
			m_comp_params.m_hdr_favor_astc = true;
			return true;
		}

		return false;
	}

public:
	command_line_params() :
		m_mode(cDefault),
		m_ktx2_mode(true),
		m_ktx2_zstandard(true),
		m_ktx2_zstandard_level(6),
		m_ktx2_animdata_duration(1),
		m_ktx2_animdata_timescale(15),
		m_ktx2_animdata_loopcount(0),
		m_format_only(-1),
		m_multifile_first(0),
		m_multifile_num(0),
		m_max_threads(1024), // surely this is high enough
		m_individual(true),
		m_no_ktx(false),
		m_ktx_only(false),
		m_write_out(false),
		m_etc1_only(false),
		m_fuzz_testing(false),
		m_compare_ssim(false),
		m_compare_plot(false),
		m_parallel_compression(false),
		m_tonemap_dither_flag(false)
	{
		m_comp_params.m_compression_level = basisu::maximum<int>(0, BASISU_DEFAULT_COMPRESSION_LEVEL - 1);

		m_comp_params.m_uastc_hdr_4x4_options.set_quality_level(uastc_hdr_4x4_codec_options::cDefaultLevel);

		m_test_file_dir = "../test_files";
	}

	bool parse(int arg_c, const char **arg_v)
	{
		int arg_index = 1;
		while (arg_index < arg_c)
		{
			const char *pArg = arg_v[arg_index];
			const int num_remaining_args = arg_c - (arg_index + 1);
			int arg_count = 1;

			if ((strcasecmp(pArg, "-help") == 0) || (strcasecmp(pArg, "--help") == 0))
			{
				print_usage();
				exit(EXIT_SUCCESS);
			}

			if (strcasecmp(pArg, "-ktx2") == 0)
			{
				m_ktx2_mode = true;
			}
			else if (strcasecmp(pArg, "-basis") == 0)
			{
				m_ktx2_mode = false;
			}
			else if (strcasecmp(pArg, "-ktx2_no_zstandard") == 0)
			{
				m_ktx2_zstandard = false;
			}
			else if (strcasecmp(pArg, "-ktx2_zstandard_level") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_ktx2_zstandard_level = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-ktx2_animdata_duration") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_ktx2_animdata_duration = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-ktx2_animdata_timescale") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_ktx2_animdata_timescale = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-ktx2_animdata_loopcount") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_ktx2_animdata_loopcount = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-compress") == 0)
				m_mode = cCompress;
			else if (strcasecmp(pArg, "-compare") == 0)
				m_mode = cCompare;
			else if ((strcasecmp(pArg, "-hdr_compare") == 0) || (strcasecmp(pArg, "-compare_hdr") == 0))
				m_mode = cHDRCompare;
			else if (strcasecmp(pArg, "-split") == 0)
				m_mode = cSplitImage;
			else if (strcasecmp(pArg, "-combine") == 0)
				m_mode = cCombineImages;
			else if (strcasecmp(pArg, "-tonemap") == 0)
				m_mode = cTonemapImage;
			else if (strcasecmp(pArg, "-unpack") == 0)
				m_mode = cUnpack;
			else if (strcasecmp(pArg, "-validate") == 0)
				m_mode = cValidate;
			else if (strcasecmp(pArg, "-info") == 0)
				m_mode = cInfo;
			else if ((strcasecmp(pArg, "-version") == 0) || (strcasecmp(pArg, "--version") == 0))
				m_mode = cVersion;
			else if (strcasecmp(pArg, "-compare_ssim") == 0)
				m_compare_ssim = true;
			else if (strcasecmp(pArg, "-compare_plot") == 0)
				m_compare_plot = true;
			else if (strcasecmp(pArg, "-bench") == 0)
				m_mode = cBench;
			else if (strcasecmp(pArg, "-comp_size") == 0)
				m_mode = cCompSize;
			else if ((strcasecmp(pArg, "-test") == 0) || (strcasecmp(pArg, "-test_ldr") == 0))
				m_mode = cTestLDR;
			else if (strcasecmp(pArg, "-test_hdr_4x4") == 0)
				m_mode = cTestHDR_4x4;
			else if (strcasecmp(pArg, "-test_hdr_6x6") == 0)
				m_mode = cTestHDR_6x6;
			else if (strcasecmp(pArg, "-test_hdr_6x6i") == 0)
				m_mode = cTestHDR_6x6i;
			else if (strcasecmp(pArg, "-clbench") == 0)
				m_mode = cCLBench;
			else if (strcasecmp(pArg, "-test_dir") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_test_file_dir = std::string(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-no_sse") == 0)
			{
#if BASISU_SUPPORT_SSE
				g_cpu_supports_sse41 = false;
#endif
			}
			else if (strcasecmp(pArg, "-no_status_output") == 0)
			{
				m_comp_params.m_status_output = false;
			}
			else if (strcasecmp(pArg, "-file") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_input_filenames.push_back(std::string(arg_v[arg_index + 1]));
				arg_count++;
			}
			else if (strcasecmp(pArg, "-alpha_file") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_input_alpha_filenames.push_back(std::string(arg_v[arg_index + 1]));
				arg_count++;
			}
			else if (strcasecmp(pArg, "-multifile_printf") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_multifile_printf = std::string(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-multifile_first") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_multifile_first = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-multifile_num") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_multifile_num = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-uastc") == 0)
			{
				m_comp_params.set_format_mode(basist::basis_tex_format::cUASTC4x4);
			}
			else if (strcasecmp(pArg, "-etc1s") == 0)
			{
				m_comp_params.set_format_mode(basist::basis_tex_format::cETC1S);
			}
			else if (strcasecmp(pArg, "-fastest") == 0)
			{
				m_comp_params.m_pack_uastc_ldr_4x4_flags &= ~cPackUASTCLevelMask;
				m_comp_params.m_pack_uastc_ldr_4x4_flags |= cPackUASTCLevelFastest;

				m_comp_params.m_uastc_hdr_4x4_options.set_quality_level(0);

				m_comp_params.m_astc_hdr_6x6_options.set_user_level(0);
			}
			else if (strcasecmp(pArg, "-slower") == 0)
			{
				m_comp_params.m_pack_uastc_ldr_4x4_flags &= ~cPackUASTCLevelMask;
				m_comp_params.m_pack_uastc_ldr_4x4_flags |= cPackUASTCLevelSlower;

				m_comp_params.m_uastc_hdr_4x4_options.set_quality_level(3);

				m_comp_params.m_astc_hdr_6x6_options.set_user_level(5);
			}
			else if (strcasecmp(pArg, "-uastc_level") == 0)
			{
				REMAINING_ARGS_CHECK(1);

				int uastc_level = atoi(arg_v[arg_index + 1]);

				uastc_level = clamp<int>(uastc_level, 0, TOTAL_PACK_UASTC_LEVELS - 1);

				static_assert(TOTAL_PACK_UASTC_LEVELS == 5, "TOTAL_PACK_UASTC_LEVELS==5");
				static const uint32_t s_level_flags[TOTAL_PACK_UASTC_LEVELS] = { cPackUASTCLevelFastest, cPackUASTCLevelFaster, cPackUASTCLevelDefault, cPackUASTCLevelSlower, cPackUASTCLevelVerySlow };

				m_comp_params.m_pack_uastc_ldr_4x4_flags &= ~cPackUASTCLevelMask;
				m_comp_params.m_pack_uastc_ldr_4x4_flags |= s_level_flags[uastc_level];

				m_comp_params.m_uastc_hdr_4x4_options.set_quality_level(uastc_level);

				arg_count++;
			}
			else if (strcasecmp(pArg, "-resample") == 0)
			{
				REMAINING_ARGS_CHECK(2);
				m_comp_params.m_resample_width = atoi(arg_v[arg_index + 1]);
				m_comp_params.m_resample_height = atoi(arg_v[arg_index + 2]);
				arg_count += 2;
			}
			else if (strcasecmp(pArg, "-resample_factor") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_resample_factor = (float)atof(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-uastc_rdo_l") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_rdo_uastc_ldr_4x4_quality_scalar = (float)atof(arg_v[arg_index + 1]);
				m_comp_params.m_rdo_uastc_ldr_4x4 = true;
				arg_count++;
			}
			else if (strcasecmp(pArg, "-uastc_rdo_d") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_rdo_uastc_ldr_4x4_dict_size = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-uastc_rdo_b") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_rdo_uastc_ldr_4x4_max_smooth_block_error_scale = (float)atof(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-uastc_rdo_s") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_rdo_uastc_ldr_4x4_smooth_block_max_std_dev = (float)atof(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-uastc_rdo_f") == 0)
				m_comp_params.m_rdo_uastc_ldr_4x4_favor_simpler_modes_in_rdo_mode = false;
			else if (strcasecmp(pArg, "-uastc_rdo_m") == 0)
				m_comp_params.m_rdo_uastc_ldr_4x4_multithreading = false;
			else if (strcasecmp(pArg, "-linear") == 0)
				m_comp_params.m_perceptual = false;
			else if (strcasecmp(pArg, "-srgb") == 0)
				m_comp_params.m_perceptual = true;
			else if (strcasecmp(pArg, "-q") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_etc1s_quality_level = clamp<int>(atoi(arg_v[arg_index + 1]), BASISU_QUALITY_MIN, BASISU_QUALITY_MAX);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-output_file") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_output_filename = arg_v[arg_index + 1];
				arg_count++;
			}
			else if (strcasecmp(pArg, "-output_path") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_output_path = arg_v[arg_index + 1];
				arg_count++;
			}
			else if ((strcasecmp(pArg, "-debug") == 0) || (strcasecmp(pArg, "-verbose") == 0))
			{
				m_comp_params.m_debug = true;
				enable_debug_printf(true);
			}
			else if (strcasecmp(pArg, "-validate_etc1s") == 0)
			{
				m_comp_params.m_validate_etc1s = true;
			}
			else if (strcasecmp(pArg, "-validate_output") == 0)
			{
				m_comp_params.m_validate_output_data = true;
			}
			else if (strcasecmp(pArg, "-debug_images") == 0)
				m_comp_params.m_debug_images = true;
			else if (strcasecmp(pArg, "-stats") == 0)
				m_comp_params.m_compute_stats = true;
			else if (strcasecmp(pArg, "-gen_global_codebooks") == 0)
			{
				// TODO
			}
			else if (strcasecmp(pArg, "-use_global_codebooks") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_etc1s_use_global_codebooks_file = arg_v[arg_index + 1];
				arg_count++;
			}
			else if (strcasecmp(pArg, "-comp_level") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_compression_level = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-max_endpoints") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_etc1s_max_endpoint_clusters = clamp<int>(atoi(arg_v[arg_index + 1]), 1, BASISU_MAX_ENDPOINT_CLUSTERS);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-max_selectors") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_etc1s_max_selector_clusters = clamp<int>(atoi(arg_v[arg_index + 1]), 1, BASISU_MAX_SELECTOR_CLUSTERS);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-y_flip") == 0)
				m_comp_params.m_y_flip = true;
			else if (strcasecmp(pArg, "-normal_map") == 0)
			{
				m_comp_params.m_perceptual = false;
				m_comp_params.m_mip_srgb = false;
				m_comp_params.m_no_selector_rdo = true;
				m_comp_params.m_no_endpoint_rdo = true;
			}
			else if (strcasecmp(pArg, "-no_alpha") == 0)
				m_comp_params.m_check_for_alpha = false;
			else if (strcasecmp(pArg, "-force_alpha") == 0)
				m_comp_params.m_force_alpha = true;
			else if ((strcasecmp(pArg, "-separate_rg_to_color_alpha") == 0) ||
			        (strcasecmp(pArg, "-seperate_rg_to_color_alpha") == 0)) // was mispelled for a while - whoops!
			{
				m_comp_params.m_swizzle[0] = 0;
				m_comp_params.m_swizzle[1] = 0;
				m_comp_params.m_swizzle[2] = 0;
				m_comp_params.m_swizzle[3] = 1;
			}
			else if (strcasecmp(pArg, "-swizzle") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				const char *swizzle = arg_v[arg_index + 1];
				if (strlen(swizzle) != 4)
				{
					error_printf("Swizzle requires exactly 4 characters\n");
					return false;
				}
				for (int i=0; i<4; ++i)
				{
					if (swizzle[i] == 'r')
						m_comp_params.m_swizzle[i] = 0;
					else if (swizzle[i] == 'g')
						m_comp_params.m_swizzle[i] = 1;
					else if (swizzle[i] == 'b')
						m_comp_params.m_swizzle[i] = 2;
					else if (swizzle[i] == 'a')
						m_comp_params.m_swizzle[i] = 3;
					else
					{
						error_printf("Swizzle must be one of [rgba]");
						return false;
					}
				}
				arg_count++;
			}
			else if (strcasecmp(pArg, "-renorm") == 0)
				m_comp_params.m_renormalize = true;
			else if ((strcasecmp(pArg, "-no_multithreading") == 0) || (strcasecmp(pArg, "-no_threading") == 0))
			{
				m_comp_params.m_multithreading = false;
			}
			else if (strcasecmp(pArg, "-parallel") == 0)
			{
				m_parallel_compression = true;
			}
			else if (strcasecmp(pArg, "-max_threads") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_max_threads = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-mipmap") == 0)
				m_comp_params.m_mip_gen = true;
			else if (strcasecmp(pArg, "-no_ktx") == 0)
				m_no_ktx = true;
			else if (strcasecmp(pArg, "-ktx_only") == 0)
				m_ktx_only = true;
			else if (strcasecmp(pArg, "-write_out") == 0)
				m_write_out = true;
			else if (strcasecmp(pArg, "-tonemap_dither") == 0)
				m_tonemap_dither_flag = true;
			else if (strcasecmp(pArg, "-format_only") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_format_only = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-etc1_only") == 0)
			{
				m_etc1_only = true;
				m_format_only = (int)basist::transcoder_texture_format::cTFETC1_RGB;
			}
			else if (strcasecmp(pArg, "-disable_hierarchical_endpoint_codebooks") == 0)
				m_comp_params.m_disable_hierarchical_endpoint_codebooks = true;
			else if (check_for_hdr_options(arg_v, pArg, arg_index, num_remaining_args, arg_count))
			{
			}
			else if (strcasecmp(pArg, "-opencl") == 0)
			{
				m_comp_params.m_use_opencl = true;
			}
			else if (strcasecmp(pArg, "-opencl_serialize") == 0)
			{
			}
			else if (strcasecmp(pArg, "-mip_scale") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_mip_scale = (float)atof(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-mip_filter") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_mip_filter = arg_v[arg_index + 1];
				// TODO: Check filter
				arg_count++;
			}
			else if (strcasecmp(pArg, "-mip_renorm") == 0)
				m_comp_params.m_mip_renormalize = true;
			else if (strcasecmp(pArg, "-mip_clamp") == 0)
				m_comp_params.m_mip_wrapping = false;
			else if (strcasecmp(pArg, "-mip_fast") == 0)
				m_comp_params.m_mip_fast = true;
			else if (strcasecmp(pArg, "-mip_slow") == 0)
				m_comp_params.m_mip_fast = false;
			else if (strcasecmp(pArg, "-mip_smallest") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_mip_smallest_dimension = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-mip_srgb") == 0)
				m_comp_params.m_mip_srgb = true;
			else if (strcasecmp(pArg, "-mip_linear") == 0)
				m_comp_params.m_mip_srgb = false;
			else if (strcasecmp(pArg, "-no_selector_rdo") == 0)
				m_comp_params.m_no_selector_rdo = true;
			else if (strcasecmp(pArg, "-selector_rdo_thresh") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_selector_rdo_thresh = (float)atof(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-no_endpoint_rdo") == 0)
				m_comp_params.m_no_endpoint_rdo = true;
			else if (strcasecmp(pArg, "-endpoint_rdo_thresh") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_endpoint_rdo_thresh = (float)atof(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-userdata0") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_userdata0 = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-userdata1") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_userdata1 = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-framerate") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				double fps = atof(arg_v[arg_index + 1]);
				double us_per_frame = 0;
				if (fps > 0)
					us_per_frame = 1000000.0f / fps;

				m_comp_params.m_us_per_frame = clamp<int>(static_cast<int>(us_per_frame + .5f), 0, basist::cBASISMaxUSPerFrame);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-cubemap") == 0)
			{
				m_comp_params.m_tex_type = basist::cBASISTexTypeCubemapArray;
				m_individual = false;
			}
			else if (strcasecmp(pArg, "-tex_type") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				const char* pType = arg_v[arg_index + 1];

				if (strcasecmp(pType, "2d") == 0)
					m_comp_params.m_tex_type = basist::cBASISTexType2D;
				else if (strcasecmp(pType, "2darray") == 0)
				{
					m_comp_params.m_tex_type = basist::cBASISTexType2DArray;
					m_individual = false;
				}
				else if (strcasecmp(pType, "3d") == 0)
				{
					m_comp_params.m_tex_type = basist::cBASISTexTypeVolume;
					m_individual = false;
				}
				else if (strcasecmp(pType, "cubemap") == 0)
				{
					m_comp_params.m_tex_type = basist::cBASISTexTypeCubemapArray;
					m_individual = false;
				}
				else if (strcasecmp(pType, "video") == 0)
				{
					m_comp_params.m_tex_type = basist::cBASISTexTypeVideoFrames;
					m_individual = false;
				}
				else
				{
					error_printf("Invalid texture type: %s\n", pType);
					return false;
				}
				arg_count++;
			}
			else if (strcasecmp(pArg, "-individual") == 0)
				m_individual = true;
			else if ((strcasecmp(pArg, "-tex_array") == 0) || (strcasecmp(pArg, "-texarray") == 0))
				m_individual = false;
			else if (strcasecmp(pArg, "-fuzz_testing") == 0)
				m_fuzz_testing = true;
			else if (strcasecmp(pArg, "-csv_file") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_csv_file = arg_v[arg_index + 1];
				m_comp_params.m_compute_stats = true;

				arg_count++;
			}
			else if (pArg[0] == '-')
			{
				error_printf("Unrecognized command line option: %s\n", pArg);
				return false;
			}
			else
			{
				// Let's assume it's a source filename, so globbing works
				//error_printf("Unrecognized command line option: %s\n", pArg);
				m_input_filenames.push_back(pArg);
			}

			arg_index += arg_count;
			assert(arg_index <= arg_c);
		}

		if (m_comp_params.m_etc1s_quality_level != -1)
		{
			m_comp_params.m_etc1s_max_endpoint_clusters = 0;
			m_comp_params.m_etc1s_max_selector_clusters = 0;
		}
		else if ((!m_comp_params.m_etc1s_max_endpoint_clusters) || (!m_comp_params.m_etc1s_max_selector_clusters))
		{
			m_comp_params.m_etc1s_max_endpoint_clusters = 0;
			m_comp_params.m_etc1s_max_selector_clusters = 0;

			m_comp_params.m_etc1s_quality_level = 128;
		}

		if (!m_comp_params.m_mip_srgb.was_changed())
		{
			// They didn't specify what colorspace to do mipmap filtering in, so choose sRGB if they've specified that the texture is sRGB.
			if (m_comp_params.m_perceptual)
				m_comp_params.m_mip_srgb = true;
			else
				m_comp_params.m_mip_srgb = false;
		}

		return true;
	}

	bool process_listing_files()
	{
		basisu::vector<std::string> new_input_filenames;
		for (uint32_t i = 0; i < m_input_filenames.size(); i++)
		{
			if (m_input_filenames[i][0] == '@')
			{
				if (!load_listing_file(m_input_filenames[i], new_input_filenames))
					return false;
			}
			else
				new_input_filenames.push_back(m_input_filenames[i]);
		}
		new_input_filenames.swap(m_input_filenames);

		basisu::vector<std::string> new_input_alpha_filenames;
		for (uint32_t i = 0; i < m_input_alpha_filenames.size(); i++)
		{
			if (m_input_alpha_filenames[i][0] == '@')
			{
				if (!load_listing_file(m_input_alpha_filenames[i], new_input_alpha_filenames))
					return false;
			}
			else
				new_input_alpha_filenames.push_back(m_input_alpha_filenames[i]);
		}
		new_input_alpha_filenames.swap(m_input_alpha_filenames);

		return true;
	}

	basis_compressor_params m_comp_params;

	tool_mode m_mode;

	bool m_ktx2_mode;
	bool m_ktx2_zstandard;
	int m_ktx2_zstandard_level;
	uint32_t m_ktx2_animdata_duration;
	uint32_t m_ktx2_animdata_timescale;
	uint32_t m_ktx2_animdata_loopcount;

	basisu::vector<std::string> m_input_filenames;
	basisu::vector<std::string> m_input_alpha_filenames;

	std::string m_output_filename;
	std::string m_output_path;

	int m_format_only;

	std::string m_multifile_printf;
	uint32_t m_multifile_first;
	uint32_t m_multifile_num;

	std::string m_csv_file;

	std::string m_etc1s_use_global_codebooks_file;

	std::string m_test_file_dir;

	uint32_t m_max_threads;

	bool m_individual;
	bool m_no_ktx;
	bool m_ktx_only;
	bool m_write_out;
	bool m_etc1_only;
	bool m_fuzz_testing;
	bool m_compare_ssim;
	bool m_compare_plot;
	bool m_parallel_compression;
	bool m_tonemap_dither_flag;
};

static bool expand_multifile(command_line_params &opts)
{
	if (!opts.m_multifile_printf.size())
		return true;

	if (!opts.m_multifile_num)
	{
		error_printf("-multifile_printf specified, but not -multifile_num\n");
		return false;
	}

	std::string fmt(opts.m_multifile_printf);
	// Workaround for MSVC debugger issues. Questionable to leave in here.
	size_t x = fmt.find_first_of('!');
	if (x != std::string::npos)
		fmt[x] = '%';

	if (string_find_right(fmt, '%') == -1)
	{
		error_printf("Must include C-style printf() format character '%%' in -multifile_printf string\n");
		return false;
	}

	for (uint32_t i = opts.m_multifile_first; i < opts.m_multifile_first + opts.m_multifile_num; i++)
	{
		char buf[1024];
#ifdef _WIN32
		sprintf_s(buf, sizeof(buf), fmt.c_str(), i);
#else
		snprintf(buf, sizeof(buf), fmt.c_str(), i);
#endif

		if (buf[0])
			opts.m_input_filenames.push_back(buf);
	}

	return true;
}

struct basis_data
{
	basis_data() :
		m_transcoder()
	{
	}
	uint8_vec m_file_data;
	basist::basisu_transcoder m_transcoder;
};

static basis_data *load_basis_file(const char *pInput_filename, bool force_etc1s)
{
	basis_data* p = new basis_data;
	uint8_vec &basis_data = p->m_file_data;
	if (!basisu::read_file_to_vec(pInput_filename, basis_data))
	{
		error_printf("Failed reading file \"%s\"\n", pInput_filename);
		delete p;
		return nullptr;
	}
	printf("Input file \"%s\"\n", pInput_filename);
	if (!basis_data.size())
	{
		error_printf("File is empty!\n");
		delete p;
		return nullptr;
	}
	if (basis_data.size() > UINT32_MAX)
	{
		error_printf("File is too large!\n");
		delete p;
		return nullptr;
	}
	if (force_etc1s)
	{
		if (p->m_transcoder.get_basis_tex_format((const void*)&p->m_file_data[0], (uint32_t)p->m_file_data.size()) != basist::basis_tex_format::cETC1S)
		{
			error_printf("Global codebook file must be in ETC1S format!\n");
			delete p;
			return nullptr;
		}
	}
	if (!p->m_transcoder.start_transcoding(&basis_data[0], (uint32_t)basis_data.size()))
	{
		error_printf("start_transcoding() failed!\n");
		delete p;
		return nullptr;
	}
	return p;
}

static bool compress_mode(command_line_params &opts)
{
	uint32_t num_threads = 1;

	if (opts.m_comp_params.m_multithreading)
	{
		// We use std::thread::hardware_concurrency() as a hint to determine the default # of threads to put into a pool.
		num_threads = std::thread::hardware_concurrency();
		if (num_threads < 1)
			num_threads = 1;
		if (num_threads > opts.m_max_threads)
			num_threads = opts.m_max_threads;
	}

	job_pool compressor_jpool(opts.m_parallel_compression ? 1 : num_threads);
	if (!opts.m_parallel_compression)
		opts.m_comp_params.m_pJob_pool = &compressor_jpool;

	if (!expand_multifile(opts))
	{
		error_printf("-multifile expansion failed!\n");
		return false;
	}

	if (!opts.m_input_filenames.size())
	{
		error_printf("No input files to process!\n");
		return false;
	}

	basis_data* pGlobal_codebook_data = nullptr;
	if (opts.m_etc1s_use_global_codebooks_file.size())
	{
		pGlobal_codebook_data = load_basis_file(opts.m_etc1s_use_global_codebooks_file.c_str(), true);
		if (!pGlobal_codebook_data)
			return false;

		printf("Loaded global codebooks from .basis file \"%s\"\n", opts.m_etc1s_use_global_codebooks_file.c_str());
	}

	basis_compressor_params &params = opts.m_comp_params;

	if (opts.m_ktx2_mode)
	{
		params.m_create_ktx2_file = true;
		if (opts.m_ktx2_zstandard)
			params.m_ktx2_uastc_supercompression = basist::KTX2_SS_ZSTANDARD;
		else
			params.m_ktx2_uastc_supercompression = basist::KTX2_SS_NONE;

		params.m_ktx2_srgb_transfer_func = opts.m_comp_params.m_perceptual;

		if (params.m_tex_type == basist::basis_texture_type::cBASISTexTypeVideoFrames)
		{
			// Create KTXanimData key value entry
			// TODO: Move this to basisu_comp.h
			basist::ktx2_transcoder::key_value kv;

			const char* pAD = "KTXanimData";
			kv.m_key.resize(strlen(pAD) + 1);
			strcpy((char*)kv.m_key.data(), pAD);

			basist::ktx2_animdata ad;
			ad.m_duration = opts.m_ktx2_animdata_duration;
			ad.m_timescale = opts.m_ktx2_animdata_timescale;
			ad.m_loopcount = opts.m_ktx2_animdata_loopcount;

			kv.m_value.resize(sizeof(ad));
			memcpy(kv.m_value.data(), &ad, sizeof(ad));

			params.m_ktx2_key_values.push_back(kv);
		}

		// TODO- expose this to command line.
		params.m_ktx2_zstd_supercompression_level = opts.m_ktx2_zstandard_level;
	}

	params.m_read_source_images = true;
	params.m_write_output_basis_or_ktx2_files = true;
	params.m_pGlobal_codebooks = pGlobal_codebook_data ? &pGlobal_codebook_data->m_transcoder.get_lowlevel_etc1s_decoder() : nullptr;

	FILE *pCSV_file = nullptr;
	if (opts.m_csv_file.size())
	{
		//pCSV_file = fopen_safe(opts.m_csv_file.c_str(), "a");
		pCSV_file = fopen_safe(opts.m_csv_file.c_str(), "w");
		if (!pCSV_file)
		{
			error_printf("Failed opening CVS file \"%s\"\n", opts.m_csv_file.c_str());
			delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
			return false;
		}
		fprintf(pCSV_file, "Filename, Size, Slices, Width, Height, HasAlpha, BitsPerTexel, Slice0RGBAvgPSNR, Slice0RGBAAvgPSNR, Slice0Luma709PSNR, Slice0BestETC1SLuma709PSNR, Q, CL, Time, RGBAvgPSNRMin, RGBAvgPSNRAvg, AAvgPSNRMin, AAvgPSNRAvg, Luma709PSNRMin, Luma709PSNRAvg\n");
	}

	printf("Processing %u total file(s)\n", (uint32_t)opts.m_input_filenames.size());

	interval_timer all_tm;
	all_tm.start();

	basisu::vector<basis_compressor_params> comp_params_vec;

	const size_t total_files = (opts.m_individual ? opts.m_input_filenames.size() : 1U);
	bool result = true;

	if ((opts.m_individual) && (opts.m_output_filename.size()))
	{
		if (total_files > 1)
		{
			error_printf("-output_file specified in individual mode, but multiple input files have been specified which would cause the output file to be written multiple times.\n");
			delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
			return false;
		}
	}

	uint32_t total_successes = 0, total_failures = 0;

	for (size_t file_index = 0; file_index < total_files; file_index++)
	{
		if (opts.m_individual)
		{
			params.m_source_filenames.resize(1);
			params.m_source_filenames[0] = opts.m_input_filenames[file_index];

			if (file_index < opts.m_input_alpha_filenames.size())
			{
				params.m_source_alpha_filenames.resize(1);
				params.m_source_alpha_filenames[0] = opts.m_input_alpha_filenames[file_index];

				if (params.m_status_output)
					printf("Processing source file \"%s\", alpha file \"%s\"\n", params.m_source_filenames[0].c_str(), params.m_source_alpha_filenames[0].c_str());
			}
			else
			{
				params.m_source_alpha_filenames.resize(0);

				if (params.m_status_output)
					printf("Processing source file \"%s\"\n", params.m_source_filenames[0].c_str());
			}
		}
		else
		{
			params.m_source_filenames = opts.m_input_filenames;
			params.m_source_alpha_filenames = opts.m_input_alpha_filenames;
		}

		if (opts.m_output_filename.size())
			params.m_out_filename = opts.m_output_filename;
		else
		{
			std::string filename;

			string_get_filename(opts.m_input_filenames[file_index].c_str(), filename);
			string_remove_extension(filename);

			if (opts.m_ktx2_mode)
				filename += ".ktx2";
			else
				filename += ".basis";

			if (opts.m_output_path.size())
				string_combine_path(filename, opts.m_output_path.c_str(), filename.c_str());

			params.m_out_filename = filename;
		}

		if (opts.m_parallel_compression)
		{
			comp_params_vec.push_back(params);
		}
		else
		{
			basis_compressor c;

			if (!c.init(opts.m_comp_params))
			{
				error_printf("basis_compressor::init() failed!\n");

				if (pCSV_file)
				{
					fclose(pCSV_file);
					pCSV_file = nullptr;
				}

				delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
				return false;
			}

			interval_timer tm;
			tm.start();

			basis_compressor::error_code ec = c.process();

			tm.stop();

			if (ec == basis_compressor::cECSuccess)
			{
				total_successes++;

				if (params.m_status_output)
				{
					printf("Compression succeeded to file \"%s\" size %zu bytes in %3.3f secs\n", params.m_out_filename.c_str(),
						opts.m_ktx2_mode ? c.get_output_ktx2_file().size() : c.get_output_basis_file().size(),
						tm.get_elapsed_secs());
				}
			}
			else
			{
				total_failures++;

				result = false;

				if (!params.m_status_output)
				{
					error_printf("Compression failed on file \"%s\"\n", params.m_out_filename.c_str());
				}

				bool exit_flag = true;

				switch (ec)
				{
				case basis_compressor::cECFailedReadingSourceImages:
				{
					error_printf("Compressor failed reading a source image!\n");

					if (opts.m_individual)
						exit_flag = false;

					break;
				}
				case basis_compressor::cECFailedValidating:
					error_printf("Compressor failed 2darray/cubemap/video validation checks!\n");
					break;
				case basis_compressor::cECFailedEncodeUASTC:
					error_printf("Compressor UASTC encode failed!\n");
					break;
				case basis_compressor::cECFailedFrontEnd:
					error_printf("Compressor frontend stage failed!\n");
					break;
				case basis_compressor::cECFailedFontendExtract:
					error_printf("Compressor frontend data extraction failed!\n");
					break;
				case basis_compressor::cECFailedBackend:
					error_printf("Compressor backend stage failed!\n");
					break;
				case basis_compressor::cECFailedCreateBasisFile:
					error_printf("Compressor failed creating Basis file data!\n");
					break;
				case basis_compressor::cECFailedWritingOutput:
					error_printf("Compressor failed writing to output Basis file!\n");
					break;
				case basis_compressor::cECFailedUASTCRDOPostProcess:
					error_printf("Compressor failed during the UASTC post process step!\n");
					break;
				case basis_compressor::cECFailedCreateKTX2File:
					error_printf("Compressor failed creating KTX2 file data!\n");
					break;
				default:
					error_printf("basis_compress::process() failed!\n");
					break;
				}

				if (exit_flag)
				{
					if (pCSV_file)
					{
						fclose(pCSV_file);
						pCSV_file = nullptr;
					}

					delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
					return false;
				}
			}

			if ((pCSV_file) && (c.get_stats().size()))
			{
				if (c.get_stats().size())
				{
					float rgb_avg_psnr_min = 1e+9f, rgb_avg_psnr_avg = 0.0f;
					float a_avg_psnr_min = 1e+9f, a_avg_psnr_avg = 0.0f;
					float luma_709_psnr_min = 1e+9f, luma_709_psnr_avg = 0.0f;

					for (size_t slice_index = 0; slice_index < c.get_stats().size(); slice_index++)
					{
						rgb_avg_psnr_min = basisu::minimum(rgb_avg_psnr_min, c.get_stats()[slice_index].m_basis_rgb_avg_psnr);
						rgb_avg_psnr_avg += c.get_stats()[slice_index].m_basis_rgb_avg_psnr;

						a_avg_psnr_min = basisu::minimum(a_avg_psnr_min, c.get_stats()[slice_index].m_basis_a_avg_psnr);
						a_avg_psnr_avg += c.get_stats()[slice_index].m_basis_a_avg_psnr;

						luma_709_psnr_min = basisu::minimum(luma_709_psnr_min, c.get_stats()[slice_index].m_basis_luma_709_psnr);
						luma_709_psnr_avg += c.get_stats()[slice_index].m_basis_luma_709_psnr;
					}

					rgb_avg_psnr_avg /= c.get_stats().size();
					a_avg_psnr_avg /= c.get_stats().size();
					luma_709_psnr_avg /= c.get_stats().size();

					fprintf(pCSV_file, "\"%s\", %u, %u, %u, %u, %u, %f, %f, %f, %f, %f, %u, %u, %f, %f, %f, %f, %f, %f, %f\n",
						params.m_out_filename.c_str(),
						c.get_basis_file_size(),
						(uint32_t)c.get_stats().size(),
						c.get_stats()[0].m_width, c.get_stats()[0].m_height, (uint32_t)c.get_any_source_image_has_alpha(),
						c.get_basis_bits_per_texel(),
						c.get_stats()[0].m_basis_rgb_avg_psnr,
						c.get_stats()[0].m_basis_rgba_avg_psnr,
						c.get_stats()[0].m_basis_luma_709_psnr,
						c.get_stats()[0].m_best_etc1s_luma_709_psnr,
						params.m_etc1s_quality_level, (int)params.m_compression_level, tm.get_elapsed_secs(),
						rgb_avg_psnr_min, rgb_avg_psnr_avg,
						a_avg_psnr_min, a_avg_psnr_avg,
						luma_709_psnr_min, luma_709_psnr_avg);
					fflush(pCSV_file);
				}
			}

			//if ((opts.m_individual) && (params.m_status_output))
			//	printf("\n");

		} // if (opts.m_parallel_compression)

	} // file_index

	if (opts.m_parallel_compression)
	{
		basisu::vector<parallel_results> results;

		bool any_failed = basis_parallel_compress(
			num_threads,
			comp_params_vec,
			results);
		BASISU_NOTE_UNUSED(any_failed);

		for (uint32_t i = 0; i < comp_params_vec.size(); i++)
		{
			if (results[i].m_error_code != basis_compressor::cECSuccess)
			{
				result = false;

				total_failures++;

				error_printf("File %u (first source image: \"%s\", output file: \"%s\") failed with error code %i!\n", i,
					comp_params_vec[i].m_source_filenames[0].c_str(),
					comp_params_vec[i].m_out_filename.c_str(),
					(int)results[i].m_error_code);
			}
			else
			{
				total_successes++;
			}
		}

	} // if (opts.m_parallel_compression)

	printf("Total successes: %u failures: %u\n", total_successes, total_failures);

	all_tm.stop();

	if (total_files > 1)
		printf("Total compression time: %3.3f secs\n", all_tm.get_elapsed_secs());

	if (pCSV_file)
	{
		fclose(pCSV_file);
		pCSV_file = nullptr;
	}
	delete pGlobal_codebook_data;
	pGlobal_codebook_data = nullptr;

	return result;
}

static bool unpack_and_validate_ktx2_file(
	uint32_t file_index,
	const std::string& base_filename,
	uint8_vec& ktx2_file_data,
	command_line_params& opts,
	FILE* pCSV_file,
	basis_data* pGlobal_codebook_data,
	uint32_t& total_unpack_warnings,
	uint32_t& total_pvrtc_nonpow2_warnings)
{
	// TODO
	(void)pCSV_file;
	(void)file_index;

	const bool validate_flag = (opts.m_mode == cValidate);

	if (ktx2_file_data.size() > UINT32_MAX)
	{
		error_printf("KTX2 file too large!\n");
		return false;
	}

	basist::ktx2_transcoder dec;

	if (!dec.init(ktx2_file_data.data(), ktx2_file_data.size_u32()))
	{
		error_printf("ktx2_transcoder::init() failed! File either uses an unsupported feature, is invalid, was corrupted, or this is a bug.\n");
		return false;
	}

	if (!dec.start_transcoding())
	{
		error_printf("ktx2_transcoder::start_transcoding() failed! File either uses an unsupported feature, is invalid, was corrupted, or this is a bug.\n");
		return false;
	}

	printf("Resolution: %ux%u\n", dec.get_width(), dec.get_height());
	fmt_printf("Block size: {}x{}\n", dec.get_block_width(), dec.get_block_height());
	printf("Mipmap Levels: %u\n", dec.get_levels());
	printf("Texture Array Size (layers): %u\n", dec.get_layers());
	printf("Total Faces: %u (%s)\n", dec.get_faces(), (dec.get_faces() == 6) ? "CUBEMAP" : "2D");
	printf("Is Texture Video: %u\n", dec.is_video());

	if (dec.is_hdr())
		fmt_printf("LDR to HDR upconversion nit multiplier: {}\n", dec.get_ldr_hdr_upconversion_nit_multiplier());

	const bool is_etc1s = (dec.get_basis_tex_format() == basist::basis_tex_format::cETC1S);

	bool is_hdr = false;

	const char* pFmt_str = nullptr;
	switch (dec.get_basis_tex_format())
	{
	case basist::basis_tex_format::cETC1S:
	{
		pFmt_str = "ETC1S";
		break;
	}
	case basist::basis_tex_format::cUASTC4x4:
	{
		pFmt_str = "UASTC_LDR_4x4";
		break;
	}
	case basist::basis_tex_format::cUASTC_HDR_4x4:
	{
		is_hdr = true;
		pFmt_str = "UASTC_HDR_4x4";
		break;
	}
	case basist::basis_tex_format::cASTC_HDR_6x6:
	{
		is_hdr = true;
		pFmt_str = "ASTC_HDR_6x6";
		break;
	}
	case basist::basis_tex_format::cASTC_HDR_6x6_INTERMEDIATE:
	{
		is_hdr = true;
		pFmt_str = "ASTC_HDR_6x6_INTERMEDIATE";
		break;
	}
	default:
	{
		assert(0);
		return false;
	}
	}

	printf("Supercompression Format: %s\n", pFmt_str);

	printf("Supercompression Scheme: ");
	switch (dec.get_header().m_supercompression_scheme)
	{
	case basist::KTX2_SS_NONE: printf("NONE\n"); break;
	case basist::KTX2_SS_BASISLZ: printf("BASISLZ\n"); break;
	case basist::KTX2_SS_ZSTANDARD: printf("ZSTANDARD\n"); break;
	default:
		error_printf("Invalid/unknown/unsupported\n");
		return false;
	}

	printf("Has Alpha: %u\n", (uint32_t)dec.get_has_alpha());

	printf("\nKTX2 header vk_format: 0x%X (decimal %u)\n", (uint32_t)dec.get_header().m_vk_format, (uint32_t)dec.get_header().m_vk_format);

	printf("\nData Format Descriptor (DFD):\n");
	printf("DFD length in bytes: %zu\n", dec.get_dfd().size());
	printf("DFD color model: %u\n", dec.get_dfd_color_model());
	printf("DFD color primaries: %u (%s)\n", dec.get_dfd_color_primaries(), basist::ktx2_get_df_color_primaries_str(dec.get_dfd_color_primaries()));
	printf("DFD transfer func: %u (%s)\n", dec.get_dfd_transfer_func(),
		(dec.get_dfd_transfer_func() == basist::KTX2_KHR_DF_TRANSFER_LINEAR) ? "LINEAR" : ((dec.get_dfd_transfer_func() == basist::KTX2_KHR_DF_TRANSFER_SRGB) ? "SRGB" : "?"));
	printf("DFD flags: %u\n", dec.get_dfd_flags());
	printf("DFD samples: %u\n", dec.get_dfd_total_samples());
	if (is_etc1s)
	{
		printf("DFD chan0: %s\n", basist::ktx2_get_etc1s_df_channel_id_str(dec.get_dfd_channel_id0()));
		if (dec.get_dfd_total_samples() == 2)
			printf("DFD chan1: %s\n", basist::ktx2_get_etc1s_df_channel_id_str(dec.get_dfd_channel_id1()));
	}
	else
		printf("DFD chan0: %s\n", basist::ktx2_get_uastc_df_channel_id_str(dec.get_dfd_channel_id0()));

	printf("DFD hex values:\n");
	for (uint32_t i = 0; i < dec.get_dfd().size(); i++)
	{
		printf("0x%X", dec.get_dfd()[i]);
		if ((i + 1) != dec.get_dfd().size())
			printf(",");
		if ((i & 3) == 3)
			printf("\n");
	}
	printf("\n");


	printf("Total key values: %zu\n", dec.get_key_values().size());
	for (uint32_t i = 0; i < dec.get_key_values().size(); i++)
	{
		printf("%u. Key: \"%s\", Value length in bytes: %zu", i, (const char*)dec.get_key_values()[i].m_key.data(), dec.get_key_values()[i].m_value.size());

		if (dec.get_key_values()[i].m_value.size() > 256)
			continue;

		bool is_ascii = true;
		for (uint32_t j = 0; j < dec.get_key_values()[i].m_value.size(); j++)
		{
			uint8_t c = dec.get_key_values()[i].m_value[j];
			if (!(
				((c >= ' ') && (c < 0x80)) ||
				((j == dec.get_key_values()[i].m_value.size() - 1) && (!c))
				))
			{
				is_ascii = false;
				break;
			}
		}

		if (is_ascii)
		{
			uint8_vec s(dec.get_key_values()[i].m_value);
			s.push_back(0);
			printf(" Value String: \"%s\"", (const char *)s.data());
		}
		else
		{
			printf(" Value Bytes: ");
			for (uint32_t j = 0; j < dec.get_key_values()[i].m_value.size(); j++)
			{
				if (j)
					printf(",");
				printf("0x%X", dec.get_key_values()[i].m_value[j]);
			}
		}
		printf("\n");
	}

	if (is_etc1s)
	{
		printf("ETC1S header:\n");

		printf("Endpoint Count: %u, Selector Count: %u, Endpoint Length: %u, Selector Length: %u, Tables Length: %u, Extended Length: %u\n",
			(uint32_t)dec.get_etc1s_header().m_endpoint_count, (uint32_t)dec.get_etc1s_header().m_selector_count,
			(uint32_t)dec.get_etc1s_header().m_endpoints_byte_length, (uint32_t)dec.get_etc1s_header().m_selectors_byte_length,
			(uint32_t)dec.get_etc1s_header().m_tables_byte_length, (uint32_t)dec.get_etc1s_header().m_extended_byte_length);

		printf("Total ETC1S image descs: %zu\n", dec.get_etc1s_image_descs().size());
		for (uint32_t i = 0; i < dec.get_etc1s_image_descs().size(); i++)
		{
			printf("%u. Flags: 0x%X, RGB Ofs: %u Len: %u, Alpha Ofs: %u, Len: %u\n", i,
				(uint32_t)dec.get_etc1s_image_descs()[i].m_image_flags,
				(uint32_t)dec.get_etc1s_image_descs()[i].m_rgb_slice_byte_offset, (uint32_t)dec.get_etc1s_image_descs()[i].m_rgb_slice_byte_length,
				(uint32_t)dec.get_etc1s_image_descs()[i].m_alpha_slice_byte_offset, (uint32_t)dec.get_etc1s_image_descs()[i].m_alpha_slice_byte_length);
		}
	}

	printf("Levels:\n");
	for (uint32_t i = 0; i < dec.get_levels(); i++)
	{
		fmt_printf("{}. Offset: {}, Length: {}, Uncompressed Length: {}\n",
			i, dec.get_level_index()[i].m_byte_offset.get_uint64(),
			dec.get_level_index()[i].m_byte_length.get_uint64(),
			dec.get_level_index()[i].m_uncompressed_byte_length.get_uint64());
	}

	const uint32_t total_layers = maximum<uint32_t>(1, dec.get_layers());

	fmt_printf("Image level info:\n");

	for (uint32_t level_index = 0; level_index < dec.get_levels(); level_index++)
	{
		for (uint32_t layer_index = 0; layer_index < total_layers; layer_index++)
		{
			for (uint32_t face_index = 0; face_index < dec.get_faces(); face_index++)
			{
				basist::ktx2_image_level_info level_info;

				if (!dec.get_image_level_info(level_info, level_index, layer_index, face_index))
				{
					error_printf("Failed retrieving image level information (%u %u %u)!\n", layer_index, level_index, face_index);
					return false;
				}

				fmt_printf("--- Level Index: {}, Layer Index: {}, Face Index: {}\n",
					level_info.m_level_index, level_info.m_layer_index, level_info.m_face_index);

				fmt_printf("Orig width/height: {}x{}\n", level_info.m_orig_width, level_info.m_orig_height);
				fmt_printf("Width/height: {}x{}\n", level_info.m_width, level_info.m_height);
				fmt_printf("Block width/height: {}x{}\n", level_info.m_block_width, level_info.m_block_height);
				fmt_printf("Num blocks: {}x{}, Total blocks: {}\n", level_info.m_num_blocks_x, level_info.m_num_blocks_y, level_info.m_total_blocks);
				fmt_printf("Alpha flag: {}, I-frame flag: {}\n", level_info.m_alpha_flag, level_info.m_iframe_flag);
			}
		}
	}

	fmt_printf("\n");

	if (opts.m_mode == cInfo)
	{
		return true;
	}

	// gpu_images[format][face][layer][level]

	basisu::vector< gpu_image_vec > gpu_images[(int)basist::transcoder_texture_format::cTFTotalTextureFormats][6];

	int first_format = 0;
	int last_format = (int)basist::transcoder_texture_format::cTFTotalTextureFormats;

	if (opts.m_format_only > -1)
	{
		first_format = opts.m_format_only;
		last_format = first_format + 1;
	}

	for (int format_iter = first_format; format_iter < last_format; format_iter++)
	{
		basist::transcoder_texture_format tex_fmt = static_cast<basist::transcoder_texture_format>(format_iter);

		if (basist::basis_transcoder_format_is_uncompressed(tex_fmt))
			continue;

		if (!basis_is_format_supported(tex_fmt, dec.get_basis_tex_format()))
			continue;

		if (tex_fmt == basist::transcoder_texture_format::cTFBC7_ALT)
			continue;

		for (uint32_t face_index = 0; face_index < dec.get_faces(); face_index++)
		{
			gpu_images[(int)tex_fmt][face_index].resize(total_layers);

			for (uint32_t layer_index = 0; layer_index < total_layers; layer_index++)
				gpu_images[(int)tex_fmt][face_index][layer_index].resize(dec.get_levels());
		}
	}

	// Now transcode the file to all supported texture formats and save mipmapped KTX/DDS files
	for (int format_iter = first_format; format_iter < last_format; format_iter++)
	{
		const basist::transcoder_texture_format transcoder_tex_fmt = static_cast<basist::transcoder_texture_format>(format_iter);

		if (basist::basis_transcoder_format_is_uncompressed(transcoder_tex_fmt))
			continue;
		if (!basis_is_format_supported(transcoder_tex_fmt, dec.get_basis_tex_format()))
			continue;
		if (transcoder_tex_fmt == basist::transcoder_texture_format::cTFBC7_ALT)
			continue;

		for (uint32_t level_index = 0; level_index < dec.get_levels(); level_index++)
		{
			for (uint32_t layer_index = 0; layer_index < total_layers; layer_index++)
			{
				for (uint32_t face_index = 0; face_index < dec.get_faces(); face_index++)
				{
					basist::ktx2_image_level_info level_info;

					if (!dec.get_image_level_info(level_info, level_index, layer_index, face_index))
					{
						error_printf("Failed retrieving image level information (%u %u %u)!\n", layer_index, level_index, face_index);
						return false;
					}

					if ((transcoder_tex_fmt == basist::transcoder_texture_format::cTFPVRTC1_4_RGB) || (transcoder_tex_fmt == basist::transcoder_texture_format::cTFPVRTC1_4_RGBA))
					{
						if (!is_pow2(level_info.m_width) || !is_pow2(level_info.m_height))
						{
							total_pvrtc_nonpow2_warnings++;

							printf("Warning: Will not transcode image %u level %u res %ux%u to PVRTC1 (one or more dimension is not a power of 2)\n", layer_index, level_index, level_info.m_width, level_info.m_height);

							// Can't transcode this image level to PVRTC because it's not a pow2 (we're going to support transcoding non-pow2 to the next larger pow2 soon)
							continue;
						}
					}

					basisu::texture_format tex_fmt = basis_get_basisu_texture_format(transcoder_tex_fmt);

					gpu_image& gi = gpu_images[(int)transcoder_tex_fmt][face_index][layer_index][level_index];
					gi.init(tex_fmt, level_info.m_orig_width, level_info.m_orig_height);

					// Fill the buffer with psuedo-random bytes, to help more visibly detect cases where the transcoder fails to write to part of the output.
					fill_buffer_with_random_bytes(gi.get_ptr(), gi.get_size_in_bytes());

					const uint32_t decode_flags = basist::cDecodeFlagsHighQuality;

					interval_timer tm;
					tm.start();

					if (!dec.transcode_image_level(level_index, layer_index, face_index, gi.get_ptr(), gi.get_total_blocks(), transcoder_tex_fmt, decode_flags))
					{
						error_printf("Failed transcoding image level (%u %u %u %u)!\n", layer_index, level_index, face_index, format_iter);
						return false;
					}

					double total_time = tm.get_elapsed_ms();

					printf("Transcode of layer %u level %u face %u res %ux%u format %s succeeded in %3.3f ms\n", layer_index, level_index, face_index,
						level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_time);
				}

			} // format_iter

		} // level_index

	} // image_info

	// Return if we're just validating that transcoding succeeds
	if (validate_flag)
		return true;

	// Now write KTX/DDS files and unpack them to individual PNG's/EXR's
	const bool is_cubemap = (dec.get_faces() > 1);
	const bool is_array = (total_layers > 1);
	const bool is_cubemap_array = is_cubemap && is_array;
	const bool is_mipmapped = dec.get_levels() > 1;
	BASISU_NOTE_UNUSED(is_cubemap_array);
	BASISU_NOTE_UNUSED(is_mipmapped);

	// The maximum Direct3D array size is 2048.
	const uint32_t MAX_DDS_TEXARRAY_SIZE = 2048;

	for (int format_iter = first_format; format_iter < last_format; format_iter++)
	{
		const basist::transcoder_texture_format transcoder_tex_fmt = static_cast<basist::transcoder_texture_format>(format_iter);
		const basisu::texture_format tex_fmt = basis_get_basisu_texture_format(transcoder_tex_fmt);

		if (basist::basis_transcoder_format_is_uncompressed(transcoder_tex_fmt))
			continue;
		if (!basis_is_format_supported(transcoder_tex_fmt, dec.get_basis_tex_format()))
			continue;
		if (transcoder_tex_fmt == basist::transcoder_texture_format::cTFBC7_ALT)
			continue;

		// TODO: Could write DDS texture arrays.

		// No KTX tool that we know of supports cubemap arrays, so write individual cubemap files for each layer.
		if ((!opts.m_no_ktx) && (is_cubemap))
		{
			// Write a separate compressed texture file for each layer in a texarray.
			for (uint32_t layer_index = 0; layer_index < total_layers; layer_index++)
			{
				basisu::vector<gpu_image_vec> cubemap;
				for (uint32_t face_index = 0; face_index < 6; face_index++)
					cubemap.push_back(gpu_images[format_iter][face_index][layer_index]);

				{
					std::string ktx_filename(base_filename + string_format("_transcoded_cubemap_%s_layer_%u.ktx", basist::basis_get_format_name(transcoder_tex_fmt), layer_index));

					if (!write_compressed_texture_file(ktx_filename.c_str(), cubemap, true, true))
					{
						error_printf("Failed writing KTX file \"%s\"!\n", ktx_filename.c_str());
						return false;
					}
					printf("Wrote .KTX cubemap file \"%s\"\n", ktx_filename.c_str());
				}

				if (does_dds_support_format(cubemap[0][0].get_format()))
				{
					std::string dds_filename(base_filename + string_format("_transcoded_cubemap_%s_layer_%u.dds", basist::basis_get_format_name(transcoder_tex_fmt), layer_index));

					if (!write_compressed_texture_file(dds_filename.c_str(), cubemap, true, true))
					{
						error_printf("Failed writing DDS file \"%s\"!\n", dds_filename.c_str());
						return false;
					}
					printf("Wrote .DDS cubemap file \"%s\"\n", dds_filename.c_str());
				}
			} // layer_index
		}

		// For texture arrays, let's be adventurous and write a DDS texture array file. RenderDoc and DDSView (DirectXTex) can view them. (Only RenderDoc allows viewing them entirely.)
		if ((!opts.m_no_ktx) && (is_array) && (total_layers <= MAX_DDS_TEXARRAY_SIZE))
		{
			if (does_dds_support_format(tex_fmt))
			{
				basisu::vector<gpu_image_vec> tex_array;
				for (uint32_t layer_index = 0; layer_index < total_layers; layer_index++)
					for (uint32_t face_index = 0; face_index < dec.get_faces(); face_index++)
						tex_array.push_back(gpu_images[format_iter][face_index][layer_index]);

				std::string dds_filename(base_filename + string_format("_transcoded_array_%s.dds", basist::basis_get_format_name(transcoder_tex_fmt)));

				if (!write_compressed_texture_file(dds_filename.c_str(), tex_array, is_cubemap, true))
				{
					error_printf("Failed writing DDS file \"%s\"!\n", dds_filename.c_str());
					return false;
				}
				printf("Wrote .DDS texture array file \"%s\"\n", dds_filename.c_str());
			}
		}

		// Now unpack each layer and face individually and write KTX/DDS/PNG/EXR files for each
		for (uint32_t layer_index = 0; layer_index < total_layers; layer_index++)
		{
			for (uint32_t face_index = 0; face_index < dec.get_faces(); face_index++)
			{
				gpu_image_vec& gi = gpu_images[format_iter][face_index][layer_index];

				if (!gi.size())
					continue;

				uint32_t level;
				for (level = 0; level < gi.size(); level++)
					if (!gi[level].get_total_blocks())
						break;

				if (level < gi.size())
					continue;

				// Write separate compressed KTX/DDS textures with mipmap levels for each individual texarray layer and face.
				if (!opts.m_no_ktx)
				{
					// Write KTX
					{
						std::string ktx_filename;
						if (is_cubemap)
							ktx_filename = base_filename + string_format("_transcoded_%s_face_%u_layer_%04u.ktx", basist::basis_get_format_name(transcoder_tex_fmt), face_index, layer_index);
						else
							ktx_filename = base_filename + string_format("_transcoded_%s_layer_%04u.ktx", basist::basis_get_format_name(transcoder_tex_fmt), layer_index);

						if (!write_compressed_texture_file(ktx_filename.c_str(), gi, true))
						{
							error_printf("Failed writing KTX file \"%s\"!\n", ktx_filename.c_str());
							return false;
						}
						printf("Wrote .KTX file \"%s\"\n", ktx_filename.c_str());
					}

					// Write DDS if it supports this texture format
					if (does_dds_support_format(gi[0].get_format()))
					{
						std::string dds_filename;
						if (is_cubemap)
							dds_filename = base_filename + string_format("_transcoded_%s_face_%u_layer_%04u.dds", basist::basis_get_format_name(transcoder_tex_fmt), face_index, layer_index);
						else
							dds_filename = base_filename + string_format("_transcoded_%s_layer_%04u.dds", basist::basis_get_format_name(transcoder_tex_fmt), layer_index);

						if (!write_compressed_texture_file(dds_filename.c_str(), gi, true))
						{
							error_printf("Failed writing DDS file \"%s\"!\n", dds_filename.c_str());
							return false;
						}
						printf("Wrote .DDS file \"%s\"\n", dds_filename.c_str());
					}
				}

				// Now unpack and save PNG/EXR files
				for (uint32_t level_index = 0; level_index < gi.size(); level_index++)
				{
					basist::ktx2_image_level_info level_info;

					if (!dec.get_image_level_info(level_info, level_index, layer_index, face_index))
					{
						error_printf("Failed retrieving image level information (%u %u %u)!\n", layer_index, level_index, face_index);
						return false;
					}

					if (basist::basis_transcoder_format_is_hdr(transcoder_tex_fmt))
					{
						imagef u;

						if (!gi[level_index].unpack_hdr(u))
						{
							printf("Warning: Failed unpacking HDR GPU texture data (%u %u %u %u). Unpacking as much as possible.\n", format_iter, layer_index, level_index, face_index);
							total_unpack_warnings++;
						}

						if (!opts.m_ktx_only)
						{
							std::string rgb_filename;
							if (gi.size() > 1)
								rgb_filename = base_filename + string_format("_hdr_unpacked_rgb_%s_level_%u_face_%u_layer_%04u.exr", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index);
							else
								rgb_filename = base_filename + string_format("_hdr_unpacked_rgb_%s_face_%u_layer_%04u.exr", basist::basis_get_format_name(transcoder_tex_fmt), face_index, layer_index);

							if (!write_exr(rgb_filename.c_str(), u, 3, 0))
							{
								error_printf("Failed writing to EXR file \"%s\"\n", rgb_filename.c_str());
								delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
								return false;
							}
							printf("Wrote .EXR file \"%s\"\n", rgb_filename.c_str());
						}
					}
					else
					{
						image u;
						if (!gi[level_index].unpack(u))
						{
							printf("Warning: Failed unpacking GPU texture data (%u %u %u %u). Unpacking as much as possible.\n", format_iter, layer_index, level_index, face_index);
							total_unpack_warnings++;
						}
						//u.crop(level_info.m_orig_width, level_info.m_orig_height);

						bool write_png = true;

						// Save PNG (ignoring alpha)
						if ((!opts.m_ktx_only) && (write_png))
						{
							std::string rgb_filename;
							if (gi.size() > 1)
								rgb_filename = base_filename + string_format("_unpacked_rgb_%s_level_%u_face_%u_layer_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index);
							else
								rgb_filename = base_filename + string_format("_unpacked_rgb_%s_face_%u_layer_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), face_index, layer_index);
							if (!save_png(rgb_filename, u, cImageSaveIgnoreAlpha))
							{
								error_printf("Failed writing to PNG file \"%s\"\n", rgb_filename.c_str());
								delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
								return false;
							}
							printf("Wrote .PNG file \"%s\"\n", rgb_filename.c_str());
						}

						// Save .OUT
						if ((transcoder_tex_fmt == basist::transcoder_texture_format::cTFFXT1_RGB) && (opts.m_write_out))
						{
							std::string out_filename;
							if (gi.size() > 1)
								out_filename = base_filename + string_format("_unpacked_rgb_%s_level_%u_face_%u_layer_%04u.out", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index);
							else
								out_filename = base_filename + string_format("_unpacked_rgb_%s_face_%u_layer_%04u.out", basist::basis_get_format_name(transcoder_tex_fmt), face_index, layer_index);
							if (!write_3dfx_out_file(out_filename.c_str(), gi[level_index]))
							{
								error_printf("Failed writing to OUT file \"%s\"\n", out_filename.c_str());
								return false;
							}
							printf("Wrote .OUT file \"%s\"\n", out_filename.c_str());
						}

						// Save alpha
						if (basis_transcoder_format_has_alpha(transcoder_tex_fmt) && (!opts.m_ktx_only) && (write_png))
						{
							std::string a_filename;
							if (gi.size() > 1)
								a_filename = base_filename + string_format("_unpacked_a_%s_level_%u_face_%u_layer_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index);
							else
								a_filename = base_filename + string_format("_unpacked_a_%s_face_%u_layer_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), face_index, layer_index);
							if (!save_png(a_filename, u, cImageSaveGrayscale, 3))
							{
								error_printf("Failed writing to PNG file \"%s\"\n", a_filename.c_str());
								return false;
							}
							printf("Wrote .PNG file \"%s\"\n", a_filename.c_str());

							std::string rgba_filename;
							if (gi.size() > 1)
								rgba_filename = base_filename + string_format("_unpacked_rgba_%s_level_%u_face_%u_layer_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index);
							else
								rgba_filename = base_filename + string_format("_unpacked_rgba_%s_face_%u_layer_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), face_index, layer_index);
							if (!save_png(rgba_filename, u))
							{
								error_printf("Failed writing to PNG file \"%s\"\n", rgba_filename.c_str());
								return false;
							}
							printf("Wrote .PNG file \"%s\"\n", rgba_filename.c_str());
						}

					} // is_hdr

				} // level_index

			} // face_index

		} // layer_index

	} // format_iter

	if ((opts.m_format_only == -1) && (!validate_flag))
	{
		if (is_hdr)
		{
			// RGBA HALF
			for (uint32_t level_index = 0; level_index < dec.get_levels(); level_index++)
			{
				for (uint32_t layer_index = 0; layer_index < total_layers; layer_index++)
				{
					for (uint32_t face_index = 0; face_index < dec.get_faces(); face_index++)
					{
						const basist::transcoder_texture_format transcoder_tex_fmt = basist::transcoder_texture_format::cTFRGBA_HALF;

						basist::ktx2_image_level_info level_info;

						if (!dec.get_image_level_info(level_info, level_index, layer_index, face_index))
						{
							fmt_error_printf("Failed retrieving image level information ({} {} {})!\n", layer_index, level_index, face_index);
							return false;
						}

						const uint32_t total_pixels = level_info.m_orig_width * level_info.m_orig_height;

						basisu::vector<basist::half_float> half_img(total_pixels * 4);

						fill_buffer_with_random_bytes(&half_img[0], half_img.size_in_bytes());

						interval_timer tm;
						tm.start();

						if (!dec.transcode_image_level(level_index, layer_index, face_index, half_img.data(), total_pixels, transcoder_tex_fmt, 0))
						{
							fmt_error_printf("Failed transcoding image level ({} {} {})!\n", layer_index, level_index, face_index);
							return false;
						}

						double total_transcode_time = tm.get_elapsed_ms();

						fmt_printf("Transcode of level {} layer {} face {} res {}x{} format {} succeeded in {} ms\n",
							level_index, layer_index, face_index,
							level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_transcode_time);

						if ((!validate_flag) && (!opts.m_ktx_only))
						{
							// TODO: HDR alpha support
							imagef float_img(level_info.m_orig_width, level_info.m_orig_height);

							for (uint32_t y = 0; y < level_info.m_orig_height; y++)
								for (uint32_t x = 0; x < level_info.m_orig_width; x++)
									for (uint32_t c = 0; c < 4; c++)
										float_img(x, y)[c] = basist::half_to_float(half_img[(x + y * level_info.m_orig_width) * 4 + c]);

							std::string rgb_filename(base_filename + fmt_string("_hdr_unpacked_rgba_{}_level_{}_face_{}_layer_{04}.exr", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index));
							if (!write_exr(rgb_filename.c_str(), float_img, 3, 0))
							{
								fmt_error_printf("Failed writing to .EXR file \"{}\"\n", rgb_filename.c_str());
								return false;
							}
							fmt_printf("Wrote .EXR file \"{}\"\n", rgb_filename.c_str());
						}

					} // face_index
				} // layer_index
			} // level_index

			// RGB HALF
			for (uint32_t level_index = 0; level_index < dec.get_levels(); level_index++)
			{
				for (uint32_t layer_index = 0; layer_index < total_layers; layer_index++)
				{
					for (uint32_t face_index = 0; face_index < dec.get_faces(); face_index++)
					{
						const basist::transcoder_texture_format transcoder_tex_fmt = basist::transcoder_texture_format::cTFRGB_HALF;

						basist::ktx2_image_level_info level_info;

						if (!dec.get_image_level_info(level_info, level_index, layer_index, face_index))
						{
							fmt_error_printf("Failed retrieving image level information ({} {} {})!\n", layer_index, level_index, face_index);
							return false;
						}

						const uint32_t total_pixels = level_info.m_orig_width * level_info.m_orig_height;

						basisu::vector<basist::half_float> half_img(total_pixels * 3);

						fill_buffer_with_random_bytes(&half_img[0], half_img.size_in_bytes());

						interval_timer tm;
						tm.start();

						if (!dec.transcode_image_level(level_index, layer_index, face_index, half_img.data(), total_pixels, transcoder_tex_fmt, 0))
						{
							fmt_error_printf("Failed transcoding image level ({} {} {})!\n", layer_index, level_index, face_index);
							return false;
						}

						double total_transcode_time = tm.get_elapsed_ms();

						fmt_printf("Transcode of level {} layer {} face {} res {}x{} format {} succeeded in {} ms\n",
							level_index, layer_index, face_index,
							level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_transcode_time);

						if ((!validate_flag) && (!opts.m_ktx_only))
						{
							// TODO: HDR alpha support
							imagef float_img(level_info.m_orig_width, level_info.m_orig_height);

							for (uint32_t y = 0; y < level_info.m_orig_height; y++)
								for (uint32_t x = 0; x < level_info.m_orig_width; x++)
									for (uint32_t c = 0; c < 3; c++)
										float_img(x, y)[c] = basist::half_to_float(half_img[(x + y * level_info.m_orig_width) * 3 + c]);

							std::string rgb_filename(base_filename + fmt_string("_hdr_unpacked_rgb_{}_level_{}_face_{}_layer_{04}.exr", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index));
							if (!write_exr(rgb_filename.c_str(), float_img, 3, 0))
							{
								fmt_error_printf("Failed writing to .EXR file \"{}\"\n", rgb_filename.c_str());
								return false;
							}
							fmt_printf("Wrote .EXR file \"{}\"\n", rgb_filename.c_str());
						}

					} // face_index
				} // layer_index
			} // level_index

			// RGB HALF
			for (uint32_t level_index = 0; level_index < dec.get_levels(); level_index++)
			{
				for (uint32_t layer_index = 0; layer_index < total_layers; layer_index++)
				{
					for (uint32_t face_index = 0; face_index < dec.get_faces(); face_index++)
					{
						const basist::transcoder_texture_format transcoder_tex_fmt = basist::transcoder_texture_format::cTFRGB_9E5;

						basist::ktx2_image_level_info level_info;

						if (!dec.get_image_level_info(level_info, level_index, layer_index, face_index))
						{
							fmt_error_printf("Failed retrieving image level information ({} {} {})!\n", layer_index, level_index, face_index);
							return false;
						}

						const uint32_t total_pixels = level_info.m_orig_width * level_info.m_orig_height;

						basisu::vector<uint32_t> rgb9e5_img(total_pixels);

						fill_buffer_with_random_bytes(&rgb9e5_img[0], rgb9e5_img.size_in_bytes());

						interval_timer tm;
						tm.start();

						if (!dec.transcode_image_level(level_index, layer_index, face_index, rgb9e5_img.data(), total_pixels, transcoder_tex_fmt, 0))
						{
							fmt_error_printf("Failed transcoding image level ({} {} {})!\n", layer_index, level_index, face_index);
							return false;
						}

						double total_transcode_time = tm.get_elapsed_ms();

						fmt_printf("Transcode of level {} layer {} face {} res {}x{} format {} succeeded in {} ms\n",
							level_index, layer_index, face_index,
							level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_transcode_time);

						if ((!validate_flag) && (!opts.m_ktx_only))
						{
							// TODO: HDR alpha support
							imagef float_img(level_info.m_orig_width, level_info.m_orig_height);

							for (uint32_t y = 0; y < level_info.m_orig_height; y++)
								for (uint32_t x = 0; x < level_info.m_orig_width; x++)
									astc_helpers::unpack_rgb9e5(rgb9e5_img[x + y * level_info.m_orig_width], float_img(x, y)[0], float_img(x, y)[1], float_img(x, y)[2]);

							std::string rgb_filename(base_filename + fmt_string("_hdr_unpacked_rgb_{}_level_{}_face_{}_layer_{04}.exr", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index));
							if (!write_exr(rgb_filename.c_str(), float_img, 3, 0))
							{
								fmt_error_printf("Failed writing to .EXR file \"{}\"\n", rgb_filename.c_str());
								return false;
							}
							fmt_printf("Wrote .EXR file \"{}\"\n", rgb_filename.c_str());
						}

					} // face_index
				} // layer_index
			} // level_index

		}
		else
		{
			// TODO: Add LDR uncompressed formats
		}
	}

	return true;
}

static bool unpack_and_validate_basis_file(
	uint32_t file_index,
	const std::string &base_filename,
	uint8_vec &basis_file_data,
	command_line_params& opts,
	FILE *pCSV_file,
	basis_data* pGlobal_codebook_data,
	uint32_t &total_unpack_warnings,
	uint32_t &total_pvrtc_nonpow2_warnings)
{
	const bool validate_flag = (opts.m_mode == cValidate);

	basist::basisu_transcoder dec;

	if (pGlobal_codebook_data)
	{
		dec.set_global_codebooks(&pGlobal_codebook_data->m_transcoder.get_lowlevel_etc1s_decoder());
	}

	if (!opts.m_fuzz_testing)
	{
		// Skip the full validation, which CRC16's the entire file.

		// Validate the file - note this isn't necessary for transcoding
		if (!dec.validate_file_checksums(&basis_file_data[0], (uint32_t)basis_file_data.size(), true))
		{
			error_printf("File version is unsupported, or file failed one or more CRC checks!\n");

			return false;
		}
	}

	printf("File version and CRC checks succeeded\n");

	basist::basisu_file_info fileinfo;
	if (!dec.get_file_info(&basis_file_data[0], (uint32_t)basis_file_data.size(), fileinfo))
	{
		error_printf("Failed retrieving Basis file information!\n");
		return false;
	}

	assert(fileinfo.m_total_images == fileinfo.m_image_mipmap_levels.size());
	assert(fileinfo.m_total_images == dec.get_total_images(&basis_file_data[0], (uint32_t)basis_file_data.size()));

	printf("File info:\n");
	printf("  Version: %X\n", fileinfo.m_version);
	printf("  Total header size: %u\n", fileinfo.m_total_header_size);
	printf("  Total selectors: %u\n", fileinfo.m_total_selectors);
	printf("  Selector codebook size: %u\n", fileinfo.m_selector_codebook_size);
	printf("  Total endpoints: %u\n", fileinfo.m_total_endpoints);
	printf("  Endpoint codebook size: %u\n", fileinfo.m_endpoint_codebook_size);
	printf("  Tables size: %u\n", fileinfo.m_tables_size);
	printf("  Slices size: %u\n", fileinfo.m_slices_size);
	fmt_printf("  Block Dimensions: {}x{}\n", fileinfo.m_block_width, fileinfo.m_block_height);

	bool is_hdr = false;

	const char* pFmt_str = nullptr;
	switch (fileinfo.m_tex_format)
	{
	case basist::basis_tex_format::cETC1S:
	{
		pFmt_str = "ETC1S";
		break;
	}
	case basist::basis_tex_format::cUASTC4x4:
	{
		pFmt_str = "UASTC_LDR_4x4";
		break;
	}
	case basist::basis_tex_format::cUASTC_HDR_4x4:
	{
		is_hdr = true;
		pFmt_str = "UASTC_HDR_4x4";
		break;
	}
	case basist::basis_tex_format::cASTC_HDR_6x6:
	{
		is_hdr = true;
		pFmt_str = "ASTC_HDR_6x6";
		break;
	}
	case basist::basis_tex_format::cASTC_HDR_6x6_INTERMEDIATE:
	{
		is_hdr = true;
		pFmt_str = "ASTC_HDR_6x6_INTERMEDIATE";
		break;
	}
	default:
	{
		assert(0);
		return false;
	}
	}

	fmt_printf("  Texture format: {}\n", pFmt_str);

	printf("  Texture type: %s\n", basist::basis_get_texture_type_name(fileinfo.m_tex_type));
	printf("  us per frame: %u (%f fps)\n", fileinfo.m_us_per_frame, fileinfo.m_us_per_frame ? (1.0f / ((float)fileinfo.m_us_per_frame / 1000000.0f)) : 0.0f);
	printf("  Total slices: %u\n", (uint32_t)fileinfo.m_slice_info.size());
	printf("  Total images: %i\n", fileinfo.m_total_images);
	printf("  Y Flipped: %u, Has alpha slices: %u\n", fileinfo.m_y_flipped, fileinfo.m_has_alpha_slices);
	printf("  userdata0: 0x%X userdata1: 0x%X\n", fileinfo.m_userdata0, fileinfo.m_userdata1);
	printf("  Per-image mipmap levels: ");
	for (uint32_t i = 0; i < fileinfo.m_total_images; i++)
		printf("%u ", fileinfo.m_image_mipmap_levels[i]);
	printf("\n");

	uint32_t total_texels = 0;

	printf("\nImage info:\n");
	for (uint32_t i = 0; i < fileinfo.m_total_images; i++)
	{
		basist::basisu_image_info ii;
		if (!dec.get_image_info(&basis_file_data[0], (uint32_t)basis_file_data.size(), ii, i))
		{
			error_printf("get_image_info() failed!\n");
			return false;
		}

		printf("Image %u: MipLevels: %u OrigDim: %ux%u, BlockDim: %ux%u, FirstSlice: %u, HasAlpha: %u\n", i, ii.m_total_levels, ii.m_orig_width, ii.m_orig_height,
			ii.m_num_blocks_x, ii.m_num_blocks_y, ii.m_first_slice_index, (uint32_t)ii.m_alpha_flag);

		total_texels += ii.m_width * ii.m_height;
	}

	printf("\nSlice info:\n");

	for (uint32_t i = 0; i < fileinfo.m_slice_info.size(); i++)
	{
		const basist::basisu_slice_info& sliceinfo = fileinfo.m_slice_info[i];
		printf("%u: OrigWidthHeight: %ux%u, NumBlocks: %ux%u, BlockSize: %ux%u, TotalBlocks: %u, Compressed size: %u, Image: %u, Level: %u, UnpackedCRC16: 0x%X, alpha: %u, iframe: %i\n",
			i,
			sliceinfo.m_orig_width, sliceinfo.m_orig_height,
			sliceinfo.m_num_blocks_x, sliceinfo.m_num_blocks_y,
			sliceinfo.m_block_width, sliceinfo.m_block_height,
			sliceinfo.m_total_blocks,
			sliceinfo.m_compressed_size,
			sliceinfo.m_image_index, sliceinfo.m_level_index,
			sliceinfo.m_unpacked_slice_crc16,
			(uint32_t)sliceinfo.m_alpha_flag,
			(uint32_t)sliceinfo.m_iframe_flag);
	}
	printf("\n");

	size_t comp_size = 0;
	void* pComp_data = tdefl_compress_mem_to_heap(&basis_file_data[0], basis_file_data.size(), &comp_size, TDEFL_MAX_PROBES_MASK);// TDEFL_DEFAULT_MAX_PROBES);
	mz_free(pComp_data);

	const float basis_bits_per_texel = basis_file_data.size() * 8.0f / total_texels;
	const float comp_bits_per_texel = comp_size * 8.0f / total_texels;

	printf("Original size: %u, bits per texel: %3.3f\nCompressed size (Deflate): %u, bits per texel: %3.3f\n", (uint32_t)basis_file_data.size(), basis_bits_per_texel, (uint32_t)comp_size, comp_bits_per_texel);

	if (opts.m_mode == cInfo)
	{
		return true;
	}

	if ((fileinfo.m_etc1s) && (fileinfo.m_selector_codebook_size == 0) && (fileinfo.m_endpoint_codebook_size == 0))
	{
		// File is ETC1S and uses global codebooks - make sure we loaded one
		if (!pGlobal_codebook_data)
		{
			error_printf("ETC1S file uses global codebooks, but none were loaded (see the -use_global_codebooks option)\n");
			return false;
		}

		if ((pGlobal_codebook_data->m_transcoder.get_lowlevel_etc1s_decoder().get_endpoints().size() != fileinfo.m_total_endpoints) ||
			(pGlobal_codebook_data->m_transcoder.get_lowlevel_etc1s_decoder().get_selectors().size() != fileinfo.m_total_selectors))
		{
			error_printf("Supplied global codebook is not compatible with this file\n");
			return false;
		}
	}

	interval_timer tm;
	tm.start();

	if (!dec.start_transcoding(&basis_file_data[0], (uint32_t)basis_file_data.size()))
	{
		error_printf("start_transcoding() failed!\n");
		return false;
	}

	const double start_transcoding_time_ms = tm.get_elapsed_ms();

	printf("start_transcoding time: %3.3f ms\n", start_transcoding_time_ms);

	basisu::vector< gpu_image_vec > gpu_images[(int)basist::transcoder_texture_format::cTFTotalTextureFormats];

	double total_format_transcoding_time_ms[(int)basist::transcoder_texture_format::cTFTotalTextureFormats];
	clear_obj(total_format_transcoding_time_ms);

	int first_format = 0;
	int last_format = (int)basist::transcoder_texture_format::cTFTotalTextureFormats;

	if (opts.m_format_only > -1)
	{
		first_format = opts.m_format_only;
		last_format = first_format + 1;
	}

	if ((pCSV_file) && (file_index == 0))
	{
		std::string desc;
		desc = "filename,basis_bitrate,comp_bitrate,images,levels,slices,start_transcoding_time,";
		for (int format_iter = first_format; format_iter < last_format; format_iter++)
		{
			const basist::transcoder_texture_format transcoder_tex_fmt = static_cast<basist::transcoder_texture_format>(format_iter);

			if (!basis_is_format_supported(transcoder_tex_fmt, fileinfo.m_tex_format))
				continue;
			if (transcoder_tex_fmt == basist::transcoder_texture_format::cTFBC7_ALT)
				continue;

			desc += std::string(basis_get_format_name(transcoder_tex_fmt));
			if (format_iter != last_format - 1)
				desc += ",";
		}
		fprintf(pCSV_file, "%s\n", desc.c_str());
	}

	for (int format_iter = first_format; format_iter < last_format; format_iter++)
	{
		basist::transcoder_texture_format tex_fmt = static_cast<basist::transcoder_texture_format>(format_iter);

		if (basist::basis_transcoder_format_is_uncompressed(tex_fmt))
			continue;

		if (!basis_is_format_supported(tex_fmt, fileinfo.m_tex_format))
			continue;

		if (tex_fmt == basist::transcoder_texture_format::cTFBC7_ALT)
			continue;

		gpu_images[(int)tex_fmt].resize(fileinfo.m_total_images);

		for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index++)
			gpu_images[(int)tex_fmt][image_index].resize(fileinfo.m_image_mipmap_levels[image_index]);
	}

	// Now transcode the file to all supported texture formats and save mipmapped KTX files
	for (int format_iter = first_format; format_iter < last_format; format_iter++)
	{
		const basist::transcoder_texture_format transcoder_tex_fmt = static_cast<basist::transcoder_texture_format>(format_iter);

		if (basist::basis_transcoder_format_is_uncompressed(transcoder_tex_fmt))
			continue;
		if (!basis_is_format_supported(transcoder_tex_fmt, fileinfo.m_tex_format))
			continue;
		if (transcoder_tex_fmt == basist::transcoder_texture_format::cTFBC7_ALT)
			continue;

		for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index++)
		{
			for (uint32_t level_index = 0; level_index < fileinfo.m_image_mipmap_levels[image_index]; level_index++)
			{
				basist::basisu_image_level_info level_info;

				if (!dec.get_image_level_info(&basis_file_data[0], (uint32_t)basis_file_data.size(), level_info, image_index, level_index))
				{
					error_printf("Failed retrieving image level information (%u %u)!\n", image_index, level_index);
					return false;
				}

				if ((transcoder_tex_fmt == basist::transcoder_texture_format::cTFPVRTC1_4_RGB) || (transcoder_tex_fmt == basist::transcoder_texture_format::cTFPVRTC1_4_RGBA))
				{
					if (!is_pow2(level_info.m_width) || !is_pow2(level_info.m_height))
					{
						total_pvrtc_nonpow2_warnings++;

						printf("Warning: Will not transcode image %u level %u res %ux%u to PVRTC1 (one or more dimension is not a power of 2)\n", image_index, level_index, level_info.m_width, level_info.m_height);

						// Can't transcode this image level to PVRTC because it's not a pow2 (we're going to support transcoding non-pow2 to the next larger pow2 soon)
						continue;
					}
				}

				basisu::texture_format tex_fmt = basis_get_basisu_texture_format(transcoder_tex_fmt);

				gpu_image& gi = gpu_images[(int)transcoder_tex_fmt][image_index][level_index];
				gi.init(tex_fmt, level_info.m_orig_width, level_info.m_orig_height);

				// Fill the buffer with psuedo-random bytes, to help more visibly detect cases where the transcoder fails to write to part of the output.
				fill_buffer_with_random_bytes(gi.get_ptr(), gi.get_size_in_bytes());

				uint32_t decode_flags = basist::cDecodeFlagsHighQuality;

				tm.start();

				if (!dec.transcode_image_level(&basis_file_data[0], (uint32_t)basis_file_data.size(), image_index, level_index, gi.get_ptr(), gi.get_total_blocks(), transcoder_tex_fmt, decode_flags))
				{
					error_printf("Failed transcoding image level (%u %u %u)!\n", image_index, level_index, format_iter);
					return false;
				}

				double total_transcode_time = tm.get_elapsed_ms();

				total_format_transcoding_time_ms[format_iter] += total_transcode_time;

				printf("Transcode of image %u level %u res %ux%u format %s succeeded in %3.3f ms\n", image_index, level_index, level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_transcode_time);

			} // format_iter

		} // level_index

	} // image_info

	// Upack UASTC files seperately, to validate we can transcode slices to UASTC and unpack them to pixels.
	// This is a special path because UASTC is not yet a valid transcoder_texture_format, but a lower-level block_format.
	if (fileinfo.m_tex_format == basist::basis_tex_format::cUASTC4x4)
	{
		for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index++)
		{
			for (uint32_t level_index = 0; level_index < fileinfo.m_image_mipmap_levels[image_index]; level_index++)
			{
				basist::basisu_image_level_info level_info;

				if (!dec.get_image_level_info(&basis_file_data[0], (uint32_t)basis_file_data.size(), level_info, image_index, level_index))
				{
					error_printf("Failed retrieving image level information (%u %u)!\n", image_index, level_index);
					return false;
				}

				gpu_image gi;
				gi.init(basisu::texture_format::cUASTC4x4, level_info.m_orig_width, level_info.m_orig_height);

				// Fill the buffer with psuedo-random bytes, to help more visibly detect cases where the transcoder fails to write to part of the output.
				fill_buffer_with_random_bytes(gi.get_ptr(), gi.get_size_in_bytes());

				tm.start();

				if (!dec.transcode_slice(
					&basis_file_data[0], (uint32_t)basis_file_data.size(),
					level_info.m_first_slice_index, gi.get_ptr(), gi.get_total_blocks(), basist::block_format::cUASTC_4x4, gi.get_bytes_per_block()))
				{
					error_printf("Failed transcoding image level (%u %u) to UASTC!\n", image_index, level_index);
					return false;
				}

				double total_transcode_time = tm.get_elapsed_ms();

				printf("Transcode of image %u level %u res %ux%u format UASTC_4x4 succeeded in %3.3f ms\n", image_index, level_index, level_info.m_orig_width, level_info.m_orig_height, total_transcode_time);

				if ((!validate_flag) && (!opts.m_ktx_only))
				{
					image u;
					if (!gi.unpack(u))
					{
						error_printf("Warning: Failed unpacking GPU texture data (%u %u) to UASTC. \n", image_index, level_index);
						return false;
					}
					//u.crop(level_info.m_orig_width, level_info.m_orig_height);

					std::string rgb_filename;
					if (fileinfo.m_image_mipmap_levels[image_index] > 1)
						rgb_filename = base_filename + string_format("_unpacked_rgb_UASTC_4x4_%u_%04u.png", level_index, image_index);
					else
						rgb_filename = base_filename + string_format("_unpacked_rgb_UASTC_4x4_%04u.png", image_index);

					if (!save_png(rgb_filename, u, cImageSaveIgnoreAlpha))
					{
						error_printf("Failed writing to PNG file \"%s\"\n", rgb_filename.c_str());
						delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
						return false;
					}
					printf("Wrote .PNG file \"%s\"\n", rgb_filename.c_str());

					std::string alpha_filename;
					if (fileinfo.m_image_mipmap_levels[image_index] > 1)
						alpha_filename = base_filename + string_format("_unpacked_a_UASTC_4x4_%u_%04u.png", level_index, image_index);
					else
						alpha_filename = base_filename + string_format("_unpacked_a_UASTC_4x4_%04u.png", image_index);
					if (!save_png(alpha_filename, u, cImageSaveGrayscale, 3))
					{
						error_printf("Failed writing to PNG file \"%s\"\n", rgb_filename.c_str());
						delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
						return false;
					}
					printf("Wrote .PNG file \"%s\"\n", alpha_filename.c_str());

				}
			}
		}
	}

	if (!validate_flag)
	{
		// Now write KTX files and unpack them to individual PNG's/EXR's

		for (int format_iter = first_format; format_iter < last_format; format_iter++)
		{
			const basist::transcoder_texture_format transcoder_tex_fmt = static_cast<basist::transcoder_texture_format>(format_iter);

			if (basist::basis_transcoder_format_is_uncompressed(transcoder_tex_fmt))
				continue;
			if (!basis_is_format_supported(transcoder_tex_fmt, fileinfo.m_tex_format))
				continue;
			if (transcoder_tex_fmt == basist::transcoder_texture_format::cTFBC7_ALT)
				continue;

			if ((!opts.m_no_ktx) && (fileinfo.m_tex_type == basist::cBASISTexTypeCubemapArray))
			{
				// No KTX tool that we know of supports cubemap arrays, so write individual cubemap files.
				for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index += 6)
				{
					basisu::vector<gpu_image_vec> cubemap;
					for (uint32_t i = 0; i < 6; i++)
						cubemap.push_back(gpu_images[format_iter][image_index + i]);

					{
						std::string ktx_filename(base_filename + string_format("_transcoded_cubemap_%s_%u.ktx", basist::basis_get_format_name(transcoder_tex_fmt), image_index / 6));
						if (!write_compressed_texture_file(ktx_filename.c_str(), cubemap, true, true))
						{
							error_printf("Failed writing KTX file \"%s\"!\n", ktx_filename.c_str());
							return false;
						}
						printf("Wrote .KTX file \"%s\"\n", ktx_filename.c_str());
					}

					if (does_dds_support_format(cubemap[0][0].get_format()))
					{
						std::string dds_filename(base_filename + string_format("_transcoded_cubemap_%s_%u.dds", basist::basis_get_format_name(transcoder_tex_fmt), image_index / 6));
						if (!write_compressed_texture_file(dds_filename.c_str(), cubemap, true, true))
						{
							error_printf("Failed writing DDS file \"%s\"!\n", dds_filename.c_str());
							return false;
						}
						printf("Wrote .DDS file \"%s\"\n", dds_filename.c_str());
					}
				}
			}

			for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index++)
			{
				gpu_image_vec& gi = gpu_images[format_iter][image_index];

				if (!gi.size())
					continue;

				uint32_t level;
				for (level = 0; level < gi.size(); level++)
					if (!gi[level].get_total_blocks())
						break;

				if (level < gi.size())
					continue;

				if ((!opts.m_no_ktx) && (fileinfo.m_tex_type != basist::cBASISTexTypeCubemapArray))
				{
					{
						std::string ktx_filename(base_filename + string_format("_transcoded_%s_%04u.ktx", basist::basis_get_format_name(transcoder_tex_fmt), image_index));
						if (!write_compressed_texture_file(ktx_filename.c_str(), gi, true))
						{
							error_printf("Failed writing KTX file \"%s\"!\n", ktx_filename.c_str());
							return false;
						}
						printf("Wrote .KTX file \"%s\"\n", ktx_filename.c_str());
					}

					if (does_dds_support_format(gi[0].get_format()))
					{
						std::string dds_filename(base_filename + string_format("_transcoded_%s_%04u.dds", basist::basis_get_format_name(transcoder_tex_fmt), image_index));
						if (!write_compressed_texture_file(dds_filename.c_str(), gi, true))
						{
							error_printf("Failed writing DDS file \"%s\"!\n", dds_filename.c_str());
							return false;
						}
						printf("Wrote .DDS file \"%s\"\n", dds_filename.c_str());
					}
				}

				for (uint32_t level_index = 0; level_index < gi.size(); level_index++)
				{
					basist::basisu_image_level_info level_info;

					if (!dec.get_image_level_info(&basis_file_data[0], (uint32_t)basis_file_data.size(), level_info, image_index, level_index))
					{
						error_printf("Failed retrieving image level information (%u %u)!\n", image_index, level_index);
						return false;
					}

					if (basist::basis_transcoder_format_is_hdr(transcoder_tex_fmt))
					{
						imagef u;

						if (!gi[level_index].unpack_hdr(u))
						{
							printf("Warning: Failed unpacking GPU texture data (%u %u %u). Unpacking as much as possible.\n", format_iter, image_index, level_index);
							total_unpack_warnings++;
						}

						if (!opts.m_ktx_only)
						{
							std::string rgb_filename;
							if (gi.size() > 1)
								rgb_filename = base_filename + string_format("_hdr_unpacked_rgb_%s_%u_%04u.exr", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index);
							else
								rgb_filename = base_filename + string_format("_hdr_unpacked_rgb_%s_%04u.exr", basist::basis_get_format_name(transcoder_tex_fmt), image_index);

							if (!write_exr(rgb_filename.c_str(), u, 3, 0))
							{
								error_printf("Failed writing to EXR file \"%s\"\n", rgb_filename.c_str());
								delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
								return false;
							}
							printf("Wrote .EXR file \"%s\"\n", rgb_filename.c_str());
						}
					}
					else
					{
						image u;
						if (!gi[level_index].unpack(u))
						{
							printf("Warning: Failed unpacking GPU texture data (%u %u %u). Unpacking as much as possible.\n", format_iter, image_index, level_index);
							total_unpack_warnings++;
						}
						//u.crop(level_info.m_orig_width, level_info.m_orig_height);

						bool write_png = true;

						if ((!opts.m_ktx_only) && (write_png))
						{
							std::string rgb_filename;
							if (gi.size() > 1)
								rgb_filename = base_filename + string_format("_unpacked_rgb_%s_%u_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index);
							else
								rgb_filename = base_filename + string_format("_unpacked_rgb_%s_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), image_index);
							if (!save_png(rgb_filename, u, cImageSaveIgnoreAlpha))
							{
								error_printf("Failed writing to PNG file \"%s\"\n", rgb_filename.c_str());
								delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
								return false;
							}
							printf("Wrote .PNG file \"%s\"\n", rgb_filename.c_str());
						}

						if ((transcoder_tex_fmt == basist::transcoder_texture_format::cTFFXT1_RGB) && (opts.m_write_out))
						{
							std::string out_filename;
							if (gi.size() > 1)
								out_filename = base_filename + string_format("_unpacked_rgb_%s_%u_%04u.out", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index);
							else
								out_filename = base_filename + string_format("_unpacked_rgb_%s_%04u.out", basist::basis_get_format_name(transcoder_tex_fmt), image_index);
							if (!write_3dfx_out_file(out_filename.c_str(), gi[level_index]))
							{
								error_printf("Failed writing to OUT file \"%s\"\n", out_filename.c_str());
								return false;
							}
							printf("Wrote .OUT file \"%s\"\n", out_filename.c_str());
						}

						if (basis_transcoder_format_has_alpha(transcoder_tex_fmt) && (!opts.m_ktx_only) && (write_png))
						{
							std::string a_filename;
							if (gi.size() > 1)
								a_filename = base_filename + string_format("_unpacked_a_%s_%u_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index);
							else
								a_filename = base_filename + string_format("_unpacked_a_%s_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), image_index);
							if (!save_png(a_filename, u, cImageSaveGrayscale, 3))
							{
								error_printf("Failed writing to PNG file \"%s\"\n", a_filename.c_str());
								return false;
							}
							printf("Wrote .PNG file \"%s\"\n", a_filename.c_str());

							std::string rgba_filename;
							if (gi.size() > 1)
								rgba_filename = base_filename + string_format("_unpacked_rgba_%s_%u_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index);
							else
								rgba_filename = base_filename + string_format("_unpacked_rgba_%s_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), image_index);
							if (!save_png(rgba_filename, u))
							{
								error_printf("Failed writing to PNG file \"%s\"\n", rgba_filename.c_str());
								return false;
							}
							printf("Wrote .PNG file \"%s\"\n", rgba_filename.c_str());
						}

					} // is_hdr

				} // level_index

			} // image_index

		} // format_iter

	} // if (!validate_flag)

	uint32_t max_mipmap_levels = 0;

	//if (!opts.m_etc1_only)
	if ((opts.m_format_only == -1) && (!validate_flag))
	{
		if (is_hdr)
		{
			// Now unpack to RGBA_HALF using the transcoder itself to do the unpacking to raster images
			for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index++)
			{
				for (uint32_t level_index = 0; level_index < fileinfo.m_image_mipmap_levels[image_index]; level_index++)
				{
					const basist::transcoder_texture_format transcoder_tex_fmt = basist::transcoder_texture_format::cTFRGBA_HALF;

					basist::basisu_image_level_info level_info;

					if (!dec.get_image_level_info(&basis_file_data[0], (uint32_t)basis_file_data.size(), level_info, image_index, level_index))
					{
						error_printf("Failed retrieving image level information (%u %u)!\n", image_index, level_index);
						return false;
					}

					const uint32_t total_pixels = level_info.m_orig_width * level_info.m_orig_height;
					basisu::vector<basist::half_float> half_img(total_pixels * 4);

					fill_buffer_with_random_bytes(&half_img[0], half_img.size_in_bytes());

					tm.start();

					if (!dec.transcode_image_level(&basis_file_data[0], (uint32_t)basis_file_data.size(), image_index, level_index,
						half_img.get_ptr(), total_pixels, transcoder_tex_fmt, 0, level_info.m_orig_width, nullptr, level_info.m_orig_height))
					{
						error_printf("Failed transcoding image level (%u %u %u)!\n", image_index, level_index, transcoder_tex_fmt);
						return false;
					}

					double total_transcode_time = tm.get_elapsed_ms();

					total_format_transcoding_time_ms[(int)transcoder_tex_fmt] += total_transcode_time;

					printf("Transcode of image %u level %u res %ux%u format %s succeeded in %3.3f ms\n", image_index, level_index, level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_transcode_time);

					if ((!validate_flag) && (!opts.m_ktx_only))
					{
						// TODO: HDR alpha support
						imagef float_img(level_info.m_orig_width, level_info.m_orig_height);

						for (uint32_t y = 0; y < level_info.m_orig_height; y++)
							for (uint32_t x = 0; x < level_info.m_orig_width; x++)
								for (uint32_t c = 0; c < 4; c++)
									float_img(x, y)[c] = basist::half_to_float(half_img[(x + y * level_info.m_orig_width) * 4 + c]);

						std::string rgb_filename(base_filename + string_format("_hdr_unpacked_rgba_%s_%u_%04u.exr", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index));
						if (!write_exr(rgb_filename.c_str(), float_img, 3, 0))
						{
							error_printf("Failed writing to EXR file \"%s\"\n", rgb_filename.c_str());
							return false;
						}
						printf("Wrote .EXR file \"%s\"\n", rgb_filename.c_str());
					}

				} // level_index
			} // image_index

			// Now unpack to RGB_HALF using the transcoder itself to do the unpacking to raster images
			for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index++)
			{
				for (uint32_t level_index = 0; level_index < fileinfo.m_image_mipmap_levels[image_index]; level_index++)
				{
					const basist::transcoder_texture_format transcoder_tex_fmt = basist::transcoder_texture_format::cTFRGB_HALF;

					basist::basisu_image_level_info level_info;

					if (!dec.get_image_level_info(&basis_file_data[0], (uint32_t)basis_file_data.size(), level_info, image_index, level_index))
					{
						error_printf("Failed retrieving image level information (%u %u)!\n", image_index, level_index);
						return false;
					}

					const uint32_t total_pixels = level_info.m_orig_width * level_info.m_orig_height;
					basisu::vector<basist::half_float> half_img(total_pixels * 3);

					fill_buffer_with_random_bytes(&half_img[0], half_img.size_in_bytes());

					tm.start();

					if (!dec.transcode_image_level(&basis_file_data[0], (uint32_t)basis_file_data.size(), image_index, level_index,
						half_img.get_ptr(), total_pixels, transcoder_tex_fmt, 0, level_info.m_orig_width, nullptr, level_info.m_orig_height))
					{
						error_printf("Failed transcoding image level (%u %u %u)!\n", image_index, level_index, transcoder_tex_fmt);
						return false;
					}

					double total_transcode_time = tm.get_elapsed_ms();

					total_format_transcoding_time_ms[(int)transcoder_tex_fmt] += total_transcode_time;

					printf("Transcode of image %u level %u res %ux%u format %s succeeded in %3.3f ms\n", image_index, level_index, level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_transcode_time);

					if ((!validate_flag) && (!opts.m_ktx_only))
					{
						// TODO: HDR alpha support
						imagef float_img(level_info.m_orig_width, level_info.m_orig_height);

						for (uint32_t y = 0; y < level_info.m_orig_height; y++)
							for (uint32_t x = 0; x < level_info.m_orig_width; x++)
								for (uint32_t c = 0; c < 3; c++)
									float_img(x, y)[c] = basist::half_to_float(half_img[(x + y * level_info.m_orig_width) * 3 + c]);

						std::string rgb_filename(base_filename + string_format("_hdr_unpacked_rgb_%s_%u_%04u.exr", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index));
						if (!write_exr(rgb_filename.c_str(), float_img, 3, 0))
						{
							error_printf("Failed writing to EXR file \"%s\"\n", rgb_filename.c_str());
							return false;
						}
						printf("Wrote .EXR file \"%s\"\n", rgb_filename.c_str());
					}

				} // level_index
			} // image_index

			// Now unpack to RGB_9E5 using the transcoder itself to do the unpacking to raster images
			for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index++)
			{
				for (uint32_t level_index = 0; level_index < fileinfo.m_image_mipmap_levels[image_index]; level_index++)
				{
					const basist::transcoder_texture_format transcoder_tex_fmt = basist::transcoder_texture_format::cTFRGB_9E5;

					basist::basisu_image_level_info level_info;

					if (!dec.get_image_level_info(&basis_file_data[0], (uint32_t)basis_file_data.size(), level_info, image_index, level_index))
					{
						error_printf("Failed retrieving image level information (%u %u)!\n", image_index, level_index);
						return false;
					}

					const uint32_t total_pixels = level_info.m_orig_width * level_info.m_orig_height;
					basisu::vector<uint32_t> rgb9e5_img(total_pixels);

					fill_buffer_with_random_bytes(&rgb9e5_img[0], rgb9e5_img.size_in_bytes());

					tm.start();

					if (!dec.transcode_image_level(&basis_file_data[0], (uint32_t)basis_file_data.size(), image_index, level_index,
						rgb9e5_img.get_ptr(), total_pixels, transcoder_tex_fmt, 0, level_info.m_orig_width, nullptr, level_info.m_orig_height))
					{
						error_printf("Failed transcoding image level (%u %u %u)!\n", image_index, level_index, transcoder_tex_fmt);
						return false;
					}

					double total_transcode_time = tm.get_elapsed_ms();

					total_format_transcoding_time_ms[(int)transcoder_tex_fmt] += total_transcode_time;

					printf("Transcode of image %u level %u res %ux%u format %s succeeded in %3.3f ms\n", image_index, level_index, level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_transcode_time);

					if ((!validate_flag) && (!opts.m_ktx_only))
					{
						// TODO: Write KTX or DDS
						imagef float_img(level_info.m_orig_width, level_info.m_orig_height);

						for (uint32_t y = 0; y < level_info.m_orig_height; y++)
							for (uint32_t x = 0; x < level_info.m_orig_width; x++)
								astc_helpers::unpack_rgb9e5(rgb9e5_img[x + y * level_info.m_orig_width], float_img(x, y)[0], float_img(x, y)[1], float_img(x, y)[2]);

						std::string rgb_filename(base_filename + string_format("_hdr_unpacked_rgb_%s_%u_%04u.exr", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index));
						if (!write_exr(rgb_filename.c_str(), float_img, 3, 0))
						{
							error_printf("Failed writing to EXR file \"%s\"\n", rgb_filename.c_str());
							return false;
						}
						printf("Wrote .EXR file \"%s\"\n", rgb_filename.c_str());
					}

				} // level_index
			} // image_index


		}
		else
		{
			// Now unpack to RGBA using the transcoder itself to do the unpacking to raster images
			for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index++)
			{
				for (uint32_t level_index = 0; level_index < fileinfo.m_image_mipmap_levels[image_index]; level_index++)
				{
					const basist::transcoder_texture_format transcoder_tex_fmt = basist::transcoder_texture_format::cTFRGBA32;

					basist::basisu_image_level_info level_info;

					if (!dec.get_image_level_info(&basis_file_data[0], (uint32_t)basis_file_data.size(), level_info, image_index, level_index))
					{
						error_printf("Failed retrieving image level information (%u %u)!\n", image_index, level_index);
						return false;
					}

					image img(level_info.m_orig_width, level_info.m_orig_height);

					fill_buffer_with_random_bytes(&img(0, 0), img.get_total_pixels() * sizeof(uint32_t));

					tm.start();

					if (!dec.transcode_image_level(&basis_file_data[0], (uint32_t)basis_file_data.size(), image_index, level_index, &img(0, 0).r, img.get_total_pixels(), transcoder_tex_fmt, 0, img.get_pitch(), nullptr, img.get_height()))
					{
						error_printf("Failed transcoding image level (%u %u %u)!\n", image_index, level_index, transcoder_tex_fmt);
						return false;
					}

					double total_transcode_time = tm.get_elapsed_ms();

					total_format_transcoding_time_ms[(int)transcoder_tex_fmt] += total_transcode_time;

					printf("Transcode of image %u level %u res %ux%u format %s succeeded in %3.3f ms\n", image_index, level_index, level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_transcode_time);

					if ((!validate_flag) && (!opts.m_ktx_only))
					{
						std::string rgb_filename(base_filename + string_format("_unpacked_rgb_%s_%u_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index));
						if (!save_png(rgb_filename, img, cImageSaveIgnoreAlpha))
						{
							error_printf("Failed writing to PNG file \"%s\"\n", rgb_filename.c_str());
							return false;
						}
						printf("Wrote .PNG file \"%s\"\n", rgb_filename.c_str());

						std::string a_filename(base_filename + string_format("_unpacked_a_%s_%u_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index));
						if (!save_png(a_filename, img, cImageSaveGrayscale, 3))
						{
							error_printf("Failed writing to PNG file \"%s\"\n", a_filename.c_str());
							return false;
						}
						printf("Wrote .PNG file \"%s\"\n", a_filename.c_str());
					}

				} // level_index
			} // image_index

			// Now unpack to RGB565 using the transcoder itself to do the unpacking to raster images
			for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index++)
			{
				for (uint32_t level_index = 0; level_index < fileinfo.m_image_mipmap_levels[image_index]; level_index++)
				{
					const basist::transcoder_texture_format transcoder_tex_fmt = basist::transcoder_texture_format::cTFRGB565;

					basist::basisu_image_level_info level_info;

					if (!dec.get_image_level_info(&basis_file_data[0], (uint32_t)basis_file_data.size(), level_info, image_index, level_index))
					{
						error_printf("Failed retrieving image level information (%u %u)!\n", image_index, level_index);
						return false;
					}

					basisu::vector<uint16_t> packed_img(level_info.m_orig_width * level_info.m_orig_height);

					fill_buffer_with_random_bytes(&packed_img[0], packed_img.size() * sizeof(uint16_t));

					tm.start();

					if (!dec.transcode_image_level(&basis_file_data[0], (uint32_t)basis_file_data.size(), image_index, level_index, &packed_img[0], (uint32_t)packed_img.size(), transcoder_tex_fmt, 0, level_info.m_orig_width, nullptr, level_info.m_orig_height))
					{
						error_printf("Failed transcoding image level (%u %u %u)!\n", image_index, level_index, transcoder_tex_fmt);
						return false;
					}

					double total_transcode_time = tm.get_elapsed_ms();

					total_format_transcoding_time_ms[(int)transcoder_tex_fmt] += total_transcode_time;

					image img(level_info.m_orig_width, level_info.m_orig_height);
					for (uint32_t y = 0; y < level_info.m_orig_height; y++)
					{
						for (uint32_t x = 0; x < level_info.m_orig_width; x++)
						{
							const uint16_t p = packed_img[x + y * level_info.m_orig_width];
							uint32_t r = p >> 11, g = (p >> 5) & 63, b = p & 31;
							r = (r << 3) | (r >> 2);
							g = (g << 2) | (g >> 4);
							b = (b << 3) | (b >> 2);
							img(x, y).set(r, g, b, 255);
						}
					}

					printf("Transcode of image %u level %u res %ux%u format %s succeeded in %3.3f ms\n", image_index, level_index, level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_transcode_time);

					if ((!validate_flag) && (!opts.m_ktx_only))
					{
						std::string rgb_filename(base_filename + string_format("_unpacked_rgb_%s_%u_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index));
						if (!save_png(rgb_filename, img, cImageSaveIgnoreAlpha))
						{
							error_printf("Failed writing to PNG file \"%s\"\n", rgb_filename.c_str());
							return false;
						}
						printf("Wrote .PNG file \"%s\"\n", rgb_filename.c_str());
					}

				} // level_index
			} // image_index

			// Now unpack to RGBA4444 using the transcoder itself to do the unpacking to raster images
			for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index++)
			{
				for (uint32_t level_index = 0; level_index < fileinfo.m_image_mipmap_levels[image_index]; level_index++)
				{
					max_mipmap_levels = basisu::maximum(max_mipmap_levels, fileinfo.m_image_mipmap_levels[image_index]);

					const basist::transcoder_texture_format transcoder_tex_fmt = basist::transcoder_texture_format::cTFRGBA4444;

					basist::basisu_image_level_info level_info;

					if (!dec.get_image_level_info(&basis_file_data[0], (uint32_t)basis_file_data.size(), level_info, image_index, level_index))
					{
						error_printf("Failed retrieving image level information (%u %u)!\n", image_index, level_index);
						return false;
					}

					basisu::vector<uint16_t> packed_img(level_info.m_orig_width * level_info.m_orig_height);

					fill_buffer_with_random_bytes(&packed_img[0], packed_img.size() * sizeof(uint16_t));

					tm.start();

					if (!dec.transcode_image_level(&basis_file_data[0], (uint32_t)basis_file_data.size(), image_index, level_index, &packed_img[0], (uint32_t)packed_img.size(), transcoder_tex_fmt, 0, level_info.m_orig_width, nullptr, level_info.m_orig_height))
					{
						error_printf("Failed transcoding image level (%u %u %u)!\n", image_index, level_index, transcoder_tex_fmt);
						return false;
					}

					double total_transcode_time = tm.get_elapsed_ms();

					total_format_transcoding_time_ms[(int)transcoder_tex_fmt] += total_transcode_time;

					image img(level_info.m_orig_width, level_info.m_orig_height);
					for (uint32_t y = 0; y < level_info.m_orig_height; y++)
					{
						for (uint32_t x = 0; x < level_info.m_orig_width; x++)
						{
							const uint16_t p = packed_img[x + y * level_info.m_orig_width];
							uint32_t r = p >> 12, g = (p >> 8) & 15, b = (p >> 4) & 15, a = p & 15;
							r = (r << 4) | r;
							g = (g << 4) | g;
							b = (b << 4) | b;
							a = (a << 4) | a;
							img(x, y).set(r, g, b, a);
						}
					}

					printf("Transcode of image %u level %u res %ux%u format %s succeeded in %3.3f ms\n", image_index, level_index, level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_transcode_time);

					if ((!validate_flag) && (!opts.m_ktx_only))
					{
						std::string rgb_filename(base_filename + string_format("_unpacked_rgb_%s_%u_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index));
						if (!save_png(rgb_filename, img, cImageSaveIgnoreAlpha))
						{
							error_printf("Failed writing to PNG file \"%s\"\n", rgb_filename.c_str());
							return false;
						}
						printf("Wrote .PNG file \"%s\"\n", rgb_filename.c_str());

						std::string a_filename(base_filename + string_format("_unpacked_a_%s_%u_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index));
						if (!save_png(a_filename, img, cImageSaveGrayscale, 3))
						{
							error_printf("Failed writing to PNG file \"%s\"\n", a_filename.c_str());
							return false;
						}
						printf("Wrote .PNG file \"%s\"\n", a_filename.c_str());
					}

				} // level_index
			} // image_index

		} // is_hdr

	} // if ((opts.m_format_only == -1) && (!validate_flag))

	if (pCSV_file)
	{
		fprintf(pCSV_file, "%s, %3.3f, %3.3f, %u, %u, %u, %3.3f, ",
			base_filename.c_str(),
			basis_bits_per_texel,
			comp_bits_per_texel,
			fileinfo.m_total_images,
			max_mipmap_levels,
			(uint32_t)fileinfo.m_slice_info.size(),
			start_transcoding_time_ms);

		for (int format_iter = first_format; format_iter < last_format; format_iter++)
		{
			const basist::transcoder_texture_format transcoder_tex_fmt = static_cast<basist::transcoder_texture_format>(format_iter);

			if (!basis_is_format_supported(transcoder_tex_fmt, fileinfo.m_tex_format))
				continue;
			if (transcoder_tex_fmt == basist::transcoder_texture_format::cTFBC7_ALT)
				continue;

			fprintf(pCSV_file, "%3.3f", total_format_transcoding_time_ms[format_iter]);
			if (format_iter != (last_format - 1))
				fprintf(pCSV_file, ",");
		}
		fprintf(pCSV_file, "\n");
	}

	return true;
}

static bool unpack_and_validate_mode(command_line_params &opts)
{
	interval_timer tm;
	tm.start();

	//const bool validate_flag = (opts.m_mode == cValidate);

	basis_data* pGlobal_codebook_data = nullptr;
	if (opts.m_etc1s_use_global_codebooks_file.size())
	{
		pGlobal_codebook_data = load_basis_file(opts.m_etc1s_use_global_codebooks_file.c_str(), true);
		if (!pGlobal_codebook_data)
		{
			error_printf("Failed loading global codebook data from file \"%s\"\n", opts.m_etc1s_use_global_codebooks_file.c_str());
			return false;
		}
		printf("Loaded global codebooks from file \"%s\"\n", opts.m_etc1s_use_global_codebooks_file.c_str());
	}

	if (!opts.m_input_filenames.size())
	{
		error_printf("No input files to process!\n");
		delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
		return false;
	}

	FILE* pCSV_file = nullptr;
	if ((opts.m_csv_file.size()) && (opts.m_mode == cValidate))
	{
		pCSV_file = fopen_safe(opts.m_csv_file.c_str(), "w");
		if (!pCSV_file)
		{
			error_printf("Failed opening CVS file \"%s\"\n", opts.m_csv_file.c_str());
			delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
			return false;
		}
		//fprintf(pCSV_file, "Filename, Size, Slices, Width, Height, HasAlpha, BitsPerTexel, Slice0RGBAvgPSNR, Slice0RGBAAvgPSNR, Slice0Luma709PSNR, Slice0BestETC1SLuma709PSNR, Q, CL, Time, RGBAvgPSNRMin, RGBAvgPSNRAvg, AAvgPSNRMin, AAvgPSNRAvg, Luma709PSNRMin, Luma709PSNRAvg\n");
	}

	uint32_t total_unpack_warnings = 0;
	uint32_t total_pvrtc_nonpow2_warnings = 0;

	for (uint32_t file_index = 0; file_index < opts.m_input_filenames.size(); file_index++)
	{
		const char* pInput_filename = opts.m_input_filenames[file_index].c_str();

		std::string base_filename;
		string_split_path(pInput_filename, nullptr, nullptr, &base_filename, nullptr);

		uint8_vec file_data;
		if (!basisu::read_file_to_vec(pInput_filename, file_data))
		{
			error_printf("Failed reading file \"%s\"\n", pInput_filename);
			if (pCSV_file) fclose(pCSV_file);
			delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
			return false;
		}

		if (!file_data.size())
		{
			error_printf("File is empty!\n");
			if (pCSV_file) fclose(pCSV_file);
			delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
			return false;
		}

		if (file_data.size() > UINT32_MAX)
		{
			error_printf("File is too large!\n");
			if (pCSV_file) fclose(pCSV_file);
			delete pGlobal_codebook_data; pGlobal_codebook_data = nullptr;
			return false;
		}

		bool is_ktx2 = false;
		if (file_data.size() >= sizeof(basist::g_ktx2_file_identifier))
		{
			is_ktx2 = (memcmp(file_data.data(), basist::g_ktx2_file_identifier, sizeof(basist::g_ktx2_file_identifier)) == 0);
		}

		printf("Input file \"%s\", KTX2: %u\n", pInput_filename, is_ktx2);

		bool status;
		if (is_ktx2)
		{
			status = unpack_and_validate_ktx2_file(
				file_index,
				base_filename,
				file_data,
				opts,
				pCSV_file,
				pGlobal_codebook_data,
				total_unpack_warnings,
				total_pvrtc_nonpow2_warnings);
		}
		else
		{
			status = unpack_and_validate_basis_file(
				file_index,
				base_filename,
				file_data,
				opts,
				pCSV_file,
				pGlobal_codebook_data,
				total_unpack_warnings,
				total_pvrtc_nonpow2_warnings);
		}

		if (!status)
		{
			if (pCSV_file)
				fclose(pCSV_file);

			delete pGlobal_codebook_data;
			pGlobal_codebook_data = nullptr;

			return false;
		}

	} // file_index

	if (total_pvrtc_nonpow2_warnings)
		printf("Warning: %u images could not be transcoded to PVRTC1 because one or both dimensions were not a power of 2\n", total_pvrtc_nonpow2_warnings);

	if (total_unpack_warnings)
		printf("ATTENTION: %u total images had invalid GPU texture data!\n", total_unpack_warnings);
	else
		printf("Success\n");

	debug_printf("Elapsed time: %3.3f secs\n", tm.get_elapsed_secs());

	if (pCSV_file)
	{
		fclose(pCSV_file);
		pCSV_file = nullptr;
	}
	delete pGlobal_codebook_data;
	pGlobal_codebook_data = nullptr;

	return true;
}

static bool hdr_compare_mode(command_line_params& opts)
{
	if (opts.m_input_filenames.size() != 2)
	{
		error_printf("Must specify two PNG filenames using -file\n");
		return false;
	}

	imagef a, b;

	if (!load_image_hdr(opts.m_input_filenames[0].c_str(), a))
	{
		error_printf("Failed loading image from file \"%s\"!\n", opts.m_input_filenames[0].c_str());
		return false;
	}

	printf("Loaded \"%s\", %ux%u\n", opts.m_input_filenames[0].c_str(), a.get_width(), a.get_height());

	if (!load_image_hdr(opts.m_input_filenames[1].c_str(), b))
	{
		error_printf("Failed loading image from file \"%s\"!\n", opts.m_input_filenames[1].c_str());
		return false;
	}

	printf("Loaded \"%s\", %ux%u\n", opts.m_input_filenames[1].c_str(), b.get_width(), b.get_height());

	if ((a.get_width() != b.get_width()) || (a.get_height() != b.get_height()))
	{
		printf("Images don't have the same dimensions - cropping input images to smallest common dimensions\n");

		uint32_t w = minimum(a.get_width(), b.get_width());
		uint32_t h = minimum(a.get_height(), b.get_height());

		a.crop(w, h);
		b.crop(w, h);
	}

	printf("Comparison image res: %ux%u\n", a.get_width(), a.get_height());

	image_metrics im;

	im.calc_half(a, b, 0, 1, true);
	im.print("R      ");

	im.calc_half(a, b, 1, 1, true);
	im.print("G      ");

	im.calc_half(a, b, 2, 1, true);
	im.print("B      ");

	im.calc_half(a, b, 0, 3, true);
	im.print("RGB    ");

	return true;
}

static bool compare_mode(command_line_params &opts)
{
	if (opts.m_input_filenames.size() != 2)
	{
		error_printf("Must specify two PNG filenames using -file\n");
		return false;
	}

	std::string ext0(string_get_extension(opts.m_input_filenames[0]));
	if ((strcasecmp(ext0.c_str(), "exr") == 0) || (strcasecmp(ext0.c_str(), "hdr") == 0))
	{
		error_printf("Can't compare HDR image files with this option. Use -hdr_compare instead.\n");
		return false;
	}

	std::string ext1(string_get_extension(opts.m_input_filenames[1]));
	if ((strcasecmp(ext1.c_str(), "exr") == 0) || (strcasecmp(ext1.c_str(), "hdr") == 0))
	{
		error_printf("Can't compare HDR image files with this option. Use -hdr_compare instead.\n");
		return false;
	}

	image a, b;
	if (!load_image(opts.m_input_filenames[0].c_str(), a))
	{
		error_printf("Failed loading image from file \"%s\"!\n", opts.m_input_filenames[0].c_str());
		return false;
	}

	printf("Loaded \"%s\", %ux%u, has alpha: %u\n", opts.m_input_filenames[0].c_str(), a.get_width(), a.get_height(), a.has_alpha());

	if (!load_image(opts.m_input_filenames[1].c_str(), b))
	{
		error_printf("Failed loading image from file \"%s\"!\n", opts.m_input_filenames[1].c_str());
		return false;
	}

	printf("Loaded \"%s\", %ux%u, has alpha: %u\n", opts.m_input_filenames[1].c_str(), b.get_width(), b.get_height(), b.has_alpha());

	if ((a.get_width() != b.get_width()) || (a.get_height() != b.get_height()))
	{
		printf("Images don't have the same dimensions - cropping input images to smallest common dimensions\n");

		uint32_t w = minimum(a.get_width(), b.get_width());
		uint32_t h = minimum(a.get_height(), b.get_height());

		a.crop(w, h);
		b.crop(w, h);
	}

	printf("Comparison image res: %ux%u\n", a.get_width(), a.get_height());

	image_metrics im;
	im.calc(a, b, 0, 3);
	im.print("RGB    ");

	im.calc(a, b, 0, 4);
	im.print("RGBA   ");

	im.calc(a, b, 0, 1);
	im.print("R      ");

	im.calc(a, b, 1, 1);
	im.print("G      ");

	im.calc(a, b, 2, 1);
	im.print("B      ");

	im.calc(a, b, 3, 1);
	im.print("A      ");

	im.calc(a, b, 0, 0);
	im.print("Y 709  " );

	im.calc(a, b, 0, 0, true, true);
	im.print("Y 601  " );

	if (opts.m_compare_ssim)
	{
		vec4F s_rgb(compute_ssim(a, b, false, false));

		printf("R SSIM: %f\n", s_rgb[0]);
		printf("G SSIM: %f\n", s_rgb[1]);
		printf("B SSIM: %f\n", s_rgb[2]);
		printf("RGB Avg SSIM: %f\n", (s_rgb[0] + s_rgb[1] + s_rgb[2]) / 3.0f);
		printf("A SSIM: %f\n", s_rgb[3]);

		vec4F s_y_709(compute_ssim(a, b, true, false));
		printf("Y 709 SSIM: %f\n", s_y_709[0]);

		vec4F s_y_601(compute_ssim(a, b, true, true));
		printf("Y 601 SSIM: %f\n", s_y_601[0]);
	}

	image delta_img(a.get_width(), a.get_height());

	const int X = 2;

	for (uint32_t y = 0; y < a.get_height(); y++)
	{
		for (uint32_t x = 0; x < a.get_width(); x++)
		{
			color_rgba &d = delta_img(x, y);

			for (int c = 0; c < 4; c++)
				d[c] = (uint8_t)clamp<int>((a(x, y)[c] - b(x, y)[c]) * X + 128, 0, 255);
		} // x
	} // y

	save_png("a_rgb.png", a, cImageSaveIgnoreAlpha);
	save_png("a_alpha.png", a, cImageSaveGrayscale, 3);
	printf("Wrote a_rgb.png and a_alpha.png\n");

	save_png("b_rgb.png", b, cImageSaveIgnoreAlpha);
	save_png("b_alpha.png", b, cImageSaveGrayscale, 3);
	printf("Wrote b_rgb.png and b_alpha.png\n");

	save_png("delta_img_rgb.png", delta_img, cImageSaveIgnoreAlpha);
	printf("Wrote delta_img_rgb.png\n");

	save_png("delta_img_a.png", delta_img, cImageSaveGrayscale, 3);
	printf("Wrote delta_img_a.png\n");

	if (opts.m_compare_plot)
	{
		uint32_t bins[5][512];
		clear_obj(bins);

		running_stat delta_stats[5];
		basisu::rand rm;

		double avg[5];
		clear_obj(avg);

		for (uint32_t y = 0; y < a.get_height(); y++)
		{
			for (uint32_t x = 0; x < a.get_width(); x++)
			{
				//color_rgba& d = delta_img(x, y);

				for (int c = 0; c < 4; c++)
				{
					int delta = a(x, y)[c] - b(x, y)[c];

					//delta = clamp<int>((int)std::round(rm.gaussian(70.0f, 10.0f)), -255, 255);

					bins[c][delta + 256]++;
					delta_stats[c].push(delta);

					avg[c] += delta;
				}

				int y_delta = a(x, y).get_709_luma() - b(x, y).get_709_luma();
				bins[4][y_delta + 256]++;
				delta_stats[4].push(y_delta);

				avg[4] += y_delta;

			} // x
		} // y

		for (uint32_t i = 0; i <= 4; i++)
			avg[i] /= a.get_total_pixels();

		printf("\n");

		//bins[2][256+-255] = 100000;
		//bins[2][256-56] = 50000;

		const uint32_t X_SIZE = 128, Y_SIZE = 40;

		for (uint32_t c = 0; c <= 4; c++)
		{
			std::vector<uint8_t> plot[Y_SIZE + 1];
			for (uint32_t i = 0; i < Y_SIZE; i++)
			{
				plot[i].resize(X_SIZE + 2);
				memset(plot[i].data(), ' ', X_SIZE + 1);
			}

			uint32_t max_val = 0;
			int max_val_bin_index = 0;
			int lowest_bin_index = INT_MAX, highest_bin_index = INT_MIN;
			double avg_val = 0;
			double total_val = 0;
			running_stat bin_stats;

			for (int y = -255; y <= 255; y++)
			{
				uint32_t val = bins[c][256 + y];
				if (!val)
					continue;

				bin_stats.push(y);

				total_val += (double)val;

				lowest_bin_index = minimum(lowest_bin_index, y);
				highest_bin_index = maximum(highest_bin_index, y);

				if (val > max_val)
				{
					max_val = val;
					max_val_bin_index = y;
				}
				avg_val += y * (double)val;
			}
			avg_val /= total_val;

			int lo_limit = -(int)X_SIZE / 2;
			int hi_limit = X_SIZE / 2;
			for (int x = lo_limit; x <= hi_limit; x++)
			{
				uint32_t total = 0;
				if (x == lo_limit)
				{
					for (int i = -255; i <= lo_limit; i++)
						total += bins[c][256 + i];
				}
				else if (x == hi_limit)
				{
					for (int i = hi_limit; i <= 255; i++)
						total += bins[c][256 + i];
				}
				else
				{
					total = bins[c][256 + x];
				}

				uint32_t height = max_val ? (total * Y_SIZE + max_val - 1) / max_val : 0;

				if (height)
				{
					for (uint32_t y = (Y_SIZE - 1) - (height - 1); y <= (Y_SIZE - 1); y++)
						plot[y][x + X_SIZE / 2] = '*';
				}
			}

			printf("%c delta histogram: total samples: %5.0f, max bin value: %u index: %i (%3.3f%% of total), range %i [%i,%i], weighted mean: %f\n", "RGBAY"[c], total_val, max_val, max_val_bin_index, max_val * 100.0f / total_val, highest_bin_index - lowest_bin_index + 1, lowest_bin_index, highest_bin_index, avg_val);
			printf("bin mean: %f, bin std deviation: %f, non-zero bins: %u\n", bin_stats.get_mean(), bin_stats.get_std_dev(), bin_stats.get_num());
			printf("delta mean: %f, delta std deviation: %f\n", delta_stats[c].get_mean(), delta_stats[c].get_std_dev());
			printf("\n");

			for (uint32_t y = 0; y < Y_SIZE; y++)
				printf("%s\n", (char*)plot[y].data());

			char tics[1024];
			tics[0] = '\0';
			char tics2[1024];
			tics2[0] = '\0';

			for (int x = 0; x <= (int)X_SIZE; x++)
			{
				char buf[64];
				if (x == X_SIZE / 2)
				{
					while ((int)strlen(tics) < x)
						strcat(tics, ".");

					while ((int)strlen(tics2) < x)
						strcat(tics2, " ");

					sprintf(buf, "0");
					strcat(tics, buf);
				}
				else if (((x & 7) == 0) || (x == X_SIZE))
				{
					while ((int)strlen(tics) < x)
						strcat(tics, ".");

					while ((int)strlen(tics2) < x)
						strcat(tics2, " ");

					int v = (x - (int)X_SIZE / 2);
					sprintf(buf, "%i", v / 10);
					strcat(tics, buf);

					if (v < 0)
					{
						if (-v < 10)
							sprintf(buf, "%i", v % 10);
						else
							sprintf(buf, " %i", -v % 10);
					}
					else
						sprintf(buf, "%i", v % 10);
					strcat(tics2, buf);
				}
				else
				{
					while ((int)strlen(tics) < x)
						strcat(tics, ".");
				}
			}
			printf("%s\n", tics);
			printf("%s\n", tics2);

			printf("\n");
		}

	} // display_plot

	return true;
}

static bool split_image_mode(command_line_params& opts)
{
	if (opts.m_input_filenames.size() != 1)
	{
		error_printf("Must specify one image filename using -file\n");
		return false;
	}

	image a;
	if (!load_image(opts.m_input_filenames[0].c_str(), a))
	{
		error_printf("Failed loading image from file \"%s\"!\n", opts.m_input_filenames[0].c_str());
		return false;
	}

	printf("Loaded \"%s\", %ux%u, has alpha: %u\n", opts.m_input_filenames[0].c_str(), a.get_width(), a.get_height(), a.has_alpha());

	if (!save_png("split_rgb.png", a, cImageSaveIgnoreAlpha))
	{
		fprintf(stderr, "Failed writing file split_rgb.png\n");
		return false;
	}
	printf("Wrote file split_rgb.png\n");

	for (uint32_t i = 0; i < 4; i++)
	{
		char buf[256];
		snprintf(buf, sizeof(buf), "split_%c.png", "RGBA"[i]);
		if (!save_png(buf, a, cImageSaveGrayscale, i))
		{
			fprintf(stderr, "Failed writing file %s\n", buf);
			return false;
		}
		printf("Wrote file %s\n", buf);
	}

	return true;
}

static bool combine_images_mode(command_line_params& opts)
{
	if (opts.m_input_filenames.size() != 2)
	{
		error_printf("Must specify two image filename using -file\n");
		return false;
	}

	image a, b;
	if (!load_image(opts.m_input_filenames[0].c_str(), a))
	{
		error_printf("Failed loading image from file \"%s\"!\n", opts.m_input_filenames[0].c_str());
		return false;
	}

	printf("Loaded \"%s\", %ux%u, has alpha: %u\n", opts.m_input_filenames[0].c_str(), a.get_width(), a.get_height(), a.has_alpha());

	if (!load_image(opts.m_input_filenames[1].c_str(), b))
	{
		error_printf("Failed loading image from file \"%s\"!\n", opts.m_input_filenames[1].c_str());
		return false;
	}

	printf("Loaded \"%s\", %ux%u, has alpha: %u\n", opts.m_input_filenames[1].c_str(), b.get_width(), b.get_height(), b.has_alpha());

	const uint32_t width = minimum(a.get_width(), b.get_width());
	const uint32_t height = minimum(b.get_height(), b.get_height());

	image combined_img(width, height);
	for (uint32_t y = 0; y < height; y++)
	{
		for (uint32_t x = 0; x < width; x++)
		{
			combined_img(x, y) = a(x, y);
			combined_img(x, y).a = b(x, y).g;
		}
	}

	const char* pOutput_filename = "combined.png";
	if (opts.m_output_filename.size())
		pOutput_filename = opts.m_output_filename.c_str();

	if (!save_png(pOutput_filename, combined_img))
	{
		fprintf(stderr, "Failed writing file %s\n", pOutput_filename);
		return false;
	}
	printf("Wrote file %s\n", pOutput_filename);

	return true;
}

static bool tonemap_image_mode(command_line_params& opts)
{
	if (opts.m_input_filenames.size() != 1)
	{
		error_printf("Must specify one LDR image filename using -file\n");
		return false;
	}

	imagef hdr_img;
	if (!load_image_hdr(opts.m_input_filenames[0].c_str(), hdr_img, opts.m_comp_params.m_ldr_hdr_upconversion_srgb_to_linear))
	{
		error_printf("Failed loading LDR image from file \"%s\"!\n", opts.m_input_filenames[0].c_str());
		return false;
	}

	hdr_img.clean_astc_hdr_pixels(1e+30f);

	const uint32_t width = hdr_img.get_width(), height = hdr_img.get_height();

	printf("Loaded \"%s\", %ux%u\n", opts.m_input_filenames[0].c_str(), width, height);

	std::string output_filename;
	string_get_filename(opts.m_input_filenames[0].c_str(), output_filename);
	string_remove_extension(output_filename);
	if (!output_filename.size())
		output_filename = "tonemapped";

	if (opts.m_output_path.size())
		string_combine_path(output_filename, opts.m_output_path.c_str(), output_filename.c_str());

	const char* pBasename = output_filename.c_str();

	image srgb_img(width, height);
	image lin_img(width, height);

	for (uint32_t y = 0; y < height; y++)
	{
		for (uint32_t x = 0; x < width; x++)
		{
			vec4F p(hdr_img(x, y));

			p[0] = clamp(p[0], 0.0f, 1.0f);
			p[1] = clamp(p[1], 0.0f, 1.0f);
			p[2] = clamp(p[2], 0.0f, 1.0f);

			{
				int rc = (int)std::round(linear_to_srgb(p[0]) * 255.0f);
				int gc = (int)std::round(linear_to_srgb(p[1]) * 255.0f);
				int bc = (int)std::round(linear_to_srgb(p[2]) * 255.0f);

				srgb_img.set_clipped(x, y, color_rgba(rc, gc, bc, 255));
			}

			{
				int rc = (int)std::round(p[0] * 255.0f);
				int gc = (int)std::round(p[1] * 255.0f);
				int bc = (int)std::round(p[2] * 255.0f);

				lin_img.set_clipped(x, y, color_rgba(rc, gc, bc, 255));
			}
		}
	}

	{
		const std::string filename(string_format("%s_linear_clamped_to_srgb.png", pBasename));
		save_png(filename.c_str(), srgb_img);
		printf("Wrote .PNG file %s\n", filename.c_str());
	}

	{
		const std::string filename(string_format("%s_linear_clamped.png", pBasename));
		save_png(filename.c_str(), lin_img);
		printf("Wrote .PNG file %s\n", filename.c_str());
	}

	{
		const std::string filename(string_format("%s_compressive_tonemapped.png", pBasename));
		image compressive_tonemapped_img;

		bool status = tonemap_image_compressive(compressive_tonemapped_img, hdr_img);
		if (!status)
		{
			error_printf("tonemap_image_compressive() failed (invalid half-float input)\n");
		}
		else
		{
			save_png(filename.c_str(), compressive_tonemapped_img);
			printf("Wrote .PNG file %s\n", filename.c_str());
		}
	}

	image tonemapped_img;

	for (int e = -6; e <= 6; e++)
	{
		const float scale = powf(2.0f, (float)e);

		tonemap_image_reinhard(tonemapped_img, hdr_img, scale, opts.m_tonemap_dither_flag);

		std::string filename(string_format("%s_reinhard_tonemapped_scale_%f.png", pBasename, scale));
		save_png(filename.c_str(), tonemapped_img, cImageSaveIgnoreAlpha);
		printf("Wrote .PNG file %s\n", filename.c_str());
	}

	return true;
}

static bool bench_mode(command_line_params& opts)
{
	BASISU_NOTE_UNUSED(opts);
	error_printf("Unsupported\n");
	return false;
}

static uint32_t compute_miniz_compressed_size(const char* pFilename, uint32_t &orig_size)
{
	orig_size = 0;

	uint8_vec buf;
	if (!read_file_to_vec(pFilename, buf))
		return 0;

	if (!buf.size())
		return 0;

	if (buf.size() > UINT32_MAX)
	{
		fprintf(stderr, "compute_miniz_compressed_size: File \"%s\" too large!\n", pFilename);
		return 0;
	}

	orig_size = buf.size_u32();

	size_t comp_size = 0;
	void* pComp_data = tdefl_compress_mem_to_heap(&buf[0], buf.size(), &comp_size, TDEFL_MAX_PROBES_MASK);// TDEFL_DEFAULT_MAX_PROBES);

	mz_free(pComp_data);

	return (uint32_t)comp_size;
}

static bool compsize_mode(command_line_params& opts)
{
	if (opts.m_input_filenames.size() != 1)
	{
		error_printf("Must specify a filename using -file\n");
		return false;
	}

	uint32_t orig_size;
	uint32_t comp_size = compute_miniz_compressed_size(opts.m_input_filenames[0].c_str(), orig_size);
	printf("Original file size: %u bytes\n", orig_size);
	printf("miniz compressed size: %u bytes\n", comp_size);

	return true;
}

const struct test_file
{
	const char* m_pFilename;
	uint32_t m_etc1s_size;
	float m_etc1s_psnr;
	float m_uastc_psnr;

	uint32_t m_etc1s_128_size;
    float m_etc1s_128_psnr;
} g_test_files[] =
{
	{ "black_1x1.png", 189, 100.0f, 100.0f, 189, 100.0f },
	{ "kodim01.png", 30993, 27.40f, 44.14f, 58354, 30.356064f },
	{ "kodim02.png", 28529, 32.20f, 41.06f, 51411, 34.713940f },
	{ "kodim03.png", 23411, 32.57f, 44.87f, 49282, 36.709675f },
	{ "kodim04.png", 28256, 31.76f, 43.02f, 57003, 34.864861f },
	{ "kodim05.png", 32646, 25.94f, 40.28f, 65731, 29.935091f },
	{ "kodim06.png", 27336, 28.66f, 44.57f, 54963, 32.294220f },
	{ "kodim07.png", 26618, 31.51f, 43.94f, 53352, 35.576595f },
	{ "kodim08.png", 31133, 25.28f, 41.15f, 63347, 29.509914f },
	{ "kodim09.png", 24777, 32.05f, 45.85f, 51355, 35.985966f },
	{ "kodim10.png", 27247, 32.20f, 45.77f, 54291, 36.395000f },
	{ "kodim11.png", 26579, 29.22f, 43.68f, 55491, 33.468971f },
	{ "kodim12.png", 25102, 32.96f, 46.77f, 51465, 36.722233f },
	{ "kodim13.png", 31604, 24.25f, 41.25f, 62629, 27.588623f },
	{ "kodim14.png", 31162, 27.81f, 39.65f, 62866, 31.206463f },
	{ "kodim15.png", 25528, 31.26f, 42.87f, 53343, 35.026314f },
	{ "kodim16.png", 26894, 32.21f, 47.78f, 51325, 35.555458f },
	{ "kodim17.png", 29334, 31.40f, 45.66f, 55630, 35.909283f },
	{ "kodim18.png", 30929, 27.46f, 41.54f, 62421, 31.348171f },
	{ "kodim19.png", 27889, 29.69f, 44.95f, 55055, 33.613987f },
	{ "kodim20.png", 21104, 31.30f, 45.31f, 47136, 35.759407f },
	{ "kodim21.png", 25943, 28.53f, 44.45f, 54768, 32.415817f },
	{ "kodim22.png", 29277, 29.85f, 42.63f, 60889, 33.495415f },
	{ "kodim23.png", 23550, 31.69f, 45.11f, 53774, 36.223492f },
	{ "kodim24.png", 29613, 26.75f, 40.61f, 59014, 31.522869f },
	{ "white_1x1.png", 189, 100.0f, 100.0f, 189, 100.000000f },
	{ "wikipedia.png", 38961, 24.10f, 30.47f, 69558, 27.630802f },
	{ "alpha0.png", 766, 100.0f, 56.16f, 747, 100.000000f }
};
const uint32_t TOTAL_TEST_FILES = sizeof(g_test_files) / sizeof(g_test_files[0]);

static bool test_mode_ldr(command_line_params& opts)
{
	uint32_t total_mismatches = 0;

	// TODO: Record min/max/avgs
	// TODO: Add another ETC1S quality level

	// Minor differences in how floating point code is optimized can result in slightly different generated files.
#ifdef USE_TIGHTER_TEST_TOLERANCES
	const float ETC1S_PSNR_THRESHOLD = .125f;
	const float UASTC_PSNR_THRESHOLD = .125f;
#else
	const float ETC1S_PSNR_THRESHOLD = .3f;
	const float UASTC_PSNR_THRESHOLD = .3f;
#endif
	const float ETC1S_FILESIZE_THRESHOLD = .045f;

	for (uint32_t i = 0; i < TOTAL_TEST_FILES; i++)
	{
		std::string filename(opts.m_test_file_dir);
		if (filename.size())
		{
			filename.push_back('/');
		}
		filename += std::string(g_test_files[i].m_pFilename);

		basisu::vector<image> source_images(1);

		image& source_image = source_images[0];
		if (!load_png(filename.c_str(), source_image))
		{
			error_printf("Failed loading test image \"%s\"\n", filename.c_str());
			return false;
		}

		printf("Loaded file \"%s\", dimemsions %ux%u has alpha: %u\n", filename.c_str(), source_image.get_width(), source_image.get_height(), source_image.has_alpha());

		image_stats stats;

		uint32_t flags_and_quality;
		float uastc_rdo_quality = 0.0f;
		size_t data_size = 0;

		// Test ETC1S
		flags_and_quality = (opts.m_comp_params.m_multithreading ? cFlagThreaded : 0) | cFlagPrintStats | cFlagPrintStatus;

		{
			printf("**** Testing ETC1S non-OpenCL level 1\n");

			// Level 1
			void* pData = basis_compress(basist::basis_tex_format::cETC1S, source_images, flags_and_quality, uastc_rdo_quality, &data_size, &stats);
			if (!pData)
			{
				error_printf("basis_compress() failed!\n");
				return false;
			}
			basis_free_data(pData);

			printf("ETC1S level 1 Size: %u, PSNR: %f\n", (uint32_t)data_size, stats.m_basis_rgba_avg_psnr);

			float file_size_ratio = fabs((data_size / (float)g_test_files[i].m_etc1s_size) - 1.0f);
			if (file_size_ratio > ETC1S_FILESIZE_THRESHOLD)
			{
				error_printf("Expected ETC1S file size was %u, but got %u instead!\n", g_test_files[i].m_etc1s_size, (uint32_t)data_size);
				total_mismatches++;
			}

			if (fabs(stats.m_basis_rgba_avg_psnr - g_test_files[i].m_etc1s_psnr) > ETC1S_PSNR_THRESHOLD)
			{
				error_printf("Expected ETC1S RGBA Avg PSNR was %f, but got %f instead!\n", g_test_files[i].m_etc1s_psnr, stats.m_basis_rgba_avg_psnr);
				total_mismatches++;
			}
		}

		{
			printf("**** Testing ETC1S non-OpenCL level 128\n");

			// Test ETC1S level 128
			flags_and_quality |= 128;

			void* pData = basis_compress(basist::basis_tex_format::cETC1S, source_images, flags_and_quality, uastc_rdo_quality, &data_size, &stats);
			if (!pData)
			{
				error_printf("basis_compress() failed!\n");
				return false;
			}
			basis_free_data(pData);

			printf("ETC1S level 128 Size: %u, PSNR: %f\n", (uint32_t)data_size, stats.m_basis_rgba_avg_psnr);

			float file_size_ratio = fabs((data_size / (float)g_test_files[i].m_etc1s_128_size) - 1.0f);
			if (file_size_ratio > ETC1S_FILESIZE_THRESHOLD)
			{
				error_printf("Expected ETC1S file size was %u, but got %u instead!\n", g_test_files[i].m_etc1s_128_size, (uint32_t)data_size);
				total_mismatches++;
			}

			if (fabs(stats.m_basis_rgba_avg_psnr - g_test_files[i].m_etc1s_128_psnr) > ETC1S_PSNR_THRESHOLD)
			{
				error_printf("Expected ETC1S RGBA Avg PSNR was %f, but got %f instead!\n", g_test_files[i].m_etc1s_128_psnr, stats.m_basis_rgba_avg_psnr);
				total_mismatches++;
			}
		}

		if (opencl_is_available())
		{
			printf("**** Testing ETC1S OpenCL level 1\n");

			// Test ETC1S OpenCL level 1
			flags_and_quality = (opts.m_comp_params.m_multithreading ? cFlagThreaded : 0) | cFlagUseOpenCL | cFlagPrintStats | cFlagPrintStatus;

			void *pData = basis_compress(basist::basis_tex_format::cETC1S, source_images, flags_and_quality, uastc_rdo_quality, &data_size, &stats);
			if (!pData)
			{
				error_printf("basis_compress() failed!\n");
				return false;
			}
			basis_free_data(pData);

			printf("ETC1S+OpenCL Size: %u, PSNR: %f\n", (uint32_t)data_size, stats.m_basis_rgba_avg_psnr);

			float file_size_ratio = fabs((data_size / (float)g_test_files[i].m_etc1s_size) - 1.0f);
			if (file_size_ratio > .04f)
			{
				error_printf("Expected ETC1S+OpenCL file size was %u, but got %u instead!\n", g_test_files[i].m_etc1s_size, (uint32_t)data_size);
				total_mismatches++;
			}

			if (g_test_files[i].m_etc1s_psnr == 100.0f)
			{
				// TODO
				if (stats.m_basis_rgba_avg_psnr < 69.0f)
				{
					error_printf("Expected ETC1S+OpenCL RGBA Avg PSNR was %f, but got %f instead!\n", g_test_files[i].m_etc1s_psnr, stats.m_basis_rgba_avg_psnr);
					total_mismatches++;
				}
			}
			else if (fabs(stats.m_basis_rgba_avg_psnr - g_test_files[i].m_etc1s_psnr) > .2f)
			{
				error_printf("Expected ETC1S+OpenCL RGBA Avg PSNR was %f, but got %f instead!\n", g_test_files[i].m_etc1s_psnr, stats.m_basis_rgba_avg_psnr);
				total_mismatches++;
			}
		}

		// Test UASTC
		{
			printf("**** Testing UASTC\n");

			flags_and_quality = (opts.m_comp_params.m_multithreading ? cFlagThreaded : 0) | cFlagPrintStats | cFlagPrintStatus;

			void* pData = basis_compress(basist::basis_tex_format::cUASTC4x4, source_images, flags_and_quality, uastc_rdo_quality, &data_size, &stats);
			if (!pData)
			{
				error_printf("basis_compress() failed!\n");
				return false;
			}
			basis_free_data(pData);

			printf("UASTC Size: %u, PSNR: %f\n", (uint32_t)data_size, stats.m_basis_rgba_avg_psnr);

			if (fabs(stats.m_basis_rgba_avg_psnr - g_test_files[i].m_uastc_psnr) > UASTC_PSNR_THRESHOLD)
			{
				error_printf("Expected UASTC RGBA Avg PSNR was %f, but got %f instead!\n", g_test_files[i].m_etc1s_psnr, stats.m_basis_rgba_avg_psnr);
				total_mismatches++;
			}
		}
	}

	printf("Total LDR mismatches: %u\n", total_mismatches);

	bool result = true;
	if (total_mismatches)
	{
		error_printf("LDR test FAILED\n");
		result = false;
	}
	else
	{
		printf("LDR test succeeded\n");
	}

	return result;
}

const uint32_t MAX_ASTC_HDR_4x4_TEST_LEVEL = 4;

struct hdr_test_file
{
	const char* m_pFilename;
	float m_level_psnr_astc[MAX_ASTC_HDR_4x4_TEST_LEVEL + 1];
	float m_level_psnr_bc6h[MAX_ASTC_HDR_4x4_TEST_LEVEL + 1];
};

static const hdr_test_file g_hdr_4x4_test_files[] =
{
	{ "black_1x1.png", { 1000.000000f,1000.000000f,1000.000000f,1000.000000f,1000.000000f }, { 1000.000000f,1000.000000f,1000.000000f,1000.000000f,1000.000000f } },
	{ "atrium.exr", { 38.630527f,39.037231f,39.561947f,39.604759f,40.181847f }, { 38.218285f,38.801189f,39.232151f,39.271103f,39.689102f } },
	{ "backyard.exr", { 39.930801f,39.894077f,40.001156f,40.020653f,40.233330f }, { 39.125782f,39.504299f,39.602329f,39.621807f,39.804798f } },
	{ "Desk.exr", { 23.786697f,24.840689f,25.399199f,25.476711f,26.183117f }, { 23.523026f,24.634579f,25.172062f,25.242109f,25.930155f } },
	{ "atrium.exr", { 38.630527f,39.037231f,39.561947f,39.604759f,40.181847f }, { 38.218285f,38.801189f,39.232151f,39.271103f,39.689102f } },
	{ "yucca.exr", { 33.830448f,34.716824f,34.941631f,35.032707f,35.377048f }, { 33.530876f,34.388000f,34.614750f,34.706139f,35.021336f } },
	{ "tough.png", { 30.077433f,32.829239f,33.760094f,35.076836f,38.015430f }, { 30.042871f,32.868286f,33.872608f,34.709766f,37.002869f } },
	{ "kodim03.png", { 44.012009f,44.699100f,44.914505f,45.099625f,45.585442f }, { 43.358746f,44.380592f,44.552963f,44.728668f,45.161995f } },
	{ "kodim18.png", { 40.636051f,40.661617f,40.807407f,40.855389f,41.059860f }, { 40.235321f,40.500309f,40.628899f,40.666466f,40.814095f } },
	{ "kodim23.png", { 43.154652f,43.808632f,44.074600f,44.188736f,44.576088f }, { 42.515514f,43.478119f,43.710693f,43.826859f,44.179974f } }
};

static const hdr_test_file g_hdr_6x6_test_files[] =
{
	{ "black_1x1.png", { 1000.000000f,1000.000000f,1000.000000f,1000.000000f,1000.000000f }, { 1000.000000f,1000.000000f,1000.000000f,1000.000000f,1000.000000f } },
	{ "atrium.exr", { 30.959572f,30.959572f,30.770338f,30.772770f,31.128767f }, { 30.882959f,30.882959f,30.612440f,30.598936f,30.895250f } },
	{ "backyard.exr", { 31.784214f,31.784214f,31.791901f,31.803551f,31.944782f }, { 31.591133f,31.591133f,31.597143f,31.591780f,31.732521f } },
	{ "Desk.exr", { 16.434078f,16.434078f,17.116821f,17.119164f,17.473869f }, { 16.378624f,16.378624f,16.720890f,16.720837f,16.989027f } },
	{ "atrium.exr", { 30.959572f,30.959572f,30.770338f,30.772770f,31.128767f }, { 30.882959f,30.882959f,30.612440f,30.598936f,30.895250f } },
	{ "yucca.exr", { 28.273916f,28.273916f,28.855904f,28.878124f,29.159794f }, { 27.989918f,27.989918f,28.310234f,28.293547f,28.570906f } },
	{ "tough.png", { 26.233910f,26.233910f,27.691349f,27.709543f,28.563215f }, { 25.678591f,25.678591f,26.385843f,26.392776f,26.868755f } },
	{ "kodim03.png", { 38.326469f,38.326469f,38.436966f,38.471195f,38.595867f }, { 37.782318f,37.782318f,37.837765f,37.847427f,37.938293f } },
	{ "kodim18.png", { 32.514179f,32.514179f,32.408348f,32.392838f,32.517056f }, { 32.434414f,32.434414f,32.321037f,32.299664f,32.424305f } },
	{ "kodim23.png", { 36.778912f,36.778912f,36.861130f,36.879044f,37.061916f }, { 36.433865f,36.433865f,36.466240f,36.460670f,36.623734f } },
};

static const hdr_test_file g_hdr_6x6i_test_files[] =
{
	{ "black_1x1.png", { 1000.000000f,1000.000000f,1000.000000f,1000.000000f,1000.000000f }, { 1000.000000f,1000.000000f,1000.000000f,1000.000000f,1000.000000f } },
	{ "atrium.exr", { 30.959572f,30.959572f,30.770338f,30.772770f,31.128767f }, { 30.882959f,30.882959f,30.612440f,30.598936f,30.895250f } },
	{ "backyard.exr", { 31.784214f,31.784214f,31.791901f,31.803551f,31.944782f }, { 31.591133f,31.591133f,31.597143f,31.591780f,31.732521f } },
	{ "Desk.exr", { 16.434078f,16.434078f,17.116821f,17.119164f,17.473869f }, { 16.378624f,16.378624f,16.720890f,16.720837f,16.989027f } },
	{ "atrium.exr", { 30.959572f,30.959572f,30.770338f,30.772770f,31.128767f }, { 30.882959f,30.882959f,30.612440f,30.598936f,30.895250f } },
	{ "yucca.exr", { 28.273916f,28.273916f,28.855904f,28.878124f,29.159794f }, { 27.989918f,27.989918f,28.310234f,28.293547f,28.570906f } },
	{ "tough.png", { 26.233910f,26.233910f,27.691349f,27.709543f,28.563215f }, { 25.678591f,25.678591f,26.385843f,26.392776f,26.868755f } },
	{ "kodim03.png", { 38.326469f,38.326469f,38.436966f,38.471195f,38.595867f }, { 37.782318f,37.782318f,37.837765f,37.847427f,37.938293f } },
	{ "kodim18.png", { 32.514179f,32.514179f,32.408348f,32.392838f,32.517056f }, { 32.434414f,32.434414f,32.321037f,32.299664f,32.424305f } },
	{ "kodim23.png", { 36.778912f,36.778912f,36.861130f,36.879044f,37.061916f }, { 36.433865f,36.433865f,36.466240f,36.460670f,36.623734f } },
};

static const hdr_test_file g_hdr_6x6i_l_test_files[] =
{
	{ "black_1x1.png", { 1000.000000f,1000.000000f,1000.000000f,1000.000000f,1000.000000f }, { 1000.000000f,1000.000000f,1000.000000f,1000.000000f,1000.000000f } },
	{ "atrium.exr", { 30.870792f,30.891232f,30.621367f,30.623915f,30.975077f }, { 30.792868f,30.800392f,30.463884f,30.454252f,30.764057f } },
	{ "backyard.exr", { 31.341709f,31.305914f,31.310831f,31.316299f,31.430468f }, { 31.179270f,31.146502f,31.150509f,31.140987f,31.252031f } },
	{ "Desk.exr", { 16.445023f,16.457247f,17.120258f,17.122082f,17.468046f }, { 16.382484f,16.391695f,16.726139f,16.725529f,16.985308f } },
	{ "atrium.exr", { 30.870792f,30.891232f,30.621367f,30.623915f,30.975077f }, { 30.792868f,30.800392f,30.463884f,30.454252f,30.764057f } },
	{ "yucca.exr", { 28.193764f,28.203444f,28.750029f,28.770260f,29.046646f }, { 27.918747f,27.925451f,28.228069f,28.217707f,28.486164f } },
	{ "tough.png", { 25.630802f,25.532228f,27.172880f,27.189053f,28.139309f }, { 25.160349f,25.056414f,26.012842f,26.018627f,26.592100f } },
	{ "kodim03.png", { 36.871231f,36.667595f,36.741497f,36.806915f,36.872837f }, { 36.500050f,36.309052f,36.360775f,36.404907f,36.454163f } },
	{ "kodim18.png", { 32.275890f,32.219872f,32.162785f,32.163559f,32.268921f }, { 32.201065f,32.143383f,32.078678f,32.073696f,32.188496f } },
	{ "kodim23.png", { 35.954903f,35.869717f,35.914257f,35.956097f,36.107834f }, { 35.681644f,35.612144f,35.616695f,35.638779f,35.797539f } },
};

static bool test_mode_hdr(command_line_params& opts, basist::basis_tex_format tex_fmt, uint32_t num_test_files, const hdr_test_file *pTest_files, float lambda)
{
	BASISU_ASSUME(uastc_hdr_4x4_codec_options::cMaxLevel == 4);

	fmt_printf("test_mode_hdr: Testing basis_tex_format {}, lambda {}\n", (uint32_t)tex_fmt, lambda);

	uint32_t total_mismatches = 0;

#ifdef USE_TIGHTER_TEST_TOLERANCES
	// The PSNR's above were created with a MSVC compiled executable, x64. Hopefully this is not too low a threshold.
	const float PSNR_THRESHOLD = .125f;
#else
	// Minor differences in how floating point code is optimized can result in slightly different generated files.
	const float PSNR_THRESHOLD = .3f;
#endif

	double highest_delta = 0.0f;

	// TODO: This doesn't test all 6x6 levels, but that's fine for now.
	basisu::vector2D<float> astc_psnr(num_test_files, MAX_ASTC_HDR_4x4_TEST_LEVEL + 1);
	basisu::vector2D<float> bc6h_psnr(num_test_files, MAX_ASTC_HDR_4x4_TEST_LEVEL + 1);

	for (uint32_t i = 0; i < num_test_files; i++)
	{
		std::string filename(opts.m_test_file_dir);
		if (filename.size())
		{
			filename.push_back('/');
		}
		filename += std::string(pTest_files[i].m_pFilename);

		basisu::vector<imagef> source_imagesf(1);

		imagef& source_image = source_imagesf[0];
		if (!load_image_hdr(filename.c_str(), source_image))
		{
			error_printf("Failed loading test image \"%s\"\n", filename.c_str());
			return false;
		}

		printf("Loaded file \"%s\", dimemsions %ux%u\n", filename.c_str(), source_image.get_width(), source_image.get_height());

		for (uint32_t uastc_hdr_level = 0; uastc_hdr_level <= MAX_ASTC_HDR_4x4_TEST_LEVEL; uastc_hdr_level++)
		{
			image_stats stats;

			uint32_t flags_and_quality;
			size_t data_size = 0;

			printf("**** Testing UASTC HDR Level %u\n", uastc_hdr_level);

			flags_and_quality = (opts.m_comp_params.m_multithreading ? cFlagThreaded : 0);// | cFlagPrintStats | cFlagPrintStatus;
			flags_and_quality |= uastc_hdr_level;

			void* pData = basis_compress(tex_fmt,
				source_imagesf, flags_and_quality, lambda,
				&data_size, &stats);
			if (!pData)
			{
				error_printf("basis_compress() failed!\n");
				return false;
			}
			basis_free_data(pData);

			double delta1, delta2;

			printf("ASTC PSNR: %f (expected %f, delta %f), BC6H PSNR: %f (expected %f, delta %f)\n",
				stats.m_basis_rgb_avg_log2_psnr, pTest_files[i].m_level_psnr_astc[uastc_hdr_level], delta1 = fabs(stats.m_basis_rgb_avg_log2_psnr - pTest_files[i].m_level_psnr_astc[uastc_hdr_level]),
				stats.m_basis_rgb_avg_bc6h_log2_psnr, pTest_files[i].m_level_psnr_bc6h[uastc_hdr_level], delta2 = fabs(stats.m_basis_rgb_avg_bc6h_log2_psnr - pTest_files[i].m_level_psnr_bc6h[uastc_hdr_level]));

			highest_delta = maximum(highest_delta, delta1);
			highest_delta = maximum(highest_delta, delta2);

			if (fabs(stats.m_basis_rgb_avg_log2_psnr - pTest_files[i].m_level_psnr_astc[uastc_hdr_level]) > PSNR_THRESHOLD)
			{
				error_printf("Expected UASTC HDR RGB Avg PSNR was %f, but got %f instead!\n", pTest_files[i].m_level_psnr_astc[uastc_hdr_level], stats.m_basis_rgb_avg_log2_psnr);
				total_mismatches++;
			}

			if (fabs(stats.m_basis_rgb_avg_bc6h_log2_psnr - pTest_files[i].m_level_psnr_bc6h[uastc_hdr_level]) > PSNR_THRESHOLD)
			{
				error_printf("Expected UASTC/ASTC->BC6H HDR RGB Avg PSNR was %f, but got %f instead!\n", pTest_files[i].m_level_psnr_bc6h[uastc_hdr_level], stats.m_basis_rgb_avg_bc6h_log2_psnr);
				total_mismatches++;
			}

			astc_psnr(i, uastc_hdr_level) = stats.m_basis_rgb_avg_log2_psnr;
			bc6h_psnr(i, uastc_hdr_level) = stats.m_basis_rgb_avg_bc6h_log2_psnr;
		}
	}

	printf("Total HDR mismatches: %u\n", total_mismatches);
	printf("Highest delta: %f\n", highest_delta);

	bool result = true;
	if (total_mismatches)
	{
		error_printf("HDR test FAILED\n");
		result = false;
	}
	else
	{
		printf("HDR test succeeded\n");
	}

#if 0
	for (uint32_t i = 0; i < num_test_files; i++)
	{
		printf("{ \"%s\", { ", pTest_files[i].m_pFilename);
		for (uint32_t uastc_hdr_level = 0; uastc_hdr_level <= 4; uastc_hdr_level++)
			printf("%ff%c", astc_psnr(i, uastc_hdr_level), (uastc_hdr_level < 4) ? ',' : ' ');

		printf("}, { ");

		for (uint32_t uastc_hdr_level = 0; uastc_hdr_level <= 4; uastc_hdr_level++)
			printf("%ff%c", bc6h_psnr(i, uastc_hdr_level), (uastc_hdr_level < 4) ? ',' : ' ');
		printf("} },\n");
	} // i
#endif

	for (uint32_t uastc_hdr_level = 0; uastc_hdr_level <= MAX_ASTC_HDR_4x4_TEST_LEVEL; uastc_hdr_level++)
	{
		float tot_astc = 0.0f, tot_bc6h = 0.0f;
		for (uint32_t i = 0; i < num_test_files; i++)
		{
			tot_astc += astc_psnr(i, uastc_hdr_level);
			tot_bc6h += bc6h_psnr(i, uastc_hdr_level);
		}

		tot_astc /= (float)num_test_files;
		tot_bc6h /= (float)num_test_files;

		fmt_printf("Level: {}, Avg. ASTC PSNR: {}, Avg. BC6H PSNR: {}\n", uastc_hdr_level, tot_astc, tot_bc6h);
	}

	return result;
}

static bool clbench_mode(command_line_params& opts)
{
	BASISU_NOTE_UNUSED(opts);

	bool opencl_failed = false;
	bool use_cl = basis_benchmark_etc1s_opencl(&opencl_failed);
	if (use_cl)
		printf("OpenCL ETC1S encoding is faster on this machine\n");
	else
	{
		if (opencl_failed)
			printf("OpenCL failed!\n");
		printf("CPU ETC1S encoding is faster on this machine\n");
	}

	return true;
}

#ifdef FORCE_SAN_FAILURE
static void force_san_failure()
{
	// Purposely do things that should trigger the address sanitizer
	int arr[5] = { 0, 1, 2, 3, 4 };
	printf("Out of bounds element: %d\n", arr[10]);

	//uint8_t* p = (uint8_t *)malloc(10);
	//p[10] = 99;

	//uint8_t* p = (uint8_t *)malloc(10);
	//free(p);
	//p[0] = 99;
}
#endif // FORCE_SAN_FAILURE

static int main_internal(int argc, const char **argv)
{
	printf("Basis Universal LDR/HDR GPU Texture Compression and Transcoding System v" BASISU_TOOL_VERSION
#if defined(_ARM64EC_) || defined(_ARM64_)
	" (ARM64)"
#elif defined(_M_IX86)
	" (x86)"
#elif defined(_M_X64) || defined(_M_AMD64)
	" (x64)"
#endif
	"\nCopyright (C) 2019-2025 Binomial LLC, All rights reserved\n");

#ifdef FORCE_SAN_FAILURE
	force_san_failure();
#endif

	//interval_timer tm;
	//tm.start();

	// See if OpenCL support has been disabled. We don't want to parse the command line until the lib is initialized
	bool use_opencl = false;
	bool opencl_force_serialization = false;

	for (int i = 1; i < argc; i++)
	{
		if ((strcmp(argv[i], "-opencl") == 0) || (strcmp(argv[i], "-clbench") == 0))
			use_opencl = true;
		if (strcmp(argv[i], "-opencl_serialize") == 0)
			opencl_force_serialization = true;
	}

#if !BASISU_SUPPORT_OPENCL
	if (use_opencl)
	{
		fprintf(stderr, "WARNING: -opencl specified, but OpenCL support was not enabled at compile time! With cmake, use -D BASISU_OPENCL=1. Falling back to CPU compression.\n");
	}
#endif

	basisu_encoder_init(use_opencl, opencl_force_serialization);

	//printf("Encoder and transcoder libraries initialized in %3.3f ms\n", tm.get_elapsed_ms());

	if (argc == 1)
	{
		print_usage();
		return EXIT_FAILURE;
	}

	command_line_params opts;
	if (!opts.parse(argc, argv))
	{
		//print_usage();
		return EXIT_FAILURE;
	}

#if BASISU_SUPPORT_SSE
	printf("Using SSE 4.1: %u, Multithreading: %u, Zstandard support: %u, OpenCL: %u\n", g_cpu_supports_sse41, (uint32_t)opts.m_comp_params.m_multithreading, basist::basisu_transcoder_supports_ktx2_zstd(), opencl_is_available());
#else
	printf("No SSE, Multithreading: %u, Zstandard support: %u, OpenCL: %u\n", (uint32_t)opts.m_comp_params.m_multithreading, basist::basisu_transcoder_supports_ktx2_zstd(), opencl_is_available());
#endif

	if (!opts.process_listing_files())
		return EXIT_FAILURE;

	if (opts.m_mode == cDefault)
	{
		for (size_t i = 0; i < opts.m_input_filenames.size(); i++)
		{
			std::string ext(string_get_extension(opts.m_input_filenames[i]));
			if ((strcasecmp(ext.c_str(), "basis") == 0) || (strcasecmp(ext.c_str(), "ktx") == 0) || (strcasecmp(ext.c_str(), "ktx2") == 0))
			{
				// If they haven't specified any modes, and they give us a .basis file, then assume they want to unpack it.
				opts.m_mode = cUnpack;
				break;
			}
		}
	}

	bool status = false;

	switch (opts.m_mode)
	{
	case cDefault:
	case cCompress:
		status = compress_mode(opts);
		break;
	case cValidate:
	case cInfo:
	case cUnpack:
		status = unpack_and_validate_mode(opts);
		break;
	case cCompare:
		status = compare_mode(opts);
		break;
	case cHDRCompare:
		status = hdr_compare_mode(opts);
		break;
	case cVersion:
		status = true; // We printed the version at the beginning of main_internal
		break;
	case cBench:
		status = bench_mode(opts);
		break;
	case cCompSize:
		status = compsize_mode(opts);
		break;
	case cTestLDR:
		status = test_mode_ldr(opts);
		break;
	case cTestHDR_4x4:
		status = test_mode_hdr(opts, basist::basis_tex_format::cUASTC_HDR_4x4, std::size(g_hdr_4x4_test_files), g_hdr_4x4_test_files, 0.0f);
		break;
	case cTestHDR_6x6:
		status = test_mode_hdr(opts, basist::basis_tex_format::cASTC_HDR_6x6, std::size(g_hdr_6x6_test_files), g_hdr_6x6_test_files, 0.0f);
		break;
	case cTestHDR_6x6i:
		status = test_mode_hdr(opts, basist::basis_tex_format::cASTC_HDR_6x6_INTERMEDIATE, std::size(g_hdr_6x6i_test_files), g_hdr_6x6i_test_files, 0.0f);

		if (status)
		{
			status = test_mode_hdr(opts, basist::basis_tex_format::cASTC_HDR_6x6_INTERMEDIATE, std::size(g_hdr_6x6i_l_test_files), g_hdr_6x6i_l_test_files, 500.0f);
		}

		break;
	case cCLBench:
		status = clbench_mode(opts);
		break;
	case cSplitImage:
		status = split_image_mode(opts);
		break;
	case cCombineImages:
		status = combine_images_mode(opts);
		break;
	case cTonemapImage:
		status = tonemap_image_mode(opts);
		break;
	default:
		assert(0);
		break;
	}

	return status ? EXIT_SUCCESS : EXIT_FAILURE;
}

//-----------------------------------------------------------------------------------

#if CLEAR_WIN32_CONSOLE
void clear_console()
{
	//if (!IsDebuggerPresent())
	//	return;

	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	DWORD consoleSize, charsWritten;
	COORD topLeft = { 0, 0 };

	// Get console screen buffer info
	GetConsoleScreenBufferInfo(hConsole, &csbi);
	consoleSize = csbi.dwSize.X * csbi.dwSize.Y;

	// Fill the entire screen with spaces
	FillConsoleOutputCharacter(hConsole, ' ', consoleSize, topLeft, &charsWritten);

	// Set the cursor at the top left corner
	SetConsoleCursorPosition(hConsole, topLeft);
}
#endif

//-----------------------------------------------------------------------------------

int main(int argc, const char** argv)
{
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif

#if CLEAR_WIN32_CONSOLE
	clear_console();
#endif

#if defined(DEBUG) || defined(_DEBUG)
	printf("DEBUG build\n");
#endif
#ifdef __SANITIZE_ADDRESS__
	printf("Address sanitizer enabled\n");
#endif

	int status = EXIT_FAILURE;

#if BASISU_CATCH_EXCEPTIONS
	try
	{
		 status = main_internal(argc, argv);
	}
	catch (const std::exception &exc)
	{
		 fprintf(stderr, "Fatal error: Caught exception \"%s\"\n", exc.what());
	}
	catch (...)
	{
		fprintf(stderr, "Fatal error: Uncaught exception!\n");
	}
#else
	status = main_internal(argc, argv);
#endif

	return status;
}

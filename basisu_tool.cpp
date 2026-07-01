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
#include "encoder/basisu_xbc7_encode.h"
#include "transcoder/basisu_xbc7_decoder.h"
#include "encoder/basisu_etc.h"
#include "encoder/basisu_gpu_texture.h"
#include "encoder/basisu_frontend.h"
#include "encoder/basisu_backend.h"
#include "encoder/basisu_comp.h"
#include "transcoder/basisu_transcoder.h"
#include "encoder/basisu_ssim.h"
#include "encoder/basisu_opencl.h"
#include "encoder/basisu_astc_ldr_common.h"

#define MINIZ_HEADER_FILE_ONLY
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "encoder/basisu_miniz.h"

#include <queue>
#include <array>
#include <initializer_list>
#include <filesystem>

#include "encoder/basisu_resampler.h"
#include "encoder/basisu_resampler_filters.h"
#include "basisu_text_image.h"
#include "encoder/basisu_dds_export.h"
#include "encoder/pvpngreader.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Work around Win11 debug console bug. DO NOT SHIP SET TO 1.
#define CLEAR_WIN32_CONSOLE 0
#endif

// Set BASISU_CATCH_EXCEPTIONS if you want exceptions to crash the app, otherwise main() catches them.
#ifndef BASISU_CATCH_EXCEPTIONS
	#define BASISU_CATCH_EXCEPTIONS 0
#endif

using namespace basisu;
using namespace buminiz;

#define BASISU_TOOL_VERSION "2.50.0"

#if defined(DEBUG)
#pragma message("DEBUG defined")
#endif

#if defined(_DEBUG)
#pragma message("_DEBUG defined")
#endif

#ifndef NDEBUG
#if !defined(DEBUG) && !defined(_DEBUG)
#pragma message("NDEBUG is NOT defined, but DEBUG or _DEBUG are also NOT defined, which isn't ideal as extra debug assertion checks will not be compiled.")
#endif
#endif

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
	cCompareHVS,
	cHDRCompare,
	cImageDumpStats,
	cVersion,
	cBench,
	cCompSize,
	cTestLDR,
	cTestHDR_4x4,
	cTestHDR_6x6,
	cTestHDR_6x6i,
	cTestXUASTCLDR,
	cTestCodecs,
	cTestCodecsGen,
	cCLBench,
	cSplitImage,
	cCombineImages,
	cExtractChannel,
	cExtractSwizzle,
	cExtractRegion,
	cTextToPng,
	cPngToText,
	cTonemapImage,
	cDDS,
	cExportDDS,
	cExportKTX,
	cTinyDDSInfo,
	cKTXInfo,
	cBenchmarkSingle,
	cBenchmarkSweep
};

static void print_usage()
{
	printf("\nUsage: basisu filename [filename ...] <options>\n");

	puts(
#include "basisu_tool_help.h"
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

// Command line option matching helper. Recognizes an option by its canonical
// name (given with a single leading dash, e.g. "-etc1s"), accepting both the
// "-name" and Unix-style "--name" forms, case-insensitively.
static inline bool opt_match(const char *pArg, const char *pName)
{
	if (strcasecmp(pArg, pName) == 0)
		return true;
	// Accept the "--name" variant when the canonical name is "-name".
	if ((pArg[0] == '-') && (pArg[1] == '-') && (pName[0] == '-') && (strcasecmp(pArg + 1, pName) == 0))
		return true;
	return false;
}

// Same, but matches against any of several alias names, e.g.
// opt_match(pArg, { "-uastc", "-uastc_ldr", "-uastc_ldr_4x4" }).
static inline bool opt_match(const char *pArg, std::initializer_list<const char *> names)
{
	for (const char *pName : names)
		if (opt_match(pArg, pName))
			return true;
	return false;
}

// Optional sRGB<->linear transform applied by the -extract_* modes.
enum extract_color_xform
{
	cExtractXformNone = 0,
	cExtractXformToLinear,	// treat input as sRGB, output linear light
	cExtractXformToSRGB		// treat input as linear light, output sRGB
};

// True if the argument is a non-empty run of decimal digits (an unsigned integer). Used to decide whether an
// optional region rectangle follows a command, so that a positional filename beginning with a digit (e.g.
// "5texture.png") is NOT mistaken for a region coordinate.
static bool arg_is_unsigned_int(const char* pArg)
{
	if ((!pArg) || (!pArg[0]))
		return false;
	for (const char* p = pArg; *p; p++)
		if ((*p < '0') || (*p > '9'))
			return false;
	return true;
}

// Maps a single channel token to a source channel index in [0,3] (R/G/B/A), or -1 if it is not a valid channel.
static int channel_token_to_index(char c)
{
	switch (c)
	{
	case 'r': case 'R': case '0': return 0;
	case 'g': case 'G': case '1': return 1;
	case 'b': case 'B': case '2': return 2;
	case 'a': case 'A': case '3': return 3;
	default: return -1;
	}
}

// Parses a -benchmark_single format token into a basis_tex_format (definition is down by codec_benchmark()).
static bool parse_benchmark_tex_format(const std::string& s, basist::basis_tex_format& out_fmt);

class command_line_params
{
	BASISU_NO_EQUALS_OR_COPY_CONSTRUCT(command_line_params);

#define REMAINING_ARGS_CHECK(n) if (num_remaining_args < (n)) { error_printf("Error: Expected %u values to follow %s!\n", n, pArg); return false; }

	bool check_for_general_options(const char** arg_v, const char* pArg, int arg_index, const int num_remaining_args, int& arg_count)
	{
		BASISU_NOTE_UNUSED(arg_v);
		BASISU_NOTE_UNUSED(arg_index);
		BASISU_NOTE_UNUSED(num_remaining_args);
		BASISU_NOTE_UNUSED(arg_count);

		if (opt_match(pArg, "-wasi_threads"))
		{
			REMAINING_ARGS_CHECK(1);
			int num_threads = atoi(arg_v[arg_index + 1]);
			if ((num_threads < 0) || (num_threads > 256))
			{
				error_printf("Invalid number of threads\n");
				exit(EXIT_FAILURE);
			}
			set_num_wasi_threads(num_threads);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-higher_quality_transcoding"))
		{
			m_higher_quality_transcoding = true;
			return true;
		}
		else if (opt_match(pArg, "-no_fast_xuastc_ldr_bc7_transcoding"))
		{
			m_xuastc_ldr_disable_bc7_transcoding = true;
			return true;
		}
		else if (opt_match(pArg, "-fast_xuastc_ldr_bc7_transcoding"))
		{
			m_xuastc_ldr_disable_bc7_transcoding = false;
			return true;
		}
		else if (opt_match(pArg, "-no_etc1s_chroma_filtering"))
		{
			m_no_etc1s_transcoding_chroma_filtering = true;
			return true;
		}
		else if (opt_match(pArg, "-transcode_force_deblocking"))
		{
			m_transcode_force_deblocking = true;
			return true;
		}
		else if (opt_match(pArg, {"-transcode_disable_deblocking", "-transcode_no_deblocking"}))
		{
			m_transcode_disable_deblocking = true;
			return true;
		}
		
		return false;
	}

	bool check_for_xuastc_options(const char** arg_v, const char* pArg, int arg_index, const int num_remaining_args, int& arg_count)
	{
		// New unified -quality level which works across all codecs
		if (opt_match(pArg, "-quality"))
		{
			REMAINING_ARGS_CHECK(1);
			m_quality_level = clamp<int>(atoi(arg_v[arg_index + 1]), 1, 100);
			arg_count++;
			return true;
		}
		// New unified -effort level, which works across all codecs
		else if (opt_match(pArg, "-effort"))
		{
			REMAINING_ARGS_CHECK(1);
			m_effort_level = clamp<int>(atoi(arg_v[arg_index + 1]), 0, 10);
			//m_comp_params.m_xuastc_ldr_effort_level = atoi(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if ((opt_match(pArg, "-xuastc_debug_block")))
		{
			REMAINING_ARGS_CHECK(2);
			m_comp_params.m_xuastc_ldr_debug_block_x = clamp<int>(atoi(arg_v[arg_index + 1]), 0, 4096);
			m_comp_params.m_xuastc_ldr_debug_block_y = clamp<int>(atoi(arg_v[arg_index + 2]), 0, 4096);
			
			arg_count += 2;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_blurring", "-xuastc_ldr_blurring"})) // experimental, not recommended, very slow
		{
			m_comp_params.m_xuastc_ldr_blurring = true;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_no_blurring", "-xuastc_ldr_no_blurring"})) // experimental, not recommended, very slow
		{
			m_comp_params.m_xuastc_ldr_blurring = false;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_use_auto_comp", "-xuastc_ldr_use_auto_comp"}))
		{
			m_comp_params.m_xuastc_ldr_astc_comp_selection = (int)xuastc_ldr_astc_comp_selection::cAuto;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_use_basisu", "-xuastc_ldr_use_basisu"}))
		{
			m_comp_params.m_xuastc_ldr_astc_comp_selection = (int)xuastc_ldr_astc_comp_selection::cBasisU;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_use_astcenc", "-xuastc_ldr_use_astcenc"})) // experimental/development, must be enabled at compilation time by setting BASISU_SUPPORT_ASTCENC=1
		{
			m_comp_params.m_xuastc_ldr_astc_comp_selection = (int)xuastc_ldr_astc_comp_selection::cASTCENC;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_use_astcf", "-xuastc_ldr_use_astcf"}))
		{
			m_comp_params.m_xuastc_ldr_astc_comp_selection = (int)xuastc_ldr_astc_comp_selection::cASTCF;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_merge_astcenc", "-xuastc_ldr_merge_astcenc"})) // experimental/development, must be enabled at compilation time by setting BASISU_SUPPORT_ASTCENC=1
		{
			m_comp_params.m_xuastc_ldr_astc_comp_selection = (int)xuastc_ldr_astc_comp_selection::cBasisU_and_ASTCENC;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_merge_astcf", "-xuastc_ldr_merge_astcf"})) // experimental/development, must be enabled at compilation time by setting BASISU_SUPPORT_ASTCENC=1
		{
			m_comp_params.m_xuastc_ldr_astc_comp_selection = (int)xuastc_ldr_astc_comp_selection::cBasisU_and_ASTCF;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_merge_all", "-xuastc_ldr_merge_all"})) // experimental/development, must be enabled at compilation time by setting BASISU_SUPPORT_ASTCENC=1
		{
			m_comp_params.m_xuastc_ldr_astc_comp_selection = (int)xuastc_ldr_astc_comp_selection::cUseAll;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_no_deblocking", "-xuastc_ldr_no_deblocking"}))
		{
			m_comp_params.m_xuastc_ldr_deblocking_mode = (int)xuastc_ldr_deblocking_mode::cDisabled;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_deblocking_largest", "-xuastc_ldr_deblocking_largest"}))
		{
			m_comp_params.m_xuastc_ldr_deblocking_mode = (int)xuastc_ldr_deblocking_mode::cUseSCDAndFilteringOnlyLargestBlocks; // codec default
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_deblocking_all", "-xuastc_ldr_deblocking_all"}))
		{
			m_comp_params.m_xuastc_ldr_deblocking_mode = (int)xuastc_ldr_deblocking_mode::cUseSCDAndFilteringAllBlockSizes;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_deblocking_scd_no_filtering", "-xuastc_ldr_deblocking_scd_no_filtering"}))
		{
			m_comp_params.m_xuastc_ldr_deblocking_mode = (int)xuastc_ldr_deblocking_mode::cUseSCDNoFiltering;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_deblocking_no_scd_filtering_largest", "-xuastc_ldr_deblocking_no_scd_filtering_largest"}))
		{
			m_comp_params.m_xuastc_ldr_deblocking_mode = (int)xuastc_ldr_deblocking_mode::cNoSCDButEnableFilteringOnLargestBlocks;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_deblocking_no_scd_filtering_all", "-xuastc_ldr_deblocking_no_scd_filtering_all"}))
		{
			m_comp_params.m_xuastc_ldr_deblocking_mode = (int)xuastc_ldr_deblocking_mode::cNoSCDButEnableFilteringOnAllBlocks;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_deblocking_num_passes", "-xuastc_ldr_deblocking_num_passes"}))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_xuastc_ldr_num_deblocking_passes = clamp(atoi(arg_v[arg_index + 1]), 2, 256);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_sharpen", "-xuastc_ldr_sharpen"}))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_xuastc_ldr_sharpen_amount = (float)atof(arg_v[arg_index + 1]);
			arg_count++;

			// setting to 0 disables
			if (m_comp_params.m_xuastc_ldr_sharpen_amount <= 0.0f)
				m_comp_params.m_xuastc_ldr_sharpen_mode = (int)xuastc_ldr_sharpen_mode::cDisabled;
			else
				m_comp_params.m_xuastc_ldr_sharpen_mode = (int)xuastc_ldr_sharpen_mode::cAllBlockSizes; // if they've specified -xuastc_sharpen, they want it no matter what block size

			return true;
		}
		else if (opt_match(pArg, {"-xuastc_no_sharpen", "-xuastc_ldr_no_sharpen"}))
		{
			m_comp_params.m_xuastc_ldr_sharpen_mode = (int)xuastc_ldr_sharpen_mode::cDisabled;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_weights", "-xuastc_ldr_weights", "-weights"}))
		{
			REMAINING_ARGS_CHECK(4);
			m_comp_params.m_xuastc_ldr_channel_weights[0] = (uint32_t)clamp<float>((float)atof(arg_v[arg_index + 1]), 1.0f, 1024.0f);
			m_comp_params.m_xuastc_ldr_channel_weights[1] = (uint32_t)clamp<float>((float)atof(arg_v[arg_index + 2]), 1.0f, 1024.0f);
			m_comp_params.m_xuastc_ldr_channel_weights[2] = (uint32_t)clamp<float>((float)atof(arg_v[arg_index + 3]), 1.0f, 1024.0f);
			m_comp_params.m_xuastc_ldr_channel_weights[3] = (uint32_t)clamp<float>((float)atof(arg_v[arg_index + 4]), 1.0f, 1024.0f);
			arg_count += 4;
			return true;
		}
		else if (opt_match(pArg, "-ls_min_psnr"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_ls_min_psnr = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-ls_min_alpha_psnr"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_ls_min_alpha_psnr = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-ls_thresh_psnr"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_ls_thresh_psnr = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-ls_thresh_alpha_psnr"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_ls_thresh_alpha_psnr = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-ls_thresh_edge_psnr"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_ls_thresh_edge_psnr = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-ls_thresh_edge_alpha_psnr"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_ls_thresh_edge_alpha_psnr = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_arith", "-xuastc_ldr_arith"}))
		{
			m_comp_params.m_xuastc_ldr_syntax = (int)basist::astc_ldr_t::xuastc_ldr_syntax::cFullArith;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_zstd", "-xuastc_ldr_zstd"}))
		{
			m_comp_params.m_xuastc_ldr_syntax = (int)basist::astc_ldr_t::xuastc_ldr_syntax::cFullZStd;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_hybrid", "-xuastc_ldr_hybrid"}))
		{
			m_comp_params.m_xuastc_ldr_syntax = (int)basist::astc_ldr_t::xuastc_ldr_syntax::cHybridArithZStd;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_heavy_subset_usage", "-xuastc_ldr_heavy_subset_usage"}))
		{
			m_comp_params.m_xuastc_ldr_heavy_subset_usage = true;
			return true;
		}
		else if (opt_match(pArg, {"-xuastc_no_heavy_subset_usage", "-xuastc_ldr_no_heavy_subset_usage"}))
		{
			m_comp_params.m_xuastc_ldr_heavy_subset_usage = false;
			return true;
		}
		else if (opt_match(pArg, "-xy"))
		{
			m_comp_params.m_xuastc_ldr_use_lossy_supercompression = true;
			return true;
		}
		else if (opt_match(pArg, "-xyd"))
		{
			m_comp_params.m_xuastc_ldr_use_lossy_supercompression = false;
			return true;
		}
		else if (opt_match(pArg, "-xs"))
		{
			m_comp_params.m_xuastc_ldr_force_disable_subsets = true;
			return true;
		}
		else if (opt_match(pArg, "-xsu"))
		{
			m_comp_params.m_xuastc_ldr_force_disable_subsets = false;
			return true;
		}
		else if (opt_match(pArg, "-xp"))
		{
			m_comp_params.m_xuastc_ldr_force_disable_rgb_dual_plane = true;
			return true;
		}
		else if (opt_match(pArg, "-xpu"))
		{
			m_comp_params.m_xuastc_ldr_force_disable_rgb_dual_plane = false;
			return true;
		}
		else if (opt_match(pArg, "-ts"))
		{
			m_comp_params.m_perceptual = true;
			m_comp_params.m_ktx2_and_basis_srgb_transfer_function = true;
			return true;
		}
		else if (opt_match(pArg, "-tl"))
		{
			m_comp_params.m_perceptual = false;
			m_comp_params.m_ktx2_and_basis_srgb_transfer_function = false;
			return true;
		}
		// Supercompressed XUASTC LDR 4x4-12x12
		else if (opt_match(pArg, {"-ldr_4x4i", "-xuastc_ldr_4x4"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_4x4);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_5x4i", "-xuastc_ldr_5x4"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_5x4);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_5x5i", "-xuastc_ldr_5x5"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_5x5);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_6x5i", "-xuastc_ldr_6x5"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_6x5);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_6x6i", "-xuastc_ldr_6x6"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_6x6);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_8x5i", "-xuastc_ldr_8x5"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_8x5);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_8x6i", "-xuastc_ldr_8x6"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_8x6);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_10x5i", "-xuastc_ldr_10x5"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_10x5);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_10x6i", "-xuastc_ldr_10x6"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_10x6);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_8x8i", "-xuastc_ldr_8x8"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_8x8);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_10x8i", "-xuastc_ldr_10x8"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_10x8);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_10x10i", "-xuastc_ldr_10x10"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_10x10);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_12x10i", "-xuastc_ldr_12x10"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_12x10);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_12x12i", "-xuastc_ldr_12x12"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUASTC_LDR_12x12);
			return true;
		}
		else if (opt_match(pArg, "-xubc7"))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cXUBC7);
			return true;
		}
		else if (opt_match(pArg, {"-xubc7_rdo", "-xubc7_rdo_level"}))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_xubc7_rdo_level = clamp(atoi(arg_v[arg_index + 1]), 0, 100);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, {"-xubc7_stripes", "-xubc7_num_stripes"}))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_xubc7_num_stripes = clamp(atoi(arg_v[arg_index + 1]), 1, 16);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-xubc7_bc7f"))
		{
			m_comp_params.m_xubc7_encoder = (int)basisu::xbc7::bc7_encoder_type::cBC7F;
			return true;
		}
		else if (opt_match(pArg, {"-xubc7_bc7e_scalar", "-xubc7_bc7e"}))
		{
			m_comp_params.m_xubc7_encoder = (int)basisu::xbc7::bc7_encoder_type::cBC7E_Scalar;
			return true;
		}
		else if (opt_match(pArg, "-xubc7_bc7e_scalar_level"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_xubc7_encoder = (int)basisu::xbc7::bc7_encoder_type::cBC7E_Scalar;
			m_comp_params.m_xubc7_bc7e_scalar_level = clamp(atoi(arg_v[arg_index + 1]), (int)basisu::xbc7::BC7E_SCALAR_MIN_LEVEL, (int)basisu::xbc7::BC7E_SCALAR_MAX_LEVEL);
			arg_count++;
			return true;
		}
		// Plain ASTC LDR 4x4-12x12
		else if (opt_match(pArg, {"-ldr_4x4", "-astc_ldr_4x4"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_4x4);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_5x4", "-astc_ldr_5x4"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_5x4);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_5x5", "-astc_ldr_5x5"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_5x5);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_6x5", "-astc_ldr_6x5"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_6x5);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_6x6", "-astc_ldr_6x6"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_6x6);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_8x5", "-astc_ldr_8x5"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_8x5);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_8x6", "-astc_ldr_8x6"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_8x6);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_10x5", "-astc_ldr_10x5"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_10x5);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_10x6", "-astc_ldr_10x6"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_10x6);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_8x8", "-astc_ldr_8x8"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_8x8);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_10x8", "-astc_ldr_10x8"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_10x8);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_10x10", "-astc_ldr_10x10"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_10x10);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_12x10", "-astc_ldr_12x10"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_12x10);
			return true;
		}
		else if (opt_match(pArg, {"-ldr_12x12", "-astc_ldr_12x12"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_LDR_12x12);
			return true;
		}

		return false;
	}

	// -dds (basic DX10 DDS writer) options. Kept in its own function so the big option chain in parse() stays
	// under the compiler's block-nesting limit. See basisu_dds_export.h.
	bool check_for_dds_options(const char** arg_v, const char* pArg, int arg_index, const int num_remaining_args, int& arg_count)
	{
		if (opt_match(pArg, "-dds"))
		{
			m_mode = cDDS;
			return true;
		}
		else if (opt_match(pArg, "-dds_format"))
		{
			REMAINING_ARGS_CHECK(1);
			m_dds_format = arg_v[arg_index + 1];
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-dds_bc7f"))
		{
			m_dds_bc7_encoder = cDDSBC7Encoder_BC7F;
			return true;
		}
		else if (opt_match(pArg, {"-dds_bc7e_scalar", "-dds_bc7e"}))
		{
			m_dds_bc7_encoder = cDDSBC7Encoder_BC7E_Scalar;
			return true;
		}
		else if (opt_match(pArg, "-dds_bc7f_level"))
		{
			REMAINING_ARGS_CHECK(1);
			m_dds_bc7_encoder = cDDSBC7Encoder_BC7F;
			m_dds_bc7f_level = clamp(atoi(arg_v[arg_index + 1]), 0, 2);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-dds_bc7e_scalar_level"))
		{
			REMAINING_ARGS_CHECK(1);
			m_dds_bc7_encoder = cDDSBC7Encoder_BC7E_Scalar;
			m_dds_bc7e_scalar_level = clamp(atoi(arg_v[arg_index + 1]), 0, 6);
			arg_count++;
			return true;
		}

		return false;
	}

	bool check_for_hdr_options(const char** arg_v, const char* pArg, int arg_index, const int num_remaining_args, int& arg_count)
	{
		if (opt_match(pArg, {"-hdr", "-hdr_4x4", "-uastc_hdr_4x4"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cUASTC_HDR_4x4);
			return true;
		}
		else if (opt_match(pArg, "-rec_2020"))
		{
			m_comp_params.m_astc_hdr_6x6_options.m_rec2020_bt2100_color_gamut = true;
			return true;
		}
		else if (opt_match(pArg, "-hdr_6x6i_16_compatibility"))
		{
			// UASTC HDR 6x6i: Write v1.60 compatible files vs. 2.0.
			m_comp_params.m_astc_hdr_6x6_options.m_write_basisu_1_6_compatible_files = true;
			return true;
		}
		else if (opt_match(pArg, "-hdr_6x6i_20_compatibility"))
		{
			// UASTC HDR 6x6i: Write v2.00 compatible files vs. 2.0.
			m_comp_params.m_astc_hdr_6x6_options.m_write_basisu_1_6_compatible_files = false;
			return true;
		}
		else if (opt_match(pArg, {"-hdr_6x6", "-astc_hdr_6x6"}))
		{
			// max quality (if -lambda=0) or RDO UASTC HDR 6x6
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_HDR_6x6);
			return true;
		}
		else if (opt_match(pArg, {"-hdr_6x6i", "-uastc_hdr_6x6", "-uastc_hdr_6x6i"}))
		{
			// intermediate format UASTC HDR 6x6
			m_comp_params.set_format_mode(basist::basis_tex_format::cUASTC_HDR_6x6_INTERMEDIATE);
			return true;
		}
		else if (opt_match(pArg, "-lambda"))
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

			m_used_old_style_codec_config_param = true;

			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-hdr_6x6_jnd"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_astc_hdr_6x6_options.m_jnd_optimization = true;
			m_comp_params.m_astc_hdr_6x6_options.m_jnd_delta_itp_thresh = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-hdr_6x6_level"))
		{
			REMAINING_ARGS_CHECK(1);
			const int level = atoi(arg_v[arg_index + 1]);
			m_comp_params.m_astc_hdr_6x6_options.set_user_level(level);
			m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_HDR_6x6);

			m_used_old_style_codec_config_param = true;

			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-hdr_6x6i_level"))
		{
			REMAINING_ARGS_CHECK(1);
			const int level = atoi(arg_v[arg_index + 1]);
			m_comp_params.m_astc_hdr_6x6_options.set_user_level(level);
			m_comp_params.set_format_mode(basist::basis_tex_format::cUASTC_HDR_6x6_INTERMEDIATE);

			m_used_old_style_codec_config_param = true;

			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-hdr_6x6_extra_pats"))
		{
			m_comp_params.m_astc_hdr_6x6_options.m_extra_patterns_flag = true;
			return true;
		}
		else if (opt_match(pArg, "-hdr_6x6_brute_force_pats"))
		{
			m_comp_params.m_astc_hdr_6x6_options.m_brute_force_partition_matching = true;
			return true;
		}
		else if (opt_match(pArg, {"-hdr_6x6_comp_levels", "-hdr_6x6i_comp_levels"}))
		{
			REMAINING_ARGS_CHECK(2);

			// Intended for low-level/development/testing
			const int lo_level = clamp<int>(atoi(arg_v[arg_index + 1]), 0, astc_6x6_hdr::ASTC_HDR_6X6_MAX_COMP_LEVEL);
			const int hi_level = clamp<int>(atoi(arg_v[arg_index + 2]), 0, astc_6x6_hdr::ASTC_HDR_6X6_MAX_COMP_LEVEL);

			m_comp_params.m_astc_hdr_6x6_options.m_master_comp_level = minimum(lo_level, hi_level);
			m_comp_params.m_astc_hdr_6x6_options.m_highest_comp_level = maximum(lo_level, hi_level);
			
			if (opt_match(pArg, "-hdr_6x6_comp_levels"))
				m_comp_params.set_format_mode(basist::basis_tex_format::cASTC_HDR_6x6);
			else
				m_comp_params.set_format_mode(basist::basis_tex_format::cUASTC_HDR_6x6_INTERMEDIATE);

			m_used_old_style_codec_config_param = true;

			arg_count += 2;
			return true;
		}
		else if (opt_match(pArg, "-hdr_6x6_no_gaussian"))
		{
			m_comp_params.m_astc_hdr_6x6_options.m_gaussian1_fallback = false;
			m_comp_params.m_astc_hdr_6x6_options.m_gaussian2_fallback = false;
			return true;
		}
		else if (opt_match(pArg, "-hdr_6x6_gaussian1"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_astc_hdr_6x6_options.m_gaussian1_strength = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-hdr_6x6_gaussian2"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_astc_hdr_6x6_options.m_gaussian2_strength = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, {"-hdr_ldr_no_srgb_to_linear", "-hdr_ldr_upconversion_no_srgb_to_linear"}))
		{
			m_comp_params.m_ldr_hdr_upconversion_srgb_to_linear = false;
			return true;
		}
		else if (opt_match(pArg, "-hdr_ldr_upconversion_black_bias"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_ldr_hdr_upconversion_black_bias = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-hdr_ldr_upconversion_nit_multiplier"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_ldr_hdr_upconversion_nit_multiplier = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-hdr_uber_mode"))
		{
			m_comp_params.m_uastc_hdr_4x4_options.m_allow_uber_mode = true;
			return true;
		}
		else if (opt_match(pArg, "-hdr_ultra_quant"))
		{
			m_comp_params.m_uastc_hdr_4x4_options.m_ultra_quant = true;
			return true;
		}
		else if (opt_match(pArg, "-hdr_favor_astc"))
		{
			m_comp_params.m_hdr_favor_astc = true;
			return true;
		}

		return false;
	}

	// ETC1S or UASTC LDR 4x4 specific options
	bool check_for_etc1s_or_uastc_options(const char** arg_v, const char* pArg, int arg_index, const int num_remaining_args, int& arg_count)
	{
		if (opt_match(pArg, {"-etc1s", "-ldr"}))
		{
			// -ldr selects the LDR/SDR default codec (ETC1S), symmetric with -hdr.
			m_comp_params.set_format_mode(basist::basis_tex_format::cETC1S);
			return true;
		}
		else if (opt_match(pArg, {"-uastc", "-uastc_ldr", "-uastc_ldr_4x4"}))
		{
			m_comp_params.set_format_mode(basist::basis_tex_format::cUASTC_LDR_4x4);
			return true;
		}
		else if (opt_match(pArg, "-uastc_level"))
		{
			REMAINING_ARGS_CHECK(1);

			int uastc_level = atoi(arg_v[arg_index + 1]);

			uastc_level = clamp<int>(uastc_level, 0, TOTAL_PACK_UASTC_LEVELS - 1);

			static_assert(TOTAL_PACK_UASTC_LEVELS == 5, "TOTAL_PACK_UASTC_LEVELS==5");
			static const uint32_t s_level_flags[TOTAL_PACK_UASTC_LEVELS] = { cPackUASTCLevelFastest, cPackUASTCLevelFaster, cPackUASTCLevelDefault, cPackUASTCLevelSlower, cPackUASTCLevelVerySlow };

			m_comp_params.m_pack_uastc_ldr_4x4_flags &= ~cPackUASTCLevelMask;
			m_comp_params.m_pack_uastc_ldr_4x4_flags |= s_level_flags[uastc_level];

			m_comp_params.m_uastc_hdr_4x4_options.set_quality_level(uastc_level);

			m_used_old_style_codec_config_param = true;

			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-uastc_rdo_l"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_rdo_uastc_ldr_4x4_quality_scalar = (float)atof(arg_v[arg_index + 1]);
			m_comp_params.m_rdo_uastc_ldr_4x4 = true;

			m_used_old_style_codec_config_param = true;

			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-uastc_rdo_d"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_rdo_uastc_ldr_4x4_dict_size = atoi(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-uastc_rdo_b"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_rdo_uastc_ldr_4x4_max_smooth_block_error_scale = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-uastc_rdo_s"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_rdo_uastc_ldr_4x4_smooth_block_max_std_dev = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-uastc_rdo_f"))
		{
			m_comp_params.m_rdo_uastc_ldr_4x4_favor_simpler_modes_in_rdo_mode = false;
			return true;
		}
		else if (opt_match(pArg, "-uastc_rdo_m"))
		{
			m_comp_params.m_rdo_uastc_ldr_4x4_multithreading = false;
			return true;
		}
		else if (opt_match(pArg, "-validate_etc1s"))
		{
			m_comp_params.m_validate_etc1s = true;
			return true;
		}
		else if (opt_match(pArg, "-comp_level"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_etc1s_compression_level = atoi(arg_v[arg_index + 1]);

			m_used_old_style_codec_config_param = true;

			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-max_endpoints"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_etc1s_max_endpoint_clusters = clamp<int>(atoi(arg_v[arg_index + 1]), 1, BASISU_MAX_ENDPOINT_CLUSTERS);

			m_used_old_style_codec_config_param = true;

			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-max_selectors"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_etc1s_max_selector_clusters = clamp<int>(atoi(arg_v[arg_index + 1]), 1, BASISU_MAX_SELECTOR_CLUSTERS);

			m_used_old_style_codec_config_param = true;

			arg_count++;
			return true;
		}
#if 0
		else if (opt_match(pArg, "-gen_global_codebooks"))
		{
			// TODO
		}
#endif
		else if (opt_match(pArg, "-use_global_codebooks"))
		{
			REMAINING_ARGS_CHECK(1);
			m_etc1s_use_global_codebooks_file = arg_v[arg_index + 1];
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-etc1_only"))
		{
			m_etc1_only = true;
			m_unpack_format_only = (int)basist::transcoder_texture_format::cTFETC1_RGB;
			return true;
		}
		else if (opt_match(pArg, "-disable_hierarchical_endpoint_codebooks"))
		{
			m_comp_params.m_disable_hierarchical_endpoint_codebooks = true;
			return true;
		}
		else if (opt_match(pArg, "-q")) // old-style ETC1S -q X option, prefer -quality instead
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_quality_level = clamp<int>(atoi(arg_v[arg_index + 1]), BASISU_QUALITY_MIN, BASISU_QUALITY_MAX);

			m_used_old_style_codec_config_param = true;

			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-no_selector_rdo"))
		{
			m_comp_params.m_no_selector_rdo = true;
			return true;
		}
		else if (opt_match(pArg, "-selector_rdo_thresh"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_selector_rdo_thresh = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}
		else if (opt_match(pArg, "-no_endpoint_rdo"))
		{
			m_comp_params.m_no_endpoint_rdo = true;
			return true;
		}
		else if (opt_match(pArg, "-endpoint_rdo_thresh"))
		{
			REMAINING_ARGS_CHECK(1);
			m_comp_params.m_endpoint_rdo_thresh = (float)atof(arg_v[arg_index + 1]);
			arg_count++;
			return true;
		}

		return false;
	}

public:
	command_line_params() :
		m_mode(cDefault),
		m_benchmark_fmt(basist::basis_tex_format::cXUASTC_LDR_6x6),
		m_benchmark_effort(0),
		m_benchmark_quality(100),
		m_benchmark_path("../test_files/kodim"),
		m_benchmark_first(1),
		m_benchmark_last(24),
		m_benchmark_path_set(false),
		m_benchmark_range_set(false),
		m_benchmark_linear(false),
		m_benchmark_xubc7_encoder(0),
		m_benchmark_xuastc_profile(0),
		m_benchmark_sweep_block_set(false),
		m_benchmark_sweep_block_lo(0), m_benchmark_sweep_block_hi(0),
		m_benchmark_sweep_effort_lo(3), m_benchmark_sweep_effort_hi(3),
		m_benchmark_sweep_quality_lo(10), m_benchmark_sweep_quality_hi(10),
		m_benchmark_sweep_csv("benchmark_sweep.csv"),
		m_ktx2_mode(true),
		m_ktx2_zstandard(true),
		m_ktx2_zstandard_level(6),
		m_ktx2_animdata_duration(1),
		m_ktx2_animdata_timescale(15),
		m_ktx2_animdata_loopcount(0),
		m_unpack_format_only(-1),
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
		m_dump_image_stats(false),
		m_dump_pixel_x(-1),
		m_dump_pixel_y(-1),
		m_dump_block_x(-1),
		m_dump_block_y(-1),
		m_stats_region_x(-1),
		m_stats_region_y(-1),
		m_stats_region_w(-1),
		m_stats_region_h(-1),
		m_extract_channel(-1),
		m_extract_region_x(-1),
		m_extract_region_y(-1),
		m_extract_region_w(-1),
		m_extract_region_h(-1),
		m_parallel_compression(false),
		m_tonemap_dither_flag(false),
		m_xuastc_ldr_disable_bc7_transcoding(false),
		m_no_etc1s_transcoding_chroma_filtering(false),
		m_higher_quality_transcoding(false), 
		m_transcode_force_deblocking(false), 
		m_transcode_disable_deblocking(false), 
		m_effort_level(-1),
		m_quality_level(-1),
		m_used_old_style_codec_config_param(false)
	{
		// This command line tool defaults to ETC1S level 1, not 2 which is the API default (for backwards compat).
		m_comp_params.m_etc1s_compression_level = maximum<int>((int)BASISU_DEFAULT_ETC1S_COMPRESSION_LEVEL - 1, 0);
		
		m_comp_params.m_uastc_hdr_4x4_options.set_quality_level(uastc_hdr_4x4_codec_options::cDefaultLevel);

		// Default to sRGB colorspace metrics/transfer functions (independent of the code defaults).
		m_comp_params.m_perceptual = true;
		m_comp_params.m_ktx2_and_basis_srgb_transfer_function = true;

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

			if (opt_match(pArg, {"-help", "--help"}))
			{
				print_usage();
				exit(EXIT_SUCCESS);
			}
			
			if (check_for_etc1s_or_uastc_options(arg_v, pArg, arg_index, num_remaining_args, arg_count))
			{
			}
			else if (check_for_hdr_options(arg_v, pArg, arg_index, num_remaining_args, arg_count))
			{
			}
			else if (check_for_xuastc_options(arg_v, pArg, arg_index, num_remaining_args, arg_count))
			{
			}
			else if (check_for_general_options(arg_v, pArg, arg_index, num_remaining_args, arg_count))
			{
			}
			else if (check_for_dds_options(arg_v, pArg, arg_index, num_remaining_args, arg_count))
			{
			}
			else if (opt_match(pArg, "-ktx2"))
			{
				m_ktx2_mode = true;
			}
			else if (opt_match(pArg, "-basis"))
			{
				m_ktx2_mode = false;
			}
			else if (opt_match(pArg, "-ktx2_no_zstandard"))
			{
				m_ktx2_zstandard = false;
			}
			else if (opt_match(pArg, "-ktx2_zstandard_level"))
			{
				REMAINING_ARGS_CHECK(1);
				m_ktx2_zstandard_level = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (opt_match(pArg, "-ktx2_animdata_duration"))
			{
				REMAINING_ARGS_CHECK(1);
				m_ktx2_animdata_duration = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (opt_match(pArg, "-ktx2_animdata_timescale"))
			{
				REMAINING_ARGS_CHECK(1);
				m_ktx2_animdata_timescale = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (opt_match(pArg, "-ktx2_animdata_loopcount"))
			{
				REMAINING_ARGS_CHECK(1);
				m_ktx2_animdata_loopcount = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (opt_match(pArg, "-compress"))
				m_mode = cCompress;
			else if (opt_match(pArg, "-compare"))
				m_mode = cCompare;
			else if (opt_match(pArg, "-compare_hvs"))
				m_mode = cCompareHVS;
			else if (opt_match(pArg, {"-hdr_compare", "-compare_hdr"}))
				m_mode = cHDRCompare;
			else if (opt_match(pArg, "-dump_image_stats"))
			{
				m_mode = cImageDumpStats;
				m_dump_image_stats = true;

				// Optional region: -dump_image_stats <x> <y> <w> <h>. If the next argument looks like a
				// number then all four must follow; otherwise the stats cover the whole image.
				if ((num_remaining_args >= 1) && arg_is_unsigned_int(arg_v[arg_index + 1]))
				{
					REMAINING_ARGS_CHECK(4);
					m_stats_region_x = atoi(arg_v[arg_index + 1]);
					m_stats_region_y = atoi(arg_v[arg_index + 2]);
					m_stats_region_w = atoi(arg_v[arg_index + 3]);
					m_stats_region_h = atoi(arg_v[arg_index + 4]);
					arg_count += 4;
					if ((m_stats_region_x < 0) || (m_stats_region_y < 0) || (m_stats_region_w <= 0) || (m_stats_region_h <= 0))
					{
						error_printf("-dump_image_stats region requires non-negative x,y and positive w,h: -dump_image_stats <x> <y> <w> <h>\n");
						return false;
					}
				}
			}
			else if (opt_match(pArg, "-dump_pixel"))
			{
				REMAINING_ARGS_CHECK(2);
				m_mode = cImageDumpStats;
				m_dump_pixel_x = atoi(arg_v[arg_index + 1]);
				m_dump_pixel_y = atoi(arg_v[arg_index + 2]);
				arg_count += 2;
				if ((m_dump_pixel_x < 0) || (m_dump_pixel_y < 0))
				{
					error_printf("-dump_pixel requires two non-negative integer coordinates: -dump_pixel <x> <y>\n");
					return false;
				}
			}
			else if (opt_match(pArg, "-dump_block"))
			{
				REMAINING_ARGS_CHECK(2);
				m_mode = cImageDumpStats;
				m_dump_block_x = atoi(arg_v[arg_index + 1]);
				m_dump_block_y = atoi(arg_v[arg_index + 2]);
				arg_count += 2;
				if ((m_dump_block_x < 0) || (m_dump_block_y < 0))
				{
					error_printf("-dump_block requires two non-negative integer block coordinates: -dump_block <bx> <by>\n");
					return false;
				}
			}
			else if (opt_match(pArg, "-split"))
				m_mode = cSplitImage;
			else if (opt_match(pArg, "-combine"))
				m_mode = cCombineImages;
			else if (opt_match(pArg, "-extract_channel"))
			{
				REMAINING_ARGS_CHECK(2);
				m_mode = cExtractChannel;

				// Channel: r/g/b/a (case-insensitive) or 0..3. Always assign (don't leave a stale value from a prior -extract_channel).
				const char* pCh = arg_v[arg_index + 1];
				m_extract_channel = ((pCh[0] != '\0') && (pCh[1] == '\0')) ? channel_token_to_index(pCh[0]) : -1;
				if (m_extract_channel < 0)
				{
					error_printf("-extract_channel requires a channel (r/g/b/a or 0..3): -extract_channel <c> <out.png> [x y w h]\n");
					return false;
				}

				m_extract_filename = arg_v[arg_index + 2];
				arg_count += 2;

				// Optional region: -extract_channel <c> <out.png> <x> <y> <w> <h>.
				if ((num_remaining_args >= 3) && arg_is_unsigned_int(arg_v[arg_index + 3]))
				{
					REMAINING_ARGS_CHECK(6);
					m_extract_region_x = atoi(arg_v[arg_index + 3]);
					m_extract_region_y = atoi(arg_v[arg_index + 4]);
					m_extract_region_w = atoi(arg_v[arg_index + 5]);
					m_extract_region_h = atoi(arg_v[arg_index + 6]);
					arg_count += 4;
					if ((m_extract_region_x < 0) || (m_extract_region_y < 0) || (m_extract_region_w <= 0) || (m_extract_region_h <= 0))
					{
						error_printf("-extract_channel region requires non-negative x,y and positive w,h: -extract_channel <c> <out.png> <x> <y> <w> <h>\n");
						return false;
					}
				}
			}
			else if (opt_match(pArg, "-extract_swizzle"))
			{
				REMAINING_ARGS_CHECK(2);
				m_mode = cExtractSwizzle;

				// Swizzle: comma-separated source channels for output R,G,B,A, e.g. "0,1,2,3" (identity) or "r,g,b,a" or "2,1,0".
				// Each token selects a source channel in [0,3]; 1..4 tokens. Any output channel not listed passes through unchanged.
				const char* p = arg_v[arg_index + 1];
				int count = 0;
				bool swizzle_ok = true;
				while (*p)
				{
					const int idx = channel_token_to_index(*p);
					if ((idx < 0) || (count >= 4))
					{
						swizzle_ok = false;
						break;
					}
					m_extract_swizzle[count++] = idx;
					p++;
					if (*p == ',')
					{
						p++;
						if (*p == '\0') { swizzle_ok = false; break; }	// trailing comma
					}
					else if (*p != '\0')
					{
						swizzle_ok = false;	// token longer than a single channel char
						break;
					}
				}
				if (!swizzle_ok || (count == 0))
				{
					error_printf("-extract_swizzle requires 1 to 4 comma-separated source channels (each r/g/b/a or 0..3), e.g. \"0,1,2,3\"\n");
					return false;
				}
				m_extract_swizzle_count = count;

				m_extract_filename = arg_v[arg_index + 2];
				arg_count += 2;

				// Optional region: -extract_swizzle <swizzle> <out.png> <x> <y> <w> <h>.
				if ((num_remaining_args >= 3) && arg_is_unsigned_int(arg_v[arg_index + 3]))
				{
					REMAINING_ARGS_CHECK(6);
					m_extract_region_x = atoi(arg_v[arg_index + 3]);
					m_extract_region_y = atoi(arg_v[arg_index + 4]);
					m_extract_region_w = atoi(arg_v[arg_index + 5]);
					m_extract_region_h = atoi(arg_v[arg_index + 6]);
					arg_count += 4;
					if ((m_extract_region_x < 0) || (m_extract_region_y < 0) || (m_extract_region_w <= 0) || (m_extract_region_h <= 0))
					{
						error_printf("-extract_swizzle region requires non-negative x,y and positive w,h: -extract_swizzle <swizzle> <out.png> <x> <y> <w> <h>\n");
						return false;
					}
				}
			}
			else if (opt_match(pArg, "-extract_region"))
			{
				REMAINING_ARGS_CHECK(5);
				m_mode = cExtractRegion;
				m_extract_filename = arg_v[arg_index + 1];
				m_extract_region_x = atoi(arg_v[arg_index + 2]);
				m_extract_region_y = atoi(arg_v[arg_index + 3]);
				m_extract_region_w = atoi(arg_v[arg_index + 4]);
				m_extract_region_h = atoi(arg_v[arg_index + 5]);
				arg_count += 5;
				if ((m_extract_region_x < 0) || (m_extract_region_y < 0) || (m_extract_region_w <= 0) || (m_extract_region_h <= 0))
				{
					error_printf("-extract_region requires non-negative x,y and positive w,h: -extract_region <out.png> <x> <y> <w> <h>\n");
					return false;
				}
			}
			// Optional color transforms applied by the -extract_* modes (modifiers; order vs. the -extract_* option does not matter).
			else if (opt_match(pArg, "-extract_to_linear"))
				m_extract_color_xform = cExtractXformToLinear;
			else if (opt_match(pArg, "-extract_to_srgb"))
				m_extract_color_xform = cExtractXformToSRGB;
			else if (opt_match(pArg, "-extract_xform_alpha"))
				m_extract_xform_alpha = true;
			else if (opt_match(pArg, "-text_to_png"))
			{
				REMAINING_ARGS_CHECK(2);
				m_mode = cTextToPng;
				m_text_image_in = arg_v[arg_index + 1];
				m_text_image_out = arg_v[arg_index + 2];
				arg_count += 2;
			}
			else if (opt_match(pArg, "-png_to_text"))
			{
				REMAINING_ARGS_CHECK(2);
				m_mode = cPngToText;
				m_text_image_in = arg_v[arg_index + 1];
				m_text_image_out = arg_v[arg_index + 2];
				arg_count += 2;
			}
			else if (opt_match(pArg, "-tonemap"))
				m_mode = cTonemapImage;
			else if (opt_match(pArg, "-export_dds"))
			{
				REMAINING_ARGS_CHECK(1);
				m_mode = cExportDDS;
				m_export_dds_format = arg_v[arg_index + 1];
				arg_count++;
			}
			else if (opt_match(pArg, "-export_ktx"))
			{
				REMAINING_ARGS_CHECK(1);
				m_mode = cExportKTX;
				m_export_ktx_format = arg_v[arg_index + 1];
				arg_count++;
			}
			// -benchmark_single <FORMAT> <EFFORT> <QUALITY>: developer codec_benchmark() single run (not in help text yet).
			// FORMAT is any codec name with underscores, e.g. XUASTC_LDR_4x4..12x12, XUBC7, ETC1S, ASTC_LDR_6x6, UASTC_LDR_4x4.
			else if (opt_match(pArg, "-benchmark_single"))
			{
				REMAINING_ARGS_CHECK(3);
				m_mode = cBenchmarkSingle;
				if (!parse_benchmark_tex_format(arg_v[arg_index + 1], m_benchmark_fmt))
				{
					error_printf("-benchmark_single: unrecognized format \"%s\"\n", arg_v[arg_index + 1]);
					return false;
				}
				// Clamp to the same ranges as -effort [0,10] and -quality [1,100] (matches lines above); also keeps the cast non-negative.
				m_benchmark_effort = (uint32_t)clamp<int>(atoi(arg_v[arg_index + 2]), 0, 10);
				m_benchmark_quality = (uint32_t)clamp<int>(atoi(arg_v[arg_index + 3]), 1, 100);
				arg_count += 3;
			}
			// Optional overrides for -benchmark_single's base path and file index range (defaults: "../test_files/kodim", 1..24).
			else if (opt_match(pArg, "-benchmark_path"))
			{
				REMAINING_ARGS_CHECK(1);
				m_benchmark_path = arg_v[arg_index + 1];
				m_benchmark_path_set = true;
				arg_count++;
			}
			else if (opt_match(pArg, "-benchmark_range"))
			{
				REMAINING_ARGS_CHECK(2);
				m_benchmark_first = (uint32_t)clamp<int>(atoi(arg_v[arg_index + 1]), 0, 1000000);
				m_benchmark_last = (uint32_t)clamp<int>(atoi(arg_v[arg_index + 2]), 0, 1000000);
				if (m_benchmark_last < m_benchmark_first) // keep first<=last so codec_benchmark's (last-first+1) can't underflow
					std::swap(m_benchmark_first, m_benchmark_last);
				m_benchmark_range_set = true;
				arg_count += 2;
			}
			else if (opt_match(pArg, "-benchmark_linear"))
				m_benchmark_linear = true;
			// XUBC7-only BC7 base encoder: 0 = bc7f (default), 1-7 = bc7e_scalar level 0-6 (ignored for non-XUBC7 formats).
			else if (opt_match(pArg, "-benchmark_xubc7_encoder"))
			{
				REMAINING_ARGS_CHECK(1);
				m_benchmark_xubc7_encoder = (uint32_t)clamp<int>(atoi(arg_v[arg_index + 1]), 0, (int)basisu::cFlagXUBC7BaseEncoderMask);
				arg_count++;
			}
			// XUASTC LDR entropy syntax/profile: 0 = full arith (default), 1 = hybrid, 2 = full zstd (ignored for non-XUASTC formats).
			else if (opt_match(pArg, "-benchmark_xuastc_profile"))
			{
				REMAINING_ARGS_CHECK(1);
				m_benchmark_xuastc_profile = (uint32_t)clamp<int>(atoi(arg_v[arg_index + 1]), 0, (int)basist::astc_ldr_t::xuastc_ldr_syntax::cTotal - 1);
				arg_count++;
			}
			// -benchmark_sweep <FORMAT>: sweep block size/effort/quality for one codec, dumping a CSV (see benchmark_sweep_mode()).
			else if (opt_match(pArg, "-benchmark_sweep"))
			{
				REMAINING_ARGS_CHECK(1);
				m_mode = cBenchmarkSweep;
				if (!parse_benchmark_tex_format(arg_v[arg_index + 1], m_benchmark_fmt))
				{
					error_printf("-benchmark_sweep: unrecognized format \"%s\"\n", arg_v[arg_index + 1]);
					return false;
				}
				arg_count++;
			}
			// -benchmark_sweep axis ranges (inclusive low/high). Block index [0,5] (4x4..12x12 squares); effort [0,10]; quality [0,10] (x10 -> [1,100]).
			else if (opt_match(pArg, "-benchmark_sweep_block"))
			{
				REMAINING_ARGS_CHECK(2);
				m_benchmark_sweep_block_set = true;
				m_benchmark_sweep_block_lo = (uint32_t)clamp<int>(atoi(arg_v[arg_index + 1]), 0, 5);
				m_benchmark_sweep_block_hi = (uint32_t)clamp<int>(atoi(arg_v[arg_index + 2]), 0, 5);
				arg_count += 2;
			}
			else if (opt_match(pArg, "-benchmark_sweep_effort"))
			{
				REMAINING_ARGS_CHECK(2);
				m_benchmark_sweep_effort_lo = (uint32_t)clamp<int>(atoi(arg_v[arg_index + 1]), 0, 10);
				m_benchmark_sweep_effort_hi = (uint32_t)clamp<int>(atoi(arg_v[arg_index + 2]), 0, 10);
				arg_count += 2;
			}
			else if (opt_match(pArg, "-benchmark_sweep_quality"))
			{
				REMAINING_ARGS_CHECK(2);
				m_benchmark_sweep_quality_lo = (uint32_t)clamp<int>(atoi(arg_v[arg_index + 1]), 0, 10);
				m_benchmark_sweep_quality_hi = (uint32_t)clamp<int>(atoi(arg_v[arg_index + 2]), 0, 10);
				arg_count += 2;
			}
			else if (opt_match(pArg, "-benchmark_sweep_csv"))
			{
				REMAINING_ARGS_CHECK(1);
				m_benchmark_sweep_csv = arg_v[arg_index + 1];
				arg_count++;
			}
			else if (opt_match(pArg, "-tinydds_info"))	// opt_match also accepts the "--tinydds_info" form
				m_mode = cTinyDDSInfo;
			else if (opt_match(pArg, "-ktx_info"))
				m_mode = cKTXInfo;
			else if (opt_match(pArg, "-unpack"))
				m_mode = cUnpack;
			else if (opt_match(pArg, "-validate"))
				m_mode = cValidate;
			else if (opt_match(pArg, "-info"))
				m_mode = cInfo;
			else if (opt_match(pArg, {"-version", "--version"}))
				m_mode = cVersion;
			else if (opt_match(pArg, "-compare_ssim"))
				m_compare_ssim = true;
			else if (opt_match(pArg, "-compare_plot"))
				m_compare_plot = true;
			else if (opt_match(pArg, "-bench"))
				m_mode = cBench;
			else if (opt_match(pArg, "-comp_size"))
				m_mode = cCompSize;
			else if (opt_match(pArg, {"-test", "-test_ldr"}))
				m_mode = cTestLDR;
			else if (opt_match(pArg, {"-test_xuastc", "-test_xuastc_ldr"}))
				m_mode = cTestXUASTCLDR;
			else if (opt_match(pArg, {"-test_xuastc_dump", "-test_xuastc_ldr_dump"}))
			{
				// Run the XUASTC LDR test across all block sizes and print the expected-value
				// tables (for copy/paste into the code) instead of validating.
				m_mode = cTestXUASTCLDR;
				m_xuastc_test_dump = true;
			}
			else if (opt_match(pArg, "-test_hdr_4x4"))
				m_mode = cTestHDR_4x4;
			else if (opt_match(pArg, "-test_hdr_6x6"))
				m_mode = cTestHDR_6x6;
			else if (opt_match(pArg, "-test_hdr_6x6i"))
				m_mode = cTestHDR_6x6i;
			else if (opt_match(pArg, "-test_codecs"))
			{
				// Sweep all LDR+HDR codecs vs the golden .inl table. Optional trailing codec
				// filter (not beginning with '-'), e.g. "-test_codecs XUASTC".
				m_mode = cTestCodecs;
				if ((num_remaining_args >= 1) && (arg_v[arg_index + 1][0] != '-'))
				{
					m_codec_filter = std::string(arg_v[arg_index + 1]);
					arg_count++;
				}
			}
			else if (opt_match(pArg, "-test_codecs_gen"))
			{
				// (Re)generate the golden .inl table. Optional trailing output filename, then an
				// optional codec filter, both not beginning with '-'.
				m_mode = cTestCodecsGen;
				if ((num_remaining_args >= 1) && (arg_v[arg_index + 1][0] != '-'))
				{
					m_test_codecs_gen_filename = std::string(arg_v[arg_index + 1]);
					arg_count++;

					if ((num_remaining_args >= 2) && (arg_v[arg_index + 2][0] != '-'))
					{
						m_codec_filter = std::string(arg_v[arg_index + 2]);
						arg_count++;
					}
				}
			}
			else if (opt_match(pArg, "-clbench"))
				m_mode = cCLBench;
			else if (opt_match(pArg, "-test_dir"))
			{
				REMAINING_ARGS_CHECK(1);
				m_test_file_dir = std::string(arg_v[arg_index + 1]);
				m_test_dir_explicit = true;
				arg_count++;
			}
			else if (opt_match(pArg, "-no_sse"))
			{
#if BASISU_SUPPORT_SSE
				g_cpu_supports_sse41 = false;
#endif
			}
			else if (opt_match(pArg, {"-no_status_output", "-quiet"}))
			{
				m_comp_params.m_status_output = false;
			}
			else if (opt_match(pArg, "-file"))
			{
				REMAINING_ARGS_CHECK(1);
				m_input_filenames.push_back(std::string(arg_v[arg_index + 1]));
				arg_count++;
			}
			else if (opt_match(pArg, "-alpha_file"))
			{
				REMAINING_ARGS_CHECK(1);
				m_input_alpha_filenames.push_back(std::string(arg_v[arg_index + 1]));
				arg_count++;
			}
			else if (opt_match(pArg, "-multifile_printf"))
			{
				REMAINING_ARGS_CHECK(1);
				m_multifile_printf = std::string(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (opt_match(pArg, "-multifile_first"))
			{
				REMAINING_ARGS_CHECK(1);
				m_multifile_first = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (opt_match(pArg, "-multifile_num"))
			{
				REMAINING_ARGS_CHECK(1);
				m_multifile_num = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (opt_match(pArg, "-resample"))
			{
				REMAINING_ARGS_CHECK(2);
				m_comp_params.m_resample_width = atoi(arg_v[arg_index + 1]);
				m_comp_params.m_resample_height = atoi(arg_v[arg_index + 2]);
				arg_count += 2;
			}
			else if (opt_match(pArg, "-resample_factor"))
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_resample_factor = (float)atof(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (opt_match(pArg, { "-output_file", "-out_file" }))
			{
				REMAINING_ARGS_CHECK(1);
				m_output_filename = arg_v[arg_index + 1];
				arg_count++;
			}
			else if (opt_match(pArg, { "-output_path", "-out_path" } ))
			{
				REMAINING_ARGS_CHECK(1);
				m_output_path = arg_v[arg_index + 1];
				arg_count++;
			}
			else if (opt_match(pArg, "-debug"))
			{
				m_comp_params.m_debug = true;
				enable_debug_printf(true);
			}
			else if (opt_match(pArg, "-verbose"))
			{
				// -verbose is shorthand for -debug -stats
				m_comp_params.m_debug = true;
				enable_debug_printf(true);
				m_comp_params.m_compute_stats = true;
				m_comp_params.m_psnr_hvs_m_stats = true;
			}
			else if (opt_match(pArg, "-validate_output"))
			{
				m_comp_params.m_validate_output_data = true;
			}
			else if (opt_match(pArg, "-debug_images"))
				m_comp_params.m_debug_images = true;
			else if (opt_match(pArg, {"-stats", "-image_stats"}))
			{
				m_comp_params.m_compute_stats = true;
				m_comp_params.m_psnr_hvs_m_stats = true;
			}
			else if (opt_match(pArg, "-no_stats"))
			{
				// Turn stats back off (e.g. to override -verbose or -csv_file, which enable them).
				m_comp_params.m_compute_stats = false;
				m_comp_params.m_psnr_hvs_m_stats = false;
			}
			else if (opt_match(pArg, "-y_flip"))
				m_comp_params.m_y_flip = true;
			else if (opt_match(pArg, {"-normal_map", "-texture"}))
			{
				// Normal map/plain texture preset - disable all fancy things, linear weights, no sharpening largest blocks, no deblocking awareness.
				m_comp_params.set_srgb_options(false);
								
				m_comp_params.m_no_selector_rdo = true;
				m_comp_params.m_no_endpoint_rdo = true;

				m_comp_params.set_xuastc_ldr_srgb_channel_weights(false);

				m_comp_params.m_xuastc_ldr_sharpen_mode = (int)xuastc_ldr_sharpen_mode::cDisabled;
				m_comp_params.m_xuastc_ldr_deblocking_mode = (int)xuastc_ldr_deblocking_mode::cDisabled;

				// TODO: Once we support some sort of angular metric, enable them for -normal_map.
			}
			else if (opt_match(pArg, "-photo"))
			{
				// Photo preset (the codec's defaults, opposite of -normal_map/-texture)
				m_comp_params.set_srgb_options(true);

				// -normal_map sets these to true, this preset sets them to false
				m_comp_params.m_no_selector_rdo = false;
				m_comp_params.m_no_endpoint_rdo = false;

				m_comp_params.set_xuastc_ldr_srgb_channel_weights(true);

				m_comp_params.m_xuastc_ldr_sharpen_mode = (int)xuastc_ldr_sharpen_mode::cDisabled;
				m_comp_params.m_xuastc_ldr_deblocking_mode = (int)xuastc_ldr_deblocking_mode::cUseSCDAndFilteringOnlyLargestBlocks;
			}
			else if (opt_match(pArg, "-linear"))
			{
				// linear preset (opposite of -srgb)
				m_comp_params.set_srgb_options(false);

				m_comp_params.set_xuastc_ldr_srgb_channel_weights(false);
			}
			else if (opt_match(pArg, "-srgb"))
			{
				// sRGB preset (opposite of -linear)
				m_comp_params.set_srgb_options(true);

				m_comp_params.set_xuastc_ldr_srgb_channel_weights(true);
			}
			else if (opt_match(pArg, "-no_alpha"))
				m_comp_params.m_check_for_alpha = false;
			else if (opt_match(pArg, "-force_alpha"))
				m_comp_params.m_force_alpha = true;
			else if (opt_match(pArg, {"-separate_rg_to_color_alpha", "-seperate_rg_to_color_alpha"})) // was mispelled for a while - whoops!
			{
				m_comp_params.m_swizzle[0] = 0;
				m_comp_params.m_swizzle[1] = 0;
				m_comp_params.m_swizzle[2] = 0;
				m_comp_params.m_swizzle[3] = 1;
			}
			else if (opt_match(pArg, "-swizzle"))
			{
				REMAINING_ARGS_CHECK(1);
				const char *swizzle = arg_v[arg_index + 1];
				if (strlen(swizzle) != 4)
				{
					error_printf("Swizzle requires exactly 4 characters (each one of [rgba] or [0123])\n");
					return false;
				}
				for (int i=0; i<4; ++i)
				{
					if ((tolower(swizzle[i]) == 'r') || (swizzle[i] == '0'))
						m_comp_params.m_swizzle[i] = 0;
					else if ((tolower(swizzle[i]) == 'g') || (swizzle[i] == '1'))
						m_comp_params.m_swizzle[i] = 1;
					else if ((tolower(swizzle[i]) == 'b') || (swizzle[i] == '2'))
						m_comp_params.m_swizzle[i] = 2;
					else if ((tolower(swizzle[i]) == 'a') || (swizzle[i] == '3'))
						m_comp_params.m_swizzle[i] = 3;
					else
					{
						error_printf("Swizzle must be one of [rgba] or [0123]");
						return false;
					}
				}
				arg_count++;
			}
			else if (opt_match(pArg, "-renorm"))
				m_comp_params.m_renormalize = true;
			else if (opt_match(pArg, {"-no_multithreading", "-no_threading", "-no_threads"}))
			{
				m_comp_params.m_multithreading = false;
			}
			else if (opt_match(pArg, "-parallel"))
			{
				m_parallel_compression = true;
			}
			else if (opt_match(pArg, "-max_threads"))
			{
				REMAINING_ARGS_CHECK(1);
				m_max_threads = maximum(1, atoi(arg_v[arg_index + 1]));
				arg_count++;
			}
			else if (opt_match(pArg, "-mipmap"))
				m_comp_params.m_mip_gen = true;
			else if (opt_match(pArg, {"-no_ktx", "-no_dds"}))
				m_no_ktx = true;
			else if (opt_match(pArg, {"-ktx_only", "-dds_only"}))
				m_ktx_only = true;
			else if (opt_match(pArg, "-write_out"))
				m_write_out = true;
			else if (opt_match(pArg, "-tonemap_dither"))
				m_tonemap_dither_flag = true;
			else if (opt_match(pArg, "-format_only"))
			{
				REMAINING_ARGS_CHECK(1);
				m_unpack_format_only = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (opt_match(pArg, "-opencl"))
			{
				m_comp_params.m_use_opencl = true;
			}
			else if (opt_match(pArg, "-opencl_serialize"))
			{
			}
			else if (opt_match(pArg, "-mip_scale"))
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_mip_scale = (float)atof(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (opt_match(pArg, "-mip_filter"))
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_mip_filter = arg_v[arg_index + 1];
				// TODO: Check filter 
				arg_count++;
			}
			else if (opt_match(pArg, "-mip_renorm"))
				m_comp_params.m_mip_renormalize = true;
			else if (opt_match(pArg, "-mip_clamp"))
				m_comp_params.m_mip_wrapping = false;
			else if (opt_match(pArg, "-mip_fast"))
				m_comp_params.m_mip_fast = true;
			else if (opt_match(pArg, "-mip_slow"))
				m_comp_params.m_mip_fast = false;
			else if (opt_match(pArg, "-mip_smallest"))
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_mip_smallest_dimension = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (opt_match(pArg, "-mip_srgb"))
				m_comp_params.m_mip_srgb = true;
			else if (opt_match(pArg, "-mip_linear"))
				m_comp_params.m_mip_srgb = false;
			else if (opt_match(pArg, "-userdata0"))
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_userdata0 = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (opt_match(pArg, "-userdata1"))
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_userdata1 = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (opt_match(pArg, "-framerate"))
			{
				REMAINING_ARGS_CHECK(1);
				double fps = atof(arg_v[arg_index + 1]);
				double us_per_frame = 0;
				if (fps > 0)
					us_per_frame = 1000000.0f / fps;

				m_comp_params.m_us_per_frame = clamp<int>(static_cast<int>(us_per_frame + .5f), 0, basist::cBASISMaxUSPerFrame);
				arg_count++;
			}
			else if (opt_match(pArg, "-cubemap"))
			{
				m_comp_params.m_tex_type = basist::cBASISTexTypeCubemapArray;
				m_individual = false;
			}
			else if (opt_match(pArg, "-tex_type"))
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
			else if (opt_match(pArg, "-individual"))
				m_individual = true;
			else if (opt_match(pArg, {"-tex_array", "-texarray"}))
				m_individual = false;
			else if (opt_match(pArg, "-fuzz_testing"))
				m_fuzz_testing = true;
			else if (opt_match(pArg, "-csv_file"))
			{
				REMAINING_ARGS_CHECK(1);
				m_csv_file = arg_v[arg_index + 1];
				m_comp_params.m_compute_stats = true;
				m_comp_params.m_psnr_hvs_m_stats = true;

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

		const bool lossy_supercompression_changed = m_comp_params.m_xuastc_ldr_use_lossy_supercompression.was_changed();
		const bool lossy_supercompression_value = m_comp_params.m_xuastc_ldr_use_lossy_supercompression;
						
		if (m_comp_params.m_quality_level != -1) // they set the old-style "-q X" option, which is the legacy ETC1S-specific quality level which ranges from [1,255]
		{
			m_comp_params.m_etc1s_max_endpoint_clusters = 0;
			m_comp_params.m_etc1s_max_selector_clusters = 0;
						
			if (basis_tex_format_is_xuastc_ldr(m_comp_params.get_format_mode()))
			{
				printf("WARNING: Using \"-q [1,255]\" is not recommended for XUASTC LDR, instead use \"-quality [1,100]\"\n");

				// -q also controls XUASTC LDR/XUBC7 weight grid DCT quality level
				if (m_comp_params.m_quality_level < 100)
					m_comp_params.m_xuastc_ldr_use_dct = true;

				// Automatically enable lossy XUASTC supercompression if DCT is enabled.
				if ((!lossy_supercompression_changed) && (m_comp_params.m_quality_level < 100))
					m_comp_params.m_xuastc_ldr_use_lossy_supercompression = true;
			}
			else if (basis_tex_format_is_xubc7(m_comp_params.get_format_mode()))
			{
				printf("WARNING: Using \"-q [1,255]\" is not recommended for XUBC7, instead use \"-quality [1,100]\"\n");
			}
		}
		else if ((!m_comp_params.m_etc1s_max_endpoint_clusters) || (!m_comp_params.m_etc1s_max_selector_clusters))
		{
			m_comp_params.m_etc1s_max_endpoint_clusters = 0;
			m_comp_params.m_etc1s_max_selector_clusters = 0;

			// Don't slam to 128 unless it's ETC1S
			if (m_comp_params.get_format_mode() == basist::basis_tex_format::cETC1S)
				m_comp_params.m_quality_level = 128;
		}
		
		// Ensure mip_srgb is set to match the perceptual flag if the user didn't explicitly set it.
		if (!m_comp_params.m_mip_srgb.was_changed())
		{
			// They didn't specify what colorspace to do mipmap filtering in, so choose sRGB if they've specified that the texture is sRGB.
			// 4/26/2026: m_mip_srgb now defaults to true, unsure if this is needed now.
			if (m_comp_params.m_perceptual)
				m_comp_params.m_mip_srgb = true;
			else
				m_comp_params.m_mip_srgb = false;
		}

		// Handle new-style unified effort and quality levels across all codecs.
		// We have so many codecs now that it's necessary to unify the primary quality/effort controls otherwise it's too confusing.
		// If they've specified either -effort or -quality, assume they want the new unified API.
		// If they haven't specified either, they get the old parameters/options.
		if ((m_effort_level != -1) || (m_quality_level != -1))
		{
			if (m_used_old_style_codec_config_param)
			{
				fmt_printf("WARNING: Mixing old and new-style (-effort and/or -quality) codec configuration parameters.\nNew-style parameters may overwrite your old-style codec configuration settings.\nPrefer using -effort X and -quality X.\n");
			}
						
			// Set the new-style effort/quality level, but importantly don't override any settings already changed if they haven't explictly specified -effort or -quality.
			m_comp_params.set_format_mode_and_quality_effort(m_comp_params.get_format_mode(), m_quality_level, m_effort_level, false);

			// Allow the user to override the lossy supercompression setting, independent of the quality/effort levels.
			if (lossy_supercompression_changed)
			{
				m_comp_params.m_xuastc_ldr_use_lossy_supercompression = lossy_supercompression_value;
			}
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

	// -benchmark_single <FORMAT> <EFFORT> <QUALITY> parameters (cBenchmarkSingle).
	basist::basis_tex_format m_benchmark_fmt;
	uint32_t m_benchmark_effort;
	uint32_t m_benchmark_quality;
	// Optional overrides for the codec_benchmark() base path and file index range: -benchmark_path, -benchmark_range.
	// The _set flags record whether the user explicitly overrode them (so HDR can fall back to its own default set).
	std::string m_benchmark_path;
	uint32_t m_benchmark_first;
	uint32_t m_benchmark_last;
	bool m_benchmark_path_set;
	bool m_benchmark_range_set;
	// -benchmark_linear: use linear (not sRGB/perceptual) metrics for the benchmark. Default is sRGB.
	bool m_benchmark_linear;
	// -benchmark_xubc7_encoder [0,7]: XUBC7-only BC7 base encoder selection. 0 = bc7f (default), 1-7 = bc7e_scalar level 0-6.
	uint32_t m_benchmark_xubc7_encoder;
	// -benchmark_xuastc_profile [0,2]: XUASTC LDR entropy syntax/profile. 0 = full arith (default), 1 = hybrid, 2 = full zstd.
	uint32_t m_benchmark_xuastc_profile;
	// -benchmark_sweep axis ranges (cBenchmarkSweep). Block index [0,5] (4x4..12x12 squares), effort [0,10], quality [0,10] (x10 -> [1,100]).
	bool m_benchmark_sweep_block_set;
	uint32_t m_benchmark_sweep_block_lo, m_benchmark_sweep_block_hi;
	uint32_t m_benchmark_sweep_effort_lo, m_benchmark_sweep_effort_hi;
	uint32_t m_benchmark_sweep_quality_lo, m_benchmark_sweep_quality_hi;
	std::string m_benchmark_sweep_csv;

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

	// Target texture format string for -export_dds (e.g. "BC1", "BC7", "RGBA32").
	std::string m_export_dds_format;

	// Target output format string for -dds (e.g. "bc7", "a8r8g8b8"); see basisu_dds_export.h.
	std::string m_dds_format;

	// -dds BC7 packer options (defaults mean "not specified" -> use the dds_export_params default). See basisu_dds_export.h.
	dds_bc7_encoder m_dds_bc7_encoder = cDDSBC7Encoder_Default;	// which BC7 packer
	int m_dds_bc7f_level = -1;			// 0 = analytical, 1 = partially analytical, 2 = non analytical (bc7f); -1 = unset
	int m_dds_bc7e_scalar_level = -1;	// 0..6 (bc7e_scalar); -1 = unset

	// Target texture format string for -export_ktx (e.g. "BC7", "ETC2", "ASTC_6x6").
	std::string m_export_ktx_format;

	int m_unpack_format_only;

	std::string m_multifile_printf;
	uint32_t m_multifile_first;
	uint32_t m_multifile_num;

	std::string m_csv_file;

	std::string m_etc1s_use_global_codebooks_file;

	std::string m_test_file_dir;
	bool m_test_dir_explicit = false;	// true if -test_dir was explicitly given (so -test_codecs can default to test_files)

	// When true, -test_xuastc runs the compressor across all block sizes and prints the
	// expected-value tables (to copy/paste into the code) instead of validating.
	bool m_xuastc_test_dump = false;

	// -test_codecs: optional codec name filter (token-boundary, case-insensitive), e.g. "ETC1S", "XUASTC", "ASTC", "HDR".
	std::string m_codec_filter;
	// -test_codecs_gen: output .inl path (defaults to "basisu_tool_test_codecs.inl").
	std::string m_test_codecs_gen_filename;

	uint32_t m_max_threads;
		
	bool m_individual;
	bool m_no_ktx;
	bool m_ktx_only;
	bool m_write_out;
	bool m_etc1_only;
	bool m_fuzz_testing;
	bool m_compare_ssim;
	bool m_compare_plot;

	// -dump_image_stats / -dump_pixel / -dump_block (diagnostic image inspection; see image_stats_mode())
	bool m_dump_image_stats;
	int m_dump_pixel_x, m_dump_pixel_y;		// -1 == not requested
	int m_dump_block_x, m_dump_block_y;		// -1 == not requested (4x4 texel block coords)
	int m_stats_region_x, m_stats_region_y, m_stats_region_w, m_stats_region_h;	// -1 == stats cover the whole image; otherwise restrict to this rectangle
	int m_extract_channel;					// -extract_channel: 0=R,1=G,2=B,3=A; -1 == not requested
	int m_extract_swizzle[4] = { 0, 1, 2, 3 };	// -extract_swizzle: source channel index feeding each output R/G/B/A (identity by default)
	int m_extract_swizzle_count = -1;		// number of swizzle tokens given (1..4); -1 == not requested. Output channels >= count pass through unchanged
	std::string m_extract_filename;			// output PNG for -extract_channel / -extract_swizzle / -extract_region
	int m_extract_region_x, m_extract_region_y, m_extract_region_w, m_extract_region_h;	// region rectangle; w<=0 means "whole image" (optional for -extract_channel/-extract_swizzle, required for -extract_region)
	int m_extract_color_xform = cExtractXformNone;	// optional sRGB<->linear transform applied to the extracted output
	bool m_extract_xform_alpha = false;		// default: transform RGB only (A passes through); true == also transform the alpha channel
	std::string m_text_image_in, m_text_image_out;	// -text_to_png / -png_to_text: input and output filenames (see basisu_text_image.h)
	bool m_parallel_compression;
	bool m_tonemap_dither_flag;
	bool m_xuastc_ldr_disable_bc7_transcoding;
	bool m_no_etc1s_transcoding_chroma_filtering;
	bool m_higher_quality_transcoding;
	bool m_transcode_force_deblocking;
	bool m_transcode_disable_deblocking; 
	
	int m_effort_level;
	int m_quality_level; // new-style -quality X
	bool m_used_old_style_codec_config_param; // true if the user has specified low-level or old-style codec configuration parameters
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
	printf("\nInput file \"%s\"\n", pInput_filename);
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

static uint32_t get_transcode_flags_from_options(const command_line_params& opts)
{
	uint32_t transcode_flags = opts.m_higher_quality_transcoding ? basist::cDecodeFlagsHighQuality : 0;

	if (opts.m_transcode_disable_deblocking)
		transcode_flags |= basist::cDecodeFlagsNoDeblockFiltering;
	else if (opts.m_transcode_force_deblocking)
		transcode_flags |= basist::cDecodeFlagsForceDeblockFiltering;

	if (opts.m_no_etc1s_transcoding_chroma_filtering)
		transcode_flags |= basist::cDecodeFlagsNoETC1SChromaFiltering;
	if (opts.m_xuastc_ldr_disable_bc7_transcoding)
		transcode_flags |= basist::cDecodeFlagXUASTCLDRDisableFastBC7Transcoding;

	return transcode_flags;
}

static bool compress_mode(command_line_params &opts)
{
	uint32_t num_threads = 1;

	if (opts.m_comp_params.m_multithreading)
	{
		// We use std::thread::hardware_concurrency() as a hint to determine the default # of threads to put into a pool.
		num_threads = get_num_hardware_threads();
		if (num_threads < 1)
			num_threads = 1;
		if (num_threads > opts.m_max_threads)
			num_threads = opts.m_max_threads;
	}

	// num_threads is the total thread pool size, *including* the calling thread. So 1=no extra threads.
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
				
		if (params.m_tex_type == basist::basis_texture_type::cBASISTexTypeVideoFrames)
		{
			// Create KTXanimData key value entry
			// TODO: Move this to basisu_comp.h
			basist::key_value kv;

			const char* pAD = "KTXanimData";
			kv.m_key.resize(strlen(pAD) + 1);
			strcpy((char*)kv.m_key.data(), pAD);
			
			basist::ktx2_animdata ad;
			ad.m_duration = opts.m_ktx2_animdata_duration;
			ad.m_timescale = opts.m_ktx2_animdata_timescale;
			ad.m_loopcount = opts.m_ktx2_animdata_loopcount;

			kv.m_value.resize(sizeof(ad));
			memcpy(kv.m_value.data(), &ad, sizeof(ad));

			params.m_key_values.push_back(kv);
		}
		
		// TODO- expose this to command line.
		params.m_ktx2_zstd_supercompression_level = opts.m_ktx2_zstandard_level;
	}

	params.m_read_source_images = true;
	params.m_write_output_basis_or_ktx2_files = true;
	params.m_pGlobal_codebooks = pGlobal_codebook_data ? &pGlobal_codebook_data->m_transcoder.get_lowlevel_etc1s_decoder() : nullptr; 
	
	// Get the transcode/decode flags used when validating the output by calling the transcoder from the options.
	params.m_transcode_flags = get_transcode_flags_from_options(opts);
	
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
					fmt_printf("Compression succeeded to file \"{}\" size {} bytes in {3.3} secs, {3.3} bits/texel\n",
						params.m_out_filename.c_str(),
						opts.m_ktx2_mode ? (uint64_t)c.get_output_ktx2_file().size() : (uint64_t)c.get_output_basis_file().size(),
						tm.get_elapsed_secs(),
						opts.m_ktx2_mode ? c.get_ktx2_bits_per_texel() : c.get_basis_bits_per_texel());
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
				case basis_compressor::cECFailedInvalidParameters:
				{
					error_printf("Invalid compressor parameters (internal error)\n");
					
					if (opts.m_individual)
						exit_flag = false;

					break;
				}
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
				case basis_compressor::cECFailedFrontendExtract:
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
						(uint32_t)c.get_basis_file_size(),
						(uint32_t)c.get_stats().size(),
						c.get_stats()[0].m_width, c.get_stats()[0].m_height, (uint32_t)c.get_any_source_image_has_alpha(),
						c.get_basis_bits_per_texel(),
						c.get_stats()[0].m_basis_rgb_avg_psnr,
						c.get_stats()[0].m_basis_rgba_avg_psnr,
						c.get_stats()[0].m_basis_luma_709_psnr,
						c.get_stats()[0].m_best_etc1s_luma_709_psnr,
						params.m_quality_level, (int)params.m_etc1s_compression_level, tm.get_elapsed_secs(),
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
	BASISU_NOTE_UNUSED(pCSV_file);
	BASISU_NOTE_UNUSED(file_index);

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
	
	printf("Deblocking filter ID: %u\n", dec.get_deblocking_filter_index());

	const bool is_etc1s = (dec.get_basis_tex_format() == basist::basis_tex_format::cETC1S);
	
	bool is_hdr = false;
	//bool is_xuastc_ldr = false, is_astc_ldr = false;

	std::string fmt_str_temp;

	const char* pFmt_str = nullptr;
	switch (dec.get_basis_tex_format())
	{
	case basist::basis_tex_format::cETC1S:
	{
		pFmt_str = "ETC1S";
		break;
	}
	case basist::basis_tex_format::cUASTC_LDR_4x4:
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
	case basist::basis_tex_format::cUASTC_HDR_6x6_INTERMEDIATE:
	{
		is_hdr = true;
		pFmt_str = "UASTC_HDR_6x6_INTERMEDIATE";
		break;
	}
	case basist::basis_tex_format::cXUASTC_LDR_4x4:
	case basist::basis_tex_format::cXUASTC_LDR_5x4:
	case basist::basis_tex_format::cXUASTC_LDR_5x5:
	case basist::basis_tex_format::cXUASTC_LDR_6x5:
	case basist::basis_tex_format::cXUASTC_LDR_6x6:
	case basist::basis_tex_format::cXUASTC_LDR_8x5:
	case basist::basis_tex_format::cXUASTC_LDR_8x6:
	case basist::basis_tex_format::cXUASTC_LDR_10x5:
	case basist::basis_tex_format::cXUASTC_LDR_10x6:
	case basist::basis_tex_format::cXUASTC_LDR_8x8:
	case basist::basis_tex_format::cXUASTC_LDR_10x8:
	case basist::basis_tex_format::cXUASTC_LDR_10x10:
	case basist::basis_tex_format::cXUASTC_LDR_12x10:
	case basist::basis_tex_format::cXUASTC_LDR_12x12:
	{
		//is_xuastc_ldr = true;

		uint32_t block_width = 0, block_height = 0;
		basist::get_basis_tex_format_block_size(dec.get_basis_tex_format(), block_width, block_height);
		fmt_str_temp = fmt_string("XUASTC_LDR_{}x{}", block_width, block_height);
		pFmt_str = fmt_str_temp.c_str();
		break;
	}
	case basist::basis_tex_format::cASTC_LDR_4x4:
	case basist::basis_tex_format::cASTC_LDR_5x4:
	case basist::basis_tex_format::cASTC_LDR_5x5:
	case basist::basis_tex_format::cASTC_LDR_6x5:
	case basist::basis_tex_format::cASTC_LDR_6x6:
	case basist::basis_tex_format::cASTC_LDR_8x5:
	case basist::basis_tex_format::cASTC_LDR_8x6:
	case basist::basis_tex_format::cASTC_LDR_10x5:
	case basist::basis_tex_format::cASTC_LDR_10x6:
	case basist::basis_tex_format::cASTC_LDR_8x8:
	case basist::basis_tex_format::cASTC_LDR_10x8:
	case basist::basis_tex_format::cASTC_LDR_10x10:
	case basist::basis_tex_format::cASTC_LDR_12x10:
	case basist::basis_tex_format::cASTC_LDR_12x12:
	{
		//is_astc_ldr = true;

		uint32_t block_width = 0, block_height = 0;
		basist::get_basis_tex_format_block_size(dec.get_basis_tex_format(), block_width, block_height);
		fmt_str_temp = fmt_string("ASTC_LDR_{}x{}", block_width, block_height);
		pFmt_str = fmt_str_temp.c_str();
		break;
	}
	case basist::basis_tex_format::cXUBC7:
	{
		pFmt_str = "XUBC7";
		break;
	}
	default:
	{
		fmt_error_printf("Unknown/invalid format!\n");

		assert(0);
		return false;
	}
	}
			
	printf("KTX2 Supercompression Scheme: ");
	switch (dec.get_header().m_supercompression_scheme)
	{
	case basist::KTX2_SS_NONE: printf("NONE\n"); break;
	case basist::KTX2_SS_BASISLZ: printf("BASISLZ\n"); break;
	case basist::KTX2_SS_ZSTANDARD: printf("ZSTANDARD\n"); break;
	case basist::KTX2_SS_DEFLATE: printf("DEFLATE\n"); break;
	case basist::KTX2_SS_UASTC_HDR_6x6I: printf("UASTC_HDR_6x6I\n"); break;
	case basist::KTX2_SS_XUASTC_LDR: printf("XUASTC_LDR\n"); break;
	case basist::KTX2_SS_XUBC7: printf("XUBC7\n"); break;
	default:
		error_printf("Invalid/unknown/unsupported\n");
		return false;
	}

	printf("Library Supercompression Format: %s\n", pFmt_str);

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
	{
		printf("DFD chan0: %s\n", basist::ktx2_get_uastc_df_channel_id_str(dec.get_dfd_channel_id0()));
	}

	// For proper ASTC decoding we must know which ASTC decode profile to apply (sRGB or linear).
	const bool actual_ktx2_srgb_transfer_func = (dec.get_dfd_transfer_func() == basist::KTX2_KHR_DF_TRANSFER_SRGB);
		
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

	// the sRGB transfer function to use while unpacking astc content (ideally we want this to always match what we used during astc encoding)
	bool srgb_transfer_func_astc_unpacking = actual_ktx2_srgb_transfer_func;

	// the sRGB transfer function to use when writing out files (we want to indicate to the caller if the data is sRGB or linear)
	bool srgb_transfer_func_astc_writing = actual_ktx2_srgb_transfer_func;
		
	const bool is_uastc_ldr_4x4 = (dec.get_basis_tex_format() == basist::basis_tex_format::cUASTC_LDR_4x4);
	
	if ((is_etc1s) || (is_uastc_ldr_4x4))
	{
		// The ETC1S and UASTC LDR 4x4 transcoders supply ASTC LDR 4x4 data assuming the decoder will NOT be using the sRGB read decode profile, which is likely the most common case (in geospatial rendering scenarios).
		// Note XUASTC/UASTC LDR 4x4-12x12 supports both linear and sRGB decode profiles throughout the entire pipeline (encoding/transcoding/decoding to raw pixels).
		srgb_transfer_func_astc_unpacking = false;
		
		// This matches the behavior of our original tools. It ensures astcenc uses linear by default when reading our transcoded .KTX files.
		srgb_transfer_func_astc_writing = false;

		if (actual_ktx2_srgb_transfer_func)
			printf("Note: ETC1S/UASTC LDR 4x4 will always be decoded by this tool using the ASTC linear decode profile, regardless of the KTX2/.basis DFD transfer function field.\n");
	}

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
				(!c) ||
				((c >= ' ') && (c < 0x80))  
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

	if (opts.m_unpack_format_only > -1)
	{
		first_format = opts.m_unpack_format_only;
		last_format = first_format + 1;
	}

	uint32_t transcode_flags = get_transcode_flags_from_options(opts);
					
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
						if (!is_pow2(level_info.m_orig_width) || !is_pow2(level_info.m_orig_height))
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
										
					interval_timer tm;
					tm.start();

					if (!dec.transcode_image_level(level_index, layer_index, face_index, gi.get_ptr(), gi.get_total_blocks(), transcoder_tex_fmt, transcode_flags))
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

	// Now write KTX/DDS/ASTC files and unpack them to individual PNG's/EXR's
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

		const bool is_fmt_astc = basis_is_transcoder_texture_format_astc(transcoder_tex_fmt);

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

				// Write KTX1 file
				{
					std::string ktx_filename(base_filename + string_format("_transcoded_cubemap_%s_layer_%u.ktx", basist::basis_get_format_name(transcoder_tex_fmt), layer_index));

					if (!write_compressed_texture_file(ktx_filename.c_str(), cubemap, true, is_fmt_astc ? srgb_transfer_func_astc_writing : actual_ktx2_srgb_transfer_func))
					{
						error_printf("Failed writing KTX file \"%s\"!\n", ktx_filename.c_str());
						return false;
					}
					printf("Wrote .KTX cubemap file \"%s\"\n", ktx_filename.c_str());
				}
								
				// Write .DDS file
				if (does_dds_support_format(cubemap[0][0].get_format()))
				{
					std::string dds_filename(base_filename + string_format("_transcoded_cubemap_%s_layer_%u.dds", basist::basis_get_format_name(transcoder_tex_fmt), layer_index));

					if (!write_compressed_texture_file(dds_filename.c_str(), cubemap, true, actual_ktx2_srgb_transfer_func))
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

				if (!write_compressed_texture_file(dds_filename.c_str(), tex_array, is_cubemap, actual_ktx2_srgb_transfer_func))
				{
					error_printf("Failed writing DDS file \"%s\"!\n", dds_filename.c_str());
					return false;
				}
				printf("Wrote .DDS texture array file \"%s\"\n", dds_filename.c_str());
			}
		}

		// Now unpack each layer and face individually and write KTX/DDS/ASTC/PNG/EXR/OUT files for each
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

						if (!write_compressed_texture_file(ktx_filename.c_str(), gi, is_fmt_astc ? srgb_transfer_func_astc_writing : actual_ktx2_srgb_transfer_func))
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

						if (!write_compressed_texture_file(dds_filename.c_str(), gi, actual_ktx2_srgb_transfer_func))
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

						// Save .astc
						if ((!opts.m_ktx_only) && basist::basis_is_transcoder_texture_format_astc(transcoder_tex_fmt))
						{
							std::string astc_filename;
							if (gi.size() > 1)
								astc_filename = base_filename + string_format("_unpacked_%s_level_%u_face_%u_layer_%04u.astc", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index);
							else
								astc_filename = base_filename + string_format("_unpacked_%s_face_%u_layer_%04u.astc", basist::basis_get_format_name(transcoder_tex_fmt), face_index, layer_index);

							const gpu_image& level_g = gi[level_index];

							if (!write_astc_file(astc_filename.c_str(), level_g.get_ptr(), level_g.get_block_width(), level_g.get_block_height(), level_info.m_width, level_info.m_height))
							{
								error_printf("Failed writing to .ASTC file \"%s\"\n", astc_filename.c_str());
								return false;
							}
							printf("Wrote .ASTC file \"%s\"\n", astc_filename.c_str());
						}
					}
					else
					{
						image u;
						if (!gi[level_index].unpack(u, srgb_transfer_func_astc_unpacking))
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

						// Save .astc
						if ((!opts.m_ktx_only) && basist::basis_is_transcoder_texture_format_astc(transcoder_tex_fmt))
						{
							std::string astc_filename;
							if (gi.size() > 1)
								astc_filename = base_filename + string_format("_unpacked_%s_level_%u_face_%u_layer_%04u.astc", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index);
							else
								astc_filename = base_filename + string_format("_unpacked_%s_face_%u_layer_%04u.astc", basist::basis_get_format_name(transcoder_tex_fmt), face_index, layer_index);

							const gpu_image& level_g = gi[level_index];

							if (!write_astc_file(astc_filename.c_str(), level_g.get_ptr(), level_g.get_block_width(), level_g.get_block_height(), level_info.m_width, level_info.m_height))
							{
								error_printf("Failed writing to .ASTC file \"%s\"\n", astc_filename.c_str());
								return false;
							}
							printf("Wrote .ASTC file \"%s\"\n", astc_filename.c_str());
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
						
					} // is_hdr

				} // level_index

			} // face_index

		} // layer_index

	} // format_iter

	if ((opts.m_unpack_format_only == -1) && (!validate_flag))
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

						if (!dec.transcode_image_level(level_index, layer_index, face_index, half_img.data(), total_pixels, transcoder_tex_fmt, transcode_flags))
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

						if (!dec.transcode_image_level(level_index, layer_index, face_index, half_img.data(), total_pixels, transcoder_tex_fmt, transcode_flags))
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

			// RGB_9E5
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

						if (!dec.transcode_image_level(level_index, layer_index, face_index, rgb9e5_img.data(), total_pixels, transcoder_tex_fmt, transcode_flags))
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
			// RGBA 32bpp
			for (uint32_t level_index = 0; level_index < dec.get_levels(); level_index++)
			{
				for (uint32_t layer_index = 0; layer_index < total_layers; layer_index++)
				{
					for (uint32_t face_index = 0; face_index < dec.get_faces(); face_index++)
					{
						const basist::transcoder_texture_format transcoder_tex_fmt = basist::transcoder_texture_format::cTFRGBA32;

						basist::ktx2_image_level_info level_info;

						if (!dec.get_image_level_info(level_info, level_index, layer_index, face_index))
						{
							fmt_error_printf("Failed retrieving image level information ({} {} {})!\n", layer_index, level_index, face_index);
							return false;
						}

						const uint32_t total_pixels = level_info.m_orig_width * level_info.m_orig_height;

						image img(level_info.m_orig_width, level_info.m_orig_height);

						fill_buffer_with_random_bytes(img.get_ptr(), img.get_total_pixels() * sizeof(color_rgba));

						interval_timer tm;
						tm.start();

						if (!dec.transcode_image_level(level_index, layer_index, face_index, img.get_ptr(), total_pixels, transcoder_tex_fmt, transcode_flags))
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
							std::string rgba_filename(base_filename + fmt_string("_unpacked_rgba_{}_level_{}_face_{}_layer{04}.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index));
							if (!save_png(rgba_filename, img))
							{
								error_printf("Failed writing to .PNG file \"%s\"\n", rgba_filename.c_str());
								return false;
							}

							std::string rgb_filename(base_filename + fmt_string("_unpacked_rgb_{}_level_{}_face_{}_layer{04}.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index));
							if (!save_png(rgb_filename, img, cImageSaveIgnoreAlpha))
							{
								error_printf("Failed writing to .PNG file \"%s\"\n", rgb_filename.c_str());
								return false;
							}
							printf("Wrote .PNG file \"%s\"\n", rgb_filename.c_str());

							std::string a_filename(base_filename + fmt_string("_unpacked_a_{}_{}_{}_{04}.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index));
							if (!save_png(a_filename, img, cImageSaveGrayscale, 3))
							{
								error_printf("Failed writing to .PNG file \"%s\"\n", a_filename.c_str());
								return false;
							}
							printf("Wrote .PNG file \"%s\"\n", a_filename.c_str());
						}

					} // face_index
				} // layer_index 
			} // level_index

			// RGB565
			for (uint32_t level_index = 0; level_index < dec.get_levels(); level_index++)
			{
				for (uint32_t layer_index = 0; layer_index < total_layers; layer_index++)
				{
					for (uint32_t face_index = 0; face_index < dec.get_faces(); face_index++)
					{
						const basist::transcoder_texture_format transcoder_tex_fmt = basist::transcoder_texture_format::cTFRGB565;

						basist::ktx2_image_level_info level_info;

						if (!dec.get_image_level_info(level_info, level_index, layer_index, face_index))
						{
							fmt_error_printf("Failed retrieving image level information ({} {} {})!\n", layer_index, level_index, face_index);
							return false;
						}

						const uint32_t total_pixels = level_info.m_orig_width * level_info.m_orig_height;
						
						basisu::vector<uint16_t> packed_img(total_pixels);
												
						fill_buffer_with_random_bytes(packed_img.get_ptr(), packed_img.size_in_bytes());

						interval_timer tm;
						tm.start();

						if (!dec.transcode_image_level(level_index, layer_index, face_index, packed_img.get_ptr(), total_pixels, transcoder_tex_fmt, transcode_flags))
						{
							fmt_error_printf("Failed transcoding image level ({} {} {})!\n", layer_index, level_index, face_index);
							return false;
						}
												
						double total_transcode_time = tm.get_elapsed_ms();

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

						fmt_printf("Transcode of level {} layer {} face {} res {}x{} format {} succeeded in {} ms\n",
							level_index, layer_index, face_index,
							level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_transcode_time);

						if ((!validate_flag) && (!opts.m_ktx_only))
						{
							std::string rgb_filename(base_filename + fmt_string("_unpacked_rgb_{}_level_{}_face_{}_layer{04}.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index));
							if (!save_png(rgb_filename, img, cImageSaveIgnoreAlpha))
							{
								error_printf("Failed writing to .PNG file \"%s\"\n", rgb_filename.c_str());
								return false;
							}
							printf("Wrote .PNG file \"%s\"\n", rgb_filename.c_str());

						}

					} // face_index
				} // layer_index 
			} // level_index

			// RGBA4444
			for (uint32_t level_index = 0; level_index < dec.get_levels(); level_index++)
			{
				for (uint32_t layer_index = 0; layer_index < total_layers; layer_index++)
				{
					for (uint32_t face_index = 0; face_index < dec.get_faces(); face_index++)
					{
						const basist::transcoder_texture_format transcoder_tex_fmt = basist::transcoder_texture_format::cTFRGBA4444;

						basist::ktx2_image_level_info level_info;

						if (!dec.get_image_level_info(level_info, level_index, layer_index, face_index))
						{
							fmt_error_printf("Failed retrieving image level information ({} {} {})!\n", layer_index, level_index, face_index);
							return false;
						}

						const uint32_t total_pixels = level_info.m_orig_width * level_info.m_orig_height;

						basisu::vector<uint16_t> packed_img(total_pixels);

						fill_buffer_with_random_bytes(packed_img.get_ptr(), packed_img.size_in_bytes());

						interval_timer tm;
						tm.start();

						if (!dec.transcode_image_level(level_index, layer_index, face_index, packed_img.get_ptr(), total_pixels, transcoder_tex_fmt, transcode_flags))
						{
							fmt_error_printf("Failed transcoding image level ({} {} {})!\n", layer_index, level_index, face_index);
							return false;
						}

						double total_transcode_time = tm.get_elapsed_ms();

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

						fmt_printf("Transcode of level {} layer {} face {} res {}x{} format {} succeeded in {} ms\n",
							level_index, layer_index, face_index,
							level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_transcode_time);

						if ((!validate_flag) && (!opts.m_ktx_only))
						{
							std::string rgba_filename(base_filename + fmt_string("_unpacked_rgba_{}_level_{}_face_{}_layer{04}.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index));
							if (!save_png(rgba_filename, img))
							{
								error_printf("Failed writing to .PNG file \"%s\"\n", rgba_filename.c_str());
								return false;
							}

							std::string rgb_filename(base_filename + fmt_string("_unpacked_rgb_{}_level_{}_face_{}_layer{04}.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index));
							if (!save_png(rgb_filename, img, cImageSaveIgnoreAlpha))
							{
								error_printf("Failed writing to .PNG file \"%s\"\n", rgb_filename.c_str());
								return false;
							}
							printf("Wrote .PNG file \"%s\"\n", rgb_filename.c_str());

							std::string a_filename(base_filename + fmt_string("_unpacked_a_{}_{}_{}_{04}.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, face_index, layer_index));
							if (!save_png(a_filename, img, cImageSaveGrayscale, 3))
							{
								error_printf("Failed writing to .PNG file \"%s\"\n", a_filename.c_str());
								return false;
							}
							printf("Wrote .PNG file \"%s\"\n", a_filename.c_str());
						}

					} // face_index
				} // layer_index 
			} // level_index
		}
	}

	return true;
}

static bool vec_is_ascii(const uint8_vec& v)
{
	bool is_ascii = true;

	for (uint32_t j = 0; j < v.size(); j++)
	{
		uint8_t c = v[j];
		if (!c)
			break;

		if ((c < 32) || (c > 127))
		{
			is_ascii = false;
			break;
		}
	} // j

	return is_ascii;
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

	std::string fmt_str_temp;
	
	const char* pFmt_str = nullptr;
	switch (fileinfo.m_tex_format)
	{
	case basist::basis_tex_format::cETC1S:
	{
		pFmt_str = "ETC1S";
		break;
	}
	case basist::basis_tex_format::cUASTC_LDR_4x4:
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
	case basist::basis_tex_format::cUASTC_HDR_6x6_INTERMEDIATE:
	{
		is_hdr = true;
		pFmt_str = "UASTC_HDR_6x6_INTERMEDIATE";
		break;
	}
	case basist::basis_tex_format::cXUASTC_LDR_4x4:
	case basist::basis_tex_format::cXUASTC_LDR_5x4:
	case basist::basis_tex_format::cXUASTC_LDR_5x5:
	case basist::basis_tex_format::cXUASTC_LDR_6x5:
	case basist::basis_tex_format::cXUASTC_LDR_6x6:
	case basist::basis_tex_format::cXUASTC_LDR_8x5:
	case basist::basis_tex_format::cXUASTC_LDR_8x6:
	case basist::basis_tex_format::cXUASTC_LDR_10x5:
	case basist::basis_tex_format::cXUASTC_LDR_10x6:
	case basist::basis_tex_format::cXUASTC_LDR_8x8:
	case basist::basis_tex_format::cXUASTC_LDR_10x8:
	case basist::basis_tex_format::cXUASTC_LDR_10x10:
	case basist::basis_tex_format::cXUASTC_LDR_12x10:
	case basist::basis_tex_format::cXUASTC_LDR_12x12:
	{
		uint32_t block_width = 0, block_height = 0;
		basist::get_basis_tex_format_block_size(fileinfo.m_tex_format, block_width, block_height);
		fmt_str_temp = fmt_string("XUASTC_LDR_{}x{}", block_width, block_height);
		pFmt_str = fmt_str_temp.c_str();
		break;
	}
	case basist::basis_tex_format::cASTC_LDR_4x4:
	case basist::basis_tex_format::cASTC_LDR_5x4:
	case basist::basis_tex_format::cASTC_LDR_5x5:
	case basist::basis_tex_format::cASTC_LDR_6x5:
	case basist::basis_tex_format::cASTC_LDR_6x6:
	case basist::basis_tex_format::cASTC_LDR_8x5:
	case basist::basis_tex_format::cASTC_LDR_8x6:
	case basist::basis_tex_format::cASTC_LDR_10x5:
	case basist::basis_tex_format::cASTC_LDR_10x6:
	case basist::basis_tex_format::cASTC_LDR_8x8:
	case basist::basis_tex_format::cASTC_LDR_10x8:
	case basist::basis_tex_format::cASTC_LDR_10x10:
	case basist::basis_tex_format::cASTC_LDR_12x10:
	case basist::basis_tex_format::cASTC_LDR_12x12:
	{
		uint32_t block_width = 0, block_height = 0;
		basist::get_basis_tex_format_block_size(fileinfo.m_tex_format, block_width, block_height);
		fmt_str_temp = fmt_string("ASTC_LDR_{}x{}", block_width, block_height);
		pFmt_str = fmt_str_temp.c_str();
		break;
	}
	case basist::basis_tex_format::cXUBC7:
	{
		pFmt_str = "XUBC7";
		break;
	}
	default:
	{
		fmt_error_printf("Invalid/unknown format!\n");
		
		assert(0);
		return false;
	}
	}

	fmt_printf("  Texture format: {}\n", pFmt_str);
		
	printf("  Texture type: %s\n", basist::basis_get_texture_type_name(fileinfo.m_tex_type));
	printf("  us per frame: %u (%f fps)\n", fileinfo.m_us_per_frame, fileinfo.m_us_per_frame ? (1.0f / ((float)fileinfo.m_us_per_frame / 1000000.0f)) : 0.0f);
	printf("  Total slices: %u\n", (uint32_t)fileinfo.m_slice_info.size());
	printf("  Total images: %i\n", fileinfo.m_total_images);
	printf("  Y Flipped: %u, Has alpha slices: %u, sRGB: %u\n", fileinfo.m_y_flipped, fileinfo.m_has_alpha_slices, fileinfo.m_srgb);
	printf("  userdata0: 0x%X userdata1: 0x%X\n", fileinfo.m_userdata0, fileinfo.m_userdata1);
	printf("  Per-image mipmap levels: ");
	for (uint32_t i = 0; i < fileinfo.m_total_images; i++)
		printf("%u ", fileinfo.m_image_mipmap_levels[i]);
	printf("\n");

	basist::key_value_vec key_values;
	if (!dec.get_key_values(&basis_file_data[0], (uint32_t)basis_file_data.size(), key_values))
	{
		error_printf("get_key_values() failed!\n");
		return false;
	}

	fmt_printf("\nTotal key value fields: {}\n", key_values.size_u32());

	if (key_values.size())\
	{
		for (uint32_t k = 0; k < key_values.size(); k++)
		{
			// Always zero terminated, always at least 1 character
			if (vec_is_ascii(key_values[k].m_key))
				fmt_printf(" {}. \"{}\" - {} bytes: ", k, (const char*)key_values[k].m_key.get_ptr(), key_values[k].m_value.size_u32());
			else
				fmt_printf(" {}. [Key not ASCII] - {} bytes: ", k, (const char*)key_values[k].m_key.get_ptr(), key_values[k].m_value.size_u32()); // shouldn't happen, but just in case - we could print as hex

			const uint8_vec& val = key_values[k].m_value;

			// val will always be empty, or it's guaranteed to be zero terminated (we always add an additional 0 at the end)
			if (val.size() >= 100)
			{
				fmt_printf("[too large to print]\n");
				continue;
			}

			if (vec_is_ascii(val))
			{
				fmt_printf("\"{}\"\n", (const char*)val.get_ptr());
			}
			else
			{
				for (uint32_t j = 0; j < val.size(); j++)
				{
					fmt_printf("0x{X} ", val[j]);

					if ((j & 15) == 15)
						fmt_printf("\n");
				} // j

				fmt_printf("\n");
			}

		} // k

		//fmt_printf("\n");
	}

	// the sRGB transfer function to use while astc unpacking (we want this to ideally match what we used during astc encoding)
	bool srgb_transfer_func_astc_unpacking = fileinfo.m_srgb; 
	
	// the sRGB transfer function to use when writing out files (we want to indicate to the caller if the data is sRGB or linear)
	bool srgb_transfer_func_astc_writing = fileinfo.m_srgb; 
	
	const bool is_etc1s = (fileinfo.m_tex_format == basist::basis_tex_format::cETC1S);
	const bool is_uastc_ldr_4x4 = (fileinfo.m_tex_format == basist::basis_tex_format::cUASTC_LDR_4x4);
	if ((is_etc1s) || (is_uastc_ldr_4x4))
	{
		// The ETC1S and UASTC LDR 4x4 transcoders supply ASTC LDR 4x4 data assuming the decoder will NOT be using the sRGB read decode profile, which is likely the most common case (in geospatial rendering scenarios).
		// Note XUASTC/UASTC LDR 4x4-12x12 supports both linear and sRGB decode profiles throughout the entire pipeline (encoding/transcoding/decoding to raw pixels).
		srgb_transfer_func_astc_unpacking = false;
		
		// This matches the behavior of our original tools. It ensures astcenc uses linear by default when reading our transcoded .KTX files.
		srgb_transfer_func_astc_writing = false;

		if (fileinfo.m_srgb)
			printf("Note: ETC1S/UASTC LDR 4x4 will always be decoded by this tool using the ASTC linear decode profile, regardless of the KTX2/.basis DFD transfer function field.\n");
	}
	
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

	if (opts.m_unpack_format_only > -1)
	{
		first_format = opts.m_unpack_format_only;
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

	uint32_t transcode_flags = get_transcode_flags_from_options(opts);

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
					if (!is_pow2(level_info.m_orig_width) || !is_pow2(level_info.m_orig_height))
					{
						total_pvrtc_nonpow2_warnings++;

						printf("Warning: Will not transcode image %u level %u res %ux%u to PVRTC1 (one or more dimension is not a power of 2)\n", image_index, level_index, level_info.m_width, level_info.m_height);

						// Can't transcode this image level to PVRTC because it's not a pow2 (we're going to support transcoding non-pow2 to the next "larger" pow2 soon)
						continue;
					}
				}

				basisu::texture_format tex_fmt = basis_get_basisu_texture_format(transcoder_tex_fmt);

				fmt_printf("Transcoding format: {}\n", (uint32_t)tex_fmt);

				gpu_image& gi = gpu_images[(int)transcoder_tex_fmt][image_index][level_index];
				gi.init(tex_fmt, level_info.m_orig_width, level_info.m_orig_height);

				// Fill the buffer with psuedo-random bytes, to help more visibly detect cases where the transcoder fails to write to part of the output.
				fill_buffer_with_random_bytes(gi.get_ptr(), gi.get_size_in_bytes());
								
				tm.start();

				if (!dec.transcode_image_level(&basis_file_data[0], (uint32_t)basis_file_data.size(), image_index, level_index, gi.get_ptr(), gi.get_total_blocks(), transcoder_tex_fmt, transcode_flags))
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

	// Upack UASTC LDR 4x4 files seperately, to validate we can transcode slices to UASTC LDR 4x4 and unpack them to pixels.
	// This is a special path because UASTC LDR 4x4 is not yet a valid transcoder_texture_format, but a lower-level block_format.
	if (fileinfo.m_tex_format == basist::basis_tex_format::cUASTC_LDR_4x4)
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
					level_info.m_first_slice_index, gi.get_ptr(), gi.get_total_blocks(), basist::block_format::cUASTC_4x4, gi.get_bytes_per_block(), transcode_flags))
				{
					error_printf("Failed transcoding image level (%u %u) to UASTC!\n", image_index, level_index);
					return false;
				}

				double total_transcode_time = tm.get_elapsed_ms();

				printf("Transcode of image %u level %u res %ux%u format UASTC_4x4 succeeded in %3.3f ms\n", image_index, level_index, level_info.m_orig_width, level_info.m_orig_height, total_transcode_time);

				if ((!validate_flag) && (!opts.m_ktx_only))
				{
					image u;
					if (!gi.unpack(u, srgb_transfer_func_astc_unpacking))
					{
						error_printf("Warning: Failed unpacking GPU texture data (%u %u). \n", image_index, level_index);
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

			const bool is_fmt_astc = basis_is_transcoder_texture_format_astc(transcoder_tex_fmt);

			if ((!opts.m_no_ktx) && (fileinfo.m_tex_type == basist::cBASISTexTypeCubemapArray))
			{
				// No KTX tool that we know of supports cubemap arrays, so write individual cubemap files.
				for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index += 6)
				{
					basisu::vector<gpu_image_vec> cubemap;
					for (uint32_t i = 0; i < 6; i++)
						cubemap.push_back(gpu_images[format_iter][image_index + i]);

					// KTX1
					{
						std::string ktx_filename(base_filename + string_format("_transcoded_cubemap_%s_%u.ktx", basist::basis_get_format_name(transcoder_tex_fmt), image_index / 6));
						if (!write_compressed_texture_file(ktx_filename.c_str(), cubemap, true, is_fmt_astc ? srgb_transfer_func_astc_writing : fileinfo.m_srgb))
						{
							error_printf("Failed writing KTX file \"%s\"!\n", ktx_filename.c_str());
							return false;
						}
						printf("Wrote .KTX file \"%s\"\n", ktx_filename.c_str());
					}

					// DDS
					if (does_dds_support_format(cubemap[0][0].get_format()))
					{
						std::string dds_filename(base_filename + string_format("_transcoded_cubemap_%s_%u.dds", basist::basis_get_format_name(transcoder_tex_fmt), image_index / 6));
						if (!write_compressed_texture_file(dds_filename.c_str(), cubemap, true, fileinfo.m_srgb))
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
					// KTX1
					{
						std::string ktx_filename(base_filename + string_format("_transcoded_%s_%04u.ktx", basist::basis_get_format_name(transcoder_tex_fmt), image_index));
						if (!write_compressed_texture_file(ktx_filename.c_str(), gi, is_fmt_astc ? srgb_transfer_func_astc_writing : fileinfo.m_srgb))
						{
							error_printf("Failed writing KTX file \"%s\"!\n", ktx_filename.c_str());
							return false;
						}
						printf("Wrote .KTX file \"%s\"\n", ktx_filename.c_str());
					}

					// DDS
					if (does_dds_support_format(gi[0].get_format()))
					{
						std::string dds_filename(base_filename + string_format("_transcoded_%s_%04u.dds", basist::basis_get_format_name(transcoder_tex_fmt), image_index));
						if (!write_compressed_texture_file(dds_filename.c_str(), gi, fileinfo.m_srgb))
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
						if (!gi[level_index].unpack(u, srgb_transfer_func_astc_unpacking))
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
	if ((opts.m_unpack_format_only == -1) && (!validate_flag))
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
						half_img.get_ptr(), total_pixels, transcoder_tex_fmt, transcode_flags, level_info.m_orig_width, nullptr, level_info.m_orig_height))
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
						half_img.get_ptr(), total_pixels, transcoder_tex_fmt, transcode_flags, level_info.m_orig_width, nullptr, level_info.m_orig_height))
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
						rgb9e5_img.get_ptr(), total_pixels, transcoder_tex_fmt, transcode_flags, level_info.m_orig_width, nullptr, level_info.m_orig_height))
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

					if (!dec.transcode_image_level(&basis_file_data[0], (uint32_t)basis_file_data.size(), image_index, level_index, &img(0, 0).r, img.get_total_pixels(), transcoder_tex_fmt, transcode_flags, img.get_pitch(), nullptr, img.get_height()))
					{
						error_printf("Failed transcoding image level (%u %u %u)!\n", image_index, level_index, transcoder_tex_fmt);
						return false;
					}

					double total_transcode_time = tm.get_elapsed_ms();

					total_format_transcoding_time_ms[(int)transcoder_tex_fmt] += total_transcode_time;

					printf("Transcode of image %u level %u res %ux%u format %s succeeded in %3.3f ms\n", image_index, level_index, level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt), total_transcode_time);

					if ((!validate_flag) && (!opts.m_ktx_only))
					{
						std::string rgba_filename(base_filename + string_format("_unpacked_rgba_%s_%u_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index));
						if (!save_png(rgba_filename, img))
						{
							error_printf("Failed writing to PNG file \"%s\"\n", rgba_filename.c_str());
							return false;
						}
						printf("Wrote .PNG file \"%s\"\n", rgba_filename.c_str());

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

					if (!dec.transcode_image_level(&basis_file_data[0], (uint32_t)basis_file_data.size(), image_index, level_index, &packed_img[0], (uint32_t)packed_img.size(), transcoder_tex_fmt, transcode_flags, level_info.m_orig_width, nullptr, level_info.m_orig_height))
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

					if (!dec.transcode_image_level(&basis_file_data[0], (uint32_t)basis_file_data.size(), image_index, level_index, &packed_img[0], (uint32_t)packed_img.size(), transcoder_tex_fmt, transcode_flags, level_info.m_orig_width, nullptr, level_info.m_orig_height))
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
						std::string rgba_filename(base_filename + string_format("_unpacked_rgba_%s_%u_%04u.png", basist::basis_get_format_name(transcoder_tex_fmt), level_index, image_index));
						if (!save_png(rgba_filename, img))
						{
							error_printf("Failed writing to PNG file \"%s\"\n", rgba_filename.c_str());
							return false;
						}
						printf("Wrote .PNG file \"%s\"\n", rgba_filename.c_str());

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

	} // if ((opts.m_unpack_format_only == -1) && (!validate_flag))

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

// Unpacks a Microsoft DDS file (DX9/DX10) using basist::dds_transcoder, bringing it up to the .ktx2/.basis bar.
// The DDS transcoder is LDR-only, so it transcodes the source to each DDS-supported target format and:
//  (1) for the 12 block-compressed targets (BC1/3/4/5/7, ETC1/2, EAC R11/RG11, ASTC 4x4, PVRTC1) -- writes the
//      GPU container files (per-image whole-mip-chain, plus cubemap and DDS texture-array shapes; .KTX for all,
//      .DDS for BC1-7 only), AND round-trips each transcoded block image back to RGBA as one _unpacked_rgba_ PNG;
//  (2) for the 3 uncompressed targets (RGBA32, RGB565, RGBA4444) -- writes one _unpacked_rgba_ PNG each (the
//      RGBA32 decode is the source ground truth).
// -validate runs the full pipeline (transcode AND round-trip unpack, so a broken unpack path is caught too) but
// writes nothing. Honors -output_path, -no_ktx (skip GPU container files), -ktx_only (skip PNGs), and
// -unpack_format_only N (restrict to the single transcoder format whose enum index == N). BC6H/HDR DDS is future work (post v2.5).
static bool unpack_and_validate_dds_file(
	const std::string& base_filename,
	uint8_vec& dds_file_data,
	command_line_params& opts,
	uint32_t& total_unpack_warnings,
	uint32_t& total_pvrtc_nonpow2_warnings)
{
	if (dds_file_data.size() > UINT32_MAX)
	{
		error_printf("DDS file too large!\n");
		return false;
	}

	basist::dds_transcoder dec;

	if (!dec.init(dds_file_data.data(), dds_file_data.size_u32()))
	{
		error_printf("dds_transcoder::init() failed! File either uses an unsupported feature, is invalid, was corrupted, or this is a bug.\n");
		return false;
	}

	if (!dec.start_transcoding())
	{
		error_printf("dds_transcoder::start_transcoding() failed!\n");
		return false;
	}

	const uint32_t levels = dec.get_levels();
	const uint32_t faces = dec.get_faces();
	const uint32_t num_layers = dec.get_layers();			// 0 == not an array (ktx2 convention)
	const uint32_t eff_layers = num_layers ? num_layers : 1;
	const basist::transcoder_texture_format src_fmt = dec.get_format();
	const char* pFmt_name = basist::basis_get_format_name(src_fmt);

	printf("Resolution: %ux%u\n", dec.get_width(), dec.get_height());
	printf("Mipmap Levels: %u\n", levels);
	printf("Texture Array Size (layers): %u\n", num_layers);
	printf("Total Faces: %u (%s)\n", faces, (faces == 6) ? "CUBEMAP" : "2D");
	printf("DDS source format: %s\n", basisu::get_dds_format_string(dec.get_dds_format()));
	printf("Contained format: %s\n", pFmt_name);
	// For uncompressed sources the "Contained format" is the CLOSEST-matching transcoder format, not necessarily
	// the .DDS's physical pixel layout (e.g. an A8R8G8B8/X8R8G8B8 or B5G5R5A1 file reports RGBA32 / RGBA4444).
	// The exact physical layout is shown above on the "DDS source format" line.
	if (dec.get_source_kind() == basist::dds_transcoder::source_kind::cUncompressed)
		printf("  (note: 'Contained format' is the closest matching transcoder format; use -tinydds_info for the full DDS header)\n");
	printf("Has Alpha: %u, sRGB: %u\n", dec.get_has_alpha(), dec.is_srgb() ? 1 : 0);

	// -info reports metadata only. -unpack writes files; -validate just decodes every image to exercise the path.
	if (opts.m_mode == cInfo)
		return true;
	const bool write_files = (opts.m_mode == cUnpack);
	const bool validating = (opts.m_mode == cValidate);
	const bool write_gpu_files = write_files && !opts.m_no_ktx;	// _transcoded_ .ktx/.dds container files
	const bool write_png = write_files && !opts.m_ktx_only;		// decoded _unpacked_rgba_ PNGs
	const bool srgb = dec.is_srgb();
	const bool status_output = opts.m_comp_params.m_status_output;	// -no_status_output / -quiet silences progress

	// -validate exercises every codec like -unpack but writes nothing. We go further than the .ktx2/.basis
	// validate (which only verifies the transcode-to-GPU step): we also round-trip the block data back to pixels
	// below, so a broken unpack path is caught too.
	bool any_format_matched = false;	// for the -unpack_format_only "nothing produced" warning

	const bool is_cubemap = (faces == 6);
	const bool is_array = (num_layers != 0);						// ktx2 convention: 0 == not an array
	const uint32_t MAX_DDS_TEXARRAY_SIZE = 2048;				// Direct3D maximum array size

	// NOTE: the DDS transcoder is currently LDR-only (sources: BC1-5 / BC7 / uncompressed). BC6H/HDR DDS support
	// is planned for a future release (post v2.5); when it lands, the HDR target formats (BC6H, ASTC HDR,
	// RGBA_HALF, RGB_9E5) and their EXR outputs would be added here, mirroring unpack_and_validate_ktx2_file().

	// Block-compressed transcoder targets the DDS transcoder's get_target_info() accepts. Each goes to .ktx;
	// BC1-7 additionally goes to .dds (does_dds_support_format). Listing them explicitly (rather than iterating
	// the whole enum like the .ktx2/.basis unpackers) keeps us to exactly what the DDS transcoder can produce.
	static const basist::transcoder_texture_format s_compressed_formats[] =
	{
		basist::transcoder_texture_format::cTFBC1_RGB, basist::transcoder_texture_format::cTFBC3_RGBA,
		basist::transcoder_texture_format::cTFBC4_R, basist::transcoder_texture_format::cTFBC5_RG,
		basist::transcoder_texture_format::cTFBC7_RGBA, basist::transcoder_texture_format::cTFETC1_RGB,
		basist::transcoder_texture_format::cTFETC2_RGBA, basist::transcoder_texture_format::cTFETC2_EAC_R11,
		basist::transcoder_texture_format::cTFETC2_EAC_RG11, basist::transcoder_texture_format::cTFASTC_4x4_RGBA,
		basist::transcoder_texture_format::cTFPVRTC1_4_RGB, basist::transcoder_texture_format::cTFPVRTC1_4_RGBA
	};

	// Uncompressed LDR transcoder targets the DDS transcoder accepts. These have no GPU-container writer; we
	// transcode them to pixels and write a single _unpacked_rgba_ PNG each (RGBA32 also serves as ground truth).
	static const basist::transcoder_texture_format s_uncompressed_formats[] =
	{
		basist::transcoder_texture_format::cTFRGBA32,
		basist::transcoder_texture_format::cTFRGB565,
		basist::transcoder_texture_format::cTFRGBA4444
	};

	// -unpack_format_only N restricts processing to the single transcoder format whose enum index == N
	// (consistent with the .ktx2/.basis unpackers). Formats not in our supported tables simply produce nothing.
	const int only_fmt = opts.m_unpack_format_only;

	// PVRTC1 needs power-of-2 dims; warn up front so the user understands the per-image skips below.
	if (!is_pow2(dec.get_width()) || !is_pow2(dec.get_height()))
		printf("Warning: dimensions %ux%u are not a power of 2 -- PVRTC1 output will be skipped (it requires power-of-2 dimensions).\n", dec.get_width(), dec.get_height());

	// ============================ Block-compressed targets ============================
	// For each format: transcode every (layer, face) into a full mip chain, round-trip each level back to RGBA
	// (one _unpacked_rgba_ PNG), and write the GPU container files in per-image / cubemap / texture-array shapes.
	for (basist::transcoder_texture_format tex_fmt : s_compressed_formats)
	{
		if ((only_fmt > -1) && ((int)tex_fmt != only_fmt))
			continue;
		any_format_matched = true;

		const basisu::texture_format gpu_fmt = basis_get_basisu_texture_format(tex_fmt);
		const char* pTarget_fmt_name = basist::basis_get_format_name(tex_fmt);
		const bool dds_ok = does_dds_support_format(gpu_fmt);
		const bool ktx_ok = does_ktx_support_format(gpu_fmt);
		if (!dds_ok && !ktx_ok)
			continue;

		// Transcoded mip chains, indexed [layer * faces + face]. Built for both -unpack and -validate so
		// -validate exercises every codec; only -unpack actually writes anything below.
		basisu::vector<gpu_image_vec> chains;
		chains.resize(eff_layers * faces);

		bool format_complete = true;	// false if any image dropped a level (PVRTC1 on non-power-of-2 dims)

		for (uint32_t layer = 0; layer < eff_layers; layer++)
		{
			for (uint32_t face = 0; face < faces; face++)
			{
				gpu_image_vec& chain = chains[layer * faces + face];

				for (uint32_t level = 0; level < levels; level++)
				{
					basist::ktx2_image_level_info level_info;
					if (!dec.get_image_level_info(level_info, level, layer, face))
					{
						error_printf("dds_transcoder::get_image_level_info() failed (layer %u, face %u, level %u)\n", layer, face, level);
						return false;
					}

					gpu_image gi(gpu_fmt, level_info.m_orig_width, level_info.m_orig_height);
					// Pre-fill with pseudo-random bytes so a transcoder that fails to write part of the output
					// is detectable (matches the .ktx2 unpacker's under-write detection).
					fill_buffer_with_random_bytes(gi.get_ptr(), gi.get_size_in_bytes());

					if (status_output)
						printf("Transcoding to %s: layer %u, face %u, level %u, res %ux%u\n", pTarget_fmt_name, layer, face, level, level_info.m_orig_width, level_info.m_orig_height);

					interval_timer tm;
					tm.start();
					const bool transcode_ok = dec.transcode_image_level(level, layer, face, gi.get_ptr(), gi.get_total_blocks(), tex_fmt);
					const double transcode_ms = tm.get_elapsed_ms();

					if (!transcode_ok)
					{
						// PVRTC1 on non-power-of-2 dims is an expected skip (warned up front), NOT a failure. Any
						// other transcode failure is a real error -> fail (so -validate catches a broken codec path).
						if ((tex_fmt == basist::transcoder_texture_format::cTFPVRTC1_4_RGB) || (tex_fmt == basist::transcoder_texture_format::cTFPVRTC1_4_RGBA))
						{
							total_pvrtc_nonpow2_warnings++;
							format_complete = false;
							break;	// can't build a partial mip chain -- drop this whole image
						}
						error_printf("Failed transcoding image to %s (layer %u, face %u, level %u)\n", pTarget_fmt_name, layer, face, level);
						return false;
					}

					if (status_output)
						printf("Transcode to %s succeeded in %3.3f ms\n", pTarget_fmt_name, transcode_ms);

					chain.push_back(gi);
				}
			}
		}

		// (a) Round-trip each transcoded block image back to RGBA. We always do the unpack when -validate (to
		// exercise the unpack path), and write one _unpacked_rgba_ PNG only when actually unpacking to files.
		if (write_png || validating)
		{
			for (uint32_t layer = 0; layer < eff_layers; layer++)
			{
				for (uint32_t face = 0; face < faces; face++)
				{
					const gpu_image_vec& chain = chains[layer * faces + face];
					for (uint32_t level = 0; level < chain.size(); level++)
					{
						image img;
						if (!chain[level].unpack(img, srgb))
						{
							// Under -unpack, mirror the .ktx2 unpacker: a failed round-trip unpack is a warning, and
							// we unpack as much as possible. Under -validate it's a hard failure (catching it is the point).
							error_printf("Failed unpacking %s block data to pixels (layer %u, face %u, level %u)\n", pTarget_fmt_name, layer, face, level);
							if (validating)
								return false;
							total_unpack_warnings++;
							continue;
						}

						if (!write_png)
							continue;	// -validate: exercised the unpack above, but don't write a file

						std::string fn = (levels > 1)
							? base_filename + string_format("_unpacked_rgba_%s_level_%u_face_%u_layer_%04u.png", pTarget_fmt_name, level, face, layer)
							: base_filename + string_format("_unpacked_rgba_%s_face_%u_layer_%04u.png", pTarget_fmt_name, face, layer);
						if (!save_png(fn, img)) { error_printf("Failed writing to PNG file \"%s\"\n", fn.c_str()); return false; }
						printf("Wrote .PNG file \"%s\"\n", fn.c_str());
					}
				}
			}
		}

		// (b) GPU container files: cubemap (per layer), texture array (one DDS), and per-image whole-mip-chain.
		// Skipped entirely if any image was incomplete (format_complete == false), since the chains don't line up.
		if (write_gpu_files && format_complete)
		{
			// Cubemap: one file per layer holding all 6 faces (KTX for any format, DDS for BC1-7).
			if (is_cubemap)
			{
				for (uint32_t layer = 0; layer < eff_layers; layer++)
				{
					basisu::vector<gpu_image_vec> cubemap;
					for (uint32_t face = 0; face < 6; face++)
						cubemap.push_back(chains[layer * faces + face]);

					if (ktx_ok)
					{
						std::string fn = base_filename + string_format("_transcoded_cubemap_%s_layer_%u.ktx", pTarget_fmt_name, layer);
						if (!write_compressed_texture_file(fn.c_str(), cubemap, true, srgb)) { error_printf("Failed writing KTX file \"%s\"\n", fn.c_str()); return false; }
						printf("Wrote .KTX cubemap file \"%s\"\n", fn.c_str());
					}
					if (dds_ok)
					{
						std::string fn = base_filename + string_format("_transcoded_cubemap_%s_layer_%u.dds", pTarget_fmt_name, layer);
						if (!write_compressed_texture_file(fn.c_str(), cubemap, true, srgb)) { error_printf("Failed writing DDS file \"%s\"\n", fn.c_str()); return false; }
						printf("Wrote .DDS cubemap file \"%s\"\n", fn.c_str());
					}
				}
			}

			// Texture array: a single DDS holding every layer x face (DDS only, matching the .ktx2 unpacker).
			if (is_array && dds_ok && (eff_layers <= MAX_DDS_TEXARRAY_SIZE))
			{
				basisu::vector<gpu_image_vec> tex_array;
				for (uint32_t layer = 0; layer < eff_layers; layer++)
					for (uint32_t face = 0; face < faces; face++)
						tex_array.push_back(chains[layer * faces + face]);

				std::string fn = base_filename + string_format("_transcoded_array_%s.dds", pTarget_fmt_name);
				if (!write_compressed_texture_file(fn.c_str(), tex_array, is_cubemap, srgb)) { error_printf("Failed writing DDS file \"%s\"\n", fn.c_str()); return false; }
				printf("Wrote .DDS texture array file \"%s\"\n", fn.c_str());
			}

			// Per-image whole-mip-chain files (one per layer/face).
			for (uint32_t layer = 0; layer < eff_layers; layer++)
			{
				for (uint32_t face = 0; face < faces; face++)
				{
					const gpu_image_vec& chain = chains[layer * faces + face];
					if (!chain.size())
						continue;

					if (ktx_ok)
					{
						std::string fn = is_cubemap
							? base_filename + string_format("_transcoded_%s_face_%u_layer_%04u.ktx", pTarget_fmt_name, face, layer)
							: base_filename + string_format("_transcoded_%s_layer_%04u.ktx", pTarget_fmt_name, layer);
						if (!write_compressed_texture_file(fn.c_str(), chain, srgb)) { error_printf("Failed writing KTX file \"%s\"\n", fn.c_str()); return false; }
						printf("Wrote .KTX file \"%s\"\n", fn.c_str());
					}
					if (dds_ok)
					{
						std::string fn = is_cubemap
							? base_filename + string_format("_transcoded_%s_face_%u_layer_%04u.dds", pTarget_fmt_name, face, layer)
							: base_filename + string_format("_transcoded_%s_layer_%04u.dds", pTarget_fmt_name, layer);
						if (!write_compressed_texture_file(fn.c_str(), chain, srgb)) { error_printf("Failed writing DDS file \"%s\"\n", fn.c_str()); return false; }
						printf("Wrote .DDS file \"%s\"\n", fn.c_str());
					}
				}
			}
		}
	}

	// ============================ Uncompressed LDR targets ============================
	// Transcode the source to each uncompressed pixel format and write one _unpacked_rgba_ PNG per image. This
	// exercises those transcode paths (for -validate) and the RGBA32 decode also serves as the corruption canary.
	for (basist::transcoder_texture_format tex_fmt : s_uncompressed_formats)
	{
		if ((only_fmt > -1) && ((int)tex_fmt != only_fmt))
			continue;
		any_format_matched = true;

		const char* pTarget_fmt_name = basist::basis_get_format_name(tex_fmt);

		for (uint32_t layer = 0; layer < eff_layers; layer++)
		{
			for (uint32_t face = 0; face < faces; face++)
			{
				for (uint32_t level = 0; level < levels; level++)
				{
					basist::ktx2_image_level_info level_info;
					if (!dec.get_image_level_info(level_info, level, layer, face))
					{
						error_printf("dds_transcoder::get_image_level_info() failed (layer %u, face %u, level %u)\n", layer, face, level);
						return false;
					}

					const uint32_t mw = level_info.m_orig_width, mh = level_info.m_orig_height;
					image img(mw, mh);

					if (status_output)
						printf("Transcoding to %s: layer %u, face %u, level %u, res %ux%u\n", pTarget_fmt_name, layer, face, level, mw, mh);

					interval_timer tm;
					double transcode_ms = 0.0;

					if (tex_fmt == basist::transcoder_texture_format::cTFRGBA32)
					{
						// Direct RGBA decode of the source -- the ground-truth pixels (and a corruption canary when
						// -unpack_format_only selects RGBA32; otherwise the compressed loop above fails first).
						// Pre-fill with random bytes to detect a partial (under-)write, like the .ktx2 unpacker.
						fill_buffer_with_random_bytes(img.get_ptr(), (size_t)img.get_total_pixels() * sizeof(color_rgba));
						tm.start();
						const bool transcode_ok = dec.transcode_image_level(level, layer, face, img.get_ptr(), img.get_total_pixels(), tex_fmt, 0, mw, mh);
						transcode_ms = tm.get_elapsed_ms();
						if (!transcode_ok)
						{
							error_printf("Failed decoding image to RGBA32 (layer %u, face %u, level %u) -- invalid or corrupt DDS data\n", layer, face, level);
							return false;
						}
					}
					else
					{
						// 16-bpp packed formats: transcode into a uint16 buffer, then expand to 8-bit RGBA for the PNG.
						basisu::vector<uint16_t> packed(mw * mh);
						// Pre-fill with random bytes to detect a partial (under-)write, like the .ktx2 unpacker.
						fill_buffer_with_random_bytes(packed.data(), packed.size() * sizeof(uint16_t));
						tm.start();
						const bool transcode_ok = dec.transcode_image_level(level, layer, face, packed.data(), (uint32_t)packed.size(), tex_fmt, 0, mw, mh);
						transcode_ms = tm.get_elapsed_ms();
						if (!transcode_ok)
						{
							error_printf("Failed transcoding image to %s (layer %u, face %u, level %u)\n", pTarget_fmt_name, layer, face, level);
							return false;
						}

						for (uint32_t y = 0; y < mh; y++)
						{
							for (uint32_t x = 0; x < mw; x++)
							{
								const uint16_t p = packed[x + y * mw];
								if (tex_fmt == basist::transcoder_texture_format::cTFRGB565)
								{
									uint32_t r = p >> 11, g = (p >> 5) & 63, b = p & 31;
									r = (r << 3) | (r >> 2);
									g = (g << 2) | (g >> 4);
									b = (b << 3) | (b >> 2);
									img(x, y).set(r, g, b, 255);
								}
								else // cTFRGBA4444
								{
									uint32_t r = p >> 12, g = (p >> 8) & 15, b = (p >> 4) & 15, a = p & 15;
									img(x, y).set((r << 4) | r, (g << 4) | g, (b << 4) | b, (a << 4) | a);
								}
							}
						}
					}

					if (status_output)
						printf("Transcode to %s succeeded in %3.3f ms\n", pTarget_fmt_name, transcode_ms);

					if (write_png)
					{
						std::string fn = (levels > 1)
							? base_filename + string_format("_unpacked_rgba_%s_level_%u_face_%u_layer_%04u.png", pTarget_fmt_name, level, face, layer)
							: base_filename + string_format("_unpacked_rgba_%s_face_%u_layer_%04u.png", pTarget_fmt_name, face, layer);
						if (!save_png(fn, img)) { error_printf("Failed writing to PNG file \"%s\"\n", fn.c_str()); return false; }
						printf("Wrote .PNG file \"%s\"\n", fn.c_str());
					}
				}
			}
		}
	}

	// -unpack_format_only asked for a format that isn't a DDS-transcodable target -- say so rather than
	// silently succeeding with no output.
	if ((only_fmt > -1) && !any_format_matched)
		printf("Warning: -unpack_format_only %d is not a DDS-transcodable format; no files were produced.\n", only_fmt);

	return true;
}

// Prints basic header info about a PNG file (used by -info). Uses pv_png::get_png_info (no full decode).
static bool print_png_info(const char* pInput_filename, const uint8_vec& file_data)
{
	pv_png::png_info info;
	if (!pv_png::get_png_info(file_data.data(), file_data.size(), info))
	{
		error_printf("Failed parsing PNG file \"%s\"\n", pInput_filename);
		return false;
	}

	const char* pColor_type = "?";
	switch (info.m_color_type)
	{
	case pv_png::PNG_COLOR_TYPE_GREYSCALE:			pColor_type = "Grayscale"; break;
	case pv_png::PNG_COLOR_TYPE_TRUECOLOR:			pColor_type = "Truecolor (RGB)"; break;
	case pv_png::PNG_COLOR_TYPE_PALETTIZED:			pColor_type = "Palettized"; break;
	case pv_png::PNG_COLOR_TYPE_GREYSCALE_ALPHA:	pColor_type = "Grayscale+Alpha"; break;
	case pv_png::PNG_COLOR_TYPE_TRUECOLOR_ALPHA:	pColor_type = "Truecolor+Alpha (RGBA)"; break;
	default: break;
	}

	printf("Dimensions: %ux%u\n", info.m_width, info.m_height);
	printf("Channels (incl. transparency): %u\n", info.m_num_chans);
	printf("Bit depth: %u\n", info.m_bit_depth);
	printf("Color type: %u (%s)\n", info.m_color_type, pColor_type);
	printf("Colorkey transparency (tRNS): %u\n", info.m_has_trns);
	if (info.m_has_gamma)
		printf("Gamma (gAMA): %f\n", info.m_gamma_value / 100000.0f);

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

		// Honor -output_path when unpacking: both the .basis and .KTX2 unpackers
		// build all their output filenames by prefixing base_filename, so prefixing
		// the output directory here redirects every unpacked file.
		if (opts.m_output_path.size())
			string_combine_path(base_filename, opts.m_output_path.c_str(), base_filename.c_str());

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
			is_ktx2 = (memcmp(file_data.data(), basist::g_ktx2_file_identifier, sizeof(basist::g_ktx2_file_identifier)) == 0);

		// Microsoft DDS magic ("DDS ", 0x20534444 little-endian).
		bool is_dds = false;
		if (file_data.size() >= 4)
			is_dds = (memcmp(file_data.data(), "DDS ", 4) == 0);

		// KTX1 (.ktx) 12-byte identifier. KTX1 holds already-compressed GPU data we can't transcode here,
		// so we just print its header info (via print_ktx_info), regardless of mode.
		static const uint8_t s_ktx1_identifier[12] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
		const bool is_ktx1 = (file_data.size() >= 12) && (memcmp(file_data.data(), s_ktx1_identifier, 12) == 0);

		// .basis container 2-byte signature ("sB", i.e. cBASISSigValue stored little-endian).
		bool is_basis = false;
		if (file_data.size() >= 2)
			is_basis = ((file_data[0] | (file_data[1] << 8)) == basist::basis_file_header::cBASISSigValue);

		// PNG 8-byte signature.
		static const uint8_t s_png_identifier[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
		const bool is_png = (file_data.size() >= 8) && (memcmp(file_data.data(), s_png_identifier, 8) == 0);

		// Detect by magic, not extension, so each file in a mixed input list routes correctly. The fallback
		// (else) path below still attempts a .basis parse for an unrecognized file, which fails with a clear error.
		const char* pFormat = is_ktx2 ? "KTX2" : (is_ktx1 ? "KTX1" : (is_dds ? "DDS" : (is_basis ? ".basis" : (is_png ? "PNG" : "unknown"))));
		printf("\nInput file \"%s\", Format: %s\n", pInput_filename, pFormat);

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
		else if (is_dds)
		{
			status = unpack_and_validate_dds_file(
				base_filename,
				file_data,
				opts,
				total_unpack_warnings,
				total_pvrtc_nonpow2_warnings);
		}
		else if (is_ktx1)
		{
			// KTX1 holds already-compressed GPU data this tool can't transcode. -info dumps the header
			// (same as -ktx_info); -unpack/-validate are unsupported and fail with a clear error rather
			// than a misleading "Success".
			if (opts.m_mode == cInfo)
			{
				status = print_ktx_info(pInput_filename);
			}
			else
			{
				error_printf("KTX1 (.ktx) files can't be unpacked or validated yet -- use -info (or -ktx_info) for header info\n");
				status = false;
			}
		}
		else if (is_png)
		{
			// PNG is a source image, not a GPU texture container: -info dumps its header; unpack/validate don't apply.
			if (opts.m_mode == cInfo)
			{
				status = print_png_info(pInput_filename, file_data);
			}
			else
			{
				error_printf("PNG files can't be unpacked or validated (they're source images, not GPU textures) -- use -info for header info\n");
				status = false;
			}
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

// Reference-free image diagnostics: per-channel RGBA statistics, plus optional single-pixel / 4x4-block dumps.
// Intended to make image problems (e.g. a dropped/constant channel, an all-opaque or all-zero alpha, an
// unexpected channel swizzle) obvious WITHOUT needing a second image to diff against. LDR/8-bit inputs only
// for now (PNG/TGA/QOI/etc); HDR (.exr/.hdr) support can be added later.
static bool image_stats_mode(command_line_params& opts)
{
	if (!opts.m_input_filenames.size())
	{
		error_printf("Must specify at least one image filename using -file\n");
		return false;
	}

	const bool do_stats = opts.m_dump_image_stats;
	const bool do_dump_pixel = (opts.m_dump_pixel_x >= 0) && (opts.m_dump_pixel_y >= 0);
	const bool do_dump_block = (opts.m_dump_block_x >= 0) && (opts.m_dump_block_y >= 0);

	bool any_failed = false;

	for (uint32_t file_index = 0; file_index < opts.m_input_filenames.size(); file_index++)
	{
		const char* pFilename = opts.m_input_filenames[file_index].c_str();

		std::string ext(string_get_extension(opts.m_input_filenames[file_index]));
		if ((strcasecmp(ext.c_str(), "exr") == 0) || (strcasecmp(ext.c_str(), "hdr") == 0))
		{
			error_printf("Skipping HDR image \"%s\" (-image_stats/-dump_* are LDR/8-bit only for now)\n", pFilename);
			any_failed = true;
			continue;
		}

		image img;
		if (!load_image(pFilename, img))
		{
			error_printf("Failed loading image from file \"%s\"!\n", pFilename);
			any_failed = true;
			continue;
		}

		const uint32_t width = img.get_width(), height = img.get_height();
		printf("\nImage \"%s\": %ux%u (%llu pixels), loader reports has_alpha: %u\n",
			pFilename, width, height, (unsigned long long)((uint64_t)width * height), img.has_alpha());

		// ---- Per-channel statistics (single pass over the pixels) ----
		if (do_stats)
		{
			// Resolve the region to gather stats over: the whole image by default, or the user's rectangle
			// (-dump_image_stats <x> <y> <w> <h>). The rectangle must lie fully inside the image.
			uint32_t region_x0 = 0, region_y0 = 0, region_w = width, region_h = height;
			bool region_valid = true;
			if (opts.m_stats_region_w > 0)
			{
				region_x0 = (uint32_t)opts.m_stats_region_x;
				region_y0 = (uint32_t)opts.m_stats_region_y;
				region_w = (uint32_t)opts.m_stats_region_w;
				region_h = (uint32_t)opts.m_stats_region_h;

				if (((uint64_t)region_x0 + region_w > width) || ((uint64_t)region_y0 + region_h > height))
				{
					error_printf("stats region (%u,%u) %ux%u is out of range (image is %ux%u)\n",
						region_x0, region_y0, region_w, region_h, width, height);
					any_failed = true;
					region_valid = false;
				}
				else
				{
					printf("  Stats restricted to region (%u,%u) %ux%u  [x %u..%u, y %u..%u]\n",
						region_x0, region_y0, region_w, region_h,
						region_x0, region_x0 + region_w - 1, region_y0, region_y0 + region_h - 1);
				}
			}

			if (region_valid)
			{
			const uint32_t region_x1 = region_x0 + region_w, region_y1 = region_y0 + region_h;
			const uint64_t total_pixels = (uint64_t)region_w * region_h;

			uint32_t chan_min[4] = { 255, 255, 255, 255 };
			uint32_t chan_max[4] = { 0, 0, 0, 0 };
			double chan_sum[4] = { 0, 0, 0, 0 };
			double chan_sum_sq[4] = { 0, 0, 0, 0 };
			bool seen[4][256];
			memset(seen, 0, sizeof(seen));
			uint64_t alpha_lt_255 = 0, alpha_eq_0 = 0;
			bool premult_possible = true; // true while R,G,B <= A holds for every pixel (a hint of premultiplied alpha)
			bool rgb_identical = true;    // true while R==G==B for every pixel (effectively grayscale / duplicated channel)

			for (uint32_t y = region_y0; y < region_y1; y++)
			{
				for (uint32_t x = region_x0; x < region_x1; x++)
				{
					const color_rgba& c = img(x, y);
					for (uint32_t ch = 0; ch < 4; ch++)
					{
						const uint32_t v = c[ch];
						if (v < chan_min[ch]) chan_min[ch] = v;
						if (v > chan_max[ch]) chan_max[ch] = v;
						chan_sum[ch] += (double)v;
						chan_sum_sq[ch] += (double)v * (double)v;
						seen[ch][v] = true;
					}
					if (c.a < 255) alpha_lt_255++;
					if (c.a == 0) alpha_eq_0++;
					if ((c.r > c.a) || (c.g > c.a) || (c.b > c.a)) premult_possible = false;
					if ((c.r != c.g) || (c.g != c.b)) rgb_identical = false;
				}
			}

			printf("  Chan |  Min |  Max |        Mean |        Variance |     StdDev | Unique | Notes\n");
			static const char chan_names[4] = { 'R', 'G', 'B', 'A' };
			for (uint32_t ch = 0; ch < 4; ch++)
			{
				const double mean = total_pixels ? (chan_sum[ch] / (double)total_pixels) : 0.0;
				double var = total_pixels ? (chan_sum_sq[ch] / (double)total_pixels - mean * mean) : 0.0;
				if (var < 0.0) var = 0.0; // guard against tiny negative floating point error
				const double stddev = sqrt(var);
				uint32_t unique = 0;
				for (uint32_t v = 0; v < 256; v++) unique += seen[ch][v] ? 1u : 0u;

				char notes[64]; notes[0] = '\0';
				if (chan_min[ch] == chan_max[ch])
					snprintf(notes, sizeof(notes), "CONSTANT = %u", chan_min[ch]);

				printf("  %c    | %4u | %4u | %11.4f | %15.4f | %10.4f | %6u | %s\n",
					chan_names[ch], chan_min[ch], chan_max[ch], mean, var, stddev, unique, notes);
			}

			// Alpha-focused summary -- a frequent source of bugs.
			printf("  Alpha: ");
			if (chan_min[3] == 255)
				printf("fully opaque (all 255)\n");
			else if (chan_max[3] == 0)
				printf("fully transparent (all 0)\n");
			else
				printf("varies [%u..%u]; %llu px (%.2f%%) < 255, %llu px (%.2f%%) == 0\n",
					chan_min[3], chan_max[3],
					(unsigned long long)alpha_lt_255, total_pixels ? (100.0 * (double)alpha_lt_255 / (double)total_pixels) : 0.0,
					(unsigned long long)alpha_eq_0, total_pixels ? (100.0 * (double)alpha_eq_0 / (double)total_pixels) : 0.0);

			// Flags that commonly indicate a problem.
			if (img.has_alpha() && (chan_min[3] == 255))
				printf("  Note: image has an alpha channel but every alpha value is 255 (effectively opaque).\n");
			if ((chan_min[3] != 255) && premult_possible)
				printf("  Note: every pixel has R,G,B <= A -- the image may use premultiplied alpha.\n");
			for (uint32_t ch = 0; ch < 3; ch++)
				if (chan_min[ch] == chan_max[ch])
					printf("  Note: %c channel is constant (%u) everywhere -- possible dropped/unused channel.\n", chan_names[ch], chan_min[ch]);
			if (rgb_identical)
				printf("  Note: R, G, B are identical for every pixel -- image is effectively grayscale (or a duplicated/swizzled channel).\n");
			} // region_valid
		}

		// ---- Single-pixel dump ----
		if (do_dump_pixel)
		{
			const uint32_t px = (uint32_t)opts.m_dump_pixel_x, py = (uint32_t)opts.m_dump_pixel_y;
			if ((px >= width) || (py >= height))
				printf("  dump_pixel: (%u,%u) is out of range (image is %ux%u)\n", px, py, width, height);
			else
			{
				const color_rgba& c = img(px, py);
				printf("  Pixel (%u,%u): R=%3u G=%3u B=%3u A=%3u  (hex 0x%02X%02X%02X%02X)\n",
					px, py, c.r, c.g, c.b, c.a, c.r, c.g, c.b, c.a);
			}
		}

		// ---- 4x4 texel-block dump: source texels [bx*4..bx*4+3] x [by*4..by*4+3] (matches the 4x4 grid the
		//      block codecs use, so you can line this up against a transcoded/decoded block). ----
		if (do_dump_block)
		{
			const uint32_t bx = (uint32_t)opts.m_dump_block_x, by = (uint32_t)opts.m_dump_block_y;
			const uint32_t num_blocks_x = (width + 3) / 4, num_blocks_y = (height + 3) / 4;
			if ((bx >= num_blocks_x) || (by >= num_blocks_y))
				printf("  dump_block: block (%u,%u) out of range (image has %ux%u 4x4 blocks)\n", bx, by, num_blocks_x, num_blocks_y);
			else
			{
				printf("  4x4 block (%u,%u) -> texels (%u,%u)..(%u,%u)  [R,G,B,A]:\n", bx, by, bx * 4, by * 4, bx * 4 + 3, by * 4 + 3);
				for (uint32_t ty = 0; ty < 4; ty++)
				{
					const uint32_t sy = by * 4 + ty;
					printf("   ");
					for (uint32_t tx = 0; tx < 4; tx++)
					{
						const uint32_t sx = bx * 4 + tx;
						if ((sx < width) && (sy < height))
						{
							const color_rgba& c = img(sx, sy);
							printf(" [%3u,%3u,%3u,%3u]", c.r, c.g, c.b, c.a);
						}
						else
							printf(" [  -- oob  --  ]");
					}
					printf("\n");
				}
			}
		}
	}

	return !any_failed;
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

	fmt_printf("\nPSNR-HVS and PSNR-HVS-M metrics:\n");
	psnr_hvs_metrics hvs_metrics;
	psnr_hvs_compute_metrics(a, b, hvs_metrics);
	psnr_hvs_print_metrics(hvs_metrics);
	fmt_printf("\n");

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

					snprintf(buf, sizeof(buf), "0");
					strcat(tics, buf);
				}
				else if (((x & 7) == 0) || (x == X_SIZE))
				{
					while ((int)strlen(tics) < x)
						strcat(tics, ".");

					while ((int)strlen(tics2) < x)
						strcat(tics2, " ");

					int v = (x - (int)X_SIZE / 2);
					snprintf(buf, sizeof(buf), "%i", v / 10);
					strcat(tics, buf);

					if (v < 0)
					{
						if (-v < 10)
							snprintf(buf, sizeof(buf), "%i", v % 10);
						else
							snprintf(buf, sizeof(buf), " %i", -v % 10);
					}
					else
						snprintf(buf, sizeof(buf), "%i", v % 10);
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

// -compare_hvs: computes and prints standard image metrics plus PSNR-HVS / PSNR-HVS-M for two LDR images (specified via
// -file). Uses the same psnr_hvs_compute_metrics()/print sequence the old dev test code used - the details matter.
static bool compare_hvs_mode(command_line_params& opts)
{
	if (opts.m_input_filenames.size() != 2)
	{
		error_printf("Must specify two PNG filenames using -file\n");
		return false;
	}

	for (uint32_t i = 0; i < 2; i++)
	{
		std::string ext(string_get_extension(opts.m_input_filenames[i]));
		if ((strcasecmp(ext.c_str(), "exr") == 0) || (strcasecmp(ext.c_str(), "hdr") == 0))
		{
			error_printf("Can't compare HDR image files with this option (LDR only).\n");
			return false;
		}
	}

	image a, b;
	if (!load_png(opts.m_input_filenames[0].c_str(), a))
	{
		error_printf("Failed loading image from file \"%s\"!\n", opts.m_input_filenames[0].c_str());
		return false;
	}
	printf("Loaded \"%s\", %ux%u, has alpha: %u\n", opts.m_input_filenames[0].c_str(), a.get_width(), a.get_height(), a.has_alpha());

	if (!load_png(opts.m_input_filenames[1].c_str(), b))
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

	fmt_printf("Resolution: {}x{}\n", a.get_width(), a.get_height());

	print_image_metrics(a, b);

	psnr_hvs_metrics hvs_metrics;
	if (!psnr_hvs_compute_metrics(a, b, hvs_metrics))
	{
		error_printf("psnr_hvs_compute_metrics() failed!\n");
		return false;
	}
	psnr_hvs_print_metrics(hvs_metrics);

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
	const uint32_t height = minimum(a.get_height(), b.get_height());

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

// Resolves the optional rectangle for the -extract_* modes against an image's dimensions.
// If no region was given (region_w <= 0), returns the whole image. Returns false (with a message) if the rectangle lies outside the image.
static bool resolve_extract_region(const command_line_params& opts, uint32_t width, uint32_t height,
	uint32_t& out_x, uint32_t& out_y, uint32_t& out_w, uint32_t& out_h)
{
	if (opts.m_extract_region_w <= 0)
	{
		out_x = 0; out_y = 0; out_w = width; out_h = height;
		return true;
	}

	out_x = (uint32_t)opts.m_extract_region_x;
	out_y = (uint32_t)opts.m_extract_region_y;
	out_w = (uint32_t)opts.m_extract_region_w;
	out_h = (uint32_t)opts.m_extract_region_h;

	if (((uint64_t)out_x + out_w > width) || ((uint64_t)out_y + out_h > height))
	{
		error_printf("Region (%u,%u) %ux%u is out of range (image is %ux%u)\n", out_x, out_y, out_w, out_h, width, height);
		return false;
	}
	return true;
}

// Applies one sRGB<->linear transform to an 8-bit value. The transform functions take/return normalized [0,1]
// (and assert that range), so we normalize, transform, then round-to-nearest back to 8-bit.
static uint8_t apply_extract_xform_u8(uint8_t v, int xform)
{
	const float f = (float)v * (1.0f / 255.0f);	// exactly within [0,1] for v in [0,255]
	const float g = (xform == cExtractXformToLinear) ? srgb_to_linear(f) : linear_to_srgb(f);
	const int q = (int)(g * 255.0f + 0.5f);		// round to nearest
	return (uint8_t)clamp<int>(q, 0, 255);
}

// Applies the optional color transform to an output image in place: RGB always, alpha only if xform_alpha is set.
static void apply_extract_xform_image(image& img, int xform, bool xform_alpha)
{
	if (xform == cExtractXformNone)
		return;
	const uint32_t num_channels = xform_alpha ? 4u : 3u;
	for (uint32_t y = 0; y < img.get_height(); y++)
		for (uint32_t x = 0; x < img.get_width(); x++)
		{
			color_rgba& c = img(x, y);
			for (uint32_t ch = 0; ch < num_channels; ch++)
				c[ch] = apply_extract_xform_u8(c[ch], xform);
		}
}

// Human-readable suffix describing the active transform (for the "Wrote ..." messages).
static const char* extract_xform_desc(int xform)
{
	if (xform == cExtractXformToLinear) return " [sRGB->linear]";
	if (xform == cExtractXformToSRGB) return " [linear->sRGB]";
	return "";
}

static bool extract_channel_mode(command_line_params& opts)
{
	if (opts.m_input_filenames.size() != 1)
	{
		error_printf("Must specify one image filename using -file\n");
		return false;
	}

	image img;
	if (!load_image(opts.m_input_filenames[0].c_str(), img))
	{
		error_printf("Failed loading image from file \"%s\"!\n", opts.m_input_filenames[0].c_str());
		return false;
	}

	const uint32_t width = img.get_width(), height = img.get_height();
	printf("Loaded \"%s\", %ux%u, has alpha: %u\n", opts.m_input_filenames[0].c_str(), width, height, img.has_alpha());

	uint32_t rx, ry, rw, rh;
	if (!resolve_extract_region(opts, width, height, rx, ry, rw, rh))
		return false;

	// Copy the (possibly cropped) region; only the requested channel is later written out as grayscale.
	const uint32_t channel = (uint32_t)opts.m_extract_channel;
	image out_img(rw, rh);
	for (uint32_t y = 0; y < rh; y++)
		for (uint32_t x = 0; x < rw; x++)
			out_img(x, y) = img(rx + x, ry + y);

	// Optional sRGB<->linear transform on just the extracted channel. Alpha is only transformed if -extract_xform_alpha was given,
	// so for the alpha channel the transform may be requested but not actually applied (reflected in the message below).
	const bool channel_is_alpha = (channel == 3);
	const bool xform_applied = (opts.m_extract_color_xform != cExtractXformNone) && (!channel_is_alpha || opts.m_extract_xform_alpha);
	if (xform_applied)
	{
		for (uint32_t y = 0; y < rh; y++)
			for (uint32_t x = 0; x < rw; x++)
				out_img(x, y)[channel] = apply_extract_xform_u8(out_img(x, y)[channel], opts.m_extract_color_xform);
	}

	if (!save_png(opts.m_extract_filename.c_str(), out_img, cImageSaveGrayscale, channel))
	{
		error_printf("Failed writing file %s\n", opts.m_extract_filename.c_str());
		return false;
	}

	printf("Wrote channel %c of region (%u,%u) %ux%u as grayscale to \"%s\"%s\n",
		"RGBA"[channel], rx, ry, rw, rh, opts.m_extract_filename.c_str(), xform_applied ? extract_xform_desc(opts.m_extract_color_xform) : "");
	return true;
}

static bool extract_swizzle_mode(command_line_params& opts)
{
	if (opts.m_input_filenames.size() != 1)
	{
		error_printf("Must specify one image filename using -file\n");
		return false;
	}

	image img;
	if (!load_image(opts.m_input_filenames[0].c_str(), img))
	{
		error_printf("Failed loading image from file \"%s\"!\n", opts.m_input_filenames[0].c_str());
		return false;
	}

	const uint32_t width = img.get_width(), height = img.get_height();
	printf("Loaded \"%s\", %ux%u, has alpha: %u\n", opts.m_input_filenames[0].c_str(), width, height, img.has_alpha());

	uint32_t rx, ry, rw, rh;
	if (!resolve_extract_region(opts, width, height, rx, ry, rw, rh))
		return false;

	// Build the per-output-channel source map: listed channels are swizzled, the rest pass through unchanged (identity).
	uint32_t src_chan[4] = { 0, 1, 2, 3 };
	for (int i = 0; i < opts.m_extract_swizzle_count; i++)
		src_chan[i] = (uint32_t)opts.m_extract_swizzle[i];

	image out_img(rw, rh);
	for (uint32_t y = 0; y < rh; y++)
	{
		for (uint32_t x = 0; x < rw; x++)
		{
			const color_rgba& s = img(rx + x, ry + y);
			color_rgba& d = out_img(x, y);
			for (uint32_t c = 0; c < 4; c++)
				d[c] = s[src_chan[c]];
		}
	}

	// Optional sRGB<->linear transform: RGB always, alpha only with -extract_xform_alpha.
	apply_extract_xform_image(out_img, opts.m_extract_color_xform, opts.m_extract_xform_alpha);

	if (!save_png(opts.m_extract_filename.c_str(), out_img))
	{
		error_printf("Failed writing file %s\n", opts.m_extract_filename.c_str());
		return false;
	}

	printf("Wrote swizzle R<-%c G<-%c B<-%c A<-%c of region (%u,%u) %ux%u to \"%s\"%s\n",
		"RGBA"[src_chan[0]], "RGBA"[src_chan[1]], "RGBA"[src_chan[2]], "RGBA"[src_chan[3]],
		rx, ry, rw, rh, opts.m_extract_filename.c_str(), extract_xform_desc(opts.m_extract_color_xform));
	return true;
}

static bool extract_region_mode(command_line_params& opts)
{
	if (opts.m_input_filenames.size() != 1)
	{
		error_printf("Must specify one image filename using -file\n");
		return false;
	}

	image img;
	if (!load_image(opts.m_input_filenames[0].c_str(), img))
	{
		error_printf("Failed loading image from file \"%s\"!\n", opts.m_input_filenames[0].c_str());
		return false;
	}

	const uint32_t width = img.get_width(), height = img.get_height();
	printf("Loaded \"%s\", %ux%u, has alpha: %u\n", opts.m_input_filenames[0].c_str(), width, height, img.has_alpha());

	uint32_t rx, ry, rw, rh;
	if (!resolve_extract_region(opts, width, height, rx, ry, rw, rh))
		return false;

	image out_img(rw, rh);
	for (uint32_t y = 0; y < rh; y++)
		for (uint32_t x = 0; x < rw; x++)
			out_img(x, y) = img(rx + x, ry + y);

	// Optional sRGB<->linear transform: RGB always, alpha only with -extract_xform_alpha.
	apply_extract_xform_image(out_img, opts.m_extract_color_xform, opts.m_extract_xform_alpha);

	if (!save_png(opts.m_extract_filename.c_str(), out_img))
	{
		error_printf("Failed writing file %s\n", opts.m_extract_filename.c_str());
		return false;
	}

	printf("Wrote region (%u,%u) %ux%u to \"%s\"%s\n", rx, ry, rw, rh, opts.m_extract_filename.c_str(), extract_xform_desc(opts.m_extract_color_xform));
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

const struct etc1s_uastc_4x4_ldr_test_file
{
	const char* m_pFilename;
	uint32_t m_etc1s_size;
	float m_etc1s_psnr;
	float m_uastc_psnr;
	
	uint32_t m_etc1s_128_size;
    float m_etc1s_128_psnr;
} g_etc1s_uastc_4x4_ldr_test_files[] = 
{
	{ "black_1x1.png", 220, 100.0f, 100.0f, 220, 100.0f },
	{ "kodim01.png", 31003, 27.40f, 44.14f, 58385, 30.356064f },
	{ "kodim02.png", 28560, 32.20f, 41.06f, 51442, 34.713940f },
	{ "kodim03.png", 23442, 32.57f, 44.87f, 49548, 36.709675f },
	{ "kodim04.png", 28287, 31.76f, 43.02f, 57034, 34.864861f },
	{ "kodim05.png", 32677, 25.94f, 40.28f, 65742, 29.935091f },
	{ "kodim06.png", 27367, 28.66f, 44.57f, 54994, 32.294220f },
	{ "kodim07.png", 26649, 31.51f, 43.94f, 53374, 35.576595f },
	{ "kodim08.png", 31164, 25.28f, 41.15f, 63516, 29.509914f },
	{ "kodim09.png", 24808, 32.05f, 45.85f, 51402, 35.985966f },
	{ "kodim10.png", 27278, 32.20f, 45.77f, 54322, 36.395000f },
	{ "kodim11.png", 26610, 29.22f, 43.68f, 55526, 33.468971f },
	{ "kodim12.png", 25133, 32.96f, 46.77f, 51503, 36.722233f },
	{ "kodim13.png", 31635, 24.25f, 41.25f, 62660, 27.588623f },
	{ "kodim14.png", 31193, 27.81f, 39.65f, 62897, 31.206463f },
	{ "kodim15.png", 25559, 31.26f, 42.87f, 53424, 35.026314f },
	{ "kodim16.png", 26925, 32.21f, 47.78f, 51354, 35.555458f },
	{ "kodim17.png", 29365, 31.40f, 45.66f, 55675, 35.909283f },
	{ "kodim18.png", 30960, 27.46f, 41.54f, 62388, 31.348171f },
	{ "kodim19.png", 27920, 29.69f, 44.95f, 55098, 33.613987f },
	{ "kodim20.png", 21135, 31.30f, 45.31f, 47160, 35.759407f },
	{ "kodim21.png", 25974, 28.53f, 44.45f, 54799, 32.415817f },
	{ "kodim22.png", 29111, 29.85f, 42.63f, 60994, 33.495415f },
	{ "kodim23.png", 23825, 31.69f, 45.11f, 53614, 36.223492f },
	{ "kodim24.png", 29644, 26.75f, 40.61f, 58909, 31.522869f },
	{ "white_1x1.png", 220, 100.0f, 100.0f, 220, 100.000000f },
	{ "wikipedia.png", 38992, 24.10f, 30.47f, 69608, 27.630802f },
	{ "alpha0.png", 807, 100.0f, 56.16f, 810, 100.000000f }
};

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

	for (uint32_t i = 0; i < std::size(g_etc1s_uastc_4x4_ldr_test_files); i++)
	{
		const auto& test_file = g_etc1s_uastc_4x4_ldr_test_files[i];

		std::string filename(opts.m_test_file_dir);
		if (filename.size())
		{
			filename.push_back('/');
		}
		filename += std::string(test_file.m_pFilename);

		basisu::vector<image> source_images(1);

		image& source_image = source_images[0];
		if (!load_png(filename.c_str(), source_image))
		{
			error_printf("Failed loading test image \"%s\"\n", filename.c_str());
			return false;
		}

		printf("Loaded file \"%s\", dimensions %ux%u has alpha: %u\n", filename.c_str(), source_image.get_width(), source_image.get_height(), source_image.has_alpha());

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

			float file_size_ratio = fabs((data_size / (float)test_file.m_etc1s_size) - 1.0f);
			if (file_size_ratio > ETC1S_FILESIZE_THRESHOLD)
			{
				error_printf("Expected ETC1S file size was %u, but got %u instead!\n", test_file.m_etc1s_size, (uint32_t)data_size);
				total_mismatches++;
			}

			if (fabs(stats.m_basis_rgba_avg_psnr - test_file.m_etc1s_psnr) > ETC1S_PSNR_THRESHOLD)
			{
				error_printf("Expected ETC1S RGBA Avg PSNR was %f, but got %f instead!\n", test_file.m_etc1s_psnr, stats.m_basis_rgba_avg_psnr);
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

			float file_size_ratio = fabs((data_size / (float)test_file.m_etc1s_128_size) - 1.0f);
			if (file_size_ratio > ETC1S_FILESIZE_THRESHOLD)
			{
				error_printf("Expected ETC1S file size was %u, but got %u instead!\n", test_file.m_etc1s_128_size, (uint32_t)data_size);
				total_mismatches++;
			}

			if (fabs(stats.m_basis_rgba_avg_psnr - test_file.m_etc1s_128_psnr) > ETC1S_PSNR_THRESHOLD)
			{
				error_printf("Expected ETC1S RGBA Avg PSNR was %f, but got %f instead!\n", test_file.m_etc1s_128_psnr, stats.m_basis_rgba_avg_psnr);
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

			float file_size_ratio = fabs((data_size / (float)test_file.m_etc1s_size) - 1.0f);
			if (file_size_ratio > .04f)
			{
				error_printf("Expected ETC1S+OpenCL file size was %u, but got %u instead!\n", test_file.m_etc1s_size, (uint32_t)data_size);
				total_mismatches++;
			}

			if (test_file.m_etc1s_psnr == 100.0f)
			{
				// TODO
				if (stats.m_basis_rgba_avg_psnr < 69.0f)
				{
					error_printf("Expected ETC1S+OpenCL RGBA Avg PSNR was %f, but got %f instead!\n", test_file.m_etc1s_psnr, stats.m_basis_rgba_avg_psnr);
					total_mismatches++;
				}
			}
			else if (fabs(stats.m_basis_rgba_avg_psnr - test_file.m_etc1s_psnr) > .2f)
			{
				error_printf("Expected ETC1S+OpenCL RGBA Avg PSNR was %f, but got %f instead!\n", test_file.m_etc1s_psnr, stats.m_basis_rgba_avg_psnr);
				total_mismatches++;
			}
		}

		// Test UASTC
		{
			printf("**** Testing UASTC LDR 4x4\n");

			flags_and_quality = (opts.m_comp_params.m_multithreading ? cFlagThreaded : 0) | cFlagPrintStats | cFlagPrintStatus;

			void* pData = basis_compress(basist::basis_tex_format::cUASTC_LDR_4x4, source_images, flags_and_quality, uastc_rdo_quality, &data_size, &stats);
			if (!pData)
			{
				error_printf("basis_compress() failed!\n");
				return false;
			}
			basis_free_data(pData);

			printf("UASTC Size: %u, PSNR: %f\n", (uint32_t)data_size, stats.m_basis_rgba_avg_psnr);

			if (fabs(stats.m_basis_rgba_avg_psnr - test_file.m_uastc_psnr) > UASTC_PSNR_THRESHOLD)
			{
				error_printf("Expected UASTC RGBA Avg PSNR was %f, but got %f instead!\n", test_file.m_etc1s_psnr, stats.m_basis_rgba_avg_psnr);
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

		printf("Loaded file \"%s\", dimensions %ux%u\n", filename.c_str(), source_image.get_width(), source_image.get_height());
				
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
				stats.m_basis_rgb_avg_astc_hdr_log2_psnr, pTest_files[i].m_level_psnr_astc[uastc_hdr_level], delta1 = fabs(stats.m_basis_rgb_avg_astc_hdr_log2_psnr - pTest_files[i].m_level_psnr_astc[uastc_hdr_level]),
				stats.m_basis_rgb_avg_bc6h_log2_psnr, pTest_files[i].m_level_psnr_bc6h[uastc_hdr_level], delta2 = fabs(stats.m_basis_rgb_avg_bc6h_log2_psnr - pTest_files[i].m_level_psnr_bc6h[uastc_hdr_level]));

			highest_delta = maximum(highest_delta, delta1);
			highest_delta = maximum(highest_delta, delta2);

			if (fabs(stats.m_basis_rgb_avg_astc_hdr_log2_psnr - pTest_files[i].m_level_psnr_astc[uastc_hdr_level]) > PSNR_THRESHOLD)
			{
				error_printf("Expected UASTC HDR RGB Avg PSNR was %f, but got %f instead!\n", pTest_files[i].m_level_psnr_astc[uastc_hdr_level], stats.m_basis_rgb_avg_astc_hdr_log2_psnr);
				total_mismatches++;
			}

			if (fabs(stats.m_basis_rgb_avg_bc6h_log2_psnr - pTest_files[i].m_level_psnr_bc6h[uastc_hdr_level]) > PSNR_THRESHOLD)
			{
				error_printf("Expected UASTC/ASTC->BC6H HDR RGB Avg PSNR was %f, but got %f instead!\n", pTest_files[i].m_level_psnr_bc6h[uastc_hdr_level], stats.m_basis_rgb_avg_bc6h_log2_psnr);
				total_mismatches++;
			}

			astc_psnr(i, uastc_hdr_level) = stats.m_basis_rgb_avg_astc_hdr_log2_psnr;
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

const uint32_t XUASTC_LDR_TEST_FILE_NUM_RUNS = 3;

struct xuastc_ldr_test_file
{
	const char* m_pFilename;

	struct test_run
	{
		float m_dct_q;
		uint32_t m_comp_size;
		float m_rgba_psnr;
	};

	test_run m_test_runs[XUASTC_LDR_TEST_FILE_NUM_RUNS];
};

xuastc_ldr_test_file g_xuastc_ldr_test_files_6x6[] =
{
	{ "black_1x1.png", { { 100.000000f, 142, 100.000000f },  { 75.000000f, 143, 100.000000f },  { 35.000000f, 143, 100.000000f } } },
	{ "kodim01.png", { { 100.000000f, 143338, 37.631470f },  { 75.000000f, 132606, 34.858498f },  { 35.000000f, 112536, 31.930468f } } },
	{ "kodim02.png", { { 100.000000f, 136308, 40.657822f },  { 75.000000f, 101839, 38.057598f },  { 35.000000f, 81085, 36.128090f } } },
	{ "kodim03.png", { { 100.000000f, 133461, 43.046989f },  { 75.000000f, 86361, 39.970840f },  { 35.000000f, 69200, 37.480732f } } },
	{ "kodim04.png", { { 100.000000f, 139788, 41.014957f },  { 75.000000f, 101258, 38.003296f },  { 35.000000f, 83902, 35.965012f } } },
	{ "kodim05.png", { { 100.000000f, 148943, 36.247421f },  { 75.000000f, 138468, 34.564281f },  { 35.000000f, 122126, 31.844128f } } },
	{ "kodim06.png", { { 100.000000f, 137452, 39.072002f },  { 75.000000f, 113603, 36.145702f },  { 35.000000f, 93972, 33.209007f } } },
	{ "kodim07.png", { { 100.000000f, 137948, 41.442890f },  { 75.000000f, 98386, 39.420319f },  { 35.000000f, 83659, 37.224255f } } },
	{ "kodim08.png", { { 100.000000f, 147884, 36.300182f },  { 75.000000f, 134661, 34.453938f },  { 35.000000f, 118202, 31.751554f } } },
	{ "kodim09.png", { { 100.000000f, 136609, 42.575459f },  { 75.000000f, 83721, 39.448776f },  { 35.000000f, 68192, 37.360222f } } },
	{ "kodim10.png", { { 100.000000f, 138614, 42.184914f },  { 75.000000f, 94047, 39.332645f },  { 35.000000f, 77621, 37.126518f } } },
	{ "kodim11.png", { { 100.000000f, 139548, 39.074104f },  { 75.000000f, 113306, 36.617252f },  { 35.000000f, 91571, 34.055752f } } },
	{ "kodim12.png", { { 100.000000f, 134259, 43.179798f },  { 75.000000f, 90453, 39.607330f },  { 35.000000f, 70234, 37.351040f } } },
	{ "kodim13.png", { { 100.000000f, 144618, 34.267788f },  { 75.000000f, 141867, 32.225410f },  { 35.000000f, 120881, 29.205767f } } },
	{ "kodim14.png", { { 100.000000f, 143775, 37.229355f },  { 75.000000f, 129872, 35.301926f },  { 35.000000f, 109048, 33.015049f } } },
	{ "kodim15.png", { { 100.000000f, 136213, 40.758198f },  { 75.000000f, 92132, 38.082176f },  { 35.000000f, 73418, 35.928169f } } },
	{ "kodim16.png", { { 100.000000f, 135975, 42.594410f },  { 75.000000f, 103275, 38.658211f },  { 35.000000f, 83547, 35.843338f } } },
	{ "kodim17.png", { { 100.000000f, 140399, 40.927052f },  { 75.000000f, 100822, 38.284622f },  { 35.000000f, 83755, 35.921692f } } },
	{ "kodim18.png", { { 100.000000f, 145298, 36.728806f },  { 75.000000f, 122486, 34.681892f },  { 35.000000f, 102720, 32.151287f } } },
	{ "kodim19.png", { { 100.000000f, 140296, 40.024555f },  { 75.000000f, 107528, 36.965836f },  { 35.000000f, 89350, 34.593811f } } },
	{ "kodim20.png", { { 100.000000f, 122203, 41.429142f },  { 75.000000f, 79399, 38.525173f },  { 35.000000f, 62388, 36.222507f } } },
	{ "kodim21.png", { { 100.000000f, 140177, 38.664650f },  { 75.000000f, 104123, 36.225704f },  { 35.000000f, 85020, 33.629925f } } },
	{ "kodim22.png", { { 100.000000f, 143846, 38.921207f },  { 75.000000f, 114520, 36.468395f },  { 35.000000f, 94574, 34.228367f } } },
	{ "kodim23.png", { { 100.000000f, 139961, 42.794750f },  { 75.000000f, 89146, 40.377716f },  { 35.000000f, 75422, 38.508953f } } },
	{ "kodim24.png", { { 100.000000f, 140310, 36.486050f },  { 75.000000f, 120326, 34.828285f },  { 35.000000f, 102832, 32.087605f } } },
	{ "white_1x1.png", { { 100.000000f, 142, 100.000000f },  { 75.000000f, 143, 100.000000f },  { 35.000000f, 143, 100.000000f } } },
	{ "wikipedia.png", { { 100.000000f, 190480, 32.429928f },  { 75.000000f, 173407, 32.292751f },  { 35.000000f, 168150, 31.088394f } } },
	//{ "alpha0.png", { { 100.000000f, 1389, 49.883366f },  { 75.000000f, 1385, 49.125038f },  { 35.000000f, 1479, 42.865246f } } } // alpha0.png is minor nightmare for testing XUASTC LDR because it's very sensitive to tiny FP differences
};

xuastc_ldr_test_file g_xuastc_ldr_test_files_4x4[] =
{
	{ "black_1x1.png", { { 100.000000f, 142, 100.000000f },  { 75.000000f, 143, 100.000000f },  { 35.000000f, 143, 100.000000f } } },
	{ "kodim01.png", { { 100.000000f, 310218, 47.030079f },  { 75.000000f, 255162, 38.170372f },  { 35.000000f, 210620, 34.740185f } } },
	{ "kodim02.png", { { 100.000000f, 294357, 47.193066f },  { 75.000000f, 194442, 40.707787f },  { 35.000000f, 163870, 38.593933f } } },
	{ "kodim03.png", { { 100.000000f, 283110, 49.651531f },  { 75.000000f, 169617, 42.514809f },  { 35.000000f, 137983, 39.903645f } } },
	{ "kodim04.png", { { 100.000000f, 301581, 47.235493f },  { 75.000000f, 197552, 40.555023f },  { 35.000000f, 170542, 38.400024f } } },
	{ "kodim05.png", { { 100.000000f, 322641, 43.768642f },  { 75.000000f, 265551, 38.349422f },  { 35.000000f, 229721, 34.853809f } } },
	{ "kodim06.png", { { 100.000000f, 297732, 46.904057f },  { 75.000000f, 222107, 39.441322f },  { 35.000000f, 183761, 36.179142f } } },
	{ "kodim07.png", { { 100.000000f, 296921, 47.893196f },  { 75.000000f, 193982, 42.567802f },  { 35.000000f, 164339, 39.916515f } } },
	{ "kodim08.png", { { 100.000000f, 321301, 44.123753f },  { 75.000000f, 266214, 38.408657f },  { 35.000000f, 227531, 34.939663f } } },
	{ "kodim09.png", { { 100.000000f, 297129, 49.033165f },  { 75.000000f, 173342, 41.962223f },  { 35.000000f, 141332, 39.750557f } } },
	{ "kodim10.png", { { 100.000000f, 299737, 48.689640f },  { 75.000000f, 185178, 41.973049f },  { 35.000000f, 156008, 39.650200f } } },
	{ "kodim11.png", { { 100.000000f, 299730, 46.588585f },  { 75.000000f, 215553, 39.980545f },  { 35.000000f, 180852, 36.994793f } } },
	{ "kodim12.png", { { 100.000000f, 289459, 49.914066f },  { 75.000000f, 175999, 42.131413f },  { 35.000000f, 145621, 39.855633f } } },
	{ "kodim13.png", { { 100.000000f, 317109, 43.169689f },  { 75.000000f, 274743, 36.435371f },  { 35.000000f, 230455, 32.394821f } } },
	{ "kodim14.png", { { 100.000000f, 311466, 44.403767f },  { 75.000000f, 241362, 38.612110f },  { 35.000000f, 206563, 35.850140f } } },
	{ "kodim15.png", { { 100.000000f, 290996, 47.307865f },  { 75.000000f, 181963, 40.791069f },  { 35.000000f, 147984, 38.407547f } } },
	{ "kodim16.png", { { 100.000000f, 293439, 49.771076f },  { 75.000000f, 198284, 41.343952f },  { 35.000000f, 163840, 38.560184f } } },
	{ "kodim17.png", { { 100.000000f, 303429, 48.048100f },  { 75.000000f, 194161, 41.256020f },  { 35.000000f, 166376, 38.627644f } } },
	{ "kodim18.png", { { 100.000000f, 316790, 43.609463f },  { 75.000000f, 237898, 38.175785f },  { 35.000000f, 205271, 35.113762f } } },
	{ "kodim19.png", { { 100.000000f, 305786, 47.092293f },  { 75.000000f, 213024, 39.941761f },  { 35.000000f, 184103, 37.369053f } } },
	{ "kodim20.png", { { 100.000000f, 259950, 48.652317f },  { 75.000000f, 155659, 41.508587f },  { 35.000000f, 124257, 38.938896f } } },
	{ "kodim21.png", { { 100.000000f, 303954, 46.846046f },  { 75.000000f, 209365, 39.592907f },  { 35.000000f, 172113, 36.425140f } } },
	{ "kodim22.png", { { 100.000000f, 312642, 45.427174f },  { 75.000000f, 221411, 39.440510f },  { 35.000000f, 189278, 36.970325f } } },
	{ "kodim23.png", { { 100.000000f, 298345, 48.780674f },  { 75.000000f, 175964, 42.915535f },  { 35.000000f, 148486, 40.830143f } } },
	{ "kodim24.png", { { 100.000000f, 302608, 44.029816f },  { 75.000000f, 229249, 39.054703f },  { 35.000000f, 196935, 35.450607f } } },
	{ "white_1x1.png", { { 100.000000f, 142, 100.000000f },  { 75.000000f, 143, 100.000000f },  { 35.000000f, 143, 100.000000f } } },
	{ "wikipedia.png", { { 100.000000f, 360418, 39.591961f },  { 75.000000f, 335408, 38.473557f },  { 35.000000f, 318023, 35.556229f } } },
};
xuastc_ldr_test_file g_xuastc_ldr_test_files_5x4[] =
{
	{ "black_1x1.png", { { 100.000000f, 142, 100.000000f },  { 75.000000f, 143, 100.000000f },  { 35.000000f, 143, 100.000000f } } },
	{ "kodim01.png", { { 100.000000f, 249211, 44.451683f },  { 75.000000f, 210509, 36.881168f },  { 35.000000f, 168587, 33.625820f } } },
	{ "kodim02.png", { { 100.000000f, 238603, 45.184010f },  { 75.000000f, 159386, 39.738209f },  { 35.000000f, 129436, 37.669529f } } },
	{ "kodim03.png", { { 100.000000f, 230397, 47.695618f },  { 75.000000f, 136798, 41.549713f },  { 35.000000f, 110021, 39.007256f } } },
	{ "kodim04.png", { { 100.000000f, 244119, 45.411850f },  { 75.000000f, 159148, 39.640678f },  { 35.000000f, 135726, 37.497944f } } },
	{ "kodim05.png", { { 100.000000f, 260814, 41.621056f },  { 75.000000f, 219953, 37.053875f },  { 35.000000f, 187075, 33.647770f } } },
	{ "kodim06.png", { { 100.000000f, 240122, 44.883125f },  { 75.000000f, 179382, 38.298389f },  { 35.000000f, 146560, 35.105358f } } },
	{ "kodim07.png", { { 100.000000f, 241474, 45.955803f },  { 75.000000f, 157918, 41.505169f },  { 35.000000f, 134039, 38.954472f } } },
	{ "kodim08.png", { { 100.000000f, 259620, 41.868359f },  { 75.000000f, 218900, 37.081158f },  { 35.000000f, 185281, 33.697517f } } },
	{ "kodim09.png", { { 100.000000f, 240072, 47.183537f },  { 75.000000f, 136701, 41.090607f },  { 35.000000f, 111111, 38.871655f } } },
	{ "kodim10.png", { { 100.000000f, 242688, 46.832684f },  { 75.000000f, 148827, 41.053978f },  { 35.000000f, 124419, 38.726421f } } },
	{ "kodim11.png", { { 100.000000f, 243015, 44.532112f },  { 75.000000f, 175753, 38.829491f },  { 35.000000f, 144384, 35.890057f } } },
	{ "kodim12.png", { { 100.000000f, 234426, 47.991623f },  { 75.000000f, 141044, 41.192932f },  { 35.000000f, 115000, 38.965549f } } },
	{ "kodim13.png", { { 100.000000f, 253290, 40.965210f },  { 75.000000f, 226116, 35.000126f },  { 35.000000f, 184823, 31.168385f } } },
	{ "kodim14.png", { { 100.000000f, 252166, 42.261086f },  { 75.000000f, 198054, 37.443531f },  { 35.000000f, 165845, 34.783745f } } },
	{ "kodim15.png", { { 100.000000f, 236533, 45.223949f },  { 75.000000f, 146577, 39.803493f },  { 35.000000f, 118822, 37.435871f } } },
	{ "kodim16.png", { { 100.000000f, 237555, 47.910408f },  { 75.000000f, 161093, 40.372368f },  { 35.000000f, 131482, 37.610260f } } },
	{ "kodim17.png", { { 100.000000f, 245639, 46.034199f },  { 75.000000f, 155299, 40.225517f },  { 35.000000f, 132135, 37.615314f } } },
	{ "kodim18.png", { { 100.000000f, 256813, 41.648430f },  { 75.000000f, 194254, 36.990253f },  { 35.000000f, 164193, 33.996323f } } },
	{ "kodim19.png", { { 100.000000f, 247269, 45.170345f },  { 75.000000f, 170744, 38.804966f },  { 35.000000f, 144924, 36.359180f } } },
	{ "kodim20.png", { { 100.000000f, 213141, 46.714931f },  { 75.000000f, 125433, 40.431580f },  { 35.000000f, 98170, 37.925316f } } },
	{ "kodim21.png", { { 100.000000f, 244810, 44.717579f },  { 75.000000f, 168321, 38.396568f },  { 35.000000f, 135060, 35.355556f } } },
	{ "kodim22.png", { { 100.000000f, 252431, 43.553535f },  { 75.000000f, 179142, 38.341515f },  { 35.000000f, 150546, 35.956043f } } },
	{ "kodim23.png", { { 100.000000f, 242860, 46.938763f },  { 75.000000f, 142099, 42.049194f },  { 35.000000f, 119089, 39.947498f } } },
	{ "kodim24.png", { { 100.000000f, 244829, 41.747345f },  { 75.000000f, 189205, 37.679653f },  { 35.000000f, 159214, 34.212723f } } },
	{ "white_1x1.png", { { 100.000000f, 142, 100.000000f },  { 75.000000f, 143, 100.000000f },  { 35.000000f, 143, 100.000000f } } },
	{ "wikipedia.png", { { 100.000000f, 307678, 37.244614f },  { 75.000000f, 282188, 36.445412f },  { 35.000000f, 269165, 33.900223f } } },
};
xuastc_ldr_test_file g_xuastc_ldr_test_files_8x8[] =
{
	{ "black_1x1.png", { { 100.000000f, 142, 100.000000f },  { 75.000000f, 143, 100.000000f },  { 35.000000f, 143, 100.000000f } } },
	{ "kodim01.png", { { 100.000000f, 82815, 33.129543f },  { 75.000000f, 80163, 32.320469f },  { 35.000000f, 72960, 30.570271f } } },
	{ "kodim02.png", { { 100.000000f, 78367, 37.343704f },  { 75.000000f, 64920, 36.143528f },  { 35.000000f, 51500, 34.768459f } } },
	{ "kodim03.png", { { 100.000000f, 77599, 39.404751f },  { 75.000000f, 55460, 38.039928f },  { 35.000000f, 44970, 36.254837f } } },
	{ "kodim04.png", { { 100.000000f, 80792, 37.649517f },  { 75.000000f, 64576, 36.303604f },  { 35.000000f, 54242, 34.725628f } } },
	{ "kodim05.png", { { 100.000000f, 84711, 32.105507f },  { 75.000000f, 84081, 31.671597f },  { 35.000000f, 76634, 30.126841f } } },
	{ "kodim06.png", { { 100.000000f, 79855, 34.817501f },  { 75.000000f, 69603, 33.753742f },  { 35.000000f, 61087, 31.827003f } } },
	{ "kodim07.png", { { 100.000000f, 79449, 37.754784f },  { 75.000000f, 62135, 37.021587f },  { 35.000000f, 53152, 35.554993f } } },
	{ "kodim08.png", { { 100.000000f, 84365, 31.781384f },  { 75.000000f, 81169, 31.304081f },  { 35.000000f, 74352, 29.816698f } } },
	{ "kodim09.png", { { 100.000000f, 78971, 38.773418f },  { 75.000000f, 53689, 37.463520f },  { 35.000000f, 43699, 35.936676f } } },
	{ "kodim10.png", { { 100.000000f, 80147, 38.478813f },  { 75.000000f, 59821, 37.286610f },  { 35.000000f, 49992, 35.712532f } } },
	{ "kodim11.png", { { 100.000000f, 80255, 35.045494f },  { 75.000000f, 70796, 34.217140f },  { 35.000000f, 60030, 32.591511f } } },
	{ "kodim12.png", { { 100.000000f, 78152, 39.493549f },  { 75.000000f, 58381, 37.853058f },  { 35.000000f, 46241, 36.070549f } } },
	{ "kodim13.png", { { 100.000000f, 83603, 29.707319f },  { 75.000000f, 83231, 29.235767f },  { 35.000000f, 77262, 27.668018f } } },
	{ "kodim14.png", { { 100.000000f, 82796, 33.518745f },  { 75.000000f, 80312, 32.885574f },  { 35.000000f, 70597, 31.453365f } } },
	{ "kodim15.png", { { 100.000000f, 79262, 37.283611f },  { 75.000000f, 59147, 36.219261f },  { 35.000000f, 48554, 34.670193f } } },
	{ "kodim16.png", { { 100.000000f, 78792, 38.528458f },  { 75.000000f, 65336, 36.765572f },  { 35.000000f, 54384, 34.544426f } } },
	{ "kodim17.png", { { 100.000000f, 81297, 37.139481f },  { 75.000000f, 65278, 36.174728f },  { 35.000000f, 55032, 34.575806f } } },
	{ "kodim18.png", { { 100.000000f, 83288, 32.887947f },  { 75.000000f, 75361, 32.249966f },  { 35.000000f, 65759, 30.717367f } } },
	{ "kodim19.png", { { 100.000000f, 80857, 36.188713f },  { 75.000000f, 65937, 34.959888f },  { 35.000000f, 56324, 33.180534f } } },
	{ "kodim20.png", { { 100.000000f, 72423, 37.353615f },  { 75.000000f, 49889, 36.328712f },  { 35.000000f, 41065, 34.822941f } } },
	{ "kodim21.png", { { 100.000000f, 80646, 34.472473f },  { 75.000000f, 63150, 33.743328f },  { 35.000000f, 52850, 32.177635f } } },
	{ "kodim22.png", { { 100.000000f, 82579, 35.452717f },  { 75.000000f, 71076, 34.477604f },  { 35.000000f, 60499, 32.928665f } } },
	{ "kodim23.png", { { 100.000000f, 80795, 39.518650f },  { 75.000000f, 57122, 38.509109f },  { 35.000000f, 48485, 37.112495f } } },
	{ "kodim24.png", { { 100.000000f, 80427, 32.505745f },  { 75.000000f, 73730, 32.054939f },  { 35.000000f, 65558, 30.470564f } } },
	{ "white_1x1.png", { { 100.000000f, 142, 100.000000f },  { 75.000000f, 143, 100.000000f },  { 35.000000f, 143, 100.000000f } } },
	{ "wikipedia.png", { { 100.000000f, 112186, 29.022635f },  { 75.000000f, 109799, 29.001532f },  { 35.000000f, 109059, 28.538498f } } },
};
xuastc_ldr_test_file g_xuastc_ldr_test_files_10x5[] =
{
	{ "black_1x1.png", { { 100.000000f, 142, 100.000000f },  { 75.000000f, 143, 100.000000f },  { 35.000000f, 143, 100.000000f } } },
	{ "kodim01.png", { { 100.000000f, 105404, 34.920200f },  { 75.000000f, 100965, 33.497791f },  { 35.000000f, 90611, 31.226894f } } },
	{ "kodim02.png", { { 100.000000f, 99914, 38.595970f },  { 75.000000f, 80665, 36.986778f },  { 35.000000f, 64270, 35.383049f } } },
	{ "kodim03.png", { { 100.000000f, 98251, 40.790932f },  { 75.000000f, 67869, 38.898525f },  { 35.000000f, 54886, 36.762691f } } },
	{ "kodim04.png", { { 100.000000f, 102414, 38.946281f },  { 75.000000f, 79893, 37.033978f },  { 35.000000f, 66785, 35.246025f } } },
	{ "kodim05.png", { { 100.000000f, 108302, 33.642941f },  { 75.000000f, 104798, 32.862797f },  { 35.000000f, 95050, 30.865231f } } },
	{ "kodim06.png", { { 100.000000f, 101243, 36.626984f },  { 75.000000f, 85666, 34.866810f },  { 35.000000f, 71982, 32.394196f } } },
	{ "kodim07.png", { { 100.000000f, 101129, 39.211941f },  { 75.000000f, 76369, 38.055550f },  { 35.000000f, 65894, 36.308075f } } },
	{ "kodim08.png", { { 100.000000f, 107566, 33.244900f },  { 75.000000f, 101592, 32.423939f },  { 35.000000f, 92874, 30.506023f } } },
	{ "kodim09.png", { { 100.000000f, 100574, 40.205162f },  { 75.000000f, 63935, 38.267521f },  { 35.000000f, 52720, 36.494003f } } },
	{ "kodim10.png", { { 100.000000f, 101792, 39.791870f },  { 75.000000f, 74618, 38.100864f },  { 35.000000f, 60932, 36.214828f } } },
	{ "kodim11.png", { { 100.000000f, 101858, 36.660149f },  { 75.000000f, 86801, 35.305096f },  { 35.000000f, 72462, 33.252529f } } },
	{ "kodim12.png", { { 100.000000f, 99075, 40.883018f },  { 75.000000f, 69568, 38.559566f },  { 35.000000f, 54291, 36.616749f } } },
	{ "kodim13.png", { { 100.000000f, 106502, 31.561871f },  { 75.000000f, 106013, 30.642658f },  { 35.000000f, 95531, 28.429173f } } },
	{ "kodim14.png", { { 100.000000f, 105379, 35.074528f },  { 75.000000f, 98775, 33.989864f },  { 35.000000f, 84924, 32.170376f } } },
	{ "kodim15.png", { { 100.000000f, 100550, 38.506466f },  { 75.000000f, 73302, 36.981686f },  { 35.000000f, 60268, 35.197380f } } },
	{ "kodim16.png", { { 100.000000f, 99833, 40.386166f },  { 75.000000f, 79010, 37.707489f },  { 35.000000f, 63900, 35.129288f } } },
	{ "kodim17.png", { { 100.000000f, 102975, 38.561710f },  { 75.000000f, 79270, 37.071556f },  { 35.000000f, 66965, 35.134377f } } },
	{ "kodim18.png", { { 100.000000f, 107301, 34.268379f },  { 75.000000f, 95912, 33.241734f },  { 35.000000f, 83236, 31.308125f } } },
	{ "kodim19.png", { { 100.000000f, 103796, 37.636776f },  { 75.000000f, 82419, 35.821537f },  { 35.000000f, 69996, 33.751148f } } },
	{ "kodim20.png", { { 100.000000f, 91331, 38.943798f },  { 75.000000f, 61619, 37.297562f },  { 35.000000f, 49093, 35.451199f } } },
	{ "kodim21.png", { { 100.000000f, 102857, 36.242493f },  { 75.000000f, 77121, 34.898647f },  { 35.000000f, 64434, 32.857597f } } },
	{ "kodim22.png", { { 100.000000f, 104989, 36.767838f },  { 75.000000f, 88040, 35.332642f },  { 35.000000f, 74724, 33.503368f } } },
	{ "kodim23.png", { { 100.000000f, 102295, 40.710884f },  { 75.000000f, 71039, 39.289032f },  { 35.000000f, 60360, 37.647507f } } },
	{ "kodim24.png", { { 100.000000f, 102518, 34.120747f },  { 75.000000f, 92503, 33.321411f },  { 35.000000f, 81974, 31.218332f } } },
	{ "white_1x1.png", { { 100.000000f, 142, 100.000000f },  { 75.000000f, 143, 100.000000f },  { 35.000000f, 143, 100.000000f } } },
	{ "wikipedia.png", { { 100.000000f, 138617, 30.214272f },  { 75.000000f, 130237, 30.166813f },  { 35.000000f, 127678, 29.492228f } } },
};
xuastc_ldr_test_file g_xuastc_ldr_test_files_10x10[] =
{
	{ "black_1x1.png", { { 100.000000f, 164, 100.000000f },  { 75.000000f, 165, 100.000000f },  { 35.000000f, 165, 100.000000f } } },
	{ "kodim01.png", { { 100.000000f, 53866, 30.283733f },  { 75.000000f, 53616, 30.075840f },  { 35.000000f, 50347, 29.163128f } } },
	{ "kodim02.png", { { 100.000000f, 51026, 35.123016f },  { 75.000000f, 46980, 34.621269f },  { 35.000000f, 37703, 33.580551f } } },
	{ "kodim03.png", { { 100.000000f, 51031, 36.793110f },  { 75.000000f, 41431, 36.207535f },  { 35.000000f, 34960, 35.044708f } } },
	{ "kodim04.png", { { 100.000000f, 53041, 35.332870f },  { 75.000000f, 46533, 34.796043f },  { 35.000000f, 39862, 33.541615f } } },
	{ "kodim05.png", { { 100.000000f, 55189, 29.119299f },  { 75.000000f, 55108, 29.015081f },  { 35.000000f, 51969, 28.237648f } } },
	{ "kodim06.png", { { 100.000000f, 51951, 31.890593f },  { 75.000000f, 48228, 31.475647f },  { 35.000000f, 43607, 30.357023f } } },
	{ "kodim07.png", { { 100.000000f, 51629, 35.143024f },  { 75.000000f, 45495, 34.901260f },  { 35.000000f, 39163, 33.916348f } } },
	{ "kodim08.png", { { 100.000000f, 54592, 28.505661f },  { 75.000000f, 54535, 28.359890f },  { 35.000000f, 51421, 27.674751f } } },
	{ "kodim09.png", { { 100.000000f, 51854, 35.922009f },  { 75.000000f, 40015, 35.579926f },  { 35.000000f, 34664, 34.545792f } } },
	{ "kodim10.png", { { 100.000000f, 52237, 35.590847f },  { 75.000000f, 44712, 35.157581f },  { 35.000000f, 37465, 34.189934f } } },
	{ "kodim11.png", { { 100.000000f, 53091, 32.386562f },  { 75.000000f, 49709, 32.083656f },  { 35.000000f, 43594, 31.182316f } } },
	{ "kodim12.png", { { 100.000000f, 50822, 36.772594f },  { 75.000000f, 43481, 36.005375f },  { 35.000000f, 35465, 34.854721f } } },
	{ "kodim13.png", { { 100.000000f, 54490, 26.917747f },  { 75.000000f, 54727, 26.806589f },  { 35.000000f, 52410, 26.057467f } } },
	{ "kodim14.png", { { 100.000000f, 54156, 30.993803f },  { 75.000000f, 53868, 30.794903f },  { 35.000000f, 50038, 29.964369f } } },
	{ "kodim15.png", { { 100.000000f, 52157, 35.004284f },  { 75.000000f, 43205, 34.554562f },  { 35.000000f, 36555, 33.478485f } } },
	{ "kodim16.png", { { 100.000000f, 51024, 35.731007f },  { 75.000000f, 46169, 34.875183f },  { 35.000000f, 40174, 33.368984f } } },
	{ "kodim17.png", { { 100.000000f, 53562, 34.584801f },  { 75.000000f, 46775, 34.203426f },  { 35.000000f, 40370, 33.207817f } } },
	{ "kodim18.png", { { 100.000000f, 54710, 30.220909f },  { 75.000000f, 52152, 30.071476f },  { 35.000000f, 47304, 29.228931f } } },
	{ "kodim19.png", { { 100.000000f, 52609, 33.399029f },  { 75.000000f, 46521, 32.911697f },  { 35.000000f, 40742, 31.708160f } } },
	{ "kodim20.png", { { 100.000000f, 48131, 34.454784f },  { 75.000000f, 38410, 34.073807f },  { 35.000000f, 32410, 33.349335f } } },
	{ "kodim21.png", { { 100.000000f, 52704, 31.636679f },  { 75.000000f, 45878, 31.425013f },  { 35.000000f, 39147, 30.604250f } } },
	{ "kodim22.png", { { 100.000000f, 53864, 33.018543f },  { 75.000000f, 49948, 32.652431f },  { 35.000000f, 42747, 31.578960f } } },
	{ "kodim23.png", { { 100.000000f, 52922, 36.926800f },  { 75.000000f, 42386, 36.554390f },  { 35.000000f, 36771, 35.552139f } } },
	{ "kodim24.png", { { 100.000000f, 52621, 29.655001f },  { 75.000000f, 51282, 29.539488f },  { 35.000000f, 46518, 28.826057f } } },
	{ "white_1x1.png", { { 100.000000f, 164, 100.000000f },  { 75.000000f, 165, 100.000000f },  { 35.000000f, 165, 100.000000f } } },
	{ "wikipedia.png", { { 100.000000f, 86446, 26.514196f },  { 75.000000f, 82331, 26.514242f },  { 35.000000f, 83217, 26.387506f } } },
};
xuastc_ldr_test_file g_xuastc_ldr_test_files_12x12[] =
{
	{ "black_1x1.png", { { 100.000000f, 164, 100.000000f },  { 75.000000f, 165, 100.000000f },  { 35.000000f, 165, 100.000000f } } },
	{ "kodim01.png", { { 100.000000f, 37797, 28.798088f },  { 75.000000f, 37597, 28.722265f },  { 35.000000f, 36100, 28.216297f } } },
	{ "kodim02.png", { { 100.000000f, 35764, 33.883194f },  { 75.000000f, 34288, 33.613396f },  { 35.000000f, 28049, 32.870983f } } },
	{ "kodim03.png", { { 100.000000f, 35991, 35.376232f },  { 75.000000f, 30705, 35.071907f },  { 35.000000f, 26302, 34.256138f } } },
	{ "kodim04.png", { { 100.000000f, 37256, 33.985607f },  { 75.000000f, 34480, 33.726845f },  { 35.000000f, 30001, 32.846050f } } },
	{ "kodim05.png", { { 100.000000f, 38362, 27.451393f },  { 75.000000f, 38445, 27.405621f },  { 35.000000f, 37828, 27.075932f } } },
	{ "kodim06.png", { { 100.000000f, 36824, 30.414103f },  { 75.000000f, 35002, 30.223726f },  { 35.000000f, 32224, 29.430540f } } },
	{ "kodim07.png", { { 100.000000f, 36160, 33.560291f },  { 75.000000f, 33870, 33.467850f },  { 35.000000f, 29863, 32.859840f } } },
	{ "kodim08.png", { { 100.000000f, 37995, 26.794462f },  { 75.000000f, 38093, 26.760965f },  { 35.000000f, 37094, 26.328840f } } },
	{ "kodim09.png", { { 100.000000f, 36401, 34.493057f },  { 75.000000f, 29870, 34.332375f },  { 35.000000f, 26454, 33.613277f } } },
	{ "kodim10.png", { { 100.000000f, 36501, 33.844082f },  { 75.000000f, 32742, 33.686195f },  { 35.000000f, 28238, 33.049423f } } },
	{ "kodim11.png", { { 100.000000f, 37149, 30.947369f },  { 75.000000f, 36099, 30.802794f },  { 35.000000f, 32881, 30.267765f } } },
	{ "kodim12.png", { { 100.000000f, 35849, 35.354534f },  { 75.000000f, 33053, 34.918404f },  { 35.000000f, 26873, 34.069679f } } },
	{ "kodim13.png", { { 100.000000f, 37949, 25.461622f },  { 75.000000f, 37959, 25.415657f },  { 35.000000f, 38216, 25.047762f } } },
	{ "kodim14.png", { { 100.000000f, 37660, 29.483248f },  { 75.000000f, 37843, 29.437609f },  { 35.000000f, 36214, 28.999678f } } },
	{ "kodim15.png", { { 100.000000f, 36649, 33.611992f },  { 75.000000f, 31835, 33.415279f },  { 35.000000f, 27625, 32.736401f } } },
	{ "kodim16.png", { { 100.000000f, 35972, 34.145802f },  { 75.000000f, 33835, 33.685509f },  { 35.000000f, 29950, 32.574253f } } },
	{ "kodim17.png", { { 100.000000f, 37435, 33.135811f },  { 75.000000f, 34680, 32.991879f },  { 35.000000f, 30935, 32.323273f } } },
	{ "kodim18.png", { { 100.000000f, 37958, 28.844978f },  { 75.000000f, 37232, 28.796362f },  { 35.000000f, 34487, 28.304411f } } },
	{ "kodim19.png", { { 100.000000f, 36608, 31.887293f },  { 75.000000f, 34105, 31.679951f },  { 35.000000f, 29955, 30.871086f } } },
	{ "kodim20.png", { { 100.000000f, 34010, 32.896221f },  { 75.000000f, 28133, 32.748100f },  { 35.000000f, 24280, 32.296890f } } },
	{ "kodim21.png", { { 100.000000f, 36704, 30.122864f },  { 75.000000f, 34125, 30.046625f },  { 35.000000f, 29272, 29.595934f } } },
	{ "kodim22.png", { { 100.000000f, 37758, 31.691223f },  { 75.000000f, 35986, 31.529528f },  { 35.000000f, 32299, 30.867397f } } },
	{ "kodim23.png", { { 100.000000f, 37121, 35.387283f },  { 75.000000f, 31453, 35.222927f },  { 35.000000f, 27471, 34.600704f } } },
	{ "kodim24.png", { { 100.000000f, 36877, 28.162710f },  { 75.000000f, 36580, 28.106171f },  { 35.000000f, 34104, 27.782391f } } },
	{ "white_1x1.png", { { 100.000000f, 164, 100.000000f },  { 75.000000f, 165, 100.000000f },  { 35.000000f, 165, 100.000000f } } },
	{ "wikipedia.png", { { 100.000000f, 64288, 24.992327f },  { 75.000000f, 62309, 24.994276f },  { 35.000000f, 61714, 24.962925f } } },
};


static bool test_mode_xuastc_ldr(command_line_params& opts)
{
	uint32_t total_mismatches = 0;

	// Minor differences in how floating point code is optimized can result in slightly different generated files.

	// XUASTC LDR's IDCT is currently float - at low q's and high (>48) dB's tiny differences during decompression are noticeable
	const float XUASTC_PSNR_THRESHOLD = 1.0f;
	const float XUASTC_FILESIZE_THRESHOLD = .045f;

	// Dump mode (-test_xuastc_dump): run the compressor across all block sizes and print the
	// expected-value tables (copy/paste into the code below), then return without validating.
	// Uses g_xuastc_ldr_test_files_6x6 for the shared file list (filenames + DCT q's).
	if (opts.m_xuastc_test_dump)
	{
		const struct { basist::basis_tex_format m_fmt; const char* m_pName; } block_sizes[] =
		{
			{ basist::basis_tex_format::cXUASTC_LDR_4x4, "4x4" },
			{ basist::basis_tex_format::cXUASTC_LDR_5x4, "5x4" },
			{ basist::basis_tex_format::cXUASTC_LDR_6x6, "6x6" },
			{ basist::basis_tex_format::cXUASTC_LDR_8x8, "8x8" },
			{ basist::basis_tex_format::cXUASTC_LDR_10x5, "10x5" },
			{ basist::basis_tex_format::cXUASTC_LDR_10x10, "10x10" },
			{ basist::basis_tex_format::cXUASTC_LDR_12x12, "12x12" },
		};

		const uint32_t effort_level = 8;
		const bool use_srgb_metrics = false; // false = linear (1,1,1,1) weights

		for (uint32_t bs = 0; bs < std::size(block_sizes); bs++)
		{
			fmt_printf("\nxuastc_ldr_test_file g_xuastc_ldr_test_files_{}[] =\n{{\n", block_sizes[bs].m_pName);

			for (uint32_t i = 0; i < std::size(g_xuastc_ldr_test_files_6x6); i++)
			{
				const auto& test_file = g_xuastc_ldr_test_files_6x6[i];

				std::string filename(opts.m_test_file_dir);
				if (filename.size())
					filename.push_back('/');
				filename += std::string(test_file.m_pFilename);

				basisu::vector<image> source_images(1);
				if (!load_png(filename.c_str(), source_images[0]))
				{
					error_printf("Failed loading test image \"%s\"\n", filename.c_str());
					return false;
				}

				uint32_t run_sizes[XUASTC_LDR_TEST_FILE_NUM_RUNS];
				float run_psnrs[XUASTC_LDR_TEST_FILE_NUM_RUNS];

				for (uint32_t run_index = 0; run_index < XUASTC_LDR_TEST_FILE_NUM_RUNS; run_index++)
				{
					const auto& test_run = test_file.m_test_runs[run_index];

					uint32_t flags_and_quality = (opts.m_comp_params.m_multithreading ? cFlagThreaded : 0) | cFlagPrintStats | cFlagPrintStatus | (use_srgb_metrics ? cFlagSRGB : 0);
					flags_and_quality |= effort_level;

					float uastc_rdo_quality = (test_run.m_dct_q < 100.0f) ? test_run.m_dct_q : 0.0f;
					size_t data_size = 0;
					image_stats stats;

					void* pData = basis_compress(block_sizes[bs].m_fmt, source_images, flags_and_quality, uastc_rdo_quality, &data_size, &stats);
					if (!pData)
					{
						error_printf("basis_compress() failed!\n");
						return false;
					}
					basis_free_data(pData);

					run_sizes[run_index] = (uint32_t)data_size;
					run_psnrs[run_index] = stats.m_basis_rgba_avg_psnr;
				}

				fmt_printf("\t{{ \"{}\", {{", test_file.m_pFilename);
				for (uint32_t run_index = 0; run_index < XUASTC_LDR_TEST_FILE_NUM_RUNS; run_index++)
				{
					fmt_printf(" {{ {}f, {}, {}f }", test_file.m_test_runs[run_index].m_dct_q, run_sizes[run_index], run_psnrs[run_index]);
					if (run_index != (XUASTC_LDR_TEST_FILE_NUM_RUNS - 1))
						fmt_printf(", ");
				}
				fmt_printf(" } },\n");
			}

			fmt_printf("};\n");
		}

		return true;
	}

	const bool use_srgb_metrics = false; // false = linear (1,1,1,1) weights, true = sRGB perceptual (9,11,1,11)
	const uint32_t effort_level = 8;

	// Validate each block size against its stored expected-value table.
	struct block_size_test
	{
		basist::basis_tex_format m_fmt;
		const char* m_pName;
		const xuastc_ldr_test_file* m_pFiles;
		uint32_t m_num_files;
	};

	const block_size_test block_sizes[] =
	{
		{ basist::basis_tex_format::cXUASTC_LDR_4x4, "4x4", g_xuastc_ldr_test_files_4x4, (uint32_t)std::size(g_xuastc_ldr_test_files_4x4) },
		{ basist::basis_tex_format::cXUASTC_LDR_5x4, "5x4", g_xuastc_ldr_test_files_5x4, (uint32_t)std::size(g_xuastc_ldr_test_files_5x4) },
		{ basist::basis_tex_format::cXUASTC_LDR_6x6, "6x6", g_xuastc_ldr_test_files_6x6, (uint32_t)std::size(g_xuastc_ldr_test_files_6x6) },
		{ basist::basis_tex_format::cXUASTC_LDR_8x8, "8x8", g_xuastc_ldr_test_files_8x8, (uint32_t)std::size(g_xuastc_ldr_test_files_8x8) },
		{ basist::basis_tex_format::cXUASTC_LDR_10x5, "10x5", g_xuastc_ldr_test_files_10x5, (uint32_t)std::size(g_xuastc_ldr_test_files_10x5) },
		{ basist::basis_tex_format::cXUASTC_LDR_10x10, "10x10", g_xuastc_ldr_test_files_10x10, (uint32_t)std::size(g_xuastc_ldr_test_files_10x10) },
		{ basist::basis_tex_format::cXUASTC_LDR_12x12, "12x12", g_xuastc_ldr_test_files_12x12, (uint32_t)std::size(g_xuastc_ldr_test_files_12x12) },
	};

	for (uint32_t bs = 0; bs < std::size(block_sizes); bs++)
	{
		const block_size_test& bsize = block_sizes[bs];

		for (uint32_t i = 0; i < bsize.m_num_files; i++)
		{
			const xuastc_ldr_test_file& test_file = bsize.m_pFiles[i];

			std::string filename(opts.m_test_file_dir);
			if (filename.size())
				filename.push_back('/');
			filename += std::string(test_file.m_pFilename);

			basisu::vector<image> source_images(1);
			image& source_image = source_images[0];
			if (!load_png(filename.c_str(), source_image))
			{
				error_printf("Failed loading test image \"%s\"\n", filename.c_str());
				return false;
			}

			printf("Loaded file \"%s\", dimensions %ux%u has alpha: %u\n", filename.c_str(), source_image.get_width(), source_image.get_height(), source_image.has_alpha());

			image_stats stats;

			for (uint32_t run_index = 0; run_index < XUASTC_LDR_TEST_FILE_NUM_RUNS; run_index++)
			{
				const auto& test_run = test_file.m_test_runs[run_index];

				uint32_t flags_and_quality = (opts.m_comp_params.m_multithreading ? cFlagThreaded : 0) | cFlagPrintStats | cFlagPrintStatus | (use_srgb_metrics ? cFlagSRGB : 0);
				flags_and_quality |= effort_level;

				float uastc_rdo_quality = (test_run.m_dct_q < 100.0f) ? test_run.m_dct_q : 0.0f;
				size_t data_size = 0;

				fmt_printf("**** Testing XUASTC LDR {}, DCT q {}, effort {}\n", bsize.m_pName, test_run.m_dct_q, effort_level);

				void* pData = basis_compress(bsize.m_fmt, source_images, flags_and_quality, uastc_rdo_quality, &data_size, &stats);
				if (!pData)
				{
					error_printf("basis_compress() failed!\n");
					return false;
				}
				basis_free_data(pData);

				fmt_printf("XUASTC Size: {} (expected {}), RGBA PSNR: {3.3} dB (expected {3.3} dB)\n",
					(uint32_t)data_size, test_run.m_comp_size,
					stats.m_basis_rgba_avg_psnr, test_run.m_rgba_psnr);

				float file_size_ratio = fabs((data_size / (float)test_run.m_comp_size) - 1.0f);
				if (file_size_ratio > XUASTC_FILESIZE_THRESHOLD)
				{
					fmt_error_printf("Mismatch: XUASTC LDR {} expected file size {}, but got {} instead!\n", bsize.m_pName, test_run.m_comp_size, (uint32_t)data_size);
					total_mismatches++;
				}

				if (fabs(stats.m_basis_rgba_avg_psnr - test_run.m_rgba_psnr) > XUASTC_PSNR_THRESHOLD)
				{
					fmt_error_printf("Mismatch: XUASTC LDR {} expected RGBA Avg PSNR {}, but got {} instead!\n", bsize.m_pName, test_run.m_rgba_psnr, stats.m_basis_rgba_avg_psnr);
					total_mismatches++;
				}
			}
		}
	}

	printf("Total XUASTC LDR mismatches: %u\n", total_mismatches);

	bool result = true;
	if (total_mismatches)
	{
		error_printf("XUASTC LDR test FAILED\n");
		result = false;
	}
	else
	{
		printf("XUASTC LDR test succeeded\n");
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

// ---------------------------------------------------------------------------------------------------------------------
// -test_codecs / -test_codecs_gen
//
// Sweeps every supported LDR and HDR codec across a quality x effort grid, driving the basis_compressor DIRECTLY
// (filenames in, m_compute_stats + m_validate_output_data on). The compressor transcodes each result to >=2 formats and
// computes their PSNRs. -test_codecs_gen writes the golden expected values to basisu_tool_test_codecs.inl; -test_codecs
// re-runs the sweep and compares against that table (with build/OS/compiler-divergence tolerances).
// ---------------------------------------------------------------------------------------------------------------------

struct codec_test_case
{
	const char* m_pFilename;
	basist::basis_tex_format m_fmt;
	int m_quality;			// [1,100]
	int m_effort;			// [0,100]
	bool m_hdr;
	uint32_t m_size;		// expected KTX2 size, bytes
	float m_rgb_psnr;		// LDR: native RGB Avg | HDR: native ASTC-HDR log2
	float m_rgba_psnr;		// LDR: native RGBA Avg | HDR: -1 (n/a)
	float m_2nd_psnr;		// LDR: transcoded BC7 RGBA Avg | HDR: transcoded BC6H log2
};

#include "basisu_tool_test_codecs.inl"

// The codec sweep (also used to emit the golden table). Order is LDR codecs first, then HDR.
struct codec_sweep_desc { basist::basis_tex_format m_fmt; const char* m_pEnum_name; bool m_hdr; };
static const codec_sweep_desc g_codec_sweep[] =
{
	{ basist::basis_tex_format::cETC1S,						"cETC1S",						false },
	{ basist::basis_tex_format::cUASTC_LDR_4x4,				"cUASTC_LDR_4x4",				false },
	{ basist::basis_tex_format::cXUBC7,						"cXUBC7",						false },
	{ basist::basis_tex_format::cASTC_LDR_4x4,				"cASTC_LDR_4x4",				false },
	{ basist::basis_tex_format::cASTC_LDR_6x6,				"cASTC_LDR_6x6",				false },
	{ basist::basis_tex_format::cASTC_LDR_10x10,			"cASTC_LDR_10x10",				false },
	{ basist::basis_tex_format::cASTC_LDR_12x12,			"cASTC_LDR_12x12",				false },
	{ basist::basis_tex_format::cXUASTC_LDR_4x4,			"cXUASTC_LDR_4x4",				false },
	{ basist::basis_tex_format::cXUASTC_LDR_6x6,			"cXUASTC_LDR_6x6",				false },
	{ basist::basis_tex_format::cXUASTC_LDR_10x10,			"cXUASTC_LDR_10x10",			false },
	{ basist::basis_tex_format::cXUASTC_LDR_12x12,			"cXUASTC_LDR_12x12",			false },
	{ basist::basis_tex_format::cUASTC_HDR_4x4,				"cUASTC_HDR_4x4",				true },
	{ basist::basis_tex_format::cASTC_HDR_6x6,				"cASTC_HDR_6x6",				true },
	{ basist::basis_tex_format::cUASTC_HDR_6x6_INTERMEDIATE,"cUASTC_HDR_6x6_INTERMEDIATE",	true },
};

static const char* g_codec_ldr_test_files[] = { "kodim03.png", "kodim23.png", "kodim18.png", "alpha0.png", "wikipedia.png", "black_1x1.png" };
static const char* g_codec_hdr_test_files[] = { "Desk.exr", "atrium.exr", "yucca.exr", "kodim18.png" }; // kodim18.png is auto-upconverted LDR->linear->100 nit HDR
static const int g_codec_test_qualities[] = { 10, 25, 50, 75, 100 };
static const int g_codec_test_efforts[] = { 0, 3, 6 };

// Case-insensitive token-boundary match: filter matches at the start of pName or immediately after a '_'.
// So "ASTC" matches "ASTC_LDR_4x4" but NOT "XUASTC_LDR_4x4"/"UASTC_HDR_4x4"; "XUASTC" matches all XUASTC sizes; "HDR" matches all *_HDR_*.
static bool codec_name_matches_filter(const char* pName, const std::string& filter)
{
	if (filter.empty())
		return true;
	std::string n(pName), f(filter);
	for (char& c : n) c = (char)toupper((uint8_t)c);
	for (char& c : f) c = (char)toupper((uint8_t)c);
	size_t pos = 0;
	while ((pos = n.find(f, pos)) != std::string::npos)
	{
		if ((pos == 0) || (n[pos - 1] == '_'))
			return true;
		pos++;
	}
	return false;
}

// Short (no leading 'c') enum name for a format, for filtering/reporting.
static const char* codec_short_name(basist::basis_tex_format fmt)
{
	for (const auto& d : g_codec_sweep)
		if (d.m_fmt == fmt)
			return d.m_pEnum_name + 1; // skip leading 'c'
	return "?";
}

// Pull the headline (native) + 2nd-format PSNRs out of an image_stats, per LDR/HDR.
static void codec_extract_psnrs(const basisu::image_stats& s, bool hdr, float& rgb, float& rgba, float& second)
{
	if (hdr)
	{
		rgb = s.m_basis_rgb_avg_astc_hdr_log2_psnr;
		rgba = -1.0f;
		second = s.m_basis_rgb_avg_bc6h_log2_psnr;
	}
	else
	{
		rgb = s.m_basis_rgb_avg_psnr;
		rgba = s.m_basis_rgba_avg_psnr;
		second = s.m_bc7_rgba_avg_psnr;
	}
}

// Run one case via the compressor directly. Returns false on compress failure.
static bool run_codec_test_case(basisu::job_pool& jp, const std::string& dir, const char* pFilename,
	basist::basis_tex_format fmt, [[maybe_unused]] bool hdr, int quality, int effort,
	uint32_t& out_size, basisu::image_stats& out_stats, double& out_time_ms)
{
	out_time_ms = 0.0;

	std::string path(dir);
	if (path.size())
		path.push_back('/');
	path += pFilename;

	basis_compressor_params params;
	params.m_pJob_pool = &jp;
	params.m_multithreading = true;
	params.m_read_source_images = true;					// let the compressor load the file (and upconvert LDR->HDR when m_hdr is set)
	params.m_source_filenames.push_back(path);
	params.m_create_ktx2_file = true;					// measure the .KTX2 file size
	params.m_write_output_basis_or_ktx2_files = false;	// keep it in memory only
	params.m_compute_stats = true;						// fill image_stats (native + transcoded BC7/BC6H PSNRs)
	params.m_print_stats = false;
	params.m_validate_output_data = true;				// validate the encoded output
	params.m_perceptual = true;							// sRGB-style metrics (held constant for gen + run)
	params.m_status_output = false;
	params.m_debug = false;

	// Sets m_hdr/m_uastc and maps quality[1,100]/effort[0,100] to the codec's low-level knobs.
	if (!params.set_format_mode_and_quality_effort(fmt, quality, effort, true))
		return false;

	interval_timer tm;
	tm.start();

	basis_compressor comp;
	if (!comp.init(params))
		return false;

	if (comp.process() != basis_compressor::cECSuccess)
		return false;

	out_time_ms = tm.get_elapsed_ms();

	out_size = (uint32_t)comp.get_output_ktx2_file().size();

	const basisu::vector<basisu::image_stats>& sv = comp.get_stats();
	if (!sv.size())
		return false;
	out_stats = sv[0];

	return true;
}

static uint32_t codec_test_num_threads()
{
	uint32_t n = std::thread::hardware_concurrency();
	return n ? n : 1;
}

// Returns the path prefix to the repo root so -test_codecs works whether run from the repo
// root ("") or the bin/ subdir ("../"). Probes for test_files/kodim03.png.
static std::string codec_test_root_prefix()
{
	FILE* f = fopen_safe("test_files/kodim03.png", "rb");
	if (f) { fclose(f); return ""; }
	f = fopen_safe("../test_files/kodim03.png", "rb");
	if (f) { fclose(f); return "../"; }
	return ""; // fallback: repo-root-relative
}

static bool test_codecs_generate(command_line_params& opts)
{
	const std::string dir = opts.m_test_dir_explicit ? opts.m_test_file_dir : (codec_test_root_prefix() + "test_files");
	const std::string out_path = opts.m_test_codecs_gen_filename.size() ? opts.m_test_codecs_gen_filename : (codec_test_root_prefix() + "basisu_tool_test_codecs.inl");

	fmt_printf("Generating codec test table: dir \"{}\", output \"{}\"", dir, out_path);
	if (opts.m_codec_filter.size())
		fmt_printf(", codec filter \"{}\"", opts.m_codec_filter);
	fmt_printf("\n");

	FILE* pFile = fopen_safe(out_path.c_str(), "wb");
	if (!pFile)
	{
		error_printf("Failed creating output file \"%s\"\n", out_path.c_str());
		return false;
	}

	fprintf(pFile, "// basisu_tool_test_codecs.inl\n");
	fprintf(pFile, "//\n// AUTO-GENERATED by: basisu -test_codecs_gen [outfile.inl] [-test_dir <dir>] [codec_filter]\n");
	fprintf(pFile, "// Do NOT edit by hand. Regenerate whenever a codec changes its output.\n");
	fprintf(pFile, "//\n// Row: file, basis_tex_format, quality[1,100], effort[0,100], is_hdr, ktx2_size,\n");
	fprintf(pFile, "//      rgb_psnr (LDR native RGB | HDR ASTC-HDR log2), rgba_psnr (LDR native RGBA | HDR -1),\n");
	fprintf(pFile, "//      2nd_psnr (LDR BC7 RGBA | HDR BC6H log2).\n//\n");
	fprintf(pFile, "static const codec_test_case g_codec_test_cases[] =\n{\n");

	basisu::job_pool jp(codec_test_num_threads());

	uint32_t total = 0, fails = 0;

	for (const auto& codec : g_codec_sweep)
	{
		if (!codec_name_matches_filter(codec.m_pEnum_name + 1, opts.m_codec_filter))
			continue;
				
		const char* const* pFiles = codec.m_hdr ? g_codec_hdr_test_files : g_codec_ldr_test_files;
		const uint32_t num_files = codec.m_hdr ? (uint32_t)std::size(g_codec_hdr_test_files) : (uint32_t)std::size(g_codec_ldr_test_files);

		fprintf(pFile, "\t// ---- %s ----\n", codec.m_pEnum_name + 1);

		for (uint32_t fi = 0; fi < num_files; fi++)
		{
			for (int q : g_codec_test_qualities)
			{
				for (int e : g_codec_test_efforts)
				{
					uint32_t size = 0;
					basisu::image_stats stats;
					double elapsed_ms = 0.0;

					if (!run_codec_test_case(jp, dir, pFiles[fi], codec.m_fmt, codec.m_hdr, q, e, size, stats, elapsed_ms))
					{
						fmt_printf("COMPRESS FAILED: {} {} q{} e{}\n", codec.m_pEnum_name + 1, pFiles[fi], q, e);
						fprintf(pFile, "\t// COMPRESS FAILED: { \"%s\", basist::basis_tex_format::%s, %d, %d, %s },\n",
							pFiles[fi], codec.m_pEnum_name, q, e, codec.m_hdr ? "true" : "false");
						fails++;
						total++;
						continue;
					}

					float rgb, rgba, second;
					codec_extract_psnrs(stats, codec.m_hdr, rgb, rgba, second);

					// Progress: print every ~10 runs unless the user passed -quiet/-no_status_output (m_status_output==false).
					if (opts.m_comp_params.m_status_output && ((total % 10) == 0))
						fmt_printf("[{}] {} q{} e{} {}  size {} rgb {}\n", total, codec.m_pEnum_name + 1, q, e, pFiles[fi], size, rgb);

					fprintf(pFile, "\t{ \"%s\", basist::basis_tex_format::%s, %d, %d, %s, %u, %.4ff, %.4ff, %.4ff },\n",
						pFiles[fi], codec.m_pEnum_name, q, e, codec.m_hdr ? "true" : "false", size, rgb, rgba, second);

					total++;
				}
			}
		}
	}

	fprintf(pFile, "};\n");
	fclose(pFile);

	fmt_printf("\nWrote {} cases ({} compress failures) to \"{}\"\n", total, fails, out_path);
	return fails == 0;
}

static bool test_codecs_run(command_line_params& opts)
{
	const std::string dir = opts.m_test_dir_explicit ? opts.m_test_file_dir : (codec_test_root_prefix() + "test_files");

#ifdef USE_TIGHTER_TEST_TOLERANCES
	const float PSNR_THRESHOLD = .125f;
	const float XUASTC_PSNR_THRESHOLD = .5f;
#else
	const float PSNR_THRESHOLD = 5.0f;
	const float XUASTC_PSNR_THRESHOLD = 5.0f;	// XUASTC LDR has more cross-build variance (matches test_mode_xuastc_ldr)
#endif
	const float FILESIZE_THRESHOLD = .045f;		// 4.5% relative, matches the other -test modes
	// Tiny outputs (e.g. the XUASTC LDR runs on small alpha test images) jitter by a large RELATIVE amount across
	// builds (MSVC vs clang) for a small absolute byte delta, tripping the 4.5% check spuriously. For golden sizes
	// below this many bytes, relax to a much looser relative threshold so those don't fail the build.
	const uint32_t SMALL_FILESIZE_BYTES = 2048;
	const float SMALL_FILESIZE_THRESHOLD = .50f;	// 50% relative, for golden sizes < SMALL_FILESIZE_BYTES
	const float LDR_SENTINEL_PSNR = 99.0f;		// lossless/degenerate inputs store ~100; don't require an exact match
	const float HDR_SENTINEL_PSNR = 200.0f;

	if (opts.m_codec_filter.size())
		fmt_printf("Codec filter: \"{}\"\n", opts.m_codec_filter);

	basisu::job_pool jp(codec_test_num_threads());

	const uint32_t kNumFmts = (uint32_t)basist::basis_tex_format::cTotalFormats;
	basisu::vector<float> highest_psnr_delta(kNumFmts);
	basisu::vector<float> highest_size_ratio(kNumFmts);
	basisu::vector<uint8_t> fmt_used(kNumFmts);
	basisu::vector<double> codec_total_ms(kNumFmts);	// per-codec compression time accumulators
	basisu::vector<double> codec_max_ms(kNumFmts);
	basisu::vector<uint32_t> codec_count(kNumFmts);
	for (uint32_t i = 0; i < kNumFmts; i++) { highest_psnr_delta[i] = 0.0f; highest_size_ratio[i] = 0.0f; fmt_used[i] = 0; codec_total_ms[i] = 0.0; codec_max_ms[i] = 0.0; codec_count[i] = 0; }

	uint32_t total = 0, tested = 0, mismatches = 0, run_fails = 0, total_skipped = 0;

	for (const codec_test_case& tc : g_codec_test_cases)
	{
		total++;

		if (!codec_name_matches_filter(codec_short_name(tc.m_fmt), opts.m_codec_filter))
			continue;

		if constexpr (sizeof(void*) == sizeof(uint32_t))
		{
			// TODO: disable testing some codecs in 32-bit builds to avoid running out of memory (remove once 32-bit memory issue is addressed)
			if (basist::basis_tex_format_is_xuastc_ldr(tc.m_fmt) || basis_tex_format_is_astc_ldr(tc.m_fmt))
			{
				uint32_t block_width = 0, block_height = 0;
				basist::get_basis_tex_format_block_size(tc.m_fmt, block_width, block_height);
				if ((block_width < 6) || (block_height < 6))
				{
					fmt_printf("WARNING: Skipping XUASTC/ASTC format with block size {}x{} to avoid running out of memory in 32-bit builds\n", block_width, block_height);
					total_skipped++;
					continue;
				}
			}
		}

		tested++;

		uint32_t size = 0;
		basisu::image_stats stats;
		double elapsed_ms = 0.0;

		if (!run_codec_test_case(jp, dir, tc.m_pFilename, tc.m_fmt, tc.m_hdr, tc.m_quality, tc.m_effort, size, stats, elapsed_ms))
		{
			error_printf("COMPRESS FAILED: %s %s q%d e%d\n", codec_short_name(tc.m_fmt), tc.m_pFilename, tc.m_quality, tc.m_effort);
			run_fails++;
			continue;
		}

		float rgb, rgba, second;
		codec_extract_psnrs(stats, tc.m_hdr, rgb, rgba, second);

		// Progress: print every ~10 runs unless the user passed -quiet/-no_status_output (m_status_output==false).
		if (opts.m_comp_params.m_status_output && ((tested == 1) || ((tested % 10) == 0)))
			printf("  [%u] %-26s %-14s q%d e%d  size=%u rgb=%.2f%s  time=%.1fms\n",
				tested, codec_short_name(tc.m_fmt), tc.m_pFilename, tc.m_quality, tc.m_effort, size, rgb, tc.m_hdr ? " (log2)" : "", elapsed_ms);

		const float psnr_thresh = basist::basis_tex_format_is_xuastc_ldr(tc.m_fmt) ? XUASTC_PSNR_THRESHOLD : PSNR_THRESHOLD;
		const float sentinel = tc.m_hdr ? HDR_SENTINEL_PSNR : LDR_SENTINEL_PSNR;

		const uint32_t fi = (uint32_t)tc.m_fmt;
		fmt_used[fi] = 1;
		codec_total_ms[fi] += elapsed_ms;
		if (elapsed_ms > codec_max_ms[fi]) codec_max_ms[fi] = elapsed_ms;
		codec_count[fi]++;

		// File size: relative ratio.
		if (tc.m_size)
		{
			float ratio = fabsf((float)size / (float)tc.m_size - 1.0f);
			if (ratio > highest_size_ratio[fi]) highest_size_ratio[fi] = ratio;
			// Small golden sizes swing widely in relative terms across builds for a tiny absolute delta, so relax
			// the threshold for them (see SMALL_FILESIZE_BYTES above).
			const float size_thresh = (tc.m_size < SMALL_FILESIZE_BYTES) ? SMALL_FILESIZE_THRESHOLD : FILESIZE_THRESHOLD;
			if (ratio > size_thresh)
			{
				error_printf("SIZE mismatch: %s %s q%d e%d: expected %u, got %u (%.2f%%)\n",
					codec_short_name(tc.m_fmt), tc.m_pFilename, tc.m_quality, tc.m_effort, tc.m_size, size, ratio * 100.0f);
				mismatches++;
			}
		}

		// PSNR comparisons. Sentinel (lossless) values are only required to be "high", not exact.
		struct { const char* m_pName; float m_expected; float m_measured; bool m_active; } checks[3] =
		{
			{ "RGB",  tc.m_rgb_psnr,  rgb,    true },
			{ "RGBA", tc.m_rgba_psnr, rgba,   (!tc.m_hdr) && (tc.m_rgba_psnr >= 0.0f) },
			{ "2nd",  tc.m_2nd_psnr,  second, tc.m_2nd_psnr > 1.0f },	// skip when the 2nd format wasn't computed
		};

		for (const auto& c : checks)
		{
			if (!c.m_active)
				continue;

			if (c.m_expected >= sentinel)
			{
				// Lossless/degenerate: just require the measured value is also very high.
				if (c.m_measured < (sentinel - 20.0f))
				{
					error_printf("%s PSNR mismatch (sentinel): %s %s q%d e%d: expected >=%.1f, got %.3f\n",
						c.m_pName, codec_short_name(tc.m_fmt), tc.m_pFilename, tc.m_quality, tc.m_effort, sentinel - 20.0f, c.m_measured);
					mismatches++;
				}
				continue;
			}

			float delta = fabsf(c.m_measured - c.m_expected);
			if (delta > highest_psnr_delta[fi]) highest_psnr_delta[fi] = delta;
			if ((c.m_measured < 55.0f) && (delta > psnr_thresh)) // ignore if PSNR is already very high
			{
				error_printf("%s PSNR mismatch: %s %s q%d e%d: expected %.3f, got %.3f (delta %.3f > %.3f)\n",
					c.m_pName, codec_short_name(tc.m_fmt), tc.m_pFilename, tc.m_quality, tc.m_effort, c.m_expected, c.m_measured, delta, psnr_thresh);
				mismatches++;
			}
		}
	}

	// Per-codec highest deltas, grouped LDR then HDR (g_codec_sweep order).
	printf("\n---- Highest deltas per codec (PSNR dB / file-size ratio) ----\n");
	for (uint32_t group = 0; group < 2; group++)
	{
		printf("%s:\n", group == 0 ? "LDR" : "HDR");
		for (const auto& d : g_codec_sweep)
		{
			if ((d.m_hdr ? 1u : 0u) != group)
				continue;
			const uint32_t fi = (uint32_t)d.m_fmt;
			if (!fmt_used[fi])
				continue;
			printf("  %-28s  max PSNR delta %.4f dB,  max size ratio %.2f%%\n",
				d.m_pEnum_name + 1, highest_psnr_delta[fi], highest_size_ratio[fi] * 100.0f);
		}
	}

	// Per-codec compression timing (gross-perf-regression signal), grouped LDR then HDR.
	printf("\n---- Per-codec compression time (total / avg / max) ----\n");
	double grand_total_ms = 0.0;
	for (uint32_t group = 0; group < 2; group++)
	{
		printf("%s:\n", group == 0 ? "LDR" : "HDR");
		for (const auto& d : g_codec_sweep)
		{
			if ((d.m_hdr ? 1u : 0u) != group)
				continue;
			const uint32_t fi = (uint32_t)d.m_fmt;
			if (!codec_count[fi])
				continue;
			grand_total_ms += codec_total_ms[fi];
			printf("  %-28s  total %.1f ms,  avg %.1f ms,  max %.1f ms  (%u runs)\n",
				d.m_pEnum_name + 1, codec_total_ms[fi], codec_total_ms[fi] / (double)codec_count[fi], codec_max_ms[fi], codec_count[fi]);
		}
	}
	printf("Grand total compression time: %.1f ms (%.2f s)\n", grand_total_ms, grand_total_ms / 1000.0);

	if (total_skipped != 0)
		fmt_printf("WARNING: {} encodes had to be skipped to avoid running out of memory in 32-bit builds\n", total_skipped);

	printf("\nCodec test: %u cases in table, %u tested, %u mismatches, %u compress failures\n", total, tested, mismatches, run_fails);

	if (mismatches || run_fails)
	{
		error_printf("Codec test FAILED\n");
		return false;
	}
	printf("Codec test succeeded\n");
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

static bool peek_astc_file(const char* pFilename)
{
	fmt_printf("\nExamining .astc file: \"{}\"\n", pFilename);

	vector2D<astc_helpers::astc_block> blocks;
	uint32_t block_width, block_height, image_width, image_height;
	if (!read_astc_file(pFilename, blocks, block_width, block_height, image_width, image_height))
	{
		fmt_error_printf("Failed reading .astc file!\n");
		return false;
	}

	return display_astc_statistics(blocks, block_width, block_height, image_width, image_height, true);
}

static bool xuastc_ldr_decoder_fuzz_test()
{
	basisu::rand rnd;
	rnd.seed(1);

	const uint32_t N = 16;

	interval_timer itm;
	double total_time_a = 0, total_time_b = 0;

	for (uint32_t blk_size_index = 0; blk_size_index < astc_helpers::NUM_ASTC_BLOCK_SIZES; blk_size_index++)
	{
		const uint32_t bw = astc_helpers::g_astc_block_sizes[blk_size_index][0];
		const uint32_t bh = astc_helpers::g_astc_block_sizes[blk_size_index][1];

		fmt_printf("Testing block size {}x{}\n", bw, bh);

		const auto& trial_modes = basist::astc_ldr_t::g_encoder_trial_modes[blk_size_index];

		if (!trial_modes.size())
		{
			assert(0);
			return false;
		}

		for (uint32_t j = 0; j < trial_modes.size(); j++)
		{
			const auto& tm = trial_modes[j];

			astc_helpers::log_astc_block log_blk;
			log_blk.clear();

			const bool test_solid = rnd.irand(0, 63) == 0;

			log_blk.m_grid_width = (uint8_t)tm.m_grid_width;
			log_blk.m_grid_height = (uint8_t)tm.m_grid_height;
			
			log_blk.m_weight_ise_range = (uint8_t)tm.m_weight_ise_range;
			log_blk.m_endpoint_ise_range = (uint8_t)tm.m_endpoint_ise_range;
			
			log_blk.m_dual_plane = tm.m_ccs_index != -1;
			if (tm.m_ccs_index != -1)
				log_blk.m_color_component_selector = (uint8_t)tm.m_ccs_index;

			log_blk.m_num_partitions = (uint8_t)tm.m_num_parts;
			for (uint32_t s = 0; s < tm.m_num_parts; s++)
				log_blk.m_color_endpoint_modes[s] = (uint8_t)tm.m_cem;

			for (uint32_t k = 0; k < N; k++)
			{
				if (log_blk.m_num_partitions > 1)
					log_blk.m_partition_id = (uint16_t)rnd.irand(0, 1023);

				const uint32_t num_cem_endpoint_vals = astc_helpers::get_num_cem_values(tm.m_cem);
				const uint32_t total_cem_endpoint_vals = num_cem_endpoint_vals * log_blk.m_num_partitions;

				for (uint32_t i = 0; i < total_cem_endpoint_vals; i++)
					log_blk.m_endpoints[i] = (uint8_t)rnd.irand(0, astc_helpers::get_ise_levels(log_blk.m_endpoint_ise_range) - 1);

				const uint32_t num_weight_vals = (log_blk.m_dual_plane ? 2 : 1) * log_blk.m_grid_width * log_blk.m_grid_height;
				for (uint32_t i = 0; i < num_weight_vals; i++)
					log_blk.m_weights[i] = (uint8_t)rnd.irand(0, astc_helpers::get_ise_levels(log_blk.m_weight_ise_range) - 1);

				if (test_solid)
				{
					log_blk.clear();
					log_blk.m_solid_color_flag_ldr = true;
					
					uint32_t r = rnd.byte();
					uint32_t g = rnd.byte();
					uint32_t b = rnd.byte();
					uint32_t a = rnd.byte();

					log_blk.m_solid_color[0] = (uint16_t)((r << 8) | r);
					log_blk.m_solid_color[1] = (uint16_t)((g << 8) | g);
					log_blk.m_solid_color[2] = (uint16_t)((b << 8) | b);
					log_blk.m_solid_color[3] = (uint16_t)((a << 8) | a);
				}

				const bool srgb = rnd.bit();

				basist::color32 blk_a[astc_helpers::MAX_BLOCK_PIXELS];
				clear_obj(blk_a);

				itm.start();

				bool status_a = astc_helpers::decode_block(log_blk, blk_a, bw, bh, srgb ? astc_helpers::cDecodeModeSRGB8 : astc_helpers::cDecodeModeLDR8);
				if (!status_a)
				{
					error_printf("astc_helpers::decode_block() failed\n");
					return false;
				}

				total_time_a += itm.get_elapsed_secs();

				basist::color32 blk_b[astc_helpers::MAX_BLOCK_PIXELS];
				clear_obj(blk_b);

				itm.start();

				bool status_b = astc_helpers::decode_block_xuastc_ldr(log_blk, blk_b, bw, bh, srgb ? astc_helpers::cDecodeModeSRGB8 : astc_helpers::cDecodeModeLDR8);
				if (!status_b)
				{
					error_printf("astc_helpers::decode_block() failed\n");
					return false;
				}

				total_time_b += itm.get_elapsed_secs();
				
				for (uint32_t i = 0; i < bw * bh; i++)
				{
					if ((blk_a[i].r != blk_b[i].r) || (blk_a[i].g != blk_b[i].g) || (blk_a[i].b != blk_b[i].b) || (blk_a[i].a != blk_b[i].a))
					{
						error_printf("decode block mismatch\n");
						return false;
					}
				}

			} // k

		} // j

	} // blk_size_index

	printf("ASTC block decoder vs. XUASTC LDR block decoding fuzz test succeeded\n");
	fmt_printf("Total time A: {}, B: {}\n", total_time_a, total_time_b);

	return true;
}

[[maybe_unused]] static inline uint8_t get_601_y(int r, int g, int b)
{
	return (uint8_t)std::round(16.0f + 65.481f * (float)r / 255.0f + 128.553f * (float)g / 255.0f + 24.966f * (float)b / 255.0f);
}

// Calculates a proper average PSNR by converting each PSNR value to MSE, averaging the MSE values, and then converting back to PSNR. 
// This is necessary because PSNR is a logarithmic metric, so you can't just average the PSNR values directly.
float calc_average_psnr(const float* psnr_values, uint32_t count, float lossless_db_thresh)
{
	if (!count)
		return 0.0f;

	float mse_sum = 0.0;
	for (uint32_t i = 0; i < count; i++)
	{
		if (psnr_values[i] >= lossless_db_thresh)
			continue;

		mse_sum += powf(10.0f, -psnr_values[i] / 10.0f);
	}
	
	float mean_mse = mse_sum / (float)count;
	return (mean_mse > 0.0f) ? clamp<float>(-10.0f * log10f(mean_mse), 0.0f, 100.0f) : 100.0f;
}

static float print_psnr_stats(const float_vec &orig_psnrs, float lossless_db_thresh)
{
	const float avg_psnr = calc_average_psnr(orig_psnrs.get_ptr(), orig_psnrs.size_u32(), lossless_db_thresh);
	fmt_printf("  Average (from all MSE's): {3.3} dB\n", avg_psnr);

	uint32_t total_lossless = 0;
	float_vec psnrs;
	for (uint32_t i = 0; i < orig_psnrs.size(); i++)
	{
		if (orig_psnrs[i] >= lossless_db_thresh)
			total_lossless++;
		else
			psnrs.push_back(orig_psnrs[i]);
	}

	fmt_printf("  Total > {3.2} dB (lossless): {}, lossy: {}\n", lossless_db_thresh, total_lossless, psnrs.size_u32());
	fmt_printf("  Lossy-only statistics:\n");

	stats<float> psnrs_stats;
	psnrs_stats.calc(psnrs.size_u32(), psnrs.get_ptr(), 1, true);

	fmt_printf("  Average (of only lossy PSNR's): {3.3} dB\n", psnrs_stats.m_avg);
	fmt_printf("  Std dev: {3.3} dB, Skewness: {3.3} dB\n", psnrs_stats.m_std_dev, psnrs_stats.m_skewness);
	fmt_printf("  Min: {3.3} dB\n", psnrs_stats.m_min);
	fmt_printf("  Median: {3.3} dB at file index: {}\n", psnrs_stats.m_median, psnrs_stats.m_median_index + 1);
	fmt_printf("  Max: {3.3} dB\n", psnrs_stats.m_max);
	fmt_printf("  Avg 5% low: {3.3} dB\n", psnrs_stats.m_five_percent_lo);
	fmt_printf("  Avg 5% high: {3.3} dB\n", psnrs_stats.m_five_percent_hi);

	return avg_psnr;
}

struct benchmark_results
{
	benchmark_results()
	{
		clear();
	}

	void clear()
	{
		clear_obj(*this);
	}

	basist::basis_tex_format m_tex_fmt;
	uint32_t m_effort, m_quality, m_flags;

	uint32_t m_total_images;
	double m_total_time;
	uint64_t m_total_input_texels;
	uint64_t m_total_compressed_size;

	// LDR averages - all calculated from MSE's then converted to PSNR.
	float m_avg_rgba_psnr; 
	float m_avg_luma_709_psnr;
	float m_avg_y_hvs_psnr;
	float m_avg_y_hvsm_psnr;

	// HDR averages: native ASTC HDR + transcoded BC6H, each in linear half-float and log2 space
	// All calculated from MSE's then converted to PSNR.
	float m_avg_astc_hdr_psnr;
	float m_avg_astc_hdr_log2_psnr;
	float m_avg_bc6h_psnr;
	float m_avg_bc6h_log2_psnr;
};

// Parses a -benchmark_single format token (e.g. "XUASTC_LDR_4x4", "XUBC7", "ETC1S", "ASTC_LDR_6x6", "UASTC_LDR_4x4")
// into a basis_tex_format. Matches case-insensitively against basis_get_tex_format_name() with spaces normalized to
// underscores, so every codec the encoder supports (XUASTC LDR 4x4-12x12, XUBC7, ETC1S, ASTC LDR, etc.) is accepted.
// Returns false if no format matches.
static bool parse_benchmark_tex_format(const std::string& s, basist::basis_tex_format& out_fmt)
{
	auto normalize = [](std::string v) -> std::string
	{
		for (char& c : v)
			c = (c == ' ') ? '_' : (char)toupper((uint8_t)c);
		return v;
	};

	const std::string want(normalize(s));

	for (uint32_t i = 0; i < (uint32_t)basist::basis_tex_format::cTotalFormats; i++)
	{
		const basist::basis_tex_format fmt = (basist::basis_tex_format)i;
		if (normalize(basist::basis_get_tex_format_name(fmt)) == want)
		{
			out_fmt = fmt;
			return true;
		}
	}

	return false;
}

[[maybe_unused]] static bool codec_benchmark(
	const std::string &base_filename, //("d:/dev/test_images/photo_png/kodim");
	const uint32_t first_test_file_index,// = 1;
	const uint32_t last_test_file_index,// = 166,
	basist::basis_tex_format tex_fmt, // = basist::basis_tex_format::cXUASTC_LDR_6x6;
	uint32_t effort_level, uint32_t quality_level, uint32_t flags,
	bool srgb_flag, // true = sRGB/perceptual metrics, false = linear
	benchmark_results &results)
{
	results.m_tex_fmt = tex_fmt;
	results.m_effort = effort_level;
	results.m_quality = quality_level;
	results.m_flags = flags;

	fmt_printf("Base filename: {}, first file index: {}, last file index: {}\n", base_filename, first_test_file_index, last_test_file_index);

	fmt_printf("Testing tex fmt: {}, Flags: 0x{X}, Effort: {}, Quality: {}\n", (uint32_t)tex_fmt, flags, effort_level, quality_level);

	const bool is_hdr = basist::basis_tex_format_is_hdr(tex_fmt);

	// sRGB vs. linear metrics only applies to LDR; HDR metrics are always linear half-float / log2, so don't set the
	// sRGB flag or print the metric mode for HDR.
	const bool srgb_metrics = srgb_flag && !is_hdr;
	if (!is_hdr)
		fmt_printf("Using {} metrics\n", srgb_metrics ? "sRGB (perceptual)" : "linear");

	interval_timer tm;
	tm.start();

	// LDR metrics
	float_vec rgba_psnrs, luma_709_psnrs, y_hvs_psnr, y_hvsm_psnr;
	// HDR metrics: native ASTC HDR and transcoded BC6H RGB PSNRs, in linear half-float and log2 space
	float_vec astc_hdr_psnrs, astc_hdr_log2_psnrs, bc6h_psnrs, bc6h_log2_psnrs;

	uint64_t total_compressed_size = 0;
	uint64_t total_input_texels = 0;

	uint32_t flags_and_quality = cFlagThreaded | (srgb_metrics ? cFlagSRGB : 0) | flags;

	const uint32_t total_test_images = (last_test_file_index - first_test_file_index) + 1;

	for (uint32_t file_index = first_test_file_index; file_index <= last_test_file_index; file_index++)
	{
		// HDR formats benchmark against .exr inputs (loaded as float images); LDR against .png. The LDR sets (e.g.
		// kodimNN.png) use 2-digit zero-padded indices; the HDR set (hdr_N.exr) is not padded.
		std::string filename(base_filename);
		const char* pExt = is_hdr ? ".exr" : ".png";
		if (!is_hdr && (file_index < 10))
			filename += fmt_string("{02}{}", file_index, pExt);
		else
			filename += fmt_string("{}{}", file_index, pExt);

		image orig_img;
		imagef orig_imgf;
		uint32_t width = 0, height = 0;
		bool has_alpha = false;

		if (is_hdr)
		{
			if (!load_image_hdr(filename.c_str(), orig_imgf))
			{
				fmt_error_printf("Failed to load HDR image file {}\n", filename);
				return false;
			}
			width = orig_imgf.get_width();
			height = orig_imgf.get_height();
		}
		else
		{
			if (!load_png(filename, orig_img))
			{
				fmt_error_printf("Failed to load image file {}\n", filename);
				return false;
			}
			width = orig_img.get_width();
			height = orig_img.get_height();
			has_alpha = orig_img.has_alpha();
		}

		fmt_printf("---- Loading image {}, {}x{}, has_alpha: {}\n", filename, width, height, has_alpha);

		size_t data_size = 0;
		basisu::image_stats stats;

		// HDR uses the imagef (float) basis_compress2 overload; LDR uses the 8-bit image overload.
		void* pData;
		if (is_hdr)
		{
			basisu::vector<imagef> source_images_hdr;
			source_images_hdr.push_back(orig_imgf);
			pData = basis_compress2(tex_fmt, source_images_hdr, flags_and_quality, quality_level, effort_level, &data_size, (flags & cFlagPrintStats) ? &stats : nullptr);
		}
		else
		{
			basisu::vector<image> source_images;
			source_images.push_back(orig_img);
			pData = basis_compress2(tex_fmt, source_images, flags_and_quality, quality_level, effort_level, &data_size, (flags & cFlagPrintStats) ? &stats : nullptr);
		}

		if (!pData)
		{
			error_printf("basis_compress() failed!\n");
			return false;
		}

		basis_free_data(pData);

		const double bpp = ((double)data_size * 8.0f) / (double)(width * height);

		if (is_hdr)
		{
			// HDR: native ASTC HDR + transcoded BC6H RGB PSNRs, each in linear half-float and log2 space (see image_stats).
			const float astc_hdr_psnr = stats.m_basis_rgb_avg_psnr;
			const float astc_hdr_log2_psnr = stats.m_basis_rgb_avg_astc_hdr_log2_psnr;
			const float bc6h_psnr = stats.m_basis_rgb_avg_bc6h_psnr;
			const float bc6h_log2_psnr = stats.m_basis_rgb_avg_bc6h_log2_psnr;

			fmt_printf("Size: {}, bpp: {}, ASTC HDR RGB PSNR: {3.3} dB (log2: {3.3}), BC6H RGB PSNR: {3.3} dB (log2: {3.3})\n",
				(uint64_t)data_size, bpp, astc_hdr_psnr, astc_hdr_log2_psnr, bc6h_psnr, bc6h_log2_psnr);

			astc_hdr_psnrs.push_back(astc_hdr_psnr);
			astc_hdr_log2_psnrs.push_back(astc_hdr_log2_psnr);
			bc6h_psnrs.push_back(bc6h_psnr);
			bc6h_log2_psnrs.push_back(bc6h_log2_psnr);
		}
		else
		{
			// XUBC7 is a native BC7 codec, so its native RGBA/Luma quality lives in the m_bc7_* stats; the m_basis_* stats
			// for XUBC7 reflect the ASTC LDR 4x4 transcode, not the native output. All other codecs use the m_basis_* stats.
			const bool is_xubc7 = (tex_fmt == basist::basis_tex_format::cXUBC7);
			const float rgba_psnr = is_xubc7 ? stats.m_bc7_rgba_avg_psnr : stats.m_basis_rgba_avg_psnr;
			const float luma_709_psnr = is_xubc7 ? stats.m_bc7_luma_709_psnr : stats.m_basis_luma_709_psnr;
			const psnr_hvs_metrics& hvs = is_xubc7 ? stats.m_hvs_metrics_bc7 : stats.m_hvs_metrics;

			fmt_printf("Size: {}, bpp: {}, RGBA PSNR: {3.3} dB, Luma 709 PSNR: {3.3} dB, Y PSNR-HVS: {3.3} dB, Y PSNR-HVS-M: {3.3} dB\n",
				(uint64_t)data_size,
				bpp,
				rgba_psnr,
				luma_709_psnr,
				hvs.m_y_601_float.m_psnr_hvs,
				hvs.m_y_601_float.m_psnr_hvsm);

			rgba_psnrs.push_back(rgba_psnr);
			luma_709_psnrs.push_back(luma_709_psnr);
			y_hvs_psnr.push_back((float)hvs.m_y_601_float.m_psnr_hvs);
			y_hvsm_psnr.push_back((float)hvs.m_y_601_float.m_psnr_hvsm);
		}

		total_compressed_size += data_size;
		total_input_texels += (uint64_t)width * (uint64_t)height;

	} // file_index

	const double total_time = tm.get_elapsed_secs();
	
	fmt_printf("\n-------------------------\n");
	
	fmt_printf("tex fmt: {}, Flags: 0x{X}, Effort: {}, Quality: {}\n", (uint32_t)tex_fmt, flags_and_quality, effort_level, quality_level);

	fmt_printf("Total time: {3.3} secs, average time per image: {3.3} secs\n", total_time, total_time / (double)total_test_images);

	fmt_printf("Total input texels: {} megapixels, total compressed size: {} mebibytes, average bpp: {3.3}\n", (double)total_input_texels / (1e+6), (double)total_compressed_size / (1024.0f * 1024.0f), ((double)total_compressed_size * 8.0f) / (double)total_input_texels);

	results.m_total_images = total_test_images;
	results.m_total_input_texels = total_input_texels;
	results.m_total_compressed_size = total_compressed_size;
	results.m_total_time = total_time;

	if (is_hdr)
	{
		fmt_printf("\nASTC HDR RGB PSNR:\n");
		results.m_avg_astc_hdr_psnr = print_psnr_stats(astc_hdr_psnrs, 100.0f);

		fmt_printf("\nASTC HDR RGB log2 PSNR:\n");
		results.m_avg_astc_hdr_log2_psnr = print_psnr_stats(astc_hdr_log2_psnrs, 100.0f);

		fmt_printf("\nBC6H RGB PSNR:\n");
		results.m_avg_bc6h_psnr = print_psnr_stats(bc6h_psnrs, 100.0f);

		fmt_printf("\nBC6H RGB log2 PSNR:\n");
		results.m_avg_bc6h_log2_psnr = print_psnr_stats(bc6h_log2_psnrs, 100.0f);
	}
	else
	{
		fmt_printf("\nRGBA PSNR:\n");
		results.m_avg_rgba_psnr = print_psnr_stats(rgba_psnrs, 100.0f);

		fmt_printf("\nLuma 709 PSNR:\n");
		results.m_avg_luma_709_psnr = print_psnr_stats(luma_709_psnrs, 100.0f);

		fmt_printf("\nY PSNR-HVS:\n");
		results.m_avg_y_hvs_psnr = print_psnr_stats(y_hvs_psnr, PSNR_HVS_LOSSLESS_DB);

		fmt_printf("\nY PSNR-HVSM:\n");
		results.m_avg_y_hvsm_psnr = print_psnr_stats(y_hvsm_psnr, PSNR_HVS_LOSSLESS_DB);
	}

	return true;
}

// -benchmark_single <FORMAT> <EFFORT> <QUALITY>: single codec_benchmark() run over a numbered image set.
// LDR loads <prefix>NN.png (zero-padded); HDR loads <prefix>N.exr (not padded). Defaults: LDR "../test_files/kodim" 1..24,
// HDR "../test_files/hdr_" 1..6 (used only when -benchmark_path / -benchmark_range aren't given). Override via
// -benchmark_path and -benchmark_range MIN MAX. LDR metrics are sRGB/perceptual by default (-benchmark_linear selects
// linear); HDR metrics are always linear half-float / log2.
// -benchmark_xubc7_encoder [0,7] selects the XUBC7 BC7 base encoder (0=bc7f, 1-7=bc7e_scalar level 0-6; XUBC7 only).
// -benchmark_xuastc_profile [0,2] selects the XUASTC LDR entropy syntax/profile (0=full arith, 1=hybrid, 2=full zstd; XUASTC only).
// FORMAT/EFFORT/QUALITY come from the command line. Not in the help text yet.
// Example: basisu -benchmark_single XUBC7 9 100 [-benchmark_path <prefix>] [-benchmark_range 1 24] [-benchmark_linear] [-benchmark_xubc7_encoder 4] [-benchmark_xuastc_profile 2]
// For HDR codecs, if the user didn't explicitly override the image path/range, switch from the LDR kodim default to the
// HDR default set ("../test_files/hdr_N.exr", indices 1..6). LDR keeps its existing defaults.
static void apply_hdr_benchmark_image_defaults(command_line_params& opts)
{
	if (!basist::basis_tex_format_is_hdr(opts.m_benchmark_fmt))
		return;
	if (!opts.m_benchmark_path_set)
		opts.m_benchmark_path = "../test_files/hdr_";
	if (!opts.m_benchmark_range_set)
	{
		opts.m_benchmark_first = 1;
		opts.m_benchmark_last = 6;
	}
}

// Packs the benchmark flag set (KTX2 + UASTC supercompression + print stats/status + the XUBC7 base encoder and XUASTC
// profile selections) and runs one codec_benchmark() over the configured image set. Shared by -benchmark_single and -benchmark_sweep.
static bool run_one_benchmark(const command_line_params& opts,
	basist::basis_tex_format fmt, uint32_t effort, uint32_t quality, benchmark_results& results)
{
	// XUBC7-only: pack the BC7 base encoder selection into the high flag bits (0=bc7f, 1-7=bc7e_scalar level 0-6); ignored for other formats.
	const uint32_t xubc7_encoder_flags = (opts.m_benchmark_xubc7_encoder & cFlagXUBC7BaseEncoderMask) << cFlagXUBC7BaseEncoderShift;
	// XUASTC-only: pack the LDR entropy syntax/profile (0=full arith, 1=hybrid, 2=full zstd); ignored for other formats.
	const uint32_t xuastc_profile_flags = (opts.m_benchmark_xuastc_profile & cFlagXUASTCLDRSyntaxMask) << cFlagXUASTCLDRSyntaxShift;
	return codec_benchmark(opts.m_benchmark_path, opts.m_benchmark_first, opts.m_benchmark_last,
		fmt, effort, quality,
		cFlagKTX2 | cFlagKTX2UASTCSuperCompression | cFlagPrintStats | cFlagPrintStatus | xubc7_encoder_flags | xuastc_profile_flags,
		!opts.m_benchmark_linear, // sRGB metrics by default; -benchmark_linear selects linear
		results);
}

static bool benchmark_single_mode(command_line_params& opts)
{
	apply_hdr_benchmark_image_defaults(opts);
	benchmark_results bres;
	return run_one_benchmark(opts, opts.m_benchmark_fmt, opts.m_benchmark_effort, opts.m_benchmark_quality, bres);
}

// Maps a square-block-size sweep index [0,5] (0=4x4,1=5x5,2=6x6,3=8x8,4=10x10,5=12x12) to the basis_tex_format in
// base_fmt's family. Only XUASTC LDR and ASTC LDR have selectable block sizes; returns false for any other family.
static bool benchmark_block_index_to_fmt(basist::basis_tex_format base_fmt, uint32_t square_index, basist::basis_tex_format& out_fmt)
{
	typedef basist::basis_tex_format tf;

	const bool is_xuastc = basist::basis_tex_format_is_xuastc_ldr(base_fmt);
	const bool is_astc = basist::basis_tex_format_is_astc_ldr(base_fmt);
	if (!is_xuastc && !is_astc)
		return false;

	switch (square_index)
	{
	case 0: out_fmt = is_xuastc ? tf::cXUASTC_LDR_4x4   : tf::cASTC_LDR_4x4;   return true;
	case 1: out_fmt = is_xuastc ? tf::cXUASTC_LDR_5x5   : tf::cASTC_LDR_5x5;   return true;
	case 2: out_fmt = is_xuastc ? tf::cXUASTC_LDR_6x6   : tf::cASTC_LDR_6x6;   return true;
	case 3: out_fmt = is_xuastc ? tf::cXUASTC_LDR_8x8   : tf::cASTC_LDR_8x8;   return true;
	case 4: out_fmt = is_xuastc ? tf::cXUASTC_LDR_10x10 : tf::cASTC_LDR_10x10; return true;
	case 5: out_fmt = is_xuastc ? tf::cXUASTC_LDR_12x12 : tf::cASTC_LDR_12x12; return true;
	default: return false;
	}
}

// -benchmark_sweep <FORMAT>: sweeps any combination of block size, effort, and quality for one codec, writing a CSV
// (one row per combination) for spreadsheet analysis. Axis ranges (inclusive low/high): -benchmark_sweep_block [0,5]
// (square sizes 4x4..12x12; XUASTC_LDR/ASTC_LDR only - error otherwise; if omitted, FORMAT's own block size is used and
// no block sweep happens), -benchmark_sweep_effort [0,10] (default 3 3), -benchmark_sweep_quality [0,10] (quality =
// clamp(q*10,1,100); default 10 10 => quality 100). Loop order is block (major), then effort, then quality (minor).
// Metrics: LDR = RGBA + Y PSNR-HVS-M; HDR = non-log2 ASTC HDR RGB PSNR. Output CSV via -benchmark_sweep_csv (default
// "benchmark_sweep.csv"). Other -benchmark_* options apply per run.
// Not in the help text yet. Example: basisu -benchmark_sweep XUASTC_LDR_6x6 -benchmark_sweep_block 0 5 -benchmark_sweep_quality 0 10
static bool benchmark_sweep_mode(command_line_params& opts)
{
	apply_hdr_benchmark_image_defaults(opts);

	const basist::basis_tex_format base_fmt = opts.m_benchmark_fmt;
	const bool is_hdr = basist::basis_tex_format_is_hdr(base_fmt);
	const bool block_family = basist::basis_tex_format_is_xuastc_ldr(base_fmt) || basist::basis_tex_format_is_astc_ldr(base_fmt);

	// Build the block-size axis. If -benchmark_sweep_block was given, sweep square block-size indices [lo,hi] within the
	// codec's family. If it was NOT given, default to exactly the codec/block size specified (e.g. XUASTC_LDR_6x6 stays 6x6,
	// ETC1S/XUBC7/HDR stay as-is) - no block sweep.
	basisu::vector<basist::basis_tex_format> block_fmts;
	if (opts.m_benchmark_sweep_block_set)
	{
		if (!block_family)
		{
			fmt_error_printf("-benchmark_sweep_block requires an XUASTC_LDR or ASTC_LDR codec family\n");
			return false;
		}
		uint32_t blo = opts.m_benchmark_sweep_block_lo, bhi = opts.m_benchmark_sweep_block_hi;
		if (blo > bhi) std::swap(blo, bhi);
		for (uint32_t b = blo; b <= bhi; b++)
		{
			basist::basis_tex_format f;
			if (!benchmark_block_index_to_fmt(base_fmt, b, f))
			{
				fmt_error_printf("-benchmark_sweep: invalid block size index {}\n", b);
				return false;
			}
			block_fmts.push_back(f);
		}
	}
	else
	{
		block_fmts.push_back(base_fmt); // default: the exact codec/block size specified
	}

	uint32_t elo = opts.m_benchmark_sweep_effort_lo, ehi = opts.m_benchmark_sweep_effort_hi;
	if (elo > ehi) std::swap(elo, ehi);
	uint32_t qlo = opts.m_benchmark_sweep_quality_lo, qhi = opts.m_benchmark_sweep_quality_hi;
	if (qlo > qhi) std::swap(qlo, qhi);

	const uint32_t num_blocks = (uint32_t)block_fmts.size();
	const uint32_t num_efforts = (ehi - elo) + 1;
	const uint32_t num_qualities = (qhi - qlo) + 1;

	// Announce the actual sweep up front. With -benchmark_sweep_block the block axis sweeps square sizes within the codec's
	// family (XUASTC/ASTC only; everything else is fixed 4x4); without it, the codec's own block size is used.
	fmt_printf("Benchmark sweep: codec {}, {} block size(s), effort {}..{}, quality(x10) {}..{}, {} runs total\n",
		basist::basis_get_tex_format_name(base_fmt), num_blocks, elo, ehi, qlo, qhi, num_blocks * num_efforts * num_qualities);
	for (uint32_t bi = 0; bi < num_blocks; bi++)
	{
		uint32_t bw = 0, bh = 0;
		basist::get_basis_tex_format_block_size(block_fmts[bi], bw, bh);
		fmt_printf("  block[{}]: {} ({}x{})\n", bi, basist::basis_get_tex_format_name(block_fmts[bi]), bw, bh);
	}

	// Open the CSV up front so a bad path fails before the (potentially long) sweep runs.
	FILE* pFile = fopen(opts.m_benchmark_sweep_csv.c_str(), "w");
	if (!pFile)
	{
		fmt_error_printf("Failed to open output CSV file \"{}\" for writing!\n", opts.m_benchmark_sweep_csv);
		return false;
	}

	if (is_hdr)
	{
		// HDR metrics are always linear half-float / log2 (sRGB vs linear doesn't apply), so no metrics field.
		fmt_fprintf(pFile, "// codec: {}, images: {}N.exr indices {}..{}\n",
			basist::basis_get_tex_format_name(base_fmt), opts.m_benchmark_path, opts.m_benchmark_first, opts.m_benchmark_last);
		fmt_fprintf(pFile, "// fmt, block, effort, quality, bitrate, astc_hdr_rgb_psnr\n");
	}
	else
	{
		fmt_fprintf(pFile, "// codec: {}, metrics: {}, images: {}NN.png indices {}..{}\n",
			basist::basis_get_tex_format_name(base_fmt), opts.m_benchmark_linear ? "linear" : "sRGB (perceptual)",
			opts.m_benchmark_path, opts.m_benchmark_first, opts.m_benchmark_last);
		fmt_fprintf(pFile, "// fmt, block, effort, quality, bitrate, rgba_psnr, y_hvsm_psnr\n");
	}

	// Run all combinations, storing results in a 3D [block][effort][quality] array, then write the CSV (one row per
	// combination; loop order block major, then effort, then quality).
	basisu::vector<basisu::vector<basisu::vector<benchmark_results>>> results;
	results.resize(num_blocks);

	for (uint32_t bi = 0; bi < num_blocks; bi++)
	{
		results[bi].resize(num_efforts);
		for (uint32_t ei = 0; ei < num_efforts; ei++)
		{
			results[bi][ei].resize(num_qualities);
			for (uint32_t qi = 0; qi < num_qualities; qi++)
			{
				const uint32_t effort = elo + ei;
				const uint32_t quality = (uint32_t)clamp<int>((int)(qlo + qi) * 10, 1, 100);

				if (!run_one_benchmark(opts, block_fmts[bi], effort, quality, results[bi][ei][qi]))
				{
					fclose(pFile);
					return false;
				}
			}
		}
	}

	for (uint32_t bi = 0; bi < num_blocks; bi++)
	{
		for (uint32_t ei = 0; ei < num_efforts; ei++)
		{
			for (uint32_t qi = 0; qi < num_qualities; qi++)
			{
				const benchmark_results& r = results[bi][ei][qi];

				uint32_t bw = 0, bh = 0;
				basist::get_basis_tex_format_block_size(r.m_tex_fmt, bw, bh);
				const double bitrate = (double)r.m_total_compressed_size * 8.0 / (double)r.m_total_input_texels;

				if (is_hdr)
					fmt_fprintf(pFile, "{}, {}x{}, {}, {}, {3.4}, {3.4}\n",
						basist::basis_get_tex_format_name(r.m_tex_fmt), bw, bh, r.m_effort, r.m_quality, bitrate, r.m_avg_astc_hdr_psnr);
				else
					fmt_fprintf(pFile, "{}, {}x{}, {}, {}, {3.4}, {3.4}, {3.4}\n",
						basist::basis_get_tex_format_name(r.m_tex_fmt), bw, bh, r.m_effort, r.m_quality, bitrate, r.m_avg_rgba_psnr, r.m_avg_y_hvsm_psnr);
			}
		}
	}

	fclose(pFile);

	fmt_printf("Wrote benchmark sweep CSV \"{}\"\n", opts.m_benchmark_sweep_csv);
	return true;
}

// -test_dds: standalone test harness for the basist::dds_transcoder DX9/DX10 DDS reader+transcoder.
// Usage: basisu -test_dds <file.dds> [output_prefix]
// Generate inputs with the existing exporter, e.g.:
//   basisu -ktx2 -uastc <img>.png ; basisu -file <img>.ktx2 -export_dds   (or BC7/array/cubemap variants)
static bool test_dds(int argc, const char* argv[])
{
	if (argc < 3)
	{
		fmt_error_printf("Usage: basisu -test_dds <file.dds> [output_prefix]\n");
		return false;
	}

	const char* pFilename = argv[2];
	const std::string out_prefix = (argc >= 4) ? std::string(argv[3]) : std::string("dds_test");

	uint8_vec file_data;
	if (!read_file_to_vec(pFilename, file_data))
	{
		fmt_error_printf("Failed reading file \"{}\"\n", pFilename);
		return false;
	}
	fmt_printf("Read {} bytes from \"{}\"\n", file_data.size_u32(), pFilename);

	basist::dds_transcoder dds;
	if (!dds.init(file_data.data(), file_data.size_u32()))
	{
		fmt_error_printf("dds_transcoder::init() failed (unsupported or malformed DDS)\n");
		return false;
	}
	if (!dds.start_transcoding())
	{
		fmt_error_printf("dds_transcoder::start_transcoding() failed\n");
		return false;
	}

	const uint32_t levels = dds.get_levels();
	const uint32_t eff_layers = dds.get_layers() ? dds.get_layers() : 1;
	const uint32_t faces = dds.get_faces();
	const basist::transcoder_texture_format contained = dds.get_format();

	fmt_printf("DDS: {}x{}, levels: {}, layers: {} ({}), faces: {} ({})\n",
		dds.get_width(), dds.get_height(), levels, dds.get_layers(), dds.get_layers() ? "array" : "non-array",
		faces, dds.get_is_cubemap() ? "cubemap" : "2D");
	fmt_printf("Contained format: {} (enum {}), has_alpha: {}, sRGB: {}\n",
		basist::basis_get_format_name(contained), (int)contained, (uint32_t)dds.get_has_alpha(), dds.is_srgb() ? 1 : 0);

	// Exercise is_transcode_format_supported() (PVRTC1 depends on pow2 dims; cTFBC6H/PVRTC2 are unsupported).
	{
		const bool pow2 = is_pow2(dds.get_width()) && is_pow2(dds.get_height());
		const basist::transcoder_texture_format probe[] = {
			basist::transcoder_texture_format::cTFBC7_RGBA, basist::transcoder_texture_format::cTFASTC_4x4_RGBA,
			basist::transcoder_texture_format::cTFRGBA32, basist::transcoder_texture_format::cTFPVRTC1_4_RGB,
			basist::transcoder_texture_format::cTFBC6H, basist::transcoder_texture_format::cTFPVRTC2_4_RGB
		};
		fmt_printf("is_transcode_format_supported (pow2 dims: {}):\n", pow2 ? 1 : 0);
		for (basist::transcoder_texture_format f : probe)
			fmt_printf("  {}: {}\n", basist::basis_get_format_name(f), dds.is_transcode_format_supported(f) ? "yes" : "no");
	}

	// Dump per-image info.
	for (uint32_t layer = 0; layer < eff_layers; layer++)
		for (uint32_t face = 0; face < faces; face++)
			for (uint32_t level = 0; level < levels; level++)
			{
				basist::ktx2_image_level_info li;
				if (!dds.get_image_level_info(li, level, layer, face))
				{
					fmt_error_printf("get_image_level_info failed at A{} F{} M{}\n", layer, face, level);
					return false;
				}
				fmt_printf("  A{} F{} M{}: {}x{} ({}x{} blocks, {} total)\n",
					layer, face, level, li.m_orig_width, li.m_orig_height, li.m_num_blocks_x, li.m_num_blocks_y, li.m_total_blocks);
			}

	// Decode every image to RGBA32 and save as PNG (visual verification).
	uint32_t png_count = 0;
	for (uint32_t layer = 0; layer < eff_layers; layer++)
	{
		for (uint32_t face = 0; face < faces; face++)
		{
			for (uint32_t level = 0; level < levels; level++)
			{
				basist::ktx2_image_level_info li;
				dds.get_image_level_info(li, level, layer, face);

				image img(li.m_orig_width, li.m_orig_height);
				if (!dds.transcode_image_level(level, layer, face, img.get_ptr(), img.get_total_pixels(),
					basist::transcoder_texture_format::cTFRGBA32, 0, img.get_width(), img.get_height()))
				{
					fmt_error_printf("transcode to RGBA32 failed at A{} F{} M{}\n", layer, face, level);
					return false;
				}

				char fn[512];
				snprintf(fn, sizeof(fn), "%s_A%u_F%u_M%u.png", out_prefix.c_str(), layer, face, level);
				if (!save_png(fn, img))
				{
					fmt_error_printf("save_png failed for \"{}\"\n", fn);
					return false;
				}
				png_count++;
			}
		}
	}
	fmt_printf("Wrote {} decoded RGBA32 PNG(s) (prefix \"{}\")\n", png_count, out_prefix.c_str());

	// Reference RGBA32 decode of the base image, for round-trip PSNR of re-encoded targets.
	basist::ktx2_image_level_info base_li;
	dds.get_image_level_info(base_li, 0, 0, 0);
	image ref_img(base_li.m_orig_width, base_li.m_orig_height);
	dds.transcode_image_level(0, 0, 0, ref_img.get_ptr(), ref_img.get_total_pixels(),
		basist::transcoder_texture_format::cTFRGBA32, 0, ref_img.get_width(), ref_img.get_height());

	// Exercise every supported re-encode target on the base image.
	struct target_row { basist::transcoder_texture_format fmt; basisu::texture_format unpack_fmt; bool can_unpack; };
	const target_row targets[] =
	{
		{ basist::transcoder_texture_format::cTFBC1_RGB,       basisu::texture_format::cBC1, true },
		{ basist::transcoder_texture_format::cTFBC3_RGBA,      basisu::texture_format::cBC3, true },
		{ basist::transcoder_texture_format::cTFBC4_R,         basisu::texture_format::cBC4, true },
		{ basist::transcoder_texture_format::cTFBC5_RG,        basisu::texture_format::cBC5, true },
		{ basist::transcoder_texture_format::cTFBC7_RGBA,      basisu::texture_format::cBC7, true },
		{ basist::transcoder_texture_format::cTFETC1_RGB,      basisu::texture_format::cInvalidTextureFormat, false },
		{ basist::transcoder_texture_format::cTFETC2_RGBA,     basisu::texture_format::cInvalidTextureFormat, false },
		{ basist::transcoder_texture_format::cTFASTC_4x4_RGBA, basisu::texture_format::cInvalidTextureFormat, false },
		{ basist::transcoder_texture_format::cTFPVRTC1_4_RGB,  basisu::texture_format::cInvalidTextureFormat, false },
		{ basist::transcoder_texture_format::cTFRGB565,        basisu::texture_format::cInvalidTextureFormat, false },
		{ basist::transcoder_texture_format::cTFRGBA4444,      basisu::texture_format::cInvalidTextureFormat, false },
	};

	// For the re-encode tests use a 4x4-grid sized image (covers compressed targets cleanly).
	const uint32_t grid_blocks_x = (base_li.m_orig_width + 3) / 4;
	const uint32_t grid_blocks_y = (base_li.m_orig_height + 3) / 4;

	for (const target_row& t : targets)
	{
		const bool uncompressed = (t.fmt == basist::transcoder_texture_format::cTFRGBA32) ||
			(t.fmt == basist::transcoder_texture_format::cTFRGB565) || (t.fmt == basist::transcoder_texture_format::cTFRGBA4444);
		const bool is_pvrtc1 = (t.fmt == basist::transcoder_texture_format::cTFPVRTC1_4_RGB) || (t.fmt == basist::transcoder_texture_format::cTFPVRTC1_4_RGBA);

		if (is_pvrtc1 && (!is_pow2(base_li.m_orig_width) || !is_pow2(base_li.m_orig_height)))
		{
			fmt_printf("  {}: skipped (PVRTC1 needs power-of-2 dims)\n", basist::basis_get_format_name(t.fmt));
			continue;
		}

		const uint32_t bytes_per = basist::basis_get_bytes_per_block_or_pixel(t.fmt);
		uint32_t buf_units, byte_size;
		if (uncompressed)
		{
			buf_units = base_li.m_orig_width * base_li.m_orig_height;
			byte_size = buf_units * bytes_per;
		}
		else
		{
			buf_units = grid_blocks_x * grid_blocks_y;
			byte_size = buf_units * bytes_per;
		}

		uint8_vec out_buf(byte_size);
		const uint32_t row_pitch = uncompressed ? base_li.m_orig_width : 0;
		const uint32_t rows = uncompressed ? base_li.m_orig_height : 0;

		if (!dds.transcode_image_level(0, 0, 0, out_buf.data(), buf_units, t.fmt, 0, row_pitch, rows))
		{
			fmt_error_printf("  {}: transcode FAILED\n", basist::basis_get_format_name(t.fmt));
			return false;
		}

		// Round-trip PSNR for the BC formats we can unpack directly.
		if (t.can_unpack)
		{
			image rt(grid_blocks_x * 4, grid_blocks_y * 4);
			for (uint32_t by = 0; by < grid_blocks_y; by++)
				for (uint32_t bx = 0; bx < grid_blocks_x; bx++)
				{
					color_rgba pixels[16];
					unpack_block(t.unpack_fmt, out_buf.data() + (by * grid_blocks_x + bx) * bytes_per, pixels, false);
					for (uint32_t y = 0; y < 4; y++)
						for (uint32_t x = 0; x < 4; x++)
							rt.set_clipped(bx * 4 + x, by * 4 + y, pixels[y * 4 + x]);
				}

			image_metrics m;
			m.calc(ref_img, rt, 0, 3);
			const double rgb_psnr = m.m_psnr;
			m.calc(ref_img, rt, 0, 4);
			fmt_printf("  {}: OK {} bytes, RGB PSNR {}, RGBA PSNR {}\n",
				basist::basis_get_format_name(t.fmt), byte_size, rgb_psnr, m.m_psnr);
		}
		else
		{
			fmt_printf("  {}: OK {} bytes\n", basist::basis_get_format_name(t.fmt), byte_size);
		}
	}

	fmt_printf("-test_dds: success\n");
	return true;
}

// ---- -test_dds_overwrite: transcoder output-buffer SANITY (overrun) test ----
// This is a SEPARATE test from -test_dds, and a different kind of test: it does NOT check "did we get the right
// pixels?" -- it checks "did dds_transcoder::transcode_image_level() ever write past the buffer I declared, and does
// it correctly REJECT bogus arguments?". For every .dds under the given root (default c:\dev\dds_files), for every
// (level,layer,face) and every supported output format (compressed AND uncompressed), we transcode into a
// basisu::vector<uint8_t> that is deliberately LARGER than required and pre-filled with the 0xDEADBEEF byte pattern,
// then verify the bytes at/after the declared buffer size are still pristine (nothing was overwritten). We exercise
// the optional output_row_pitch / output_rows overrides -- expand AND shrink for uncompressed targets (e.g. a 4x4 DDS
// unpacked into a clipped 3x3 region), expand-only for compressed -- plus a battery of must-reject argument errors
// (undersize buffer, shrunken compressed pitch, PVRTC1 non-tight pitch, out-of-range level/layer/face, null output).
// A rejected call must also leave the buffer entirely pristine. Intended to be run against a DEBUG build.
// Usage: basisu -test_dds_overwrite [root_dir_or_file]
static const uint8_t s_deadbeef[4] = { 0xEF, 0xBE, 0xAD, 0xDE }; // little-endian 0xDEADBEEF

static void fill_deadbeef(uint8_vec& buf)
{
	for (size_t i = 0; i < buf.size(); i++)
		buf[i] = s_deadbeef[i & 3];
}

struct dds_file_entry { std::string m_path; uint64_t m_size; };

// Returns the index of the first byte in [start,end) that is no longer the 0xDEADBEEF pattern, or SIZE_MAX if clean.
static size_t check_deadbeef(const uint8_vec& buf, size_t start, size_t end)
{
	for (size_t i = start; i < end; i++)
		if (buf[i] != s_deadbeef[i & 3])
			return i;
	return SIZE_MAX;
}

static bool test_dds_overwrite(int argc, const char* argv[])
{
	const std::string root = (argc >= 3) ? std::string(argv[2]) : std::string("c:\\dev\\dds_files");

	// Gather every .dds file under root (recursively) via C++17 std::filesystem; allow a single file too.
	std::vector<dds_file_entry> dds_files;
	{
		std::error_code ec;
		const std::filesystem::path root_path(root);
		const std::filesystem::file_status st = std::filesystem::status(root_path, ec);
		if (ec || !std::filesystem::exists(st))
		{
			fmt_error_printf("Root path \"{}\" does not exist\n", root.c_str());
			return false;
		}
		if (std::filesystem::is_directory(st))
		{
			for (std::filesystem::recursive_directory_iterator it(root_path, std::filesystem::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
			{
				if (ec) { ec.clear(); continue; }
				std::error_code fec;
				if (!it->is_regular_file(fec))
					continue;
				std::string ext = it->path().extension().string();
				for (char& c : ext) c = (char)tolower((unsigned char)c);
				if (ext == ".dds")
				{
					const uintmax_t sz = it->file_size(fec);
					dds_files.push_back({ it->path().string(), fec ? 0ull : (uint64_t)sz });
				}
			}
		}
		else
		{
			dds_files.push_back({ root, 0 }); // explicit single file: size 0 => never skipped by the cap below
		}
	}

	if (dds_files.empty())
	{
		fmt_error_printf("No .dds files found under \"{}\"\n", root.c_str());
		return false;
	}
	std::sort(dds_files.begin(), dds_files.end(), [](const dds_file_entry& a, const dds_file_entry& b) { return a.m_path < b.m_path; });
	fmt_printf("-test_dds_overwrite: found {} .dds file(s) under \"{}\"\n", (uint64_t)dds_files.size(), root.c_str());

	uint64_t total_calls = 0, total_overwrite_failures = 0, total_expectation_mismatches = 0;
	uint32_t files_skipped = 0;

	const size_t GUARD_BYTES = 1024; // sentinel margin past the declared buffer to catch overruns

	enum call_kind { KIND_PROBE, KIND_POSITIVE, KIND_MUST_REJECT };

	// A Debug re-encode of an enormous file (e.g. a multi-hundred-MB BC7 texture array) to every output format x
	// pitch/rows combo is impractical; skip files past this cap. Bounds behavior doesn't depend on image size.
	const uint64_t MAX_FILE_BYTES = 64ull * 1024 * 1024;

	for (const dds_file_entry& fe : dds_files)
	{
		const std::string& fn = fe.m_path;
		if (fe.m_size > MAX_FILE_BYTES)
		{
			fmt_printf("  SKIP (too large for Debug test, {} MB): \"{}\"\n", (uint64_t)(fe.m_size / (1024 * 1024)), fn.c_str());
			files_skipped++;
			continue;
		}

		uint8_vec file_data;
		if (!read_file_to_vec(fn.c_str(), file_data))
		{
			fmt_printf("  SKIP (read failed): \"{}\"\n", fn.c_str());
			files_skipped++;
			continue;
		}

		basist::dds_transcoder dds;
		if (!dds.init(file_data.data(), file_data.size_u32()) || !dds.start_transcoding())
		{
			// The corpus may legitimately contain DDS variants this transcoder doesn't accept -- not a test failure.
			fmt_printf("  SKIP (init failed): \"{}\"\n", fn.c_str());
			files_skipped++;
			continue;
		}

		const uint32_t levels = dds.get_levels();
		const uint32_t eff_layers = dds.get_layers() ? dds.get_layers() : 1;
		const uint32_t faces = dds.get_faces();

		uint64_t file_calls = 0, file_fail = 0;

		// One transcode into a guarded, sentinel-filled buffer. Verifies: (1) nothing written at/after the declared
		// size; (2) a rejected call wrote nothing anywhere; (3) the success/failure expectation. Returns the result.
		auto run_one = [&](uint32_t lvl, uint32_t lyr, uint32_t fc,
			basist::transcoder_texture_format f, uint32_t bytes_per,
			uint32_t buf_units, uint32_t P, uint32_t R,
			call_kind kind, bool path_active, const char* desc) -> bool
		{
			total_calls++; file_calls++;
			const size_t declared_bytes = (size_t)buf_units * bytes_per;
			uint8_vec buf(declared_bytes + GUARD_BYTES);
			fill_deadbeef(buf);

			const bool ok = dds.transcode_image_level(lvl, lyr, fc, buf.data(), buf_units, f, 0, P, R);

			// (1) CORE CHECK: the transcoder may never touch a byte at/after the declared buffer size.
			const size_t bad_guard = check_deadbeef(buf, declared_bytes, buf.size());
			if (bad_guard != SIZE_MAX)
			{
				fmt_error_printf("  *** OVERWRITE PAST BUFFER: {} | {} [{}] L{} A{} F{} P={} R={} units={} -> wrote +{} bytes past declared {} ***\n",
					fn.c_str(), basist::basis_get_format_name(f), desc, lvl, lyr, fc, P, R, buf_units,
					(uint64_t)(bad_guard - declared_bytes), (uint64_t)declared_bytes);
				total_overwrite_failures++; file_fail++;
			}

			// (2) A rejected transcode must not have written ANYTHING into the buffer.
			if (!ok)
			{
				const size_t bad_any = check_deadbeef(buf, 0, buf.size());
				if (bad_any != SIZE_MAX)
				{
					fmt_error_printf("  *** REJECTED CALL WROTE TO BUFFER: {} | {} [{}] L{} A{} F{} P={} R={} at byte {} ***\n",
						fn.c_str(), basist::basis_get_format_name(f), desc, lvl, lyr, fc, P, R, (uint64_t)bad_any);
					total_overwrite_failures++; file_fail++;
				}
			}

			// (3) Expectation. A must-reject call that succeeds, or a should-succeed call (when the path is active)
			// that fails, is a mismatch. The PROBE kind only establishes whether the path is active (no expectation).
			if ((kind == KIND_MUST_REJECT) && ok)
			{
				fmt_error_printf("  *** EXPECTED REJECTION BUT SUCCEEDED: {} | {} [{}] L{} A{} F{} P={} R={} units={} ***\n",
					fn.c_str(), basist::basis_get_format_name(f), desc, lvl, lyr, fc, P, R, buf_units);
				total_expectation_mismatches++; file_fail++;
			}
			else if ((kind == KIND_POSITIVE) && path_active && !ok)
			{
				fmt_error_printf("  *** EXPECTED SUCCESS BUT FAILED: {} | {} [{}] L{} A{} F{} P={} R={} units={} ***\n",
					fn.c_str(), basist::basis_get_format_name(f), desc, lvl, lyr, fc, P, R, buf_units);
				total_expectation_mismatches++; file_fail++;
			}
			return ok;
		};

		for (uint32_t lyr = 0; lyr < eff_layers; lyr++)
		for (uint32_t fc = 0; fc < faces; fc++)
		for (uint32_t lvl = 0; lvl < levels; lvl++)
		{
			basist::ktx2_image_level_info li;
			if (!dds.get_image_level_info(li, lvl, lyr, fc))
			{
				fmt_error_printf("  get_image_level_info failed L{} A{} F{} in \"{}\"\n", lvl, lyr, fc, fn.c_str());
				file_fail++;
				continue;
			}
			const uint32_t W = li.m_orig_width, H = li.m_orig_height;
			// DDS blocks are always 4x4; the output 4x4-block grid is ceil(dim/4) regardless of source kind.
			const uint32_t nbx = (W + 3) / 4, nby = (H + 3) / 4;

			for (int fi = 0; fi < (int)basist::transcoder_texture_format::cTFTotalTextureFormats; fi++)
			{
				const basist::transcoder_texture_format f = (basist::transcoder_texture_format)fi;
				if (!dds.is_transcode_format_supported(f))
					continue;

				const bool uncompressed = basist::basis_transcoder_format_is_uncompressed(f);
				const bool is_pvrtc = (f == basist::transcoder_texture_format::cTFPVRTC1_4_RGB) ||
				                      (f == basist::transcoder_texture_format::cTFPVRTC1_4_RGBA);
				const uint32_t bytes_per = basist::basis_get_bytes_per_block_or_pixel(f);

				// Declared output units for a given (pitch,rows) per the transcode contract:
				//   uncompressed: (pitch?:width) * (rows?:height) pixels;  compressed: (pitch?:num_blocks_x) * num_blocks_y blocks.
				auto declared_units = [&](uint32_t P, uint32_t R) -> uint32_t
				{
					if (uncompressed)
						return (P ? P : W) * (R ? R : H);
					return (P ? P : nbx) * nby;
				};

				// PROBE the default pitch/rows: is this format's transcode path active for this source+build?
				// (A decode->repack target returns false in a build without BASISD_SUPPORT_XUASTC; don't flag that.)
				const uint32_t du_default = declared_units(0, 0);
				const bool path_active = run_one(lvl, lyr, fc, f, bytes_per, du_default, 0, 0, KIND_PROBE, false, "default");

				// ---- Positive combos (should succeed whenever the path is active) ----
				struct PR { uint32_t P, R; const char* desc; };
				std::vector<PR> pos;
				if (uncompressed)
				{
					pos.push_back({ W + 5, 0,     "expand-pitch" });
					pos.push_back({ W + 5, H + 3, "expand-pitch+rows" });
					pos.push_back({ 0,     H + 3, "expand-rows" });
					if (W >= 2) { pos.push_back({ W - 1, 0, "shrink-pitch-1" }); pos.push_back({ (W + 1) / 2, 0, "shrink-pitch-half" }); }
					if (H >= 2) { pos.push_back({ 0, H - 1, "shrink-rows-1" }); pos.push_back({ 0, (H + 1) / 2, "shrink-rows-half" }); }
					if ((W >= 3) && (H >= 3)) pos.push_back({ 3, 3, "clip-to-3x3" });
				}
				else if (!is_pvrtc)
				{
					pos.push_back({ nbx + 2, 0, "expand-pitch" });
				}
				for (const PR& c : pos)
					run_one(lvl, lyr, fc, f, bytes_per, declared_units(c.P, c.R), c.P, c.R, KIND_POSITIVE, path_active, c.desc);

				// ---- Must-reject combos ----
				if (du_default >= 1) // buffer one unit too small for the default extent
					run_one(lvl, lyr, fc, f, bytes_per, du_default - 1, 0, 0, KIND_MUST_REJECT, false, "undersize-buffer");
				if (is_pvrtc) // PVRTC1 is whole-image: any non-tight pitch must be rejected
					run_one(lvl, lyr, fc, f, bytes_per, declared_units(0, 0), nbx + 2, 0, KIND_MUST_REJECT, false, "pvrtc-nontight-pitch");
				else if (!uncompressed && (nbx >= 2)) // compressed pitch < num_blocks_x would drop blocks (generous buffer, so only the pitch is bad)
					run_one(lvl, lyr, fc, f, bytes_per, nbx * nby, nbx - 1, 0, KIND_MUST_REJECT, false, "compressed-shrink-pitch");
			} // fmt

			// ---- Per-(level,layer,face) must-reject: out-of-range indices & null output (format-independent) ----
			{
				const basist::transcoder_texture_format f = basist::transcoder_texture_format::cTFRGBA32;
				const uint32_t bytes_per = basist::basis_get_bytes_per_block_or_pixel(f);
				const uint32_t du = W * H;
				run_one(levels,    lyr,        fc,    f, bytes_per, du, 0, 0, KIND_MUST_REJECT, false, "bad-level");
				run_one(lvl,       eff_layers, fc,    f, bytes_per, du, 0, 0, KIND_MUST_REJECT, false, "bad-layer");
				run_one(lvl,       lyr,        faces, f, bytes_per, du, 0, 0, KIND_MUST_REJECT, false, "bad-face");

				total_calls++; file_calls++;
				if (dds.transcode_image_level(lvl, lyr, fc, nullptr, du, f, 0, 0, 0))
				{
					fmt_error_printf("  *** EXPECTED REJECTION (null output) BUT SUCCEEDED: {} L{} A{} F{} ***\n", fn.c_str(), lvl, lyr, fc);
					total_expectation_mismatches++; file_fail++;
				}
			}
		} // level/layer/face

		if (file_fail)
			fmt_error_printf("  {} : {} call(s), {} FAILURE(s)\n", fn.c_str(), (uint64_t)file_calls, (uint64_t)file_fail);
		else
			fmt_printf("  {} : {} calls OK\n", fn.c_str(), (uint64_t)file_calls);
		fflush(stdout); // stream per-file progress (stdout is block-buffered when redirected)
	} // file

	fmt_printf("\n-test_dds_overwrite summary: {} call(s) across {} file(s) ({} skipped)\n",
		(uint64_t)total_calls, (uint64_t)dds_files.size(), files_skipped);
	fmt_printf("  buffer-overwrite failures: {}\n", (uint64_t)total_overwrite_failures);
	fmt_printf("  expectation mismatches:    {}\n", (uint64_t)total_expectation_mismatches);

	const bool success = (total_overwrite_failures == 0) && (total_expectation_mismatches == 0);
	fmt_printf("-test_dds_overwrite: {}\n", success ? "SUCCESS" : "*** FAILED ***");
	return success;
}

// Maps an -export_dds format string to a transcoder_texture_format. Accepts the
// DirectX BC formats DDS supports plus uncompressed 32-bit RGBA. Case-insensitive.
static bool parse_export_dds_format(const std::string& s, basist::transcoder_texture_format& fmt)
{
	typedef basist::transcoder_texture_format tf;

	if (strcasecmp(s.c_str(), "BC1") == 0)
		fmt = tf::cTFBC1_RGB;
	else if (strcasecmp(s.c_str(), "BC3") == 0)
		fmt = tf::cTFBC3_RGBA;
	else if (strcasecmp(s.c_str(), "BC4") == 0)
		fmt = tf::cTFBC4_R;
	else if (strcasecmp(s.c_str(), "BC5") == 0)
		fmt = tf::cTFBC5_RG;
	else if (strcasecmp(s.c_str(), "BC6H") == 0)
		fmt = tf::cTFBC6H;
	else if (strcasecmp(s.c_str(), "BC7") == 0)
		fmt = tf::cTFBC7_RGBA;
	else if (strcasecmp(s.c_str(), "RGBA32") == 0)
		fmt = tf::cTFRGBA32;
	else
		return false;

	return true;
}

// Writes a basic DX10 .dds file from source image(s). Uses basis_compressor in process_source_images()
// mode to load/mipmap/lay-out the slices, then packs them to the requested format (see basisu_dds_export.*).
static bool dds_mode(command_line_params& opts)
{
	if (!opts.m_input_filenames.size())
	{
		error_printf("-dds: no input image file(s) specified (use -file or list them on the command line)\n");
		return false;
	}

	dds_export_params dds_params;
	if (!opts.m_dds_format.size())
	{
		// No -dds_format given: default to bc7 (a reasonable general-purpose default), but warn.
		printf("Warning: -dds: no -dds_format specified; defaulting to BC7\n");
		dds_params.m_format = cDDSFmtBC7;
	}
	else if (!parse_dds_output_format(opts.m_dds_format.c_str(), dds_params.m_format))
	{
		error_printf("-dds: invalid -dds_format \"%s\". Valid: BC1 BC2 BC3 BC4 BC5 BC7 A8R8G8B8 A8B8G8R8 R8 R8G8 R5G6B5 A1R5G5B5 A4R4G4B4\n", opts.m_dds_format.c_str());
		return false;
	}

	// Apply optional BC7 packer overrides (only meaningful for -dds_format bc7).
	if (opts.m_dds_bc7_encoder != cDDSBC7Encoder_Default)
		dds_params.m_bc7_encoder = opts.m_dds_bc7_encoder;
	if (opts.m_dds_bc7f_level >= 0)
		dds_params.m_bc7f_level = opts.m_dds_bc7f_level;
	if (opts.m_dds_bc7e_scalar_level >= 0)
		dds_params.m_bc7e_scalar_level = opts.m_dds_bc7e_scalar_level;

	// Only 2D, 2D arrays, and cubemaps/cubemap arrays are supported. Reject volume/video (we always write a
	// 2D-dimension DX10 header), rather than silently producing a mislabeled 2D-array file.
	const basist::basis_texture_type tex_type = opts.m_comp_params.m_tex_type;
	if ((tex_type != basist::cBASISTexType2D) && (tex_type != basist::cBASISTexType2DArray) && (tex_type != basist::cBASISTexTypeCubemapArray))
	{
		error_printf("-dds: -tex_type %u is not supported; use 2d, 2darray, or cubemap.\n", (uint32_t)tex_type);
		return false;
	}

	// -dds is LDR only (we force the XUBC7/LDR prep path below); reject HDR up front rather than silently
	// down-converting an HDR request.
	if (opts.m_comp_params.m_hdr)
	{
		error_printf("-dds: HDR is not supported (LDR only).\n");
		return false;
	}

	// Thread pool size, same approach as compress_mode().
	uint32_t num_threads = 1;
	if (opts.m_comp_params.m_multithreading)
	{
		num_threads = get_num_hardware_threads();
		if (num_threads < 1)
			num_threads = 1;
		if (num_threads > opts.m_max_threads)
			num_threads = opts.m_max_threads;
	}
	job_pool jpool(num_threads);

	// Run the compressor in source-images-only mode: it loads the images, generates mipmaps, and lays out the
	// cubemap/array slices, but does NOT encode. We inherit the relevant CLI options (mip/cubemap/array,
	// sRGB/linear, swizzle, resample, y_flip, ...) from m_comp_params.
	basis_compressor_params params = opts.m_comp_params;
	params.m_pJob_pool = &jpool;
	params.m_read_source_images = true;
	params.m_source_filenames = opts.m_input_filenames;
	params.m_write_output_basis_or_ktx2_files = false;

	// Spoof the format to XUBC7 so the LDR prep path runs (keeps RGBA together, 4x4 block layout), and enable
	// the KTX2 constraint check (all images same resolution + same mip count == the DDS array/cubemap constraints).
	params.set_format_mode(basist::basis_tex_format::cXUBC7);
	params.m_create_ktx2_file = true;

	basis_compressor comp;
	if (!comp.init(params))
	{
		error_printf("-dds: basis_compressor::init() failed\n");
		return false;
	}

	basis_compressor::error_code ec = comp.process_source_images();
	if (ec != basis_compressor::cECSuccess)
	{
		error_printf("-dds: failed loading/preparing source images (error code %u)\n", (uint32_t)ec);
		return false;
	}

	// Output filename: -output_file if given, else <first_input_base>.dds (honoring -output_path).
	std::string output_filename;
	if (opts.m_output_filename.size())
	{
		output_filename = opts.m_output_filename;
	}
	else
	{
		std::string base;
		string_get_filename(opts.m_input_filenames[0].c_str(), base);
		string_remove_extension(base);
		output_filename = base + ".dds";

		if (opts.m_output_path.size())
			string_combine_path(output_filename, opts.m_output_path.c_str(), output_filename.c_str());
	}

	uint32_t width = 0, height = 0, levels = 0, layers = 0, faces = 0;
	uint8_vec dds_data;
	std::string err;
	if (!build_dds(dds_data, comp, dds_params, err, &width, &height, &levels, &layers, &faces))
	{
		error_printf("-dds: %s\n", err.c_str());
		return false;
	}

	if (!write_vec_to_file(output_filename.c_str(), dds_data))
	{
		error_printf("-dds: failed writing output file \"%s\"\n", output_filename.c_str());
		return false;
	}

	// Report the variant actually written: sRGB only when the format has an sRGB DXGI variant AND it was requested.
	// Gated on status output so -quiet / -no_status_output silences it.
	const bool wrote_srgb = comp.get_params().m_ktx2_and_basis_srgb_transfer_function && dds_output_format_has_srgb_variant(dds_params.m_format);
	if (opts.m_comp_params.m_status_output)
		printf("Wrote DDS file \"%s\": %u bytes, format %s %s, %ux%u, %u level(s), %u layer(s), %u face(s)\n",
			output_filename.c_str(), dds_data.size_u32(), get_dds_output_format_string(dds_params.m_format),
			wrote_srgb ? "(sRGB)" : "(UNORM)",
			width, height, levels, layers, faces);

	return true;
}

// -export_dds <FORMAT>: transcodes each input .ktx2 file to FORMAT and writes a
// .DDS file (all mip levels/array layers/cubemap faces). FORMAT is one of
// BC1 BC3 BC4 BC5 BC6H BC7 RGBA32.
static bool export_dds_mode(command_line_params& opts)
{
	if (!opts.m_input_filenames.size())
	{
		error_printf("-export_dds: no input .ktx2 file(s) specified (use -file)\n");
		return false;
	}

	basist::transcoder_texture_format fmt;
	if (!parse_export_dds_format(opts.m_export_dds_format, fmt))
	{
		error_printf("-export_dds: invalid format \"%s\". Valid formats: BC1 BC3 BC4 BC5 BC6H BC7 RGBA32\n", opts.m_export_dds_format.c_str());
		return false;
	}

	if ((opts.m_output_filename.size()) && (opts.m_input_filenames.size() > 1))
		printf("Warning: -output_file is ignored with multiple inputs; using <base>_<FORMAT>.dds names\n");

	uint32_t num_succeeded = 0;

	// Per-file errors skip-and-continue so one bad file doesn't abandon the batch.
	for (size_t file_index = 0; file_index < opts.m_input_filenames.size(); file_index++)
	{
		const std::string& input_filename = opts.m_input_filenames[file_index];

		std::string ext(string_get_extension(input_filename));
		if (strcasecmp(ext.c_str(), "ktx2") != 0)
		{
			error_printf("-export_dds: input file \"%s\" is not a .ktx2 file, skipping\n", input_filename.c_str());
			continue;
		}

		uint8_vec ktx2_file_data;
		if (!read_file_to_vec(input_filename.c_str(), ktx2_file_data))
		{
			error_printf("Failed reading input file \"%s\", skipping\n", input_filename.c_str());
			continue;
		}

		basist::ktx2_transcoder dec;
		if (!dec.init(ktx2_file_data.data(), ktx2_file_data.size_u32()))
		{
			error_printf("Failed parsing KTX2 file \"%s\", skipping\n", input_filename.c_str());
			continue;
		}

		uint8_vec dds_data;
		// -1 = auto sRGB; decode flags from the same options -unpack uses (deblocking, HQ, etc.).
		if (!transcode_ktx2_to_dds(dec, fmt, dds_data, -1, get_transcode_flags_from_options(opts)))
		{
			error_printf("Failed exporting \"%s\" to a %s DDS file, skipping\n", input_filename.c_str(), opts.m_export_dds_format.c_str());
			continue;
		}

		// Output name: -output_file (single input only), else <base>_<FORMAT>.dds.
		// -output_path sets the directory.
		std::string output_filename;
		if ((opts.m_output_filename.size()) && (opts.m_input_filenames.size() == 1))
		{
			output_filename = opts.m_output_filename;
		}
		else
		{
			std::string base;
			string_get_filename(input_filename.c_str(), base);
			string_remove_extension(base);
			output_filename = string_format("%s_%s.dds", base.c_str(), opts.m_export_dds_format.c_str());

			if (opts.m_output_path.size())
				string_combine_path(output_filename, opts.m_output_path.c_str(), output_filename.c_str());
		}

		if (!write_vec_to_file(output_filename.c_str(), dds_data))
		{
			error_printf("Failed writing output DDS file \"%s\", skipping\n", output_filename.c_str());
			continue;
		}

		printf("Wrote DDS file \"%s\": %u bytes, format %s, %ux%u, %u level(s), %u layer(s), %u face(s)\n",
			output_filename.c_str(), dds_data.size_u32(), opts.m_export_dds_format.c_str(),
			dec.get_width(), dec.get_height(), dec.get_levels(), maximum<uint32_t>(1, dec.get_layers()), dec.get_faces());

		num_succeeded++;
	}

	return num_succeeded != 0;
}

// Maps an -export_ktx format string to a transcoder_texture_format. Compressed
// formats only (the KTX1 writer doesn't do uncompressed yet). Case-insensitive.
static bool parse_export_ktx_format(const std::string& s, basist::transcoder_texture_format& fmt)
{
	typedef basist::transcoder_texture_format tf;

	static const struct { const char* pName; tf fmt; } s_map[] =
	{
		{ "BC1", tf::cTFBC1_RGB }, { "BC3", tf::cTFBC3_RGBA }, { "BC4", tf::cTFBC4_R }, { "BC5", tf::cTFBC5_RG },
		{ "BC6H", tf::cTFBC6H }, { "BC7", tf::cTFBC7_RGBA },
		{ "ETC1", tf::cTFETC1_RGB }, { "ETC2", tf::cTFETC2_RGBA },
		{ "EAC_R11", tf::cTFETC2_EAC_R11 }, { "EAC_RG11", tf::cTFETC2_EAC_RG11 },
		{ "PVRTC1", tf::cTFPVRTC1_4_RGBA }, { "PVRTC1_RGBA", tf::cTFPVRTC1_4_RGBA }, { "PVRTC1_RGB", tf::cTFPVRTC1_4_RGB },
		{ "PVRTC2", tf::cTFPVRTC2_4_RGBA }, { "PVRTC2_RGBA", tf::cTFPVRTC2_4_RGBA }, { "PVRTC2_RGB", tf::cTFPVRTC2_4_RGB },
		{ "ASTC", tf::cTFASTC_LDR_4x4_RGBA }, { "ASTC_4x4", tf::cTFASTC_LDR_4x4_RGBA },
		{ "ASTC_5x4", tf::cTFASTC_LDR_5x4_RGBA }, { "ASTC_5x5", tf::cTFASTC_LDR_5x5_RGBA },
		{ "ASTC_6x5", tf::cTFASTC_LDR_6x5_RGBA }, { "ASTC_6x6", tf::cTFASTC_LDR_6x6_RGBA },
		{ "ASTC_8x5", tf::cTFASTC_LDR_8x5_RGBA }, { "ASTC_8x6", tf::cTFASTC_LDR_8x6_RGBA },
		{ "ASTC_10x5", tf::cTFASTC_LDR_10x5_RGBA }, { "ASTC_10x6", tf::cTFASTC_LDR_10x6_RGBA },
		{ "ASTC_8x8", tf::cTFASTC_LDR_8x8_RGBA }, { "ASTC_10x8", tf::cTFASTC_LDR_10x8_RGBA },
		{ "ASTC_10x10", tf::cTFASTC_LDR_10x10_RGBA }, { "ASTC_12x10", tf::cTFASTC_LDR_12x10_RGBA },
		{ "ASTC_12x12", tf::cTFASTC_LDR_12x12_RGBA },
		{ "ASTC_HDR_4x4", tf::cTFASTC_HDR_4x4_RGBA }, { "ASTC_HDR_6x6", tf::cTFASTC_HDR_6x6_RGBA },
	};

	for (uint32_t i = 0; i < (uint32_t)std::size(s_map); i++)
	{
		if (strcasecmp(s.c_str(), s_map[i].pName) == 0)
		{
			fmt = s_map[i].fmt;
			return true;
		}
	}

	return false;
}

// -export_ktx <FORMAT>: transcodes each input .ktx2 file to a compressed KTX1 (.ktx)
// file (all mip levels/array layers/cubemap faces). Compressed formats only.
static bool export_ktx_mode(command_line_params& opts)
{
	if (!opts.m_input_filenames.size())
	{
		error_printf("-export_ktx: no input .ktx2 file(s) specified (use -file)\n");
		return false;
	}

	basist::transcoder_texture_format fmt;
	if (!parse_export_ktx_format(opts.m_export_ktx_format, fmt))
	{
		error_printf("-export_ktx: invalid/unsupported format \"%s\". Valid: BC1 BC3 BC4 BC5 BC6H BC7 ETC1 ETC2 EAC_R11 EAC_RG11 PVRTC1[_RGB] PVRTC2[_RGB] ASTC ASTC_<WxH> ASTC_HDR_4x4 ASTC_HDR_6x6\n", opts.m_export_ktx_format.c_str());
		return false;
	}

	if ((opts.m_output_filename.size()) && (opts.m_input_filenames.size() > 1))
		printf("Warning: -output_file is ignored with multiple inputs; using <base>_<FORMAT>.ktx names\n");

	uint32_t num_succeeded = 0;

	// Per-file errors skip-and-continue so one bad file doesn't abandon the batch.
	for (size_t file_index = 0; file_index < opts.m_input_filenames.size(); file_index++)
	{
		const std::string& input_filename = opts.m_input_filenames[file_index];

		std::string ext(string_get_extension(input_filename));
		if (strcasecmp(ext.c_str(), "ktx2") != 0)
		{
			error_printf("-export_ktx: input file \"%s\" is not a .ktx2 file, skipping\n", input_filename.c_str());
			continue;
		}

		uint8_vec ktx2_file_data;
		if (!read_file_to_vec(input_filename.c_str(), ktx2_file_data))
		{
			error_printf("Failed reading input file \"%s\", skipping\n", input_filename.c_str());
			continue;
		}

		basist::ktx2_transcoder dec;
		if (!dec.init(ktx2_file_data.data(), ktx2_file_data.size_u32()))
		{
			error_printf("Failed parsing KTX2 file \"%s\", skipping\n", input_filename.c_str());
			continue;
		}

		uint8_vec ktx_data;
		// -1 = auto sRGB; decode flags from the same options -unpack uses (deblocking, HQ, etc.).
		if (!transcode_ktx2_to_ktx(dec, fmt, ktx_data, -1, get_transcode_flags_from_options(opts)))
		{
			error_printf("Failed exporting \"%s\" to a %s KTX file, skipping\n", input_filename.c_str(), opts.m_export_ktx_format.c_str());
			continue;
		}

		// Output name: -output_file (single input only), else <base>_<FORMAT>.ktx.
		// -output_path sets the directory.
		std::string output_filename;
		if ((opts.m_output_filename.size()) && (opts.m_input_filenames.size() == 1))
		{
			output_filename = opts.m_output_filename;
		}
		else
		{
			std::string base;
			string_get_filename(input_filename.c_str(), base);
			string_remove_extension(base);
			output_filename = string_format("%s_%s.ktx", base.c_str(), opts.m_export_ktx_format.c_str());

			if (opts.m_output_path.size())
				string_combine_path(output_filename, opts.m_output_path.c_str(), output_filename.c_str());
		}

		if (!write_vec_to_file(output_filename.c_str(), ktx_data))
		{
			error_printf("Failed writing output KTX file \"%s\", skipping\n", output_filename.c_str());
			continue;
		}

		printf("Wrote KTX file \"%s\": %u bytes, format %s, %ux%u, %u level(s), %u layer(s), %u face(s)\n",
			output_filename.c_str(), ktx_data.size_u32(), opts.m_export_ktx_format.c_str(),
			dec.get_width(), dec.get_height(), dec.get_levels(), maximum<uint32_t>(1, dec.get_layers()), dec.get_faces());

		num_succeeded++;
	}

	return num_succeeded != 0;
}

// --tinydds_info <file(s)>: prints high-level header info for each input .DDS file (via tinydds)
// (header only, no pixel decode) -- a quick sanity check for exported DDS files.
static bool tinydds_info_mode(command_line_params& opts)
{
	if (!opts.m_input_filenames.size())
	{
		error_printf("--tinydds_info: no input .DDS file(s) specified (use -file)\n");
		return false;
	}

	uint32_t num_ok = 0;
	for (size_t i = 0; i < opts.m_input_filenames.size(); i++)
	{
		if (i)
			printf("\n");
		if (print_dds_info(opts.m_input_filenames[i].c_str()))
			num_ok++;
	}

	return num_ok != 0;
}

// --ktx_info <file(s)>: prints the KTX1 (.ktx) header fields for each input file
// (header only, no key/value or image data) -- a quick sanity check for exports.
static bool ktx_info_mode(command_line_params& opts)
{
	if (!opts.m_input_filenames.size())
	{
		error_printf("--ktx_info: no input .ktx file(s) specified (use -file)\n");
		return false;
	}

	uint32_t num_ok = 0;
	for (size_t i = 0; i < opts.m_input_filenames.size(); i++)
	{
		if (i)
			printf("\n");
		if (print_ktx_info(opts.m_input_filenames[i].c_str()))
			num_ok++;
	}

	return num_ok != 0;
}

static int main_internal(int argc, const char** argv)
{
	printf("Basis Universal LDR/HDR GPU Texture Supercompression System v" BASISU_TOOL_VERSION

#if defined(_ARM64EC_) || defined(_ARM64_)
		" (ARM64)"
#elif defined(_M_IX86)
		" (x86)"
#elif defined(_M_X64) || defined(_M_AMD64)
		" (x64)"
#elif defined(__wasi__)
		" (WASI"
#if BASISU_WASI_THREADS
		" Threaded"
#endif
		")"
#endif

		"\nCopyright (C) 2019-2026 Binomial LLC, All rights reserved\n");

#ifdef FORCE_SAN_FAILURE
	force_san_failure();
#endif

	//interval_timer tm;
	//tm.start();

	// See if OpenCL support has been disabled. We don't want to parse the command line until the lib is initialized
	bool use_opencl = false;
	bool opencl_force_serialization = false;
	bool astc_peek_flag = false;
	bool astc_fuzz_flag = false;

	for (int i = 1; i < argc; i++)
	{
		if (opt_match(argv[i], {"-opencl", "-clbench"}))
			use_opencl = true;

		if (opt_match(argv[i], "-opencl_serialize"))
			opencl_force_serialization = true;

		if (opt_match(argv[i], {"-peek_astc", "-peek"}))
			astc_peek_flag = true;

		if (opt_match(argv[i], "-dev_astc_fuzz"))
			astc_fuzz_flag = true;
	}

#if !BASISU_SUPPORT_OPENCL
	if (use_opencl)
	{
		fprintf(stderr, "WARNING: -opencl specified, but OpenCL support was not defined or enabled at compile time! With cmake, use -D BASISU_OPENCL=1. Falling back to CPU compression.\n");
	}
#endif
		
	basisu_encoder_init(use_opencl, opencl_force_serialization);

#if 0
	image a, b;
	if (!load_png("d:/dev/test_images/bik/bik23.png", a))
		return EXIT_FAILURE;
	if (!load_png("c:/dev/e23_4.png", b))
		return EXIT_FAILURE;

	fmt_printf("Resolution: {}x{}\n", a.get_width(), a.get_height());

	print_image_metrics(a, b);

	psnr_hvs_metrics hvs_metrics;
	if (!psnr_hvs_compute_metrics(a, b, hvs_metrics))
		return EXIT_FAILURE;
	psnr_hvs_print_metrics(hvs_metrics);
	
#if 0
	for (uint32_t y = 0; y < a.get_height(); y++)
	{
		for (uint32_t x = 0; x < a.get_width(); x++)
		{
			a(x, y).set(get_601_y(a(x, y).r, a(x, y).g, a(x, y).b));
			b(x, y).set(get_601_y(b(x, y).r, b(x, y).g, b(x, y).b));
		}
	}

	image_metrics im;
	image_metrics::hvs_metrics hvs[3];
			
	double sum_hvs_mseh = 0.0f;
	double sum_hvsm_mseh = 0.0f;

	for (uint32_t c = 0; c < 3; c++)
	{
		if (!im.compute_hvs_chan(a, b, c, hvs[c]))
			return EXIT_FAILURE;
		
		fmt_printf("{c}: PSNR-HVS: {}, PSNR-HVS-M: {}\n", (int)("RGB"[c]), hvs[c].psnr_hvs, hvs[c].psnr_hvsm);
		
		sum_hvs_mseh += hvs[c].mseh_hvs;
		sum_hvsm_mseh += hvs[c].mseh_hvsm;
	}

	sum_hvs_mseh /= 3.0f;
	sum_hvsm_mseh /= 3.0f;

	fmt_printf("RGB PSNR-HVS: {}\n", psnr_hvs_calc_psnr(sum_hvs_mseh, 1.0f));
	fmt_printf("RGB PSNR-HVS-M: {}\n", psnr_hvs_calc_psnr(sum_hvsm_mseh, 1.0f));
#endif
	
	exit(0);
#endif

	// Standalone DDS reader/transcoder test harness (see test_dds()).
	if ((argc >= 2) && (strcmp(argv[1], "-test_dds") == 0))
	{
		return test_dds(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	// DDS transcoder output-buffer overrun / argument-rejection sanity test (see test_dds_overwrite()).
	if ((argc >= 2) && (strcmp(argv[1], "-test_dds_overwrite") == 0))
	{
		return test_dds_overwrite(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (astc_fuzz_flag)
	{
		bool status = xuastc_ldr_decoder_fuzz_test();
		return status ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (astc_peek_flag)
	{
		if (argc != 3)
		{
			fmt_error_printf("Requires filename argument of .astc file\n");
			return EXIT_FAILURE;
		}

		bool status = peek_astc_file(argv[2]);
		return status ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	//printf("Encoder and transcoder libraries initialized in %3.3f ms\n", tm.get_elapsed_ms());
		
	if (argc == 1)
	{
		print_usage();
		return EXIT_FAILURE;
	}
		
	command_line_params opts;

#if defined(__wasi__) && !BASISU_WASI_THREADS
	opts.m_comp_params.m_multithreading = false;
#endif

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
				// If they haven't specified any modes, and they give us a .basis/.ktx2 file, then assume they want to unpack it.
				// NOTE: a bare .dds is intentionally NOT auto-unpacked -- it is a valid compressor SOURCE image
				// (read_uncompressed_dds_file). To unpack a .dds use -unpack, or -info for header info.
				opts.m_mode = cUnpack;
				break;
			}
		}
	}

	// Note: -info/-validate/-unpack all flow through unpack_and_validate_mode(), which sniffs each input's
	// magic per-file (KTX2 / DDS / KTX1 / .basis) and routes it to the right handler -- so a mixed-type input
	// list (e.g. a .ktx2 and a .dds on one command line) works without any extension-based mode rerouting.

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
	case cCompareHVS:
		status = compare_hvs_mode(opts);
		break;
	case cHDRCompare:
		status = hdr_compare_mode(opts);
		break;
	case cImageDumpStats:
		status = image_stats_mode(opts);
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
	case cTestXUASTCLDR:
		status = test_mode_xuastc_ldr(opts);
		break;
	case cTestCodecs:
		status = test_codecs_run(opts);
		break;
	case cTestCodecsGen:
		status = test_codecs_generate(opts);
		break;
	case cTestHDR_4x4:
		status = test_mode_hdr(opts, basist::basis_tex_format::cUASTC_HDR_4x4, std::size(g_hdr_4x4_test_files), g_hdr_4x4_test_files, 0.0f);
		break;
	case cTestHDR_6x6:
		status = test_mode_hdr(opts, basist::basis_tex_format::cASTC_HDR_6x6, std::size(g_hdr_6x6_test_files), g_hdr_6x6_test_files, 0.0f);
		break;
	case cTestHDR_6x6i:
		status = test_mode_hdr(opts, basist::basis_tex_format::cUASTC_HDR_6x6_INTERMEDIATE, std::size(g_hdr_6x6i_test_files), g_hdr_6x6i_test_files, 0.0f);
		
		if (status)
		{
			status = test_mode_hdr(opts, basist::basis_tex_format::cUASTC_HDR_6x6_INTERMEDIATE, std::size(g_hdr_6x6i_l_test_files), g_hdr_6x6i_l_test_files, 500.0f);
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
	case cExtractChannel:
		status = extract_channel_mode(opts);
		break;
	case cExtractSwizzle:
		status = extract_swizzle_mode(opts);
		break;
	case cExtractRegion:
		status = extract_region_mode(opts);
		break;
	case cTextToPng:
		status = text_to_png(opts.m_text_image_in.c_str(), opts.m_text_image_out.c_str());
		break;
	case cPngToText:
		status = png_to_text(opts.m_text_image_in.c_str(), opts.m_text_image_out.c_str());
		break;
	case cTonemapImage:
		status = tonemap_image_mode(opts);
		break;
	case cDDS:
		status = dds_mode(opts);
		break;
	case cExportDDS:
		status = export_dds_mode(opts);
		break;
	case cExportKTX:
		status = export_ktx_mode(opts);
		break;
	case cTinyDDSInfo:
		status = tinydds_info_mode(opts);
		break;
	case cKTXInfo:
		status = ktx_info_mode(opts);
		break;
	case cBenchmarkSingle:
		status = benchmark_single_mode(opts);
		break;
	case cBenchmarkSweep:
		status = benchmark_sweep_mode(opts);
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

// Attempt to detect AddressSanitizer (ASan) across compilers - only used for debug 
// output purposes.
#ifndef DETECT_ASAN_H
#define DETECT_ASAN_H

// Start with ASAN disabled
#undef USING_ASAN

#if defined(__wasi__)
    #define USING_ASAN 0
#else
    // --- Clang / Apple Clang: use __has_feature ---
    #if defined(__has_feature)
    #  if __has_feature(address_sanitizer)
    #    define USING_ASAN 1
    #  endif
    #endif

    // --- GCC: __SANITIZE_ADDRESS__ ---
    #if defined(__SANITIZE_ADDRESS__)
    #  define USING_ASAN 1
    #endif
#endif // #if defined(__wasi__)

// If still undefined, ensure USING_ASAN is cleanly defined to 0
#ifndef USING_ASAN
#  define USING_ASAN 0
#endif

#endif // DETECT_ASAN_H

//-----------------------------------------------------------------------------------

int main(int argc, const char** argv)
{
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif

#if CLEAR_WIN32_CONSOLE
	clear_console();
	fmt_printf("{}\n", argv[0]);
#endif

#if defined(DEBUG) || defined(_DEBUG)
	printf("DEBUG or _DEBUG defined\n");
#endif
#if !defined(NDEBUG)
	printf("NDEBUG is NOT defined\n");
#endif
#if USING_ASAN
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

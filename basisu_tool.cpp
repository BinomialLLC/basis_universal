// basisu_tool.cpp
// Copyright (C) 2017-2019 Binomial LLC. All Rights Reserved.
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
#include "transcoder/basisu.h"
#include "transcoder/basisu_transcoder_internal.h"
#include "basisu_enc.h"
#include "basisu_etc.h"
#include "basisu_gpu_texture.h"
#include "basisu_frontend.h"
#include "basisu_backend.h"
#include "transcoder/basisu_global_selector_palette.h"
#include "basisu_comp.h"
#include "transcoder/basisu_transcoder.h"
#include "basisu_ssim.h"
#if defined(_OPENMP)
#include <omp.h>
#endif

using namespace basisu;

#define BASISU_TOOL_VERSION "1.00.00"

enum tool_mode
{
	cDefault,
	cCompress,
	cValidate,
	cUnpack,
	cCompare
};

static void print_usage()
{
	printf("\nUsage: basisu filename [filename ...] <options>\n");
	
	puts("\n"
		"The default mode is compression of one or more PNG files to a .basis file. Alternate modes:\n"
		" -unpack: Use transcoder to unpack .basis file to one or more .ktx/.png files\n"
		" -validate: Validate and display information about a .basis file\n"
		" -compare: Compare two PNG images specified with -file, output PSNR and SSIM statistics and RGB/A delta images\n"
		"\n"
		"Important: By default, the compressor assumes the input is not sRGB. If the input is sRGB (diffuse/albedo textures, images, etc), be sure to specify -srgb for much better compression.\n"
		"\n"
		"Options:\n"
		" -file filename.png: Input image filename, multiple images are OK, use -file X for each input filename (prefixing input filenames with -file is now optional)\n"
		" -alpha_file filename.png: Input alpha image filename, multiple images are OK, use -file X for each input filename (must be paired with -file), images converted to REC709 grayscale and used as input alpha\n"
		" -multifile_printf: printf() format strint to use to compose multiple filenames\n"
		" -multifile_first: The index of the first file to process, default is 0 (must specify -multifile_printf and -multifile_num))\n"
		" -multifile_num: The total number of files to process\n"
		" -srgb: Use perceptual colorspace metrics for significantly higher rate distortion performance on sRGB textures. Don't use on non-sRGB inputs.\n"
		" -q X: Set quality level, 1-255, default is 128, lower=better compression/lower quality/faster, higher=less compression/higher quality/slower, default is 128\n"
		" -output_file filename: Output .basis/.ktx filename\n"
		" -output_path: Output .basis/.ktx files to specified directory\n"
		" -debug_output: Enable codec debug print to stdout (slightly slower)\n"
		" -debug_images: Enable codec debug images (much slower)\n"
		" -compute_stats: Compute and display image quality metrics (slightly slower)\n"
		" -slower: Enable optional stages in the compressor for slower but higher quality compression\n"
		"\n"
		"More options:\n"
		" -max_endpoint_clusters X: Manually set the max number of color endpoint clusters from 1-8192, use instead of -q\n"
		" -max_selector_clusters X: Manually set the max number of color selector clusters from 1-7936, use instead of -q\n"
		" -y_flip: Flip input images vertically before compression\n"
		" -normal_map: Tunes codec parameters for better quality on normal maps (no selector RDO, no sRGB)\n"
		" -no_alpha: Always output non-alpha basis files, even if one or more inputs has alpha\n"
		" -force_alpha: Always output alpha basis files, even if no inputs has alpha\n"
		" -seperate_rg_to_color_alpha: Seperate input R and G channels to RGB and A (for tangent space XY normal maps)\n"
		" -no_multithreading: Disable OpenMP multithreading\n"
		"\n"
		"Mipmap generation options:\n"
		" -mipmap: Generate mipmaps for each source image\n"
		" -mip_scale X: Set mipmap filter kernel's scale, lower=sharper, higher=more blurry, default is 1.0\n"
		" -mip_filter X: Set mipmap filter kernel, default is kaiser, filters: box, tent, bell, blackman, catmullrom, mitchell, etc.\n"
		" -mip_renorm: Renormalize normal map to unit length vectors after filtering\n"
		" -mip_clamp: Use clamp addressing on borders, instead of wrapping\n"
		" -mip_smallest X: Set smallest pixel dimension for generated mipmaps, default is 1\n"
		" -mip_srgb: Convert image to linear before filtering, then back to sRGB\n"
		"\n"
		"Backend selector RDO codec options:\n"
		" -no_selector_rdo: Disable backend's selector rate distortion optimizations (slightly faster, less noisy output, but lower quality per output bit)\n"
		" -selector_rdo_thresh X: Set selector RDO quality threshold, default is 1.25, lower is higher quality but less quality per output bit (try 1.0-3.0)\n"
		"\n"
		"Hierarchical virtual selector codebook options:\n"
		" -global_sel_pal: Always use vitual selector palettes (instead of custom palettes), slightly smaller files, but lower quality, slower encoding\n"
		" -no_auto_global_sel_pal: Don't automatically use virtual selector palettes on small images\n"
		" -no_hybrid_sel_cb: Don't automatically use hybrid virtual selector codebooks (for higher quality, only active when -global_sel_pal is specified)\n"
		" -global_pal_bits X: Set virtual selector codebook palette bits, range is [0,12], default is 8, higher is slower/better quality\n"
		" -global_mod_bits X: Set virtual selector codebook modifier bits, range is [0,15], defualt is 8, higher is slower/better quality\n"
		" -no_endpoint_refinement: Disable endpoint codebook refinement stage (slightly faster, but lower quality)\n"
		" -hybrid_sel_cb_quality_thresh X: Set hybrid selector codebook quality threshold, default is 2.0, try 1.5-3, higher is lower quality/smaller codebooks\n"
		"\n"
		"Various command line examples:\n"
		"basisu -srgb -file x.png -mipmap -y_flip : Compress a mipmapped x.basis file from an sRGB image named x.png, Y flip each source image\n"
		"basisu -validate -file x.basis : Validate x.basis (check header, check file CRC's, attempt to transcode all slices)\n"
		"basisu -unpack -file x.basis : Validates, transcodes and unpacks x.basis to mipmapped .KTX and RGB/A .PNG files (transcodes to all supported GPU texture formats)\n"
		"basisu -q 255 -srgb -file x.png -mipmap -debug_output -comput_stats : Compress sRGB x.png to x.basis at quality level 255 with compressor debug output/statistics\n"
		"basisu -max_endpoint_clusters 8192 -max_selector_clusters 7936 -file x.png : Compress non-sRGB x.png to x.basis using the largest supported manually specified codebook sizes\n"
		"basisu -global_sel_pal -no_hybrid_sel_cb -file x.png : Compress a non-sRGB image, use virtual selector codebooks for improved compression (but slower encoding)\n"
		"basisu -global_sel_pal -file x.png: Compress a non-sRGB image, use hybrid selector codebooks for slightly improved compression (but slower encoding)\n"
		"basisu -srgb -multifile_printf \"x%02u.png\" -multifile_first 1 -multifile_count 20 : Compress a 20 sRGB source image video sequence (x01.png, x02.png, x03.png, etc.) to x01.basis\n"
		"basisu -srgb x.png : Compress sRGB image x.png to x.basis using default settings (multiple filenames OK)\n"
		"basisu x.basis : Unpack x.basis to PNG/KTX files (multiple filenames OK)\n"
	);
}

class command_line_params
{
public:
	command_line_params() :
		m_mode(cDefault),
		m_multifile_first(0),
		m_multifile_num(0)
	{
	}

	bool parse(int arg_c, const char **arg_v)
	{
		int arg_index = 1;
		while (arg_index < arg_c)
		{
			const char *pArg = arg_v[arg_index];
			const int num_remaining_args = arg_c - (arg_index + 1);
			int arg_count = 1;

#define REMAINING_ARGS_CHECK(n) if (num_remaining_args < (n)) { error_printf("Error: Expected %u values to follow %s!\n", n, pArg); return false; }

			if (strcasecmp(pArg, "-compress") == 0)
				m_mode = cCompress;
			else if (strcasecmp(pArg, "-compare") == 0)
				m_mode = cCompare;
			else if (strcasecmp(pArg, "-unpack") == 0)
				m_mode = cUnpack;
			else if (strcasecmp(pArg, "-validate") == 0)
				m_mode = cValidate;
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
			else if (strcasecmp(pArg, "-srgb") == 0)
				m_comp_params.m_perceptual = true;
			else if (strcasecmp(pArg, "-q") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_quality_level = clamp<int>(atoi(arg_v[arg_index + 1]), BASISU_QUALITY_MIN, BASISU_QUALITY_MAX);
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
			else if (strcasecmp(pArg, "-debug_output") == 0)
			{
				m_comp_params.m_debug = true;
				enable_debug_printf(true);
			}
			else if (strcasecmp(pArg, "-debug_images") == 0)
				m_comp_params.m_debug_images = true;
			else if (strcasecmp(pArg, "-compute_stats") == 0)
				m_comp_params.m_compute_stats = true;
			else if (strcasecmp(pArg, "-slower") == 0)
				m_comp_params.m_faster = false;
			else if (strcasecmp(pArg, "-max_endpoint_clusters") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_max_endpoint_clusters = clamp<int>(atoi(arg_v[arg_index + 1]), 1, BASISU_MAX_ENDPOINT_CLUSTERS);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-max_selector_clusters") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_max_selector_clusters = clamp<int>(atoi(arg_v[arg_index + 1]), 1, BASISU_MAX_SELECTOR_CLUSTERS);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-y_flip") == 0)
				m_comp_params.m_y_flip = true;
			else if (strcasecmp(pArg, "-normal_map") == 0)
			{
				m_comp_params.m_perceptual = false;
				m_comp_params.m_mip_srgb = false;
				m_comp_params.m_no_selector_rdo = true;
			}
			else if (strcasecmp(pArg, "-no_alpha") == 0)
				m_comp_params.m_check_for_alpha = false;
			else if (strcasecmp(pArg, "-force_alpha") == 0)
				m_comp_params.m_force_alpha = true;
			else if (strcasecmp(pArg, "-seperate_rg_to_color_alpha") == 0)
				m_comp_params.m_seperate_rg_to_color_alpha = true;
			else if (strcasecmp(pArg, "-no_multithreading") == 0)
			{
#if defined(_OPENMP)
				omp_set_num_threads(1);
#endif				
			}
			else if (strcasecmp(pArg, "-mipmap") == 0)
				m_comp_params.m_mip_gen = true;
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
			else if (strcasecmp(pArg, "-mip_smallest") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_mip_smallest_dimension = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-mip_srgb") == 0)
				m_comp_params.m_mip_srgb = true;
			else if (strcasecmp(pArg, "-no_selector_rdo") == 0)
				m_comp_params.m_no_selector_rdo = true;
			else if (strcasecmp(pArg, "-selector_rdo_thresh") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_selector_rdo_thresh = (float)atof(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-global_sel_pal") == 0)
				m_comp_params.m_global_sel_pal = true;
			else if (strcasecmp(pArg, "-no_endpoint_refinement") == 0)
				m_comp_params.m_no_endpoint_refinement = true;
			else if (strcasecmp(pArg, "-no_auto_global_sel_pal") == 0)
				m_comp_params.m_no_auto_global_sel_pal = true;
			else if (strcasecmp(pArg, "-global_pal_bits") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_global_pal_bits = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-global_mod_bits") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_global_mod_bits = atoi(arg_v[arg_index + 1]);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-no_hybrid_sel_cb") == 0)
				m_comp_params.m_no_hybrid_sel_cb = true;
			else if (strcasecmp(pArg, "-hybrid_sel_cb_quality_thresh") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				m_comp_params.m_hybrid_sel_cb_quality_thresh = (float)atof(arg_v[arg_index + 1]);
				arg_count++;
			}
			else
			{
				// Let's assume it's a source filename, so globbing works
				//error_printf("Unrecognized command line option: %s\n", pArg);
				m_input_filenames.push_back(pArg);
			}

			arg_index += arg_count;
		}
		
		if (m_comp_params.m_quality_level != -1)
		{
			m_comp_params.m_max_endpoint_clusters = 0;
			m_comp_params.m_max_selector_clusters = 0;
		}
		else if ((!m_comp_params.m_max_endpoint_clusters) || (!m_comp_params.m_max_selector_clusters))
		{
			m_comp_params.m_max_endpoint_clusters = 0;
			m_comp_params.m_max_selector_clusters = 0;

			m_comp_params.m_quality_level = 128;
		}

		return true;
	}

	basis_compressor_params m_comp_params;
		
	tool_mode m_mode;
		
	std::vector<std::string> m_input_filenames;
	std::vector<std::string> m_input_alpha_filenames;

	std::string m_output_filename;
	std::string m_output_path;

	std::string m_multifile_printf;
	uint32_t m_multifile_first;
	uint32_t m_multifile_num;
};

static bool expand_multifile(command_line_params &opts)
{
	if (!opts.m_multifile_printf.size())
		return true;
	
	if (!opts.m_multifile_num)
	{
		error_printf("Error: -multifile_printf specified, but not -multifile_num\n");
		return false;
	}
	
	std::string fmt(opts.m_multifile_printf);
	size_t x = fmt.find_first_of('!');
	if (x != std::string::npos)
		fmt[x] = '%';

	if (string_find_right(fmt, '%') == -1)
	{
		error_printf("Error: Must include C-style printf() format character '%' in -multifile_printf string\n");
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

static bool compress_mode(command_line_params &opts)
{
	basist::etc1_global_selector_codebook sel_codebook(basist::g_global_selector_cb_size, basist::g_global_selector_cb);

	if (!expand_multifile(opts))
		return false;
		
	if (!opts.m_input_filenames.size())
	{
		error_printf("No input files to process!\n");
		return false;
	}
				
	basis_compressor_params &params = opts.m_comp_params;

	params.m_source_filenames = opts.m_input_filenames;
	params.m_source_alpha_filenames = opts.m_input_alpha_filenames;

	params.m_read_source_images = true;
	params.m_write_output_basis_files = true;
	params.m_pSel_codebook = &sel_codebook;

	if (opts.m_output_filename.size())
		params.m_out_filename = opts.m_output_filename;
	else 
	{
		std::string filename;
		
		string_get_filename(opts.m_input_filenames[0].c_str(), filename);
		string_remove_extension(filename);
		filename += ".basis";

		if (opts.m_output_path.size())
			string_combine_path(filename, opts.m_output_path.c_str(), filename.c_str());
		
		params.m_out_filename = filename;
	}
		
	basis_compressor c;

	if (!c.init(opts.m_comp_params))
	{
		error_printf("basis_compressor::init() failed!\n");
		return false;
	}

	basis_compressor::error_code ec = c.process();
	if (ec != basis_compressor::cECSuccess)
	{
		switch (ec)
		{
			case basis_compressor::cECFailedReadingSourceImages:
				error_printf("Compressor failed reading a source image!\n");
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
			default:
				error_printf("basis_compress::process() failed!\n");
				break;
		}
		
		return false;
	}

	printf("Compression succeeded\n");

	return true;
}

static bool unpack_and_validate_mode(command_line_params &opts, bool validate_flag)
{
	basist::etc1_global_selector_codebook sel_codebook(basist::g_global_selector_cb_size, basist::g_global_selector_cb);
		
	if (!opts.m_input_filenames.size())
	{
		error_printf("No input files to process!\n");
		return false;
	}

	for (uint32_t file_index = 0; file_index < opts.m_input_filenames.size(); file_index++)
	{
		const char *pInput_filename = opts.m_input_filenames[file_index].c_str();

		std::string base_filename;
		string_split_path(pInput_filename, nullptr, nullptr, &base_filename, nullptr);

		uint8_vec basis_data;
		if (!basisu::read_file_to_vec(pInput_filename, basis_data))
		{
			error_printf("Failed reading file \"%s\"\n", pInput_filename);
			return false;
		}

		printf("Input file \"%s\"\n", pInput_filename);

		if (!basis_data.size())
		{
			error_printf("File is empty!\n");
			return false;
		}

		if (basis_data.size() > UINT32_MAX)
		{
			error_printf("File is too large!\n");
			return false;
		}

		basist::basisu_transcoder dec(&sel_codebook);

		// Validate the file - note this isn't necessary for transcoding
		if (!dec.validate_file_checksums(&basis_data[0], (uint32_t)basis_data.size(), true))
		{
			error_printf("File failed CRC checks!\n");
			return false;
		}

		printf("File CRC checks succeeded\n");
				
		basist::basisu_file_info fileinfo;
		if (!dec.get_file_info(&basis_data[0], (uint32_t)basis_data.size(), fileinfo))
		{
			error_printf("Failed retrieving Basis file information!\n");
			return false;
		}

		assert(fileinfo.m_total_images == fileinfo.m_image_mipmap_levels.size());
		assert(fileinfo.m_total_images == dec.get_total_images(&basis_data[0], (uint32_t)basis_data.size()));

		printf("File info:\n");
		printf("  Version: %X\n", fileinfo.m_version);
		printf("  Total header size: %u\n", fileinfo.m_total_header_size);
		printf("  Total selectors: %u\n", fileinfo.m_total_selectors);
		printf("  Selector codebook size: %u\n", fileinfo.m_selector_codebook_size);
		printf("  Total endpoints: %u\n", fileinfo.m_total_endpoints);
		printf("  Endpoint codebook size: %u\n", fileinfo.m_endpoint_codebook_size);
		printf("  Tables size: %u\n", fileinfo.m_tables_size);
		printf("  Slices size: %u\n", fileinfo.m_slices_size);
		printf("  Total slices: %u\n", (uint32_t)fileinfo.m_slice_info.size());
		printf("  Total images: %i\n", fileinfo.m_total_images);
		printf("  Image mipmap levels: ");
		for (uint32_t i = 0; i < fileinfo.m_total_images; i++)
			printf("%u ", fileinfo.m_image_mipmap_levels[i]);
		printf("\n");
		printf("  Y Flipped: %u, Has alpha slices: %u\n", fileinfo.m_y_flipped, fileinfo.m_has_alpha_slices);
				
		if (!dec.start_decoding(&basis_data[0], (uint32_t)basis_data.size()))
		{
			error_printf("start_decoding() failed!\n");
			return false;
		}

		std::vector< gpu_image_vec > gpu_images[basist::cTFTotalTextureFormats];

		for (int format_iter = 0; format_iter < basist::cTFTotalTextureFormats; format_iter++)
		{
			basist::transcoder_texture_format tex_fmt = static_cast<basist::transcoder_texture_format>(format_iter);
			
			gpu_images[tex_fmt].resize(fileinfo.m_total_images);

			for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index++)
				gpu_images[tex_fmt][image_index].resize(fileinfo.m_image_mipmap_levels[image_index]);
		}

		bool pvrtc_nonpow2_warning = false;

		// Now transcode the file to all supported texture formats and save mipmapped KTX files
		for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index++)
		{
			for (uint32_t level_index = 0; level_index < fileinfo.m_image_mipmap_levels[image_index]; level_index++)
			{
				basist::basisu_image_level_info level_info;

				if (!dec.get_image_level_info(&basis_data[0], (uint32_t)basis_data.size(), level_info, image_index, level_index))
				{
					error_printf("Failed retrieving image level information (%u %u)!\n", image_index, level_index);
					return false;
				}

				for (int format_iter = 0; format_iter < basist::cTFTotalTextureFormats; format_iter++)
				{
					const basist::transcoder_texture_format transcoder_tex_fmt = static_cast<basist::transcoder_texture_format>(format_iter);

					if (transcoder_tex_fmt == basist::cTFPVRTC1_4_OPAQUE_ONLY)
					{
						if (!is_pow2(level_info.m_width) || !is_pow2(level_info.m_height))
						{
							if (!pvrtc_nonpow2_warning)
							{
								pvrtc_nonpow2_warning = true;

								printf("Warning: Will not transcode image %u level %u res %ux%u to PVRTC1 (one or more dimension is not a power of 2)\n", image_index, level_index, level_info.m_width, level_info.m_height);
							}
							
							// Can't transcode this image level to PVRTC because it's not a pow2 (we're going to support transcoding non-pow2 to the next larger pow2 soon)
							continue;
						}
					}

					basisu::texture_format tex_fmt = basis_get_basisu_texture_format(transcoder_tex_fmt);
				
					gpu_image &gi = gpu_images[transcoder_tex_fmt][image_index][level_index];
					gi.init(tex_fmt, level_info.m_orig_width, level_info.m_orig_height);

					if (!dec.transcode_image_level(&basis_data[0], (uint32_t)basis_data.size(), image_index, level_index, gi.get_ptr(), gi.get_total_blocks(), transcoder_tex_fmt, 0))
					{
						error_printf("Failed transcoding image level (%u %u %u)!\n", image_index, level_index, format_iter);
						return false;
					}

					printf("Transcode of image %u level %u res %ux%u format %s succeeded\n", image_index, level_index, level_info.m_orig_width, level_info.m_orig_height, basist::basis_get_format_name(transcoder_tex_fmt));

				} // format_iter
			
			} // level_index

		} // image_info

		if (!validate_flag)
		{
			// Now write KTX files and unpack them to individual PNG's
				
			for (int format_iter = 0; format_iter < basist::cTFTotalTextureFormats; format_iter++)
			{
				const basist::transcoder_texture_format transcoder_tex_fmt = static_cast<basist::transcoder_texture_format>(format_iter);

				for (uint32_t image_index = 0; image_index < fileinfo.m_total_images; image_index++)
				{
					gpu_image_vec &gi = gpu_images[format_iter][image_index];

					if (!gi.size())
						continue;
				
					uint32_t level;
					for (level = 0; level < gi.size(); level++)
						if (!gi[level].get_total_blocks())
							break;

					if (level < gi.size())
						continue;

					std::string ktx_filename(base_filename + string_format("_transcoded_%s_%u.ktx", basist::basis_get_format_name(transcoder_tex_fmt), image_index));
					if (!write_compressed_texture_file(ktx_filename, gi))
					{
						error_printf("Failed writing KTX file \"%s\"!\n", ktx_filename.c_str());
						return false;
					}
					printf("Wrote KTX file \"%s\"\n", ktx_filename.c_str());

					for (uint32_t level_index = 0; level_index < gi.size(); level_index++)
					{
						basist::basisu_image_level_info level_info;

						if (!dec.get_image_level_info(&basis_data[0], (uint32_t)basis_data.size(), level_info, image_index, level_index))
						{
							error_printf("Failed retrieving image level information (%u %u)!\n", image_index, level_index);
							return false;
						}

						image u;
						if (!gi[level_index].unpack(u))
						{
							error_printf("Failed unpacking GPU texture data (%u %u %u)\n", format_iter, image_index, level_index);
							return false;
						}
						//u.crop(level_info.m_orig_width, level_info.m_orig_height);
					
						std::string rgb_filename(base_filename + string_format("_unpacked_rgb_%s_%u_%u.png", basist::basis_get_format_name(transcoder_tex_fmt), image_index, level_index));
						if (!save_png(rgb_filename, u, cImageSaveIgnoreAlpha))
						{
							error_printf("Failed writing to PNG file \"%s\"\n", rgb_filename.c_str());
							return false;
						}
						printf("Wrote PNG file \"%s\"\n", rgb_filename.c_str());

						if (basis_transcoder_format_has_alpha(transcoder_tex_fmt))
						{
							std::string a_filename(base_filename + string_format("_unpacked_a_%s_%u_%u.png", basist::basis_get_format_name(transcoder_tex_fmt), image_index, level_index));
							if (!save_png(a_filename, u, cImageSaveGrayscale, 3))
							{
								error_printf("Failed writing to PNG file \"%s\"\n", a_filename.c_str());
								return false;
							}
							printf("Wrote PNG file \"%s\"\n", a_filename.c_str());
						}

					} // level_index

				} // image_index

			} // format_iter
		} // if (!validate_flag)

	} // image_index

	printf("Success\n");
	
	return true;
}

static bool compare_mode(command_line_params &opts)
{
	if (opts.m_input_filenames.size() != 2)
	{
		error_printf("Must specify two PNG filenames using -file\n");
		return false;
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

	printf("Comparison image res: %ux%u\n", a.get_width(), a.get_height());

	image_metrics im;
	im.calc(a, b, 0, 3);
	im.print("RGB ");

	im.calc(a, b, 0, 1);
	im.print("R   ");

	im.calc(a, b, 1, 1);
	im.print("G   ");

	im.calc(a, b, 2, 1);
	im.print("B   ");

	im.calc(a, b, 0, 0);
	im.print("Y   " );

	vec4F s_rgb(compute_ssim(a, b, false));

	printf("R SSIM: %f\n", s_rgb[0]);
	printf("G SSIM: %f\n", s_rgb[1]);
	printf("B SSIM: %f\n", s_rgb[2]);
	printf("RGB Avg SSIM: %f\n", (s_rgb[0] + s_rgb[1] + s_rgb[2]) / 3.0f);
	printf("A SSIM: %f\n", s_rgb[3]);
			
	vec4F s_y(compute_ssim(a, b, true));
	printf("Y SSIM: %f\n", s_y[0]);

	image delta_img(a.get_width(), a.get_height());

	const int X = 2;

#pragma omp parallel for
	for (int y = 0; y < (int)a.get_height(); y++)
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
	
	return true;
}

int main(int argc, const char **argv)
{
	basisu_encoder_init();

	printf("Basis Universal GPU Texture Compressor v" BASISU_TOOL_VERSION ", Copyright (C) 2017-2019 Binomial LLC, All rights reserved\n");

	if (argc == 1)
	{
		print_usage();
		return EXIT_FAILURE;
	}

	command_line_params opts;
	if (!opts.parse(argc, argv))
	{
		print_usage();
		return EXIT_FAILURE;
	}

	if (opts.m_mode == cDefault)
	{
		for (size_t i = 0; i < opts.m_input_filenames.size(); i++)
		{
			std::string ext(string_get_extension(opts.m_input_filenames[i]));
			if (strcasecmp(ext.c_str(), "basis") == 0)
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
		status = unpack_and_validate_mode(opts, true);
		break;
	case cUnpack:
		status = unpack_and_validate_mode(opts, false);
		break;
	case cCompare:
		status = compare_mode(opts);
		break;
	default:
		assert(0);
		break;
	}

	return status ? EXIT_SUCCESS : EXIT_FAILURE;
}

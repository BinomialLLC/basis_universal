// example_transcoding.cpp: Very simple transcoding-only example. Does not depend on the basisu encoder library at all, just basisu_transcoder.cpp.
// You can use AMD Compressonator or Microsoft's DirectXTex tools on github to view the written DX10 .DDS file.
//
// Two demos run in main():
//   1. KTX2 -> BC7 -> writes out.dds (uses basist::ktx2_transcoder).
//   2. .DDS (BC1 and BC7, mipmapped) -> RGBA -> writes a .BMP per mip (uses basist::dds_transcoder). See transcode_dds_to_bmps() below.
#include <stdlib.h>
#include <stdio.h>

// for testing
//#define BASISD_SUPPORT_XUASTC (0)
//#define BASISD_SUPPORT_KTX2_ZSTD (0)

#include "../transcoder/basisu_transcoder.h"
#include "utils.h"
#include <string>

// ============================================================================================================
// DDS transcoding example.
//
// Loads a Microsoft .DDS file with basist::dds_transcoder, prints its per-mip info, transcodes EVERY mipmap
// level to 32-bpp RGBA, and writes each decoded mip out as an uncompressed .BMP.
//
// The dds_transcoder API deliberately mirrors ktx2_transcoder, so the usage pattern is the same:
//     init() -> start_transcoding() -> get_*() / get_image_level_info() -> transcode_image_level()
//
// pName is a bare filename looked up in ../test_files first (when run from the build dir), then the cwd.
// pOutput_prefix is prepended to each written "<prefix>_layer<L>_face<F>_mip<M>.bmp".
// ============================================================================================================
static bool transcode_dds_to_bmps(const char* pName, const char* pOutput_prefix)
{
    // 1. Read the entire .DDS file into memory. dds_transcoder BORROWS this buffer (it does not copy it), so
    //    the data must remain alive for as long as we call transcode methods on the transcoder.
    utils::uint8_vec dds_file_data;
    const std::string test_files_path = std::string("../test_files/") + pName;
    if (!utils::read_file(test_files_path.c_str(), dds_file_data))
    {
        if (!utils::read_file(pName, dds_file_data))
        {
            fprintf(stderr, "Can't read DDS file %s or %s\n", test_files_path.c_str(), pName);
            return false;
        }
    }

    if (dds_file_data.size() > UINT32_MAX)
    {
        fprintf(stderr, "DDS file too large\n");
        return false;
    }

    // 2. init() parses the DDS header(s), detects and validates the format/layout, and precomputes the byte
    //    range of every (layer, face, level) slice. It returns false on any malformed or unsupported input
    //    (the parser is hardened against corrupt/truncated files).
    basist::dds_transcoder dds;
    if (!dds.init(dds_file_data.data(), (uint32_t)dds_file_data.size()))
    {
        fprintf(stderr, "dds_transcoder::init() failed on %s (unsupported or corrupt DDS)\n", pName);
        return false;
    }

    // 3. start_transcoding() must be called before transcoding. For DDS there are no global tables to unpack
    //    (unlike ETC1S KTX2), so this just verifies init() succeeded -- it exists to mirror ktx2_transcoder.
    if (!dds.start_transcoding())
    {
        fprintf(stderr, "dds_transcoder::start_transcoding() failed\n");
        return false;
    }

    // Geometry accessors (valid after init), mirroring ktx2_transcoder. get_layers() returns 0 when the file
    // is not a texture array (the KTX2 convention), so treat 0 as a single layer when iterating.
    const uint32_t num_levels = dds.get_levels();
    const uint32_t num_layers = dds.get_layers();
    const uint32_t eff_layers = num_layers ? num_layers : 1;
    const uint32_t num_faces = dds.get_faces();
    const bool has_alpha = (dds.get_has_alpha() != 0);

    printf("\nLoaded %s\n", pName);
    printf("  %ux%u, mip levels: %u, array layers: %u, faces: %u, has_alpha: %u, sRGB: %u\n",
        dds.get_width(), dds.get_height(), num_levels, num_layers, num_faces,
        dds.get_has_alpha(), dds.is_srgb() ? 1 : 0);
    // get_format() returns the closest transcoder_texture_format the file's contents map to (i.e. what a
    // passthrough transcode would emit). basis_get_format_name() turns it into a printable string.
    printf("  contained format: %s\n", basist::basis_get_format_name(dds.get_format()));

    // 4. Walk every image (array layer, cubemap face, mip level), print its info, transcode it to RGBA, and
    //    write a .BMP. Most 2D textures have eff_layers==1 and num_faces==1, so this is usually just the mips.
    for (uint32_t layer = 0; layer < eff_layers; layer++)
    {
        for (uint32_t face = 0; face < num_faces; face++)
        {
            for (uint32_t level = 0; level < num_levels; level++)
            {
                // Per-image info (the same struct ktx2_transcoder uses). m_orig_width/height are the real pixel
                // dimensions (may not be a multiple of the 4x4 block size); m_width/height are padded up to it.
                basist::ktx2_image_level_info level_info;
                if (!dds.get_image_level_info(level_info, level, layer, face))
                {
                    fprintf(stderr, "get_image_level_info() failed (level %u, layer %u, face %u)\n", level, layer, face);
                    return false;
                }

                printf("  mip %2u: %ux%u (%u blocks, %ux%u), alpha: %u\n",
                    level, level_info.m_orig_width, level_info.m_orig_height,
                    level_info.m_total_blocks, level_info.m_num_blocks_x, level_info.m_num_blocks_y,
                    level_info.m_alpha_flag);

                // Transcode this mip to 32-bpp RGBA. cTFRGBA32 is an UNCOMPRESSED target, so the output buffer
                // holds one 32-bit pixel per texel (not 4x4 blocks), tightly packed at the image's actual
                // dimensions. image_u8's pixels are color_quad_u8 {r,g,b,a}, which is exactly the RGBA32 byte
                // layout, so we can transcode straight into the image's pixel buffer.
                utils::image_u8 img(level_info.m_orig_width, level_info.m_orig_height);
                const uint32_t num_output_pixels = level_info.m_orig_width * level_info.m_orig_height;

                // For uncompressed targets the "output_blocks_buf_size_in_blocks_or_pixels" argument is a PIXEL
                // count; the transcoder uses it to verify the destination buffer is large enough.
                if (!dds.transcode_image_level(level, layer, face,
                    img.get_pixels().data(), num_output_pixels,
                    basist::transcoder_texture_format::cTFRGBA32))
                {
                    fprintf(stderr, "transcode_image_level() to RGBA32 failed (level %u, layer %u, face %u)\n", level, layer, face);
                    return false;
                }

                // Save the decoded image. 32-bpp BGRA when the source has alpha (e.g. BC7), else 24-bpp BGR.
                char out_filename[300];
                snprintf(out_filename, sizeof(out_filename), "%s_layer%u_face%u_mip%02u.bmp", pOutput_prefix, layer, face, level);
                if (!utils::save_bmp(out_filename, img, has_alpha))
                {
                    fprintf(stderr, "save_bmp(%s) failed\n", out_filename);
                    return false;
                }
                printf("    wrote %s\n", out_filename);
            }
        }
    }

    return true;
}

int main()
{
    basist::basisu_transcoder_init();

    // Read the .KTX2 file's data into memory.
    utils::uint8_vec ktx2_file_data;
    if (!utils::read_file("../test_files/base_xuastc_arith.ktx2", ktx2_file_data))
    {
        if (!utils::read_file("base_xuastc_arith.ktx2", ktx2_file_data))
        {
            fprintf(stderr, "Can't read file ../test_files/base_xuastc_arith.ktx2 or base_xuastc_arith.ktx2\n");
            return EXIT_FAILURE;
        }
    }

    printf("Read file base_xuastc_arith.ktx2\n");

    if (ktx2_file_data.size() > UINT32_MAX)
    {
        fprintf(stderr, "KTX2 file too large\n");
        return EXIT_FAILURE;
    }

    basist::ktx2_transcoder transcoder;

    // Initialize the transcoder.
    if (!transcoder.init(ktx2_file_data.data(), (uint32_t)ktx2_file_data.size()))
        return EXIT_FAILURE;

    const uint32_t width = transcoder.get_width();
    const uint32_t height = transcoder.get_height();
    const uint32_t num_levels = transcoder.get_levels();
    const bool is_srgb = transcoder.is_srgb();

    printf("KTX2 dimensions: %ux%u, num mip levels: %u, sRGB: %u\n", width, height, num_levels, is_srgb);

    // Can't transcode HDR to LDR formats.
    if (transcoder.is_hdr())
    {
        fprintf(stderr, "Expected LDR KTX2 file\n");
        return EXIT_FAILURE;
    }

    // Ensure BC7 support was enabled at compilation time (it will be enabled by default).
    const basist::transcoder_texture_format tex_fmt = basist::transcoder_texture_format::cTFBC7_RGBA;
    if (!basist::basis_is_format_supported(tex_fmt, transcoder.get_basis_tex_format()))
    {
        printf("BC7 was disabled in the transcoder at compilation\n");
        return EXIT_FAILURE;
    }

    // Begin transcoding (this will be a no-op with UASTC HDR textures, but you still need to do it. For ETC1S it'll unpack the global codebooks).
    transcoder.start_transcoding();

    // Transcode to BC7 and write a BC7 .DDS file.
                
    // Bytes per block (8 or 16 for BC1-7)
    const uint32_t bytes_per_block = basist::basis_get_bytes_per_block_or_pixel(tex_fmt);
    // Compute total bytes needed to transcode the slice
    const uint32_t total_bytes = basist::basis_compute_transcoded_image_size_in_bytes(tex_fmt, width, height);
    // Derive the total number of blocks the output buffer can hold. The transcoder will use this to verify the buffer is large enough.
    const uint32_t total_blocks = total_bytes / bytes_per_block;

    // Allocate the buffer to hold the blocks
    utils::uint8_vec tex_buffer(total_bytes);

    // Transcode the level
    bool status = transcoder.transcode_image_level(0, 0, 0,
        tex_buffer.data(), total_blocks,
        tex_fmt, 0);

    if (!status)
    {
        fprintf(stderr, "transcoder.transcode_image_level() failed\n");
        return EXIT_FAILURE;
    }

    // Write an sRGB DX10-style .DDS file.
    if (!utils::save_dds("out.dds", width, height, tex_buffer.data(), 8, DXGI_FORMAT_BC7_UNORM_SRGB, true, true))
    {
        fprintf(stderr, "save_dds() failed\n");
        return EXIT_FAILURE;
    }

    printf("Wrote out.dds\n");

    // --- DDS transcoding example -----------------------------------------------------------------------------
    // Load two example .DDS files made with texconv -- a BC1/DXT1 color texture (kodim01, no alpha) and a BC7
    // texture with alpha (texarray_alpha_0) -- and transcode every mip level of each to RGBA, writing .BMPs.
    if (!transcode_dds_to_bmps("kodim01.dds", "kodim01_bc1"))
        return EXIT_FAILURE;

    if (!transcode_dds_to_bmps("texarray_alpha_0.dds", "texarray_alpha_0_bc7"))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

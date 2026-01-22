// example_transcoding.cpp: Very simple transcoding-only example. Does not depend on the basisu encoder library at all, just basisu_transcoder.cpp.
// You can use AMD Compressonator or Microsoft's DirectXTex tools on github to view the written DX10 .DDS file.
#include <stdlib.h>
#include <stdio.h>

// for testing
//#define BASISD_SUPPORT_XUASTC (0)
//#define BASISD_SUPPORT_KTX2_ZSTD (0)

#include "../transcoder/basisu_transcoder.h"
#include "utils.h"

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

    return EXIT_SUCCESS;
}

// example_capi.c - Plain C API examples
// Compresses a procedurally generated 32bpp 512x512 test image to a XUASTC LDR 8x5 .ktx2 file with mipmaps and writes a .ktx2 file.
// The .ktx2 file is then opened by the transcoder module, examined and unpacked to RGBA 32bpp and ASTC textures which are saved to disk as .tga and .astc files.
// The .tga image files can be viewed by many common image editors/viewers.
// The standard .astc texture files can be unpacked to .PNG using ARM's astcenc tool, using a command line like this: astcenc-avx2.exe -ds transcoded_0_0_0.astc 0.png

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <memory.h>

typedef int BOOL;
#define TRUE (1)
#define FALSE (0)

// Include compressor and transcoder C API definitions
#include "../encoder/basisu_wasm_api.h"
#include "../encoder/basisu_wasm_transcoder_api.h"

// Write a blob of data in memory to a file
int write_blob_to_file(const char* pFilename, const void* pData, size_t len)
{
    assert(pFilename != NULL);
    assert(pData != NULL);

    if (!pFilename || !pData)
        return FALSE;

    FILE* f = fopen(pFilename, "wb");
    if (!f)
        return FALSE;

    /* Write the data */
    size_t written = fwrite(pData, 1, len, f);
    if (written != len)
    {
        fclose(f);
        return FALSE;
    }

    if (fclose(f) != 0)
        return FALSE;

    return TRUE; /* success */
}

// Writes 24/32bpp .TGA image files
int write_tga_image(const char* pFilename, int w, int h, int has_alpha, const uint8_t* pPixelsRGBA)
{
    assert(pFilename != NULL);
    assert(pPixelsRGBA != NULL);
    assert(w > 0);
    assert(h > 0);
    assert((has_alpha == 0) || (has_alpha == 1));

    /* Runtime argument validation */
    if ((!pFilename) || (!pPixelsRGBA) || (w <= 0) || (h <= 0))
        return -1;  // invalid argument

    FILE* pFile = fopen(pFilename, "wb");
    if (!pFile)
        return -2;  // cannot open file

    uint8_t header[18] = { 0 };
    header[2] = 2;                  // uncompressed true-color
    header[12] = (uint8_t)(w & 0xFF);
    header[13] = (uint8_t)((w >> 8) & 0xFF);
    header[14] = (uint8_t)(h & 0xFF);
    header[15] = (uint8_t)((h >> 8) & 0xFF);
    header[16] = has_alpha ? 32 : 24;

    /* Classic TGA: bottom-left origin */
    header[17] = has_alpha ? 8 : 0;

    if (fwrite(header, 1, 18, pFile) != 18) 
    {
        fclose(pFile);
        return -3; // header write failed
    }

    uint64_t bytes_per_pixel = has_alpha ? 4ULL : 3ULL;
    uint64_t pixel_bytes_u64 = (uint64_t)w * (uint64_t)h * bytes_per_pixel;
    size_t   pixel_bytes = (size_t)pixel_bytes_u64;

    if ((uint64_t)pixel_bytes != pixel_bytes_u64)
        return -6; // overflow bogus dimensions

    /* allocate one scanline for BGRA/BGR output */
    size_t row_bytes = (size_t)w * bytes_per_pixel;
    uint8_t* pRow = (uint8_t*)malloc(row_bytes);
    if (!pRow) 
    {
        fclose(pFile);
        return -7; // out of memory
    }

    /* TGA expects rows in bottom-to-top order */
    for (int y = 0; y < h; y++)
    {
        const uint8_t* pSrcRow = pPixelsRGBA + (size_t)(h - 1 - y) * w * bytes_per_pixel;

        /* Convert RGBA->BGRA or RGB->BGR for this row */
        if (has_alpha)
        {
            /* 4 bytes per pixel */
            for (int x = 0; x < w; x++) 
            {
                const uint8_t* s = &pSrcRow[x * 4];
                uint8_t* d = &pRow[x * 4];

                d[0] = s[2];   // B
                d[1] = s[1];   // G
                d[2] = s[0];   // R
                d[3] = s[3];   // A
            }
        }
        else
        {
            /* 3 bytes per pixel */
            for (int x = 0; x < w; x++) 
            {
                const uint8_t* s = &pSrcRow[x * 3];
                uint8_t* d = &pRow[x * 3];

                d[0] = s[2];   // B
                d[1] = s[1];   // G
                d[2] = s[0];   // R
            }
        }

        if (fwrite(pRow, 1, row_bytes, pFile) != row_bytes) 
        {
            free(pRow);
            fclose(pFile);
            return -4; // pixel write failed
        }
    }

    free(pRow);

    if (fclose(pFile) != 0)
        return -5; // close failed

    return 0; // success
}

// Write standard ARM .ASTC format texture files
int write_astc_file(const char* pFilename,
    const void* pBlocks, // pointer to ASTC blocks
    uint32_t block_width, // in texels [4,12]
    uint32_t block_height, // in texels [4,12]
    uint32_t dim_x, // image actual dimension in texels
    uint32_t dim_y) // image actual dimension in texels
{
    assert(pFilename != NULL);
    assert(pBlocks != NULL);
    assert(dim_x > 0);
    assert(dim_y > 0);
    assert((block_width >= 4) && (block_width <= 12));
    assert((block_height >= 4) && (block_height <= 12));
    
    FILE* f = fopen(pFilename, "wb");
    if (!f)
        return 0;

    /* Helper macro for writing single bytes with error check */
#define PUTB(v) do { if (fputc((int)(v), f) == EOF) { fclose(f); return 0; } } while (0)

    /* Magic */
    PUTB(0x13);
    PUTB(0xAB);
    PUTB(0xA1);
    PUTB(0x5C);

    /* Block dimensions: x, y, z = 1 */
    PUTB((uint8_t)block_width);
    PUTB((uint8_t)block_height);
    PUTB(1); /* block depth */

    /* dim_x (24-bit little endian) */
    PUTB((uint8_t)(dim_x & 0xFF));
    PUTB((uint8_t)((dim_x >> 8) & 0xFF));
    PUTB((uint8_t)((dim_x >> 16) & 0xFF));

    /* dim_y (24-bit little endian) */
    PUTB((uint8_t)(dim_y & 0xFF));
    PUTB((uint8_t)((dim_y >> 8) & 0xFF));
    PUTB((uint8_t)((dim_y >> 16) & 0xFF));

    /* dim_z = 1 (24-bit LE) */
    PUTB(1);
    PUTB(0);
    PUTB(0);

    /* Compute block count and total bytes */
    uint32_t num_blocks_x = (dim_x + block_width - 1) / block_width;
    uint32_t num_blocks_y = (dim_y + block_height - 1) / block_height;

    uint64_t total_bytes_u64 =
        (uint64_t)num_blocks_x * (uint64_t)num_blocks_y * 16ULL;

    size_t total_bytes = (size_t)total_bytes_u64;

    if ((uint64_t)total_bytes != total_bytes_u64) 
    {
        fclose(f);
        return 0; /* overflow → fail */
    }

    /* Write block data directly */
    size_t written = fwrite(pBlocks, 1, total_bytes, f);
    if (written != total_bytes) 
    {
        fclose(f); /* still close even if error */
        return 0;
    }

    if (fclose(f) != 0)
        return 0;

    return 1; /* success */

#undef PUTB
}

// Procedurally create a simple test image in memory
uint8_t* create_pretty_rgba_pattern(int w, int h, float q)
{
    if (w <= 0 || h <= 0)
        return NULL;

    uint8_t* pImage = (uint8_t*)malloc((size_t)w * h * 4);
    if (!pImage)
        return NULL;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            /* normalized coordinates 0..1 */
            float fx = (float)x / (float)w;
            float fy = (float)y / (float)h;

            /* --- Extra coordinate warping when q != 0 --- */
            if (q != 0.0f) {
                float warp = sinf((fx + fy) * 10.0f * q);
                fx += 0.15f * q * warp;
                fy += 0.15f * q * sinf((fx - fy) * 8.0f * q);
            }

            /* Original plasma formula */
            float v = sinf(fx * 12.0f + fy * 4.0f);
            v += sinf(fy * 9.0f - fx * 6.0f);
            v += sinf((fx + fy) * 7.0f);

            /* Extra variation term — contributes only when q != 0 */
            if (q != 0.0f) 
            {
                v += q * 0.7f * sinf((fx * fx + fy) * 20.0f);
                v += q * 0.4f * cosf((fx - fy) * 18.0f);
            }

            /* scale to 0..1 */
            v = v * 0.25f + 0.5f;

            float L = 1.5f;

            /* Convert to RGB colors */
            int r = (int)roundf(255.0f * sinf(v * 6.28f) * L);
            int g = (int)roundf(255.0f * (1.0f - v) * L);
            int b = (int)roundf(255.0f * v * L);

            /* clamp */
            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;

            /* write RGBA */
            uint8_t* p = &pImage[(y * w + x) * 4];
            p[0] = (uint8_t)r;
            p[1] = (uint8_t)g;
            p[2] = (uint8_t)b;
            p[3] = 255;
        }
    }

    return pImage;
}

// Takes a KTX2 file in memory and displays info about it, then transcodes it to RGBA32 and ASTC, writing .tga/.astc files to disk
int transcode_ktx2_file(const void* pKTX2_data, size_t ktx2_data_size, const char *pDesc)
{
    printf("------ transcode_ktx2_file(): ktx2 size: %zu, desc: %s\n", ktx2_data_size, pDesc);
     
    if (!pKTX2_data || !ktx2_data_size)
        return FALSE;

    if ((uint32_t)ktx2_data_size != ktx2_data_size)
        return FALSE;

    uint64_t ktx2_data_ofs = bt_alloc(ktx2_data_size);
    if (!ktx2_data_ofs)
        return FALSE;

    memcpy((void*)ktx2_data_ofs, pKTX2_data, ktx2_data_size);

    uint64_t ktx2_handle = bt_ktx2_open(ktx2_data_ofs, (uint32_t)ktx2_data_size);
    if (!ktx2_handle)
    {
        bt_free(ktx2_data_ofs);
        return FALSE;
    }

    // Just testing LDR here for now
    if (!bt_ktx2_is_ldr(ktx2_handle))
    {
        bt_ktx2_close(ktx2_handle);
        bt_free(ktx2_data_ofs);
        return FALSE;
    }

    if (!bt_ktx2_start_transcoding(ktx2_handle))
    {
        bt_ktx2_close(ktx2_handle);
        bt_free(ktx2_data_ofs);
        return FALSE;
    }

    uint32_t width = bt_ktx2_get_width(ktx2_handle), height = bt_ktx2_get_height(ktx2_handle);
    uint32_t levels = bt_ktx2_get_levels(ktx2_handle); // number of mipmap levels, must be >= 1
    uint32_t faces = bt_ktx2_get_faces(ktx2_handle); // 1 or 6
    uint32_t layers = bt_ktx2_get_layers(ktx2_handle); // 0 or array size

    uint32_t basis_tex_format = bt_ktx2_get_basis_tex_format(ktx2_handle);
    uint32_t block_width = bt_ktx2_get_block_width(ktx2_handle);
    uint32_t block_height = bt_ktx2_get_block_height(ktx2_handle);
    uint32_t is_srgb = bt_ktx2_is_srgb(ktx2_handle);
    uint32_t is_video = bt_ktx2_is_video(ktx2_handle); // only reliably set after calling bt_ktx2_start_transcoding()
    
    printf("KTX2 Dimensions: %ux%u, Levels: %u, Faces: %u, Layers: %u\n", width, height, levels, faces, layers);
    printf("basis_tex_format: %u\n", basis_tex_format);
    printf("Block dimensions: %ux%u\n", block_width, block_height);
    printf("is sRGB: %u\n", is_srgb);
    printf("is video: %u\n", is_video);

    assert((width >= 1) && (height >= 1));
    assert(levels >= 1);
    assert((faces == 6) || (faces == 1));

    // If layers==0 it's not a texture array
    if (layers < 1)
        layers = 1;

    // Create our transcoding state handle (which contains thread-local state)
    // This is actually optional, and only needed for thread-safe transcoding, but we'll test it here.
    uint64_t transcode_state_handle = bt_ktx2_create_transcode_state();
       
    for (uint32_t level_index = 0; level_index < levels; level_index++)
    {
        for (uint32_t layer_index = 0; layer_index < layers; layer_index++)
        {
            for (uint32_t face_index = 0; face_index < faces; face_index++)
            {
                printf("- Level: %u, layer: %u, face: %u\n", level_index, layer_index, face_index);

                uint32_t orig_width = bt_ktx2_get_level_orig_width(ktx2_handle, level_index, layer_index, face_index);
                uint32_t orig_height = bt_ktx2_get_level_orig_height(ktx2_handle, level_index, layer_index, face_index);

                printf("  Orig dimensions: %ux%u, actual: %ux%u\n",
                    orig_width, orig_height,
                    bt_ktx2_get_level_actual_width(ktx2_handle, level_index, layer_index, face_index), bt_ktx2_get_level_actual_height(ktx2_handle, level_index, layer_index, face_index));
                
                printf("  Block dimensions: %ux%u, total blocks: %u\n",
                    bt_ktx2_get_level_num_blocks_x(ktx2_handle, level_index, layer_index, face_index),
                    bt_ktx2_get_level_num_blocks_y(ktx2_handle, level_index, layer_index, face_index),
                    bt_ktx2_get_level_total_blocks(ktx2_handle, level_index, layer_index, face_index));

                printf("  Alpha flag: %u, iframe flag: %u\n",
                    bt_ktx2_get_level_alpha_flag(ktx2_handle, level_index, layer_index, face_index),
                    bt_ktx2_get_level_iframe_flag(ktx2_handle, level_index, layer_index, face_index));

                // First transcode level to uncompressed RGBA32 and write a .tga file
                {
                    char tga_filename[256];
                    snprintf(tga_filename, sizeof(tga_filename), "transcoded_%s_L%u_Y%u_F%u.tga", pDesc, level_index, layer_index, face_index);

                    uint32_t transcode_buf_size = bt_basis_compute_transcoded_image_size_in_bytes(TF_RGBA32, orig_width, orig_height);
                    assert(transcode_buf_size);

                    uint64_t transcode_buf_ofs = bt_alloc(transcode_buf_size);

                    uint32_t decode_flags = 0;

                    if (!bt_ktx2_transcode_image_level(ktx2_handle, level_index, layer_index, face_index,
                        transcode_buf_ofs, transcode_buf_size,
                        TF_RGBA32,
                        decode_flags,
                        0, 0, -1, -1, transcode_state_handle))
                    {
                        bt_free(transcode_buf_ofs);
                        bt_ktx2_destroy_transcode_state(transcode_state_handle);
                        bt_ktx2_close(ktx2_handle);
                        bt_free(ktx2_data_ofs);
                        return FALSE;
                    }

                    write_tga_image(tga_filename, orig_width, orig_height, TRUE, (uint8_t*)transcode_buf_ofs);
                    printf("Wrote file %s\n", tga_filename);

                    bt_free(transcode_buf_ofs);
                    transcode_buf_ofs = 0;
                }

                // Now transcode to ASTC and write a .astc file
                {
                    char astc_filename[256];
                    snprintf(astc_filename, sizeof(astc_filename), "transcoded_%s_L%u_Y%u_F%u.astc", pDesc, level_index, layer_index, face_index);

                    // Determine the correct ASTC transcode texture format from the ktx2 format
                    uint32_t target_transcode_fmt = bt_basis_get_transcoder_texture_format_from_basis_tex_format(basis_tex_format);

                    uint32_t transcode_buf_size = bt_basis_compute_transcoded_image_size_in_bytes(target_transcode_fmt, orig_width, orig_height);
                    assert(transcode_buf_size);

                    uint64_t transcode_buf_ofs = bt_alloc(transcode_buf_size);

                    uint32_t decode_flags = 0;

                    if (!bt_ktx2_transcode_image_level(ktx2_handle, level_index, layer_index, face_index,
                        transcode_buf_ofs, transcode_buf_size,
                        target_transcode_fmt,
                        decode_flags,
                        0, 0, -1, -1, transcode_state_handle))
                    {
                        bt_free(transcode_buf_ofs);
                        bt_ktx2_destroy_transcode_state(transcode_state_handle);
                        bt_ktx2_close(ktx2_handle);
                        bt_free(ktx2_data_ofs);
                        return FALSE;
                    }

                    write_astc_file(astc_filename, (void*)transcode_buf_ofs, block_width, block_height, orig_width, orig_height);
                    printf("Wrote .astc file %s\n", astc_filename);

                    bt_free(transcode_buf_ofs);
                    transcode_buf_ofs = 0;
                }

            } // face_index 

        } // layer_index

    } // level_index
    
    bt_ktx2_destroy_transcode_state(transcode_state_handle);
    transcode_state_handle = 0;
    
    bt_ktx2_close(ktx2_handle);
    ktx2_handle = 0;

    bt_free(ktx2_data_ofs);
    ktx2_data_ofs = 0;

    return TRUE;
}

// Simple 2D test
int test_2D()
{
    printf("------ test_2D():\n");

    // Generate a test image
    int W = 512, H = 512;

    uint8_t* pSrc_image = create_pretty_rgba_pattern(W, H, 0.0f);

    // Save the test image to a .tga file
    write_tga_image("test_image.tga", W, H, TRUE, pSrc_image);
    printf("Wrote file test_image.tga\n");

    // Compress it to .ktx2
    uint64_t comp_params = bu_new_comp_params();

    // Allocate memory 
    uint64_t img_ofs = bu_alloc(W * H * 4);
    if (!img_ofs)
    {
        fprintf(stderr, "bu_alloc() failed\n");
        return EXIT_FAILURE;
    }

    // Copy the test image into the allocated memory
    memcpy((void*)img_ofs, pSrc_image, W * H * 4);

    // Supply the image to the compressor - it'll immediately make a copy of the data
    if (!bu_comp_params_set_image_rgba32(comp_params, 0, img_ofs, W, H, W * 4))
    {
        fprintf(stderr, "bu_comp_params_set_image_rgba32() failed\n");
        return EXIT_FAILURE;
    }

    bu_free(img_ofs);
    img_ofs = 0;

    // Now compress it to XUASTC LDR 8x5 with weight grid DCT
    uint32_t basis_tex_format = BTF_XUASTC_LDR_8X5;
    //uint32_t basis_tex_format = BTF_ASTC_LDR_8X5;
    //uint32_t basis_tex_format = BTF_ETC1S;
    //uint32_t basis_tex_format = BTF_UASTC_LDR_4X4;

    uint32_t quality_level = 85;
    uint32_t effort_level = 2;

    uint32_t flags = BU_COMP_FLAGS_KTX2_OUTPUT | BU_COMP_FLAGS_SRGB |
        BU_COMP_FLAGS_THREADED | BU_COMP_FLAGS_GEN_MIPS_CLAMP |
        BU_COMP_FLAGS_PRINT_STATS | BU_COMP_FLAGS_PRINT_STATUS;

    if (!bu_compress_texture(comp_params, basis_tex_format, quality_level, effort_level, flags, 0.0f))
    {
        fprintf(stderr, "bu_compress_texture() failed\n");
        return EXIT_FAILURE;
    }

    // Retrieve the compressed .KTX2 file data
    uint64_t comp_size = bu_comp_params_get_comp_data_size(comp_params);
    if (!comp_size)
    {
        fprintf(stderr, "bu_comp_params_get_comp_data_size() failed\n");
        return EXIT_FAILURE;
    }

    void* pComp_data = (void*)bu_comp_params_get_comp_data_ofs(comp_params);
    if (!pComp_data)
    {
        fprintf(stderr, "bu_comp_params_get_comp_data_ofs() failed\n");
        return EXIT_FAILURE;
    }

    // Write the data to disk
    write_blob_to_file("test.ktx2", pComp_data, comp_size);
    printf("Wrote file test.ktx2\n");

    // Now inspect and transcode the .KTX2 data to png/astc files
    if (!transcode_ktx2_file(pComp_data, comp_size, "2D"))
    {
        fprintf(stderr, "transcode_ktx2_file() failed\n");
        return EXIT_FAILURE;
    }

    bu_delete_comp_params(comp_params);

    free(pSrc_image);
    return EXIT_SUCCESS;
}

// 2D array/texture video test
int test_2D_array(BOOL tex_video_flag, int L, BOOL mipmap_flag)
{
    printf("------ test_2D_array() %i %i %i:\n", tex_video_flag, L, mipmap_flag);

    // Generate a test image
    int W = 256, H = 256;
    
    // Compress it to .ktx2
    uint64_t comp_params = bu_new_comp_params();

    const char* pDesc = tex_video_flag ? "video" : "array";

    char filename_buf[256];

    for (int layer = 0; layer < L; layer++)
    {
        uint8_t* pSrc_image = create_pretty_rgba_pattern(W, H, (float)layer * .05f);

        // Save the test image to a .tga file
        snprintf(filename_buf, sizeof(filename_buf), "test_%s_layer_%u.tga", pDesc, layer);

        write_tga_image(filename_buf, W, H, TRUE, pSrc_image);
        printf("Wrote file %s\n", filename_buf);

        // Allocate memory 
        uint64_t img_ofs = bu_alloc(W * H * 4);
        if (!img_ofs)
        {
            fprintf(stderr, "bu_alloc() failed\n");
            return EXIT_FAILURE;
        }

        // Copy the test image into the allocated memory
        memcpy((void*)img_ofs, pSrc_image, W * H * 4);

        // Supply the image to the compressor - it'll immediately make a copy of the data
        if (!bu_comp_params_set_image_rgba32(comp_params, layer, img_ofs, W, H, W * 4))
        {
            fprintf(stderr, "bu_comp_params_set_image_rgba32() failed\n");
            return EXIT_FAILURE;
        }

        bu_free(img_ofs);
        img_ofs = 0;

        free(pSrc_image);

    } // layer

    // ETC1S has special optimizations for texture video (basic p-frames with skip blocks).
    uint32_t basis_tex_format = tex_video_flag ? BTF_ETC1S : BTF_XUASTC_LDR_4X4;

    uint32_t quality_level = 100;
    uint32_t effort_level = 4;

    uint32_t flags = BU_COMP_FLAGS_KTX2_OUTPUT | BU_COMP_FLAGS_SRGB |
        BU_COMP_FLAGS_THREADED |
        BU_COMP_FLAGS_PRINT_STATS | BU_COMP_FLAGS_PRINT_STATUS;
        
    if (tex_video_flag)
        flags |= BU_COMP_FLAGS_TEXTURE_TYPE_VIDEO_FRAMES;
    else
        flags |= BU_COMP_FLAGS_TEXTURE_TYPE_2D_ARRAY;

    if (mipmap_flag)
        flags |= BU_COMP_FLAGS_GEN_MIPS_CLAMP;

    if (!bu_compress_texture(comp_params, basis_tex_format, quality_level, effort_level, flags, 0.0f))
    {
        fprintf(stderr, "bu_compress_texture() failed\n");
        return EXIT_FAILURE;
    }

    // Retrieve the compressed .KTX2 file data
    uint64_t comp_size = bu_comp_params_get_comp_data_size(comp_params);
    if (!comp_size)
    {
        fprintf(stderr, "bu_comp_params_get_comp_data_size() failed\n");
        return EXIT_FAILURE;
    }

    void* pComp_data = (void*)bu_comp_params_get_comp_data_ofs(comp_params);
    if (!pComp_data)
    {
        fprintf(stderr, "bu_comp_params_get_comp_data_ofs() failed\n");
        return EXIT_FAILURE;
    }

    // Write the data to disk
    snprintf(filename_buf, sizeof(filename_buf), "test_%s.ktx2", pDesc);
    write_blob_to_file(filename_buf, pComp_data, comp_size);
    printf("Wrote file %s\n", filename_buf);

    // Now inspect and transcode the .KTX2 data to png/astc files
    if (!transcode_ktx2_file(pComp_data, comp_size, pDesc))
    {
        fprintf(stderr, "transcode_ktx2_file() failed\n");
        return EXIT_FAILURE;
    }

    bu_delete_comp_params(comp_params);
        
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("example_capi.c:\n");
        
	// Initialize the encoder (which initializers the transcoder for us)
    printf("bu_init:\n");
	bu_init();
	
	// bu_init() already does this for us, but it's harmless to call again.
    printf("bt_init:\n");
	bt_init();

    // Control debug output from the compressor
    bu_enable_debug_printf(FALSE);

    // simple 2D
    if (test_2D() != EXIT_SUCCESS)
    {
        fprintf(stderr, "test_2D() failed!\n");
        return EXIT_FAILURE;
    }

    // 2D array
    if (test_2D_array(FALSE, 8, FALSE) != EXIT_SUCCESS)
    {
        fprintf(stderr, "test_2D_array() (array mode) failed!\n");
        return EXIT_FAILURE;
    }

    // texture video
    if (test_2D_array(TRUE, 8, TRUE) != EXIT_SUCCESS)
    {
        fprintf(stderr, "test_2D_array() (texture video mode) failed!\n");
        return EXIT_FAILURE;
    }

    printf("Success\n");

	return EXIT_SUCCESS;
}



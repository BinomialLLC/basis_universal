// File: example.cpp
// This minimal LDR/HDR encoding/transcoder example relies on encoder_lib. It shows how to use the encoder in a few different ways, and the transcoder.
//
// It should be compiled with the preprocessor macros BASISU_SUPPORT_SSE (typically 1) and BASISU_SUPPORT_OPENCL (typically 1).
// They should be set to the same preprocesor options as the encoder.
// If OpenCL is enabled, the "..\OpenCL" directory should be in your compiler's include path. Additionally, link against "..\OpenCL\lib\opencl64.lib".
#include "../encoder/basisu_comp.h"
#include "../transcoder/basisu_transcoder.h"
#include "../encoder/basisu_gpu_texture.h"

#define USE_ENCODER (1)

const bool USE_OPENCL = false;

// The encoder lives in the "basisu" namespace.
// The transcoder lives entirely in the "basist" namespace.
using namespace basisu;

// Quick function to create a visualization of the Mandelbrot set as an float HDR image.
static void create_mandelbrot(imagef& img)
{
    const int width = 256;
    const int height = 256;
    const int max_iter = 1000;

    // Create a more interesting color palette
    uint8_t palette[256][3];
    for (int i = 0; i < 256; i++)
    {
        if (i < 64)
        {
            // Blue to cyan transition
            palette[i][0] = static_cast<uint8_t>(0);                   // Red component
            palette[i][1] = static_cast<uint8_t>(i * 4);               // Green component
            palette[i][2] = static_cast<uint8_t>(255);                 // Blue component
        }
        else if (i < 128)
        {
            // Cyan to green transition
            palette[i][0] = static_cast<uint8_t>(0);                   // Red component
            palette[i][1] = static_cast<uint8_t>(255);                 // Green component
            palette[i][2] = static_cast<uint8_t>(255 - (i - 64) * 4);  // Blue component
        }
        else if (i < 192)
        {
            // Green to yellow transition
            palette[i][0] = static_cast<uint8_t>((i - 128) * 4);       // Red component
            palette[i][1] = static_cast<uint8_t>(255);                 // Green component
            palette[i][2] = static_cast<uint8_t>(0);                   // Blue component
        }
        else
        {
            // Yellow to red transition
            palette[i][0] = static_cast<uint8_t>(255);                 // Red component
            palette[i][1] = static_cast<uint8_t>(255 - (i - 192) * 4); // Green component
            palette[i][2] = static_cast<uint8_t>(0);                   // Blue component
        }
    }

    // Iterate over each pixel in the image
    for (int px = 0; px < width; px++)
    {
        for (int py = 0; py < height; py++)
        {
            double x0 = (px - width / 2.0) * 4.0 / width;
            double y0 = (py - height / 2.0) * 4.0 / height;
            double zx = 0.0;
            double zy = 0.0;
            double zx_squared = 0.0;
            double zy_squared = 0.0;
            double x_temp;

            int iter;
            for (iter = 0; iter < max_iter; iter++)
            {
                zx_squared = zx * zx;
                zy_squared = zy * zy;

                if (zx_squared + zy_squared > 4.0) break;

                // Update z = z^2 + c, but split into real and imaginary parts
                x_temp = zx_squared - zy_squared + x0;
                zy = 2.0 * zx * zy + y0;
                zx = x_temp;
            }

            // Map the number of iterations to a color in the palette
            int color_idx = iter % 256;

            // Set the pixel color in the image
            img.set_clipped(px, py, vec4F(((float)palette[color_idx][0])/128.0f, ((float)palette[color_idx][1])/128.0f, ((float)palette[color_idx][2])/128.0f));
        }
    }
}

// This LDR example function uses the basis_compress() C-style function to compress a ETC1S .KTX2 file.
static bool encode_etc1s()
{
    const uint32_t W = 512, H = 512;

    image img(W, H);
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
            img(x, y).set(0, y >> 1, x >> 1, ((x ^ y) & 1) ? 255 : 0);

    basisu::vector<image> source_images;
    source_images.push_back(img);

    size_t file_size = 0;
    uint32_t quality_level = 255;

    // basis_compress() is a simple wrapper around the basis_compressor_params and basis_compressor classes.
    void* pKTX2_data = basis_compress(
        basist::basis_tex_format::cETC1S,
        source_images,
        quality_level | cFlagSRGB | cFlagGenMipsClamp | cFlagThreaded | cFlagPrintStats | cFlagDebug | cFlagPrintStatus | cFlagUseOpenCL, 0.0f,
        &file_size,
        nullptr);

    if (!pKTX2_data)
        return false;

    if (!write_data_to_file("test_etc1s.ktx2", pKTX2_data, file_size))
    {
        basis_free_data(pKTX2_data);
        return false;
    }

    basis_free_data(pKTX2_data);

    return true;
}

// This LDR example function uses the basis_compress() C-style function to compress a UASTC LDR .KTX2 file.
static bool encode_uastc_ldr()
{
    const uint32_t W = 512, H = 512;

    image img(W, H);
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
            img(x, y).set(x >> 1, y >> 1, 0, 1);

    basisu::vector<image> source_images;
    source_images.push_back(img);

    size_t file_size = 0;

    // basis_compress() is a simple wrapper around the basis_compressor_params and basis_compressor classes.
    void* pKTX2_data = basis_compress(
        basist::basis_tex_format::cUASTC4x4,
        source_images,
        cFlagThreaded | cFlagPrintStats | cFlagDebug | cFlagPrintStatus, 0.0f,
        &file_size,
        nullptr);

    if (!pKTX2_data)
        return false;

    if (!write_data_to_file("test_uastc_ldr_4x4.ktx2", pKTX2_data, file_size))
    {
        basis_free_data(pKTX2_data);
        return false;
    }

    basis_free_data(pKTX2_data);

    return true;
}

// This HDR example function directly uses the basis_compressor_params and basis_compressor classes to compress to a UASTC HDR .KTX2 file.
// These classes expose all encoder functionality (the C-style wrappers used above don't).
static bool encode_uastc_hdr()
{
	const uint32_t W = 256, H = 256;

    imagef img(W, H);

#if 1
    create_mandelbrot(img);
#else
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
            img(x, y).set(((x ^ y) & 1) ? basist::ASTC_HDR_MAX_VAL : 1000.0f);
#endif

	basis_compressor_params params;
	params.m_hdr = true;
	params.m_source_images_hdr.push_back(img);
	params.m_uastc_hdr_4x4_options.set_quality_level(3);
	params.m_debug = true;
	//params.m_debug_images = true;
	params.m_status_output = true;
	params.m_compute_stats = true;
	params.m_create_ktx2_file = true;
	params.m_write_output_basis_or_ktx2_files = true;
	params.m_out_filename = "test_uastc_hdr.ktx2";
	params.m_perceptual = true;

#if 1
    // Create a job pool containing 7 total threads (the calling thread plus 6 additional threads).
    // A job pool must be created, even if threading is disabled. It's fine to pass in 0 for NUM_THREADS.
	const uint32_t NUM_THREADS = 6;
	job_pool jp(NUM_THREADS);
	params.m_pJob_pool = &jp;
	params.m_multithreading = true;
#else
    // No threading
    const uint32_t NUM_THREADS = 1;
    job_pool jp(NUM_THREADS);
    params.m_pJob_pool = &jp;
    params.m_multithreading = false;
#endif

	basis_compressor comp;
	if (!comp.init(params))
		return false;

	basisu::basis_compressor::error_code ec = comp.process();
	if (ec != basisu::basis_compressor::cECSuccess)
		return false;

	return true;
}

// This example function loads a .KTX2 file and then transcodes it to various compressed/uncompressed texture formats.
// It writes .DDS and .ASTC files.
// ARM's astcenc tool can be used to unpack the .ASTC file:
// astcenc-avx2.exe -dh test_uastc_hdr_astc.astc out.exr
static bool transcode_hdr()
{
    // Note: The encoder already initializes the transcoder, but if you haven't initialized the encoder you MUST call this function to initialize the transcoder.
    basist::basisu_transcoder_init();

    // Read the .KTX2 file's data into memory.
    uint8_vec ktx2_file_data;
    if (!read_file_to_vec("test_uastc_hdr.ktx2", ktx2_file_data))
        return false;

    // Create the KTX2 transcoder object.
    basist::ktx2_transcoder transcoder;

    // Initialize the transcoder.
    if (!transcoder.init(ktx2_file_data.data(), ktx2_file_data.size_u32()))
        return false;

    const uint32_t width = transcoder.get_width();
    const uint32_t height = transcoder.get_height();

    printf("Texture dimensions: %ux%u, levels: %u\n", width, height, transcoder.get_levels());

    // This example only transcodes UASTC HDR textures.
    if (!transcoder.is_hdr())
        return false;

    // Begin transcoding (this will be a no-op with UASTC HDR textures, but you still need to do it. For ETC1S it'll unpack the global codebooks.)
    transcoder.start_transcoding();

    // Transcode to BC6H and write a BC6H .DDS file.
    {
        gpu_image tex(texture_format::cBC6HUnsigned, width, height);

        bool status = transcoder.transcode_image_level(0, 0, 0,
            tex.get_ptr(), tex.get_total_blocks(),
            basist::transcoder_texture_format::cTFBC6H, 0);
        if (!status)
            return false;

        gpu_image_vec tex_vec;
        tex_vec.push_back(tex);
        if (!write_compressed_texture_file("test_uastc_hdr_bc6h.dds", tex_vec, true))
            return false;
    }

    // Transcode to ASTC HDR 4x4 and write a ASTC 4x4 HDR .astc file.
    {
        gpu_image tex(texture_format::cASTC_HDR_4x4, width, height);

        bool status = transcoder.transcode_image_level(0, 0, 0,
            tex.get_ptr(), tex.get_total_blocks(),
            basist::transcoder_texture_format::cTFASTC_HDR_4x4_RGBA, 0);
        if (!status)
            return false;

        if (!write_astc_file("test_uastc_hdr_astc.astc", tex.get_ptr(), 4, 4, tex.get_pixel_width(), tex.get_pixel_height()))
            return false;
    }

    // Transcode to RGBA HALF and write an .EXR file.
    {
        basisu::vector<uint16_t> half_img(width * 4 * height);

        bool status = transcoder.transcode_image_level(0, 0, 0,
            half_img.get_ptr(), half_img.size_u32() / 4,
            basist::transcoder_texture_format::cTFRGBA_HALF, 0);
        if (!status)
            return false;

        // Convert FP16 (half float) image to 32-bit float
        imagef float_img(transcoder.get_width(), transcoder.get_height());
        for (uint32_t y = 0; y < transcoder.get_height(); y++)
        {
            for (uint32_t x = 0; x < transcoder.get_height(); x++)
            {
                float_img(x, y).set(
                    basist::half_to_float(half_img[(x + y * width) * 4 + 0]),
                    basist::half_to_float(half_img[(x + y * width) * 4 + 1]),
                    basist::half_to_float(half_img[(x + y * width) * 4 + 2]),
                    1.0f);
            }
        }

        if (!write_exr("test_uastc_hdr_rgba_half.exr", float_img, 3, 0))
            return false;
    }

    return true;
}

// These ASTC HDR/BC6H blocks are from the UASTC HDR spec:
// https://github.com/BinomialLLC/basis_universal/wiki/UASTC-HDR-Texture-Specification
static const uint8_t g_test_blocks[][16] =
{
    { 252, 255, 255, 255, 255, 255, 255, 255, 118, 19, 118, 19, 118, 19, 0, 60 },   // ASTC HDR
    { 207, 5, 23, 92, 0, 10, 40, 160, 0, 0, 0, 0, 0, 0, 0, 0 },                     // BC6H
    { 252, 255, 255, 255, 255, 255, 255, 255, 0, 60, 0, 60, 0, 60, 0, 60 },
    { 239, 251, 239, 191, 7, 15, 60, 240, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 81, 224, 44, 65, 64, 144, 1, 0, 0, 0, 0, 0, 0, 196, 0, 0 },
    { 3, 18, 72, 32, 241, 202, 43, 175, 0, 0, 0, 0, 0, 0, 143, 0 },
    { 81, 224, 30, 1, 192, 158, 1, 0, 0, 0, 0, 0, 64, 126, 126, 6 },
    { 3, 0, 0, 0, 152, 102, 154, 105, 0, 0, 255, 255, 255, 255, 255, 255 },
    { 66, 224, 12, 85, 210, 123, 1, 0, 0, 0, 0, 0, 39, 39, 39, 39 },
    { 3, 33, 131, 30, 82, 46, 185, 233, 80, 250, 80, 250, 80, 250, 80, 250 },
    { 66, 224, 58, 1, 128, 58, 1, 0, 0, 0, 0, 0, 208, 65, 0, 65 },
    { 35, 148, 80, 66, 1, 0, 0, 0, 250, 95, 255, 255, 245, 95, 80, 255 },
    { 82, 224, 152, 37, 166, 3, 1, 0, 0, 0, 0, 176, 80, 50, 166, 219 },
    { 235, 189, 251, 24, 197, 23, 95, 124, 73, 72, 139, 139, 139, 136, 143, 184 },
    { 82, 224, 166, 45, 176, 3, 1, 0, 0, 0, 0, 40, 76, 72, 19, 0 },
    { 235, 62, 4, 133, 77, 80, 65, 3, 1, 0, 7, 75, 7, 7, 11, 119 },
    { 67, 224, 46, 65, 64, 244, 1, 0, 0, 0, 128, 84, 33, 130, 75, 74 },
    { 227, 139, 47, 190, 0, 11, 44, 176, 54, 63, 3, 111, 3, 111, 51, 63 },
    { 67, 224, 88, 196, 10, 48, 0, 0, 0, 0, 64, 216, 11, 111, 113, 173 },
    { 139, 80, 64, 243, 116, 214, 217, 103, 157, 153, 150, 153, 150, 153, 150, 153 },
    { 83, 224, 2, 128, 128, 40, 1, 0, 0, 0, 118, 163, 46, 204, 20, 183 },
    { 108, 173, 181, 214, 162, 136, 2, 138, 40, 0, 168, 177, 97, 150, 106, 218 },
    { 83, 224, 120, 64, 0, 48, 1, 0, 0, 0, 36, 73, 146, 35, 57, 146 },
    { 160, 150, 90, 106, 113, 192, 113, 23, 64, 23, 148, 56, 137, 147, 36, 73 },
    { 65, 226, 76, 64, 128, 38, 1, 0, 0, 248, 239, 191, 255, 254, 251, 111 },
    { 107, 247, 221, 119, 71, 1, 5, 20, 170, 170, 170, 170, 170, 170, 170, 170 },
    { 65, 226, 76, 64, 128, 38, 1, 0, 0, 248, 239, 191, 255, 254, 219, 239 },
    { 107, 252, 241, 199, 199, 6, 27, 108, 90, 165, 85, 85, 85, 85, 85, 85 },
    { 81, 226, 92, 67, 132, 166, 1, 0, 128, 150, 161, 218, 172, 106, 165, 186 },
    { 35, 55, 220, 110, 3, 231, 27, 111, 18, 226, 17, 17, 18, 17, 79, 17 },
    { 81, 226, 90, 64, 128, 172, 1, 0, 128, 116, 171, 219, 229, 106, 223, 154 },
    { 7, 63, 252, 240, 67, 13, 53, 212, 20, 84, 18, 34, 33, 17, 18, 226 },
    { 66, 226, 100, 1, 128, 152, 0, 0, 216, 238, 190, 222, 216, 222, 216, 222 },
    { 103, 173, 181, 214, 34, 139, 44, 178, 136, 228, 132, 228, 132, 130, 136, 228 },
    { 66, 226, 36, 1, 128, 44, 1, 0, 125, 221, 0, 13, 215, 125, 221, 0 },
    { 3, 0, 0, 0, 160, 132, 18, 74, 0, 187, 190, 235, 176, 0, 187, 190 },
    { 81, 96, 199, 142, 204, 34, 92, 47, 1, 0, 0, 0, 64, 86, 115, 126 },
    { 131, 164, 34, 118, 177, 108, 180, 188, 0, 0, 0, 0, 112, 0, 255, 0 },
    { 81, 96, 47, 9, 124, 112, 126, 254, 0, 0, 0, 0, 64, 122, 134, 129 },
    { 163, 166, 90, 134, 105, 105, 133, 93, 254, 255, 119, 255, 15, 0, 15, 0 },
    { 66, 96, 247, 184, 16, 185, 130, 83, 1, 0, 0, 0, 0, 85, 255, 255 },
    { 35, 175, 188, 160, 202, 47, 70, 11, 1, 0, 0, 0, 85, 85, 255, 255 },
    { 66, 96, 1, 201, 28, 213, 136, 99, 1, 0, 0, 0, 255, 170, 0, 0 },
    { 3, 66, 36, 99, 212, 108, 54, 201, 0, 0, 0, 0, 85, 85, 255, 255 },
    { 82, 96, 9, 211, 16, 199, 126, 81, 1, 0, 0, 100, 167, 135, 73, 118 },
    { 195, 195, 24, 13, 132, 205, 50, 165, 64, 255, 64, 255, 64, 255, 64, 255 },
    { 82, 96, 191, 138, 41, 202, 122, 120, 0, 0, 0, 248, 243, 26, 253, 219 },
    { 11, 234, 82, 17, 136, 238, 61, 252, 72, 184, 4, 248, 132, 68, 64, 68 },
    { 67, 96, 193, 134, 37, 188, 0, 8, 0, 0, 64, 230, 249, 209, 109, 164 },
    { 75, 107, 97, 157, 8, 111, 60, 225, 156, 207, 105, 3, 57, 198, 6, 147 },
    { 67, 96, 245, 43, 102, 246, 107, 32, 0, 0, 64, 170, 2, 15, 85, 148 },
    { 75, 68, 220, 76, 122, 182, 221, 121, 97, 207, 96, 207, 144, 207, 96, 156 },
    { 83, 96, 39, 144, 13, 174, 126, 122, 0, 0, 59, 245, 171, 166, 2, 8 },
    { 78, 162, 134, 118, 73, 238, 0, 195, 18, 0, 160, 159, 50, 43, 64, 65 },
    { 83, 96, 251, 132, 172, 38, 1, 85, 0, 0, 159, 228, 212, 139, 251, 80 },
    { 106, 41, 211, 12, 147, 102, 2, 150, 5, 0, 152, 161, 91, 214, 81, 10 },
    { 65, 98, 91, 63, 178, 78, 59, 69, 0, 228, 51, 44, 243, 217, 170, 203 },
    { 235, 156, 207, 166, 82, 46, 184, 219, 52, 50, 51, 86, 32, 3, 207, 102 },
    { 65, 98, 229, 178, 100, 164, 81, 180, 0, 96, 5, 44, 129, 46, 232, 51 },
    { 43, 220, 52, 123, 162, 145, 73, 19, 49, 201, 32, 250, 32, 252, 32, 252 },
    { 81, 98, 247, 16, 234, 94, 61, 125, 128, 59, 245, 206, 170, 72, 122, 66 },
    { 75, 8, 148, 158, 73, 168, 162, 132, 24, 149, 17, 225, 246, 154, 214, 171 },
    { 81, 98, 79, 241, 45, 197, 14, 98, 128, 11, 208, 6, 112, 1, 112, 0 },
    { 39, 222, 90, 145, 164, 67, 16, 42, 0, 245, 0, 182, 0, 149, 0, 164 },
    { 66, 98, 89, 167, 60, 234, 94, 65, 123, 119, 247, 183, 255, 219, 234, 12 },
    { 39, 165, 26, 90, 63, 179, 76, 66, 48, 87, 219, 255, 237, 239, 238, 222 },
    { 66, 98, 77, 232, 12, 46, 2, 95, 242, 238, 122, 110, 25, 106, 5, 82 },
    { 199, 170, 148, 188, 199, 122, 232, 173, 186, 95, 169, 103, 137, 161, 136, 176 },
    { 81, 40, 2, 78, 90, 161, 75, 48, 58, 97, 43, 16, 0, 195, 3, 97 },
    { 170, 235, 154, 215, 109, 145, 1, 174, 90, 186, 177, 127, 255, 79, 224, 39 },
    { 81, 8, 2, 46, 93, 129, 76, 241, 95, 193, 236, 16, 128, 202, 121, 21 },
    { 242, 111, 189, 217, 36, 112, 152, 33, 241, 89, 128, 143, 248, 142, 239, 248 },
    { 66, 232, 4, 174, 190, 161, 173, 48, 251, 160, 203, 16, 216, 255, 170, 0 },
    { 146, 13, 52, 186, 26, 152, 252, 225, 158, 232, 1, 64, 146, 254, 255, 21 },
    { 66, 104, 13, 174, 130, 80, 21, 41, 66, 176, 20, 9, 32, 8, 165, 127 },
    { 178, 210, 201, 221, 198, 21, 23, 252, 120, 194, 8, 188, 109, 15, 1, 2 },
    { 82, 232, 4, 46, 216, 200, 214, 83, 40, 79, 5, 128, 243, 158, 1, 0 },
    { 193, 54, 154, 92, 16, 80, 80, 161, 146, 229, 1, 0, 0, 222, 246, 5 },
    { 82, 200, 9, 206, 97, 38, 77, 110, 141, 73, 21, 229, 237, 31, 22, 104 },
    { 1, 10, 33, 112, 217, 111, 175, 93, 147, 195, 129, 125, 235, 37, 64, 18 },
    { 67, 136, 85, 238, 154, 126, 225, 184, 235, 87, 132, 97, 75, 229, 150, 178 },
    { 221, 218, 108, 171, 230, 159, 15, 254, 129, 56, 15, 0, 25, 55, 255, 49 },
    { 67, 40, 2, 110, 61, 154, 128, 205, 39, 140, 70, 191, 16, 239, 182, 190 },
    { 161, 216, 160, 113, 144, 107, 174, 217, 38, 161, 189, 13, 25, 71, 31, 217 },
    { 83, 136, 3, 78, 242, 175, 250, 9, 242, 245, 156, 170, 177, 10, 107, 115 },
    { 117, 153, 228, 108, 190, 209, 238, 251, 211, 23, 228, 77, 166, 100, 75, 117 },
    { 83, 200, 9, 110, 6, 104, 61, 242, 111, 61, 255, 103, 203, 18, 221, 214 },
    { 189, 198, 90, 97, 54, 216, 40, 3, 255, 219, 221, 150, 110, 89, 50, 0 },
    { 81, 40, 2, 150, 184, 130, 106, 248, 236, 2, 64, 134, 65, 248, 0, 114 },
    { 1, 23, 28, 96, 223, 25, 151, 27, 28, 163, 1, 224, 255, 255, 31, 0 },
    { 81, 136, 2, 22, 131, 211, 10, 0, 96, 65, 98, 31, 74, 35, 184, 166 },
    { 2, 219, 67, 75, 204, 42, 129, 4, 3, 44, 188, 31, 251, 129, 239, 24 },
    { 66, 40, 2, 22, 229, 136, 130, 104, 69, 64, 136, 8, 247, 130, 0, 95 },
    { 225, 182, 27, 94, 239, 61, 159, 123, 30, 164, 41, 224, 255, 251, 23, 16 },
    { 66, 136, 31, 118, 66, 50, 19, 104, 66, 58, 214, 16, 229, 93, 222, 252 },
    { 162, 220, 87, 223, 220, 206, 8, 208, 128, 61, 2, 14, 161, 18, 132, 74 }
};
const uint32_t NUM_TEST_BLOCKS = (sizeof(g_test_blocks) / sizeof(g_test_blocks[0])) / 2;

static bool block_unpack_and_transcode_example(void)
{
    printf("block_unpack_and_transcode_example:\n");

    for (uint32_t test_block_iter = 0; test_block_iter < NUM_TEST_BLOCKS; test_block_iter++)
    {
        printf("-- Test block %u:\n", test_block_iter);

        const uint8_t* pASTC_blk = &g_test_blocks[test_block_iter * 2 + 0][0];
        const uint8_t* pBC6H_blk = &g_test_blocks[test_block_iter * 2 + 1][0];

        // Unpack the physical ASTC block to logical.
        // Note this is a full ASTC block unpack, and is not specific to UASTC. It does not verify that the block follows the UASTC HDR spec, only ASTC.
        astc_helpers::log_astc_block log_blk;
        bool status = astc_helpers::unpack_block(pASTC_blk, log_blk, 4, 4);
        assert(status);
        if (!status)
        {
            fprintf(stderr, "Could not unpack ASTC HDR block!\n");
            return false;
        }

        // Print out basic block configuration.
        printf("Solid color: %u\n", log_blk.m_solid_color_flag_hdr);
        if (!log_blk.m_solid_color_flag_hdr)
        {
            printf("Num partitions: %u\n", log_blk.m_num_partitions);
            printf("CEMs: %u %u\n", log_blk.m_color_endpoint_modes[0], log_blk.m_color_endpoint_modes[1]);
            printf("Weight ISE range: %u\n", log_blk.m_weight_ise_range);
            printf("Endpoint ISE range: %u\n", log_blk.m_endpoint_ise_range);
        }

        // Try to transcode this block to BC6H. This will fail if the block is not UASTC HDR.
        basist::bc6h_block transcoded_bc6h_blk;
        status = basist::astc_hdr_transcode_to_bc6h(*(const basist::astc_blk*)pASTC_blk, transcoded_bc6h_blk);
        if (!status)
            printf("!");
        assert(status);

        // Make sure our transcoded BC6H block matches the unexpected block from the UASTC HDR spec.
        if (memcmp(&transcoded_bc6h_blk, pBC6H_blk, 16) == 0)
        {
            printf("Block transcoded OK\n");
        }
        else
        {
            fprintf(stderr, "Block did NOT transcode as expected\n");
            return false;
        }
    } // test_block_iter

    printf("Transcode test OK\n");

    return true;
}

static void fuzz_uastc_hdr_transcoder_test()
{
    printf("fuzz_uastc_hdr_transcoder_test:\n");

    basisu::rand rg;
    rg.seed(2000);

#ifdef __SANITIZE_ADDRESS__
    const uint32_t NUM_TRIES = 100000000;
#else
    const uint32_t NUM_TRIES = 2000000;
#endif

    for (uint32_t t = 0; t < NUM_TRIES; t++)
    {
        basist::astc_blk astc_blk;

        if (rg.frand(0.0f, 1.0f) < .3f)
        {
            // Fully random block
            for (uint32_t k = 0; k < 16; k++)
                ((uint8_t*)&astc_blk)[k] = rg.byte();
        }
        else
        {
            // Take a UASTC HDR block and corrupt it
            uint32_t test_block_index = rg.irand(0, NUM_TEST_BLOCKS - 1);

            const uint8_t* pGood_ASTC_blk = &g_test_blocks[test_block_index * 2 + 0][0];
            memcpy(&astc_blk, pGood_ASTC_blk, 16);

            const uint32_t num_regions = rg.irand(1, 3);
            for (uint32_t k = 0; k < num_regions; k++)
            {
                if (rg.bit())
                {
                    // Flip a set of random bits
                    const uint32_t bit_index = rg.irand(0, 127);
                    const uint32_t num_bits = rg.irand(1, 128 - 127);
                    assert((bit_index + num_bits) <= 128);

                    for (uint32_t i = 0; i < num_bits; i++)
                    {
                        uint32_t bit_ofs = bit_index + i;
                        assert(bit_ofs < 128);

                        uint32_t bit_mask = 1 << (bit_ofs & 7);
                        uint32_t byte_ofs = bit_ofs >> 3;
                        assert(byte_ofs < 16);

                        ((uint8_t*)&astc_blk)[byte_ofs] ^= bit_mask;
                    }
                }
                else
                {
                    // Set some bits to random values
                    const uint32_t bit_index = rg.irand(0, 127);
                    const uint32_t num_bits = rg.irand(1, 128 - 127);
                    assert((bit_index + num_bits) <= 128);

                    for (uint32_t i = 0; i < num_bits; i++)
                    {
                        uint32_t bit_ofs = bit_index + i;
                        assert(bit_ofs < 128);

                        uint32_t bit_mask = 1 << (bit_ofs & 7);
                        uint32_t byte_ofs = bit_ofs >> 3;
                        assert(byte_ofs < 16);

                        ((uint8_t*)&astc_blk)[byte_ofs] &= ~bit_mask;

                        if (rg.bit())
                            ((uint8_t*)&astc_blk)[byte_ofs] |= bit_mask;
                    }
                }
            } // k
        }

        basist::bc6h_block bc6h_blk;
        bool status = basist::astc_hdr_transcode_to_bc6h(astc_blk, bc6h_blk);

        if (!(t % 100000))
            printf("%u %u\n", t, status);
    }

    printf("OK\n");
}

enum class codec_class
{
    cETC1S = 0,
    cUASTC_LDR_4x4 = 1,
    cUASTC_HDR_4x4 = 2,
    cASTC_HDR_6x6 = 3,
    cUASTC_HDR_6x6 = 4,
    cTOTAL
};

// The main point of this test is to exercise lots of internal compressor code paths, and transcoder code paths.
bool random_compression_fuzz_test()
{
    printf("Random XUASTC/ASTC LDR 4x4-12x12 compression test:\n");

    //const uint32_t N = 256;
    const uint32_t N = 64;
    const uint32_t MAX_WIDTH = 1024, MAX_HEIGHT = 1024;

    basisu::rand rnd;

    float lowest_psnr1 = BIG_FLOAT_VAL, lowest_psnr2 = BIG_FLOAT_VAL;

    struct result
    {
        uint32_t m_seed;
        basist::basis_tex_format m_fmt;
        float m_psnr1;
        float m_psnr2;
    };
    basisu::vector<result> results;

    for (uint32_t i = 0; i < N; i++)
    {
        uint32_t seed = 0x2603455 + i;

        //seed = 23082246; // ETC1S perceptual colorspace error overflow test

        fmt_printf("------------------------------ Seed: {}\n", seed);
        rnd.seed(seed);

        const uint32_t w = rnd.irand(1, MAX_WIDTH);
        const uint32_t h = rnd.irand(1, MAX_HEIGHT);
        const bool mips = rnd.bit();
        const bool use_a = rnd.bit();

        fmt_printf("Trying {}x{}, mips: {}, use_a: {}\n", w, h, mips, use_a);

        // Chose a random codec/block size to test
        basist::basis_tex_format tex_mode = basist::basis_tex_format::cETC1S;

        bool is_hdr = false;

        uint32_t rnd_codec_class = rnd.irand(0, (uint32_t)codec_class::cTOTAL - 1);

        //rnd_codec_class = (uint32_t)codec_class::cETC1S;

        switch (rnd_codec_class)
        {
        case (uint32_t)codec_class::cETC1S:
        {
            tex_mode = basist::basis_tex_format::cETC1S;
            break;
        }
        case (uint32_t)codec_class::cUASTC_LDR_4x4:
        {
            tex_mode = basist::basis_tex_format::cUASTC4x4;
            break;
        }
        case (uint32_t)codec_class::cUASTC_HDR_4x4:
        {
            tex_mode = basist::basis_tex_format::cUASTC_HDR_4x4;
            is_hdr = true;
            break;
        }
        case (uint32_t)codec_class::cASTC_HDR_6x6:
        {
            tex_mode = basist::basis_tex_format::cASTC_HDR_6x6;
            is_hdr = true;
            break;
        }
        case (uint32_t)codec_class::cUASTC_HDR_6x6:
        {
            tex_mode = basist::basis_tex_format::cASTC_HDR_6x6_INTERMEDIATE;
            is_hdr = true;
            break;
        }
        default:
            assert(0);
            tex_mode = basist::basis_tex_format::cETC1S;
            break;
        }

        fmt_printf("Testing basis_tex_format={}\n", (uint32_t)tex_mode);

        size_t comp_size = 0;

        // Create random LDR source image to compress
        image src_img;
        src_img.resize(w, h, w, color_rgba(rnd.byte(), rnd.byte(), rnd.byte(), use_a ? rnd.byte() : 255));

        if (rnd.irand(0, 7) >= 1)
        {
            const uint32_t nt = rnd.irand(0, 1000);

            for (uint32_t k = 0; k < nt; k++)
            {
                color_rgba c(rnd.byte(), rnd.byte(), rnd.byte(), use_a ? rnd.byte() : 255);

                uint32_t r = rnd.irand(0, 25);
                if (r == 0)
                {
                    uint32_t xs = rnd.irand(0, w - 1);
                    uint32_t xe = rnd.irand(0, w - 1);
                    if (xs > xe)
                        std::swap(xs, xe);

                    uint32_t ys = rnd.irand(0, h - 1);
                    uint32_t ye = rnd.irand(0, h - 1);
                    if (ys > ye)
                        std::swap(ys, ye);

                    src_img.fill_box(xs, ys, xe - xs + 1, ye - ys + 1, c);
                }
                else if (r <= 5)
                {
                    uint32_t xs = rnd.irand(0, w - 1);
                    uint32_t xe = rnd.irand(0, w - 1);

                    uint32_t ys = rnd.irand(0, h - 1);
                    uint32_t ye = rnd.irand(0, h - 1);

                    basisu::draw_line(src_img, xs, ys, xe, ye, c);
                }
                else if (r == 6)
                {
                    uint32_t cx = rnd.irand(0, w - 1);
                    uint32_t cy = rnd.irand(0, h - 1);
                    uint32_t ra = rnd.irand(0, 100);

                    basisu::draw_circle(src_img, cx, cy, ra, c);
                }
                else if (r < 10)
                {
                    uint32_t x = rnd.irand(0, w - 1);
                    uint32_t y = rnd.irand(0, h - 1);
                    uint32_t sx = rnd.irand(1, 3);
                    uint32_t sy = rnd.irand(1, 3);

                    uint32_t l = rnd.irand(1, 10);

                    char buf[32] = {};
                    for (uint32_t j = 0; j < l; j++)
                        buf[j] = (char)rnd.irand(32, 127);

                    src_img.debug_text(x, y, sx, sy, c, nullptr, rnd.bit(), "%s", buf);
                }
                else if (r < 12)
                {
                    uint32_t xs = rnd.irand(0, w - 1);
                    uint32_t ys = rnd.irand(0, h - 1);

                    uint32_t xl = rnd.irand(1, 100);
                    uint32_t yl = rnd.irand(1, 100);

                    uint32_t xe = minimum<int>(xs + xl - 1, w - 1);
                    uint32_t ye = minimum<int>(ys + yl - 1, h - 1);

                    color_rgba cols[4];
                    cols[0] = c;
                    for (uint32_t j = 1; j < 4; j++)
                        cols[j] = color_rgba(rnd.byte(), rnd.byte(), rnd.byte(), use_a ? rnd.byte() : 255);

                    const bool a_only = rnd.bit();
                    const bool rgb_only = rnd.bit();
                    const bool noise_flag = rnd.irand(0, 9) == 0;

                    for (uint32_t y = ys; y <= ye; y++)
                    {
                        float fy = (ye != ys) ? (float(y - ys) / float(ye - ys)) : 0;

                        for (uint32_t x = xs; x <= xe; x++)
                        {
                            float fx = (xe != xs) ? (float(x - xs) / float(xe - xs)) : 0;

                            color_rgba q;
                            if (noise_flag)
                            {
                                for (uint32_t j = 0; j < 4; j++)
                                    q[j] = rnd.byte();
                            }
                            else
                            {
                                for (uint32_t j = 0; j < 4; j++)
                                {
                                    float lx0 = lerp((float)cols[0][j], (float)cols[1][j], fx);
                                    float lx1 = lerp((float)cols[2][j], (float)cols[3][j], fx);

                                    int ly = (int)std::round(lerp(lx0, lx1, fy));

                                    q[j] = (uint8_t)clamp(ly, 0, 255);
                                }
                            }

                            if (a_only)
                                src_img(x, y).a = q.a;
                            else if (rgb_only)
                            {
                                src_img(x, y).r = q.r;
                                src_img(x, y).g = q.g;
                                src_img(x, y).b = q.b;
                            }
                            else
                                src_img(x, y) = q;
                        } // x
                    } // y
                }
                else
                {
                    src_img(rnd.irand(0, w - 1), rnd.irand(0, h - 1)) = c;
                }
            }
        }

        if ((use_a) && (rnd.irand(0, 3) >= 2))
        {
            const uint32_t nt = rnd.irand(0, 1000);

            for (uint32_t k = 0; k < nt; k++)
                src_img(rnd.irand(0, w - 1), rnd.irand(0, h - 1)).a = rnd.byte();
        }

        if (!use_a)
        {
            for (uint32_t y = 0; y < h; y++)
                for (uint32_t x = 0; x < w; x++)
                    src_img(x, y).a = 255;
        }

        //save_png("test.png", src_img);
        //fmt_printf("Has alpha: {}\n", src_img.has_alpha());

        // Choose randomized codec parameters
        uint32_t flags = cFlagPrintStats | cFlagValidateOutput | cFlagPrintStatus;

        //flags |= cFlagDebug;

        flags |= cFlagThreaded;

        if (rnd.bit())
            flags |= cFlagSRGB;

        if (rnd.bit())
            flags |= cFlagKTX2;

        if (mips)
            flags |= (rnd.bit() ? cFlagGenMipsClamp : cFlagGenMipsWrap);

        if (rnd.bit())
            flags |= cFlagREC2020;

        float quality = 0.0f;

        switch (rnd_codec_class)
        {
        case (uint32_t)codec_class::cETC1S:
        {
            // ETC1S

            // Choose random ETC1S quality level
            flags |= rnd.irand(1, 255);

            break;
        }
        case (uint32_t)codec_class::cUASTC_LDR_4x4:
        {
            // UASTC LDR 4x4

            if (rnd.bit())
            {
                // Choose random RDO lambda
                quality = rnd.frand(0.0, 10.0f);
                flags |= cFlagUASTCRDO;
            }

            // Choose random effort level
            flags |= rnd.irand(cPackUASTCLevelFastest, cPackUASTCLevelVerySlow);

            break;
        }
        case (uint32_t)codec_class::cUASTC_HDR_4x4:
        {
            // UASTC HDR 4x4

            // Choose random effort level.
            flags |= rnd.irand(uastc_hdr_4x4_codec_options::cMinLevel, uastc_hdr_4x4_codec_options::cMaxLevel);

            break;
        }
        case (uint32_t)codec_class::cASTC_HDR_6x6:
        case (uint32_t)codec_class::cUASTC_HDR_6x6:
        {
            // RDO ASTC HDR 6x6 or UASTC HDR 6x6

            // Chose random effort level
            flags |= rnd.irand(0, astc_6x6_hdr::ASTC_HDR_6X6_MAX_USER_COMP_LEVEL);

            if (rnd.bit())
            {
                // Random RDO lambda
                quality = rnd.frand(0.0, 2000.0f);
            }

            break;
        }
        default:
        {
            assert(0);
        }
        }

        void* pComp_data = nullptr;
        image_stats stats;

        if (is_hdr)
        {
            basisu::vector<imagef> hdr_source_images;
            imagef hdr_src_img(src_img.get_width(), src_img.get_height());

            const float max_y = rnd.frand(.000125f, 30000.0f) / 255.0f;

            for (uint32_t y = 0; y < src_img.get_height(); y++)
            {
                for (uint32_t x = 0; x < src_img.get_width(); x++)
                {
                    hdr_src_img(x, y)[0] = (float)src_img(x, y).r * max_y;
                    hdr_src_img(x, y)[1] = (float)src_img(x, y).g * max_y;
                    hdr_src_img(x, y)[2] = (float)src_img(x, y).b * max_y;
                    hdr_src_img(x, y)[3] = 1.0f;
                }
            }

            //write_exr("test.exr", hdr_src_img, 3, 0);

            hdr_source_images.push_back(hdr_src_img);
            pComp_data = basisu::basis_compress(tex_mode, hdr_source_images, flags, quality, &comp_size, &stats);
        }
        else
        {
            basisu::vector<basisu::image> ldr_source_images;
            ldr_source_images.push_back(src_img);

            //save_png("test.png", src_img);

            pComp_data = basisu::basis_compress(tex_mode, ldr_source_images, flags, quality, &comp_size, &stats);
        }

        if (!pComp_data)
        {
            fprintf(stderr, "basisu::basis_compress() failed\n");
            return false;
        }

        basisu::basis_free_data(pComp_data);

        const float psnr1 = stats.m_basis_rgba_avg_psnr ? stats.m_basis_rgba_avg_psnr : stats.m_basis_rgb_avg_psnr;
        const float psnr2 = stats.m_bc7_rgba_avg_psnr ? stats.m_bc7_rgba_avg_psnr : stats.m_basis_rgb_avg_bc6h_psnr;

        lowest_psnr1 = minimum(lowest_psnr1, psnr1);
        lowest_psnr2 = minimum(lowest_psnr2, psnr2);

        results.push_back(
            result{ seed, tex_mode,
            psnr1,
            psnr2 });

    } // i

    printf("PSNR Results:\n");

    for (uint32_t i = 0; i < results.size(); i++)
        fmt_printf("{},{},{},{}\n", results[i].m_seed, (uint32_t)results[i].m_fmt, results[i].m_psnr1, results[i].m_psnr2);

    printf("\n");

    for (uint32_t i = 0; i < results.size(); i++)
        fmt_printf("seed={} tex_mode={}, psnr1={}, psnr2={}\n", results[i].m_seed, (uint32_t)results[i].m_fmt, results[i].m_psnr1, results[i].m_psnr2);

    // Success here is essentially not crashing or asserting or SAN'ing earlier
    printf("Success\n");

    return true;
}

int main(int arg_c, char* arg_v[])
{
    BASISU_NOTE_UNUSED(arg_c);
    BASISU_NOTE_UNUSED(arg_v);

#if USE_ENCODER
	basisu_encoder_init(USE_OPENCL, false);

    if (!random_compression_fuzz_test())
        return EXIT_FAILURE;

    if (!block_unpack_and_transcode_example())
        return EXIT_FAILURE;

    fuzz_uastc_hdr_transcoder_test();

    if (!encode_etc1s())
    {
        fprintf(stderr, "encode_etc1s() failed!\n");
        return EXIT_FAILURE;
    }

    if (!encode_uastc_hdr())
    {
        fprintf(stderr, "encode_uastc_hdr() failed!\n");
        return EXIT_FAILURE;
    }

    if (!encode_uastc_ldr())
    {
        fprintf(stderr, "encode_uastc_ldr() failed!\n");
        return EXIT_FAILURE;
    }
#endif

    if (!transcode_hdr())
    {
        fprintf(stderr, "transcode_hdr() failed!\n");
        return EXIT_FAILURE;
    }

    printf("All functions succeeded\n");

	return EXIT_SUCCESS;
}

// basis_wrappers.cpp - Wrappers to the C++ compressor and transcoder for WebAssembly/WebGL use.
// This file exposes wrappers for transcoding and encoding .basis and .KTX2 files.
//
// **Important**:
// Compile with -fno-strict-aliasing (same as the Linux kernel).
// This code HAS NOT been tested with strict aliasing enabled.
// The "initializeBasis()" function MUST be called at least once before using either the compressor or transcoder.
//
// There are four main categories of wrappers in this module:
// 1. Transcoding, low-level .basis file information: See class basis_file. Only depends on transcoder/basisu_transcoder.cpp.
//    getFileDesc(), getImageDesc(), getImageLevelDesc(): These functions return low-level information about where compressed data is located for each image in a .basis file.
//    This is useful for when you want to extract the compressed data and embed it into your own file formats, for container independent transcoding.
//
// 2. Encoding (optional): See class basis_encoder. Encodes LDR .PNG or 32bpp images, or HDR half-float/float or .EXR/.HDR images to .basis/.ktx2 files in memory.
//    Must compile with BASISU_SUPPORT_ENCODING=1.
//    Requires basisu_transcoder.cpp as well as all the .cpp files in the "encoder" directory. Results in a larger WebAssembly executable.
//
// 3. Low level transcoding/container independent transcoding: See class lowlevel_etc1s_image_transcoder or function transcodeUASTCImage().
//	  For transcoding raw compressed ETC1S/UASTC LDR/UASTC HDR texture data from non-.basis files (say from KTX2) to GPU texture data.
//
// 4. Helpers, transcoder texture format information: See functions getBytesPerBlockOrPixel(), formatHasAlpha(), etc.

// If BASISU_SUPPORT_ENCODING is 1, wrappers for the compressor will be included. Otherwise, only the wrappers for the transcoder will be compiled in.
#ifndef BASISU_SUPPORT_ENCODING
#define BASISU_SUPPORT_ENCODING 0
#endif

// Enable debug printf()'s in this module.
#ifndef BASISU_DEBUG_PRINTF
#define BASISU_DEBUG_PRINTF 0
#endif

#define BASISU_ENCODER_MAX_SOURCE_IMAGE_PIXELS (6291456)

#include "basisu_transcoder.h"
#include <algorithm>

#if BASISU_SUPPORT_ENCODING
#include "../../encoder/basisu_comp.h"
#include "../../encoder/basisu_astc_hdr_6x6_enc.h"
#include "../../encoder/basisu_resampler_filters.h"
#endif

#include <emscripten/bind.h>
#if BASISU_SUPPORT_ENCODING
#include <emscripten/threading.h>
#endif
#include <emscripten.h>

using namespace emscripten;
using namespace basist;
using namespace basisu;

static bool g_basis_initialized_flag;

// Global one-time initialization. MUST be called before using any other functionality.
void basis_init()
{
    static std::mutex s_init_mutex;
    std::lock_guard<std::mutex> lock(s_init_mutex);

#if BASISU_DEBUG_PRINTF
    printf("basis_init()\n");
#endif

    if (g_basis_initialized_flag)
        return;

#if BASISU_SUPPORT_ENCODING
    basisu_encoder_init();
#endif

    basisu_transcoder_init();

    g_basis_initialized_flag = true;
}

static void copy_from_jsbuffer(const emscripten::val& srcBuffer, basisu::vector<uint8_t>& dstVec)
{
    unsigned int length = srcBuffer["length"].as<unsigned int>();

    dstVec.resize(length);

    emscripten::val memory = emscripten::val::module_property("HEAP8")["buffer"];
    emscripten::val memoryView = srcBuffer["constructor"].new_(memory, reinterpret_cast<uintptr_t>(dstVec.data()), length);

    // Copy the bytes from the Javascript buffer.
    memoryView.call<void>("set", srcBuffer);
}

static bool copy_to_jsbuffer(const emscripten::val& dstBuffer, const basisu::vector<uint8_t>& srcVec)
{
    if (srcVec.empty())
    {
#if BASISU_DEBUG_PRINTF
        printf("copy_to_jsbuffer: Provided source buffer is empty\n");
#endif
        return false;
    }

    // Make sure the provided buffer from Javascript is big enough. If not, bail.
    int dstBufferLen = dstBuffer["byteLength"].as<int>();

    if (srcVec.size() > dstBufferLen)
    {
#if BASISU_DEBUG_PRINTF
        printf("copy_to_jsbuffer: Provided destination buffer is too small (wanted %u bytes, got %u bytes)!\n", (uint32_t)srcVec.size(), dstBufferLen);
#endif
        assert(0);
        return false;
    }

    emscripten::val memory = emscripten::val::module_property("HEAP8")["buffer"];
    emscripten::val memoryView = emscripten::val::global("Uint8Array").new_(memory, reinterpret_cast<uintptr_t>(srcVec.data()), srcVec.size());

    // Copy the bytes into the Javascript buffer.
    dstBuffer.call<void>("set", memoryView);

    return true;
}

const uint32_t BASIS_MAGIC = 0xD4ADBEA1;
const uint32_t KTX2_MAGIC = 0xD4ADBEF2;

struct basis_file_desc
{
    uint32_t m_version;

    uint32_t m_us_per_frame;

    uint32_t m_total_images;

    uint32_t m_userdata0;
    uint32_t m_userdata1;

    // Type of texture (cETC1S, cUASTC4x4, cUASTC_HDR_4x4, etc.)
    uint32_t m_tex_format; // basis_tex_format

    bool m_y_flipped;
    bool m_has_alpha_slices;

    // ETC1S endpoint codebook
    uint32_t m_num_endpoints;
    uint32_t m_endpoint_palette_ofs;
    uint32_t m_endpoint_palette_len;

    // ETC1S selector codebook
    uint32_t m_num_selectors;
    uint32_t m_selector_palette_ofs;
    uint32_t m_selector_palette_len;

    // Huffman codelength tables
    uint32_t m_tables_ofs;
    uint32_t m_tables_len;

    uint32_t m_block_width;
    uint32_t m_block_height;
};

struct basis_image_desc
{
    uint32_t m_orig_width;
    uint32_t m_orig_height;
    uint32_t m_num_blocks_x;
    uint32_t m_num_blocks_y;
    uint32_t m_num_levels;
    uint32_t m_block_width;
    uint32_t m_block_height;

    // Will be true if the image has alpha (for UASTC this may vary per-image)
    bool m_alpha_flag;
    bool m_iframe_flag;
};

struct basis_image_level_desc
{
    // File offset/length of the compressed ETC1S or UASTC LDR/HDR texture data.
    uint32_t m_rgb_file_ofs;
    uint32_t m_rgb_file_len;

    // Optional alpha data file offset/length - will be 0's for UASTC LDR/HDR or opaque ETC1S files.
    uint32_t m_alpha_file_ofs;
    uint32_t m_alpha_file_len;
};

struct basis_file
{
    int m_magic = 0;
    basisu_transcoder m_transcoder;
    basisu::vector<uint8_t> m_file;

    basis_file(const emscripten::val& jsBuffer)
        : m_file([&]() {
        size_t byteLength = jsBuffer["byteLength"].as<size_t>();
        return basisu::vector<uint8_t>(byteLength);
            }())
    {
        if (!g_basis_initialized_flag)
        {
#if BASISU_DEBUG_PRINTF
            printf("basis_file::basis_file: Must call basis_init() first!\n");
#endif
            assert(0);
            return;
        }

        unsigned int length = jsBuffer["length"].as<unsigned int>();
        emscripten::val memory = emscripten::val::module_property("HEAP8")["buffer"];
        emscripten::val memoryView = jsBuffer["constructor"].new_(memory, reinterpret_cast<uintptr_t>(m_file.data()), length);
        memoryView.call<void>("set", jsBuffer);

        if (!m_transcoder.validate_header(m_file.data(), m_file.size()))
        {
#if BASISU_DEBUG_PRINTF
            printf("basis_file::basis_file: m_transcoder.validate_header() failed!\n");
#endif
            m_file.clear();
        }

        // Initialized after validation
        m_magic = BASIS_MAGIC;
    }

    void close()
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return;

        m_file.clear();
    }

    uint32_t getHasAlpha()
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return 0;

        basisu_image_level_info li;
        if (!m_transcoder.get_image_level_info(m_file.data(), m_file.size(), li, 0, 0))
            return 0;

        return li.m_alpha_flag;
    }

    uint32_t getNumImages()
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return 0;

        return m_transcoder.get_total_images(m_file.data(), m_file.size());
    }

    uint32_t getNumLevels(uint32_t image_index)
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return 0;

        basisu_image_info ii;
        if (!m_transcoder.get_image_info(m_file.data(), m_file.size(), ii, image_index))
            return 0;

        return ii.m_total_levels;
    }

    uint32_t getImageWidth(uint32_t image_index, uint32_t level_index)
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return 0;

        uint32_t orig_width, orig_height, total_blocks;
        if (!m_transcoder.get_image_level_desc(m_file.data(), m_file.size(), image_index, level_index, orig_width, orig_height, total_blocks))
            return 0;

        return orig_width;
    }

    uint32_t getImageHeight(uint32_t image_index, uint32_t level_index)
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return 0;

        uint32_t orig_width, orig_height, total_blocks;
        if (!m_transcoder.get_image_level_desc(m_file.data(), m_file.size(), image_index, level_index, orig_width, orig_height, total_blocks))
            return 0;

        return orig_height;
    }

    // Returns a basis_tex_format (cETC1S, cUASTC, cUASTC_HDR_4x4, etc.)
    uint32_t getBasisTexFormat()
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return false;

        basis_tex_format fmt = m_transcoder.get_basis_tex_format(m_file.data(), m_file.size());
        return (uint32_t)fmt;
    }

    // Currently 4 or 6
    uint32_t getBlockWidth() const
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return false;

        basis_tex_format fmt = m_transcoder.get_basis_tex_format(m_file.data(), m_file.size());
        return basis_tex_format_get_block_width(fmt);
    }

    // Currently 4 or 6
    uint32_t getBlockHeight()
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return false;

        basis_tex_format fmt = m_transcoder.get_basis_tex_format(m_file.data(), m_file.size());
        return basis_tex_format_get_block_height(fmt);
    }

    basis_file_desc getFileDesc()
    {
        basis_file_desc result;
        memset(&result, 0, sizeof(result));

        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return result;

        basisu_file_info file_info;

        if (!m_transcoder.get_file_info(m_file.data(), m_file.size(), file_info))
        {
            assert(0);
            return result;
        }

        result.m_version = file_info.m_version;
        result.m_us_per_frame = file_info.m_us_per_frame;
        result.m_total_images = file_info.m_total_images;
        result.m_userdata0 = file_info.m_userdata0;
        result.m_userdata1 = file_info.m_userdata1;
        result.m_tex_format = static_cast<uint32_t>(file_info.m_tex_format);
        result.m_y_flipped = file_info.m_y_flipped;
        result.m_has_alpha_slices = file_info.m_has_alpha_slices;

        result.m_num_endpoints = file_info.m_total_endpoints;
        result.m_endpoint_palette_ofs = file_info.m_endpoint_codebook_ofs;
        result.m_endpoint_palette_len = file_info.m_endpoint_codebook_size;

        result.m_num_selectors = file_info.m_total_selectors;
        result.m_selector_palette_ofs = file_info.m_selector_codebook_ofs;
        result.m_selector_palette_len = file_info.m_selector_codebook_size;

        result.m_tables_ofs = file_info.m_tables_ofs;
        result.m_tables_len = file_info.m_tables_size;

        result.m_block_width = file_info.m_block_width;
        result.m_block_height = file_info.m_block_height;

        return result;
    }

    basis_image_desc getImageDesc(uint32_t image_index)
    {
        basis_image_desc result;
        memset(&result, 0, sizeof(result));

        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return result;

        basisu_image_info image_info;

        // bool get_image_info(const void *pData, uint32_t data_size, basisu_image_info &image_info, uint32_t image_index) const;
        if (!m_transcoder.get_image_info(m_file.data(), m_file.size(), image_info, image_index))
        {
            assert(0);
            return result;
        }

        result.m_orig_width = image_info.m_orig_width;
        result.m_orig_height = image_info.m_orig_height;
        result.m_num_blocks_x = image_info.m_num_blocks_x;
        result.m_num_blocks_y = image_info.m_num_blocks_y;
        result.m_num_levels = image_info.m_total_levels;
        result.m_alpha_flag = image_info.m_alpha_flag;
        result.m_iframe_flag = image_info.m_iframe_flag;
        result.m_block_width = image_info.m_block_width;
        result.m_block_height = image_info.m_block_height;

        return result;
    }

    basis_image_level_desc getImageLevelDesc(uint32_t image_index, uint32_t level_index)
    {
        basis_image_level_desc result;
        memset(&result, 0, sizeof(result));

        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return result;

        basisu_image_level_info image_info;

        if (!m_transcoder.get_image_level_info(m_file.data(), m_file.size(), image_info, image_index, level_index))
        {
            assert(0);
            return result;
        }

        result.m_rgb_file_ofs = image_info.m_rgb_file_ofs;
        result.m_rgb_file_len = image_info.m_rgb_file_len;
        result.m_alpha_file_ofs = image_info.m_alpha_file_ofs;
        result.m_alpha_file_len = image_info.m_alpha_file_len;

        return result;
    }

    uint32_t getImageTranscodedSizeInBytes(uint32_t image_index, uint32_t level_index, uint32_t format)
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return 0;

        if (format >= (int)transcoder_texture_format::cTFTotalTextureFormats)
        {
            assert(0);
            return 0;
        }

        const transcoder_texture_format tex_format = static_cast<transcoder_texture_format>(format);

        uint32_t orig_width, orig_height, total_src_blocks;
        if (!m_transcoder.get_image_level_desc(m_file.data(), m_file.size(), image_index, level_index, orig_width, orig_height, total_src_blocks))
        {
            assert(0);
            return 0;
        }

        return basis_compute_transcoded_image_size_in_bytes(tex_format, orig_width, orig_height);
    }

    // Only true for UASTC LDR 4x4.
    bool isUASTC_LDR_4x4()
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return false;

        return m_transcoder.get_basis_tex_format(m_file.data(), m_file.size()) == basis_tex_format::cUASTC4x4;
    }

    bool isETC1S()
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return false;

        return m_transcoder.get_basis_tex_format(m_file.data(), m_file.size()) == basis_tex_format::cETC1S;
    }

    // True for any LDR texture types
    bool isLDR()
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return false;

        basis_tex_format fmt = m_transcoder.get_basis_tex_format(m_file.data(), m_file.size());
        return (fmt == basis_tex_format::cETC1S) || (fmt == basis_tex_format::cUASTC4x4);
    }

	// True if the texture is UASTC HDR 4x4 or ASTC HDR 6x6.
    // In this case, it can only be transcoded to BC6H, ASTC HDR (of the same block dimensions, currently 4x4 or 6x6), RGB9E5 or half-float RGB/RGBA images.
    bool isHDR()
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return false;

        basis_tex_format fmt = m_transcoder.get_basis_tex_format(m_file.data(), m_file.size());
        return basis_tex_format_is_hdr(fmt);
    }

    bool isHDR4x4()
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return false;

        basis_tex_format fmt = m_transcoder.get_basis_tex_format(m_file.data(), m_file.size());
        return (fmt == basis_tex_format::cUASTC_HDR_4x4);
    }

    bool isHDR6x6()
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return false;

        basis_tex_format fmt = m_transcoder.get_basis_tex_format(m_file.data(), m_file.size());
        return (fmt == basis_tex_format::cASTC_HDR_6x6) || (fmt == basis_tex_format::cASTC_HDR_6x6_INTERMEDIATE);
    }

    uint32_t startTranscoding()
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return 0;

        return m_transcoder.start_transcoding(m_file.data(), m_file.size());
    }

    // Here for backwards compat, prefer transcodeImageWithFlags().
    uint32_t transcodeImage(const emscripten::val& dst, uint32_t image_index, uint32_t level_index, uint32_t format, uint32_t unused, uint32_t get_alpha_for_opaque_formats)
    {
        (void)unused;

        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return 0;

        if (format >= (int)transcoder_texture_format::cTFTotalTextureFormats)
            return 0;

        const transcoder_texture_format transcoder_format = static_cast<transcoder_texture_format>(format);

        uint32_t orig_width, orig_height, total_src_blocks;
        if (!m_transcoder.get_image_level_desc(m_file.data(), m_file.size(), image_index, level_index, orig_width, orig_height, total_src_blocks))
            return 0;

        basisu::vector<uint8_t> dst_data;

        uint32_t flags = get_alpha_for_opaque_formats ? cDecodeFlagsTranscodeAlphaDataToOpaqueFormats : 0;

        const uint32_t transcoded_size_in_bytes = getImageTranscodedSizeInBytes(image_index, level_index, format);

        dst_data.resize(transcoded_size_in_bytes);

        uint32_t status;

        if (basis_transcoder_format_is_uncompressed(transcoder_format))
        {
            status = m_transcoder.transcode_image_level(
                m_file.data(), m_file.size(), image_index, level_index,
                dst_data.data(), orig_width * orig_height,
                transcoder_format,
                flags,
                orig_width,
                nullptr,
                orig_height);
        }
        else
        {
            const uint32_t bytes_per_block = basis_get_bytes_per_block_or_pixel(transcoder_format);

            status = m_transcoder.transcode_image_level(
                m_file.data(), m_file.size(), image_index, level_index,
                dst_data.data(), dst_data.size() / bytes_per_block,
                static_cast<basist::transcoder_texture_format>(format),
                flags);
        }

        emscripten::val memory = emscripten::val::module_property("HEAP8")["buffer"];
        emscripten::val memoryView = emscripten::val::global("Uint8Array").new_(memory, reinterpret_cast<uintptr_t>(dst_data.data()), dst_data.size());

        dst.call<void>("set", memoryView);
        return status;
    }

    // Like transcodeImage(), but with fixed parameters.
    // For flags, see cDecodeFlagsPVRTCDecodeToNextPow2 etc.
    uint32_t transcodeImageWithFlags(const emscripten::val& dst, uint32_t image_index, uint32_t level_index, uint32_t format, uint32_t flags)
    {
        assert(m_magic == BASIS_MAGIC);
        if (m_magic != BASIS_MAGIC)
            return 0;

        if (format >= (int)transcoder_texture_format::cTFTotalTextureFormats)
            return 0;

        const transcoder_texture_format transcoder_format = static_cast<transcoder_texture_format>(format);

        uint32_t orig_width, orig_height, total_src_blocks;
        if (!m_transcoder.get_image_level_desc(m_file.data(), m_file.size(), image_index, level_index, orig_width, orig_height, total_src_blocks))
            return 0;

        basisu::vector<uint8_t> dst_data;

        const uint32_t transcoded_size_in_bytes = getImageTranscodedSizeInBytes(image_index, level_index, format);

        dst_data.resize(transcoded_size_in_bytes);

        uint32_t status;

        if (basis_transcoder_format_is_uncompressed(transcoder_format))
        {
            status = m_transcoder.transcode_image_level(
                m_file.data(), m_file.size(), image_index, level_index,
                dst_data.data(), orig_width * orig_height,
                transcoder_format,
                flags,
                orig_width,
                nullptr,
                orig_height);
        }
        else
        {
            const uint32_t bytes_per_block = basis_get_bytes_per_block_or_pixel(transcoder_format);

            status = m_transcoder.transcode_image_level(
                m_file.data(), m_file.size(), image_index, level_index,
                dst_data.data(), dst_data.size() / bytes_per_block,
                static_cast<basist::transcoder_texture_format>(format),
                flags);
        }

        emscripten::val memory = emscripten::val::module_property("HEAP8")["buffer"];
        emscripten::val memoryView = emscripten::val::global("Uint8Array").new_(memory, reinterpret_cast<uintptr_t>(dst_data.data()), dst_data.size());

        dst.call<void>("set", memoryView);
        return status;
    }
};

#if BASISD_SUPPORT_KTX2
struct ktx2_header_js
{
    uint32_t m_vk_format;
    uint32_t m_type_size;
    uint32_t m_pixel_width;
    uint32_t m_pixel_height;
    uint32_t m_pixel_depth;
    uint32_t m_layer_count;
    uint32_t m_face_count;
    uint32_t m_level_count;
    uint32_t m_supercompression_scheme;
    uint32_t m_dfd_byte_offset;
    uint32_t m_dfd_byte_length;
    uint32_t m_kvd_byte_offset;
    uint32_t m_kvd_byte_length;
    uint32_t m_sgd_byte_offset;
    uint32_t m_sgd_byte_length;
};

struct ktx2_file
{
    int m_magic = 0;
    basist::ktx2_transcoder m_transcoder;
    basisu::vector<uint8_t> m_file;
    bool m_is_valid = false;

    ktx2_file(const emscripten::val& jsBuffer)
        : m_file([&]() {
        size_t byteLength = jsBuffer["byteLength"].as<size_t>();
        return basisu::vector<uint8_t>(byteLength);
            }())
    {
        if (!g_basis_initialized_flag)
        {
#if BASISU_DEBUG_PRINTF
            printf("basis_file::basis_file: Must call basis_init() first!\n");
#endif
            assert(0);
            return;
        }

        unsigned int length = jsBuffer["length"].as<unsigned int>();
        emscripten::val memory = emscripten::val::module_property("HEAP8")["buffer"];
        emscripten::val memoryView = jsBuffer["constructor"].new_(memory, reinterpret_cast<uintptr_t>(m_file.data()), length);
        memoryView.call<void>("set", jsBuffer);

        if (!m_transcoder.init(m_file.data(), m_file.size()))
        {
#if BASISU_DEBUG_PRINTF
            printf("m_transcoder.init() failed!\n");
#endif
            assert(0);

            m_file.clear();
        }

        m_is_valid = true;

        // Initialized after validation
        m_magic = KTX2_MAGIC;
    }

    bool isValid()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return false;

        return m_is_valid;
    }

    void close()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return;

        m_file.clear();
        m_transcoder.clear();
    }

    uint32_t getDFDSize()
    {
        return m_transcoder.get_dfd().size();
    }

    uint32_t getDFD(const emscripten::val& dst)
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;

        const uint8_vec &dst_data = m_transcoder.get_dfd();

        if (dst_data.size())
            return copy_to_jsbuffer(dst, dst_data);

        return 1;
    }

    ktx2_header_js getHeader()
    {
        ktx2_header_js hdr;
        memset(&hdr, 0, sizeof(hdr));

        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return hdr;

        const basist::ktx2_header& h = m_transcoder.get_header();

		if ((h.m_sgd_byte_offset.get_uint64() > UINT32_MAX) || (h.m_sgd_byte_length.get_uint64() > UINT32_MAX))
		{
			// File is too large to handle with 32-bit WASM.
			// (emscripten doesn't support binding uint64_t for some reason.)
			assert(0);
			return hdr;
		}

        hdr.m_vk_format = h.m_vk_format;
        hdr.m_type_size = h.m_type_size;
        hdr.m_pixel_width = h.m_pixel_width;
        hdr.m_pixel_height = h.m_pixel_height;
        hdr.m_pixel_depth = h.m_pixel_depth;
        hdr.m_layer_count = h.m_layer_count;
        hdr.m_face_count = h.m_face_count;
        hdr.m_level_count = h.m_level_count;
        hdr.m_supercompression_scheme = h.m_supercompression_scheme;
        hdr.m_dfd_byte_offset = h.m_dfd_byte_offset;
        hdr.m_dfd_byte_length = h.m_dfd_byte_length;
        hdr.m_kvd_byte_offset = h.m_kvd_byte_offset;
        hdr.m_kvd_byte_length = h.m_kvd_byte_length;

        hdr.m_sgd_byte_offset = (uint32_t)h.m_sgd_byte_offset.get_uint64();
        hdr.m_sgd_byte_length = (uint32_t)h.m_sgd_byte_length.get_uint64();

        return hdr;
    }

    bool hasKey(std::string key_name)
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return false;

        return m_transcoder.find_key(key_name) != nullptr;
    }

    uint32_t getTotalKeys()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;

        return m_transcoder.get_key_values().size();
    }

    std::string getKey(uint32_t index)
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return std::string("");

        return std::string((const char*)m_transcoder.get_key_values()[index].m_key.data());
    }

    uint32_t getKeyValueSize(std::string key_name)
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;

        const uint8_vec* p = m_transcoder.find_key(key_name);
        return p ? p->size() : 0;
    }

    uint32_t getKeyValue(std::string key_name, const emscripten::val& dst)
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;

        const uint8_vec* p = m_transcoder.find_key(key_name);
        if (!p)
            return 0;

        if (p->size())
            return copy_to_jsbuffer(dst, *p);

        return 1;
    }

    uint32_t getWidth()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_width();
    }

    uint32_t getHeight()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_height();
    }

    uint32_t getBlockWidth()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_block_width();
    }

    uint32_t getBlockHeight()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_block_height();
    }

    uint32_t getFaces()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_faces();
    }

    uint32_t getLayers()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_layers();
    }

    uint32_t getLevels()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_levels();
    }

	// Returns a basis_tex_format: cETC1S, cUASTC4x4, or cUASTC_HDR_4x4, etc.
    uint32_t getBasisTexFormat()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return (uint32_t)m_transcoder.get_basis_tex_format();
    }

	// Returns true if the texture is UASTC LDR 4x4 (and not HDR - see isHDR()).
    bool isUASTC_LDR_4x4()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return false;
        return m_transcoder.is_uastc();
    }

    // Returns true if the texture is UASTC LDR 4x4 (and not HDR - see isHDR()).
    bool isUASTC()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return false;
        return m_transcoder.is_uastc();
    }

    bool isLDR()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return false;
        return !m_transcoder.is_hdr();
    }

    bool isETC1S()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return false;
        return m_transcoder.is_etc1s();
    }

	// Returns true if the texture is UASTC HDR or ASTC HDR. In this case, it can only be transcoded to BC6H, ASTC HDR (of the same block dimensions), RGB9E5 or half-float RGB/RGBA images.
	bool isHDR()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return false;
        return m_transcoder.is_hdr();
    }

    bool isHDR4x4()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return false;
        return m_transcoder.is_hdr_4x4();
    }

    bool isHDR6x6()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return false;
        return m_transcoder.is_hdr_6x6();
    }

    bool getHasAlpha()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return false;
        return m_transcoder.get_has_alpha();
    }

    uint32_t getDFDColorModel()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_dfd_color_model();
    }

    uint32_t getDFDColorPrimaries()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_dfd_color_primaries();
    }

    uint32_t getDFDTransferFunc()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_dfd_transfer_func();
    }

    uint32_t getDFDFlags()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_dfd_flags();
    }

    uint32_t getDFDTotalSamples()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_dfd_total_samples();
    }

    uint32_t getDFDChannelID0()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_dfd_channel_id0();
    }

    uint32_t getDFDChannelID1()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_dfd_channel_id1();
    }

    // isVideo() will return true if there was a KTXanimData key, or if (after calling start_transcoding()) there were any P-frames on ETC1S files.
    bool isVideo()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.is_video();
    }

    float getLDRHDRUpconversionNitMultiplier()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;
        return m_transcoder.get_ldr_hdr_upconversion_nit_multiplier();
    }

    // startTranscoding() must be called before calling getETC1SImageDescImageFlags().
    uint32_t getETC1SImageDescImageFlags(uint32_t level_index, uint32_t layer_index, uint32_t face_index)
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;

        return m_transcoder.get_etc1s_image_descs_image_flags(level_index, layer_index, face_index);
    }

    ktx2_image_level_info getImageLevelInfo(uint32_t level_index, uint32_t layer_index, uint32_t face_index)
    {
        ktx2_image_level_info info;
        memset(&info, 0, sizeof(info));

        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return info;

        if (!m_transcoder.get_image_level_info(info, level_index, layer_index, face_index))
        {
            assert(0);
            return info;
        }

        return info;
    }

    uint32_t getImageTranscodedSizeInBytes(uint32_t level_index, uint32_t layer_index, uint32_t face_index, uint32_t format)
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;

        if (format >= (int)transcoder_texture_format::cTFTotalTextureFormats)
        {
            assert(0);
            return 0;
        }

        const transcoder_texture_format tex_format = static_cast<transcoder_texture_format>(format);

        ktx2_image_level_info info;
        if (!m_transcoder.get_image_level_info(info, level_index, layer_index, face_index))
        {
            assert(0);
            return 0;
        }

        return basis_compute_transcoded_image_size_in_bytes(tex_format, info.m_orig_width, info.m_orig_height);
    }

    // Must be called before transcodeImage() can be called.
    // On ETC1S files this method decompresses the ETC1S global data, along with fetching the ETC1S image desc array, so it's not free to call.
    uint32_t startTranscoding()
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;

        return m_transcoder.start_transcoding();
    }

    // Here for backwards compat, prefer transcodeImageWithFlags().
    // get_alpha_for_opaque_formats defaults to false
    // channel0/channel1 default to -1
    uint32_t transcodeImage(const emscripten::val& dst, uint32_t level_index, uint32_t layer_index, uint32_t face_index, uint32_t format, uint32_t get_alpha_for_opaque_formats, int channel0, int channel1)
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;

        if (format >= (int)transcoder_texture_format::cTFTotalTextureFormats)
            return 0;

        const transcoder_texture_format transcoder_format = static_cast<transcoder_texture_format>(format);

        const uint32_t dst_block_width = basis_get_block_width(transcoder_format);
        const uint32_t dst_block_height = basis_get_block_height(transcoder_format);

        ktx2_image_level_info info;
        if (!m_transcoder.get_image_level_info(info, level_index, layer_index, face_index))
            return 0;

        uint32_t orig_width = info.m_orig_width, orig_height = info.m_orig_height, total_src_blocks = info.m_total_blocks;

        basisu::vector<uint8_t> dst_data;

        uint32_t flags = get_alpha_for_opaque_formats ? cDecodeFlagsTranscodeAlphaDataToOpaqueFormats : 0;

        const uint32_t transcoded_size_in_bytes = getImageTranscodedSizeInBytes(level_index, layer_index, face_index, format);

        dst_data.resize(transcoded_size_in_bytes);

        uint32_t status;

        if (basis_transcoder_format_is_uncompressed(transcoder_format))
        {
            status = m_transcoder.transcode_image_level(
                level_index, layer_index, face_index,
                dst_data.data(), orig_width * orig_height,
                transcoder_format,
                flags,
                orig_width,
                orig_height,
                channel0, channel1,
                nullptr);
        }
        else
        {
            const uint32_t bytes_per_block = basis_get_bytes_per_block_or_pixel(transcoder_format);

            status = m_transcoder.transcode_image_level(
                level_index, layer_index, face_index,
                dst_data.data(), dst_data.size() / bytes_per_block,
                static_cast<basist::transcoder_texture_format>(format),
                flags,
                0,
                0,
                channel0, channel1,
                nullptr);
        }

        emscripten::val memory = emscripten::val::module_property("HEAP8")["buffer"];
        emscripten::val memoryView = emscripten::val::global("Uint8Array").new_(memory, reinterpret_cast<uintptr_t>(dst_data.data()), dst_data.size());

        dst.call<void>("set", memoryView);
        return status;
    }

    // like transcodeImage(), but with fixed parameters (includes flags)
    // For flags, see cDecodeFlagsPVRTCDecodeToNextPow2 etc.
    uint32_t transcodeImageWithFlags(const emscripten::val& dst, uint32_t level_index, uint32_t layer_index, uint32_t face_index, uint32_t format, uint32_t flags, int channel0, int channel1)
    {
        assert(m_magic == KTX2_MAGIC);
        if (m_magic != KTX2_MAGIC)
            return 0;

        if (format >= (int)transcoder_texture_format::cTFTotalTextureFormats)
            return 0;

        const transcoder_texture_format transcoder_format = static_cast<transcoder_texture_format>(format);

        const uint32_t dst_block_width = basis_get_block_width(transcoder_format);
        const uint32_t dst_block_height = basis_get_block_height(transcoder_format);

        ktx2_image_level_info info;
        if (!m_transcoder.get_image_level_info(info, level_index, layer_index, face_index))
            return 0;

        uint32_t orig_width = info.m_orig_width, orig_height = info.m_orig_height, total_src_blocks = info.m_total_blocks;

        basisu::vector<uint8_t> dst_data;

        const uint32_t transcoded_size_in_bytes = getImageTranscodedSizeInBytes(level_index, layer_index, face_index, format);

        dst_data.resize(transcoded_size_in_bytes);

        uint32_t status;

        if (basis_transcoder_format_is_uncompressed(transcoder_format))
        {
            status = m_transcoder.transcode_image_level(
                level_index, layer_index, face_index,
                dst_data.data(), orig_width * orig_height,
                transcoder_format,
                flags,
                orig_width,
                orig_height,
                channel0, channel1,
                nullptr);
        }
        else
        {
            const uint32_t bytes_per_block = basis_get_bytes_per_block_or_pixel(transcoder_format);

            status = m_transcoder.transcode_image_level(
                level_index, layer_index, face_index,
                dst_data.data(), dst_data.size() / bytes_per_block,
                static_cast<basist::transcoder_texture_format>(format),
                flags,
                0,
                0,
                channel0, channel1,
                nullptr);
        }

        emscripten::val memory = emscripten::val::module_property("HEAP8")["buffer"];
        emscripten::val memoryView = emscripten::val::global("Uint8Array").new_(memory, reinterpret_cast<uintptr_t>(dst_data.data()), dst_data.size());

        dst.call<void>("set", memoryView);
        return status;
    }

};
#endif // BASISD_SUPPORT_KTX2

#if BASISU_SUPPORT_ENCODING

enum class ldr_image_type
{
    cRGBA32 = 0,
    cPNGImage = 1,
    cJPGImage = 2
};

class basis_encoder
{
    bool m_threading_enabled = false;
    uint32_t m_num_extra_worker_threads = 0;

public:
    basis_compressor_params m_params;

    basis_encoder()
    {
    }

    void control_threading(bool enabled_flag, uint32_t num_extra_worker_threads)
    {
        m_threading_enabled = enabled_flag;
        m_num_extra_worker_threads = num_extra_worker_threads;
    }

	// Only works for LDR inputs.
    bool set_slice_source_image(uint32_t slice_index, const emscripten::val& src_image_js_val, uint32_t src_image_width, uint32_t src_image_height, ldr_image_type img_type)
    {
        // Resize the source_images array if necessary
        if (slice_index >= m_params.m_source_images.size())
            m_params.m_source_images.resize(slice_index + 1);

        // First copy the src image buffer to the heap.
        basisu::vector<uint8_t> src_image_buf;
        copy_from_jsbuffer(src_image_js_val, src_image_buf);

        // Now load the source image.
        image& src_img = m_params.m_source_images[slice_index];
        if (img_type == ldr_image_type::cPNGImage)
        {
            // It's a PNG file, so try and parse it.
            if (!load_png(src_image_buf.data(), src_image_buf.size(), src_img, nullptr))
            {
#if BASISU_DEBUG_PRINTF
                printf("basis_encoder::set_slice_source_image: Failed parsing provided PNG file!\n");
#endif
                return false;
            }

            src_image_width = src_img.get_width();
            src_image_height = src_img.get_height();
        }
        else if (img_type == ldr_image_type::cJPGImage)
        {
            // It's a JPG file, so try and parse it.
            if (!load_jpg(src_image_buf.data(), src_image_buf.size(), src_img))
            {
#if BASISU_DEBUG_PRINTF
                printf("basis_encoder::set_slice_source_image: Failed parsing provided JPG file!\n");
#endif
                return false;
            }

            src_image_width = src_img.get_width();
            src_image_height = src_img.get_height();
        }
        else if (img_type == ldr_image_type::cRGBA32)
        {
            // It's a raw image, so check the buffer's size.
            if (src_image_buf.size() != src_image_width * src_image_height * sizeof(uint32_t))
            {
#if BASISU_DEBUG_PRINTF
                printf("basis_encoder::set_slice_source_image: Provided source buffer has an invalid size!\n");
#endif
                return false;
            }

            // Copy the raw image's data into our source image.
            src_img.resize(src_image_width, src_image_height);
            memcpy(src_img.get_ptr(), src_image_buf.data(), src_image_width * src_image_height * sizeof(uint32_t));
        }
        else
        {
#if BASISU_DEBUG_PRINTF
            printf("basis_encoder::set_slice_source_image: Invalid img_type parameter\n");
#endif
            assert(0);
            return false;
        }

        return true;
    }

	// Accepts RGBA half float or RGBA float images, or .EXR, .HDR, .PNG, or .JPG file data.
    bool set_slice_source_image_hdr(
        uint32_t slice_index,
        const emscripten::val& src_image_js_val,
        uint32_t src_image_width, uint32_t src_image_height,
        hdr_image_type img_type, bool ldr_srgb_to_linear_conversion, float ldr_to_hdr_nit_multiplier)
    {
        assert(ldr_to_hdr_nit_multiplier > 0.0f);

        // Resize the source_images_hdr array if necessary
        if (slice_index >= m_params.m_source_images_hdr.size())
            m_params.m_source_images_hdr.resize(slice_index + 1);

        // First copy the src image buffer to the heap.
        basisu::vector<uint8_t> src_image_buf;
        copy_from_jsbuffer(src_image_js_val, src_image_buf);

        // Now load the source image.
        imagef& src_img = m_params.m_source_images_hdr[slice_index];

        if (!load_image_hdr(src_image_buf.get_ptr(), src_image_buf.size(), src_img, src_image_width, src_image_height, img_type, ldr_srgb_to_linear_conversion, ldr_to_hdr_nit_multiplier))
            return false;

        if ((img_type == hdr_image_type::cHITPNGImage) || (img_type == hdr_image_type::cHITJPGImage))
        {
            // Because we're loading the image ourselves we need to add these tags so the UI knows how to tone map LDR upconverted outputs.
            // Normally basis_compressor adds them when it loads the images itself from source files.
            basist::ktx2_add_key_value(m_params.m_ktx2_key_values, "LDRUpconversionMultiplier", fmt_string("{}", ldr_to_hdr_nit_multiplier));

            if (ldr_srgb_to_linear_conversion)
                basist::ktx2_add_key_value(m_params.m_ktx2_key_values, "LDRUpconversionSRGBToLinear", "1");
        }

        return true;
    }

    uint32_t encode(const emscripten::val& dst_basis_file_js_val)
    {
        if (!g_basis_initialized_flag)
        {
#if BASISU_DEBUG_PRINTF
            printf("basis_encoder::encode: Must call basis_init() first!\n");
#endif
            assert(0);
            return 0;
        }

        // We don't use threading for now, but the compressor needs a job pool.
        uint32_t num_new_threads = 0;
        bool enable_threading = false;

#if WASM_THREADS_ENABLED
        if ((emscripten_has_threading_support()) && (m_threading_enabled) && (m_num_extra_worker_threads))
        {
            enable_threading = true;
            num_new_threads = m_num_extra_worker_threads;
        }
#endif

        // We always need a job pool, but making a job pool with just 1 thread doesn't actually create any additional helper threads (i.e. it always utilizes the main thread).
        job_pool jpool(1 + num_new_threads);

        // Initialize the compression parameters structure. This is the same structure that the command line tool fills in.
        basis_compressor_params &params = m_params;

        // Check to see if we would risk running out of memory in 32-bit WASM. There's not much we can do about this limit until memory64 is available.
        uint64_t total_src_texels = 0;

        for (uint32_t i = 0; i < m_params.m_source_images.size(); i++)
            total_src_texels += m_params.m_source_images[i].get_total_pixels();

        for (uint32_t i = 0; i < m_params.m_source_images_hdr.size(); i++)
            total_src_texels += m_params.m_source_images_hdr[i].get_total_pixels();

        if (total_src_texels > BASISU_ENCODER_MAX_SOURCE_IMAGE_PIXELS)
        {
            printf("ERROR: basis_encoder::encode(): The total number of source texels to compress is too large for 32-bit WASM (above BASISU_ENCODER_MAX_SOURCE_IMAGE_PIXELS in basis_wrappers.cpp)."
                "This is not a fundamental limitation of the library, but of WASM. Processing images this large risks running out of memory until WASM memory64 is available.\n");
            return 0;
        }

        params.m_pJob_pool = &jpool;

        params.m_multithreading = enable_threading;

        params.m_status_output = params.m_debug;

        params.m_read_source_images = false;
        params.m_write_output_basis_or_ktx2_files = false;

        basis_compressor comp;

        if (!comp.init(params))
        {
#if BASISU_DEBUG_PRINTF
            printf("Failed initializing BasisU compressor! One or more provided parameters may be invalid.\n");
#endif
            return 0;
        }

#if BASISU_DEBUG_PRINTF
        printf("Begin BasisU compression\n");
#endif

        basis_compressor::error_code ec = comp.process();

#if BASISU_DEBUG_PRINTF
        printf("BasisU compression done, status %u, %u bytes\n", (uint32_t)ec, (uint32_t)comp.get_output_basis_file().size());
#endif

        if (ec != basis_compressor::cECSuccess)
        {
            // Something failed during compression.
#if BASISU_DEBUG_PRINTF
            printf("BasisU compression failed with status %u!\n", (uint32_t)ec);
#endif
            return 0;
        }

        if (params.m_create_ktx2_file)
        {
            // Compression succeeded, so copy the .ktx2 file bytes to the caller's buffer.
            if (!copy_to_jsbuffer(dst_basis_file_js_val, comp.get_output_ktx2_file()))
                return 0;

            // Return the file size of the .basis file in bytes.
            return (uint32_t)comp.get_output_ktx2_file().size();
        }
        else
        {
            // Compression succeeded, so copy the .basis file bytes to the caller's buffer.
            if (!copy_to_jsbuffer(dst_basis_file_js_val, comp.get_output_basis_file()))
                return 0;

            // Return the file size of the .basis file in bytes.
            return (uint32_t)comp.get_output_basis_file().size();
        }
    }
};
#endif

class lowlevel_etc1s_image_transcoder : public basisu_lowlevel_etc1s_transcoder
{
    // Using our own transcoder state, for video support.
    basisu_transcoder_state m_state;

public:
    lowlevel_etc1s_image_transcoder()
    {
    }

    bool decode_palettes(uint32_t num_endpoints, const emscripten::val& endpoint_data, uint32_t num_selectors, const emscripten::val& selector_data)
    {
        basisu::vector<uint8_t> temp_endpoint_data, temp_selector_data;
        copy_from_jsbuffer(endpoint_data, temp_endpoint_data);
        copy_from_jsbuffer(selector_data, temp_selector_data);

#if 0
        printf("decode_palettes: %u %u %u %u, %u %u\n",
            num_endpoints, (uint32_t)temp_endpoint_data.size(),
            num_selectors, (uint32_t)temp_selector_data.size(),
            temp_endpoint_data[0], temp_selector_data[0]);
#endif

        if (!temp_endpoint_data.size() || !temp_selector_data.size())
        {
#if BASISU_DEBUG_PRINTF
            printf("decode_tables: endpoint_data and/or selector_data is empty\n");
#endif
            assert(0);
            return false;
        }

        return basisu_lowlevel_etc1s_transcoder::decode_palettes(num_endpoints, &temp_endpoint_data[0], (uint32_t)temp_endpoint_data.size(),
            num_selectors, &temp_selector_data[0], (uint32_t)temp_selector_data.size());
    }

    bool decode_tables(const emscripten::val& table_data)
    {
        basisu::vector<uint8_t> temp_table_data;
        copy_from_jsbuffer(table_data, temp_table_data);

        if (!temp_table_data.size())
        {
#if BASISU_DEBUG_PRINTF
            printf("decode_tables: table_data is empty\n");
#endif
            assert(0);
            return false;
        }

        return basisu_lowlevel_etc1s_transcoder::decode_tables(&temp_table_data[0], (uint32_t)temp_table_data.size());
    }

    bool transcode_image(
        uint32_t target_format, // see transcoder_texture_format
        const emscripten::val& output_blocks, uint32_t output_blocks_buf_size_in_blocks_or_pixels,
        const emscripten::val& compressed_data,
        uint32_t num_blocks_x, uint32_t num_blocks_y, uint32_t orig_width, uint32_t orig_height, uint32_t level_index,
        uint32_t rgb_offset, uint32_t rgb_length, uint32_t alpha_offset, uint32_t alpha_length,
        uint32_t decode_flags, // see cDecodeFlagsPVRTCDecodeToNextPow2 etc.
        bool basis_file_has_alpha_slices,
        bool is_video,
        uint32_t output_row_pitch_in_blocks_or_pixels,
        uint32_t output_rows_in_pixels)
    {
        if (!g_basis_initialized_flag)
        {
#if BASISU_DEBUG_PRINTF
            printf("transcode_etc1s_image: basis_init() must be called first\n");
#endif
            assert(0);
            return false;
        }

        // FIXME: Access the JavaScript buffer directly vs. copying it.
        basisu::vector<uint8_t> temp_comp_data;
        copy_from_jsbuffer(compressed_data, temp_comp_data);

        if (!temp_comp_data.size())
        {
#if BASISU_DEBUG_PRINTF
            printf("transcode_etc1s_image: compressed_data is empty\n");
#endif
            assert(0);
            return false;
        }

        uint32_t output_blocks_len = output_blocks["byteLength"].as<uint32_t>();
        if (!output_blocks_len)
        {
#if BASISU_DEBUG_PRINTF
            printf("transcode_etc1s_image: output_blocks is empty\n");
#endif
            assert(0);
            return false;
        }

        basisu::vector<uint8_t> temp_output_blocks(output_blocks_len);

        bool status = basisu_lowlevel_etc1s_transcoder::transcode_image(
            (transcoder_texture_format)target_format,
            &temp_output_blocks[0], output_blocks_buf_size_in_blocks_or_pixels,
            &temp_comp_data[0], temp_comp_data.size(),
            num_blocks_x, num_blocks_y, orig_width, orig_height, level_index,
            rgb_offset, rgb_length, alpha_offset, alpha_length,
            decode_flags,
            basis_file_has_alpha_slices,
            is_video,
            output_row_pitch_in_blocks_or_pixels,
            &m_state,
            output_rows_in_pixels);

        if (!status)
        {
#if BASISU_DEBUG_PRINTF
            printf("transcode_etc1s_image: basisu_lowlevel_etc1s_transcoder::transcode_image failed\n");
#endif
            assert(0);
            return false;
        }

        if (!copy_to_jsbuffer(output_blocks, temp_output_blocks))
            return false;

        return true;
    }
};

// Supports UASTC LDR 4x4, UASTC HDR 4x4, and ASTC HDR 6x6/intermediate (but not ETC1S).
bool transcode_uastc_image(
    uint32_t basis_tex_format_int,
    uint32_t target_format_int, // see transcoder_texture_format
    const emscripten::val& output_blocks, uint32_t output_blocks_buf_size_in_blocks_or_pixels,
    const emscripten::val& compressed_data,
    uint32_t num_blocks_x, uint32_t num_blocks_y, uint32_t orig_width, uint32_t orig_height, uint32_t level_index,
    uint32_t slice_offset, uint32_t slice_length,
    uint32_t decode_flags, // see cDecodeFlagsPVRTCDecodeToNextPow2 etc.
    bool has_alpha,
    bool is_video,
    uint32_t output_row_pitch_in_blocks_or_pixels,
    uint32_t output_rows_in_pixels,
    int channel0, int channel1)
{
    assert(basis_tex_format_int < (uint32_t)basis_tex_format::cTotalFormats);
    assert(target_format_int < (uint32_t)transcoder_texture_format::cTFTotalTextureFormats);

    transcoder_texture_format target_format = static_cast<transcoder_texture_format>(target_format_int);
    basis_tex_format tex_format = static_cast<basis_tex_format>(basis_tex_format_int);

    if (!g_basis_initialized_flag)
    {
#if BASISU_DEBUG_PRINTF
        printf("transcode_uastc_image: basis_init() must be called first\n");
#endif
        assert(0);
        return false;
    }

    // FIXME: Access the JavaScript buffer directly vs. copying it.
    basisu::vector<uint8_t> temp_comp_data;
    copy_from_jsbuffer(compressed_data, temp_comp_data);

    if (!temp_comp_data.size())
    {
#if BASISU_DEBUG_PRINTF
        printf("transcode_uastc_image: compressed_data is empty\n");
#endif
        assert(0);
        return false;
    }

    uint32_t output_blocks_len = output_blocks["byteLength"].as<uint32_t>();
    if (!output_blocks_len)
    {
#if BASISU_DEBUG_PRINTF
        printf("transcode_uastc_image: output_blocks is empty\n");
#endif
        assert(0);
        return false;
    }

#if 0
    printf("format: %u\n", (uint32_t)target_format);
    printf("output_blocks size: %u buf size: %u\n", output_blocks_len, output_blocks_buf_size_in_blocks_or_pixels);
    printf("compressed_data size: %u\n", compressed_data["byteLength"].as<uint32_t>());
    printf("%u %u %u %u %u\n", num_blocks_x, num_blocks_y, orig_width, orig_height, level_index);
    printf("%u %u\n", slice_offset, slice_length);
    printf("%u\n", decode_flags);
    printf("has_alpha: %u is_video: %u\n", has_alpha, is_video);
#endif

    basisu::vector<uint8_t> temp_output_blocks(output_blocks_len);

	bool status = false;
	if (tex_format == basis_tex_format::cUASTC_HDR_4x4)
	{
		basisu_lowlevel_uastc_hdr_4x4_transcoder transcoder;

    	status = transcoder.transcode_image(
        	(transcoder_texture_format)target_format,
        	&temp_output_blocks[0], output_blocks_buf_size_in_blocks_or_pixels,
        	&temp_comp_data[0], temp_comp_data.size(),
        	num_blocks_x, num_blocks_y, orig_width, orig_height, level_index,
        	slice_offset, slice_length,
        	decode_flags,
        	has_alpha,
        	is_video,
        	output_row_pitch_in_blocks_or_pixels,
        	nullptr,
        	output_rows_in_pixels,
        	channel0, channel1);
	}
    else if (tex_format == basis_tex_format::cASTC_HDR_6x6)
    {
        basisu_lowlevel_astc_hdr_6x6_transcoder transcoder;

        status = transcoder.transcode_image(
            (transcoder_texture_format)target_format,
            &temp_output_blocks[0], output_blocks_buf_size_in_blocks_or_pixels,
            &temp_comp_data[0], temp_comp_data.size(),
            num_blocks_x, num_blocks_y, orig_width, orig_height, level_index,
            slice_offset, slice_length,
            decode_flags,
            has_alpha,
            is_video,
            output_row_pitch_in_blocks_or_pixels,
            nullptr,
            output_rows_in_pixels,
            channel0, channel1);
    }
    else if (tex_format == basis_tex_format::cASTC_HDR_6x6_INTERMEDIATE)
    {
        basisu_lowlevel_astc_hdr_6x6_intermediate_transcoder transcoder;

        status = transcoder.transcode_image(
            (transcoder_texture_format)target_format,
            &temp_output_blocks[0], output_blocks_buf_size_in_blocks_or_pixels,
            &temp_comp_data[0], temp_comp_data.size(),
            num_blocks_x, num_blocks_y, orig_width, orig_height, level_index,
            slice_offset, slice_length,
            decode_flags,
            has_alpha,
            is_video,
            output_row_pitch_in_blocks_or_pixels,
            nullptr,
            output_rows_in_pixels,
            channel0, channel1);
    }
	else if (tex_format == basis_tex_format::cUASTC4x4)
	{
    	basisu_lowlevel_uastc_ldr_4x4_transcoder transcoder;

    	status = transcoder.transcode_image(
        	(transcoder_texture_format)target_format,
        	&temp_output_blocks[0], output_blocks_buf_size_in_blocks_or_pixels,
        	&temp_comp_data[0], temp_comp_data.size(),
        	num_blocks_x, num_blocks_y, orig_width, orig_height, level_index,
        	slice_offset, slice_length,
        	decode_flags,
        	has_alpha,
        	is_video,
        	output_row_pitch_in_blocks_or_pixels,
        	nullptr,
        	output_rows_in_pixels,
        	channel0, channel1);
	}
    else
    {
        assert(0);
    }

    if (!status)
    {
#if BASISU_DEBUG_PRINTF
        printf("transcode_uastc_image: basisu_lowlevel_uastc_transcoder::transcode_image failed\n");
#endif
        assert(0);
        return false;
    }

    if (!copy_to_jsbuffer(output_blocks, temp_output_blocks))
        return false;

    return true;
}

uint32_t get_bytes_per_block_or_pixel(uint32_t transcoder_tex_fmt)
{
    assert(transcoder_tex_fmt < (uint32_t)transcoder_texture_format::cTFTotalTextureFormats);
    return basis_get_bytes_per_block_or_pixel(static_cast<transcoder_texture_format>(transcoder_tex_fmt));
}

bool format_has_alpha(uint32_t transcoder_tex_fmt)
{
    assert(transcoder_tex_fmt < (uint32_t)transcoder_texture_format::cTFTotalTextureFormats);
    return basis_transcoder_format_has_alpha(static_cast<transcoder_texture_format>(transcoder_tex_fmt));
}

bool format_is_hdr(uint32_t transcode_tex_fmt)
{
    assert(transcode_tex_fmt < (uint32_t)transcoder_texture_format::cTFTotalTextureFormats);
	return basis_transcoder_format_is_hdr(static_cast<transcoder_texture_format>(transcode_tex_fmt));
}

bool format_is_ldr(uint32_t transcode_tex_fmt)
{
    assert(transcode_tex_fmt < (uint32_t)transcoder_texture_format::cTFTotalTextureFormats);
    return !basis_transcoder_format_is_hdr(static_cast<transcoder_texture_format>(transcode_tex_fmt));
}

bool format_is_uncompressed(uint32_t transcoder_tex_fmt)
{
    assert(transcoder_tex_fmt < (uint32_t)transcoder_texture_format::cTFTotalTextureFormats);
    return basis_transcoder_format_is_uncompressed(static_cast<transcoder_texture_format>(transcoder_tex_fmt));
}

bool is_format_supported(uint32_t transcoder_tex_fmt, uint32_t file_fmt)
{
    assert(transcoder_tex_fmt < (uint32_t)transcoder_texture_format::cTFTotalTextureFormats);
    return basis_is_format_supported(static_cast<transcoder_texture_format>(transcoder_tex_fmt), static_cast<basis_tex_format>(file_fmt));
}

// transcoder_texture_format
uint32_t get_format_block_width(uint32_t transcoder_tex_fmt)
{
    assert(transcoder_tex_fmt < (uint32_t)transcoder_texture_format::cTFTotalTextureFormats);
    return basis_get_block_width(static_cast<transcoder_texture_format>(transcoder_tex_fmt));
}

// transcoder_texture_format
uint32_t get_format_block_height(uint32_t transcoder_tex_fmt)
{
    assert(transcoder_tex_fmt < (uint32_t)transcoder_texture_format::cTFTotalTextureFormats);
    return basis_get_block_height(static_cast<transcoder_texture_format>(transcoder_tex_fmt));
}

// basis_tex_format
uint32_t get_basis_tex_format_block_width(uint32_t fmt)
{
    assert(fmt < (uint32_t)basis_tex_format::cTotalFormats);
    return basis_tex_format_get_block_width(static_cast<basis_tex_format>(fmt));
}

// basis_tex_format
uint32_t get_basis_tex_format_block_height(uint32_t fmt)
{
    assert(fmt < (uint32_t)basis_tex_format::cTotalFormats);
    return basis_tex_format_get_block_height(static_cast<basis_tex_format>(fmt));
}

// basis_tex_format
bool is_basis_tex_format_hdr(uint32_t fmt)
{
    assert(fmt < (uint32_t)basis_tex_format::cTotalFormats);
    return ((basis_tex_format)fmt == basis_tex_format::cUASTC_HDR_4x4) || ((basis_tex_format)fmt == basis_tex_format::cASTC_HDR_6x6) || ((basis_tex_format)fmt == basis_tex_format::cASTC_HDR_6x6_INTERMEDIATE);
}

// basis_tex_format
bool is_basis_tex_format_ldr(uint32_t fmt)
{
    assert(fmt < (uint32_t)basis_tex_format::cTotalFormats);
    return ((basis_tex_format)fmt == basis_tex_format::cETC1S) || ((basis_tex_format)fmt == basis_tex_format::cUASTC4x4);
}

uint32_t convert_float_to_half(float f)
{
	return basist::float_to_half(f);
}

float convert_half_to_float(uint32_t h)
{
	assert(h < 65536);
	return basist::half_to_float(h);
}

void set_debug_flags_wrapper(uint32_t flags)
{
	basist::set_debug_flags(flags);
}

uint32_t get_debug_flags_wrapper()
{
	return basist::get_debug_flags();
}

EMSCRIPTEN_BINDINGS(basis_codec) {
  function("initializeBasis", &basis_init);

  function("setDebugFlags", &set_debug_flags_wrapper);
  function("getDebugFlags", &get_debug_flags_wrapper);

  // Expose BasisFileDesc structure
  value_object<basis_file_desc>("BasisFileDesc")
      .field("version", &basis_file_desc::m_version)
      .field("usPerFrame", &basis_file_desc::m_us_per_frame)
      .field("totalImages", &basis_file_desc::m_total_images)
      .field("userdata0", &basis_file_desc::m_userdata0)
      .field("userdata1", &basis_file_desc::m_userdata1)
      .field("texFormat", &basis_file_desc::m_tex_format)
      .field("yFlipped", &basis_file_desc::m_y_flipped)
      .field("hasAlphaSlices", &basis_file_desc::m_has_alpha_slices)
      .field("numEndpoints", &basis_file_desc::m_num_endpoints)
      .field("endpointPaletteOfs", &basis_file_desc::m_endpoint_palette_ofs)
      .field("endpointPaletteLen", &basis_file_desc::m_endpoint_palette_len)
      .field("numSelectors", &basis_file_desc::m_num_selectors)
      .field("selectorPaletteOfs", &basis_file_desc::m_selector_palette_ofs)
      .field("selectorPaletteLen", &basis_file_desc::m_selector_palette_len)
      .field("tablesOfs", &basis_file_desc::m_tables_ofs)
      .field("tablesLen", &basis_file_desc::m_tables_len)
      .field("blockWidth", &basis_file_desc::m_block_width)
      .field("blockHeight", &basis_file_desc::m_block_height)
    ;

  // Expose BasisImageDesc structure
  value_object<basis_image_desc>("BasisImageDesc")
    .field("origWidth", &basis_image_desc::m_orig_width)
    .field("origHeight", &basis_image_desc::m_orig_height)
    .field("numBlocksX", &basis_image_desc::m_num_blocks_x)
    .field("numBlocksY", &basis_image_desc::m_num_blocks_y)
    .field("numLevels", &basis_image_desc::m_num_levels)
    .field("blockWidth", &basis_image_desc::m_block_width)
    .field("blockHeight", &basis_image_desc::m_block_height)
    .field("alphaFlag", &basis_image_desc::m_alpha_flag)
    .field("iframeFlag", &basis_image_desc::m_iframe_flag)
    ;

  // Expose BasisImageLevelDesc structure
  value_object<basis_image_level_desc>("BasisImageLevelDesc")
    .field("rgbFileOfs", &basis_image_level_desc::m_rgb_file_ofs)
    .field("rgbFileLen", &basis_image_level_desc::m_rgb_file_len)
    .field("alphaFileOfs", &basis_image_level_desc::m_alpha_file_ofs)
    .field("alphaFileLen", &basis_image_level_desc::m_alpha_file_len)
    ;

  // Expose some key enums to JavaScript code.

  // enum class transcoder_texture_format
  enum_<transcoder_texture_format>("transcoder_texture_format")
        .value("cTFETC1_RGB", transcoder_texture_format::cTFETC1_RGB)
        .value("cTFETC2_RGBA", transcoder_texture_format::cTFETC2_RGBA)
        .value("cTFBC1_RGB", transcoder_texture_format::cTFBC1_RGB)
        .value("cTFBC3_RGBA", transcoder_texture_format::cTFBC3_RGBA)
        .value("cTFBC4_R", transcoder_texture_format::cTFBC4_R)
        .value("cTFBC5_RG", transcoder_texture_format::cTFBC5_RG)
        .value("cTFBC7_RGBA", transcoder_texture_format::cTFBC7_RGBA)
        .value("cTFPVRTC1_4_RGB", transcoder_texture_format::cTFPVRTC1_4_RGB)
        .value("cTFPVRTC1_4_RGBA", transcoder_texture_format::cTFPVRTC1_4_RGBA)
        .value("cTFASTC_4x4_RGBA", transcoder_texture_format::cTFASTC_4x4_RGBA)
        .value("cTFATC_RGB", transcoder_texture_format::cTFATC_RGB)
        .value("cTFATC_RGBA", transcoder_texture_format::cTFATC_RGBA)
        .value("cTFFXT1_RGB", transcoder_texture_format::cTFFXT1_RGB)
        .value("cTFPVRTC2_4_RGB", transcoder_texture_format::cTFPVRTC2_4_RGB)
        .value("cTFPVRTC2_4_RGBA", transcoder_texture_format::cTFPVRTC2_4_RGBA)
        .value("cTFETC2_EAC_R11", transcoder_texture_format::cTFETC2_EAC_R11)
        .value("cTFETC2_EAC_RG11", transcoder_texture_format::cTFETC2_EAC_RG11)
		.value("cTFBC6H", transcoder_texture_format::cTFBC6H)
		.value("cTFASTC_HDR_4x4_RGBA", transcoder_texture_format::cTFASTC_HDR_4x4_RGBA)
        .value("cTFRGBA32", transcoder_texture_format::cTFRGBA32)
        .value("cTFRGB565", transcoder_texture_format::cTFRGB565)
        .value("cTFBGR565", transcoder_texture_format::cTFBGR565)
        .value("cTFRGBA4444", transcoder_texture_format::cTFRGBA4444)
		.value("cTFRGB_HALF", transcoder_texture_format::cTFRGB_HALF)
		.value("cTFRGBA_HALF", transcoder_texture_format::cTFRGBA_HALF)
		.value("cTFRGB_9E5", transcoder_texture_format::cTFRGB_9E5)
        .value("cTFASTC_HDR_6x6_RGBA", transcoder_texture_format::cTFASTC_HDR_6x6_RGBA)
        .value("cTFTotalTextureFormats", transcoder_texture_format::cTFTotalTextureFormats)
    ;

    // Expose some useful transcoder_texture_format helper functions
    function("getBytesPerBlockOrPixel", &::get_bytes_per_block_or_pixel);
    function("formatHasAlpha", &format_has_alpha);
	function("formatIsHDR", &format_is_hdr);
    function("formatIsLDR", &format_is_ldr);
    function("formatIsUncompressed", &format_is_uncompressed);
    function("isFormatSupported", &is_format_supported);
    function("getFormatBlockWidth", &get_format_block_width);
    function("getFormatBlockHeight", &get_format_block_height);

    function("getBasisTexFormatBlockWidth", &get_basis_tex_format_block_width);
    function("getBasisTexFormatBlockHeight", &get_basis_tex_format_block_height);

    function("isBasisTexFormatHDR", &is_basis_tex_format_hdr);
    function("isBasisTexFormatLDR", &is_basis_tex_format_ldr);

	function("convertFloatToHalf", &convert_float_to_half);
	function("convertHalfToFloat", &convert_half_to_float);

    // Expose enum basis_texture_type
    enum_<basis_texture_type>("basis_texture_type")
    .value("cBASISTexType2D", cBASISTexType2D)
        .value("cBASISTexType2DArray", cBASISTexType2DArray)
        .value("cBASISTexTypeCubemapArray", cBASISTexTypeCubemapArray)
        .value("cBASISTexTypeVideoFrames", cBASISTexTypeVideoFrames)
        .value("cBASISTexTypeVolume", cBASISTexTypeVolume)
    ;

    // Expose enum basis_tex_format
    enum_<basis_tex_format>("basis_tex_format")
        .value("cETC1S", basis_tex_format::cETC1S)
        .value("cUASTC4x4", basis_tex_format::cUASTC4x4)
        .value("cUASTC_HDR_4x4", basis_tex_format::cUASTC_HDR_4x4)
        .value("cASTC_HDR_6x6", basis_tex_format::cASTC_HDR_6x6)
        .value("cASTC_HDR_6x6_INTERMEDIATE", basis_tex_format::cASTC_HDR_6x6_INTERMEDIATE)
    ;

  // .basis file transcoder object. If all you want to do is transcode already encoded .basis files, this is all you really need.
  class_<basis_file>("BasisFile")
    .constructor<const emscripten::val&>()
    .function("close", optional_override([](basis_file& self) {
      return self.close();
    }))
    .function("getHasAlpha", optional_override([](basis_file& self) {
      return self.getHasAlpha();
    }))
    .function("getBasisTexFormat", optional_override([](basis_file& self) {
      return self.getBasisTexFormat();
    }))
    .function("isUASTC_LDR_4x4", optional_override([](basis_file& self) {
      return self.isUASTC_LDR_4x4();
    }))
    .function("isUASTC", optional_override([](basis_file& self) {
      return self.isUASTC_LDR_4x4();
    }))
    .function("isETC1S", optional_override([](basis_file& self) {
      return self.isETC1S();
    }))
	.function("isHDR", optional_override([](basis_file& self) {
      return self.isHDR();
    }))
    .function("isHDR4x4", optional_override([](basis_file& self) {
      return self.isHDR4x4();
    }))
    .function("isHDR6x6", optional_override([](basis_file& self) {
      return self.isHDR6x6();
    }))
      .function("isLDR", optional_override([](basis_file& self) {
      return self.isLDR();
    }))
    .function("getNumImages", optional_override([](basis_file& self) {
      return self.getNumImages();
    }))
    .function("getNumLevels", optional_override([](basis_file& self, uint32_t imageIndex) {
      return self.getNumLevels(imageIndex);
    }))
    .function("getImageWidth", optional_override([](basis_file& self, uint32_t imageIndex, uint32_t levelIndex) {
      return self.getImageWidth(imageIndex, levelIndex);
    }))
    .function("getImageHeight", optional_override([](basis_file& self, uint32_t imageIndex, uint32_t levelIndex) {
      return self.getImageHeight(imageIndex, levelIndex);
    }))
    .function("getBlockWidth", optional_override([](basis_file& self) {
      return self.getBlockWidth();
    }))
    .function("getBlockHeight", optional_override([](basis_file& self) {
      return self.getBlockHeight();
    }))
    // format is enum class transcoder_texture_format
    .function("getImageTranscodedSizeInBytes", optional_override([](basis_file& self, uint32_t imageIndex, uint32_t levelIndex, uint32_t format) {
      return self.getImageTranscodedSizeInBytes(imageIndex, levelIndex, format);
    }))
    .function("startTranscoding", optional_override([](basis_file& self) {
      return self.startTranscoding();
    }))
    // format is enum class transcoder_texture_format
    .function("transcodeImage", optional_override([](basis_file& self, const emscripten::val& dst, uint32_t imageIndex, uint32_t levelIndex, uint32_t format, uint32_t unused, uint32_t getAlphaForOpaqueFormats) {
      return self.transcodeImage(dst, imageIndex, levelIndex, format, unused, getAlphaForOpaqueFormats);
    }))
    .function("transcodeImageWithFlags", optional_override([](basis_file& self, const emscripten::val& dst, uint32_t imageIndex, uint32_t levelIndex, uint32_t format, uint32_t flags) {
      return self.transcodeImageWithFlags(dst, imageIndex, levelIndex, format, flags);
    }))
    // Returns low-level information about the basis file.
    .function("getFileDesc", optional_override([](basis_file& self) {
      return self.getFileDesc();
    }))
    // Returns low-level information about a specific image in a basis file. An image can contain 1 or more mipmap levels.
    .function("getImageDesc", optional_override([](basis_file& self, uint32_t imageIndex) {
      return self.getImageDesc(imageIndex);
    }))
    // Returns low-level information about a specific image mipmap level in a basis file.
    .function("getImageLevelDesc", optional_override([](basis_file& self, uint32_t imageIndex, uint32_t levelIndex) {
      return self.getImageLevelDesc(imageIndex, levelIndex);
    }))

  ;

  // Low-level container independent transcoding of ETC1S and UASTC slice data.
  // These functions allow the caller to transcode compressed ETC1S or UASTC texture data that is embedded within arbitrary data files, such as from KTX2.
  enum_<basisu_decode_flags>("basisu_decode_flags")
    .value("cDecodeFlagsPVRTCDecodeToNextPow2", cDecodeFlagsPVRTCDecodeToNextPow2)
    .value("cDecodeFlagsTranscodeAlphaDataToOpaqueFormats", cDecodeFlagsTranscodeAlphaDataToOpaqueFormats)
    .value("cDecodeFlagsBC1ForbidThreeColorBlocks", cDecodeFlagsBC1ForbidThreeColorBlocks)
    .value("cDecodeFlagsOutputHasAlphaIndices", cDecodeFlagsOutputHasAlphaIndices)
    .value("cDecodeFlagsHighQuality", cDecodeFlagsHighQuality)
    .value("cDecodeFlagsNoETC1SChromaFiltering", cDecodeFlagsNoETC1SChromaFiltering)
  ;

  // The low-level ETC1S transcoder is a class because it has persistent state (such as the endpoint/selector codebooks and Huffman tables, and transcoder state for video)
  class_<lowlevel_etc1s_image_transcoder>("LowLevelETC1SImageTranscoder")
    .constructor<>()
    .function("decodePalettes", &lowlevel_etc1s_image_transcoder::decode_palettes)
    .function("decodeTables", &lowlevel_etc1s_image_transcoder::decode_tables)
    .function("transcodeImage", &lowlevel_etc1s_image_transcoder::transcode_image)
   ;

  // The low-level UASTC transcoder (for UASTC LDR 4x4, HDR 4x4, or ASTC HDR 6x6) is a single function.
  function("transcodeUASTCImage", &transcode_uastc_image);

  function("transcoderSupportsKTX2", &basisu_transcoder_supports_ktx2);
  function("transcoderSupportsKTX2Zstd", &basisu_transcoder_supports_ktx2_zstd);

#if BASISD_SUPPORT_KTX2
  // KTX2 enums/constants
  enum_<ktx2_supercompression>("ktx2_supercompression")
      .value("KTX2_SS_NONE", KTX2_SS_NONE)
      .value("KTX2_SS_BASISLZ", KTX2_SS_BASISLZ)
      .value("KTX2_SS_ZSTANDARD", KTX2_SS_ZSTANDARD)
      ;

  constant("KTX2_VK_FORMAT_UNDEFINED", KTX2_VK_FORMAT_UNDEFINED);
  constant("KTX2_KDF_DF_MODEL_UASTC", KTX2_KDF_DF_MODEL_UASTC_LDR_4X4);
  constant("KTX2_KDF_DF_MODEL_ETC1S", KTX2_KDF_DF_MODEL_ETC1S);
  constant("KTX2_KDF_DF_MODEL_ASTC_HDR_6X6_INTERMEDIATE", KTX2_KDF_DF_MODEL_ASTC_HDR_6X6_INTERMEDIATE);

  constant("KTX2_IMAGE_IS_P_FRAME", KTX2_IMAGE_IS_P_FRAME);
  constant("KTX2_UASTC_BLOCK_SIZE", KTX2_UASTC_BLOCK_SIZE);
  constant("KTX2_MAX_SUPPORTED_LEVEL_COUNT", KTX2_MAX_SUPPORTED_LEVEL_COUNT);

  constant("KTX2_KHR_DF_TRANSFER_LINEAR", KTX2_KHR_DF_TRANSFER_LINEAR);
  constant("KTX2_KHR_DF_TRANSFER_SRGB", KTX2_KHR_DF_TRANSFER_SRGB);

  enum_<ktx2_df_channel_id>("ktx2_df_channel_id")
      .value("KTX2_DF_CHANNEL_ETC1S_RGB", KTX2_DF_CHANNEL_ETC1S_RGB)
      .value("KTX2_DF_CHANNEL_ETC1S_RRR", KTX2_DF_CHANNEL_ETC1S_RRR)
      .value("KTX2_DF_CHANNEL_ETC1S_GGG", KTX2_DF_CHANNEL_ETC1S_GGG)
      .value("KTX2_DF_CHANNEL_ETC1S_AAA", KTX2_DF_CHANNEL_ETC1S_AAA)
      .value("KTX2_DF_CHANNEL_UASTC_DATA", KTX2_DF_CHANNEL_UASTC_DATA)
      .value("KTX2_DF_CHANNEL_UASTC_RGB", KTX2_DF_CHANNEL_UASTC_RGB)
      .value("KTX2_DF_CHANNEL_UASTC_RGBA", KTX2_DF_CHANNEL_UASTC_RGBA)
      .value("KTX2_DF_CHANNEL_UASTC_RRR", KTX2_DF_CHANNEL_UASTC_RRR)
      .value("KTX2_DF_CHANNEL_UASTC_RRRG", KTX2_DF_CHANNEL_UASTC_RRRG)
      .value("KTX2_DF_CHANNEL_UASTC_RG", KTX2_DF_CHANNEL_UASTC_RG)
      ;

  enum_<ktx2_df_color_primaries>("ktx2_df_color_primaries")
      .value("KTX2_DF_PRIMARIES_UNSPECIFIED", KTX2_DF_PRIMARIES_UNSPECIFIED)
      .value("KTX2_DF_PRIMARIES_BT709", KTX2_DF_PRIMARIES_BT709)
      .value("KTX2_DF_PRIMARIES_SRGB", KTX2_DF_PRIMARIES_SRGB)
      .value("KTX2_DF_PRIMARIES_BT601_EBU", KTX2_DF_PRIMARIES_BT601_EBU)
      .value("KTX2_DF_PRIMARIES_BT601_SMPTE", KTX2_DF_PRIMARIES_BT601_SMPTE)
      .value("KTX2_DF_PRIMARIES_BT2020", KTX2_DF_PRIMARIES_BT2020)
      .value("KTX2_DF_PRIMARIES_CIEXYZ", KTX2_DF_PRIMARIES_CIEXYZ)
      .value("KTX2_DF_PRIMARIES_ACES", KTX2_DF_PRIMARIES_ACES)
      .value("KTX2_DF_PRIMARIES_ACESCC", KTX2_DF_PRIMARIES_ACESCC)
      .value("KTX2_DF_PRIMARIES_NTSC1953", KTX2_DF_PRIMARIES_NTSC1953)
      .value("KTX2_DF_PRIMARIES_PAL525", KTX2_DF_PRIMARIES_PAL525)
      .value("KTX2_DF_PRIMARIES_DISPLAYP3", KTX2_DF_PRIMARIES_DISPLAYP3)
      .value("KTX2_DF_PRIMARIES_ADOBERGB", KTX2_DF_PRIMARIES_ADOBERGB)
      ;

  // Expose ktx2_image_level_info structure
  value_object<ktx2_image_level_info>("KTX2ImageLevelInfo")
      .field("levelIndex", &ktx2_image_level_info::m_level_index)
      .field("layerIndex", &ktx2_image_level_info::m_layer_index)
      .field("faceIndex", &ktx2_image_level_info::m_face_index)
      .field("origWidth", &ktx2_image_level_info::m_orig_width)
      .field("origHeight", &ktx2_image_level_info::m_orig_height)
      .field("width", &ktx2_image_level_info::m_width)
      .field("height", &ktx2_image_level_info::m_height)
      .field("numBlocksX", &ktx2_image_level_info::m_num_blocks_x)
      .field("numBlocksY", &ktx2_image_level_info::m_num_blocks_y)
      .field("blockWidth", &ktx2_image_level_info::m_block_width)
      .field("blockHeight", &ktx2_image_level_info::m_block_height)
      .field("totalBlocks", &ktx2_image_level_info::m_total_blocks)
      .field("alphaFlag", &ktx2_image_level_info::m_alpha_flag)
      .field("iframeFlag", &ktx2_image_level_info::m_iframe_flag)
      ;

  // Expose the ktx2_header_js structure
  value_object<ktx2_header_js>("KTX2Header")
      .field("vkFormat", &ktx2_header_js::m_vk_format)
      .field("typeSize", &ktx2_header_js::m_type_size)
      .field("pixelWidth", &ktx2_header_js::m_pixel_width)
      .field("pixelHeight", &ktx2_header_js::m_pixel_height)
      .field("pixelDepth", &ktx2_header_js::m_pixel_depth)
      .field("layerCount", &ktx2_header_js::m_layer_count)
      .field("faceCount", &ktx2_header_js::m_face_count)
      .field("levelCount", &ktx2_header_js::m_level_count)
      .field("supercompressionScheme", &ktx2_header_js::m_supercompression_scheme)
      .field("dfdByteOffset", &ktx2_header_js::m_dfd_byte_offset)
      .field("dfdByteLength", &ktx2_header_js::m_dfd_byte_length)
      .field("kvdByteOffset", &ktx2_header_js::m_kvd_byte_offset)
      .field("kvdByteLength", &ktx2_header_js::m_kvd_byte_length)
      .field("sgdByteOffset", &ktx2_header_js::m_sgd_byte_offset)
      .field("sgdByteLength", &ktx2_header_js::m_sgd_byte_length)
      ;

  // KTX2 transcoder class
  class_<ktx2_file>("KTX2File")
      .constructor<const emscripten::val&>()
        .function("isValid", &ktx2_file::isValid)
        .function("close", &ktx2_file::close)
        .function("getDFDSize", &ktx2_file::getDFDSize)
        .function("getDFD", &ktx2_file::getDFD)
        .function("getHeader", &ktx2_file::getHeader)
        .function("hasKey", &ktx2_file::hasKey)
        .function("getTotalKeys", &ktx2_file::getTotalKeys)
        .function("getKey", &ktx2_file::getKey)
        .function("getKeyValueSize", &ktx2_file::getKeyValueSize)
        .function("getKeyValue", &ktx2_file::getKeyValue)
        .function("getWidth", &ktx2_file::getWidth)
        .function("getHeight", &ktx2_file::getHeight)
        .function("getBlockWidth", &ktx2_file::getBlockWidth)
        .function("getBlockHeight", &ktx2_file::getBlockHeight)
        .function("getFaces", &ktx2_file::getFaces)
        .function("getLayers", &ktx2_file::getLayers)
        .function("getLevels", &ktx2_file::getLevels)
        .function("getBasisTexFormat", &ktx2_file::getBasisTexFormat)
        .function("isUASTC_LDR_4x4", &ktx2_file::isUASTC_LDR_4x4)
        .function("isUASTC", &ktx2_file::isUASTC_LDR_4x4)
        .function("isHDR", &ktx2_file::isHDR)
        .function("isHDR4x4", &ktx2_file::isHDR4x4)
        .function("isHDR6x6", &ktx2_file::isHDR6x6)
        .function("isLDR", &ktx2_file::isLDR)
        .function("isETC1S", &ktx2_file::isETC1S)
        .function("getHasAlpha", &ktx2_file::getHasAlpha)
        .function("getDFDColorModel", &ktx2_file::getDFDColorModel)
        .function("getDFDColorPrimaries", &ktx2_file::getDFDColorPrimaries)
        .function("getDFDTransferFunc", &ktx2_file::getDFDTransferFunc)
        .function("getDFDFlags", &ktx2_file::getDFDFlags)
        .function("getDFDTotalSamples", &ktx2_file::getDFDTotalSamples)
        .function("getDFDChannelID0", &ktx2_file::getDFDChannelID0)
        .function("getDFDChannelID1", &ktx2_file::getDFDChannelID1)
        .function("isVideo", &ktx2_file::isVideo)
        .function("getLDRHDRUpconversionNitMultiplier", &ktx2_file::getLDRHDRUpconversionNitMultiplier)
        .function("getETC1SImageDescImageFlags", &ktx2_file::getETC1SImageDescImageFlags)
        .function("getImageLevelInfo", &ktx2_file::getImageLevelInfo)
        .function("getImageTranscodedSizeInBytes", &ktx2_file::getImageTranscodedSizeInBytes)
        .function("startTranscoding", &ktx2_file::startTranscoding)
        .function("transcodeImage", &ktx2_file::transcodeImage)
        .function("transcodeImageWithFlags", &ktx2_file::transcodeImageWithFlags)
      ;

#endif // BASISD_SUPPORT_KTX2

  // Optional encoding/compression support of .basis and .KTX2 files (the same class encodes/compresses to either format).

#if BASISU_SUPPORT_ENCODING

    // Compressor Constants

    constant("BASISU_MAX_SUPPORTED_TEXTURE_DIMENSION", BASISU_MAX_SUPPORTED_TEXTURE_DIMENSION);
    constant("BASISU_DEFAULT_ENDPOINT_RDO_THRESH", BASISU_DEFAULT_ENDPOINT_RDO_THRESH);
    constant("BASISU_DEFAULT_SELECTOR_RDO_THRESH", BASISU_DEFAULT_SELECTOR_RDO_THRESH);
    constant("BASISU_DEFAULT_QUALITY", BASISU_DEFAULT_QUALITY);
    constant("BASISU_DEFAULT_HYBRID_SEL_CB_QUALITY_THRESH", BASISU_DEFAULT_HYBRID_SEL_CB_QUALITY_THRESH);
    constant("BASISU_MAX_IMAGE_DIMENSION", BASISU_MAX_IMAGE_DIMENSION);
    constant("BASISU_QUALITY_MIN", BASISU_QUALITY_MIN);
    constant("BASISU_QUALITY_MAX", BASISU_QUALITY_MAX);
    constant("BASISU_MAX_ENDPOINT_CLUSTERS", BASISU_MAX_ENDPOINT_CLUSTERS);
    constant("BASISU_MAX_SELECTOR_CLUSTERS", BASISU_MAX_SELECTOR_CLUSTERS);
    constant("BASISU_MAX_SLICES", BASISU_MAX_SLICES);
    constant("BASISU_RDO_UASTC_DICT_SIZE_DEFAULT", BASISU_RDO_UASTC_DICT_SIZE_DEFAULT);
    constant("BASISU_RDO_UASTC_DICT_SIZE_MIN", BASISU_RDO_UASTC_DICT_SIZE_MIN);
    constant("BASISU_RDO_UASTC_DICT_SIZE_MAX", BASISU_RDO_UASTC_DICT_SIZE_MAX);
    constant("BASISU_MAX_RESAMPLER_FILTERS", g_num_resample_filters);
    constant("BASISU_DEFAULT_COMPRESSION_LEVEL", BASISU_DEFAULT_COMPRESSION_LEVEL);
    constant("BASISU_MAX_COMPRESSION_LEVEL", BASISU_MAX_COMPRESSION_LEVEL);

	// The maximum representable floating point value in a UASTC HDR or ASTC HDR texture (any larger values will get clamped and a warning issued).
	constant("ASTC_HDR_MAX_VAL", basist::ASTC_HDR_MAX_VAL);

    // UASTC LDR/HDR flags/options
    constant("cPackUASTCLevelFastest", cPackUASTCLevelFastest);
    constant("cPackUASTCLevelFaster", cPackUASTCLevelFaster);
    constant("cPackUASTCLevelDefault", cPackUASTCLevelDefault);
    constant("cPackUASTCLevelSlower", cPackUASTCLevelSlower);
    constant("cPackUASTCLevelVerySlow", cPackUASTCLevelVerySlow);
    constant("cPackUASTCLevelMask", cPackUASTCLevelMask);
    constant("cPackUASTCFavorUASTCError", cPackUASTCFavorUASTCError);
    constant("cPackUASTCFavorBC7Error", cPackUASTCFavorBC7Error);
    constant("cPackUASTCETC1FasterHints", cPackUASTCETC1FasterHints);
    constant("cPackUASTCETC1FastestHints", cPackUASTCETC1FastestHints);
    constant("cPackUASTCETC1DisableFlipAndIndividual", cPackUASTCETC1DisableFlipAndIndividual);

    constant("UASTC_RDO_DEFAULT_MAX_ALLOWED_RMS_INCREASE_RATIO", UASTC_RDO_DEFAULT_MAX_ALLOWED_RMS_INCREASE_RATIO);
    constant("UASTC_RDO_DEFAULT_SKIP_BLOCK_RMS_THRESH", UASTC_RDO_DEFAULT_SKIP_BLOCK_RMS_THRESH);

    constant("cPackASTC6x6MaxUserCompLevel", ::astc_6x6_hdr::ASTC_HDR_6X6_MAX_USER_COMP_LEVEL);

	enum_<hdr_image_type>("hdr_image_type")
		.value("cHITRGBAHalfFloat", hdr_image_type::cHITRGBAHalfFloat)
		.value("cHITRGBAFloat", hdr_image_type::cHITRGBAFloat)
		.value("cHITPNGImage", hdr_image_type::cHITPNGImage)
		.value("cHITEXRImage", hdr_image_type::cHITEXRImage)
		.value("cHITHDRImage", hdr_image_type::cHITHDRImage)
        .value("cHITJPGImage", hdr_image_type::cHITJPGImage)
	;

    enum_<ldr_image_type>("ldr_image_type")
        .value("cRGBA32", ldr_image_type::cRGBA32)
        .value("cPNGImage", ldr_image_type::cPNGImage)
        .value("cJPGImage", ldr_image_type::cJPGImage)
	;

  // Compression/encoding object.
  // You create this object, call the set() methods to fill in the parameters/source images/options, call encode(), and you get back a .basis or .KTX2 file.
  // You can call .encode() multiple times, changing the parameters/options in between calls.
  // By default this class encodes to .basis, but call setCreateKTX2File() with true to get .KTX2 files.
  class_<basis_encoder>("BasisEncoder")
    .constructor<>()

    // Compresses the provided source slice(s) to an output .basis file.
    // At the minimum, you must provided at least 1 source slice by calling setSliceSourceImage() or setSliceSourceImageHDR() (for UASTC HDR) before calling this method.
    .function("encode", optional_override([](basis_encoder& self, const emscripten::val& dst_basis_file_js_val) {
        return self.encode(dst_basis_file_js_val);
    }))

    // Sets the slice's source image, either from a PNG/JPG file or from a raw 32-bit RGBA raster image.
    // If the input is a raster image, the buffer must be width*height*4 bytes in size. The raster image is stored in top down scanline order.
    // The first texel is the top-left texel. The texel byte order in memory is R,G,B,A (R first at offset 0, A last at offset 3).
    // slice_index is the slice to change. Valid range is [0,BASISU_MAX_SLICES-1].
    // All formats, including HDR formats. The compressor will upconvert LDR/SDR content to absolute light HDR as needed. See LDR upconversion options below.
    .function("setSliceSourceImage", optional_override([](basis_encoder& self, uint32_t slice_index, const emscripten::val& src_image_js_val, uint32_t width, uint32_t height, uint32_t img_type) {
        return self.set_slice_source_image(slice_index, src_image_js_val, width, height, (ldr_image_type)img_type);
    }))

    .function("controlThreading", optional_override([](basis_encoder& self, bool enable_threading, uint32_t num_extra_worker_threads) {
      return self.control_threading(enable_threading, num_extra_worker_threads);
    }))

    // HDR targets only
	.function("setSliceSourceImageHDR", optional_override([](basis_encoder& self, uint32_t slice_index, const emscripten::val& src_image_js_val, uint32_t width, uint32_t height, uint32_t img_type,
        bool ldr_srgb_to_linear_conversion, float ldr_to_hdr_nit_multiplier) {
        return self.set_slice_source_image_hdr(slice_index, src_image_js_val, width, height, (hdr_image_type)img_type, ldr_srgb_to_linear_conversion, ldr_to_hdr_nit_multiplier);
    }))

    // Sets the desired encoding format. This is the preferred way to control which format the encoder creates.
    // tex_format is a basis_tex_format (cETC1s, cUASTC4x4, cUASTC_HDR_4x4 etc.)
    // This can be used instead of the older setUASTC(), setHDR() etc. methods.
    // All formats
    .function("setFormatMode", optional_override([](basis_encoder& self, int tex_format) {
      assert((tex_format >= 0) && (tex_format < (uint32_t)basis_tex_format::cTotalFormats));
      self.m_params.set_format_mode((basis_tex_format)tex_format);
    }))

    // If true, the encoder will output a UASTC LDR 4x4 texture, otherwise a ETC1S texture.
    // (This is for backwards compatibility, prefer setFormatMode() instead.)
    // All formats
    .function("setUASTC", optional_override([](basis_encoder& self, bool uastc_flag) {
      if (uastc_flag)
          self.m_params.set_format_mode(basis_tex_format::cUASTC4x4);
      else
          self.m_params.set_format_mode(basis_tex_format::cETC1S);
    }))

    // If true, the encoder will output a UASTC HDR 4x4 texture, otherwise a ETC1S texture.
    // (This is for backwards compatibility, prefer setFormatMode() instead.)
    // All formats
    .function("setHDR", optional_override([](basis_encoder& self, bool hdr_flag) {
      if (hdr_flag)
          self.m_params.set_format_mode(basis_tex_format::cUASTC_HDR_4x4);
      else
          self.m_params.set_format_mode(basis_tex_format::cETC1S); // don't really know what to set
    }))

	// Sets the UASTC HDR 4x4 quality vs. encoder performance tradeoff (0-4, default is 1). Higher=slower but better quality.
    // UASTC HDR 4x4
    .function("setUASTCHDRQualityLevel", optional_override([](basis_encoder& self, int level) {
		assert((level >= uastc_hdr_4x4_codec_options::cMinLevel) && (level <= uastc_hdr_4x4_codec_options::cMaxLevel));
		self.m_params.m_uastc_hdr_4x4_options.set_quality_level(level);
    }))

    // If true the source images will be Y flipped before compression.
    // All formats
    .function("setYFlip", optional_override([](basis_encoder& self, bool y_flip_flag) {
        self.m_params.m_y_flip = y_flip_flag;
    }))

    // Enables debug output to stdout
    // All formats
    .function("setDebug", optional_override([](basis_encoder& self, bool debug_flag) {
        self.m_params.m_debug = debug_flag;
        g_debug_printf = debug_flag;
    }))

    // If true, the input is assumed to be in sRGB space. Be sure to set this correctly! (Examples: True on photos, albedo/spec maps, and false on normal maps.)
	// In HDR mode, if perceptual is true R and G are weighted higher (2.0, 3.0) than B (1.0). Otherwise the encoder uses equal weightings for each channel.
    // ETC1S, UASTC LDR 4x4, UASTC HDR 4x4
    .function("setPerceptual", optional_override([](basis_encoder& self, bool perceptual_flag) {
        self.m_params.m_perceptual = perceptual_flag;
    }))

    // Check source images for active/used alpha channels
    // ETC1S, UASTC LDR 4x4
    .function("setCheckForAlpha", optional_override([](basis_encoder& self, bool check_for_alpha_flag) {
        self.m_params.m_check_for_alpha = check_for_alpha_flag;
    }))

    // Fource output .basis/.ktx2 file to have an alpha channel
    // ETC1S, UASTC LDR 4x4
    .function("setForceAlpha", optional_override([](basis_encoder& self, bool force_alpha_flag) {
        self.m_params.m_force_alpha = force_alpha_flag;
    }))

    // Set source image component swizzle.
    // r,g,b,a - valid range is [0,3]
    // All formats (but formats without alpha will ignore a)
    .function("setSwizzle", optional_override([](basis_encoder& self, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
        assert((r < 4) && (g < 4) && (b < 4) && (a < 4));
        self.m_params.m_swizzle[0] = (char)r;
        self.m_params.m_swizzle[1] = (char)g;
        self.m_params.m_swizzle[2] = (char)b;
        self.m_params.m_swizzle[3] = (char)a;
    }))

    // ASTC HDR 6x6 options
    // Sets ASTC HDR 6x6/6x6 intermediate lambda setting. The higher this setting, the lower the bitrate.
    // ASTC HDR 6x6
    .function("setLambda", optional_override([](basis_encoder& self, float rdo_quality) {
        self.m_params.m_astc_hdr_6x6_options.m_lambda = rdo_quality;
    }))

    // ASTC HDR 6x6: Enables REC 2020 delta E ITP vs. REC 709 in the  encoder.
    .function("setRec2020", optional_override([](basis_encoder& self, bool rec2020) {
        self.m_params.m_astc_hdr_6x6_options.m_rec2020_bt2100_color_gamut = rec2020;
    }))

    // ASTC HDR 6x6: Enables JND (Just Noticeable Difference) optimization
    .function("setJND", optional_override([](basis_encoder& self, float thresh) {
      if (thresh == 0.0f)
      {
          self.m_params.m_astc_hdr_6x6_options.m_jnd_optimization = false;
      }
      else
      {
          self.m_params.m_astc_hdr_6x6_options.m_jnd_optimization = true;
          self.m_params.m_astc_hdr_6x6_options.m_jnd_delta_itp_thresh = thresh;
      }
    }))

    // ASTC HDR 6x6: Compression speed vs. quality level. Ranges from [0,cPackASTC6x6MaxUserCompLevel].
    .function("setASTC_HDR_6x6_Level", optional_override([](basis_encoder& self, int level) {
        self.m_params.m_astc_hdr_6x6_options.set_user_level(level);
    }))

    // ASTC HDR 6x6: Enable extra 2/3 subset patterns (slower)
    .function("setASTC_HDR_6x6_ExtraPats", optional_override([](basis_encoder& self, bool flag) {
        self.m_params.m_astc_hdr_6x6_options.m_extra_patterns_flag = flag;
    }))

    // ASTC HDR 6x6: Enable brute force pattern searching (slower)
    .function("setASTC_HDR_6x6_BruteForcePats", optional_override([](basis_encoder& self, bool flag) {
        self.m_params.m_astc_hdr_6x6_options.m_brute_force_partition_matching = flag;
    }))

    // ASTC HDR 6x6: Control gaussian filtering on very hard blocks
    .function("setASTC_HDR_6x6_SetGaussian1", optional_override([](basis_encoder& self, float strength) {
        if (strength <= 0.0f)
            self.m_params.m_astc_hdr_6x6_options.m_gaussian1_fallback = false;
        else
        {
            self.m_params.m_astc_hdr_6x6_options.m_gaussian1_fallback = true;
            self.m_params.m_astc_hdr_6x6_options.m_gaussian1_strength = strength;
        }
    }))

    // ASTC HDR 6x6
    .function("setASTC_HDR_6x6_SetGaussian2", optional_override([](basis_encoder& self, float strength) {
        if (strength <= 0.0f)
            self.m_params.m_astc_hdr_6x6_options.m_gaussian2_fallback = false;
        else
        {
            self.m_params.m_astc_hdr_6x6_options.m_gaussian2_fallback = true;
            self.m_params.m_astc_hdr_6x6_options.m_gaussian2_strength = strength;
        }
    }))

    // LDR->HDR upconversion options (all HDR modes)
    // All HDR modes
    .function("setLDR_HDR_Upconversion_sRGBToLinear", optional_override([](basis_encoder& self, bool srgb_to_linear) {
        self.m_params.m_ldr_hdr_upconversion_srgb_to_linear = srgb_to_linear;
    }))

    // All HDR modes
    .function("setLDR_HDR_Upconversion_NitMultiplier", optional_override([](basis_encoder& self, float multiplier) {
        self.m_params.m_ldr_hdr_upconversion_nit_multiplier = multiplier;
    }))

    // If true, the input is assumed to be a normal map, and all source texels will be renormalized before encoding.
    // Impacts LDR images only
    .function("setRenormalize", optional_override([](basis_encoder& self, bool renormalize_flag) {
        self.m_params.m_renormalize = renormalize_flag;
    }))

    // Sets the max # of endpoint clusters for ETC1S mode. Use instead of setQualityLevel.
    // Default is 512, range is [1,BASISU_MAX_ENDPOINT_CLUSTERS]
    // ETC1S mode
    .function("setMaxEndpointClusters", optional_override([](basis_encoder& self, uint32_t max_endpoint_clusters) {
        assert(max_endpoint_clusters <= BASISU_MAX_ENDPOINT_CLUSTERS);
        self.m_params.m_etc1s_max_endpoint_clusters = max_endpoint_clusters;
    }))

    // Sets the max # of selectors clusters for ETC1S mode. Use instead of setQualityLevel.
    // Default is 512, range is [1,BASISU_MAX_ENDPOINT_CLUSTERS]
    // ETC1S mode
    .function("setMaxSelectorClusters", optional_override([](basis_encoder& self, uint32_t max_selector_clusters) {
        assert(max_selector_clusters <= BASISU_MAX_SELECTOR_CLUSTERS);
        self.m_params.m_etc1s_max_selector_clusters = max_selector_clusters;
    }))

    // Sets the ETC1S encoder's quality level, which controls the file size vs. quality tradeoff.
    // Default is -1 (meaning unused - the compressor will use m_max_endpoint_clusters/m_max_selector_clusters instead to control the codebook sizes).
    // Range is [1,BASISU_QUALITY_MAX]
    // ETC1S mode
    .function("setQualityLevel", optional_override([](basis_encoder& self, int quality_level) {
        assert(quality_level >= -1 && quality_level <= BASISU_QUALITY_MAX);
        self.m_params.m_etc1s_quality_level = quality_level;
    }))

    // The compression_level parameter controls the encoder perf vs. file size tradeoff for ETC1S files.
    // It does not directly control file size vs. quality - see quality_level().
    // Default is BASISU_DEFAULT_COMPRESSION_LEVEL, range is [0,BASISU_MAX_COMPRESSION_LEVEL]
    // ETC1S mode
    .function("setCompressionLevel", optional_override([](basis_encoder& self, int comp_level) {
        assert(comp_level >= 0 && comp_level <= BASISU_MAX_COMPRESSION_LEVEL);
        self.m_params.m_compression_level = comp_level;
    }))

    // setNormalMapMode is the same as the basisu.exe "-normal_map" option. It tunes several codec parameters so compression works better on normal maps.
    // ETC1S/UASTC LDR 4x4/UASTC HDR 4x4
    .function("setNormalMap", optional_override([](basis_encoder& self) {
        self.m_params.m_perceptual = false;
        self.m_params.m_mip_srgb = false;
        self.m_params.m_no_selector_rdo = true;
        self.m_params.m_no_endpoint_rdo = true;
    }))

    // Sets ETC1S selector RDO threshold
    // Default is BASISU_DEFAULT_SELECTOR_RDO_THRESH, range is [0,1e+10]
    // ETC1S
    .function("setSelectorRDOThresh", optional_override([](basis_encoder& self, float selector_rdo_thresh) {
        self.m_params.m_selector_rdo_thresh = selector_rdo_thresh;
    }))

    // Sets ETC1S endpoint RDO threshold
    // Default is BASISU_DEFAULT_ENDPOINT_RDO_THRESH, range is [0,1e+10]
    // ETC1S
    .function("setEndpointRDOThresh", optional_override([](basis_encoder& self, float endpoint_rdo_thresh) {
        self.m_params.m_endpoint_rdo_thresh = endpoint_rdo_thresh;
    }))

#if BASISD_SUPPORT_KTX2
    // --- KTX2 related options
    //
    // Create .KTX2 files instead of .basis files. By default this is FALSE.
    // All formats
    .function("setCreateKTX2File", optional_override([](basis_encoder& self, bool create_ktx2_file) {
    self.m_params.m_create_ktx2_file = create_ktx2_file;
        }))

    // KTX2: Use UASTC Zstandard supercompression. Defaults to disabled or KTX2_SS_NONE.
    // Impacts UASTC LDR 4x4, UASTC HDR 4x4, and ASTC HDR 6x6 (but not ASTC HDR 6x6 intermediate)
    .function("setKTX2UASTCSupercompression", optional_override([](basis_encoder& self, bool use_zstandard) {
    self.m_params.m_ktx2_uastc_supercompression = use_zstandard ? basist::KTX2_SS_ZSTANDARD : basist::KTX2_SS_NONE;
        }))

    // KTX2: Use sRGB transfer func in the file's DFD. Default is FALSE. This should very probably match the "perceptual" setting.
    // All formats
    .function("setKTX2SRGBTransferFunc", optional_override([](basis_encoder& self, bool srgb_transfer_func) {
    self.m_params.m_ktx2_srgb_transfer_func = srgb_transfer_func;
        }))

    // TODO: Expose KTX2 key value array, other options to JavaScript. See encoder/basisu_comp.h.
#endif

    // --- Mip-map options (format independent)

    // If true mipmaps will be generated from the source images
    .function("setMipGen", optional_override([](basis_encoder& self, bool mip_gen_flag) {
        self.m_params.m_mip_gen = mip_gen_flag;
    }))

    // Set mipmap filter's scale factor
    // default is 1, range is [.000125, 4.0]
    .function("setMipScale", optional_override([](basis_encoder& self, float mip_scale) {
        self.m_params.m_mip_scale = mip_scale;
    }))

    // Sets the mipmap filter to apply
    // mip_filter must be < BASISU_MAX_RESAMPLER_FILTERS
    // See the end of basisu_resample_filters.cpp: g_resample_filters[]
    .function("setMipFilter", optional_override([](basis_encoder& self, uint32_t mip_filter) {
        assert(mip_filter < g_num_resample_filters);
        if (mip_filter < g_num_resample_filters)
            self.m_params.m_mip_filter = g_resample_filters[mip_filter].name;
    }))

    // If true mipmap filtering occurs in sRGB space - this generally should match the perceptual parameter.
    .function("setMipSRGB", optional_override([](basis_encoder& self, bool mip_srgb_flag) {
        self.m_params.m_mip_srgb = mip_srgb_flag;
    }))

    // If true, the input is assumed to be a normal map, and the texels of each mipmap will be renormalized before encoding.
    .function("setMipRenormalize", optional_override([](basis_encoder& self, bool mip_renormalize_flag) {
        self.m_params.m_mip_renormalize = mip_renormalize_flag;
    }))

    // If true the source texture will be sampled using wrap addressing during mipmap generation, otherwise clamp.
    .function("setMipWrapping", optional_override([](basis_encoder& self, bool mip_wrapping_flag) {
        self.m_params.m_mip_wrapping = mip_wrapping_flag;
    }))

    // Sets the mipmap generator's smallest allowed dimension.
    // default is 1, range is [1,16384]
    .function("setMipSmallestDimension", optional_override([](basis_encoder& self, int mip_smallest_dimension) {
        self.m_params.m_mip_smallest_dimension = mip_smallest_dimension;
    }))

    // Sets the .basis texture type.
    // cBASISTexTypeVideoFrames changes the encoder into video mode.
    // tex_type is enum basis_texture_type
    // default is cBASISTexType2D
    // All formats
    .function("setTexType", optional_override([](basis_encoder& self, uint32_t tex_type) {
        assert(tex_type < cBASISTexTypeTotal);
        self.m_params.m_tex_type = (basist::basis_texture_type)tex_type;
    }))

    // All formats (.basis only)
    .function("setUserData0", optional_override([](basis_encoder& self, uint32_t userdata0) {
        self.m_params.m_userdata0 = userdata0;
    }))

    // All formats (.basis only)
    .function("setUserData1", optional_override([](basis_encoder& self, uint32_t userdata1) {
        self.m_params.m_userdata1 = userdata1;
    }))

    // UASTC LDR 4x4 specific flags.

    // Sets the UASTC encoding performance vs. quality tradeoff, and other lesser used UASTC encoder flags.
    // This is a combination of flags. See cPackUASTCLevelDefault, etc.
    // UASTC LDR 4x4
    .function("setPackUASTCFlags", optional_override([](basis_encoder& self, uint32_t pack_uastc_flags) {
        assert((pack_uastc_flags & cPackUASTCLevelMask) >= cPackUASTCLevelFastest);
        assert((pack_uastc_flags & cPackUASTCLevelMask) <= cPackUASTCLevelVerySlow);
        self.m_params.m_pack_uastc_ldr_4x4_flags = pack_uastc_flags;
    }))

    // If true, the RDO post-processor will be applied to the encoded UASTC texture data.
    // UASTC LDR 4x4
    .function("setRDOUASTC", optional_override([](basis_encoder& self, bool rdo_uastc) {
        self.m_params.m_rdo_uastc_ldr_4x4 = rdo_uastc;
    }))

    // Default is 1.0 range is [0.001, 10.0]
    // UASTC LDR 4x4
    .function("setRDOUASTCQualityScalar", optional_override([](basis_encoder& self, float rdo_quality) {
        self.m_params.m_rdo_uastc_ldr_4x4_quality_scalar = rdo_quality;
    }))

    // Default is BASISU_RDO_UASTC_DICT_SIZE_DEFAULT, range is [BASISU_RDO_UASTC_DICT_SIZE_MIN, BASISU_RDO_UASTC_DICT_SIZE_MAX]
    // UASTC LDR 4x4
    .function("setRDOUASTCDictSize", optional_override([](basis_encoder& self, int dict_size) {
        assert((dict_size >= BASISU_RDO_UASTC_DICT_SIZE_MIN) && (dict_size <= BASISU_RDO_UASTC_DICT_SIZE_MAX));
        self.m_params.m_rdo_uastc_ldr_4x4_dict_size = dict_size;
    }))

    // Default is UASTC_RDO_DEFAULT_MAX_ALLOWED_RMS_INCREASE_RATIO, range is [01, 100.0]
    // UASTC LDR 4x4
    .function("setRDOUASTCMaxAllowedRMSIncreaseRatio", optional_override([](basis_encoder& self, float rdo_uastc_max_allowed_rms_increase_ratio) {
        self.m_params.m_rdo_uastc_ldr_4x4_max_allowed_rms_increase_ratio = rdo_uastc_max_allowed_rms_increase_ratio;
    }))

    // Default is UASTC_RDO_DEFAULT_SKIP_BLOCK_RMS_THRESH, range is [.01f, 100.0f]
    // UASTC LDR 4x4
    .function("setRDOUASTCSkipBlockRMSThresh", optional_override([](basis_encoder& self, float rdo_uastc_skip_block_rms_thresh) {
        self.m_params.m_rdo_uastc_ldr_4x4_skip_block_rms_thresh = rdo_uastc_skip_block_rms_thresh;
    }))

    // --- Low level options

    // Disables ETC1S selector RDO
    .function("setNoSelectorRDO", optional_override([](basis_encoder& self, bool no_selector_rdo_flag) {
        self.m_params.m_no_selector_rdo = no_selector_rdo_flag;
    }))

    // Disables ETC1S endpoint RDO
    .function("setNoEndpointRDO", optional_override([](basis_encoder& self, bool no_endpoint_rdo_flag) {
        self.m_params.m_no_endpoint_rdo = no_endpoint_rdo_flag;
    }))

    // Display output PSNR statistics
    // All formats
    .function("setComputeStats", optional_override([](basis_encoder& self, bool compute_stats_flag) {
        self.m_params.m_compute_stats = compute_stats_flag;
    }))

    // Write output .PNG/.EXR files for debugging
    // All formats
    .function("setDebugImages", optional_override([](basis_encoder& self, bool debug_images_flag) {
        self.m_params.m_debug_images = debug_images_flag;
    }))

  ;
#endif // BASISU_SUPPORT_ENCODING

}

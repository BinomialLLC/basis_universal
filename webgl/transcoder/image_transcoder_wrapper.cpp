/* -*- tab-width: 4; -*- */
/* vi: set sw=2 ts=4 expandtab textwidth=80: */

/*
 * ©2019 Khronos Group, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <emscripten/bind.h>
#include "basisu_transcoder.h"

using namespace emscripten;
using namespace basist;

namespace msc {
    // This is needed because the enum defining CDecode* is anonymous.
    enum TranscodeFlagBits {
        TranscodeAlphaDataToOpaqueFormats =
               cDecodeFlagsTranscodeAlphaDataToOpaqueFormats,
        HighQuality = cDecodeFlagsHighQuality
    };

    class BasisTranscoderState: public basisu_transcoder_state {
    };

    class TranscodedImage {
      public:
        TranscodedImage(size_t size) : image(size) { }

        uint8_t* data() { return image.data(); }
        size_t size() { return image.size(); }

        val get_typed_memory_view() {
           return val(typed_memory_view(image.size(), image.data()));
        }

      protected:
        std::vector<uint8_t> image;
    };

    class ImageTranscoderHelper {
        // block size calculations
        static inline uint32_t getWidthInBlocks(uint32_t w, uint32_t bw)
        {
            return (w + (bw - 1)) / bw;
        }

        static inline uint32_t getHeightInBlocks(uint32_t h, uint32_t bh)
        {
            return (h + (bh - 1)) / bh;
        }        //

      public:
        static size_t getTranscodedImageByteLength(transcoder_texture_format format,
                                                   uint32_t width, uint32_t height)
        {
            uint32_t blockByteLength =
                      basis_get_bytes_per_block_or_pixel(format);
            if (basis_transcoder_format_is_uncompressed(format)) {
                return width * height * blockByteLength;
            } else if (format == transcoder_texture_format::cTFPVRTC1_4_RGB
                       || format == transcoder_texture_format::cTFPVRTC1_4_RGBA) {
                // For PVRTC1, Basis only writes (or requires)
                // blockWidth * blockHeight * blockByteLength. But GL requires
                // extra padding for very small textures:
                // https://www.khronos.org/registry/OpenGL/extensions/IMG/IMG_texture_compression_pvrtc.txt
                const uint32_t paddedWidth = (width + 3) & ~3;
                const uint32_t paddedHeight = (height + 3) & ~3;
                return (std::max(8U, paddedWidth)
                        * std::max(8U, paddedHeight) * 4 + 7) / 8;
            } else {
                uint32_t blockWidth = getWidthInBlocks(width, basis_get_block_width(format));
                uint32_t blockHeight = getHeightInBlocks(height, basis_get_block_height(format));
                return blockWidth * blockHeight * blockByteLength;
            }
        }
    };


    class BasisLzEtc1sImageTranscoder : public basisu_etc1s_image_transcoder {
      public:
        BasisLzEtc1sImageTranscoder()
                : basisu_etc1s_image_transcoder(buildSelectorCodebook()) { }

        // Yes, code in the following functions handling data coming in from
        // ArrayBuffers IS copying the data. Sigh! According to Alon Zakai:
        //
        //     "There isn't a way to let compiled code access a new ArrayBuffer.
        //     The compiled code has hardcoded access to the wasm Memory it was
        //     instantiated with - all the pointers it can understand are
        //     indexes into that Memory. It can't refer to anything else,
        //     I'm afraid."
        //
        //     "In the future using different address spaces or techniques with
        //     reference types may open up some possibilities here."

        bool decode_palettes(uint32_t num_endpoints, const val& jsEndpoints,
                             uint32_t num_selectors, const val& jsSelectors)
        {
            std::vector<uint8_t> cEndpoints{}, cSelectors{};
            val memory = val::module_property("HEAP8")["buffer"];
            cEndpoints.resize(jsEndpoints["byteLength"].as<size_t>());
            val endpointsView = jsEndpoints["constructor"].new_(memory,
                                            reinterpret_cast<uintptr_t>(cEndpoints.data()),
                                            jsEndpoints["length"].as<uint32_t>());
            endpointsView.call<void>("set", jsEndpoints);

            cSelectors.resize(jsSelectors["byteLength"].as<size_t>());
            val selectorsView = jsSelectors["constructor"].new_(memory,
                                            reinterpret_cast<uintptr_t>(cSelectors.data()),
                                            jsSelectors["length"].as<uint32_t>());
            selectorsView.call<void>("set", jsSelectors);

            return basisu_etc1s_image_transcoder::decode_palettes(num_endpoints,
                                                       cEndpoints.data(),
                                                       cEndpoints.size(),
                                                       num_selectors,
                                                       cSelectors.data(),
                                                       cSelectors.size());
        }

        bool decode_tables(const val& jsTableData)
        {
            std::vector<uint8_t> cTableData{};
            val memory = val::module_property("HEAP8")["buffer"];

            cTableData.resize(jsTableData["byteLength"].as<size_t>());
            val TableDataView = jsTableData["constructor"].new_(memory,
                                            reinterpret_cast<uintptr_t>(cTableData.data()),
                                            jsTableData["length"].as<uint32_t>());
            TableDataView.call<void>("set", jsTableData);

            return basisu_etc1s_image_transcoder::decode_tables(
                                                          cTableData.data(),
                                                          cTableData.size());
        }

        // @~English
        // @brief Transcode a single BasisLZ supercompressed ETC1S image.
        //
        // @param[in] targetFormat the format to which to transcode the image.
        //                         This enum comes from Basis Universal.
        // @param[in] jsInSlices   emscripten::val of a .subarray of the 
        //                         ArrayBuffer holding the file data that
        //                         points to the first slice for this image.
        //                         An alpha slice, if it exists, always
        //                         immediately follows the rgb slice.
        // @param[in] imageDesc    reference to a struct basisu_image_desc
        //                         giving information about the image.
        // @param[in] decodeFlags
        //                   an OR of basisu_decode_flags bits setting decode
        //                   options. The only one of general interest is
        //                   @c cDecodeFlagsTranscodeAlphaDataToOpaqueFormats.
        //                   This can be used when @p targetFormat lacks an
        //                   alpha component. When set the alpha slice is
        //                   transcoded into the RGB components of the target.
        //
        // @return An emscripten::val with 1 entries, @c transcodedImage. If
        //         the transcode failed, @c transcodedImage will be undefined.
        //
        emscripten::val transcode_image(
                          transcoder_texture_format targetFormat,
                          const val& jsInSlices,
                          basisu_image_desc& imageDesc,
                          uint32_t decodeFlags = 0,
                          bool isVideo = false)
        {
            val memory = val::module_property("HEAP8")["buffer"];

            // First of all copy in the deflated data.
            std::vector <uint8_t> deflatedSlices;
            uint32_t deflatedSlicesByteLength
                                     = jsInSlices["byteLength"].as<uint32_t>();
            deflatedSlices.resize(deflatedSlicesByteLength);
            val memoryView = jsInSlices["constructor"].new_(memory,
                             reinterpret_cast<uintptr_t>(deflatedSlices.data()),
                             deflatedSlices.size());
            memoryView.call<void>("set", jsInSlices);

            size_t tiByteLength =
            ImageTranscoderHelper::getTranscodedImageByteLength(targetFormat,
                                                      imageDesc.m_orig_width,
                                                      imageDesc.m_orig_height);
            TranscodedImage* dst = new TranscodedImage(tiByteLength);

            bool status = basisu_etc1s_image_transcoder::transcode_image(
                                              targetFormat,
                                              dst->data(),
                                              dst->size(),
                                              deflatedSlices.data(),
                                              imageDesc,
                                              decodeFlags,
                                              isVideo);

            val ret = val::object();
            if (status) {
                ret.set("transcodedImage", dst);
            }
            return std::move(ret);
        }

      protected:
        static basist::etc1_global_selector_codebook* pGlobal_codebook;

        static basist::etc1_global_selector_codebook*
        buildSelectorCodebook()
        {
           if (!pGlobal_codebook) {
                pGlobal_codebook = new basist::etc1_global_selector_codebook(
                                                     g_global_selector_cb_size,
                                                     g_global_selector_cb);
            }
            return pGlobal_codebook;
        }
    };

    class UastcImageTranscoder : public basisu_uastc_image_transcoder {
      public:
        UastcImageTranscoder() : basisu_uastc_image_transcoder() { }

        // @~English
        // @brief Transcode a single UASTC encoded image.
        //
        // @param[in] targetFormat the format to which to transcode the image.
        //                         This enum comes from Basis Universal.
        // @param[in] jsInSlices   emscripten::val of a .subarray of the 
        //                         ArrayBuffer holding the file data that
        //                         points to the the image to transcode.
        // @param[in] imageDesc    reference to a struct basisu_image_desc
        //                         giving information about the image.
        // @param[in] decodeFlags
        //                   an OR of basisu_decode_flags bits setting decode
        //                   options. The only one of general interest is
        //                   @c cDecodeFlagsTranscodeAlphaDataToOpaqueFormats.
        //                   This can be used when @p targetFormat lacks an
        //                   alpha component. When set, the alpha components
        //                   are decoded into the RGB components of the target.
        //
        // @return An emscripten::val with 1 entries, @c transcodedImage. If
        //         the transcode failed, @c transcodedImage will be undefined.
        //
        emscripten::val transcode_image(
                          transcoder_texture_format targetFormat,
                          const val& jsInImage,
                          basisu_image_desc& imageDesc,
                          uint32_t decodeFlags = 0,
                          bool hasAlpha = false,
                          bool isVideo = false)
        {
            // Copy in the deflated image.
            std::vector <uint8_t> deflatedImage;
            size_t deflatedImageByteLength
                                     = jsInImage["byteLength"].as<size_t>();
            deflatedImage.resize(deflatedImageByteLength);
            val memory = val::module_property("HEAP8")["buffer"];
            val memoryView = jsInImage["constructor"].new_(memory,
                              reinterpret_cast<uintptr_t>(deflatedImage.data()),
                              deflatedImageByteLength);
            memoryView.call<void>("set", jsInImage);

            size_t tiByteLength =
            ImageTranscoderHelper::getTranscodedImageByteLength(targetFormat,
                                                      imageDesc.m_orig_width,
                                                      imageDesc.m_orig_height);
            TranscodedImage* dst = new TranscodedImage(tiByteLength);
            bool status =
                basisu_uastc_image_transcoder::transcode_image(
                                              targetFormat,
                                              dst->data(),
                                              dst->size(),
                                              deflatedImage.data(),
                                              imageDesc,
                                              decodeFlags,
                                              hasAlpha,
                                              isVideo);

            val ret = val::object();
            if (status) {
                ret.set("transcodedImage", dst);
            }
            return std::move(ret);
        }
    };

    basist::etc1_global_selector_codebook* BasisLzEtc1sImageTranscoder::pGlobal_codebook;
}

/** @page msc_basis_transcoder Basis Image Transcoder binding

 ## WebIDL for the binding

@code{.unparsed}
void initTranscoders();

bool isFormatSupported(TranscodeTarget targetFormat, TextureFormat texFormat);

interface BasisTranscoderState {
    void BasisTranscoderState();
};

interface TranscodedImage {
    ArrayBufferView get_typed_memory_view();
};

interface TranscodeResult {
    TranscodedImage transcodedImage;
};

interface BasisLzEtc1sImageTranscoder {
    void BasisLzEtc1sImageTranscoder();
    uint32_t getBytesPerBlock(TranscodeTarget format);
    bool decode_palettes(uint32_t num_endpoints,
                         const Uint8Array endpoints,
                         uint32_t num_selectors,
                         const Uint8Array selectors);
    bool decode_tables(const Uint8Array tableData);
    TranscodeResult transcode_image(
                          TranscodeTarget targetFormat,
                          const Uint8Array jsInSlices,
                          ImageInfo imageInfo,
                          uint32_t decodeFlags = 0,
                          bool isVideo = false);
};

interface BasisUastcImageTranscoder {
    void BasisUastcImageTranscoder();
    uint32_t getBytesPerBlock(const TranscodeTarget format);
    TranscodeResult transcode_image(
                          TranscodeTarget targetFormat,
                          const Uint8Array jsInImage,
                          basisu_image_desc& imageDesc,
                          uint32_t decodeFlags = 0,
                          bool hasAlpha = false,
                          bool isVideo = false);

interface ImageInfo = {
    ImageInfo(TextureFormat texFormat, uint32_t width, uint32_t height,
              uint32_t level);
    attribute uint32_t flags;
    attribute long rgbByteOffset;
    attribute long rgbByteLength;
    attribute long alphaByteOffset;
    attribute long alphaByteLength;
    attribute uint32_t width;
    attribute uint32_t height;
    attribute uint32_t numBlocksX;
    attribute uint32_t numBlocksY;
    attribute uint32_t level;
};

// Some targets may not be available depending on options used when compiling
// the web assembly.
enum TranscodeTarget = {
    "ETC1_RGB",
    "BC1_RGB",
    "BC4_R",
    "BC5_RG",
    "BC3_RGBA",
    "PVRTC1_4_RGB",
    "PVRTC1_4_RGBA",
    "BC7_M6_RGB",
    "BC7_M5_RGBA",
    "ETC2_RGBA",
    "ASTC_4x4_RGBA",
    "RGBA32",
    "RGB565",
    "BGR565",
    "RGBA4444",
    "PVRTC2_4_RGB",
    "PVRTC2_4_RGBA",
    "EAC_R11",
    "EAC_RG11"
};

enum TextureFormat = {
    "ETC1S",
    "UASTC4x4",
};

enum TranscodeFlagBits = 
    "TRANSCODE_ALPHA_DATA_TO_OPAQUE_FORMATS",
    "HIGH_QUALITY",
};

enum TranscodeFlagBits = {
    "TRANSCODE_ALPHA_DATA_TO_OPAQUE_FORMATS",
    "HIGH_QUALITY"
};

@endcode

## How to use

Put msc_basis_transcoder.js and msc_basis_transcoder.wasm in a
directory on your server. Create a script tag with
msc_basis_tranacoder.js as the @c src as shown below, changing
the path as necessary for the relative locations of your .html
file and the script source. msc_basis_transcoder.js will
automatically load msc_basis_transcoder.wasm.

### Create an instance of the MSC_TRANSCODER module

For example, add this to the .html file to initialize the transcoder and
make it available on the main window.
@code{.unparsed}
    &lt;script src="msc_transcoder_wrapper.js">&lt;/script>
    &lt;script type="text/javascript">
      MSC_TRANSCODER().then(module => {
        window.MSC_TRANSCODER = module;
        module.initTranscoders();
        // Call a function to begin loading or transcoding..
    &lt;/script>
@endcode

@e After the module is initialized, invoke code that will directly or indirectly cause
a function with code like the following to be executed.

## Somewhere in the loader/transcoder

Assume a KTX file is fetched via an XMLHttpRequest which deposits the data into
a Uint8Array, "buData"...

@note The names of the data items used in the following code are those
from the KTX2 specification but the actual data is not specific to that
container format.

@code{.unparsed}
    const {
        BasisLzEtc1sImageTranscoder,
        BasisUastcImageTranscoder,
        TranscodeTarget
    } = MSC_TRANSCODER;

    // Determine from the KTX2 header information in buData if
    // the data format  is BasisU or Uastc.
    // supercompressionScheme value == 1, it's TextureFormat.ETC1S.
    // DFD colorModel == 166, it's TextureFormat.UASTC4x4.
    const texFormat = ...

    // Determine appropriate transcode format from available targets,
    // info about the texture, e.g. texture.numComponents, and
    // expected use. Use values from TranscodeTarget.
    const targetFormat = ...
    if ( !MSC_TRANSCODER.isFormatSupported(targetFormat, texFormat) {
        throw new Error( ... );
    }

    if (TextureFormat.UASTC4x4) {
        var result = transcodeUastc(targetFormat);
    } else {
        var result = transcodeEtc1s(targetFormat);
    }
    if ( result.transcodedImage === undefined ) {
        throw new Error( 'Unable to transcode image.' );
    }
@endcode

This is the function for transcoding etc1s.

@code{.unparsed}
transcodeEtc1s(targetFormat) {
    // Locate the supercompression global data and compresssed
    // mip level data within buData.

    var bit = new BasisLzEtc1sImageTranscoder();

    // Find the index of the starts of the endpoints, selectors and tables
    // data within buData...
    var endpointsStart = ...
    var selectorsStart = ...
    var tablesStart = ...
    // The numbers of endpoints & selectors and their byteLengths are items
    // within buData. They are in the header of a .ktx2 file's
    // supercompressionGlobalData and in the header of a .basis file.

    var endpoints = new Uint8Array(buData, endpointsStart,
                                   endpointsByteLength);
    var selectors = new Uint8Array(buData, selectorsStart,
                                   selectorsByteLength);

    bit.decodePalettes(numEndpoints, endpoints,
                              numSelectors, selectors);

    var tables = new UInt8Array(buData, tablesStart, tablesByteLength);
    bit.decodeTables(tables);

    // Determine if the file contains a video sequence...
    var isVideo = ...

    // Calculate the total number of images in the data
    var numImages = ...

    // Set up a subarray pointing at the deflated image descriptions
    // in buData. This is for .ktx2 containers. The image descriptions
    // are located in supercompressionGlobalData. .basis containers will
    // require different code to locate the slice descriptions within
    // the file.
    var imageDescsStart = ...:
    // An imageDesc has 5 uint32 values.
    var imageDescs = new Uint32Data(buData, imageDescsStart,
                                    numImages * 5 * 4);
    var curImageIndex = 0;

    // Pseudo code for processing the levels of a .ktx2 container...
    foreach level {
      var leveWidth = width of image at this level
      var levelHeight = height of image at this level
      imageInfo = new ImageInfo(TextureFormat::ETC1S, levelWidth, levelHeight,
                                level);
      foreach image in level {
        // In KTX2 container locate the imageDesc for this image.
        var imageDesc = imageDescs[curImageIndex++];
        imageInfo.flags = imageDesc.imageFlags;
        imageInfo.rgbByteOffset = 0;
        imageInfo.rgbByteLength = imageDesc.rgbSliceByteLength;
        imageInfo.alphaByteOffset = imageDesc.alphaSliceByteOffset > 0 ? imageDesc.rgbSliceByteLength : 0;
        imageInfo.alphaByteLength = imageDesc.alphaSliceByteLength;
        // Determine the location in the ArrayBuffer of the start
        // of the deflated data for level.
        var levelOffset = ...
        // Make a .subarray of the rgb slice data.
        var levelData = new Uint8Array(
                     buData,
                     levelOffset + imageDesc.rgbSliceByteOffset,
                     imageDesc.rgbSliceByteLength + imageDesc.alphaByteLength
                     );
        var result = bit.transcodeImage(
                                     targetFormat,
                                     levelData,
                                     imageInfo,
                                     0,
                                     isVideo);
        if ( result.transcodedImage === undefined ) {
            throw new Error( ... );
        }
        let imgData = transcodedImage.get_typed_memory_view();

        // Upload data in imgData to WebGL...

        // Do not call delete() until data has been uploaded
        // or otherwise copied.
        transcodedImage.delete();
      }
    }

    // For .basis containers, it is necessary to locate the slice
    // description(s) for the image and set the values in imageInfo
    // from them. Use of the .basis-specific transcoder is recommended.
    // The definition of the basis_slice_desc struct makes it difficult
    // to create JS interface for it  with embind.
@endcode

This is the function for transcoding Uastc.

@code{.unparsed}
transcodeUastc(targetFormat) {
    var uit = new UastcImageTranscoder();

    // Determine if the data is supercompressed.
    var zstd = (supercompressionScheme == 2);

    // Determine if the data has alpha.
    var hasAlpha = (Channel ID of sample[0] in DFD == 1);

    var dctx;
    if (zstd) {
        // Initialize the zstd decoder. Zstd JS wrapper + wasm is
        // a separate package.
        dctx = ZSTD_createDCtx();
    }

    // Pseudo code for processing the levels of a .ktx2 container...
    foreach level {
      // Determine the location in the ArrayBuffer buData of the
      // start of the deflated data for the level.
      var levelData = ...
      if (zstd) {
          // Inflate the level data
          levelData = ZSTD_decompressDCtx(dctx, levelData, ... );
      }

      var levelWidth = width of image at this level
      var levelHeight = height of image at this level
      var depth = depth of texture at this level
      var levelImageCount = number of layers * number of faces * depth;
      var imageOffsetInLevel = 0;

      var imageInfo = new ImageInfo(TextureFormat::UASTC4x4,
                                    levelWidth, levelHeight, level);
      var levelImageByteLength = imageInfo.numBlocksX * imageInfo.numBlocksY * DFD bytesPlane0;

      foreach image in level {
        inImage = Uint8Array(levelData, imageOffsetInLevel, levelImageByteLength);
        imageInfo.flags = 0;
        imageInfo.rgbByteOffset = 0;
        imageInfo.rgbByteLength = levelImageByteLength;
        imageInfo.alphaByteOffset = 0;
        imageInfo.alphaByteLength = 0;

        const {transcodedImage} = uit.transcodeImage(
                                                    targetFormat,
                                                    inImage,
                                                    imageInfo,
                                                    0,
                                                    hasAlpha,
                                                    isVideo);
       if ( result.transcodedImage === undefined ) {
           throw new Error( ... );
       }
       let imgData = transcodedImage.get_typed_memory_view();

       // Upload data in imgData to WebGL...

       // Do not call delete() until data has been uploaded
       // or otherwise copied.
       transcodedImage.delete();

       imageOffsetInLevel += levelImageByteLength;
    }
  }
  // For .basis containers, as with ETC1S, it is necessary to locate
  // the slice description for the image and set the values in imageInfo
  // from it.
}
@endcode

*/

EMSCRIPTEN_BINDINGS(ktx_wrappers)
{
    enum_<transcoder_texture_format>("TranscodeTarget")
        .value("ETC1_RGB", transcoder_texture_format::cTFETC1_RGB)
        .value("BC1_RGB", transcoder_texture_format::cTFBC1_RGB)
        .value("BC4_R", transcoder_texture_format::cTFBC4_R)
        .value("BC5_RG", transcoder_texture_format::cTFBC5_RG)
        .value("BC3_RGBA", transcoder_texture_format::cTFBC3_RGBA)
        .value("PVRTC1_4_RGB", transcoder_texture_format::cTFPVRTC1_4_RGB)
        .value("PVRTC1_4_RGBA", transcoder_texture_format::cTFPVRTC1_4_RGBA)
        .value("BC7_M6_RGB", transcoder_texture_format::cTFBC7_M6_RGB)
        .value("BC7_M5_RGBA", transcoder_texture_format::cTFBC7_M5_RGBA)
        .value("ETC2_RGBA", transcoder_texture_format::cTFETC2_RGBA)
        .value("ASTC_4x4_RGBA", transcoder_texture_format::cTFASTC_4x4_RGBA)
        .value("RGBA32", transcoder_texture_format::cTFRGBA32)
        .value("RGB565", transcoder_texture_format::cTFRGB565)
        .value("BGR565", transcoder_texture_format::cTFBGR565)
        .value("RGBA4444", transcoder_texture_format::cTFRGBA4444)
        .value("PVRTC2_4_RGB", transcoder_texture_format::cTFPVRTC2_4_RGB)
        .value("PVRTC2_4_RGBA", transcoder_texture_format::cTFPVRTC2_4_RGBA)
        .value("EAC_R11", transcoder_texture_format::cTFETC2_EAC_R11)
        .value("EAC_RG11", transcoder_texture_format::cTFETC2_EAC_RG11)
    ;

    enum_<basis_tex_format>("TextureFormat")
        .value("ETC1S", basis_tex_format::cETC1S)
        .value("UASTC4x4", basis_tex_format::cUASTC4x4)
    ;

    enum_<msc::TranscodeFlagBits>("TranscodeFlagBits")
        .value("TRANSCODE_ALPHA_DATA_TO_OPAQUE_FORMATS",
               msc::TranscodeAlphaDataToOpaqueFormats)
        .value("HIGH_QUALITY", msc::HighQuality)
    ;

    function("initTranscoders", basisu_transcoder_init);
    function("isFormatSupported", basis_is_format_supported);

    class_<basisu_image_desc>("ImageInfo")
        .constructor<basis_tex_format,uint32_t,uint32_t,uint32_t>()
        .property("flags", &basisu_image_desc::m_flags)
        .property("rgbByteOffset", &basisu_image_desc::m_rgb_byte_offset)
        .property("rgbByteLength", &basisu_image_desc::m_rgb_byte_length)
        .property("alphaByteOffset", &basisu_image_desc::m_alpha_byte_offset)
        .property("alphaByteLength", &basisu_image_desc::m_alpha_byte_length)
        .property("width", &basisu_image_desc::m_orig_width)
        .property("height", &basisu_image_desc::m_orig_height)
        .property("numBlocksX", &basisu_image_desc::m_num_blocks_x)
        .property("numBlocksY", &basisu_image_desc::m_num_blocks_y)
        .property("level", &basisu_image_desc::m_level)
        ;

    class_<msc::BasisLzEtc1sImageTranscoder>("BasisLzEtc1sImageTranscoder")
        .constructor()
        .class_function("getBytesPerBlock", basis_get_bytes_per_block_or_pixel)
        .function("decodePalettes",
                  &msc::BasisLzEtc1sImageTranscoder::decode_palettes)
        .function("decodeTables",
                  &msc::BasisLzEtc1sImageTranscoder::decode_tables)
        .function("transcodeImage",
                  &msc::BasisLzEtc1sImageTranscoder::transcode_image)
        ;

    class_<msc::UastcImageTranscoder>("UastcImageTranscoder")
        .constructor()
        .class_function("getBytesPerBlock", basis_get_bytes_per_block_or_pixel)
        .function("transcodeImage",
                  &msc::UastcImageTranscoder::transcode_image)
        ;

    class_<basisu_transcoder_state>("BasisTranscoderState")
        .constructor()
        ;

    class_<msc::TranscodedImage>("TranscodedImage")
        .function( "get_typed_memory_view",
                  &msc::TranscodedImage::get_typed_memory_view )
    ;

}

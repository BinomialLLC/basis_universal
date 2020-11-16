/*
 * Basis Loader
 *
 * Usage:
 * // basis_loader.js should be loaded from the same directory as
 * // basis_transcoder.js and basis_transcoder.wasm
 *
 * // Create the texture loader and set the WebGL context it should use. Spawns
 * // a worker which handles all of the transcoding.
 * let basisLoader = new BasisLoader();
 * basisLoader.setWebGLContext(gl);
 *
 * // To allow separate color and alpha textures to be returned in cases where
 * // it would provide higher quality:
 * basisLoader.allowSeparateAlpha = true;
 *
 * // loadFromUrl() returns a promise which resolves to a completed WebGL
 * // texture or rejects if there's an error loading.
 * basisBasics.loadFromUrl(fullPathToTexture).then((result) => {
 *   // WebGL color+alpha texture;
 *   result.texture;
 *
 *   // WebGL alpha texture, only if basisLoader.allowSeparateAlpha is true.
 *   // null if alpha is encoded in result.texture or result.alpha is false.
 *   result.alphaTexture;
 *
 *   // True if the texture contained an alpha channel.
 *   result.alpha;
 *
 *   // Number of mip levels in texture/alphaTexture
 *   result.mipLevels;
 *
 *   // Dimensions of the base mip level.
 *   result.width;
 *   result.height;
 * });
 */

// This file contains the code both for the main thread interface and the
// worker that does the transcoding.
const IN_WORKER = typeof importScripts === "function";
const SCRIPT_PATH = typeof document !== 'undefined' && document.currentScript ? document.currentScript.src : undefined;

if (!IN_WORKER) {
  //
  // Main Thread
  //
  class PendingTextureRequest {
    constructor(gl, url) {
      this.gl = gl;
      this.url = url;
      this.texture = null;
      this.alphaTexture = null;
      this.promise = new Promise((resolve, reject) => {
        this.resolve = resolve;
        this.reject = reject;
      });
    }

    uploadImageData(webglFormat, buffer, mipLevels) {
      let gl = this.gl;
      let texture = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, mipLevels.length > 1 || webglFormat.uncompressed ? gl.LINEAR_MIPMAP_LINEAR : gl.LINEAR);

      let levelData = null;

      for (let mipLevel of mipLevels) {
        if (!webglFormat.uncompressed) {
          levelData = new Uint8Array(buffer, mipLevel.offset, mipLevel.size);
          gl.compressedTexImage2D(
            gl.TEXTURE_2D,
            mipLevel.level,
            webglFormat.format,
            mipLevel.width,
            mipLevel.height,
            0,
            levelData);
        } else {
          switch (webglFormat.type) {
            case WebGLRenderingContext.UNSIGNED_SHORT_4_4_4_4:
            case WebGLRenderingContext.UNSIGNED_SHORT_5_5_5_1:
            case WebGLRenderingContext.UNSIGNED_SHORT_5_6_5:
              levelData = new Uint16Array(buffer, mipLevel.offset, mipLevel.size / 2);
              break;
            default:
              levelData = new Uint8Array(buffer, mipLevel.offset, mipLevel.size);
              break;
          }
          gl.texImage2D(
            gl.TEXTURE_2D,
            mipLevel.level,
            webglFormat.format,
            mipLevel.width,
            mipLevel.height,
            0,
            webglFormat.format,
            webglFormat.type,
            levelData);
        }
      }

      if (webglFormat.uncompressed && mipLevels.length == 1) {
        gl.generateMipmap(gl.TEXTURE_2D);
      }

      return texture;
    }
  };

  class BasisLoader {
    constructor() {
      this.gl = null;
      this.supportedFormats = {};
      this.pendingTextures = {};
      this.nextPendingTextureId = 1;
      this.allowSeparateAlpha = false;

      // Reload the current script as a worker
      this.worker = new Worker(SCRIPT_PATH);
      this.worker.onmessage = (msg) => {
        // Find the pending texture associated with the data we just received
        // from the worker.
        let pendingTexture = this.pendingTextures[msg.data.id];
        if (!pendingTexture) {
          if (msg.data.error) {
            console.error(`Basis transcode failed: ${msg.data.error}`);
          }
          console.error(`Invalid pending texture ID: ${msg.data.id}`);
          return;
        }

        // Remove the pending texture from the waiting list.
        delete this.pendingTextures[msg.data.id];

        // If the worker indicated an error has occured handle it now.
        if (msg.data.error) {
          console.error(`Basis transcode failed: ${msg.data.error}`);
          pendingTexture.reject(`${msg.data.error}`);
          return;
        }

        // Upload the image data returned by the worker.
        pendingTexture.texture = pendingTexture.uploadImageData(
            msg.data.webglFormat,
            msg.data.buffer,
            msg.data.mipLevels);

        if (msg.data.alphaBuffer) {
          pendingTexture.alphaTexture = pendingTexture.uploadImageData(
              msg.data.webglFormat,
              msg.data.alphaBuffer,
              msg.data.mipLevels);
        }

        pendingTexture.resolve({
          mipLevels: msg.data.mipLevels.length,
          width: msg.data.mipLevels[0].width,
          height: msg.data.mipLevels[0].height,
          alpha: msg.data.hasAlpha,
          texture: pendingTexture.texture,
          alphaTexture: pendingTexture.alphaTexture,
        });
      };
    }

    setWebGLContext(gl) {
      if (this.gl != gl) {
        this.gl = gl;
        if (gl) {
          this.supportedFormats = {
            s3tc: !!gl.getExtension('WEBGL_compressed_texture_s3tc'),
            etc1: !!gl.getExtension('WEBGL_compressed_texture_etc1'),
            etc2: !!gl.getExtension('WEBGL_compressed_texture_etc'),
            pvrtc: !!gl.getExtension('WEBGL_compressed_texture_pvrtc'),
            astc: !!gl.getExtension('WEBGL_compressed_texture_astc'),
            bptc: !!gl.getExtension('EXT_texture_compression_bptc')
          };
        } else {
          this.supportedFormats = {};
        }
      }
    }

    // This method changes the active texture unit's TEXTURE_2D binding
    // immediately prior to resolving the returned promise.
    loadFromUrl(url) {
      let pendingTexture = new PendingTextureRequest(this.gl, url);
      this.pendingTextures[this.nextPendingTextureId] = pendingTexture;

      this.worker.postMessage({
        id: this.nextPendingTextureId,
        url: url,
        allowSeparateAlpha: this.allowSeparateAlpha,
        supportedFormats: this.supportedFormats
      });

      this.nextPendingTextureId++;
      return pendingTexture.promise;
    }
  }

  window.BasisLoader = BasisLoader;

} else {
  //
  // Worker
  //
  importScripts('basis_transcoder.js');

  let BasisFile = null;

  const BASIS_INITIALIZED = BASIS().then((module) => {
    BasisFile = module.BasisFile;
    module.initializeBasis();
  });

  // Copied from enum class transcoder_texture_format in basisu_transcoder.h with minor javascript-ification
  const BASIS_FORMAT = {
    // Compressed formats

    // ETC1-2
    cTFETC1_RGB: 0,							// Opaque only, returns RGB or alpha data if cDecodeFlagsTranscodeAlphaDataToOpaqueFormats flag is specified
    cTFETC2_RGBA: 1,							// Opaque+alpha, ETC2_EAC_A8 block followed by a ETC1 block, alpha channel will be opaque for opaque .basis files

    // BC1-5, BC7 (desktop, some mobile devices)
    cTFBC1_RGB: 2,							// Opaque only, no punchthrough alpha support yet, transcodes alpha slice if cDecodeFlagsTranscodeAlphaDataToOpaqueFormats flag is specified
    cTFBC3_RGBA: 3, 							// Opaque+alpha, BC4 followed by a BC1 block, alpha channel will be opaque for opaque .basis files
    cTFBC4_R: 4,								// Red only, alpha slice is transcoded to output if cDecodeFlagsTranscodeAlphaDataToOpaqueFormats flag is specified
    cTFBC5_RG: 5,								// XY: Two BC4 blocks, X=R and Y=Alpha, .basis file should have alpha data (if not Y will be all 255's)
    cTFBC7_RGBA: 6,							// RGB or RGBA, mode 5 for ETC1S, modes (1,2,3,5,6,7) for UASTC

    // PVRTC1 4bpp (mobile, PowerVR devices)
    cTFPVRTC1_4_RGB: 8,						// Opaque only, RGB or alpha if cDecodeFlagsTranscodeAlphaDataToOpaqueFormats flag is specified, nearly lowest quality of any texture format.
    cTFPVRTC1_4_RGBA: 9,					// Opaque+alpha, most useful for simple opacity maps. If .basis file doesn't have alpha cTFPVRTC1_4_RGB will be used instead. Lowest quality of any supported texture format.

    // ASTC (mobile, Intel devices, hopefully all desktop GPU's one day)
    cTFASTC_4x4_RGBA: 10,					// Opaque+alpha, ASTC 4x4, alpha channel will be opaque for opaque .basis files. Transcoder uses RGB/RGBA/L/LA modes, void extent, and up to two ([0,47] and [0,255]) endpoint precisions.

    // Uncompressed (raw pixel) formats
    cTFRGBA32: 13,							// 32bpp RGBA image stored in raster (not block) order in memory, R is first byte, A is last byte.
    cTFRGB565: 14,							// 166pp RGB image stored in raster (not block) order in memory, R at bit position 11
    cTFBGR565: 15,							// 16bpp RGB image stored in raster (not block) order in memory, R at bit position 0
    cTFRGBA4444: 16,							// 16bpp RGBA image stored in raster (not block) order in memory, R at bit position 12, A at bit position 0

    cTFTotalTextureFormats: 22,
  };

  // WebGL compressed formats types, from:
  // http://www.khronos.org/registry/webgl/extensions/

  // https://www.khronos.org/registry/webgl/extensions/WEBGL_compressed_texture_s3tc/
  const COMPRESSED_RGB_S3TC_DXT1_EXT  = 0x83F0;
  const COMPRESSED_RGBA_S3TC_DXT1_EXT = 0x83F1;
  const COMPRESSED_RGBA_S3TC_DXT3_EXT = 0x83F2;
  const COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3;

  // https://www.khronos.org/registry/webgl/extensions/WEBGL_compressed_texture_etc1/
  const COMPRESSED_RGB_ETC1_WEBGL = 0x8D64;

  // https://www.khronos.org/registry/webgl/extensions/WEBGL_compressed_texture_etc/
  const COMPRESSED_R11_EAC                        = 0x9270;
  const COMPRESSED_SIGNED_R11_EAC                 = 0x9271;
  const COMPRESSED_RG11_EAC                       = 0x9272;
  const COMPRESSED_SIGNED_RG11_EAC                = 0x9273;
  const COMPRESSED_RGB8_ETC2                      = 0x9274;
  const COMPRESSED_SRGB8_ETC2                     = 0x9275;
  const COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2  = 0x9276;
  const COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2 = 0x9277;
  const COMPRESSED_RGBA8_ETC2_EAC                 = 0x9278;
  const COMPRESSED_SRGB8_ALPHA8_ETC2_EAC          = 0x9279;

  // https://www.khronos.org/registry/webgl/extensions/WEBGL_compressed_texture_astc/
  const COMPRESSED_RGBA_ASTC_4x4_KHR = 0x93B0;

  // https://www.khronos.org/registry/webgl/extensions/WEBGL_compressed_texture_pvrtc/
  const COMPRESSED_RGB_PVRTC_4BPPV1_IMG = 0x8C00;
  const COMPRESSED_RGB_PVRTC_2BPPV1_IMG  = 0x8C01;
  const COMPRESSED_RGBA_PVRTC_4BPPV1_IMG = 0x8C02;
  const COMPRESSED_RGBA_PVRTC_2BPPV1_IMG = 0x8C03;

  // https://www.khronos.org/registry/webgl/extensions/EXT_texture_compression_bptc/
  const COMPRESSED_RGBA_BPTC_UNORM_EXT = 0x8E8C;
  const COMPRESSED_SRGB_ALPHA_BPTC_UNORM_EXT = 0x8E8D;
  const COMPRESSED_RGB_BPTC_SIGNED_FLOAT_EXT = 0x8E8E;
  const COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_EXT = 0x8E8F;

  const BASIS_WEBGL_FORMAT_MAP = {};
  // Compressed formats
  BASIS_WEBGL_FORMAT_MAP[BASIS_FORMAT.cTFBC1_RGB] = { format: COMPRESSED_RGB_S3TC_DXT1_EXT };
  BASIS_WEBGL_FORMAT_MAP[BASIS_FORMAT.cTFBC3_RGBA] = { format: COMPRESSED_RGBA_S3TC_DXT5_EXT };
  BASIS_WEBGL_FORMAT_MAP[BASIS_FORMAT.cTFBC7_RGBA] = { format: COMPRESSED_RGBA_BPTC_UNORM_EXT };
  BASIS_WEBGL_FORMAT_MAP[BASIS_FORMAT.cTFETC1_RGB] = { format: COMPRESSED_RGB_ETC1_WEBGL };
  BASIS_WEBGL_FORMAT_MAP[BASIS_FORMAT.cTFETC2_RGBA] = { format: COMPRESSED_RGBA8_ETC2_EAC };
  BASIS_WEBGL_FORMAT_MAP[BASIS_FORMAT.cTFASTC_4x4_RGBA] = { format: COMPRESSED_RGBA_ASTC_4x4_KHR };
  BASIS_WEBGL_FORMAT_MAP[BASIS_FORMAT.cTFPVRTC1_4_RGB] = { format: COMPRESSED_RGB_PVRTC_4BPPV1_IMG };
  BASIS_WEBGL_FORMAT_MAP[BASIS_FORMAT.cTFPVRTC1_4_RGBA] = { format: COMPRESSED_RGBA_PVRTC_4BPPV1_IMG };

  // Uncompressed formats
  BASIS_WEBGL_FORMAT_MAP[BASIS_FORMAT.cTFRGBA32] = { uncompressed: true, format: WebGLRenderingContext.RGBA, type: WebGLRenderingContext.UNSIGNED_BYTE };
  BASIS_WEBGL_FORMAT_MAP[BASIS_FORMAT.cTFRGB565] = { uncompressed: true, format: WebGLRenderingContext.RGB, type: WebGLRenderingContext.UNSIGNED_SHORT_5_6_5 };
  BASIS_WEBGL_FORMAT_MAP[BASIS_FORMAT.cTFRGBA4444] = { uncompressed: true, format: WebGLRenderingContext.RGBA, type: WebGLRenderingContext.UNSIGNED_SHORT_4_4_4_4 };

  // Notifies the main thread when a texture has failed to load for any reason.
  function fail(id, errorMsg) {
    postMessage({
      id: id,
      error: errorMsg
    });
  }

  function basisFileFail(id, basisFile, errorMsg) {
    fail(id, errorMsg);
    basisFile.close();
    basisFile.delete();
  }

  // This utility currently only transcodes the first image in the file.
  const IMAGE_INDEX = 0;
  const TOP_LEVEL_MIP = 0;

  function transcode(id, arrayBuffer, supportedFormats, allowSeparateAlpha) {
    let basisData = new Uint8Array(arrayBuffer);

    let basisFile = new BasisFile(basisData);
    let images = basisFile.getNumImages();
    let levels = basisFile.getNumLevels(IMAGE_INDEX);
    let hasAlpha = basisFile.getHasAlpha();
    if (!images || !levels) {
      basisFileFail(id, basisFile, 'Invalid Basis data');
      return;
    }

    if (!basisFile.startTranscoding()) {
      basisFileFail(id, basisFile, 'startTranscoding failed');
      return;
    }

    let basisFormat = undefined;
    let needsSecondaryAlpha = false;
    if (hasAlpha) {
      if (supportedFormats.etc2) {
        basisFormat = BASIS_FORMAT.cTFETC2_RGBA;
      } else if (supportedFormats.bptc) {
        basisFormat = BASIS_FORMAT.cTFBC7_RGBA;
      } else if (supportedFormats.s3tc) {
        basisFormat = BASIS_FORMAT.cTFBC3_RGBA;
      } else if (supportedFormats.astc) {
        basisFormat = BASIS_FORMAT.cTFASTC_4x4_RGBA;
      } else if (supportedFormats.pvrtc) {
        if (allowSeparateAlpha) {
          basisFormat = BASIS_FORMAT.cTFPVRTC1_4_RGB;
          needsSecondaryAlpha = true;
        } else {
          basisFormat = BASIS_FORMAT.cTFPVRTC1_4_RGBA;
        }
      } else if (supportedFormats.etc1 && allowSeparateAlpha) {
        basisFormat = BASIS_FORMAT.cTFETC1_RGB;
        needsSecondaryAlpha = true;
      } else {
        // If we don't support any appropriate compressed formats transcode to
        // raw pixels. This is something of a last resort, because the GPU
        // upload will be significantly slower and take a lot more memory, but
        // at least it prevents you from needing to store a fallback JPG/PNG and
        // the download size will still likely be smaller.
        basisFormat = BASIS_FORMAT.RGBA32;
      }
    } else {
      if (supportedFormats.etc1) {
        // Should be the highest quality, so use when available.
        // http://richg42.blogspot.com/2018/05/basis-universal-gpu-texture-format.html
        basisFormat = BASIS_FORMAT.cTFETC1_RGB;
      } else if (supportedFormats.bptc) {
        basisFormat = BASIS_FORMAT.cTFBC7_RGBA;
      } else if (supportedFormats.s3tc) {
        basisFormat = BASIS_FORMAT.cTFBC1_RGB;
      } else if (supportedFormats.etc2) {
        basisFormat = BASIS_FORMAT.cTFETC2_RGBA;
      } else if (supportedFormats.astc) {
        basisFormat = BASIS_FORMAT.cTFASTC_4x4_RGBA;
      } else if (supportedFormats.pvrtc) {
        basisFormat = BASIS_FORMAT.cTFPVRTC1_4_RGB;
      } else {
        // See note on uncompressed transcode above.
        basisFormat = BASIS_FORMAT.cTFRGB565;
      }
    }

    if (basisFormat === undefined) {
      basisFileFail(id, basisFile, 'No supported transcode formats');
      return;
    }

    let webglFormat = BASIS_WEBGL_FORMAT_MAP[basisFormat];

    // If we're not using compressed textures it'll be cheaper to generate
    // mipmaps on the fly, so only transcode a single level.
    if (webglFormat.uncompressed) {
      levels = 1;
    }

    // Gather information about each mip level to be transcoded.
    let mipLevels = [];
    let totalTranscodeSize = 0;

    for (let mipLevel = 0; mipLevel < levels; ++mipLevel) {
      let transcodeSize = basisFile.getImageTranscodedSizeInBytes(IMAGE_INDEX, mipLevel, basisFormat);
      mipLevels.push({
        level: mipLevel,
        offset: totalTranscodeSize,
        size: transcodeSize,
        width: basisFile.getImageWidth(IMAGE_INDEX, mipLevel),
        height: basisFile.getImageHeight(IMAGE_INDEX, mipLevel),
      });
      totalTranscodeSize += transcodeSize;
    }

    // Allocate a buffer large enough to hold all of the transcoded mip levels at once.
    let transcodeData = new Uint8Array(totalTranscodeSize);
    let alphaTranscodeData = needsSecondaryAlpha ? new Uint8Array(totalTranscodeSize) : null;

    // Transcode each mip level into the appropriate section of the overall buffer.
    for (let mipLevel of mipLevels) {
      let levelData = new Uint8Array(transcodeData.buffer, mipLevel.offset, mipLevel.size);
      if (!basisFile.transcodeImage(levelData, IMAGE_INDEX, mipLevel.level, basisFormat, 1, 0)) {
        basisFileFail(id, basisFile, 'transcodeImage failed');
        return;
      }
      if (needsSecondaryAlpha) {
        let alphaLevelData = new Uint8Array(alphaTranscodeData.buffer, mipLevel.offset, mipLevel.size);
        if (!basisFile.transcodeImage(alphaLevelData, IMAGE_INDEX, mipLevel.level, basisFormat, 1, 1)) {
          basisFileFail(id, basisFile, 'alpha transcodeImage failed');
          return;
        }
      }
    }

    basisFile.close();
    basisFile.delete();

    // Post the transcoded results back to the main thread.
    let transferList = [transcodeData.buffer];
    if (needsSecondaryAlpha) {
      transferList.push(alphaTranscodeData.buffer);
    }
    postMessage({
      id: id,
      buffer: transcodeData.buffer,
      alphaBuffer: needsSecondaryAlpha ? alphaTranscodeData.buffer : null,
      webglFormat: webglFormat,
      mipLevels: mipLevels,
      hasAlpha: hasAlpha,
    }, transferList);
  }

  onmessage = (msg) => {
    // Each call to the worker must contain:
    let url = msg.data.url; // The URL of the basis image OR
    let buffer = msg.data.buffer; // An array buffer with the basis image data
    let allowSeparateAlpha = msg.data.allowSeparateAlpha;
    let supportedFormats = msg.data.supportedFormats; // The formats this device supports
    let id = msg.data.id; // A unique ID for the texture

    if (url) {
      // Make the call to fetch the basis texture data
      fetch(url).then(function(response) {
        if (response.ok) {
          response.arrayBuffer().then((arrayBuffer) => {
            if (BasisFile) {
              transcode(id, arrayBuffer, supportedFormats, allowSeparateAlpha);
            } else {
              BASIS_INITIALIZED.then(() => {
                transcode(id, arrayBuffer, supportedFormats, allowSeparateAlpha);
              });
            }
          });
        } else {
          fail(id, `Fetch failed: ${response.status}, ${response.statusText}`);
        }
      });
    } else if (buffer) {
      if (BasisFile) {
        transcode(id, buffer, supportedFormats, allowSeparateAlpha);
      } else {
        BASIS_INITIALIZED.then(() => {
          transcode(id, buffer, supportedFormats, allowSeparateAlpha);
        });
      }
    } else {
      fail(id, `No url or buffer specified`);
    }
  };
}

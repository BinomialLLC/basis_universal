# WebGL Examples

Requires WebAssembly and WebGL support. The WebGL demos are hosted live [here](https://subquantumtech.com/xu/).

To build the encoder and transcoder WASM libraries using [Emscripten](https://emscripten.org/), see the README.md files in the [webgl/transcoder](https://github.com/BinomialLLC/basis_universal/tree/master/webgl/transcoder) and [webgl/encoder](https://github.com/BinomialLLC/basis_universal/tree/master/webgl/encoder) folders. The JavaScript API wrappers to the C/C++ library are located in [`webgl/transcoder/basis_wrappers.cpp`](https://github.com/BinomialLLC/basis_universal/blob/master/webgl/transcoder/basis_wrappers.cpp).

## KTX2 Compression, Transcoding, Display (ktx2_encode_test)

Live demo: [`ktx2_encode_test/index.html'](https://subquantumtech.com/xu/ktx2_encode_test/)

This demo shows how to use the compressor and transcoder from JavaScript. To use it, select a .PNG file then hit the "Encode!" button. The compressor will dynamically generate a .ktx2 file in memory which will then be immediately transcoded and displayed. Hit the "Download!" button to locally download the generated .ktx2 file.

To view the compressor's textual debug output, open your browser's developer debug console (under Developer Tools in Chrome) and enable the Debug checkbox before hitting the "Encode!" button. Multithreading and WASM64 are optionally supported, and a browser supporting both are highly recommended.

![Screenshot showing the encode_test demo](ktx2_encode_test/preview.png)

## Transcoder (texture_test)

Live demo: [webgl/texture_test/index.html](https://subquantumtech.com/xu/texture_test/)

Renders a single texture, using the transcoder (compiled to WASM with emscripten) to generate one of the following compressed texture formats:

* ASTC 4x4 LDR or HDR
* BC1 (no alpha)
* BC3, BC4 or BC5
* ETC1 (no alpha)
* PVRTC 4bpp
* BC6H, BC7

On browsers that don't support any compressed texture format, there's a low-quality fallback code path for opaque LDR textures, and a HDR half float or LDR 32bpp fallback code path for HDR textures.

![Screenshot showing a basis texture rendered as a 2D image in a webpage.](texture_test/preview.png)

*Note: This sample doesn't support all ASTC/XUASTC LDR block sizes yet, just 4x4. See the "ktx2_encode_test" or "video_test" samples, which do.*

## glTF 3D Model

Live demo: [`gltf/index.html`](https://subquantumtech.com/xu/gltf/)

Renders a glTF 3D model with `.basis` texture files, transcoded into one of the following compressed texture formats:

* ASTC
  * Tested in Chrome on Android, Pixel 3 XL.
* DTX (BC1/BC3)
  * Tested in Chrome (Linux and macOS) and Firefox (macOS).
* ETC1
  * Tested in Chrome on Android, Pixel 3 XL.
* PVRTC
  * Tested in Chrome and Safari on iOS iPhone 6 Plus.

The glTF model in this demo uses a hypothetical `GOOGLE_texture_basis` extension. That extension is defined for the sake of example only - the glTF format will officially embed Basis files within a KTX2 wrapper, through a new
extension that is [currently in development](https://github.com/KhronosGroup/glTF/pull/1612).

![Screenshot showing a basis texture rendered as the base color texture for a 3D model in a webpage.](gltf/preview.png)

## Testing locally

You can locally host the files under the "webgl" folder. One way is to use [Python to setup a local webserver](https://pythonbasics.org/webserver/) in the 'webgl' directory:

```
cd webgl
python3 -m http.server 8000
```

**Note: For WASM multithreading to be available and enabled, the server [must be properly configured](https://unlimited3d.wordpress.com/2021/12/21/webassembly-and-multi-threading/). See the `webgl/start_webserver.sh` and `webgl/webserver_cross_origin.py` example scripts.**

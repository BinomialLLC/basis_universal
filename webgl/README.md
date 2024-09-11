# WebGL Examples

Requires WebAssembly and WebGL support. The WebGL demos are hosted live [here](https://subquantumtech.com/uastchdr2/).

To build the encoder and transcoder WASM libraries using Emscripten, see the various README.md files in the 'webgl/transcoder' and 'webgl/encoder' folders. The Javascript API wrappers to the C/C++ library are located in [`webgl/transcoder/basis_wrappers.cpp`](https://github.com/BinomialLLC/basis_universal/blob/master/webgl/transcoder/basis_wrappers.cpp).

## Transcoder (texture_test)

Live demo: [webgl/texture_test/index.html](https://subquantumtech.com/uastchdr2/texture_test/)

Renders a single texture, using the transcoder (compiled to WASM with emscripten) to generate one of the following compressed texture formats:

* ASTC 4x4 LDR or HDR
* BC1 (no alpha)
* BC3, BC4 or BC5
* ETC1 (no alpha)
* PVRTC 4bpp
* BC6H, BC7

On browsers that don't support any compressed texture format, there's a low-quality fallback code path for opaque LDR textures, and a HDR half float or LDR 32bpp fallback code path for HDR textures.

![Screenshot showing a basis texture rendered as a 2D image in a webpage.](texture_test/preview.png)

## glTF 3D Model

Live demo: [`gltf/index.html`](https://subquantumtech.com/uastchdr2/gltf/)

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

## Compressor (ktx2_encode_test)

Live demo: [`ktx2_encode_test/index.html'](https://subquantumtech.com/uastchdr2/ktx2_encode_test/)

This demo shows how to use the compressor from JavaScript. To use it, select a .PNG file then hit the "Encode!" button. The compressor will dynamically generate a .ktx2 file in memory which will then be immediately transcoded and displayed. Hit the "Download!" button to locally download the generated .ktx2 file. 

To view the compressor's textual debug output, open your browser's developer debug console (under Developer Tools in Chrome) and enable the Debug checkbox before hitting the "Encode!" button. Multithreading is not currently supported when the compressor is compiled to WebAssembly, so compression will be slower than using the stand-alone command line tool.

![Screenshot showing the encode_test demo](ktx2_encode_test/preview.png)

## Testing locally

You can locally host the files under the "webgl" folder. One way is to use [Python to setup a local webserver](https://pythonbasics.org/webserver/) in the 'webgl' directory:

```
cd webgl
python3 -m http.server 8000
```

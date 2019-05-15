# WebGL Demo

### Texture Demo

`index.html` uses the transcoder compiled to wasm with emscripten and renders the texture. Currently supporting following texture formats:
* BC1
* BC3

On browsers that don't support BC1 (Firefox is one), there's a low-quality fallback code path for opaque textures (but no fallback for BC3 yet). Note that the fallback path only converts to 16-bit RGB images at the moment, so the quality isn't as good as it should be.

![](texture-demo.png)

### glTF 3D Model Demo
`gltf-demo/index.html` renders a glTF 3D model with `.basis` texture files, transcoded into one of the following compressed texture formats:
* DTX (BC1)
  * Tested in Chrome (Linux and macOS) and Firefox (macOS).
* ETC1
  * Tested in Chrome on Android, Pixel 3 XL.
* PVRTC (COMPRESSED_RGB_PVRTC_4BPPV1_IMG)
  * Tested in Chrome and Safari on iOS iPhone 6 Plus.

The glTF model in this demo uses a hypothetical `GOOGLE_texture_basis` extension. That extension is defined for the sake of example only – the glTF format will officially embed Basis files within a KTX2 wrapper, through a new
extension that is currently in development.

![](model-demo.png)

Both demos requires WebAssembly and WebGL support.

## Testing locally

See [how to run things locally](https://threejs.org/docs/#manual/en/introduction/How-to-run-things-locally), or (with [Node.js](https://nodejs.org/en/) installed), run:

```
npx serve
```

The console will display a `localhost` URL for local testing, and (on supported WiFi networks and devices) may also display an IP address accessible by other devices on the same network. Note that mobile devices must support WebAssembly to run this demo. Learn more about [remote debugging your android devices](https://developers.google.com/web/tools/chrome-devtools/remote-debugging/).

## Building transcoder locally

Prebuilt versions of `basis_transcoder.js` and `basis_transcoder.wasm` are included in the `wasm/build/` folder, and are sufficient for local demos. To build the transcoder yourself, first install emscripten ([tutorial](https://webassembly.org/getting-started/developers-guide/)) and cmake ([download](https://cmake.org/download/)). Then run:

```shell
cd webgl/wasm/build/
emcmake cmake ../
make
```

## Credits

* Contributors:
  * [Don McCurdy](https://www.donmccurdy.com)
  * [Austin Eng](https://github.com/austinEng)
  * [Shrek Shao](https://github.com/shrekshao)
* Made with [three.js](https://threejs.org/).
* Thanks to [AGI](http://agi.com/) for providing the glTF model.

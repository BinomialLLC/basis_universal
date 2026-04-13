`basisu_wrappers.cpp` contains our JavaScript API (implemented via [emscripten](https://emscripten.org/) bindings), which is a thin layer above our encoder and transcoder's C++ API's. As of Basis Universal v2.0 it supports optional WASM multithreading and WASM64.

Prebuilt versions of `basis_transcoder.js` and `basis_transcoder.wasm` are included in the `build/` folder, and are sufficient for local demos. To build the transcoder yourself, first install emscripten ([tutorial](https://webassembly.org/getting-started/developers-guide/)) and cmake ([download](https://cmake.org/download/)). Then run:

```shell
cd webgl/transcoder/build/
emcmake cmake ../
make
```

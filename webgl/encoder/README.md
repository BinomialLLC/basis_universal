Prebuilt versions of `basis_encoder.js` and `basis_encoder.wasm` are included in the `build/` folder, and are sufficient for local demos. Note the encoder also includes the transcoder. To build the encoder yourself, first install emscripten ([tutorial](https://webassembly.org/getting-started/developers-guide/)) and cmake ([download](https://cmake.org/download/)). Then run:

```shell
cd webgl/encoder/build/
emcmake cmake ../
make
```

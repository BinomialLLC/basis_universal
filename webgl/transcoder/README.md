`basisu_wrappers.cpp` contains our JavaScript API (implemented via [emscripten](https://emscripten.org/) bindings), which is a thin layer above our encoder and transcoder's C++ API's. As of Basis Universal v2.0 it supports optional WASM multithreading and WASM64.

Prebuilt versions of `basis_transcoder.js` and `basis_transcoder.wasm` are included in the `build/` folder, and are sufficient for local demos. To build the transcoder yourself, first install emscripten ([tutorial](https://webassembly.org/getting-started/developers-guide/)) and cmake ([download](https://cmake.org/download/)). Then run:

```shell
cd webgl/transcoder/build/
emcmake cmake ../
make
```

### Strict Content-Security-Policy (no `unsafe-eval`)

By default, the generated JavaScript glue uses `new Function()` to build its
embind bindings at runtime. A Content-Security-Policy without `'unsafe-eval'`
blocks this, and module startup fails with an `EvalError`. To build glue that
never calls `eval()` or `new Function()`, add `-DNO_DYNAMIC_EXECUTION=ON`:

```shell
emcmake cmake ../ -DNO_DYNAMIC_EXECUTION=ON
make
```

This adds `-s DYNAMIC_EXECUTION=0 -s EMBIND_AOT=1` to the link flags. The
`.wasm` file does not change. `EMBIND_AOT` generates the bindings at build
time, so JavaScript-to-WASM calls stay as fast as in the default build. The
page still needs `'wasm-unsafe-eval'` in `script-src` to compile the
WebAssembly module. Use `-DEMBIND_AOT=OFF` to get the smaller, generic
bindings, which are slower per call.

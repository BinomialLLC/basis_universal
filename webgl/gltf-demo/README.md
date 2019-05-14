# glTF Demo

A WebGL demo rendering a glTF 3D model with `.basis` texture files transcoded into one of the following compressed texture format:

* DTX (BC1)
  * Tested in chrome on Linux PC 
* ETC1
  * Tested in chrome on Android Pixel 3 XL
* PVRTC (COMPRESSED_RGB_PVRTC_4BPPV1_IMG)
  * Tested in chrome/safari on iOS iPhone 6 Plus

Note the glTF model is using a temperoray hypothetical extension at this moment. It should rely on a KTX2 wrapper when offically released.

## Run on PC

Launuch an http server under `webgl/gltf-demo/`. For example if you have node installed you can install `http-server` globally and run
```
http-server
```

Go to `localhost:8080` in your browser (which should supports WebAssembly and WebGL).

## Run on mobile

If you connect your mobile and PC (hosting the http server) to the same Wifi, you might be able to access the http server directly from the browser on your mobile.

For example:
```
$ http-server
Starting up http-server, serving ./
Available on:
  http://127.0.0.1:8080
  http://100.99.17.28:8080
Hit CTRL-C to stop the server
```

Go to `100.99.17.28:8080` in your mobile browser (which should supports WebAssembly and WebGL).

Or you can [remote debugging your android devices](https://developers.google.com/web/tools/chrome-devtools/remote-debugging/)

## Build Transcoder locally

A prebuild of `basis_transcoder.js` and `basis_transcoder.wasm` is already included to run the demo. You only want to follow this part if you want to build the basis transcoder locally.

Install emscripten ([tutorial](https://webassembly.org/getting-started/developers-guide/)) and cmake ([download](https://cmake.org/download/)) if you haven't yet.

Run the following instructions under `webgl/gltf-demo/wasm/build/`
```shell
emcmake cmake ../
make
```

## Credits

* Contributions: @donmccurdy, @austinEng, @shrekshao
* Three.js
* Thanks AGI for providing the glTF model.
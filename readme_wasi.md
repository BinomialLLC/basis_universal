# README_WASI.md

## Building and running Basis Universal under WASI / Wasmtime

This document describes how to build the `basisu` command-line tool as a WASI (WebAssembly System Interface) executable, and how to run it using Wasmtime.  
WASI builds run the encoder inside a secure, portable WebAssembly sandbox with no native dependencies.

---

## 1. Install Wasmtime

Install Wasmtime using the official installer:

```
curl https://wasmtime.dev/install.sh | bash
```

Verify:

```
wasmtime --version
```

---

## 2. Install WASI-SDK (WASI toolchain)

Download the latest WASI SDK from:

https://github.com/WebAssembly/wasi-sdk/releases/latest
https://github.com/WebAssembly/wasi-sdk/releases

Example (adjust version if needed):

```
wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-29/wasi-sdk-29.0-x86_64-linux.tar.gz
tar xf wasi-sdk-29.0-x86_64-linux.tar.gz
```

---

## 3. Set the WASI_SDK_PATH environment variable

You must set the path so CMake can find the WASI compiler:

```
export WASI_SDK_PATH=/path/to/wasi-sdk-29.0-x86_64-linux
```

Example:

```
export WASI_SDK_PATH=$HOME/wasi-sdk-29.0-x86_64-linux
```

Verify:

```
$WASI_SDK_PATH/bin/clang --version
```

---

## 4. Configure the WASI build using CMake

WASI builds come in two modes:
- Single-threaded (default)
- Multi-threaded (requires wasi-sdk-pthread.cmake and Wasmtime threading flags)

Create a fresh build directory and configure using the WASI toolchain file:

```
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$WASI_SDK_PATH/share/cmake/wasi-sdk-pthread.cmake -DCMAKE_BUILD_TYPE=Release -DBASISU_WASM_THREADING=ON
```

Or for a single threaded build (will run much slower):

```
cmake .. -DCMAKE_TOOLCHAIN_FILE=$WASI_SDK_PATH/share/cmake/wasi-sdk.cmake -DCMAKE_BUILD_TYPE=Release -DBASISU_WASM_THREADING=OFF
```

---

## 5. Build the WASI `.wasm` executable

Build using:

```
make
```

This produces:

```
bin/basisu.wasm
bin/examples.wasm    (if EXAMPLES=ON)
```

---

## 6. Running `basisu.wasm` with Wasmtime

WASI programs are sandboxed and cannot access your filesystem unless you explicitly grant permission.

Use one or more `--dir=` arguments to allow input/output files.

### Run internal test suite for ETC1S

```
bin$ wasmtime run --wasm threads=yes --wasi threads=yes --dir=. --dir=.. --dir=../test_files basisu.wasm -test
```

Use backslashes under Windows: "wasmtime run --wasm threads=yes --wasi threads=yes --dir=. --dir=.. --dir=..\test_files basisu.wasm -test"

For the single threaded wasm executables, "--wasm threads=yes --wasi threads=yes" isn't needed.

A Windows .cmd batch script example:

```
wasmtime --dir=. --dir=.. --dir=..\test_files --dir=d:/dev/test_images::/test_images --dir=d:/dev/test_images/bik::/bik basisu.wasm %*
```

A shell script example:

```
#!/usr/bin/env bash
wasmtime run --dir=. --dir=../test_files --dir=/mnt/d/dev/test_images::/test_images --dir=/mnt/d/dev/test_images/bik::/test_images/bik --wasm threads=yes --wasi threads=yes ./basisu.wasm "$@"
```

### Example: run compression on a PNG to ETC1S

```
wasmtime run --wasm threads=yes --wasi threads=yes --dir=. basisu.wasm xmen.png -stats
```

### Example: transcode a KTX2 file to .ktx/.png/etc.

```
wasmtime run --wasm threads=yes --wasi threads=yes --dir=. basisu.wasm xmen.ktx2

```

---

## Notes

- WASI builds run inside a secure sandbox with no filesystem access unless explicitly granted via `--dir=`.
- The CMake configuration sets a larger stack size to support ASTC/UASTC compression.
- WASI SDK and Wasmtime can be installed anywhere; just update `WASI_SDK_PATH`.

---

## Summary

To build and run BasisU under WASI:

1. Install **Wasmtime**
2. Install **WASI SDK**
3. Set **WASI_SDK_PATH**
4. Run **cmake** using the WASI toolchain in "build" directory
5. Build with **make**
6. Run using **wasmtime** with `--dir=` permissions on .wasm executables in "bin" directory

This produces a safe, portable, sandboxed version of the Basis Universal encoder that runs anywhere.


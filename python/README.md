Python support is still new and coming online, but is entirely functional.
The library's pure C (WASM friendly) API's are completely exposed to Python. Our next goal is to work on official [Wheels](https://pythonwheels.com/), once the API is settled and more examples are written.

The Python integration first tries to use native .so's in the basisu_py
directory. If they don't exist, it tries the slower and single threaded WASM
fallbacks under basisu_py/wasm, which requires wasmtime for Python to be 
installed. Some tests require an input.ktx2 or test.ktx2 to be in the current
directory.

Building:

Under the repo's root directory - build the native SO's:

```
mkdir build_python
cd build_python
cmake -DBASISU_BUILD_PYTHON=ON ..
make
```

Build the WASM modules (see README_WASI.md file for instructions on how to
install the WASI SDK, which is required):

```
mkdir build_wasm_st
cd build_wasm_st
cmake .. -DCMAKE_TOOLCHAIN_FILE=$WASI_SDK_PATH/share/cmake/wasi-sdk.cmake -DCMAKE_BUILD_TYPE=Release -DBASISU_WASM_THREADING=OFF
make
```

---

Running Tests
-------------

The tests assume the current directory is "python". Under Windows we've tested with Python v3.12.10, and under Linux v3.12.13. 

Higher-level tests:

- python3 -m tests.test_backend_loading  
- python3 -m tests.test_basic_wasm_selection  
- python3 -m tests.test_basic_backend_selection  
- python3 -m tests.test_basic_decode  
- python3 -m tests.test_basic_transcode  
- python3 -m tests.test_compress_swirl  
- python3 -m tests.test_compress_swirl_hdr  
- python3 -m tests.test_transcoder_astc  
- python3 -m tests.test_transcoder_backend_loading  
- python3 -m tests.test_transcoder_end_to_end  
- python3 -m tests.test_transcoder_end_to_end_hdr  
- python3 -m tests.test_transcoder_helpers  

Low-level tests (used while bringing up the codec):

- python3 -m lowlevel_test_native.basic_test  
- python3 -m lowlevel_test_native.test_transcoder_basic  
- python3 -m lowlevel_test_native.example_capi_python  
- python3 -m lowlevel_test_wasm.basic_test  
- python3 -m lowlevel_test_wasm.compress_test  
- python3 -m lowlevel_test_wasm.compress_test_float  

Example output:

```
richg@ryzen9:/mnt/c/dev/bu_1_22_snapshot2/basis_universal-master/python$ python3 -m tests.test_backend_loading
========== BACKEND LOADING TEST ==========

Testing native backend...
[Encoder] Using native backend
  [OK] Native backend loaded
Hello from basisu_wasm_api.cpp version 200
  Native get_version() ? 200
  Native alloc() returned ptr = 685784256
  Native free() OK
  [OK] Native basic operations working.


Testing WASM backend...
[WASM Encoder] Loaded: /mnt/c/dev/bu_1_22_snapshot2/basis_universal-master/python/basisu_py/wasm/basisu_module_st.wasm
[Encoder] Using WASM backend
  [OK] WASM backend loaded
Hello from basisu_wasm_api.cpp version 200
  WASM get_version() ? 200
  WASM alloc() returned ptr = 26920160
  WASM free() OK
  [OK] WASM basic operations working.


========== DONE ==========
```

#!/usr/bin/env bash
wasmtime run --dir=. --dir=../test_files --dir=/mnt/d/dev/test_images::/test_images --dir=/mnt/d/dev/test_images/bik::/test_images/bik ./basisu_st.wasm "$@"

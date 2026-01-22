#!/usr/bin/env bash
wasmtime run --dir=. --dir=../test_files --dir=/mnt/d/dev/test_images::/test_images --dir=/mnt/d/dev/test_images/bik::/test_images/bik --wasm threads=yes --wasi threads=yes ./basisu_mt.wasm "$@"

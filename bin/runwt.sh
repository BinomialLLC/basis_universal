#!/usr/bin/env bash
wasmtime run --dir=. --dir=../test_files --wasm threads=yes --wasi threads=yes ./basisu_mt.wasm "$@"

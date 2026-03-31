#!/usr/bin/env bash
wasmtime run --dir=. --dir=../test_files ./basisu_st.wasm "$@"

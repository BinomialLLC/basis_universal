#!/usr/bin/env python3
import subprocess
import shutil
import os
import sys

# -------------------------------------------------------------------
# CONFIGURATION - Easily add new build directories and options.
# -------------------------------------------------------------------
BUILD_CONFIGS = {
    "build_python":  ["cmake", "-DBASISU_SSE=1 -DBASISU_BUILD_PYTHON=ON", ".."],
    "build_wasm_mt": ["cmake", "-DCMAKE_TOOLCHAIN_FILE=$WASI_SDK_PATH/share/cmake/wasi-sdk-pthread.cmake -DCMAKE_BUILD_TYPE=Release -DBASISU_WASM_THREADING=ON", ".."],
    "build_wasm_st": ["cmake", "-DCMAKE_TOOLCHAIN_FILE=$WASI_SDK_PATH/share/cmake/wasi-sdk.cmake -DCMAKE_BUILD_TYPE=Release -DBASISU_WASM_THREADING=OFF", ".."],
    "build_native":  ["cmake", "-DBASISU_SSE=1", ".."]
}
# -------------------------------------------------------------------


def log(msg):
    print(f"[INFO] {msg}")


def run(cmd, work_dir):
    """
    Execute a shell command after changing the working directory.
    Always restore the original directory, even on exceptions.
    """

    if isinstance(cmd, list):
        cmd = " ".join(cmd)

    original_dir = os.getcwd()

    log(f"Preparing to run command:\n  CMD: {cmd}\n  IN:  {work_dir}")
    print(f"[INFO] Current working directory before change: {original_dir}")

    try:
        os.chdir(work_dir)
        print(f"[INFO] Changed working directory to: {os.getcwd()}")

        log(f"Running command: {cmd}")
        subprocess.check_call(cmd, shell=True)

    except subprocess.CalledProcessError:
        log(f"ERROR: Command failed: {cmd}")
        raise

    finally:
        # Always restore the directory
        os.chdir(original_dir)
        print(f"[INFO] Restored working directory to: {original_dir}")


def clean_build_dirs():
    log("Cleaning all build directories...")
    for build_dir in BUILD_CONFIGS:
        if os.path.isdir(build_dir):
            log(f"Deleting directory: {build_dir}")
            shutil.rmtree(build_dir)
        else:
            log(f"Directory not found, skipping: {build_dir}")
    log("Clean complete.\n")


def create_dir(path):
    if not os.path.isdir(path):
        log(f"Creating directory: {path}")
        os.makedirs(path)
    else:
        log(f"Directory already exists: {path}")


def perform_builds():
    for build_dir, cmake_cmd in BUILD_CONFIGS.items():
        log(f"Starting build in: {build_dir}")

        create_dir(build_dir)

        # Run CMake inside the directory
        log(f"Executing CMake for {build_dir}")
        run(cmake_cmd, work_dir=build_dir)

        # Run Make inside the directory
        log(f"Running make for {build_dir}")
        run("make", work_dir=build_dir)

        log(f"Finished build for {build_dir}\n")


def main():
    if "--clean" in sys.argv:
        clean_build_dirs()

    perform_builds()
    log("SUCCESS\n")


if __name__ == "__main__":
    main()

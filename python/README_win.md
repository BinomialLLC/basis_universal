Windows Native Python Build Instructions
========================================

This project uses pybind11 to build Python .pyd extension modules on Windows.
Because Windows installs multiple Python versions, and pybind11 currently only 
supports up to Python 3.12, you must follow these steps exactly.

Requirements
------------
- Visual Studio Developer Command Prompt (VS C++ Build Tools installed)
- Python 3.12 (pybind11 does NOT support 3.13+ at the time of writing)
- pybind11 installed into Python 3.12

Check installed Python versions:
    py -0

If Python 3.12 is missing:
    winget install Python.Python.3.12

Install pybind11 for Python 3.12:
    py -3.12 -m pip install pybind11

IMPORTANT:
You must build AND run with the same Python interpreter version (3.12).

Building the .pyd Modules
-------------------------
Open the "Developer Command Prompt for Visual Studio".

From the project root:

    mkdir build_python_win
    cd build_python_win

Run CMake using the exact path to python.exe for Python 3.12:

    cmake -G "Visual Studio 17 2022" -A x64 -DBASISU_BUILD_PYTHON=ON -DBASISU_BUILD_WASM=OFF -DPYTHON_EXECUTABLE="C:\Users\<YOU>\AppData\Local\Programs\Python\Python312\python.exe" ..

Build:

    cmake --build . --config Release

Output files will be created in:

    python/basisu_py/basisu_python.pyd
    python/basisu_py/basisu_transcoder_python.pyd

Running the Modules
-------------------
Always run using Python 3.12:

    py -3.12

Inside Python:

    import basisu_py
    print("Modules loaded OK.")
	
While in the "python" directory:

     py -m tests.test_backend_loading

WASM Backend (Optional)
-----------------------
Install wasmtime:

    py -3.12 -m pip install wasmtime

Ensure these files exist:

    python/basisu_py/wasm/*.wasm

Common Problems
---------------
1. "pybind11 not found"
   -> Installed into wrong Python version. Use:
        py -3.12 -m pip install pybind11

2. "Python config failure"
   -> You are using Python 3.13 or 3.14. Must use Python 3.12.

3. Modules not loading
   -> You must run them with the same interpreter used to build them:
        py -3.12
		

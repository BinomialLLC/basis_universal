@echo off

echo ===========================================
echo   Building Python extensions (Windows)
echo ===========================================

REM Set the Python executable path (edit if needed)
set PY_EXE=C:\Users\richg\AppData\Local\Programs\Python\Python312\python.exe

REM Ensure Python exists
if not exist "%PY_EXE%" (
    echo ERROR: Python 3.12 executable not found:
    echo   %PY_EXE%
    echo Please install Python 3.12 or update PY_EXE in this script.
    exit /b 1
)

REM Create build directory if missing
if not exist build_python_win (
    echo Creating build_python_win directory...
    mkdir build_python_win
)

cd build_python_win

echo Running CMake configure...
cmake -G "Visual Studio 17 2022" -A x64 ^
    -DBUILD_PYTHON=ON ^
    -DBUILD_WASM=OFF ^
    -DPYTHON_EXECUTABLE="%PY_EXE%" ^
    ..

IF ERRORLEVEL 1 (
    echo.
    echo *** CMake configure FAILED ***
    exit /b 1
)

echo.
echo CMake configure OK.
echo Starting build...

cmake --build . --config Release

IF ERRORLEVEL 1 (
    echo.
    echo *** Build FAILED ***
    exit /b 1
)

echo.
echo ===========================================
echo   Build SUCCESSFUL!
echo   Output in: python/basisu_py/
echo ===========================================

exit /b 0

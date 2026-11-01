# Basisu CMake Installation

This updated CMakeLists.txt adds proper installation support including CMake package configuration files.

## Files Added

1. **CMakeLists.txt** - Updated with install commands
2. **cmake/basisuConfig.cmake.in** - Package configuration template

## Installation

### Building and Installing

```bash
mkdir build
cd build
cmake ..
cmake --build .
sudo cmake --build . --target install
```

Or with custom prefix:

```bash
cmake -DCMAKE_INSTALL_PREFIX=/your/custom/path ..
cmake --build .
cmake --build . --target install
```

## What Gets Installed

### Libraries
- `basisu_encoder` static library → `lib/`

### Executables
- `basisu` command-line tool → `bin/`
- `examples` (if EXAMPLES=TRUE) → `bin/`

### Headers
- Transcoder headers → `include/basisu/transcoder/`
- Encoder headers → `include/basisu/encoder/`
- Zstd header (if ZSTD=TRUE) → `include/basisu/zstd/`

### CMake Package Files
- `basisuTargets.cmake` - Exported targets
- `basisuConfig.cmake` - Package configuration
- `basisuConfigVersion.cmake` - Version info

All installed to: `lib/cmake/basisu/`

## Using in Other CMake Projects

After installation, other CMake projects can find and link against basisu:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyProject)

# Find the installed basisu package
find_package(basisu REQUIRED)

# Link against basisu
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE basisu::basisu_encoder)
```

The `basisu::basisu_encoder` imported target automatically provides:
- Include directories
- Library location
- Compile definitions (if any)
- Transitive dependencies

### Available Targets

- `basisu::basisu_encoder` - The static encoder library

### Legacy Variables (for compatibility)

- `basisu_INCLUDE_DIRS` - Include directory path
- `basisu_LIBRARIES` - Libraries to link against

## Example Usage

```cpp
#include <basisu/transcoder/basisu_transcoder.h>
#include <basisu/encoder/basisu_enc.h>

// Your code here
```

## Notes

- The package follows CMake's `GNUInstallDirs` conventions
- On Linux/macOS, default install prefix is `/usr/local`
- On Windows with MSVC, adjust paths as needed
- Version compatibility is set to `AnyNewerVersion`

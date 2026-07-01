# C++/GLFW Shader Deblocking Sample

*Block boundaries are predictable.*

This is a native C++ port (using GLFW + OpenGL) of the Python+GLSL shader deblocking
sample. It demonstrates how to use a simple pixel shader to greatly reduce ASTC
texture block artifacts, which can be quite noticeable when the block size goes beyond
roughly 6x6. The basic idea: instead of always sampling the texture with a single tap,
you sample either once or several times with a simple low-pass filter, depending on
whether the sample location is near a block edge. The extra taps blur across the block
boundaries. There are two independent filters, for horizontal and vertical boundaries.

The shader is compatible with mipmapping, bilinear/trilinear filtering, and is
temporally stable. It smoothly lerps between no filtering and edge filtering, and is
mipmap-aware by using the pixel shader derivative instructions. Crucially, the block
lattice is evaluated in the *effective mip space*, not in base texture space, which is
why it is mipmap-aware. The sample renders either a textured quad or a cube, with
controls to move the object, rotate the cube, toggle deblocking, change filtering, etc.

Unlike the Python sample, this port loads textures **only from Basis Universal `.KTX2`
files** (the PNG input path was dropped). It links the Basis Universal transcoder
directly (no Python bindings, no separate library), so it is a small, self-contained
native program.

> **Note:** this sample currently supports **LDR textures only** — HDR `.ktx2` files
> are not handled.

---

## What it does and how it works

### The deblocking shader (`shader.glsl`)

The fragment shader is the same GLSL used by the Python sample (loaded unchanged at
runtime). Per pixel it:

1. **Recovers the effective mip level** from the screen-space UV derivatives
   (`dFdx`/`dFdy`), computes the texels-per-pixel density, takes `log2` to get the LOD
   the hardware would select, and snaps to the dominant mip. This makes the deblock
   lattice track whatever mip the GPU is actually sampling.
2. **Computes edge proximity**: the texel's offset within its block (`mod(texelPos,
   blockSize)`) gives a 0..1 weight that ramps up near each block boundary, separately
   for horizontal and vertical seams. Interior texels get weight 0 and are left
   untouched.
3. **Filters and blends**: it samples the 4 axis-neighbors (5 taps total), runs a
   3-tap low-pass across each axis, and `mix()`es from the original sample toward the
   blurred one by the edge weight. Far from a seam = original pixel; on a seam = maximum
   blur. (Press `2` to visualize the edge weight.)

The block size and on/off state are passed to the shader as uniforms (`texSize.zw` and
`const0.x`), driven by the loaded file's metadata.

### Loading a `.KTX2` (the transcoder path)

On startup the program opens the `.KTX2` with `basist::ktx2_transcoder`, inspects it,
and chooses a GPU storage format via a ladder, gated on the OpenGL extensions the
context advertises:

**ASTC → BC7 → ETC2 → uncompressed RGBA8 (fallback).**

It then transcodes every mip level to the chosen format and uploads them
(`glCompressedTexImage2D`, or `glTexImage2D` for the RGBA8 fallback), one call per
level at each level's original (unpadded) width/height.

Two important details, identical to the Python sample:

- **Block-size decoupling.** The deblock filter always uses the *original*
  ASTC/XUASTC block size stored in the `.KTX2` (e.g. 12x12), independent of the GPU
  storage format's block size. The block artifacts are baked into the content by the
  original encode and survive a re-transcode to, say, BC7's 4x4 blocks — so the shader
  filters the original 12x12 lattice even when the texture is stored on the GPU as BC7
  or as uncompressed RGBA8.
- **Deblocking metadata.** Basis Universal writes a `DeblockFilterID` key into the
  `.KTX2` (`ktx2_transcoder::get_deblocking_filter_index()`). When it is 1, the sample
  enables shader deblocking by default; the `1` key still toggles it manually. Because
  the GPU shader performs the deblocking, the transcoder's own CPU deblocking is
  disabled during transcode (the `cDecodeFlagsNoDeblockFiltering` decode flag) to avoid
  double-filtering.

The on-screen overlay's first line reports the source resolution, mip count, block
size, `DeblockID`, and the `.KTX2` format (e.g. `XUASTC LDR 12x12`).

### Note on ASTC and platforms

ASTC is rarely exposed by *desktop* OpenGL drivers — it is mostly a mobile/GLES
feature, and Apple's OpenGL is frozen at 4.1 with no ASTC. So on a typical desktop GPU
(and on macOS) the ladder resolves to **BC7**; if even that is unavailable it falls back
to **uncompressed RGBA8**, so a `.KTX2` always loads. The deblocking effect is identical
regardless of the GPU storage format, because the artifacts are intrinsic to the
content.

---

## Dependencies

- **GLFW** — windowing/input/context. On Windows/macOS it's fetched via **vcpkg**
  (declared in `vcpkg.json`); on Linux it's the system package (`libglfw3-dev`).
- **glad** — the GL function loader, **vendored** in `third_party/glad/` and built as a
  small static library, so no package manager is needed for it on any platform.
- **Basis Universal transcoder** — compiled directly from the repo sources
  (`transcoder/basisu_transcoder.cpp` + `zstd/zstddeclib.c`), not a library.
- **OpenGL** — system library.
- An 8x8 debug font (`g_debug_font8x8_basic`, embedded in `main.cpp`) for the overlay.

No PNG/image library, no NumPy/Pillow equivalents, no Python. C++17, one `CMakeLists.txt`
for MSVC / gcc / clang.

---

## Building

You need **CMake** (3.16+). The only external library is **GLFW**: on Windows/macOS it
comes from **vcpkg** (installed automatically on configure via `vcpkg.json` — substitute
your vcpkg path for `<vcpkg>` below, e.g. `C:\dev\vcpkg`); on Linux it comes from the
system package, so **vcpkg is not needed there**. glad is vendored, and the transcoder is
compiled from the repo sources.

### Windows (MSVC, with Visual Studio project files)

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Debug
```

Then open `build\shader_deblocking_glfw.sln` in Visual Studio, set `deblock` as the
startup project, and build/run (F5). The debugger working directory is set to `bin\`
automatically (via `VS_DEBUGGER_WORKING_DIRECTORY` in `CMakeLists.txt`). The `bin\`
directory is the program's runtime home: it holds the one and only `shader.glsl` plus
the test `.ktx2` files. Set the command argument to one of the `.ktx2` files, e.g.
`kodim26_12x12.ktx2`.

### Linux

No vcpkg needed: GLFW comes from the system package and glad is vendored. Install GLFW +
the GL headers, then build with plain CMake:

```
# Debian/Ubuntu
sudo apt install build-essential cmake libglfw3-dev libgl1-mesa-dev
cmake -S . -B build
cmake --build build -j

# Fedora/RHEL
sudo dnf install gcc-c++ cmake glfw-devel mesa-libGL-devel
# then the same two cmake commands
```

`find_package(glfw3 CONFIG)` picks up the system GLFW (`libglfw3-dev` provides the CMake
config); `OpenGL::GL` resolves Mesa's libGL; the vendored glad in `third_party/glad/`
builds as a static library.

### macOS

With vcpkg (provides GLFW):

```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

Or, without vcpkg, use Homebrew's GLFW (`brew install glfw`) and plain CMake:

```
cmake -S . -B build
cmake --build build
```

(Because the transcoder is native C++ here, the macOS port does **not** need the WASM /
`wasmtime` dependency that the Python sample uses on macOS. macOS OpenGL is 4.1-capped
with no ASTC, so the format ladder resolves to BC7 or the RGBA8 fallback.)

---

## Running

```
deblock <file.ktx2> [--astc | --bc7 | --etc2 | --rgba32] [--nomips]
```

By default the GPU storage format is chosen automatically via the **ASTC → BC7 → ETC2 →
RGBA8** ladder (whichever the GPU supports). An optional flag *prefers* a specific
format — `--astc`, `--bc7`, `--etc2`, or `--rgba32` (uncompressed) — which is used when
the GPU and file support it; otherwise it falls back to the default ladder. The chosen
format is printed to stdout (e.g. `GPU format : BC7  GL_COMPRESSED_RGBA_BPTC_UNORM (0x8E8C)`).

`--nomips` uploads only the base mip level (level 0) instead of the full mip chain — handy
for inspecting the deblocking on the largest level. (HDR `.ktx2` files are rejected with
an error; this sample is LDR-only.)

The build places the executable in `bin/`, which also holds the single `shader.glsl`
and the test `.ktx2` files, so it can be run in place. The shader is loaded from
`shader.glsl` in the working directory, so run from `bin/`:

```
# Linux / macOS
cd bin
./deblock kodim26_12x12.ktx2
./deblock kodim26_12x12.ktx2 --etc2     # prefer ETC2 if available

# Windows
cd bin
deblock.exe kodim26_12x12.ktx2
```

(In Visual Studio just press F5 — the debugger working directory is already set to
`bin/`, and the exe is built there.)

Expected console output (on a typical desktop GPU):

```
OpenGL: 3.3.0 ...
GPU compressed format support:  ASTC=0  BC7=1  ETC2=1
Loading KTX2: kodim26_12x12.ktx2
  Source     : 1118x1105  levels=11  fmt=XUASTC LDR 12x12
  GPU format : BC7  GL_COMPRESSED_RGBA_BPTC_UNORM (0x8E8C)  block=4x4
  Deblock    : ON  filter block=12x12
  Uploaded 11 mip level(s).
```

---

## Controls

```
Arrows      Move quad left/right/up/down
W / S       Move closer / farther
A / D       Rotate yaw (cube mode)
Q / E       Rotate pitch (cube mode)
Shift       Hold to move/rotate at 1/3 speed
C           Toggle cube / quad mode
B           Bilinear filtering
T           Trilinear filtering
P           Point filtering
R           Reload shader.glsl
1           Toggle deblocking shader off/on   (const0.x)
2           Toggle edge visualization          (const0.y; only when deblocking active)
3 / 4       Toggle spare shader const0.z / const0.w
5-8         Toggle spare shader const1.x / y / z / w
Space       Reset to initial state
Esc         Quit
```

Texture filtering defaults to trilinear. Deblocking defaults to on when the file's
`DeblockFilterID` is 1.

---

## Relationship to the Python sample

This is a faithful port of `python/shader_deblocking/testbed.py` minus PNG input. The
GLSL shader is byte-identical, and the transcode/upload logic, format ladder, deblock
metadata handling, matrices, geometry, controls, and overlay all mirror the Python.
The Python sample remains the reference for the PNG path and for experimentation via
its bindings.

---

## Credits

The deblocking technique and shader are from Binomial LLC. Licensed under Apache 2.0.

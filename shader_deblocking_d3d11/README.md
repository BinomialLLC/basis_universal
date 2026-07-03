# shader_deblocking_d3d11

Windows / Direct3D 11 port of the [shader_deblocking_glfw](../shader_deblocking_glfw) OpenGL sample:
same assets, same behavior, same controls, same shader logic — only the graphics API layer changes.
The primary deliverable is [bin/deblock.hlsl](bin/deblock.hlsl), which contains the deblocking
operator as a reusable `DeblockSample()` function that engine integrators can lift verbatim.

Two facts to understand before using the shader:

1. **This is an in-loop filter.** The Basis Universal encoder factors this exact deblocking
   operator into its rate/quality decisions — it is the decoder half of the codec, not an optional
   post-process. When a KTX2 file's `DeblockingFilterIndex == 1`, viewers should enable the GPU
   deblock shader by default, and the CPU transcoder must be passed
   `cDecodeFlagsNoDeblockFiltering` so the image is not deblocked twice.

2. **The filter block size is a property of the encoded SOURCE, not the GPU format.** On desktop
   D3D there is no ASTC support, so textures transcode to BC7 — but transcoding changes the
   container, not the artifact lattice. An 8x8 XUASTC source transcoded to BC7 still carries seams
   on the 8x8 source grid, and the shader must deblock at 8x8. The block size passed to the shader
   comes from the KTX2 header (`get_block_width()`/`get_block_height()`), never from the chosen GPU
   format.

## Building

```
cmake -B build -S .
cmake --build build --config Release
```

No dependencies beyond the C runtime, Win32, D3D11, and D3DCompiler. The Basis Universal
transcoder is compiled in directly. The executable is written to `bin/`, next to `deblock.hlsl`
and the test `.ktx2` files, and loads the shader from the working directory at runtime (edit
`deblock.hlsl` and press `R` to reload).

## Running

```
cd bin
deblock_d3d11 kodim26_12x12.ktx2            (12x12 XUASTC; deblock auto-enables)
deblock_d3d11 t8x8.ktx2                     (8x8 test texture)
deblock_d3d11 <file.ktx2> [--bc7|--rgba32] [--nomips]
```

GPU format selection: BC7 when the base mip is 4-aligned (or the texture is single-level, in
which case the dims are rounded up to the 4x4 block grid — the padding is encoder-duplicated edge
texels); otherwise uncompressed RGBA8. See the GPU-format-selection comment in
`load_ktx2_texture` in [main.cpp](main.cpp).
LDR textures only.

## Controls

Arrows move, `W`/`S` zoom, `A`/`D` yaw, `Q`/`E` pitch, `Shift` slow, `C` cube/quad,
`P`/`B`/`T` point/bilinear/trilinear, `1` deblock on/off, `2` block-edge weight visualization,
`3`-`8` spare constant toggles, `R` reload shader, `Space` reset, `Esc` quit.

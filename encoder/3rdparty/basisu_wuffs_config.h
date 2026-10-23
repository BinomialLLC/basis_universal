// basisu_wuffs_config.h
// Wuffs (wuffs-v0.4.c) configuration for basisu's LDR image loader (BASISU_USE_WUFFS_LDR_LOADER, see basisu_enc.h).
// Included before wuffs-v0.4.c by BOTH encoder/3rdparty/basisu_wuffs.cpp (the implementation) and encoder/basisu_enc.cpp
// (declarations only), so the declarations always match the compiled implementation. Do not include it anywhere else.
#pragma once

// Only the modules the PNG, TGA, JPEG and QOI decoders need. The base module is limited to its core, interfaces and pixel
// conversion sub-modules (no base64/floatconv/intconv/utf8/magic).
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE__CORE
#define WUFFS_CONFIG__MODULE__BASE__INTERFACES
#define WUFFS_CONFIG__MODULE__BASE__PIXCONV
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__ZLIB
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__MODULE__JPEG
#define WUFFS_CONFIG__MODULE__TARGA
#define WUFFS_CONFIG__MODULE__QOI

// We only ever ask for 8-bit RGBA: compile out every other destination pixel format's swizzlers.
#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ENABLE_ALLOWLIST
#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_RGBA_NONPREMUL

// No SIMD in Wuffs: portable scalar code only, on every compiler/arch. This also removes Wuffs' cpuid and
// __attribute__((target)) dispatch and its MSVC "/arch:AVX2" #pragma message.
#define WUFFS_CONFIG__AVOID_CPU_ARCH

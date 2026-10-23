// basisu_wuffs.cpp
// Compiles Google's Wuffs (https://github.com/google/wuffs, encoder/3rdparty/wuffs-v0.4.c, unmodified) for basisu's LDR image loader
// (BASISU_USE_WUFFS_LDR_LOADER, see basisu_enc.h; the adapter is in basisu_enc.cpp).
// Wuffs: Copyright 2017 The Wuffs Authors. Dual-licensed Apache-2.0 OR MIT; basis_universal uses it under the Apache License,
// Version 2.0 (LICENSES/Apache-2.0.txt, .reuse/dep5).
// Wuffs' C code is generated, so its warnings are silenced here, in this translation unit only. With BASISU_USE_WUFFS_LDR_LOADER
// set to 0 this file compiles to nothing.
#include "../basisu_enc.h"

#if BASISU_USE_WUFFS_LDR_LOADER

#include "basisu_wuffs_config.h"
#define WUFFS_IMPLEMENTATION

#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push, 0)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#if defined(__clang__)
#pragma clang diagnostic ignored "-Weverything"
#else
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
#endif

#include "wuffs-v0.4.c"

#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#endif // BASISU_USE_WUFFS_LDR_LOADER

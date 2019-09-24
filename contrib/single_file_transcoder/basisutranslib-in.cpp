/**
 * Basis Universal single file library. Generated using:
 * \code
 *	./combine.sh -r ../../transcoder -o basisutranslib.cpp basisutranslib-in.cpp
 * \endcode
 * 
 * \todo -Wunused-parameter errors when removing BASISD_SUPPORT_ETC2_EAC_A8
 * \todo enable and test BASISD_SUPPORT_ASTC in browsers
 * \todo BASISD_SUPPORT_DXT5A needs enabling for BC3?
 */

/*
 * Build options for known platforms.
 */
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#if TARGET_OS_IPHONE
#define BASISD_SUPPORT_DXT1 0
#define BASISD_SUPPORT_DXT5A 0
#endif
#if TARGET_OS_IPHONE || defined(__EMSCRIPTEN__)
#define BASISD_SUPPORT_BC7 0
#define BASISD_SUPPORT_ASTC 0
#define BASISD_SUPPORT_ATC 0
#endif

#include "basisu_transcoder.cpp"

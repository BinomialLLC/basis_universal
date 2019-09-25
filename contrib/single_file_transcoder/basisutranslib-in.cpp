/**
 * Basis Universal single file library. Generated using:
 * \code
 *	./combine.sh -r ../../transcoder -o basisutranslib.cpp basisutranslib-in.cpp
 * \endcode
 * 
 * \note BASISD_SUPPORT_DXT5A needs enabling for BC3
 * 
 * \todo remove the need for -Wno-pragma-once-outside-header when compiling standalone
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
#define BASISD_SUPPORT_ATC 0
#endif

#include "basisu_transcoder.cpp"

/**
 * Collection of unused functions and const variables to work around \c
 * -Wunused-function and \c -Wunused-const-variable warnings.
 * 
 * \todo LTO does its thing so any unused are removed but is there a better way?
 */
void _basisu_translib_dummy() {
	// These first ones are not used at all
	BASISU_NOTE_UNUSED(basisu::byteswap16);
	BASISU_NOTE_UNUSED(basisu::byteswap32);
	BASISU_NOTE_UNUSED(basisu::BASISU_PATH_SEPERATOR_CHAR);
	BASISU_NOTE_UNUSED(basisu::cHuffmanTotalSortedCodelengthCodes);
	BASISU_NOTE_UNUSED(basist::COLOR5_PAL0_DELTA_LO);
	BASISU_NOTE_UNUSED(basist::COLOR5_PAL0_DELTA_HI);
	BASISU_NOTE_UNUSED(basist::COLOR5_PAL1_DELTA_LO);
	BASISU_NOTE_UNUSED(basist::COLOR5_PAL1_DELTA_HI);
	BASISU_NOTE_UNUSED(basist::COLOR5_PAL2_DELTA_LO);
	BASISU_NOTE_UNUSED(basist::COLOR5_PAL2_DELTA_HI);
	BASISU_NOTE_UNUSED(basist::COLOR5_PAL2_PREV_HI);
	BASISU_NOTE_UNUSED(basist::COLOR5_PAL_MIN_DELTA_B_RUNLEN);
	BASISU_NOTE_UNUSED(basist::COLOR5_PAL_DELTA_5_RUNLEN_VLC_BITS);
	BASISU_NOTE_UNUSED(basist::NO_ENDPOINT_PRED_INDEX);
	BASISU_NOTE_UNUSED(basist::MAX_SELECTOR_HISTORY_BUF_SIZE);
#if BASISD_SUPPORT_ETC2_EAC_A8
	// Unused but only when building with EAC
	BASISU_NOTE_UNUSED(basist::g_eac_a8_modifier_table);
#endif
#if BASISD_SUPPORT_PVRTC1
	// Unused but only when building with PVRTC
	BASISU_NOTE_UNUSED(basist::g_pvrtc_bilinear_weights);
#else
	// Unused only when not building with PVRTC
	BASISU_NOTE_UNUSED(basist::g_etc1_inten_tables16);
	BASISU_NOTE_UNUSED(basist::g_etc1_inten_tables48);
	BASISU_NOTE_UNUSED(basist::g_etc_5_to_8);
	BASISU_NOTE_UNUSED(basist::g_etc1_x_selector_unpack);
#endif
}

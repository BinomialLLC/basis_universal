/**
 * Basis Universal single file library. Generated using:
 * \code
 *	./combine.sh -r ../../transcoder -x basisu_transcoder_tables_bc7_m6.inc -o basisu_transcoder.cpp basisu_transcoder-in.cpp
 * \endcode
 * 
 * \note The script above excludes the BC7 mode 6 tables, a choice reflected in
 * the build options.
 */

/*
 * Transcoder build options for known platforms (iOS has ETC, ASTC and PVRTC;
 * Emscripten adds DXT to iOS's options; Android adds PVRTC2 to Emscripten's
 * options; other platforms build all except BC7 mode 6 and FXT1).
 * 
 * See https://github.com/BinomialLLC/basis_universal#shrinking-the-transcoders-compiled-size
 */
#ifdef __APPLE__
	#include <TargetConditionals.h>
#endif
#if TARGET_OS_IPHONE
	#define BASISD_SUPPORT_DXT1  0
	#define BASISD_SUPPORT_DXT5A 0
#endif
#if TARGET_OS_IPHONE || defined(__EMSCRIPTEN__) || defined(__ANDROID__)
	#define BASISD_SUPPORT_BC7 0
	#define BASISD_SUPPORT_ATC 0
	#ifndef __ANDROID__
		#define BASISD_SUPPORT_PVRTC2 0
	#endif
#else
	#define BASISD_SUPPORT_BC7_MODE6_OPAQUE_ONLY 0
#endif
#define BASISD_SUPPORT_FXT1 0

#include "basisu_transcoder.cpp"

/**
 * Collection of unused functions and const variables to work around \c
 * -Wunused-function and \c -Wunused-const-variable warnings.
 * 
 * \todo LTO does its thing so any unused are removed but is there a better way?
 */
void _basisu_translib_dummy() {
	// These first ones are not used at all
	BASISU_NOTE_UNUSED(&basisu::byteswap16);
	BASISU_NOTE_UNUSED(&basisu::byteswap32);
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
	BASISU_NOTE_UNUSED(basist::g_eac_modifier_table);
#endif
#if BASISD_SUPPORT_PVRTC1 == 0
	// Unused only when not building with PVRTC
	BASISU_NOTE_UNUSED(basist::g_etc1_inten_tables16);
	BASISU_NOTE_UNUSED(basist::g_etc1_inten_tables48);
	BASISU_NOTE_UNUSED(basist::g_etc_5_to_8);
	BASISU_NOTE_UNUSED(basist::g_etc1_x_selector_unpack);
#endif
}

/**
 * \file simple.cpp
 * Bare minimum example of using the single-file \c basisu_transcoder.cpp.
 * Opens an embedded \c .basis file to test that amalgamating the transcoder
 * worked.
 * \n
 * Compile using:
 * \code
 *	cc -std=c++11 -lstdc++ simple.cpp
 * \endcode
 * 
 * Example code released under a CC0 license.
 */
#include "../basisu_transcoder.cpp"

using namespace basist;

//********************************* Test Data ********************************/

/**
 * Basis Universal compressed 256x256 RGB texture source (with mipmaps).
 * \n
 * See \c testcard.png for the original. Generate using:
 * \code
 *	basisu -comp_level 5 -linear -global_sel_pal -y_flip -mipmap
 * \endcode
 */
static uint8_t const srcRgb[] = {
#include "testcard.basis.inc"
};

//****************************************************************************/

/**
 * Shared codebook instance.
 */
static etc1_global_selector_codebook* globalCodebook = NULL;

/**
 * Simple single-file test to test the transcoder can build and run.
 */
int main() {
	basisu_transcoder_init();
	if (!globalCodebook) {
		 globalCodebook = new etc1_global_selector_codebook(g_global_selector_cb_size, g_global_selector_cb);
	}
	basisu_transcoder transcoder(globalCodebook);
	if (transcoder.validate_header(srcRgb, sizeof srcRgb)) {
		basisu_file_info fileInfo;
		if (transcoder.get_file_info(srcRgb, sizeof srcRgb, fileInfo)) {
			basisu_image_info info;
			if (transcoder.get_image_info(srcRgb, sizeof srcRgb, info, 0)) {
				printf("Success (file w: %d, h: %d, mips: %d)\n",
					info.m_width, info.m_height, info.m_total_levels);
				return EXIT_SUCCESS;
			}
		}
	}
	return EXIT_FAILURE;
}

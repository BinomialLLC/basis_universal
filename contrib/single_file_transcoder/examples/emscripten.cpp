/**
 * \file emscripten.cpp
 * Emscripten example of using the single-file \c basisu_transcoder.cpp. Draws
 * a rotating textured quad with data from the in-line compressed textures.
 * \n
 * Compile using:
 * \code
 *	export "CC_FLAGS=-std=c++11 -Wall -Wextra -Werror -Os -g0 -flto --llvm-lto 3 -fno-exceptions -fno-rtti -lGL -DNDEBUG=1"
 *	export "EM_FLAGS=-s ENVIRONMENT=web -s WASM=1 --shell-file shell.html --closure 1"
 *	emcc $CC_FLAGS $EM_FLAGS -o out.html emscripten.cpp
 * \endcode
 * Alternatively include \c basisu_transcoder.h and compile \c
 * basisu_transcoder.cpp separately (the resulting binary is exactly the same
 * size):
 * \code
 *	emcc $CC_FLAGS $EM_FLAGS -o out.html ../basisu_transcoder.cpp emscripten.cpp
 * \encode
 * To determine the WebAssembly size without the transcoder comment the \c
 * basisu_transcoder.cpp include (which stubs the texture creation).
 * \n
 * Example code released under a CC0 license.
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "../basisu_transcoder.cpp"

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

/**
 * Basis Universal compressed 256x256 RGBA texture source (with mipmaps).
 * \n
 * See \c testcard-rgba.png for the original. Generate using:
 * \code
 *	basisu -comp_level 5 -linear -global_sel_pal -y_flip -mipmap
 * \endcode
 */
static uint8_t const srcRgba[] = {
#include "testcard-rgba.basis.inc"
};

//*************************** Program and Shaders ***************************/

/**
 * Program object ID.
 */
static GLuint progId = 0;

/**
 * Vertex shader ID.
 */
static GLuint vertId = 0;

/**
 * Fragment shader ID.
 */
static GLuint fragId = 0;

//********************************* Uniforms *********************************/

/**
 * Quad rotation angle ID.
 */
static GLint uRotId = -1;

/**
 * Texture ID.
 */
static GLint uTx0Id = -1;

//******************************* Shader Source ******************************/

/**
 * Vertex shader to draw texture mapped polys with an applied rotation.
 */
static GLchar const vertShader2D[] =
#if GL_ES_VERSION_2_0
	"#version 100\n"
	"precision mediump float;\n"
#else
	"#version 120\n"
#endif
	"uniform   float uRot;"	// rotation
	"attribute vec2  aPos;"	// vertex position coords
	"attribute vec2  aUV0;"	// vertex texture UV0
	"varying   vec2  vUV0;"	// (passed to fragment shader)
	"void main() {"
	"	float cosA = cos(radians(uRot));"
	"	float sinA = sin(radians(uRot));"
	"	mat3 rot = mat3(cosA, -sinA, 0.0,"
	"					sinA,  cosA, 0.0,"
	"					0.0,   0.0,  1.0);"
	"	gl_Position = vec4(rot * vec3(aPos, 1.0), 1.0);"
	"	vUV0 = aUV0;"
	"}";

/**
 * Fragment shader for the above polys.
 */
static GLchar const fragShader2D[] =
#if GL_ES_VERSION_2_0
	"#version 100\n"
	"precision mediump float;\n"
#else
	"#version 120\n"
#endif
	"uniform sampler2D uTx0;"
	"varying vec2      vUV0;" // (passed from fragment shader)
	"void main() {"
	"	gl_FragColor = texture2D(uTx0, vUV0);"
	"}";

/**
 * Helper to compile a shader.
 * 
 * \param type shader type
 * \param text shader source
 * \return the shader ID (or zero if compilation failed)
 */
static GLuint compileShader(GLenum const type, const GLchar* text) {
	GLuint shader = glCreateShader(type);
	if (shader) {
		glShaderSource (shader, 1, &text, NULL);
		glCompileShader(shader);
		GLint compiled;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (compiled) {
			return shader;
		} else {
			GLint logLen;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
			if (logLen > 1) {
				GLchar*  logStr = static_cast<GLchar*>(malloc(logLen));
				glGetShaderInfoLog(shader, logLen, NULL, logStr);
			#ifndef NDEBUG
				printf("Shader compilation error: %s\n", logStr);
			#endif
				free(logStr);
			}
			glDeleteShader(shader);
		}
	}
	return 0;
}

//********************************** Helpers *********************************/

/**
 * Vertex position index.
 */
#define GL_VERT_POSXY_ID 0

/**
 * Vertex UV0 index.
 */
#define GL_VERT_TXUV0_ID 1

/**
 * \c GL vec2 storage type.
 */
struct vec2 {
	float x;
	float y;
};

/**
 * Combined 2D vertex and 2D texture coordinates.
 */
struct posTex2d {
	struct vec2 pos;
	struct vec2 uv0;
};

/**
 * Shortcut for \c emscripten_webgl_enable_extension().
 */
#ifndef GL_HAS_EXT
#define GL_HAS_EXT(ctx, ext) emscripten_webgl_enable_extension(ctx, ext)
#endif

/*
 * Possibly missing GL enums.
 * 
 * Note: GL_COMPRESSED_RGB_ETC1_WEBGL is the same as GL_ETC1_RGB8_OES
 */
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif
#ifndef GL_COMPRESSED_RGB8_ETC2
#define GL_COMPRESSED_RGB8_ETC2 0x9274
#endif
#ifndef GL_COMPRESSED_RGBA8_ETC2_EAC
#define GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
#endif
#ifndef COMPRESSED_RGBA_ASTC_4x4_KHR
#define COMPRESSED_RGBA_ASTC_4x4_KHR 0x93B0
#endif

//***************************** Basis Universal ******************************/

/*
 * All of the BasisU code is within this block to enable building with or
 * without the library. Not including the transcoder will build a dummy
 * implementation to (roughly) determine the size.
 */
#ifdef BASISD_LIB_VERSION

using namespace basist;

/**
 * Shared codebook instance.
 */
static etc1_global_selector_codebook* globalCodebook = NULL;

/**
 * Returns a supported compressed texture format for a given context.
 * 
 * \param[in] ctx WebGL context
 * \param[in] alpha \c true if the texture has an alpha channel
 * \return corresponding Basis format
 */
static transcoder_texture_format supports(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE const ctx, bool const alpha) {
#if BASISD_SUPPORT_PVRTC1 || !defined(BASISD_SUPPORT_PVRTC1)
	/*
	 * Test for both prefixed and non-prefixed versions. This should grab iOS
	 * and other ImgTec GPUs first as a preference.
	 * 
	 * TODO: do older iOS expose ASTC to the browser and does it transcode to RGBA?
	 */
	static bool const pvr = GL_HAS_EXT(ctx, "WEBKIT_WEBGL_compressed_texture_pvrtc")
						 || GL_HAS_EXT(ctx,        "WEBGL_compressed_texture_pvrtc");
	if (pvr) {
		return (alpha)
			? transcoder_texture_format::cTFPVRTC1_4_RGBA
			: transcoder_texture_format::cTFPVRTC1_4_RGB;
	}
#endif
#if BASISD_SUPPORT_ASTC || !defined(BASISD_SUPPORT_ASTC)
	/*
	 * Then Android, ChromeOS and others with ASTC (newer iOS devices should
	 * make the list but don't appear to be exposed from WebGL).
	 */
	static bool const astc = GL_HAS_EXT(ctx, "WEBGL_compressed_texture_astc");
	if (astc) {
		return transcoder_texture_format::cTFASTC_4x4_RGBA;
	}
#endif
#if BASISD_SUPPORT_DXT1 || !defined(BASISD_SUPPORT_DXT1)
	/*
	 * We choose DXT next, since a worry is the browser will claim ETC support
	 * then transcode (transcoding slower and with more artefacts). This gives
	 * us desktop and various (usually Intel) Android devices.
	 */
	static bool const dxt = GL_HAS_EXT(ctx,        "WEBGL_compressed_texture_s3tc")
						 || GL_HAS_EXT(ctx, "WEBKIT_WEBGL_compressed_texture_s3tc");
	if (dxt) {
		return (alpha)
			? transcoder_texture_format::cTFBC3_RGBA
			: transcoder_texture_format::cTFBC1_RGB;
	}
#endif
#if BASISD_SUPPORT_ETC2_EAC_A8 || !defined(BASISD_SUPPORT_ETC2_EAC_A8)
	/*
	 * Then ETC2 (which may be incorrect).
	 */
	static bool const etc2 = GL_HAS_EXT(ctx, "WEBGL_compressed_texture_etc");
	if (etc2) {
		return (alpha)
			? transcoder_texture_format::cTFETC2_RGBA
			: transcoder_texture_format::cTFETC1_RGB;
	}
#endif
	/*
	 * Finally ETC1, falling back on RGBA.
	 * 
	 * TODO: we might just prefer to transcode to dithered 565 once available
	 */
	static bool const etc1 = GL_HAS_EXT(ctx, "WEBGL_compressed_texture_etc1");
	if (etc1 && !alpha) {
		return transcoder_texture_format::cTFETC1_RGB;
	}
	/*
	 * We choose 8888 over 4444 and 565 (in the hope that is is never chosen).
	 */
	return transcoder_texture_format::cTFRGBA32;
}

/**
 * Returns the equivalent GL type given a BasisU type.
 * 
 * \note This relies on \c #supports() returning the supported formats, and so
 * only converts to the GL equivalents (without further testing for support).
 * 
 * \param[in] type BasisU transcode target
 * \return equivalent GL type
 */
static GLenum toGlType(transcoder_texture_format const type) {
	switch (type) {
	case transcoder_texture_format::cTFETC1_RGB:
		return GL_ETC1_RGB8_OES;
	case transcoder_texture_format::cTFETC2_RGBA:
		return GL_COMPRESSED_RGBA8_ETC2_EAC;
	case transcoder_texture_format::cTFBC1_RGB:
		return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
	case transcoder_texture_format::cTFBC3_RGBA:
		return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
	case transcoder_texture_format::cTFPVRTC1_4_RGB:
		return GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG;
	case transcoder_texture_format::cTFPVRTC1_4_RGBA:
		return GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG;
	case transcoder_texture_format::cTFASTC_4x4_RGBA:
		return GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
	case transcoder_texture_format::cTFRGBA32:
		return GL_UNSIGNED_BYTE;
	case transcoder_texture_format::cTFRGB565:
		return GL_UNSIGNED_SHORT_5_6_5;
	default:
		return GL_UNSIGNED_SHORT_4_4_4_4;
	}
}

/**
 * Uploads the texture.
 * 
 * \param[in] ctx ctx WebGL context
 * \param[in] name texture \e name
 * \param[in] data \c .basis file content
 * \param[in] size number of bytes in \a data
 * \return \c true if the texture was decoded and created
 * 
 * \todo reuse the decode buffer (the first mips level should be able to contain the rest)
 */
bool upload(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE const ctx, GLuint const name, const uint8_t* const data, size_t const size) {
	basisu_transcoder_init();
	if (!globalCodebook) {
		 globalCodebook = new etc1_global_selector_codebook(g_global_selector_cb_size, g_global_selector_cb);
	}
	basisu_transcoder transcoder(globalCodebook);
	bool success = false;
	if (transcoder.validate_header(data, size)) {
		glBindTexture(GL_TEXTURE_2D, name);
		basisu_file_info fileInfo;
		if (transcoder.get_file_info(data, size, fileInfo)) {
			transcoder_texture_format type = supports(ctx, fileInfo.m_has_alpha_slices);
			basisu_image_info info;
			if (transcoder.get_image_info(data, size, info, 0)) {
				printf("Transcoding to type: %s (w: %d, h: %d, mips: %d)\n",
					basis_get_format_name(type), info.m_width, info.m_height,
						info.m_total_levels);
				if (transcoder.start_transcoding(data, size)) {
					uint32_t descW, descH, blocks;
					for (uint32_t level = 0; level < info.m_total_levels; level++) {
						// reset per level
						success = false;
						if (transcoder.get_image_level_desc(data, size, 0, level, descW, descH, blocks)) {
							uint32_t decSize;
							if (type == transcoder_texture_format::cTFPVRTC1_4_RGB ||
								type == transcoder_texture_format::cTFPVRTC1_4_RGBA)
							{
								decSize = (std::max(8U, (descW + 3) & ~3) *
										   std::max(8U, (descH + 3) & ~3) * 4 + 7) / 8;
							} else {
								decSize = basis_get_bytes_per_block_or_pixel(type);
								if (basis_transcoder_format_is_uncompressed(type)) {
									decSize *= descW * descH;
								} else {
									decSize *= blocks;
								}
							}
							if (void* decBuf = malloc(decSize)) {
								if (basis_transcoder_format_is_uncompressed(type)) {
									// note that blocks becomes total number of pixels for RGB/RGBA
									blocks = descW * descH;
								}
								if (transcoder.transcode_image_level(data, size, 0, level, decBuf, blocks, type)) {
									if (basis_transcoder_format_is_uncompressed(type)) {
										glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA,
											descW, descH, 0, GL_RGBA, toGlType(type), decBuf);
									} else {
										glCompressedTexImage2D(GL_TEXTURE_2D, level,
											toGlType(type), descW, descH, 0, decSize, decBuf);
									}
									success = true;
								}
								free(decBuf);
							}
						}
						if (!success) {
							break;
						}
					}
				}
			}
		}
	}
	return success;
}

#else
// dummy implementation
bool upload(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE /*ctx*/, GLuint /*name*/, const uint8_t* data, size_t size) {
	return (data[0] | data[size - 1]) != 0;
}
#endif

//****************************************************************************/

/**
 * Current quad rotation angle (in degrees, updated per frame).
 */
static float rotDeg = 0.0f;

/**
 * Decoded textures (0 = opaque, 1 = transparent).
 */
static GLuint txName[2] = {};

/**
 * Emscripten (single) GL context.
 */
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE glCtx = 0;

/**
 * Emscripten resize handler.
 */
static EM_BOOL resize(int /*type*/, const EmscriptenUiEvent* /*e*/, void* /*data*/) {
	double surfaceW;
	double surfaceH;
	if (emscripten_get_element_css_size   ("#canvas", &surfaceW, &surfaceH) == EMSCRIPTEN_RESULT_SUCCESS) {
		emscripten_set_canvas_element_size("#canvas",  surfaceW,  surfaceH);
		if (glCtx) {
			glViewport(0, 0, (int) surfaceW, (int) surfaceH);
		}
	}
	return EM_FALSE;
}

/**
 * Boilerplate to create a WebGL context.
 */
static EM_BOOL initContext() {
	// Default attributes
	EmscriptenWebGLContextAttributes attr;
	emscripten_webgl_init_context_attributes(&attr);
	if ((glCtx = emscripten_webgl_create_context("#canvas", &attr))) {
		// Bind the context and fire a resize to get the initial size
		emscripten_webgl_make_context_current(glCtx);
		emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, NULL, EM_FALSE, resize);
		resize(0, NULL, NULL);
		return EM_TRUE;
	}
	return EM_FALSE;
}

/**
 * Called once per frame (clears the screen and draws the rotating quad).
 */
static void tick() {
	glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	if (uRotId >= 0) {
		glUniform1f(uRotId, rotDeg);
		rotDeg += 0.1f;
		if (rotDeg >= 360.0f) {
			rotDeg -= 360.0f;
		}
		glBindTexture(GL_TEXTURE_2D, txName[(lround(rotDeg / 45) & 1) != 0]);
	}
	
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
	glFlush();
}

/**
 * Creates the GL context, shaders and quad data, decompresses the .basis files
 * and 'uploads' the resulting textures.
 */
int main() {
	if (initContext()) {
		// Compile shaders and set the initial GL state
		if ((progId = glCreateProgram())) {
			 vertId = compileShader(GL_VERTEX_SHADER,   vertShader2D);
			 fragId = compileShader(GL_FRAGMENT_SHADER, fragShader2D);
			 
			 glBindAttribLocation(progId, GL_VERT_POSXY_ID, "aPos");
			 glBindAttribLocation(progId, GL_VERT_TXUV0_ID, "aUV0");
			 
			 glAttachShader(progId, vertId);
			 glAttachShader(progId, fragId);
			 glLinkProgram (progId);
			 glUseProgram  (progId);
			 uRotId = glGetUniformLocation(progId, "uRot");
			 uTx0Id = glGetUniformLocation(progId, "uTx0");
			 if (uTx0Id >= 0) {
				 glUniform1i(uTx0Id, 0);
			 }
			
			 glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			 glEnable(GL_BLEND);
			 glDisable(GL_DITHER);

			 glCullFace(GL_BACK);
			 glEnable(GL_CULL_FACE);
		}
		
		GLuint vertsBuf = 0;
		GLuint indexBuf = 0;
		// Create the textured quad (vert positions then UVs)
		struct posTex2d verts2d[] = {
			{{-0.85f, -0.85f}, {0.0f, 0.0f}}, // BL
			{{ 0.85f, -0.85f}, {1.0f, 0.0f}}, // BR
			{{-0.85f,  0.85f}, {0.0f, 1.0f}}, // TL
			{{ 0.85f,  0.85f}, {1.0f, 1.0f}}, // TR
		};
		uint16_t index2d[] = {
			0, 1, 2,
			2, 1, 3,
		};
		glGenBuffers(1, &vertsBuf);
		glBindBuffer(GL_ARRAY_BUFFER, vertsBuf);
		glBufferData(GL_ARRAY_BUFFER,
			sizeof(verts2d), verts2d, GL_STATIC_DRAW);
		glVertexAttribPointer(GL_VERT_POSXY_ID, 2,
			GL_FLOAT, GL_FALSE, sizeof(struct posTex2d), 0);
		glVertexAttribPointer(GL_VERT_TXUV0_ID, 2,
			GL_FLOAT, GL_FALSE, sizeof(struct posTex2d),
				(void*) offsetof(struct posTex2d, uv0));
		glGenBuffers(1, &indexBuf);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuf);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,
			sizeof(index2d), index2d, GL_STATIC_DRAW);
		glEnableVertexAttribArray(GL_VERT_POSXY_ID);
		glEnableVertexAttribArray(GL_VERT_TXUV0_ID);
		
		glGenTextures(2, txName);
		if (upload(glCtx, txName[0], srcRgb,  sizeof srcRgb) &&
			upload(glCtx, txName[1], srcRgba, sizeof srcRgba))
		{
			printf("Decoded!\n");
		}
		
		emscripten_set_main_loop(tick, 0, EM_FALSE);
		emscripten_exit_with_live_runtime();
	} else {
		printf("Failed to init WebGL!\n");
	}
	return EXIT_FAILURE;
}

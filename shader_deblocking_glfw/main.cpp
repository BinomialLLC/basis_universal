// Mipmap-Compatible Texture Sampling Deblocking Shader Testbed - native C++/GLFW port.
// Copyright (C) 2026 Binomial LLC.  LICENSE: Apache 2.0
//
// Loads a Basis Universal .KTX2, transcodes it to a GPU-compressed format
// (ASTC -> BC7 -> ETC2, whichever the GL context supports; falls back to
// uncompressed RGBA8 if none), uploads all mip levels, and renders it with a
// deblocking pixel shader. Port of the Python testbed (PNG input dropped).
//
// Usage: deblock <file.ktx2>
// Controls: arrows move, W/S zoom, A/D yaw, Q/E pitch, C cube/quad, B/T/P filter,
//           R reload shader, 1-8 toggle const0.xyzw/const1.xyzw, Space reset, Esc quit.
//           (1 = deblock on/off, 2 = edge visualization.)

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#if defined(__has_include)
#  if __has_include(<glad/gl.h>)
#    include <glad/gl.h>
#    define DEBLOCK_GLAD2 1
#  else
#    include <glad/glad.h>
#  endif
#else
#  include <glad/glad.h>
#endif

#define GLFW_INCLUDE_NONE   // glad provides the GL headers; don't let GLFW pull its own
#include <GLFW/glfw3.h>

#include "basisu_transcoder.h"

// Set to 1 to enable GL debug contexts and more GL error checking
#define ENABLE_GL_DEBUG 1

// ---------------------------------------------------------------------------
// OpenGL error checking / debug output.
// Set ENABLE_GL_DEBUG to 1 to request a GL debug context (via GLFW) and install a
// debug message callback. Requires KHR_debug / GL 4.3 (available on most desktop
// drivers); harmless no-op if the callback entry point isn't present.
// ---------------------------------------------------------------------------

static const char* gl_error_string(GLenum e) {
    switch (e) {
        case GL_NO_ERROR:                      return "GL_NO_ERROR";
        case GL_INVALID_ENUM:                  return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:                 return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION:             return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY:                 return "GL_OUT_OF_MEMORY";
        case GL_STACK_UNDERFLOW:               return "GL_STACK_UNDERFLOW";
        case GL_STACK_OVERFLOW:                return "GL_STACK_OVERFLOW";
        default:                               return "GL_<unknown>";
    }
}
// Drains the GL error queue, printing a textual message for each. Returns true if any.
static bool gl_check_error(const char* where) {
    bool any = false;
    for (GLenum e; (e = glGetError()) != GL_NO_ERROR; ) {
        fprintf(stderr, "GL ERROR at %s: 0x%04X (%s)\n", where, e, gl_error_string(e));
        any = true;
    }
    return any;
}

#if ENABLE_GL_DEBUG
static void APIENTRY gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity,
                                       GLsizei length, const GLchar* message, const void* userParam) {
    (void)source; (void)id; (void)length; (void)userParam;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return; // skip chatty notifications
    fprintf(stderr, "GL DEBUG [type=0x%04X severity=0x%04X]: %s\n", type, severity, message);
}
#endif

// ---------------------------------------------------------------------------
// GL enums that glad's core loader does not expose (ASTC LDR KHR formats).
// BC7 (BPTC) and ETC2 EAC are in core glad; declare the rest defensively.
// ---------------------------------------------------------------------------
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
#endif
#ifndef GL_COMPRESSED_RGBA8_ETC2_EAC
#define GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
#endif

// ---------------------------------------------------------------------------
// 8x8 debug font (g_debug_font8x8_basic from encoder/basisu_enc.cpp, ASCII 32-127).
// Bit order: pixel (x,y) set if (glyph[y] >> x) & 1  (LSB = leftmost, y=0 = top).
// ---------------------------------------------------------------------------
static const uint8_t g_font8x8[96][8] = {
 { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
 { 0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // '!'
 { 0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // '"'
 { 0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // '#'
 { 0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // '$'
 { 0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // '%'
 { 0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // '&'
 { 0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // '''
 { 0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // '('
 { 0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // ')'
 { 0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // '*'
 { 0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, // '+'
 { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, // ','
 { 0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // '-'
 { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // '.'
 { 0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // '/'
 { 0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // '0'
 { 0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // '1'
 { 0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, // '2'
 { 0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // '3'
 { 0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, // '4'
 { 0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // '5'
 { 0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, // '6'
 { 0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // '7'
 { 0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, // '8'
 { 0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // '9'
 { 0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // ':'
 { 0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // ';'
 { 0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // '<'
 { 0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, // '='
 { 0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // '>'
 { 0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // '?'
 { 0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // '@'
 { 0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // 'A'
 { 0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // 'B'
 { 0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // 'C'
 { 0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // 'D'
 { 0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // 'E'
 { 0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // 'F'
 { 0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // 'G'
 { 0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // 'H'
 { 0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 'I'
 { 0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // 'J'
 { 0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // 'K'
 { 0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, // 'L'
 { 0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // 'M'
 { 0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // 'N'
 { 0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // 'O'
 { 0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, // 'P'
 { 0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // 'Q'
 { 0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // 'R'
 { 0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // 'S'
 { 0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 'T'
 { 0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // 'U'
 { 0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 'V'
 { 0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 'W'
 { 0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // 'X'
 { 0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // 'Y'
 { 0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // 'Z'
 { 0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // '['
 { 0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // '\'
 { 0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // ']'
 { 0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // '^'
 { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // '_'
 { 0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // '`'
 { 0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // 'a'
 { 0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // 'b'
 { 0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // 'c'
 { 0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, // 'd'
 { 0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, // 'e'
 { 0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, // 'f'
 { 0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // 'g'
 { 0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // 'h'
 { 0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // 'i'
 { 0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, // 'j'
 { 0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // 'k'
 { 0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 'l'
 { 0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // 'm'
 { 0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // 'n'
 { 0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // 'o'
 { 0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // 'p'
 { 0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // 'q'
 { 0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // 'r'
 { 0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // 's'
 { 0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // 't'
 { 0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // 'u'
 { 0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 'v'
 { 0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // 'w'
 { 0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // 'x'
 { 0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // 'y'
 { 0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // 'z'
 { 0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // '{'
 { 0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // '|'
 { 0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // '}'
 { 0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, // '~'
 { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}  // 127
};

// ---------------------------------------------------------------------------
// Config / state
// ---------------------------------------------------------------------------
static int   WINDOW_WIDTH  = 1280;
static int   WINDOW_HEIGHT = 720;
static const float FOV_DEGREES = 90.0f;
static const float Z_MIN = 0.40f, Z_MAX = -50.0f;
static const float Z_SPEED = 1.0f, XY_SPEED = 0.75f, ROT_SPEED = 90.0f;

struct State {
    float x = 0, y = 0, z = -3.0f, yaw = 0, pitch = 0;
    bool  cube = false;                       // false = quad, true = cube
    int   filter_mode = 1;                     // 0=point,1=bilinear,2=trilinear
    GLuint program = 0;
    GLuint texture = 0;
    int    tex_w = 0, tex_h = 0;               // base mip orig dims
    int    block_w = 12, block_h = 12;         // deblock filter block size
    int    mip_count = 1;
    float  const0[4] = {0,0,0,0};
    float  const1[4] = {0,0,0,0};
    // source texture info (overlay line 1)
    int    info_orig_w = 0, info_orig_h = 0, info_mips = 0;
    int    info_block_w = 0, info_block_h = 0, info_deblock_id = 0;
    std::string info_fmt;
    // debug overlay
    GLuint debug_program = 0, debug_vao = 0, debug_vbo = 0, debug_ebo = 0, debug_tex = 0;
    bool   debug_dirty = true;
    std::string shader_path;
};
static State g;
// Initial values (for Space reset).
static float INIT_CONST0[4] = {0,0,0,0};

static const char* filter_name(int m) { return m == 0 ? "POINT" : (m == 2 ? "TRILINEAR" : "BILINEAR"); }

// ---------------------------------------------------------------------------
// 4x4 matrices (row-major, matching the NumPy version; uploaded with transpose=GL_TRUE).
// ---------------------------------------------------------------------------
struct Mat4 { float m[16]; };
static Mat4 mat_identity() { Mat4 r{}; for (int i=0;i<4;i++) r.m[i*4+i]=1.0f; return r; }
static Mat4 mat_mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i=0;i<4;i++) for (int j=0;j<4;j++) { float s=0; for(int k=0;k<4;k++) s+=a.m[i*4+k]*b.m[k*4+j]; r.m[i*4+j]=s; }
    return r;
}
static Mat4 mat_perspective(float fov_deg, float aspect, float znear, float zfar) {
    Mat4 m{};
    float f = 1.0f / std::tan((fov_deg * 3.14159265358979f / 180.0f) / 2.0f);
    m.m[0*4+0] = f / aspect;
    m.m[1*4+1] = f;
    m.m[2*4+2] = (zfar + znear) / (znear - zfar);
    m.m[2*4+3] = (2.0f * zfar * znear) / (znear - zfar);
    m.m[3*4+2] = -1.0f;
    return m;
}
static Mat4 mat_translate(float x, float y, float z) {
    Mat4 m = mat_identity(); m.m[0*4+3]=x; m.m[1*4+3]=y; m.m[2*4+3]=z; return m;
}
static Mat4 mat_rot_y(float deg) {
    Mat4 m = mat_identity(); float r=deg*3.14159265358979f/180.0f, c=std::cos(r), s=std::sin(r);
    m.m[0*4+0]=c; m.m[0*4+2]=s; m.m[2*4+0]=-s; m.m[2*4+2]=c; return m;
}
static Mat4 mat_rot_x(float deg) {
    Mat4 m = mat_identity(); float r=deg*3.14159265358979f/180.0f, c=std::cos(r), s=std::sin(r);
    m.m[1*4+1]=c; m.m[1*4+2]=-s; m.m[2*4+1]=s; m.m[2*4+2]=c; return m;
}

// ---------------------------------------------------------------------------
// Shader loading (parses #vertex / #fragment markers, like the Python version).
// ---------------------------------------------------------------------------
static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
static std::string trim(const std::string& s) {
    size_t b=0,e=s.size();
    while (b<e && (unsigned char)s[b]<=' ') b++;
    while (e>b && (unsigned char)s[e-1]<=' ') e--;
    return s.substr(b,e-b);
}
static GLuint compile_shader(GLenum type, const std::string& src) {
    GLuint sh = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]; GLsizei n=0; glGetShaderInfoLog(sh, sizeof(log), &n, log);
        fprintf(stderr, "%s SHADER ERROR:\n%.*s\n", type==GL_VERTEX_SHADER?"VERTEX":"FRAGMENT", (int)n, log);
        glDeleteShader(sh); return 0;
    }
    return sh;
}
static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; GLsizei n=0; glGetProgramInfoLog(p,sizeof(log),&n,log); fprintf(stderr,"LINK ERROR:\n%.*s\n",(int)n,log); glDeleteProgram(p); return 0; }
    return p;
}
static GLuint load_shader(const std::string& path) {
    std::string text = read_file(path);
    if (text.empty()) { fprintf(stderr, "ERROR: could not read shader '%s'\n", path.c_str()); return 0; }
    size_t v = text.find("#vertex");
    if (v == std::string::npos) { fprintf(stderr, "ERROR: no #vertex marker\n"); return 0; }
    size_t fpos = text.find("#fragment", v);
    if (fpos == std::string::npos) { fprintf(stderr, "ERROR: no #fragment marker\n"); return 0; }
    std::string vsrc = trim(text.substr(v + 7, fpos - (v + 7)));
    std::string fsrc = trim(text.substr(fpos + 9));
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vsrc); if (!vs) return 0;
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fsrc); if (!fs) { glDeleteShader(vs); return 0; }
    GLuint prog = link_program(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);
    if (prog) printf("Shader compiled successfully.\n");
    return prog;
}

// ---------------------------------------------------------------------------
// GPU compressed format selection ladder (ASTC -> BC7 -> ETC2 -> RGBA8).
// ---------------------------------------------------------------------------
// Map an ASTC block size to its GL_COMPRESSED_RGBA_ASTC_<w>x<h>_KHR enum (0x93B0..0x93BD).
// IMPORTANT: this is keyed on the actual (w,h), NOT on any Basis/transcoder enum ordinal.
// The GL ASTC enums are ordered W-major (4x4,5x4,5x5,...,8x8,10x5,...), whereas Basis
// Universal's transcoder_texture_format ASTC values are ordered by block area (W*H), so
// the two orderings diverge (e.g. GL puts 8x8 before 10x5, Basis puts it after 10x6).
// Deriving one enum from the other by index would therefore be wrong -- always map by (w,h).
static unsigned astc_gl_format(int bw, int bh) {
    switch ((bw<<8)|bh) {
        case (4<<8)|4:  return 0x93B0; case (5<<8)|4:  return 0x93B1;
        case (5<<8)|5:  return 0x93B2; case (6<<8)|5:  return 0x93B3;
        case (6<<8)|6:  return 0x93B4; case (8<<8)|5:  return 0x93B5;
        case (8<<8)|6:  return 0x93B6; case (8<<8)|8:  return 0x93B7;
        case (10<<8)|5: return 0x93B8; case (10<<8)|6: return 0x93B9;
        case (10<<8)|8: return 0x93BA; case (10<<8)|10:return 0x93BB;
        case (12<<8)|10:return 0x93BC; case (12<<8)|12:return 0x93BD;
    }
    return 0;
}

// Human-readable name for the GL internal format we upload with.
static const char* gl_internal_format_name(unsigned fmt) {
    switch (fmt) {
        case GL_RGBA8:                       return "GL_RGBA8";
        case GL_COMPRESSED_RGBA_BPTC_UNORM:  return "GL_COMPRESSED_RGBA_BPTC_UNORM";
        case GL_COMPRESSED_RGBA8_ETC2_EAC:   return "GL_COMPRESSED_RGBA8_ETC2_EAC";
        case 0x93B0: return "GL_COMPRESSED_RGBA_ASTC_4x4_KHR";
        case 0x93B1: return "GL_COMPRESSED_RGBA_ASTC_5x4_KHR";
        case 0x93B2: return "GL_COMPRESSED_RGBA_ASTC_5x5_KHR";
        case 0x93B3: return "GL_COMPRESSED_RGBA_ASTC_6x5_KHR";
        case 0x93B4: return "GL_COMPRESSED_RGBA_ASTC_6x6_KHR";
        case 0x93B5: return "GL_COMPRESSED_RGBA_ASTC_8x5_KHR";
        case 0x93B6: return "GL_COMPRESSED_RGBA_ASTC_8x6_KHR";
        case 0x93B7: return "GL_COMPRESSED_RGBA_ASTC_8x8_KHR";
        case 0x93B8: return "GL_COMPRESSED_RGBA_ASTC_10x5_KHR";
        case 0x93B9: return "GL_COMPRESSED_RGBA_ASTC_10x6_KHR";
        case 0x93BA: return "GL_COMPRESSED_RGBA_ASTC_10x8_KHR";
        case 0x93BB: return "GL_COMPRESSED_RGBA_ASTC_10x10_KHR";
        case 0x93BC: return "GL_COMPRESSED_RGBA_ASTC_12x10_KHR";
        case 0x93BD: return "GL_COMPRESSED_RGBA_ASTC_12x12_KHR";
        default:     return "GL_<unknown>";
    }
}
struct Caps { bool astc=false, bc7=false, etc2=false; };
static Caps detect_caps() {
    std::vector<std::string> exts;
    GLint n=0; glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i=0;i<n;i++) { const char* e=(const char*)glGetStringi(GL_EXTENSIONS,(GLuint)i); if (e) exts.push_back(e); }
    auto has = [&](const char* name){ for (auto& e:exts) if (e==name) return true; return false; };
    Caps c;
    c.astc = has("GL_KHR_texture_compression_astc_ldr") || has("GL_OES_texture_compression_astc");
    c.bc7  = has("GL_ARB_texture_compression_bptc") || has("GL_EXT_texture_compression_bptc");
    c.etc2 = has("GL_ARB_ES3_compatibility") || has("GL_OES_compressed_ETC2_RGBA8_texture");
    return c;
}

static std::string ktx2_format_name(basist::ktx2_transcoder& tc) {
    int bw = tc.get_block_width(), bh = tc.get_block_height();
    char buf[64];
    if (tc.is_etc1s()) return "ETC1S";
    if (tc.is_uastc()) return "UASTC LDR 4x4";
    if (tc.is_xuastc_ldr()) { snprintf(buf,sizeof(buf),"XUASTC LDR %dx%d",bw,bh); return buf; }
    if (tc.is_astc_ldr())   { snprintf(buf,sizeof(buf),"ASTC LDR %dx%d",bw,bh); return buf; }
    if (tc.is_xubc7()) return "XUBC7";
    if (tc.is_hdr_4x4()) return "UASTC HDR 4x4";
    if (tc.is_hdr_6x6()) { snprintf(buf,sizeof(buf),"HDR 6x6 %dx%d",bw,bh); return buf; }
    if (tc.is_hdr()) return "HDR";
    snprintf(buf,sizeof(buf),"basis_fmt %d",(int)tc.get_basis_tex_format());
    return buf;
}

// Load + transcode a KTX2 into a GL texture. Returns true on success.
// Optional user format preference (from --astc / --bc7 / --etc2 / --rgba32).
enum PreferFormat { PREF_NONE, PREF_ASTC, PREF_BC7, PREF_ETC2, PREF_RGBA32 };

static bool load_ktx2_texture(const std::string& path, const Caps& caps, PreferFormat pref, bool no_mips) {
    printf("Loading KTX2: %s\n", path.c_str());
    std::vector<uint8_t> data;
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) { fprintf(stderr, "ERROR: could not open '%s'\n", path.c_str()); return false; }
        f.seekg(0, std::ios::end); std::streamoff sz = f.tellg(); f.seekg(0);
        data.resize((size_t)sz); f.read((char*)data.data(), sz);
    }

    basist::ktx2_transcoder tc;
    if (!tc.init(data.data(), (uint32_t)data.size())) { fprintf(stderr, "ERROR: ktx2_transcoder.init failed\n"); return false; }

    // This sample handles LDR textures only.
    if (tc.is_hdr()) {
        fprintf(stderr, "ERROR: '%s' is an HDR texture; this sample supports LDR textures only.\n", path.c_str());
        return false;
    }

    const basist::basis_tex_format basis_fmt = tc.get_basis_tex_format();
    const int file_bw = (int)tc.get_block_width();
    const int file_bh = (int)tc.get_block_height();
    const int levels  = (int)tc.get_levels();
    // --nomips: upload only the base level (level 0) instead of the full mip chain.
    const int load_levels = no_mips ? 1 : levels;

    using TF = basist::transcoder_texture_format;
    auto supported = [&](TF f){ return basist::basis_is_format_supported(f, basis_fmt); };

    // Each candidate GPU format: usable only if the GL context advertises it AND the
    // transcoder can produce it for this file. A null/false Choice means "not usable".
    struct Choice { TF tfmt; unsigned gl_fmt; int gpu_bw, gpu_bh; bool compressed; const char* family; bool ok; };
    auto try_astc = [&]() -> Choice {
        if (caps.astc) {
            TF t = basist::basis_get_transcoder_texture_format_from_basis_tex_format(basis_fmt);
            unsigned glf = astc_gl_format(file_bw, file_bh);
            if (glf && supported(t)) return { t, glf, file_bw, file_bh, true, "ASTC", true };
        }
        return {}; };
    auto try_bc7 = [&]() -> Choice {
        if (caps.bc7 && supported(TF::cTFBC7_RGBA)) return { TF::cTFBC7_RGBA, GL_COMPRESSED_RGBA_BPTC_UNORM, 4, 4, true, "BC7", true };
        return {}; };
    auto try_etc2 = [&]() -> Choice {
        if (caps.etc2 && supported(TF::cTFETC2_RGBA)) return { TF::cTFETC2_RGBA, GL_COMPRESSED_RGBA8_ETC2_EAC, 4, 4, true, "ETC2", true };
        return {}; };
    auto try_rgba32 = [&]() -> Choice {
        if (supported(TF::cTFRGBA32)) return { TF::cTFRGBA32, GL_RGBA8, 1, 1, false, "RGBA32", true };
        return {}; };

    // Honor an explicit format preference first, if that format is usable.
    Choice chosen{};
    switch (pref) {
        case PREF_ASTC:   chosen = try_astc();   break;
        case PREF_BC7:    chosen = try_bc7();    break;
        case PREF_ETC2:   chosen = try_etc2();   break;
        case PREF_RGBA32: chosen = try_rgba32(); break;
        default: break;
    }
    if (pref != PREF_NONE && !chosen.ok)
        printf("  Note: preferred format not available for this file/GPU; using the default ladder.\n");

    // Default ladder: ASTC -> BC7 -> ETC2 -> uncompressed RGBA8.
    if (!chosen.ok) chosen = try_astc();
    if (!chosen.ok) chosen = try_bc7();
    if (!chosen.ok) chosen = try_etc2();
    if (!chosen.ok) chosen = try_rgba32();
    if (!chosen.ok) { fprintf(stderr, "ERROR: no usable transcode target (HDR file?)\n"); return false; }

    const TF tfmt = chosen.tfmt; const unsigned gl_fmt = chosen.gl_fmt;
    const int gpu_bw = chosen.gpu_bw, gpu_bh = chosen.gpu_bh;
    const bool compressed = chosen.compressed; const char* family = chosen.family;

    const uint32_t deblock_id = tc.get_deblocking_filter_index();
    std::string fmt_name = ktx2_format_name(tc);
    printf("  Source     : %dx%d  levels=%d  fmt=%s\n", tc.get_width(), tc.get_height(), levels, fmt_name.c_str());
    if (compressed) printf("  GPU format : %s  %s (0x%04X)  block=%dx%d\n", family, gl_internal_format_name(gl_fmt), gl_fmt, gpu_bw, gpu_bh);
    else            printf("  GPU format : %s  %s (0x%04X)  uncompressed\n", family, gl_internal_format_name(gl_fmt), gl_fmt);
    printf("  Deblock    : %s  filter block=%dx%d\n", deblock_id==1?"ON":"off", file_bw, file_bh);

    if (!tc.start_transcoding()) { fprintf(stderr, "ERROR: start_transcoding failed\n"); return false; }

    GLuint tex=0; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D, tex);

    const uint32_t bpb = basist::basis_get_bytes_per_block_or_pixel(tfmt);
    std::vector<uint8_t> buf;
    for (int lvl=0; lvl<load_levels; lvl++) {
        basist::ktx2_image_level_info info;
        if (!tc.get_image_level_info(info, lvl, 0, 0)) { fprintf(stderr,"ERROR: get_image_level_info failed (lvl %d)\n",lvl); return false; }
        const uint32_t ow = info.m_orig_width, oh = info.m_orig_height;
        const uint32_t out_size = basist::basis_compute_transcoded_image_size_in_bytes(tfmt, ow, oh);
        const uint32_t out_units = out_size / bpb; // blocks (compressed) or pixels (RGBA32)
        buf.resize(out_size);
        // Disable CPU deblocking: the GPU shader performs it (no double-filtering).
        if (!tc.transcode_image_level(lvl, 0, 0, buf.data(), out_units, tfmt,
                                      basist::cDecodeFlagsNoDeblockFiltering)) {
            fprintf(stderr, "ERROR: transcode_image_level failed (lvl %d)\n", lvl); return false;
        }
        if (compressed)
            glCompressedTexImage2D(GL_TEXTURE_2D, lvl, gl_fmt, ow, oh, 0, (GLsizei)out_size, buf.data());
        else
            glTexImage2D(GL_TEXTURE_2D, lvl, GL_RGBA8, ow, oh, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            fprintf(stderr, "ERROR: texture upload failed (lvl %d, %ux%u, %u bytes, fmt 0x%04X): GL 0x%04X\n",
                    lvl, ow, oh, out_size, gl_fmt, err);
            return false;
        }
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, load_levels-1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Publish results to global state.
    g.texture = tex;
    g.tex_w = tc.get_width(); g.tex_h = tc.get_height();
    g.block_w = file_bw; g.block_h = file_bh;
    g.mip_count = load_levels;
    g.const0[0] = (deblock_id == 1) ? 1.0f : 0.0f;
    INIT_CONST0[0] = g.const0[0];
    g.info_orig_w = tc.get_width(); g.info_orig_h = tc.get_height(); g.info_mips = levels;
    g.info_block_w = file_bw; g.info_block_h = file_bh; g.info_deblock_id = (int)deblock_id;
    g.info_fmt = fmt_name;
    printf("  Uploaded %d mip level(s)%s.\n", load_levels,
           no_mips ? "  (--nomips: base level only)" : "");
    return true;
}

// ---------------------------------------------------------------------------
// Texture filtering
// ---------------------------------------------------------------------------
static void apply_filter_mode() {
    glBindTexture(GL_TEXTURE_2D, g.texture);
    // For a single-level texture, use a non-mipmap min filter so it is unambiguously
    // complete on strict drivers (Mesa/Apple).
    const bool mips = g.mip_count > 1;
    if (g.filter_mode == 1) { // bilinear
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else if (g.filter_mode == 2) { // trilinear
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else { // point
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    g.debug_dirty = true;
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
static GLuint make_mesh(const float* verts, size_t vbytes, const uint32_t* idx, size_t ibytes, int stride) {
    GLuint vao,vbo,ebo; glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo); glGenBuffers(1,&ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, vbytes, verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, ibytes, idx, GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,(void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,stride,(void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return vao;
}
static GLuint g_quad_vao=0, g_cube_vao=0; static int g_cube_index_count=0;
static void create_quad(float aspect) {
    float hw, hh;
    if (aspect >= 1.0f) { hw=1.0f; hh=1.0f/aspect; } else { hw=aspect; hh=1.0f; }
    float v[] = {
        -hw,-hh,0.0f, 0.0f,1.0f,  hw,-hh,0.0f, 1.0f,1.0f,
         hw, hh,0.0f, 1.0f,0.0f, -hw, hh,0.0f, 0.0f,0.0f,
    };
    uint32_t idx[] = {0,1,2, 0,2,3};
    g_quad_vao = make_mesh(v,sizeof(v),idx,sizeof(idx),5*sizeof(float));
}
static void create_cube() {
    const float h=0.5f;
    float v[] = {
        // front (z=+h)
        -h,-h, h, 0,1,  h,-h, h, 1,1,  h, h, h, 1,0, -h, h, h, 0,0,
        // back (z=-h)
         h,-h,-h, 0,1, -h,-h,-h, 1,1, -h, h,-h, 1,0,  h, h,-h, 0,0,
        // right (x=+h)
         h,-h, h, 0,1,  h,-h,-h, 1,1,  h, h,-h, 1,0,  h, h, h, 0,0,
        // left (x=-h)
        -h,-h,-h, 0,1, -h,-h, h, 1,1, -h, h, h, 1,0, -h, h,-h, 0,0,
        // top (y=+h)
        -h, h, h, 0,1,  h, h, h, 1,1,  h, h,-h, 1,0, -h, h,-h, 0,0,
        // bottom (y=-h)
        -h,-h,-h, 0,1,  h,-h,-h, 1,1,  h,-h, h, 1,0, -h,-h, h, 0,0,
    };
    std::vector<uint32_t> idx;
    for (int i=0;i<6;i++){ int b=i*4; idx.insert(idx.end(),{(uint32_t)b,(uint32_t)b+1,(uint32_t)b+2,(uint32_t)b,(uint32_t)b+2,(uint32_t)b+3}); }
    g_cube_vao = make_mesh(v,sizeof(v),idx.data(),idx.size()*sizeof(uint32_t),5*sizeof(float));
    g_cube_index_count = (int)idx.size();
}

// ---------------------------------------------------------------------------
// Debug text overlay (8x8 font rasterized to an RGBA texture, drawn as a quad).
// ---------------------------------------------------------------------------
static const int OVL_W = 1280, OVL_H = 84, FONT_SCALE = 2, LINE_ADV = 20;
static const char* DEBUG_VS = "#version 330 core\nlayout(location=0) in vec2 aPos;layout(location=1) in vec2 aUV;out vec2 vUV;void main(){vUV=aUV;gl_Position=vec4(aPos,0.0,1.0);}";
static const char* DEBUG_FS = "#version 330 core\nuniform sampler2D tex;in vec2 vUV;out vec4 fragColor;void main(){fragColor=texture(tex,vUV);}";

static void blit_char(std::vector<uint8_t>& buf, int px, int py, char ch) {
    int c = (unsigned char)ch; if (c<32 || c>127) c='.';
    const uint8_t* glyph = g_font8x8[c-32];
    for (int y=0;y<8;y++) for (int x=0;x<8;x++) {
        if (!((glyph[y]>>x)&1)) continue;
        for (int sy=0;sy<FONT_SCALE;sy++) for (int sx=0;sx<FONT_SCALE;sx++) {
            int X=px+x*FONT_SCALE+sx, Y=py+y*FONT_SCALE+sy;
            if (X<0||X>=OVL_W||Y<0||Y>=OVL_H) continue;
            uint8_t* d=&buf[(Y*OVL_W+X)*4]; d[0]=255;d[1]=255;d[2]=255;d[3]=255;
        }
    }
}
static void update_debug_text() {
    if (!g.debug_dirty) return;
    std::vector<uint8_t> buf(OVL_W*OVL_H*4);
    for (size_t i=0;i<buf.size();i+=4){ buf[i]=0;buf[i+1]=0;buf[i+2]=0;buf[i+3]=180; }

    char l0[256], l1[256], l2[256];
    snprintf(l0,sizeof(l0),"Res:%dx%d  Mips:%d  Block:%dx%d  DeblockID:%d  Fmt:%s",
             g.info_orig_w,g.info_orig_h,g.info_mips,g.info_block_w,g.info_block_h,g.info_deblock_id,g.info_fmt.c_str());
    snprintf(l1,sizeof(l1),"Mode:%-4s Filter:%-9s Deblock:[%d%d%d%d][%d%d%d%d]",
             g.cube?"CUBE":"QUAD", filter_name(g.filter_mode),
             (int)g.const0[0],(int)g.const0[1],(int)g.const0[2],(int)g.const0[3],
             (int)g.const1[0],(int)g.const1[1],(int)g.const1[2],(int)g.const1[3]);
    snprintf(l2,sizeof(l2),"X:%+5.1f Y:%+5.1f Z:%5.1f Yaw:%+6.1f Pitch:%+6.1f", g.x,g.y,g.z,g.yaw,g.pitch);
    // Kept short so it fits the overlay width (OVL_W / (8*FONT_SCALE) chars). Full
    // controls are in --help and the README.
    const char* l3 = "Move:Arrows/WS Rot:ADQE C:cube B/T/P:filter 1:deblk 2:edge R:reload Spc:reset";
    const char* lines[4] = {l0,l1,l2,l3};

    for (int li=0; li<4; li++) {
        int py = 2 + li*LINE_ADV;
        int px = 4;
        for (const char* p=lines[li]; *p; ++p) { blit_char(buf, px, py, *p); px += 8*FONT_SCALE; }
    }

    glBindTexture(GL_TEXTURE_2D, g.debug_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,OVL_W,OVL_H,0,GL_RGBA,GL_UNSIGNED_BYTE,buf.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    g.debug_dirty = false;
}
static void init_debug() {
    GLuint vs=compile_shader(GL_VERTEX_SHADER,DEBUG_VS), fs=compile_shader(GL_FRAGMENT_SHADER,DEBUG_FS);
    g.debug_program = link_program(vs,fs); glDeleteShader(vs); glDeleteShader(fs);
    glGenVertexArrays(1,&g.debug_vao); glGenBuffers(1,&g.debug_vbo); glGenBuffers(1,&g.debug_ebo);
    glBindVertexArray(g.debug_vao);
    glBindBuffer(GL_ARRAY_BUFFER,g.debug_vbo); glBufferData(GL_ARRAY_BUFFER,16*sizeof(float),nullptr,GL_DYNAMIC_DRAW);
    uint32_t idx[]={0,1,2,0,2,3};
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,g.debug_ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float))); glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glGenTextures(1,&g.debug_tex);
}
static void draw_debug_text() {
    if (!g.debug_program) return;
    update_debug_text();
    // Top-left rect of OVL_W x OVL_H pixels in NDC (data row 0 = top => top uv.v=0).
    float w = (float)OVL_W / WINDOW_WIDTH * 2.0f, h = (float)OVL_H / WINDOW_HEIGHT * 2.0f;
    float verts[] = {
        -1.0f,      1.0f,     0.0f, 0.0f,
        -1.0f + w,  1.0f,     1.0f, 0.0f,
        -1.0f + w,  1.0f - h, 1.0f, 1.0f,
        -1.0f,      1.0f - h, 0.0f, 1.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, g.debug_vbo); glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_DEPTH_TEST);
    glUseProgram(g.debug_program);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g.debug_tex);
    glBindVertexArray(g.debug_vao); glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,nullptr);
    glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    WINDOW_WIDTH=w; WINDOW_HEIGHT=h; glViewport(0,0,w,h); g.debug_dirty=true;
}
static void reload_shader() {
    GLuint np = load_shader(g.shader_path);
    if (np) { if (g.program) glDeleteProgram(g.program); g.program=np; }
    else fprintf(stderr, "Shader reload failed, keeping previous shader.\n");
}
static void key_callback(GLFWwindow* w, int key, int sc, int action, int mods) {
    (void)sc;(void)mods;
    if (action != GLFW_PRESS) return;
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, GLFW_TRUE); break;
        case GLFW_KEY_R: reload_shader(); break;
        case GLFW_KEY_B: g.filter_mode=1; apply_filter_mode(); printf("Filter: BILINEAR\n"); break;
        case GLFW_KEY_T: g.filter_mode=2; apply_filter_mode(); printf("Filter: TRILINEAR\n"); break;
        case GLFW_KEY_P: g.filter_mode=0; apply_filter_mode(); printf("Filter: POINT\n"); break;
        case GLFW_KEY_C: g.cube=!g.cube; g.debug_dirty=true; break;
        case GLFW_KEY_1: g.const0[0]=1.0f-g.const0[0]; g.debug_dirty=true; break;
        case GLFW_KEY_2: g.const0[1]=1.0f-g.const0[1]; g.debug_dirty=true; break;
        case GLFW_KEY_3: g.const0[2]=1.0f-g.const0[2]; g.debug_dirty=true; break;
        case GLFW_KEY_4: g.const0[3]=1.0f-g.const0[3]; g.debug_dirty=true; break;
        case GLFW_KEY_5: g.const1[0]=1.0f-g.const1[0]; g.debug_dirty=true; break;
        case GLFW_KEY_6: g.const1[1]=1.0f-g.const1[1]; g.debug_dirty=true; break;
        case GLFW_KEY_7: g.const1[2]=1.0f-g.const1[2]; g.debug_dirty=true; break;
        case GLFW_KEY_8: g.const1[3]=1.0f-g.const1[3]; g.debug_dirty=true; break;
        case GLFW_KEY_SPACE:
            g.x=0;g.y=0;g.z=-3.0f;g.yaw=0;g.pitch=0;
            for(int i=0;i<4;i++){g.const0[i]=INIT_CONST0[i];g.const1[i]=0;}
            g.debug_dirty=true; printf("Reset to initial state\n"); break;
    }
}
static void process_held_keys(GLFWwindow* w, float dt) {
    if (glfwGetKey(w,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS || glfwGetKey(w,GLFW_KEY_RIGHT_SHIFT)==GLFW_PRESS) dt *= 1.0f/3.0f;
    bool moved=false;
    if (glfwGetKey(w,GLFW_KEY_W)==GLFW_PRESS){g.z+=Z_SPEED*dt;moved=true;}
    if (glfwGetKey(w,GLFW_KEY_S)==GLFW_PRESS){g.z-=Z_SPEED*dt;moved=true;}
    if (glfwGetKey(w,GLFW_KEY_LEFT)==GLFW_PRESS){g.x+=XY_SPEED*dt;moved=true;}
    if (glfwGetKey(w,GLFW_KEY_RIGHT)==GLFW_PRESS){g.x-=XY_SPEED*dt;moved=true;}
    if (glfwGetKey(w,GLFW_KEY_UP)==GLFW_PRESS){g.y+=XY_SPEED*dt;moved=true;}
    if (glfwGetKey(w,GLFW_KEY_DOWN)==GLFW_PRESS){g.y-=XY_SPEED*dt;moved=true;}
    if (glfwGetKey(w,GLFW_KEY_A)==GLFW_PRESS){g.yaw+=ROT_SPEED*dt;moved=true;}
    if (glfwGetKey(w,GLFW_KEY_D)==GLFW_PRESS){g.yaw-=ROT_SPEED*dt;moved=true;}
    if (glfwGetKey(w,GLFW_KEY_Q)==GLFW_PRESS){g.pitch+=ROT_SPEED*dt;moved=true;}
    if (glfwGetKey(w,GLFW_KEY_E)==GLFW_PRESS){g.pitch-=ROT_SPEED*dt;moved=true;}
    if (g.z < Z_MAX) g.z=Z_MAX; if (g.z > Z_MIN) g.z=Z_MIN;
    if (moved) g.debug_dirty=true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
static void set_uniforms() {
    Mat4 proj = mat_perspective(FOV_DEGREES, (float)WINDOW_WIDTH/WINDOW_HEIGHT, 0.001f, 100.0f);
    Mat4 model = mat_mul(mat_mul(mat_translate(g.x,g.y,g.z), mat_rot_y(g.yaw)), mat_rot_x(g.pitch));
    Mat4 mvp = mat_mul(proj, model);
    GLint loc;
    loc=glGetUniformLocation(g.program,"mvp");     if(loc>=0) glUniformMatrix4fv(loc,1,GL_TRUE,mvp.m);
    loc=glGetUniformLocation(g.program,"tex");     if(loc>=0) glUniform1i(loc,0);
    loc=glGetUniformLocation(g.program,"texSize"); if(loc>=0) glUniform4f(loc,(float)g.tex_w,(float)g.tex_h,(float)g.block_w,(float)g.block_h);
    loc=glGetUniformLocation(g.program,"maxLod");  if(loc>=0) glUniform1f(loc,(float)(g.mip_count-1));
    loc=glGetUniformLocation(g.program,"const0");  if(loc>=0) glUniform4f(loc,g.const0[0],g.const0[1],g.const0[2],g.const0[3]);
    loc=glGetUniformLocation(g.program,"const1");  if(loc>=0) glUniform4f(loc,g.const1[0],g.const1[1],g.const1[2],g.const1[3]);
}

int main(int argc, char** argv) {
    // First non-flag argument is the .ktx2 path. Optional --astc/--bc7/--etc2/--rgba32
    // flags set a PREFERRED GPU format (used if available, else the default ladder).
    std::string ktx2_path;
    PreferFormat pref = PREF_NONE;
    bool no_mips = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if      (a == "--astc")               pref = PREF_ASTC;
        else if (a == "--bc7")                pref = PREF_BC7;
        else if (a == "--etc2")               pref = PREF_ETC2;
        else if (a == "--rgba32" || a == "--rgba8") pref = PREF_RGBA32;
        else if (a == "--nomips")             no_mips = true;
        else if (!a.empty() && a[0] == '-')   fprintf(stderr, "WARNING: ignoring unknown option '%s'\n", a.c_str());
        else if (ktx2_path.empty())           ktx2_path = a;
    }
    if (ktx2_path.empty()) {
        printf("Usage: %s <file.ktx2> [--astc|--bc7|--etc2|--rgba32] [--nomips]\n", argv[0]);
        printf("  A format flag prefers that GPU format if the GPU/file supports it,\n");
        printf("  otherwise it falls back to the default ASTC->BC7->ETC2->RGBA8 ladder.\n");
        printf("  --nomips loads only the base mip level (level 0) instead of all levels.\n");
        printf("  The shader is loaded from shader.glsl in the working directory.\n");
        printf("  Note: LDR textures only; HDR .ktx2 files are not supported.\n");
        return 1;
    }

    basist::basisu_transcoder_init();

    // Report GLFW errors with the code + description (the biggest aid when window/
    // context creation fails on a new platform). Safe to set before glfwInit.
    glfwSetErrorCallback([](int code, const char* desc){ fprintf(stderr, "GLFW ERROR 0x%08X: %s\n", code, desc); });

    if (!glfwInit()) { fprintf(stderr,"ERROR: glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GLFW_TRUE); // required for a core context on macOS
#if ENABLE_GL_DEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH,WINDOW_HEIGHT,"Deblock Shader Testbed (C++/GLFW)",nullptr,nullptr);
#if ENABLE_GL_DEBUG
    if (!window) {
        // A debug context can fail to create on some older Mesa/GLX drivers; retry without it.
        fprintf(stderr,"WARNING: debug context unavailable; retrying without GLFW_OPENGL_DEBUG_CONTEXT\n");
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_FALSE);
        window = glfwCreateWindow(WINDOW_WIDTH,WINDOW_HEIGHT,"Deblock Shader Testbed (C++/GLFW)",nullptr,nullptr);
    }
#endif
    if (!window) { fprintf(stderr,"ERROR: window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window,key_callback);
    glfwSetFramebufferSizeCallback(window,framebuffer_size_callback);
    glfwSwapInterval(1);

#ifdef DEBLOCK_GLAD2
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) { fprintf(stderr,"ERROR: gladLoadGL failed\n"); glfwTerminate(); return 1; }
#else
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { fprintf(stderr,"ERROR: gladLoadGLLoader failed\n"); glfwTerminate(); return 1; }
#endif
    printf("OpenGL: %s\n", (const char*)glGetString(GL_VERSION));

#if ENABLE_GL_DEBUG
    {
        // glad's core loader only resolves entry points up to the 3.3 context version,
        // so the 4.3-core glDebugMessageCallback is left null. Fetch it (or its
        // KHR_debug alias) directly -- desktop drivers expose KHR_debug even on a 3.3
        // context. Keeps the sample at GL 3.3 (no version bump needed for debug output).
        auto cb  = (PFNGLDEBUGMESSAGECALLBACKPROC)glfwGetProcAddress("glDebugMessageCallback");
        if (!cb)  cb  = (PFNGLDEBUGMESSAGECALLBACKPROC)glfwGetProcAddress("glDebugMessageCallbackKHR");
        auto ctl = (PFNGLDEBUGMESSAGECONTROLPROC)glfwGetProcAddress("glDebugMessageControl");
        if (!ctl) ctl = (PFNGLDEBUGMESSAGECONTROLPROC)glfwGetProcAddress("glDebugMessageControlKHR");
        if (cb) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS); // fire callbacks on the offending call
            cb(gl_debug_callback, nullptr);
            if (ctl) ctl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            printf("OpenGL debug output active (KHR_debug).\n");
        } else {
            printf("OpenGL debug output unavailable (driver lacks KHR_debug).\n");
        }
    }
#endif
    gl_check_error("after GL init");

    // Use the real framebuffer size, which differs from the window size on Retina /
    // HiDPI / fractional-scaled displays. The resize callback keeps these in sync;
    // this sets the initial viewport (otherwise the first frame uses a wrong viewport).
    glfwGetFramebufferSize(window, &WINDOW_WIDTH, &WINDOW_HEIGHT);
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

    Caps caps = detect_caps();
    printf("GPU compressed format support:  ASTC=%d  BC7=%d  ETC2=%d\n", caps.astc, caps.bc7, caps.etc2);
    if (!caps.astc && !caps.bc7 && !caps.etc2)
        printf("Note: no GPU-compressed format available; KTX2 will load as uncompressed RGBA8.\n");

    // Try shader.glsl in the cwd, else next to the executable.
    g.shader_path = "shader.glsl";
    if (read_file(g.shader_path).empty()) {
        std::string a = argv[0]; size_t s = a.find_last_of("/\\");
        if (s != std::string::npos) g.shader_path = a.substr(0,s+1) + "shader.glsl";
    }
    g.program = load_shader(g.shader_path);
    if (!g.program) { glfwTerminate(); return 1; }

    if (!load_ktx2_texture(ktx2_path, caps, pref, no_mips)) { glfwTerminate(); return 1; }
    gl_check_error("after texture load");

    g.filter_mode = 2; // trilinear default
    apply_filter_mode();

    create_quad((float)g.tex_w / (float)g.tex_h);
    create_cube();
    init_debug();
    gl_check_error("after resource setup");

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.2f,0.2f,0.2f,1.0f);

    double last = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime(); float dt = (float)(now-last); last=now;
        if (dt > 0.1f) dt = 0.1f; // cap after a stall (occluded window / compositor) to avoid a teleport
        glfwPollEvents();
        process_held_keys(window, dt);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(g.program);
        set_uniforms();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g.texture);
        if (g.cube) { glBindVertexArray(g_cube_vao); glDrawElements(GL_TRIANGLES,g_cube_index_count,GL_UNSIGNED_INT,nullptr); }
        else        { glBindVertexArray(g_quad_vao); glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,nullptr); }

        draw_debug_text();
#if ENABLE_GL_DEBUG
        gl_check_error("frame");
#endif
        glfwSwapBuffers(window);
    }
    glfwTerminate();
    printf("Done.\n");
    return 0;
}

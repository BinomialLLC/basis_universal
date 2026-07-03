// Mipmap-Compatible Texture Sampling Deblocking Shader Testbed - native C++/Direct3D 11 port.
// Copyright (C) 2026 Binomial LLC.  LICENSE: Apache 2.0
//
// Loads a Basis Universal .KTX2, transcodes it to BC7 (or uncompressed RGBA8 when BC7
// isn't usable -- see the GPU-format-selection comment in load_ktx2_texture() for
// the 4-alignment rules), uploads all mip
// levels, and renders it with a deblocking pixel shader (bin/deblock.hlsl).
// Port of the shader_deblocking_glfw OpenGL sample: same assets, same behavior, same
// controls, same shader logic -- only the graphics API layer changes.
//
// Usage: deblock_d3d11 <file.ktx2> [--bc7|--rgba32] [--nomips]
// Controls: arrows move, W/S zoom, A/D yaw, Q/E pitch, C cube/quad, B/T/P filter,
//           R reload shader, 1-8 toggle const0.xyzw/const1.xyzw, Space reset, Esc quit.
//           (1 = deblock on/off, 2 = edge visualization.)
//
// Dependencies: the C runtime, Win32, D3D11, D3DCompiler. The Basis Universal
// transcoder is compiled in directly (see CMakeLists.txt); nothing links against
// any other part of the repo.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#include "basisu_transcoder.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Set to 1 to request the D3D11 debug layer (needs the Graphics Tools optional feature
// installed; falls back to a non-debug device when unavailable).
#ifdef _DEBUG
#define ENABLE_D3D_DEBUG 1
#else
#define ENABLE_D3D_DEBUG 0
#endif

template <typename T> static void safe_release(T*& p) { if (p) { p->Release(); p = nullptr; } }

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

// D3D device / pipeline objects (created once in main).
static ID3D11Device*           g_dev = nullptr;
static ID3D11DeviceContext*    g_ctx = nullptr;
static IDXGISwapChain*         g_swapchain = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static ID3D11Texture2D*        g_depth_tex = nullptr;
static ID3D11DepthStencilView* g_dsv = nullptr;
static ID3D11SamplerState*     g_samplers[3] = {};   // 0=point,1=bilinear,2=trilinear
static ID3D11RasterizerState*  g_raster = nullptr;   // CULL_NONE (the GL sample never enables culling)
static ID3D11DepthStencilState* g_depth_on = nullptr, * g_depth_off = nullptr;
static ID3D11BlendState*       g_blend_alpha = nullptr;
static ID3D11Buffer*           g_cbuffer = nullptr;
static bool                    g_quit = false;

struct State {
    float x = 0, y = 0, z = -3.0f, yaw = 0, pitch = 0;
    bool  cube = false;                        // false = quad, true = cube
    int   filter_mode = 1;                     // 0=point,1=bilinear,2=trilinear
    ID3D11VertexShader*  vs = nullptr;
    ID3D11PixelShader*   ps = nullptr;
    ID3D11InputLayout*   layout = nullptr;
    ID3D11Texture2D*          texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    int    tex_w = 0, tex_h = 0;               // dims of the texture AS CREATED (padded on the BC7 round-up path)
    int    block_w = 12, block_h = 12;         // deblock filter block size (from the KTX2 header -- the SOURCE lattice)
    int    mip_count = 1;
    float  const0[4] = {0,0,0,0};
    float  const1[4] = {0,0,0,0};
    // source texture info (overlay line 1)
    int    info_orig_w = 0, info_orig_h = 0, info_mips = 0;
    int    info_block_w = 0, info_block_h = 0, info_deblock_id = 0;
    std::string info_fmt, info_gpu_fmt;
    // debug overlay
    ID3D11VertexShader*       debug_vs = nullptr;
    ID3D11PixelShader*        debug_ps = nullptr;
    ID3D11InputLayout*        debug_layout = nullptr;
    ID3D11Buffer*             debug_vb = nullptr, * debug_ib = nullptr;
    ID3D11Texture2D*          debug_tex = nullptr;
    ID3D11ShaderResourceView* debug_srv = nullptr;
    bool   debug_dirty = true;
    std::string shader_path;
};
static State g;
// Initial values (for Space reset).
static float INIT_CONST0[4] = {0,0,0,0};

static const char* filter_name(int m) { return m == 0 ? "POINT" : (m == 2 ? "TRILINEAR" : "BILINEAR"); }

// ---------------------------------------------------------------------------
// 4x4 matrices (row-major with column-vector convention, exactly like the GL and
// Python samples; see set_uniforms() for the single transpose at cbuffer-write time).
// Reimplemented locally on purpose -- depending on DirectXMath would save ~50 lines
// and cost the reader SIMD alignment rules and header dependencies. Zero-dependency,
// one-file readability is a deliberate property of this sample family.
// ---------------------------------------------------------------------------
struct Mat4 { float m[16]; };
static Mat4 mat_identity() { Mat4 r{}; for (int i=0;i<4;i++) r.m[i*4+i]=1.0f; return r; }
static Mat4 mat_mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i=0;i<4;i++) for (int j=0;j<4;j++) { float s=0; for(int k=0;k<4;k++) s+=a.m[i*4+k]*b.m[k*4+j]; r.m[i*4+j]=s; }
    return r;
}
// D3D convention: clip-space depth runs 0..1 (GL runs -1..1), so m[10]/m[14] here
// intentionally differ from the GL sample's mat_perspective -- this is not a bug.
static Mat4 mat_perspective(float fov_deg, float aspect, float znear, float zfar) {
    Mat4 m{};
    float f = 1.0f / std::tan((fov_deg * 3.14159265358979f / 180.0f) / 2.0f);
    m.m[0*4+0] = f / aspect;
    m.m[1*4+1] = f;
    m.m[2*4+2] = zfar / (znear - zfar);
    m.m[2*4+3] = (zfar * znear) / (znear - zfar);
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
static Mat4 mat_transpose(const Mat4& a) {
    Mat4 r{}; for (int i=0;i<4;i++) for (int j=0;j<4;j++) r.m[i*4+j]=a.m[j*4+i]; return r;
}

// ---------------------------------------------------------------------------
// Shader loading (bin/deblock.hlsl, compiled at runtime so developers can edit it
// and press R, exactly as they can with shader.glsl in the GL sample).
// ---------------------------------------------------------------------------
static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

static ID3DBlob* compile_hlsl_file(const std::string& path, const char* entry, const char* target) {
    // Byte-to-wchar widen: fine for the ASCII "deblock.hlsl" path this sample uses,
    // but it mangles non-ASCII paths -- use MultiByteToWideChar(CP_UTF8,...) if you
    // adapt this to load shaders from arbitrary (possibly Unicode) asset paths.
    std::wstring wpath(path.begin(), path.end());
    ID3DBlob* code = nullptr, * errors = nullptr;
    // Warnings print but do not fail the compile: the R-key hot-reload workflow
    // shouldn't refuse an experimental edit over an unused variable. (The shipped
    // shader is verified warning-clean offline with fxc /WX.)
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    HRESULT hr = D3DCompileFromFile(wpath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    entry, target, flags, 0, &code, &errors);
    if (errors) { fprintf(stderr, "%s (%s):\n%s\n", FAILED(hr) ? "SHADER ERROR" : "SHADER WARNING",
                          entry, (const char*)errors->GetBufferPointer()); errors->Release(); }
    if (FAILED(hr)) { safe_release(code); return nullptr; }
    return code;
}

// Compiles deblock.hlsl (VSMain/PSMain), and on success replaces the current
// shaders + input layout. On failure the previous shaders are kept.
static bool load_shader(const std::string& path) {
    ID3DBlob* vsb = compile_hlsl_file(path, "VSMain", "vs_5_0");
    if (!vsb) return false;
    ID3DBlob* psb = compile_hlsl_file(path, "PSMain", "ps_5_0");
    if (!psb) { vsb->Release(); return false; }

    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr; ID3D11InputLayout* layout = nullptr;
    bool ok = SUCCEEDED(g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs)) &&
              SUCCEEDED(g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps));
    if (ok) {
        const D3D11_INPUT_ELEMENT_DESC il[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        ok = SUCCEEDED(g_dev->CreateInputLayout(il, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout));
    }
    vsb->Release(); psb->Release();
    if (!ok) { safe_release(vs); safe_release(ps); safe_release(layout); return false; }

    safe_release(g.vs); safe_release(g.ps); safe_release(g.layout);
    g.vs = vs; g.ps = ps; g.layout = layout;
    printf("Shader compiled successfully.\n");
    return true;
}
static void reload_shader() {
    if (!load_shader(g.shader_path))
        fprintf(stderr, "Shader reload failed, keeping previous shader.\n");
}

// ---------------------------------------------------------------------------
// GPU format selection.
// On desktop D3D11 there is no ASTC or ETC support, so the GL sample's
// ASTC -> BC7 -> ETC2 -> RGBA8 ladder collapses to BC7 -> RGBA8.
//
// IMPORTANT (D3D11 limitation): BC1-7 require the BASE mip level's width AND height
// to be multiples of 4 -- CreateTexture2D fails otherwise, and unlike D3D12 (which has
// the optional UnalignedBlockTexturesSupported feature) D3D11 has no opt-out. So:
//
//   base 4-aligned                  -> BC7, full mip chain.
//   NOT 4-aligned, mipmapped        -> uncompressed RGBA8. No reliable BC workaround
//        exists here: rounding the base up changes the dims D3D derives for every
//        smaller mip (floor(alignedDim/2^L) vs floor(origDim/2^L)), so the transcoded
//        mip data no longer lines up with what the API expects.
//   NOT 4-aligned, single level     -> STILL BC7: create the texture with dims rounded
//        up to a multiple of 4 and upload the transcoded blocks as-is. This is sound
//        because the data already is that size -- the transcoder emits complete 4x4
//        blocks and the encoder filled the padding by duplicating edge rows/columns.
//        We deliberately render the padded texture at UV 0..1 (no UV cropping): the
//        few duplicated edge texels are harmless for a viewer, and it keeps the quad
//        and shader math trivial. (An engine that needs exact bounds would instead
//        scale its UVs by origDim/alignedDim.)
//
// Note this affects the GPU STORAGE format only. The deblock filter block size always
// comes from the file's native ASTC/XUASTC block size, independent of the GPU storage
// format's block size (e.g. BC7 stores 4x4 blocks but we still filter the original
// 12x12 lattice).
// ---------------------------------------------------------------------------
enum PreferFormat { PREF_NONE, PREF_BC7, PREF_RGBA32 };

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

// Load + transcode a KTX2 into a D3D11 texture + SRV. Returns true on success.
// Build-new-then-swap: the new texture is fully constructed before the old one is
// released, so a failed load leaves the previous image on screen.
static bool load_ktx2_texture(const std::string& path, PreferFormat pref, bool no_mips) {
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
    const int base_w  = (int)tc.get_width();
    const int base_h  = (int)tc.get_height();
    // --nomips: upload only the base level (level 0) instead of the full mip chain.
    // Note this also re-enables the BC7 round-up path for non-4-aligned files, since
    // the created texture then has a single level.
    const int load_levels = no_mips ? 1 : levels;

    using TF = basist::transcoder_texture_format;
    auto supported = [&](TF f){ return basist::basis_is_format_supported(f, basis_fmt); };

    const bool base_4_aligned = ((base_w % 4) == 0) && ((base_h % 4) == 0);
    const bool bc7_usable = supported(TF::cTFBC7_RGBA) && (base_4_aligned || load_levels == 1);

    bool use_bc7 = bc7_usable;
    if (pref == PREF_RGBA32) use_bc7 = false;
    if (pref == PREF_BC7 && !bc7_usable) {
        printf("  Note: BC7 not usable for this file (non-4-aligned base with mipmaps?); using RGBA8.\n");
        use_bc7 = false;
    }
    if (use_bc7 && !base_4_aligned)
        printf("  Note: non-4-aligned single-level texture; rounding dims up to 4 for BC7 (padding is encoder-duplicated edge texels).\n");

    const TF tfmt = use_bc7 ? TF::cTFBC7_RGBA : TF::cTFRGBA32;
    const DXGI_FORMAT dxgi_fmt = use_bc7 ? DXGI_FORMAT_BC7_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    // UNORM on purpose, never BC7_UNORM_SRGB / R8G8B8A8_UNORM_SRGB: the whole pipeline
    // samples the stored sRGB-encoded bytes verbatim and writes them straight out with
    // no linear->sRGB encode, matching the GL and WebGL versions. An sRGB format here
    // would make the hardware decode sRGB->linear on sample and the output would
    // display far too dark. Do not "upgrade" this.

    // Texture dims as created: padded up to the block grid on the BC7 round-up path.
    const int tex_w = use_bc7 ? ((base_w + 3) & ~3) : base_w;
    const int tex_h = use_bc7 ? ((base_h + 3) & ~3) : base_h;

    const uint32_t deblock_id = tc.get_deblocking_filter_index();
    std::string fmt_name = ktx2_format_name(tc);
    const char* gpu_fmt_name = use_bc7 ? "DXGI_FORMAT_BC7_UNORM" : "DXGI_FORMAT_R8G8B8A8_UNORM";
    printf("  Source     : %dx%d  levels=%d  fmt=%s\n", base_w, base_h, levels, fmt_name.c_str());
    printf("  GPU format : %s%s\n", gpu_fmt_name, (use_bc7 && !base_4_aligned) ? "  (dims rounded up to 4)" : "");
    printf("  Deblock    : %s  filter block=%dx%d\n", deblock_id==1?"ON":"off", file_bw, file_bh);

    if (!tc.start_transcoding()) { fprintf(stderr, "ERROR: start_transcoding failed\n"); return false; }

    // Transcode every level up front, then create the texture with one
    // D3D11_SUBRESOURCE_DATA per level (immutable -- the sample never re-uploads).
    std::vector<std::vector<uint8_t>> level_data(load_levels);
    std::vector<D3D11_SUBRESOURCE_DATA> subs(load_levels);
    const uint32_t bpb = basist::basis_get_bytes_per_block_or_pixel(tfmt);
    for (int lvl = 0; lvl < load_levels; lvl++) {
        basist::ktx2_image_level_info info;
        if (!tc.get_image_level_info(info, lvl, 0, 0)) { fprintf(stderr,"ERROR: get_image_level_info failed (lvl %d)\n",lvl); return false; }
        const uint32_t ow = info.m_orig_width, oh = info.m_orig_height;
        const uint32_t out_size = basist::basis_compute_transcoded_image_size_in_bytes(tfmt, ow, oh);
        const uint32_t out_units = out_size / bpb; // blocks (BC7) or pixels (RGBA32)
        level_data[lvl].resize(out_size);
        // Disable CPU deblocking: the GPU shader performs it (no double-filtering).
        if (!tc.transcode_image_level(lvl, 0, 0, level_data[lvl].data(), out_units, tfmt,
                                      basist::cDecodeFlagsNoDeblockFiltering)) {
            fprintf(stderr, "ERROR: transcode_image_level failed (lvl %d)\n", lvl); return false;
        }
        // SysMemPitch: bytes per block-row for BC7 (the transcoder emits ceil(ow/4)
        // complete blocks per row), bytes per pixel-row for RGBA32.
        subs[lvl].pSysMem = level_data[lvl].data();
        subs[lvl].SysMemPitch = use_bc7 ? ((ow + 3) / 4) * 16 : ow * 4;
        subs[lvl].SysMemSlicePitch = 0;
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)tex_w; td.Height = (UINT)tex_h;
    td.MipLevels = (UINT)load_levels; td.ArraySize = 1;
    td.Format = dxgi_fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* tex = nullptr; ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = g_dev->CreateTexture2D(&td, subs.data(), &tex);
    if (FAILED(hr)) { fprintf(stderr, "ERROR: CreateTexture2D failed (0x%08X)\n", (unsigned)hr); return false; }
    hr = g_dev->CreateShaderResourceView(tex, nullptr, &srv);
    if (FAILED(hr)) { tex->Release(); fprintf(stderr, "ERROR: CreateShaderResourceView failed (0x%08X)\n", (unsigned)hr); return false; }

    // Success: swap in the new texture, then release the old one.
    safe_release(g.srv); safe_release(g.texture);
    g.texture = tex; g.srv = srv;
    g.tex_w = tex_w; g.tex_h = tex_h;
    g.block_w = file_bw; g.block_h = file_bh;
    g.mip_count = load_levels;
    g.const0[0] = (deblock_id == 1) ? 1.0f : 0.0f;
    INIT_CONST0[0] = g.const0[0];
    g.info_orig_w = base_w; g.info_orig_h = base_h; g.info_mips = levels;
    g.info_block_w = file_bw; g.info_block_h = file_bh; g.info_deblock_id = (int)deblock_id;
    g.info_fmt = fmt_name; g.info_gpu_fmt = use_bc7 ? "BC7" : "RGBA8";
    printf("  Uploaded %d mip level(s)%s.\n", load_levels,
           no_mips ? "  (--nomips: base level only)" : "");
    return true;
}

// ---------------------------------------------------------------------------
// Geometry (position float3 + uv float2, interleaved -- same as the GL sample).
// ---------------------------------------------------------------------------
static ID3D11Buffer* g_quad_vb = nullptr, * g_quad_ib = nullptr;
static ID3D11Buffer* g_cube_vb = nullptr, * g_cube_ib = nullptr;
static int g_cube_index_count = 0;

static ID3D11Buffer* make_buffer(const void* data, size_t bytes, UINT bind) {
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = (UINT)bytes; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = bind;
    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = data;
    ID3D11Buffer* b = nullptr;
    if (FAILED(g_dev->CreateBuffer(&bd, &sd, &b))) return nullptr;
    return b;
}
static void create_quad(float aspect) {
    float hw, hh;
    if (aspect >= 1.0f) { hw=1.0f; hh=1.0f/aspect; } else { hw=aspect; hh=1.0f; }
    // UV origin: standard D3D -- (0,0) = top-left = first uploaded row. KTX2 mip data
    // is stored top-row-first, so the quad maps v 0..1 top-to-bottom with NO flip
    // (top vertices get v=0). Note these UVs are identical to the GL sample's: both
    // APIs treat row 0 of the uploaded data as v=0, and neither projection flips y,
    // so the same mapping is correct in both. Verify at bring-up that the test
    // images render right-side up.
    float v[] = {
        -hw,-hh,0.0f, 0.0f,1.0f,  hw,-hh,0.0f, 1.0f,1.0f,
         hw, hh,0.0f, 1.0f,0.0f, -hw, hh,0.0f, 0.0f,0.0f,
    };
    uint32_t idx[] = {0,1,2, 0,2,3};
    g_quad_vb = make_buffer(v, sizeof(v), D3D11_BIND_VERTEX_BUFFER);
    g_quad_ib = make_buffer(idx, sizeof(idx), D3D11_BIND_INDEX_BUFFER);
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
    g_cube_vb = make_buffer(v, sizeof(v), D3D11_BIND_VERTEX_BUFFER);
    g_cube_ib = make_buffer(idx.data(), idx.size()*sizeof(uint32_t), D3D11_BIND_INDEX_BUFFER);
    g_cube_index_count = (int)idx.size();
}

// ---------------------------------------------------------------------------
// Debug text overlay (8x8 font rasterized to an RGBA texture, drawn as a quad --
// same approach and layout as the GL sample).
// ---------------------------------------------------------------------------
static const int OVL_W = 1280, OVL_H = 84, FONT_SCALE = 2, LINE_ADV = 20;
// The overlay's tiny shaders are embedded here (not in deblock.hlsl) so the
// deliverable shader file stays purely the deblocking operator.
static const char* DEBUG_HLSL =
    "struct VSO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VSO VSMain(float2 pos : POSITION, float2 uv : TEXCOORD0) {\n"
    "    VSO o; o.pos = float4(pos, 0.0, 1.0); o.uv = uv; return o; }\n"
    "Texture2D tex : register(t0); SamplerState samp : register(s0);\n"
    "float4 PSMain(VSO i) : SV_Target { return tex.Sample(samp, i.uv); }\n";

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
    snprintf(l0,sizeof(l0),"Res:%dx%d  Mips:%d  Block:%dx%d  DeblockID:%d  Fmt:%s  GPU:%s",
             g.info_orig_w,g.info_orig_h,g.info_mips,g.info_block_w,g.info_block_h,g.info_deblock_id,
             g.info_fmt.c_str(),g.info_gpu_fmt.c_str());
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

    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(g_ctx->Map(g.debug_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        for (int y = 0; y < OVL_H; y++)
            memcpy((uint8_t*)map.pData + y * map.RowPitch, &buf[y * OVL_W * 4], OVL_W * 4);
        g_ctx->Unmap(g.debug_tex, 0);
    }
    g.debug_dirty = false;
}
static bool init_debug() {
    ID3DBlob* vsb = nullptr, * psb = nullptr, * errs = nullptr;
    if (FAILED(D3DCompile(DEBUG_HLSL, strlen(DEBUG_HLSL), "debug_overlay", nullptr, nullptr,
                          "VSMain", "vs_5_0", 0, 0, &vsb, &errs))) {
        if (errs) { fprintf(stderr, "OVERLAY VS ERROR:\n%s\n", (const char*)errs->GetBufferPointer()); errs->Release(); }
        return false;
    }
    safe_release(errs);
    if (FAILED(D3DCompile(DEBUG_HLSL, strlen(DEBUG_HLSL), "debug_overlay", nullptr, nullptr,
                          "PSMain", "ps_5_0", 0, 0, &psb, &errs))) {
        if (errs) { fprintf(stderr, "OVERLAY PS ERROR:\n%s\n", (const char*)errs->GetBufferPointer()); errs->Release(); }
        vsb->Release(); return false;
    }
    safe_release(errs);
    bool ok = SUCCEEDED(g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g.debug_vs)) &&
              SUCCEEDED(g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g.debug_ps));
    if (ok) {
        const D3D11_INPUT_ELEMENT_DESC il[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        ok = SUCCEEDED(g_dev->CreateInputLayout(il, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g.debug_layout));
    }
    vsb->Release(); psb->Release();
    if (!ok) return false;

    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 16*sizeof(float); bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_dev->CreateBuffer(&bd, nullptr, &g.debug_vb))) return false;
    uint32_t idx[] = {0,1,2, 0,2,3};
    g.debug_ib = make_buffer(idx, sizeof(idx), D3D11_BIND_INDEX_BUFFER);
    if (!g.debug_ib) return false;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = OVL_W; td.Height = OVL_H; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &g.debug_tex))) return false;
    if (FAILED(g_dev->CreateShaderResourceView(g.debug_tex, nullptr, &g.debug_srv))) return false;
    return true;
}
static void draw_debug_text() {
    if (!g.debug_vs) return;
    update_debug_text();
    // Top-left rect of OVL_W x OVL_H pixels in NDC (data row 0 = top => top uv.v=0).
    float w = (float)OVL_W / WINDOW_WIDTH * 2.0f, h = (float)OVL_H / WINDOW_HEIGHT * 2.0f;
    float verts[] = {
        -1.0f,      1.0f,     0.0f, 0.0f,
        -1.0f + w,  1.0f,     1.0f, 0.0f,
        -1.0f + w,  1.0f - h, 1.0f, 1.0f,
        -1.0f,      1.0f - h, 0.0f, 1.0f,
    };
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(g_ctx->Map(g.debug_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, verts, sizeof(verts));
        g_ctx->Unmap(g.debug_vb, 0);
    }
    const float blend_factor[4] = {0,0,0,0};
    g_ctx->OMSetBlendState(g_blend_alpha, blend_factor, 0xFFFFFFFF);
    g_ctx->OMSetDepthStencilState(g_depth_off, 0);
    g_ctx->IASetInputLayout(g.debug_layout);
    UINT stride = 4*sizeof(float), offset = 0;
    g_ctx->IASetVertexBuffers(0, 1, &g.debug_vb, &stride, &offset);
    g_ctx->IASetIndexBuffer(g.debug_ib, DXGI_FORMAT_R32_UINT, 0);
    g_ctx->VSSetShader(g.debug_vs, nullptr, 0);
    g_ctx->PSSetShader(g.debug_ps, nullptr, 0);
    g_ctx->PSSetShaderResources(0, 1, &g.debug_srv);
    g_ctx->PSSetSamplers(0, 1, &g_samplers[0]); // point filtering for crisp text
    g_ctx->DrawIndexed(6, 0, 0);
    g_ctx->OMSetBlendState(nullptr, blend_factor, 0xFFFFFFFF);
    g_ctx->OMSetDepthStencilState(g_depth_on, 0);
}

// ---------------------------------------------------------------------------
// Swap chain / render target (re)creation.
// ---------------------------------------------------------------------------
static bool create_backbuffer_views() {
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(g_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return false;
    HRESULT hr = g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
    bb->Release();
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC dd{};
    dd.Width = (UINT)WINDOW_WIDTH; dd.Height = (UINT)WINDOW_HEIGHT;
    dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(g_dev->CreateTexture2D(&dd, nullptr, &g_depth_tex))) return false;
    if (FAILED(g_dev->CreateDepthStencilView(g_depth_tex, nullptr, &g_dsv))) return false;
    return true;
}
static void resize_backbuffer(int w, int h) {
    if (!g_swapchain || w <= 0 || h <= 0) return;
    WINDOW_WIDTH = w; WINDOW_HEIGHT = h;
    g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    safe_release(g_rtv); safe_release(g_dsv); safe_release(g_depth_tex);
    g_swapchain->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
    if (!create_backbuffer_views())
        fprintf(stderr, "ERROR: backbuffer view recreation failed after resize\n");
    g.debug_dirty = true;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CLOSE: case WM_DESTROY: g_quit = true; PostQuitMessage(0); return 0;
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) resize_backbuffer((int)LOWORD(lp), (int)HIWORD(lp));
            return 0;
        case WM_KEYDOWN: {
            if (lp & (1 << 30)) return 0; // ignore auto-repeat (GL sample acts on PRESS only)
            switch (wp) {
                case VK_ESCAPE: g_quit = true; break;
                case 'R': reload_shader(); break;
                case 'B': g.filter_mode=1; g.debug_dirty=true; printf("Filter: BILINEAR\n"); break;
                case 'T': g.filter_mode=2; g.debug_dirty=true; printf("Filter: TRILINEAR\n"); break;
                case 'P': g.filter_mode=0; g.debug_dirty=true; printf("Filter: POINT\n"); break;
                case 'C': g.cube=!g.cube; g.debug_dirty=true; break;
                case '1': g.const0[0]=1.0f-g.const0[0]; g.debug_dirty=true; break;
                case '2': g.const0[1]=1.0f-g.const0[1]; g.debug_dirty=true; break;
                case '3': g.const0[2]=1.0f-g.const0[2]; g.debug_dirty=true; break;
                case '4': g.const0[3]=1.0f-g.const0[3]; g.debug_dirty=true; break;
                case '5': g.const1[0]=1.0f-g.const1[0]; g.debug_dirty=true; break;
                case '6': g.const1[1]=1.0f-g.const1[1]; g.debug_dirty=true; break;
                case '7': g.const1[2]=1.0f-g.const1[2]; g.debug_dirty=true; break;
                case '8': g.const1[3]=1.0f-g.const1[3]; g.debug_dirty=true; break;
                case VK_SPACE:
                    g.x=0;g.y=0;g.z=-3.0f;g.yaw=0;g.pitch=0;
                    for(int i=0;i<4;i++){g.const0[i]=INIT_CONST0[i];g.const1[i]=0;}
                    g.debug_dirty=true; printf("Reset to initial state\n"); break;
            }
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}
static bool key_down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
static void process_held_keys(HWND hwnd, float dt) {
    if (GetForegroundWindow() != hwnd) return; // GetAsyncKeyState is global; only react when focused
    if (key_down(VK_SHIFT)) dt *= 1.0f/3.0f;
    bool moved=false;
    if (key_down('W')) {g.z+=Z_SPEED*dt;moved=true;}
    if (key_down('S')) {g.z-=Z_SPEED*dt;moved=true;}
    if (key_down(VK_LEFT))  {g.x+=XY_SPEED*dt;moved=true;}
    if (key_down(VK_RIGHT)) {g.x-=XY_SPEED*dt;moved=true;}
    if (key_down(VK_UP))    {g.y+=XY_SPEED*dt;moved=true;}
    if (key_down(VK_DOWN))  {g.y-=XY_SPEED*dt;moved=true;}
    if (key_down('A')) {g.yaw+=ROT_SPEED*dt;moved=true;}
    if (key_down('D')) {g.yaw-=ROT_SPEED*dt;moved=true;}
    if (key_down('Q')) {g.pitch+=ROT_SPEED*dt;moved=true;}
    if (key_down('E')) {g.pitch-=ROT_SPEED*dt;moved=true;}
    if (g.z < Z_MAX) g.z=Z_MAX; if (g.z > Z_MIN) g.z=Z_MIN;
    if (moved) g.debug_dirty=true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
struct CBData {          // must match cbuffer SceneConstants in deblock.hlsl (16-byte rules)
    float mvp[16];
    float texSize[4];
    float lodInfo[4];    // x = maxLod, yzw = padding
    float const0[4];
    float const1[4];
};

static void set_uniforms() {
    Mat4 proj = mat_perspective(FOV_DEGREES, (float)WINDOW_WIDTH/WINDOW_HEIGHT, 0.001f, 100.0f);
    Mat4 model = mat_mul(mat_mul(mat_translate(g.x,g.y,g.z), mat_rot_y(g.yaw)), mat_rot_x(g.pitch));
    Mat4 mvp = mat_mul(proj, model);

    CBData cb{};
    // One transpose at write time: the CPU math is row-major (like the GL sample,
    // which uploaded with transpose=GL_TRUE); HLSL's default cbuffer majority is
    // column-major, so the transpose here makes mul(mvp, v) in the shader see the
    // same matrix the GL shader saw.
    Mat4 mvp_t = mat_transpose(mvp);
    memcpy(cb.mvp, mvp_t.m, sizeof(cb.mvp));
    cb.texSize[0]=(float)g.tex_w; cb.texSize[1]=(float)g.tex_h;
    cb.texSize[2]=(float)g.block_w; cb.texSize[3]=(float)g.block_h;
    cb.lodInfo[0]=(float)(g.mip_count-1);
    memcpy(cb.const0, g.const0, sizeof(cb.const0));
    memcpy(cb.const1, g.const1, sizeof(cb.const1));

    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(g_ctx->Map(g_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, &cb, sizeof(cb));
        g_ctx->Unmap(g_cbuffer, 0);
    }
}

int main(int argc, char** argv) {
    // First non-flag argument is the .ktx2 path. Optional --bc7/--rgba32 flags set a
    // PREFERRED GPU format (used if usable, else the default BC7 -> RGBA8 ladder).
    std::string ktx2_path;
    PreferFormat pref = PREF_NONE;
    bool no_mips = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if      (a == "--bc7")                pref = PREF_BC7;
        else if (a == "--rgba32" || a == "--rgba8") pref = PREF_RGBA32;
        else if (a == "--nomips")             no_mips = true;
        else if (!a.empty() && a[0] == '-')   fprintf(stderr, "WARNING: ignoring unknown option '%s'\n", a.c_str());
        else if (ktx2_path.empty())           ktx2_path = a;
    }
    if (ktx2_path.empty()) {
        printf("Usage: %s <file.ktx2> [--bc7|--rgba32] [--nomips]\n", argv[0]);
        printf("  A format flag prefers that GPU format if usable for this file,\n");
        printf("  otherwise the default BC7 -> RGBA8 ladder applies (BC7 requires a\n");
        printf("  4-aligned base mip, or a single-level texture -- see load_ktx2_texture).\n");
        printf("  --nomips loads only the base mip level (level 0) instead of all levels.\n");
        printf("  The shader is loaded from deblock.hlsl in the working directory.\n");
        printf("  Note: LDR textures only; HDR .ktx2 files are not supported.\n");
        return 1;
    }

    basist::basisu_transcoder_init();

    // Window.
    WNDCLASSEXA wc{}; wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "deblock_d3d11_wc";
    RegisterClassExA(&wc);

    RECT r{0,0,WINDOW_WIDTH,WINDOW_HEIGHT};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "Deblock Shader Testbed (C++/D3D11)",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { fprintf(stderr, "ERROR: window creation failed\n"); return 1; }

    // Device + swap chain. Non-sRGB backbuffer on purpose (see the format comment in
    // load_ktx2_texture): the pipeline passes sRGB-encoded bytes through verbatim.
    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = (UINT)WINDOW_WIDTH; scd.BufferDesc.Height = (UINT)WINDOW_HEIGHT;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    const D3D_FEATURE_LEVEL want_fl = D3D_FEATURE_LEVEL_11_0; // BC7 requires FL 11_0
    D3D_FEATURE_LEVEL got_fl{};
    UINT dev_flags = 0;
#if ENABLE_D3D_DEBUG
    dev_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, dev_flags,
                                               &want_fl, 1, D3D11_SDK_VERSION, &scd,
                                               &g_swapchain, &g_dev, &got_fl, &g_ctx);
#if ENABLE_D3D_DEBUG
    if (FAILED(hr)) {
        // The debug layer needs the "Graphics Tools" optional feature; retry without it.
        fprintf(stderr, "WARNING: D3D11 debug layer unavailable; retrying without it\n");
        dev_flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, dev_flags,
                                           &want_fl, 1, D3D11_SDK_VERSION, &scd,
                                           &g_swapchain, &g_dev, &got_fl, &g_ctx);
    }
#endif
    if (FAILED(hr)) { fprintf(stderr, "ERROR: D3D11CreateDeviceAndSwapChain failed (0x%08X)\n", (unsigned)hr); return 1; }
    printf("Direct3D 11, feature level 0x%04X%s\n", (unsigned)got_fl,
           (dev_flags & D3D11_CREATE_DEVICE_DEBUG) ? "  (debug layer active)" : "");

    if (!create_backbuffer_views()) { fprintf(stderr, "ERROR: backbuffer view creation failed\n"); return 1; }

    // Fixed-function state.
    {
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE; // the GL sample never enables face culling
        rd.DepthClipEnable = TRUE;
        g_dev->CreateRasterizerState(&rd, &g_raster);

        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS;
        g_dev->CreateDepthStencilState(&dd, &g_depth_on);
        dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        g_dev->CreateDepthStencilState(&dd, &g_depth_off);

        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        g_dev->CreateBlendState(&bd, &g_blend_alpha);

        // Three sampler states matching the GL sample's P/B/T modes, all CLAMP.
        D3D11_SAMPLER_DESC sd{};
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;        g_dev->CreateSamplerState(&sd, &g_samplers[0]);
        sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT; g_dev->CreateSamplerState(&sd, &g_samplers[1]);
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;       g_dev->CreateSamplerState(&sd, &g_samplers[2]);

        D3D11_BUFFER_DESC cbd{}; cbd.ByteWidth = sizeof(CBData); cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_dev->CreateBuffer(&cbd, nullptr, &g_cbuffer);
    }

    // Try deblock.hlsl in the cwd, else next to the executable.
    g.shader_path = "deblock.hlsl";
    if (read_file(g.shader_path).empty()) {
        std::string a = argv[0]; size_t s = a.find_last_of("/\\");
        if (s != std::string::npos) g.shader_path = a.substr(0,s+1) + "deblock.hlsl";
    }
    if (!load_shader(g.shader_path)) return 1;

    if (!load_ktx2_texture(ktx2_path, pref, no_mips)) return 1;

    g.filter_mode = 2; // trilinear default

    create_quad((float)g.tex_w / (float)g.tex_h);
    create_cube();
    if (!init_debug()) fprintf(stderr, "WARNING: debug overlay init failed (continuing without it)\n");

    const float clear_color[4] = {0.2f, 0.2f, 0.2f, 1.0f};
    LARGE_INTEGER qpf, last, now; QueryPerformanceFrequency(&qpf); QueryPerformanceCounter(&last);

    while (!g_quit) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (g_quit) break;

        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - last.QuadPart) / (double)qpf.QuadPart);
        last = now;
        if (dt > 0.1f) dt = 0.1f; // cap after a stall (occluded window / compositor) to avoid a teleport
        process_held_keys(hwnd, dt);

        // If the backbuffer views were lost (a resize-time creation failure), skip
        // drawing but keep presenting so the loop stays vsync-throttled.
        if (!g_rtv || !g_dsv) { g_swapchain->Present(1, 0); continue; }

        D3D11_VIEWPORT vp{}; vp.Width = (float)WINDOW_WIDTH; vp.Height = (float)WINDOW_HEIGHT; vp.MaxDepth = 1.0f;
        g_ctx->RSSetViewports(1, &vp);
        g_ctx->RSSetState(g_raster);
        g_ctx->OMSetRenderTargets(1, &g_rtv, g_dsv);
        g_ctx->ClearRenderTargetView(g_rtv, clear_color);
        g_ctx->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        g_ctx->OMSetDepthStencilState(g_depth_on, 0);

        set_uniforms();
        g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_ctx->IASetInputLayout(g.layout);
        g_ctx->VSSetShader(g.vs, nullptr, 0);
        g_ctx->PSSetShader(g.ps, nullptr, 0);
        g_ctx->VSSetConstantBuffers(0, 1, &g_cbuffer);
        g_ctx->PSSetConstantBuffers(0, 1, &g_cbuffer);
        g_ctx->PSSetShaderResources(0, 1, &g.srv);
        g_ctx->PSSetSamplers(0, 1, &g_samplers[g.filter_mode]);

        UINT stride = 5*sizeof(float), offset = 0;
        if (g.cube) {
            g_ctx->IASetVertexBuffers(0, 1, &g_cube_vb, &stride, &offset);
            g_ctx->IASetIndexBuffer(g_cube_ib, DXGI_FORMAT_R32_UINT, 0);
            g_ctx->DrawIndexed((UINT)g_cube_index_count, 0, 0);
        } else {
            g_ctx->IASetVertexBuffers(0, 1, &g_quad_vb, &stride, &offset);
            g_ctx->IASetIndexBuffer(g_quad_ib, DXGI_FORMAT_R32_UINT, 0);
            g_ctx->DrawIndexed(6, 0, 0);
        }

        draw_debug_text();

        g_swapchain->Present(1, 0); // vsync, like the GL sample's swap interval 1
    }

    // Intentionally no explicit release of the D3D objects / device / swap chain:
    // process exit reclaims everything. A real app should hold these in RAII wrappers
    // (or ComPtr) and release them -- don't copy this teardown-by-exit pattern.
    printf("Done.\n");
    return 0;
}

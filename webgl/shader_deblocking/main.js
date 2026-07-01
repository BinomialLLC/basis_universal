// Shader Deblocking — WebGL 2 browser port of the native shader_deblocking_glfw sample.
// Copyright (C) 2026 Binomial LLC.  LICENSE: Apache 2.0
//
// Loads a Basis Universal .KTX2 (via the WASM transcoder), transcodes every mip
// level to a GPU format (ASTC -> BC7 -> ETC1/ETC2, falling back to uncompressed
// RGBA32), and renders it on a textured quad or cube with the mipmap-aware
// deblocking pixel shader (GLSL ES 3.00 / #version 300 es).
//
// Controls: arrows move, W/S zoom, A/D yaw, Q/E pitch, C cube/quad, B/T/P filter,
//           1 deblock on/off, 2 edge visualization, 3-8 spare consts, Space reset.

'use strict';

// ---------------------------------------------------------------------------
// Tunables (mirror the native sample's constants).
// ---------------------------------------------------------------------------
var FOV_DEGREES = 90.0;
var Z_NEAR = 0.001, Z_FAR = 100.0;
var Z_MIN = 0.40, Z_MAX = -50.0;     // camera-z clamp range (z is negative going away)
var Z_SPEED = 1.0, XY_SPEED = 0.75, ROT_SPEED = 90.0;
var DEFAULT_ASSET = 'assets/kodim23.ktx2';

// ---------------------------------------------------------------------------
// Shaders — the native bin/shader.glsl ported to #version 300 es. The deblock
// body is byte-identical to the desktop version; only the version line and the
// fragment precision qualifiers differ (dFdx/dFdy are core in GLSL ES 3.00).
// NOTE: "#version 300 es" MUST be the very first characters of the source.
// ---------------------------------------------------------------------------
var VERTEX_SHADER_SRC = `#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 mvp;

out vec2 vUV;

void main() {
    vUV = aUV;
    gl_Position = mvp * vec4(aPos, 1.0);
}
`;

var FRAGMENT_SHADER_SRC = `#version 300 es
precision highp float;
precision highp sampler2D;

uniform sampler2D tex;
uniform vec4 texSize;  // xy = base mip (mip0) dims, zw = deblock block size in texels
uniform float maxLod;  // number_of_mip_levels - 1

uniform vec4 const0;   // x = deblock on, y = edge-weight viz, z/w spare
uniform vec4 const1;   // spare user constants

in vec2 vUV;
out vec4 fragColor;

void main()
{
    vec2 texDim = vec2(texSize.x, texSize.y);
    vec2 blockSize = vec2(texSize.z, texSize.w);

    // Recover the effective mip level from screen-space UV derivatives, so the
    // deblock lattice tracks whichever mip the hardware is actually sampling.
    vec2 du = dFdx(vUV);
    vec2 dv = dFdy(vUV);
    float rho = max(length(du * texSize.xy), length(dv * texSize.xy));
    float lod = clamp(log2(max(rho, 1e-8)), 0.0, maxLod);
    float mipScale = exp2(floor(lod + 0.5)); // snap to dominant mip (1=mip0, 2=mip1, ...)

    vec2 texelStep = mipScale / texDim;          // one texel in effective mip space
    vec2 texelPos  = (vUV * texDim) / mipScale;  // texel coord in effective mip space
    vec2 blockPos  = mod(texelPos, blockSize);   // offset within the current block

    vec3 color = texture(tex, vUV).rgb;

    // Keep these fetches outside non-uniform control flow: texture() uses implicit
    // derivatives for LOD, which are undefined when neighboring fragments take
    // different branches. Use base sampling so edgeWeight can safely go to zero.
    vec3 l1 = texture(tex, vUV - vec2(texelStep.x, 0.0)).rgb;
    vec3 r1 = texture(tex, vUV + vec2(texelStep.x, 0.0)).rgb;
    vec3 u1 = texture(tex, vUV - vec2(0.0, texelStep.y)).rgb;
    vec3 d1 = texture(tex, vUV + vec2(0.0, texelStep.y)).rgb;

    if (const0.x > 0.5)
    {
        const float falloff = 1.5;

        float leftProx   = 1.0 - clamp(blockPos.x / falloff, 0.0, 1.0);
        float rightProx  = 1.0 - clamp((blockSize.x - blockPos.x) / falloff, 0.0, 1.0);
        float topProx    = 1.0 - clamp(blockPos.y / falloff, 0.0, 1.0);
        float bottomProx = 1.0 - clamp((blockSize.y - blockPos.y) / falloff, 0.0, 1.0);

        float horizWeight = max(leftProx, rightProx);
        float vertWeight  = max(topProx, bottomProx);
        float edgeWeight  = max(horizWeight, vertWeight);

        if (edgeWeight > 0.0)
        {
            vec3 c0 = color;
            vec3 filteredH = (l1 + c0 + r1) * (1.0 / 3.0);
            vec3 filteredV = (u1 + c0 + d1) * (1.0 / 3.0);

            float strengthH = horizWeight;
            float strengthV = vertWeight;

            vec3 horizColor = mix(c0, filteredH, strengthH);
            vec3 vertColor  = mix(c0, filteredV, strengthV);

            float totalW = strengthH + strengthV;
            if (totalW > 0.0)
                color = (horizColor * strengthH + vertColor * strengthV) / totalW;
        }

        // Block-edge weight visualization (key 2).
        if (const0.y > 0.5)
            color = vec3(edgeWeight, edgeWeight, edgeWeight);
    }

    fragColor = vec4(color, 1.0);
}
`;

// ---------------------------------------------------------------------------
// GPU format constants + transcoder_texture_format enum (subset; LDR only).
// ---------------------------------------------------------------------------
// WEBGL_compressed_texture_astc internal formats keyed by literal (w,h). The GL
// enum order is W-major and is NOT the same as Basis's area-ordered transcoder
// enum (note 8x8 sits out of numeric order), so we ALWAYS map by (w,h), never by
// an ordinal.
//
// IMPORTANT: we deliberately use the LINEAR (non-sRGB) ASTC enums (0x93B0..), NOT
// the sRGB variants (base + 0x20). This shader writes the sampled texel straight
// to the canvas with no linear->sRGB encode, so the stored (already-sRGB-encoded)
// bytes must be sampled verbatim. An sRGB internal format would make the hardware
// decode sRGB->linear on sample, and the un-encoded linear output then displays
// far too dark. BC7 (UNORM) and the native C++ sample behave the same way.
var ASTC_GL = {
   '4,4': 0x93B0, '5,4': 0x93B1, '5,5': 0x93B2, '6,5': 0x93B3, '6,6': 0x93B4,
   '8,5': 0x93B5, '8,6': 0x93B6, '8,8': 0x93B7, '10,5': 0x93B8, '10,6': 0x93B9,
   '10,8': 0x93BA, '10,10': 0x93BB, '12,10': 0x93BC, '12,12': 0x93BD
};
// Matching transcoder_texture_format values (Module.transcoder_texture_format).
var ASTC_TF = {
   '4,4': 10, '5,4': 28, '5,5': 29, '6,5': 30, '6,6': 31, '8,5': 32, '8,6': 33,
   '10,5': 34, '10,6': 35, '8,8': 36, '10,8': 37, '10,10': 38, '12,10': 39, '12,12': 40
};
var TF_BC7 = 6, TF_ETC1 = 0, TF_RGBA32 = 13;
var GL_COMPRESSED_RGBA_BPTC_UNORM = 0x8E8C;
var GL_COMPRESSED_RGB_ETC1_WEBGL = 0x8D64;  // ETC1 is RGB-only (alpha dropped); fine for these LDR photos
var GL_COMPRESSED_RGB8_ETC2 = 0x9274;       // ETC2 RGB (superset of ETC1) — used when only the ETC2 ext is present
                                            // (linear, non-sRGB — see the ASTC note above for why)

function astcGLName(fmt) {
   for (var k in ASTC_GL) { if (ASTC_GL[k] === fmt) return 'ASTC_' + k.replace(',', 'x'); }
   if (ASTC_GL['4,4'] + 0x20 <= fmt && fmt <= ASTC_GL['12,12'] + 0x20) {
      for (var k2 in ASTC_GL) { if (ASTC_GL[k2] + 0x20 === fmt) return 'SRGB_ASTC_' + k2.replace(',', 'x'); }
   }
   return '0x' + fmt.toString(16);
}

// ---------------------------------------------------------------------------
// Column-major 4x4 matrices (WebGL uniformMatrix4fv requires transpose=false,
// so we build column-major directly — the native sample was row-major + GL_TRUE).
// ---------------------------------------------------------------------------
// All matrix builders write into a caller-provided Float32Array `o` (and return
// it), so draw() can reuse module-scope scratch buffers instead of allocating a
// fresh matrix every frame (avoids ~360 allocs/sec of GC churn on mobile).
function mat4Identity(o) {
   o[0] = 1; o[1] = 0; o[2] = 0; o[3] = 0;
   o[4] = 0; o[5] = 1; o[6] = 0; o[7] = 0;
   o[8] = 0; o[9] = 0; o[10] = 1; o[11] = 0;
   o[12] = 0; o[13] = 0; o[14] = 0; o[15] = 1;
   return o;
}
function mat4Multiply(a, b, o) { // o = a*b; o must not alias a or b
   for (var c = 0; c < 4; c++)
      for (var r = 0; r < 4; r++) {
         var s = 0;
         for (var k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
         o[c * 4 + r] = s;
      }
   return o;
}
function mat4Perspective(fovDeg, aspect, near, far, o) {
   var f = 1.0 / Math.tan((fovDeg * Math.PI / 180.0) / 2.0);
   for (var i = 0; i < 16; i++) o[i] = 0;
   o[0] = f / aspect;
   o[5] = f;
   o[10] = (far + near) / (near - far);
   o[11] = -1.0;
   o[14] = (2.0 * far * near) / (near - far);
   return o;
}
function mat4Translate(x, y, z, o) {
   mat4Identity(o); o[12] = x; o[13] = y; o[14] = z; return o;
}
function mat4RotX(deg, o) {
   var r = deg * Math.PI / 180.0, c = Math.cos(r), s = Math.sin(r);
   mat4Identity(o); o[5] = c; o[6] = s; o[9] = -s; o[10] = c; return o;
}
function mat4RotY(deg, o) {
   var r = deg * Math.PI / 180.0, c = Math.cos(r), s = Math.sin(r);
   mat4Identity(o); o[0] = c; o[2] = -s; o[8] = s; o[10] = c; return o;
}

// ---------------------------------------------------------------------------
// Global state.
// ---------------------------------------------------------------------------
var gl = null;
var canvas = null;
var Module = null;

var prog = null;          // { program, uniforms{}, attribs{} }
var caps = { astc: false, bc7: false, etc1: false, etc2: false };
var astcExt = null, bptcExt = null, etc1Ext = null, etcExt = null;

var quad = null;          // { vao, count }
var cube = null;          // { vao, count }
var texture = null;

var g = {
   x: 0, y: 0, z: -0.4, yaw: 0, pitch: 0,
   cube: false,
   filterMode: 2,         // 0=point, 1=bilinear, 2=trilinear (native startup = trilinear)
   const0: [0, 0, 0, 0],
   const1: [0, 0, 0, 0]
};
var initConst0 = [0, 0, 0, 0];

// Info about the currently loaded texture (for the overlay).
var texInfo = {
   w: 0, h: 0, levels: 1, blockW: 4, blockH: 4,
   deblockId: 0, srcFmt: '-', gpuFmt: '-'
};

var heldKeys = {};
var lastTime = 0;
// Reused scratch matrices for draw() (see mat4* note) — avoids per-frame allocation.
var _proj = new Float32Array(16), _trans = new Float32Array(16);
var _rotY = new Float32Array(16), _rotX = new Float32Array(16), _rot = new Float32Array(16);
var _model = new Float32Array(16), _mvp = new Float32Array(16);
var rafId = 0;                    // requestAnimationFrame handle (so we can stop/restart on context loss)
var currentUrl = DEFAULT_ASSET;  // last-requested texture URL (for context-restore reload)
var loadGen = 0;                 // monotonic token: newest load wins, stale loads bail out
var loading = false;             // drives the "Loading…" overlay during a transcode

// ---------------------------------------------------------------------------
// GL helpers.
// ---------------------------------------------------------------------------
function compileShader(src, type) {
   var sh = gl.createShader(type);
   gl.shaderSource(sh, src);
   gl.compileShader(sh);
   if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
      var log = gl.getShaderInfoLog(sh);
      console.error((type === gl.VERTEX_SHADER ? 'VERTEX' : 'FRAGMENT') + ' shader error:\n' + log);
      gl.deleteShader(sh);
      throw new Error('Shader compile failed');
   }
   return sh;
}
function buildProgram(vsSrc, fsSrc) {
   var program = gl.createProgram();
   gl.attachShader(program, compileShader(vsSrc, gl.VERTEX_SHADER));
   gl.attachShader(program, compileShader(fsSrc, gl.FRAGMENT_SHADER));
   gl.linkProgram(program);
   if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      console.error('Program link failed:\n' + gl.getProgramInfoLog(program));
      throw new Error('Program link failed');
   }
   var uniforms = {};
   var nu = gl.getProgramParameter(program, gl.ACTIVE_UNIFORMS);
   for (var i = 0; i < nu; i++) {
      var ui = gl.getActiveUniform(program, i);
      uniforms[ui.name] = gl.getUniformLocation(program, ui.name);
   }
   var attribs = {};
   var na = gl.getProgramParameter(program, gl.ACTIVE_ATTRIBUTES);
   for (var j = 0; j < na; j++) {
      var ai = gl.getActiveAttrib(program, j);
      attribs[ai.name] = gl.getAttribLocation(program, ai.name);
   }
   return { program: program, uniforms: uniforms, attribs: attribs };
}

// Interleaved [pos.xyz, uv.xy] mesh -> VAO. Attribute locations match the
// shader's layout(location=...) (0 = pos, 1 = uv).
function makeMesh(verts, indices) {
   var vao = gl.createVertexArray();
   gl.bindVertexArray(vao);

   var vbo = gl.createBuffer();
   gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
   gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

   var ebo = gl.createBuffer();
   gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ebo);
   gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);

   var stride = 5 * 4;
   gl.enableVertexAttribArray(0);
   gl.vertexAttribPointer(0, 3, gl.FLOAT, false, stride, 0);
   gl.enableVertexAttribArray(1);
   gl.vertexAttribPointer(1, 2, gl.FLOAT, false, stride, 3 * 4);

   gl.bindVertexArray(null);
   // Keep the VBO/EBO handles: deleting the VAO does NOT free its buffer objects
   // in WebGL, so freeMesh() must delete all three or they leak on every reload.
   return { vao: vao, vbo: vbo, ebo: ebo, count: indices.length };
}

// Free a mesh's VAO and its owned VBO/EBO. Safe to call with null.
function freeMesh(m) {
   if (!m) return;
   gl.deleteVertexArray(m.vao);
   gl.deleteBuffer(m.vbo);
   gl.deleteBuffer(m.ebo);
}

// Quad fitted to the texture aspect (UVs v-flipped: bottom row v=1, top v=0).
function createQuad(aspect) {
   var hw, hh;
   if (aspect >= 1.0) { hw = 1.0; hh = 1.0 / aspect; } else { hw = aspect; hh = 1.0; }
   var v = new Float32Array([
      -hw, -hh, 0.0, 0.0, 1.0,
      hw, -hh, 0.0, 1.0, 1.0,
      hw, hh, 0.0, 1.0, 0.0,
      -hw, hh, 0.0, 0.0, 0.0
   ]);
   var idx = new Uint16Array([0, 1, 2, 0, 2, 3]);
   return makeMesh(v, idx);
}

// Unit cube (half-extent 0.5), same texture mapped 0..1 on all six faces.
function createCube() {
   var h = 0.5;
   var v = new Float32Array([
      // front (z=+h)
      -h, -h, h, 0, 1, h, -h, h, 1, 1, h, h, h, 1, 0, -h, h, h, 0, 0,
      // back (z=-h)
      h, -h, -h, 0, 1, -h, -h, -h, 1, 1, -h, h, -h, 1, 0, h, h, -h, 0, 0,
      // right (x=+h)
      h, -h, h, 0, 1, h, -h, -h, 1, 1, h, h, -h, 1, 0, h, h, h, 0, 0,
      // left (x=-h)
      -h, -h, -h, 0, 1, -h, -h, h, 1, 1, -h, h, h, 1, 0, -h, h, -h, 0, 0,
      // top (y=+h)
      -h, h, h, 0, 1, h, h, h, 1, 1, h, h, -h, 1, 0, -h, h, -h, 0, 0,
      // bottom (y=-h)
      -h, -h, -h, 0, 1, h, -h, -h, 1, 1, h, -h, h, 1, 0, -h, -h, h, 0, 0
   ]);
   var idx = new Uint16Array(36);
   for (var i = 0; i < 6; i++) {
      var b = i * 4, o = i * 6;
      idx[o] = b; idx[o + 1] = b + 1; idx[o + 2] = b + 2;
      idx[o + 3] = b; idx[o + 4] = b + 2; idx[o + 5] = b + 3;
   }
   return makeMesh(v, idx);
}

// ---------------------------------------------------------------------------
// GPU format support detection (WebGL 2: compressed formats are still
// extensions; compressedTexImage2D + NPOT mip chains are core).
// ---------------------------------------------------------------------------
function detectCaps() {
   astcExt = gl.getExtension('WEBGL_compressed_texture_astc');
   bptcExt = gl.getExtension('EXT_texture_compression_bptc');
   etc1Ext = gl.getExtension('WEBGL_compressed_texture_etc1');
   etcExt  = gl.getExtension('WEBGL_compressed_texture_etc');   // ETC2/EAC (GLES3-core; common on Android)
   caps.astc = !!astcExt;
   caps.bc7 = !!bptcExt;
   caps.etc1 = !!etc1Ext;
   caps.etc2 = !!etcExt;
   console.log('GPU compressed support: ASTC=' + (caps.astc ? 1 : 0) +
      ' BC7=' + (caps.bc7 ? 1 : 0) + ' ETC1=' + (caps.etc1 ? 1 : 0) + ' ETC2=' + (caps.etc2 ? 1 : 0));
}

// Pick a GPU storage format via the ASTC(same block size) -> BC7 -> ETC1/ETC2 -> RGBA32
// ladder, each rung gated on the GPU advertising it.
//
// IMPORTANT (D3D/ANGLE limitation): BC1-7 (and ETC1, also a 4x4-block format)
// require the BASE mip level's width AND height to be multiples of 4. ANGLE/D3D
// (Windows desktop browsers) rejects a non-multiple-of-4 base (e.g. 700x999) with
// GL_INVALID_OPERATION. So when the base mip isn't 4-aligned we skip the 4x4-block
// formats and fall back to uncompressed RGBA32 (which works at any size). ASTC has
// no such restriction -- the hardware handles partial edge blocks -- so it is used
// at any size, which is the common case on mobile.
// (Whether a D3D12 device can create block-compressed textures with unaligned
// dimensions is an OPTIONAL feature, D3D12_FEATURE_DATA_D3D12_OPTIONS8::
// UnalignedBlockTexturesSupported; WebGL/ANGLE does not expose or rely on it. See
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_d3d12_options8 )
//
// DEVELOPER WORKAROUND for a NON-mipmapped texture (levels == 1): you CAN still use
// BC7/ETC on a non-4-aligned image. Round the texture dimensions UP to a multiple of
// 4 (the encoder already pads the last block row/col with duplicated edge texels),
// upload that block-aligned size, and then only SAMPLE the sub-rectangle that is the
// real image (scale your UVs by imageDim/alignedDim), ignoring the padded tail. This
// is exactly what our WebGL KTX2/DDS Studio app does. It works because there are no
// smaller mips whose dimensions the driver would derive.
//   For a MIPMAPPED texture there is no reliable/simple workaround: the driver computes each
// mip level's dimensions itself from the (aligned) base, and those derived sizes
// diverge from the KTX2 mip dimensions (floor(origDim/2^L) vs floor(alignedDim/2^L)),
// so the block data no longer lines up. Hence: mipmapped + non-4-aligned base -> RGBA32.
// NOTE: all rungs use LINEAR (non-sRGB) GPU internal formats on purpose — the shader
// outputs sampled texels straight to the canvas, so the stored sRGB-encoded bytes must
// be sampled verbatim (an sRGB internal format would decode to linear and display dark).
function chooseFormat(bw, bh, w, h) {
   var key = bw + ',' + bh;
   if (caps.astc && ASTC_GL[key] !== undefined) {
      var glf = ASTC_GL[key];   // linear enum (no +0x20 sRGB variant) — see note above
      return { tf: ASTC_TF[key], glFmt: glf, compressed: true, name: astcGLName(glf) };
   }
   var base4Aligned = (w % 4 === 0) && (h % 4 === 0);
   if (caps.bc7 && base4Aligned)
      return { tf: TF_BC7, glFmt: GL_COMPRESSED_RGBA_BPTC_UNORM, compressed: true, name: 'BC7' };
   // ETC path: always transcode to ETC1 (RGB). Upload via the native ETC1 GL format
   // when WEBGL_compressed_texture_etc1 is present; otherwise via the ETC2-RGB enum
   // (every ETC1 block is a valid ETC2 RGB block) — that's the extension most Android
   // WebGL2 contexts actually expose, so probing both maximizes device coverage.
   if ((caps.etc1 || caps.etc2) && base4Aligned) {
      var etcGl = caps.etc1 ? GL_COMPRESSED_RGB_ETC1_WEBGL : GL_COMPRESSED_RGB8_ETC2;
      return { tf: TF_ETC1, glFmt: etcGl, compressed: true, name: caps.etc1 ? 'ETC1' : 'ETC2_RGB' };
   }
   // Uncompressed fallback: always valid, any dimensions. (Used when no GPU format
   // is available, OR when the base mip isn't 4-aligned for the BC/ETC path.)
   return { tf: TF_RGBA32, glFmt: gl.RGBA8, compressed: false, name: 'RGBA8' };
}

// Best-effort source-format name for the overlay (matches native ktx2_format_name).
function sourceFormatName(ktx2File, bw, bh) {
   function has(m) { return typeof ktx2File[m] === 'function'; }
   try {
      if (has('isETC1S') && ktx2File.isETC1S()) return 'ETC1S';
      if (has('isXUASTC_LDR') && ktx2File.isXUASTC_LDR()) return 'XUASTC LDR ' + bw + 'x' + bh;
      if (has('isASTC_LDR') && ktx2File.isASTC_LDR()) return 'ASTC LDR ' + bw + 'x' + bh;
      if (has('isUASTC') && ktx2File.isUASTC()) return 'UASTC LDR 4x4';
      if (has('isXUBC7') && ktx2File.isXUBC7()) return 'XUBC7';
   } catch (e) { /* fall through */ }
   return 'LDR ' + bw + 'x' + bh;
}

// ---------------------------------------------------------------------------
// Texture loading: fetch .ktx2 -> transcode every mip level -> upload.
// ---------------------------------------------------------------------------
function loadKtx2(url) {
   var myGen = ++loadGen;   // newest load wins; older in-flight loads bail out below
   currentUrl = url;
   loading = true;          // updateOverlay shows "Loading…" until this finishes
   return fetch(url).then(function (resp) {
      if (!resp.ok) throw new Error('Failed to fetch ' + url + ' (HTTP ' + resp.status + ')');
      return resp.arrayBuffer();
   }).then(function (buf) {
      if (myGen !== loadGen) return;   // a newer load started while we were fetching
      uploadKtx2(new Uint8Array(buf), url);
      loading = false;
   }).catch(function (e) {
      if (myGen === loadGen) loading = false;
      showError(e.message || String(e));
      console.error(e);
   });
}

function uploadKtx2(bytes, url) {
   // A fetch that started before a context loss can resolve after it but before the
   // restore rebuilds — don't upload into a dead context (the restore's own reload
   // will publish the texture). "Newest wins" across overlapping loads is already
   // guaranteed by the loadGen check in loadKtx2 + uploadKtx2 running synchronously.
   if (gl.isContextLost()) return;

   var ktx2File = new Module.KTX2File(bytes);
   var tex = null;   // hoisted so the finally can free it if we throw before publishing
   try {
      if (!ktx2File.isValid()) throw new Error('Invalid .ktx2 file: ' + url);
      if (ktx2File.isHDR()) throw new Error('HDR .ktx2 not supported (this sample is LDR-only): ' + url);

      var w = ktx2File.getWidth();
      var h = ktx2File.getHeight();
      var levels = ktx2File.getLevels();
      var bw = ktx2File.getBlockWidth();
      var bh = ktx2File.getBlockHeight();
      var deblockId = ktx2File.getDeblockingFilterIndex();

      var fmt = chooseFormat(bw, bh, w, h);

      if (!ktx2File.startTranscoding()) throw new Error('startTranscoding failed: ' + url);

      var noDeblockFlag = Module.basisu_decode_flags.cDecodeFlagsNoDeblockFiltering.value;

      // Build the new texture into a LOCAL first; the currently displayed texture/quad
      // are only swapped out (and freed) after the upload fully succeeds. So a failed
      // load leaves the previous image on screen instead of blanking it. (Holding two
      // textures for the brief overlap is fine — the assets are small.)
      tex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);

      // Upload each mip level at its original (unpadded) dimensions. The
      // transcoder emits ceil(w/4) x ceil(h/4) blocks with the edge blocks padded,
      // and WebGL computes the expected byte size the same way, so the sizes match.
      // Deep mips that floor to non-multiple-of-4 dims (e.g. 6x4, 3x2) are fine;
      // only the BASE mip must be 4-aligned for BC/ETC1, which chooseFormat()
      // already guaranteed (else it picked RGBA32).
      for (var lvl = 0; lvl < levels; lvl++) {
         var info = ktx2File.getImageLevelInfo(lvl, 0, 0);
         var ow = info.origWidth, oh = info.origHeight;
         var dstSize = ktx2File.getImageTranscodedSizeInBytes(lvl, 0, 0, fmt.tf);
         var dst = new Uint8Array(dstSize);
         // Pass the no-CPU-deblock flag: the GPU shader does the deblocking, so
         // letting the transcoder also deblock would double-filter.
         if (!ktx2File.transcodeImageWithFlags(dst, lvl, 0, 0, fmt.tf, noDeblockFlag, -1, -1))
            throw new Error('transcodeImageWithFlags failed (level ' + lvl + ')');

         if (fmt.compressed)
            gl.compressedTexImage2D(gl.TEXTURE_2D, lvl, fmt.glFmt, ow, oh, 0, dst);
         else
            gl.texImage2D(gl.TEXTURE_2D, lvl, gl.RGBA8, ow, oh, 0, gl.RGBA, gl.UNSIGNED_BYTE, dst);
      }

      // One getError after the whole chain (each gl.getError() forces a CPU<->GPU
      // sync, so we avoid doing it per level). The base mip is the usual culprit
      // (e.g. a non-4-aligned BC base — guarded against in chooseFormat).
      var err = gl.getError();
      if (err !== gl.NO_ERROR)
         throw new Error('texture upload failed (' + w + 'x' + h + ' ' + fmt.name +
            ', fmt 0x' + fmt.glFmt.toString(16) + '): GL 0x' + err.toString(16));

      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_BASE_LEVEL, 0);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAX_LEVEL, levels - 1);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

      // Upload succeeded — now free the previous texture and swap in the new one.
      if (texture) gl.deleteTexture(texture);
      texture = tex;
      texInfo = {
         w: w, h: h, levels: levels, blockW: bw, blockH: bh,
         deblockId: deblockId,
         srcFmt: sourceFormatName(ktx2File, bw, bh),
         gpuFmt: fmt.name
      };

      // Auto-enable the GPU deblocking filter based on the KTX2 file's stored
      // DeblockFilterID (KTX2File.getDeblockingFilterIndex()): the encoder sets it
      // to 1 when the texture was compressed with deblocking-aware encoding, so the
      // viewer should deblock by default. This mirrors the native C++ sample. The
      // user can still toggle it (key 1 / the Deblock button); we capture the auto
      // value as the Space-reset default.
      g.const0[0] = (deblockId === 1) ? 1.0 : 0.0;
      initConst0 = g.const0.slice();

      freeMesh(quad);
      quad = createQuad(w / h);
      applyFilterMode();

      hideError();
      console.log('Loaded ' + url + ': ' + w + 'x' + h + ' levels=' + levels +
         ' block=' + bw + 'x' + bh + ' src=' + texInfo.srcFmt + ' gpu=' + fmt.name +
         ' deblockID=' + deblockId);
   } finally {
      // If we threw after creating the new texture but before publishing it (texture
      // still points at the old one, or null), free the orphaned handle so failed
      // loads don't leak a GL texture.
      if (tex && texture !== tex) gl.deleteTexture(tex);
      ktx2File.close();
      ktx2File.delete();
   }
}

function applyFilterMode() {
   if (!texture) return;
   gl.bindTexture(gl.TEXTURE_2D, texture);
   var mips = texInfo.levels > 1;
   if (g.filterMode === 1) {        // bilinear
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, mips ? gl.LINEAR_MIPMAP_NEAREST : gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
   } else if (g.filterMode === 2) { // trilinear
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, mips ? gl.LINEAR_MIPMAP_LINEAR : gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
   } else {                         // point
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, mips ? gl.NEAREST_MIPMAP_NEAREST : gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
   }
}

// ---------------------------------------------------------------------------
// Rendering.
// ---------------------------------------------------------------------------
function resizeCanvas() {
   // Cap DPR: on a DPR-3 phone an uncapped backing store can be ~25M pixels, which
   // (with the 5-tap deblock shader) risks context loss / thermal throttling.
   var dpr = Math.min(window.devicePixelRatio || 1, 2);
   var w = Math.max(1, Math.floor(canvas.clientWidth * dpr));
   var h = Math.max(1, Math.floor(canvas.clientHeight * dpr));
   if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
   }
}

function draw() {
   resizeCanvas();
   gl.viewport(0, 0, canvas.width, canvas.height);
   gl.clearColor(0.2, 0.2, 0.2, 1.0);
   gl.enable(gl.DEPTH_TEST);
   gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

   if (!texture || !prog) return;

   gl.useProgram(prog.program);

   var aspect = canvas.width / canvas.height;
   mat4Perspective(FOV_DEGREES, aspect, Z_NEAR, Z_FAR, _proj);
   mat4Translate(g.x, g.y, g.z, _trans);
   mat4RotY(g.yaw, _rotY);
   mat4RotX(g.pitch, _rotX);
   mat4Multiply(_rotY, _rotX, _rot);       // model = T * (RotY * RotX)
   mat4Multiply(_trans, _rot, _model);
   mat4Multiply(_proj, _model, _mvp);      // mvp = proj * model

   var u = prog.uniforms;
   if (u.mvp) gl.uniformMatrix4fv(u.mvp, false, _mvp);
   if (u.tex) gl.uniform1i(u.tex, 0);
   if (u.texSize) gl.uniform4f(u.texSize, texInfo.w, texInfo.h, texInfo.blockW, texInfo.blockH);
   if (u.maxLod) gl.uniform1f(u.maxLod, texInfo.levels - 1);
   if (u.const0) gl.uniform4f(u.const0, g.const0[0], g.const0[1], g.const0[2], g.const0[3]);
   if (u.const1) gl.uniform4f(u.const1, g.const1[0], g.const1[1], g.const1[2], g.const1[3]);

   gl.activeTexture(gl.TEXTURE0);
   gl.bindTexture(gl.TEXTURE_2D, texture);

   var mesh = g.cube ? cube : quad;
   gl.bindVertexArray(mesh.vao);
   gl.drawElements(gl.TRIANGLES, mesh.count, gl.UNSIGNED_SHORT, 0);
   gl.bindVertexArray(null);
}

function filterName(m) { return m === 0 ? 'POINT' : (m === 2 ? 'TRILINEAR' : 'BILINEAR'); }

function updateOverlay() {
   if (loading) {
      document.getElementById('info').textContent = 'Loading ' + currentUrl + ' …';
      return;
   }
   var c0 = g.const0.map(function (v) { return v ? 1 : 0; }).join('');
   var c1 = g.const1.map(function (v) { return v ? 1 : 0; }).join('');
   var l0 = 'Res: ' + texInfo.w + 'x' + texInfo.h + '   Mips: ' + texInfo.levels +
      '   Block: ' + texInfo.blockW + 'x' + texInfo.blockH +
      '   DeblockID: ' + texInfo.deblockId +
      '   Src: ' + texInfo.srcFmt + '   GPU: ' + texInfo.gpuFmt;
   var l1 = 'Mode: ' + (g.cube ? 'CUBE' : 'QUAD') + '   Filter: ' + filterName(g.filterMode) +
      '   Deblock(1) / Edges(2): [' + c0 + '][' + c1 + ']';
   var l2 = 'X:' + g.x.toFixed(1) + '  Y:' + g.y.toFixed(1) + '  Z:' + g.z.toFixed(1) +
      '  Yaw:' + g.yaw.toFixed(1) + '  Pitch:' + g.pitch.toFixed(1);
   document.getElementById('info').textContent = l0 + '\n' + l1 + '\n' + l2;
}

// ---------------------------------------------------------------------------
// Input.
// ---------------------------------------------------------------------------
// Discrete actions, shared by the keyboard and the on-screen buttons.
function toggleCube() { g.cube = !g.cube; }
function setFilter(m) { g.filterMode = m; applyFilterMode(); }
function cycleFilter() { setFilter((g.filterMode + 1) % 3); } // point -> bilinear -> trilinear
function toggleDeblock() { g.const0[0] = 1 - g.const0[0]; }
function toggleEdges() { g.const0[1] = 1 - g.const0[1]; }
function resetView() {
   g.x = 0; g.y = 0; g.z = -0.4; g.yaw = 0; g.pitch = 0;
   g.const0 = initConst0.slice();
   g.const1 = [0, 0, 0, 0];
}
function clampZ() {
   if (g.z < Z_MAX) g.z = Z_MAX;
   if (g.z > Z_MIN) g.z = Z_MIN;
}

function onKeyDown(e) {
   // Stop the page from scrolling on arrows/space.
   if (['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Space'].indexOf(e.code) >= 0)
      e.preventDefault();

   heldKeys[e.code] = true;

   if (e.repeat) return; // discrete actions fire once per physical press
   switch (e.code) {
      case 'KeyC': toggleCube(); break;
      case 'KeyB': setFilter(1); break;
      case 'KeyT': setFilter(2); break;
      case 'KeyP': setFilter(0); break;
      case 'Digit1': toggleDeblock(); break;
      case 'Digit2': toggleEdges(); break;
      case 'Digit3': g.const0[2] = 1 - g.const0[2]; break;
      case 'Digit4': g.const0[3] = 1 - g.const0[3]; break;
      case 'Digit5': g.const1[0] = 1 - g.const1[0]; break;
      case 'Digit6': g.const1[1] = 1 - g.const1[1]; break;
      case 'Digit7': g.const1[2] = 1 - g.const1[2]; break;
      case 'Digit8': g.const1[3] = 1 - g.const1[3]; break;
      case 'Space': resetView(); break;
   }
}
function onKeyUp(e) { heldKeys[e.code] = false; }

// ---------------------------------------------------------------------------
// Pointer / touch / wheel controls (mouse + touchscreen, no keyboard needed):
//   one-finger / left-drag  -> rotate (yaw + pitch)
//   two-finger pinch        -> zoom; two-finger drag -> pan
//   mouse wheel             -> zoom
// ---------------------------------------------------------------------------
var ROT_DRAG_MOUSE = 0.5;  // degrees of rotation per pixel (mouse)
var ROT_DRAG_TOUCH = 0.25; // gentler for touch — a finger flick covers more pixels
var PINCH_ZOOM = 0.012; // z units per pixel of pinch spread
var PAN_SPEED = 0.004;  // world units per pixel of two-finger drag
var WHEEL_ZOOM = 0.0025;

var pointers = {};      // active pointerId -> {x, y}
var pinchDist = 0;
var panMid = null;

function pointerCount() { return Object.keys(pointers).length; }

function onPointerDown(e) {
   pointers[e.pointerId] = { x: e.clientX, y: e.clientY };
   // Any change in finger count re-seeds the pinch/pan baseline on the next move, so
   // adding/removing a finger (1<->2<->3) never produces a one-frame zoom/pan jump.
   pinchDist = 0; panMid = null;
   // Capture so we still get pointermove/up even if the finger leaves the canvas;
   // otherwise a missed pointerup leaves a stale entry that breaks rotate forever.
   try { canvas.setPointerCapture(e.pointerId); } catch (err) { /* not fatal */ }
   e.preventDefault();
}
function onPointerMove(e) {
   var p = pointers[e.pointerId];
   if (!p) return;
   var nx = e.clientX, ny = e.clientY;
   var n = pointerCount();

   if (n === 1) {
      var rot = (e.pointerType === 'touch') ? ROT_DRAG_TOUCH : ROT_DRAG_MOUSE;
      g.yaw += (nx - p.x) * rot;
      g.pitch += (ny - p.y) * rot;
   }
   p.x = nx; p.y = ny;

   if (n >= 2) {
      var ids = Object.keys(pointers);
      var a = pointers[ids[0]], b = pointers[ids[1]];
      var dist = Math.hypot(a.x - b.x, a.y - b.y);
      var midx = (a.x + b.x) / 2, midy = (a.y + b.y) / 2;
      if (pinchDist > 0) {
         g.z += (dist - pinchDist) * PINCH_ZOOM;     // spread fingers -> move closer
         if (panMid) { g.x += (midx - panMid.x) * PAN_SPEED; g.y -= (midy - panMid.y) * PAN_SPEED; }
         clampZ();
      }
      pinchDist = dist;
      panMid = { x: midx, y: midy };
   }
   e.preventDefault();
}
function onPointerUp(e) {
   try { canvas.releasePointerCapture(e.pointerId); } catch (err) { /* ignore */ }
   delete pointers[e.pointerId];
   pinchDist = 0; panMid = null;   // re-seed pinch/pan baseline after any finger lifts
}
function onWheel(e) {
   g.z -= e.deltaY * WHEEL_ZOOM; // wheel up -> closer
   clampZ();
   e.preventDefault();
}

// Keep the on-screen buttons' labels/active state in sync with the model state
// (so keyboard and touch stay consistent).
function syncButtons() {
   // Compact labels so the 5 buttons fit one horizontal row on a phone. Deblock/Edges
   // on-state is shown by the green .active highlight (and also in the top-left overlay).
   var b;
   if ((b = document.getElementById('btnMode'))) b.textContent = g.cube ? 'Cube' : 'Quad';
   if ((b = document.getElementById('btnDeblock'))) {
      b.textContent = 'Deblock';
      b.classList.toggle('active', !!g.const0[0]);
   }
   if ((b = document.getElementById('btnEdges'))) {
      b.textContent = 'Edges';
      b.classList.toggle('active', !!g.const0[1]);
   }
   if ((b = document.getElementById('btnFilter'))) b.textContent = filterName(g.filterMode);
}

function processHeldKeys(dt) {
   if (heldKeys['ShiftLeft'] || heldKeys['ShiftRight']) dt *= 1.0 / 3.0;
   if (heldKeys['KeyW']) g.z += Z_SPEED * dt;
   if (heldKeys['KeyS']) g.z -= Z_SPEED * dt;
   if (heldKeys['ArrowLeft']) g.x += XY_SPEED * dt;
   if (heldKeys['ArrowRight']) g.x -= XY_SPEED * dt;
   if (heldKeys['ArrowUp']) g.y += XY_SPEED * dt;
   if (heldKeys['ArrowDown']) g.y -= XY_SPEED * dt;
   if (heldKeys['KeyA']) g.yaw += ROT_SPEED * dt;
   if (heldKeys['KeyD']) g.yaw -= ROT_SPEED * dt;
   if (heldKeys['KeyQ']) g.pitch += ROT_SPEED * dt;
   if (heldKeys['KeyE']) g.pitch -= ROT_SPEED * dt;
   if (g.z < Z_MAX) g.z = Z_MAX;
   if (g.z > Z_MIN) g.z = Z_MIN;
}

// ---------------------------------------------------------------------------
// Error overlay.
// ---------------------------------------------------------------------------
function showError(msg) {
   var e = document.getElementById('error');
   e.textContent = msg;
   e.style.display = 'block';
}
function hideError() { document.getElementById('error').style.display = 'none'; }

// ---------------------------------------------------------------------------
// Main loop + bootstrap.
// ---------------------------------------------------------------------------
function frame(now) {
   var dt = (now - lastTime) / 1000.0;
   lastTime = now;
   if (dt > 0.1) dt = 0.1; // cap after a stall to avoid a teleport
   processHeldKeys(dt);
   draw();
   updateOverlay();
   syncButtons();
   rafId = requestAnimationFrame(frame);
}

// (Re)create all GL objects. Called at startup and again after a context restore,
// where every prior GL handle has been invalidated.
function initGL() {
   detectCaps();
   prog = buildProgram(VERTEX_SHADER_SRC, FRAGMENT_SHADER_SRC);
   cube = createCube();
}

function start() {
   canvas = document.getElementById('canvas');
   // Drop MSAA on high-DPR screens (it adds little at DPR>=2 and doubles bandwidth on
   // tile-based mobile GPUs); the DPR cap in resizeCanvas keeps the buffer reasonable.
   var wantAA = (window.devicePixelRatio || 1) < 2;
   gl = canvas.getContext('webgl2', { alpha: false, depth: true, antialias: wantAA });
   if (!gl) {
      showError('WebGL 2 is required for this sample (#version 300 es shaders), but ' +
         'this browser/device did not provide a WebGL 2 context.');
      return;
   }

   initGL();

   // Mobile browsers drop the GL context when backgrounded or under memory pressure.
   // Without preventDefault() the context is never restored (permanently black canvas);
   // on restore every GL object is invalid and must be rebuilt.
   canvas.addEventListener('webglcontextlost', function (e) {
      e.preventDefault();
      if (rafId) { cancelAnimationFrame(rafId); rafId = 0; }
      loading = false;   // don't let a "Loading…" overlay stay wedged if loss hit mid-load
   }, false);
   canvas.addEventListener('webglcontextrestored', function () {
      // A throw in here (e.g. shader relink fails under the same memory pressure that
      // caused the loss) would otherwise never restart the loop -> permanent freeze
      // with no feedback. Catch it and tell the user.
      try {
         texture = null; quad = null;   // old handles are dead
         initGL();
         loadKtx2(currentUrl);
         lastTime = performance.now();
         rafId = requestAnimationFrame(frame);
      } catch (err) {
         showError('The GPU context was lost and could not be rebuilt — please reload the page.');
         console.error('context restore failed:', err);
      }
   }, false);

   // Show touch-only help on coarse-pointer devices (the keyboard line is noise on
   // a phone/tablet); show the full keyboard reference on mouse/desktop.
   var coarse = window.matchMedia && window.matchMedia('(pointer: coarse)').matches;
   document.getElementById('help').textContent = coarse
      ? 'Drag: rotate    Two-finger pinch: zoom / drag: pan    Buttons below'
      : 'Drag: rotate   Two-finger pinch: zoom / drag: pan   Wheel: zoom\n' +
        'Keys — Move: Arrows/W,S   Rotate: A,D / Q,E   Shift: slow   C B T P 1 2   Space: reset';

   document.getElementById('asset').addEventListener('change', function () {
      loadKtx2(this.value);
   });

   // On-screen buttons (so the sample is fully usable on phones/tablets).
   var bind = function (id, fn) {
      var el = document.getElementById(id);
      if (el) el.addEventListener('click', fn);
   };
   bind('btnMode', toggleCube);
   bind('btnDeblock', toggleDeblock);
   bind('btnEdges', toggleEdges);
   bind('btnFilter', cycleFilter);
   bind('btnReset', resetView);

   window.addEventListener('keydown', onKeyDown);
   window.addEventListener('keyup', onKeyUp);

   // Pointer + wheel controls on the canvas (mouse and touch).
   canvas.addEventListener('pointerdown', onPointerDown);
   canvas.addEventListener('pointermove', onPointerMove);
   canvas.addEventListener('pointerup', onPointerUp);
   canvas.addEventListener('pointercancel', onPointerUp);
   // No 'pointerleave' handler: with setPointerCapture the canvas keeps receiving
   // events when the finger/cursor leaves its bounds, so deleting the pointer on
   // leave would wrongly abort an in-progress drag.
   canvas.addEventListener('wheel', onWheel, { passive: false });
   // Suppress the long-press/right-click context menu so it can't interrupt a drag
   // (Android long-press, desktop right-drag).
   canvas.addEventListener('contextmenu', function (e) { e.preventDefault(); });

   loadKtx2(DEFAULT_ASSET);

   lastTime = performance.now();
   rafId = requestAnimationFrame(frame);
}

// Boot the Basis Universal WASM transcoder, then start (the BASIS() factory and
// initializeBasis() are the shared bootstrap used by the other webgl/ samples).
if (typeof BASIS === 'undefined') {
   // The transcoder <script> failed to load (blocked, 404, wrong path). Without
   // this guard the next line throws a ReferenceError before we can surface it.
   showError('Could not load the Basis transcoder script ' +
      '(../transcoder/build/basis_transcoder.js). Serve this page over HTTP from the ' +
      'webgl/ directory so the relative path resolves.');
} else {
   BASIS({
      onRuntimeInitialized: function () { }
   }).then(function (module) {
      Module = module;
      Module.initializeBasis();
      start();
   }).catch(function (e) {
      showError('Failed to initialize the Basis transcoder WASM module: ' + (e.message || e));
      console.error(e);
   });
}

#!/usr/bin/env python3
"""
Mipmap Compatible Texture Sampling Deblocking Shader Testbed 
Copyright (C) 2026 Binomial LLC. 
LICENSE: Apache 2.0

The fragment+vertex shader is always loaded from "shader.glsl" in the current dir.

Usage:
    python testbed.py file.ktx2
        Load a Basis Universal .ktx2: transcode to a GPU-compressed format
        (ASTC -> BC7 -> ETC2, whichever the GPU supports; falls back to
        uncompressed RGBA8 if none), upload all mips. Deblock filter block size
        and on/off come from the file's metadata.

    python testbed.py block_w block_h mip0.png [mip1.png ...]
        Load PNG mip levels as uncompressed RGBA8 (development/debugging).
        block_w, block_h: deblock filter block size in texels (e.g. 12 12).

Controls:
    Arrows      Move quad left/right/up/down
    W / S       Move closer / farther
    A / D       Rotate yaw (cube mode)
    Q / E       Rotate pitch (cube mode)
    C           Toggle cube / quad mode
    B           Bilinear filtering
    T           Trilinear filtering
    P           Point filtering
    R           Reload shader
	1			Toggle deblocking shader off/on
	2			Toggle edge visualization (only when deblocking active)
    3-4         Toggle shader const0.x/y/z/w (0 <-> 1)
    5-8         Toggle shader const1.x/y/z/w (0 <-> 1)
    Space       Reset to initial state
    Esc         Quit
"""

import sys, os, importlib.util
print("=== DIAG ===")
print("exe:", sys.executable)
print("ver:", sys.version)
print("cwd:", os.getcwd())
print("glfw spec:", importlib.util.find_spec("glfw"))
print("OpenGL spec:", importlib.util.find_spec("OpenGL"))
print("============")

import sys
import ctypes
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path

import glfw
from OpenGL.GL import *

# -----------------------------------------------------------------------------
# Basis Universal transcoder bindings
# -----------------------------------------------------------------------------
# Locate the basisu_py package by searching upward from this file for a directory
# that contains basisu_py/ (or python/basisu_py/). This lets the sample run from
# either python/shader_deblocking/ or another copy elsewhere in the repo.
def _add_basisu_to_path():
    d = os.path.dirname(os.path.abspath(__file__))
    while True:
        for cand in (d, os.path.join(d, "python")):
            if os.path.isdir(os.path.join(cand, "basisu_py")):
                if cand not in sys.path:
                    sys.path.insert(0, cand)
                return cand
        parent = os.path.dirname(d)
        if parent == d:        # reached filesystem root
            return None
        d = parent

_basisu_dir = _add_basisu_to_path()
try:
    from basisu_py import Transcoder
    from basisu_py.constants import TranscoderTextureFormat as TF, TranscodeDecodeFlags
except Exception as e:
    print(f"ERROR: Failed to import basisu_py transcoder bindings: {e}")
    print(f"       Searched upward from {os.path.dirname(os.path.abspath(__file__))}; "
          f"basisu_py found in: {_basisu_dir}")
    sys.exit(1)


def load_transcoder():
    """Create the Basis Universal transcoder (native .pyd preferred, WASM fallback)."""
    print("Loading Basis Universal transcoder...")
    try:
        t = Transcoder()
    except Exception as e:
        print(f"ERROR: Could not initialize Basis Universal transcoder: {e}")
        sys.exit(1)
    print(f"Transcoder loaded: backend={t.backend_name}  version={t.get_version()}")
    return t


# -----------------------------------------------------------------------------
# GPU compressed texture formats + selection ladder (ASTC -> BC7 -> ETC2)
# -----------------------------------------------------------------------------
# GL internal-format enums declared explicitly: PyOpenGL's core namespace does not
# expose the ASTC KHR enums on desktop, so we keep all three here for consistency.
GL_FMT_BC7        = 0x8E8C   # GL_COMPRESSED_RGBA_BPTC_UNORM  (BC7, 4x4 blocks)
GL_FMT_ETC2_RGBA  = 0x9278   # GL_COMPRESSED_RGBA8_ETC2_EAC   (ETC2 RGBA, 4x4 blocks)

# 14 ASTC LDR block sizes -> GL_COMPRESSED_RGBA_ASTC_<w>x<h>_KHR (0x93B0..0x93BD).
ASTC_GL_FORMATS = {
    (4, 4): 0x93B0,  (5, 4): 0x93B1,  (5, 5): 0x93B2,  (6, 5): 0x93B3,
    (6, 6): 0x93B4,  (8, 5): 0x93B5,  (8, 6): 0x93B6,  (8, 8): 0x93B7,
    (10, 5): 0x93B8, (10, 6): 0x93B9, (10, 8): 0x93BA, (10, 10): 0x93BB,
    (12, 10): 0x93BC, (12, 12): 0x93BD,
}

# Extension names that gate each capability, across desktop GL, native GLES, and
# WebGL, per the Khronos registries. We match on extension strings only (works for
# WebGL, where these formats are NEVER core and MUST be queried this way -- e.g.
# WebGL 2 does not guarantee ETC2 despite being GLES 3.0 based). WebGL reports
# names without the "GL_" prefix, so both forms are listed.
# ASTC LDR : desktop/GLES KHR, GLES OES, WebGL.
ASTC_EXTS = ("GL_KHR_texture_compression_astc_ldr",
             "GL_OES_texture_compression_astc",
             "WEBGL_compressed_texture_astc")
# BC7 (BPTC): desktop ARB (core in GL 4.2), GLES/WebGL 2 EXT.
BC7_EXTS  = ("GL_ARB_texture_compression_bptc",
             "GL_EXT_texture_compression_bptc",
             "EXT_texture_compression_bptc")
# ETC2/EAC : desktop via ARB_ES3_compatibility (core in GL 4.3); native GLES 3.0+
#            OES alias; WebGL extension (ETC2 is NOT core in WebGL 2).
ETC2_EXTS = ("GL_ARB_ES3_compatibility",
             "GL_OES_compressed_ETC2_RGBA8_texture",
             "WEBGL_compressed_texture_etc")


def query_gl_extensions():
    """Return the set of supported GL extension strings (core-profile safe)."""
    exts = set()
    n = glGetIntegerv(GL_NUM_EXTENSIONS)
    for i in range(int(n)):
        exts.add(glGetStringi(GL_EXTENSIONS, i).decode())
    return exts


def detect_compressed_caps(exts):
    """Which transcode targets the current GL context can accept."""
    return {
        'ASTC': any(e in exts for e in ASTC_EXTS),
        'BC7':  any(e in exts for e in BC7_EXTS),
        'ETC2': any(e in exts for e in ETC2_EXTS),
    }


def ktx2_format_name(t, h):
    """Short human-readable name of the KTX2's source (basis) format."""
    bw, bh = t.get_block_width(h), t.get_block_height(h)
    if t.is_etc1s(h):          return "ETC1S"
    if t.is_uastc_ldr_4x4(h):  return "UASTC LDR 4x4"
    if t.is_xuastc_ldr(h):     return f"XUASTC LDR {bw}x{bh}"
    if t.is_astc_ldr(h):       return f"ASTC LDR {bw}x{bh}"
    if t.is_xubc7(h):          return "XUBC7"
    if t.is_hdr_4x4(h):        return "UASTC HDR 4x4"
    if t.is_hdr_6x6(h):        return f"HDR 6x6 {bw}x{bh}"
    if t.is_hdr(h):            return "HDR"
    return f"basis_fmt {t.get_basis_tex_format(h)}"


def build_load_plan(caps, t, h):
    """
    Examine an open KTX2 handle and decide: (1) the GPU storage format via the
    ASTC -> BC7 -> ETC2 ladder (gated by GL caps AND transcoder support for this
    file), and (2) the in-shader deblocking decision.

    Returns a plan dict, or None if no target format is usable.

    Key rule: the deblock FILTER block size always comes from the file's native
    ASTC/XUASTC block size, independent of the GPU storage format's block size
    (e.g. BC7 stores 4x4 blocks but we still filter the original 12x12 lattice).
    """
    basis_fmt = t.get_basis_tex_format(h)
    file_bw, file_bh = t.get_block_width(h), t.get_block_height(h)

    def supported(tfmt):
        return bool(t.basis_is_format_supported(tfmt, basis_fmt))

    chosen = None
    if caps['ASTC']:
        astc_tf = t.basis_get_transcoder_texture_format_from_basis_tex_format(basis_fmt)
        gl_fmt = ASTC_GL_FORMATS.get((file_bw, file_bh))
        if gl_fmt is not None and supported(astc_tf):
            chosen = dict(family='ASTC', tfmt=astc_tf, gl_format=gl_fmt,
                          gpu_bw=file_bw, gpu_bh=file_bh, compressed=True)
    if chosen is None and caps['BC7'] and supported(TF.TF_BC7_RGBA):
        chosen = dict(family='BC7', tfmt=TF.TF_BC7_RGBA,
                      gl_format=GL_FMT_BC7, gpu_bw=4, gpu_bh=4, compressed=True)
    if chosen is None and caps['ETC2'] and supported(TF.TF_ETC2_RGBA):
        chosen = dict(family='ETC2', tfmt=TF.TF_ETC2_RGBA,
                      gl_format=GL_FMT_ETC2_RGBA, gpu_bw=4, gpu_bh=4, compressed=True)
    if chosen is None and supported(TF.TF_RGBA32):
        # No GPU-compressed format available: fall back to uncompressed RGBA8.
        chosen = dict(family='RGBA32', tfmt=TF.TF_RGBA32,
                      gl_format=GL_RGBA8, gpu_bw=1, gpu_bh=1, compressed=False)
    if chosen is None:
        return None

    # Deblocking decision is independent of the GPU storage format.
    deblock_id = t.get_deblocking_filter_index(h)
    chosen.update(
        filter_bw=file_bw, filter_bh=file_bh,
        deblock_id=deblock_id,
        deblock_enabled=(deblock_id == 1),
        levels=t.get_levels(h),
        base_w=t.get_width(h), base_h=t.get_height(h),
        basis_fmt=basis_fmt,
        format_name=ktx2_format_name(t, h),
    )
    return chosen


# -----------------------------------------------------------------------------
# Globals
# -----------------------------------------------------------------------------
WINDOW_WIDTH = 1280
WINDOW_HEIGHT = 720
FOV_DEGREES = 90.0
Z_MIN = .40
Z_MAX = -50.0
Z_SPEED = 1.0
XY_SPEED = .75
ROT_SPEED = 90.0  # degrees per second
# Block size (set from command line)
BLOCK_WIDTH = 12
BLOCK_HEIGHT = 12

g_state = {
    'x': 0.0,
    'y': 0.0,
    'z': -3.0,
    'yaw': 0.0,
    'pitch': 0.0,
    'mode': 'QUAD',  # 'QUAD' or 'CUBE'
    'filter_mode': 'TRILINEAR',
    'shader_path': None,
    'transcoder': None,
    'gl_caps': None,
    'tex_info': None,
    'program': None,
    'texture': None,
    'tex_size': (0, 0),
    'quad_vao': None,
    'cube_vao': None,
    'cube_index_count': 0,
    'debug_vao': None,
    'debug_texture': None,
    'debug_dirty': True,
    'last_time': 0.0,
    'const0': [0.0, 0.0, 0.0, 0.0],
    'const1': [0.0, 0.0, 0.0, 0.0],
}
INIT_X = 0.0
INIT_Y = 0.0
INIT_Z = -3.0
INIT_YAW = 0.0
INIT_PITCH = 0.0
INIT_CONST0 = [0.0, 0.0, 0.0, 0.0]
INIT_CONST1 = [0.0, 0.0, 0.0, 0.0]


# -----------------------------------------------------------------------------
# Shader Loading
# -----------------------------------------------------------------------------
def parse_shader_file(path):
    """Parse shader file with #vertex and #fragment markers."""
    text = Path(path).read_text()
    
    vertex_src = None
    fragment_src = None
    
    parts = text.split('#vertex')
    if len(parts) < 2:
        print(f"ERROR: No #vertex marker found in {path}")
        return None, None
    
    rest = parts[1]
    frag_parts = rest.split('#fragment')
    if len(frag_parts) < 2:
        print(f"ERROR: No #fragment marker found in {path}")
        return None, None
    
    vertex_src = frag_parts[0].strip()
    fragment_src = frag_parts[1].strip()
    
    return vertex_src, fragment_src


def compile_shader(source, shader_type):
    """Compile a shader, return handle or None on error."""
    shader = glCreateShader(shader_type)
    glShaderSource(shader, source)
    glCompileShader(shader)
    
    if glGetShaderiv(shader, GL_COMPILE_STATUS) != GL_TRUE:
        error = glGetShaderInfoLog(shader)
        if isinstance(error, bytes):
            error = error.decode('utf-8')
        type_name = "VERTEX" if shader_type == GL_VERTEX_SHADER else "FRAGMENT"
        print(f"{type_name} SHADER ERROR:\n{error}")
        glDeleteShader(shader)
        return None
    
    return shader


def link_program(vertex_shader, fragment_shader):
    """Link shaders into program, return handle or None on error."""
    program = glCreateProgram()
    glAttachShader(program, vertex_shader)
    glAttachShader(program, fragment_shader)
    glLinkProgram(program)
    
    if glGetProgramiv(program, GL_LINK_STATUS) != GL_TRUE:
        error = glGetProgramInfoLog(program)
        if isinstance(error, bytes):
            error = error.decode('utf-8')
        print(f"LINK ERROR:\n{error}")
        glDeleteProgram(program)
        return None
    
    return program


def load_shader(path):
    """Load, compile, and link shader from file. Returns program or None."""
    print(f"Loading shader: {path}")
    
    vertex_src, fragment_src = parse_shader_file(path)
    if vertex_src is None or fragment_src is None:
        return None
    
    vertex_shader = compile_shader(vertex_src, GL_VERTEX_SHADER)
    if vertex_shader is None:
        return None
    
    fragment_shader = compile_shader(fragment_src, GL_FRAGMENT_SHADER)
    if fragment_shader is None:
        glDeleteShader(vertex_shader)
        return None
    
    program = link_program(vertex_shader, fragment_shader)
    
    glDeleteShader(vertex_shader)
    glDeleteShader(fragment_shader)
    
    if program:
        print("Shader compiled successfully.")
    
    return program


def reload_shader():
    """Attempt to reload shader. Keep old one if failed."""
    new_program = load_shader(g_state['shader_path'])
    if new_program is not None:
        if g_state['program'] is not None:
            glDeleteProgram(g_state['program'])
        g_state['program'] = new_program
    else:
        print("Shader reload failed, keeping previous shader.")


# -----------------------------------------------------------------------------
# Texture Loading
# -----------------------------------------------------------------------------
def load_mipmap_texture(paths):
    """Load PNG files as uncompressed RGBA8 mipmap levels. Returns (texture, base_size)."""
    images = []

    for i, path in enumerate(paths):
        img = Image.open(path).convert('RGBA')
        images.append(img)
        print(f"Loaded mip {i}: {path} ({img.width}x{img.height})")

    # Validate dimensions (each level half the previous)
    for i in range(1, len(images)):
        expected_w = images[i - 1].width // 2
        expected_h = images[i - 1].height // 2
        actual_w = images[i].width
        actual_h = images[i].height

        if actual_w != expected_w or actual_h != expected_h:
            print(f"ERROR: Mip {i} should be {expected_w}x{expected_h}, got {actual_w}x{actual_h}")
            sys.exit(1)

    texture = glGenTextures(1)
    glBindTexture(GL_TEXTURE_2D, texture)

    for level, img in enumerate(images):
        data = np.array(img, dtype=np.uint8)
        glTexImage2D(
            GL_TEXTURE_2D, level, GL_RGBA8,
            img.width, img.height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, data
        )

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, len(images) - 1)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE)

    base_size = (images[0].width, images[0].height)
    return texture, base_size


def load_ktx2_texture(t, caps, path):
    """
    Open a KTX2 file, decide the GPU storage format via the ASTC->BC7->ETC2 ladder
    (falling back to uncompressed RGBA8 if the GPU supports none of them), transcode
    every mip level, and upload them as a GL texture.

    Each level is uploaded at its ORIGINAL (unpadded) width/height. For compressed
    formats the transcoded block buffer (which covers the padded block grid) is the
    data, and GL computes ceil(orig/blk) blocks to match len(blocks) exactly.

    Returns (texture_handle, (base_w, base_h), plan_dict). Exits on error.
    """
    print(f"Loading KTX2: {path}")
    try:
        data = open(path, "rb").read()
    except OSError as e:
        print(f"ERROR: Could not read KTX2 file '{path}': {e}")
        sys.exit(1)

    h = t.open(data)

    # This sample handles LDR textures only.
    if t.is_hdr(h):
        t.close(h)
        print(f"ERROR: '{path}' is an HDR texture; this sample supports LDR textures only.")
        sys.exit(1)

    plan = build_load_plan(caps, t, h)
    if plan is None:
        basis_fmt = t.get_basis_tex_format(h)
        t.close(h)
        print(f"ERROR: No usable transcode target for this file (basis_fmt={basis_fmt}). Exiting.")
        sys.exit(1)

    print(f"  Source     : basis_fmt={plan['basis_fmt']}  "
          f"{plan['base_w']}x{plan['base_h']}  levels={plan['levels']}")
    if plan['compressed']:
        print(f"  GPU format : {plan['family']}  gl_internal=0x{plan['gl_format']:04X}  "
              f"block={plan['gpu_bw']}x{plan['gpu_bh']}")
    else:
        print(f"  GPU format : {plan['family']} (uncompressed)  "
              f"gl_internal=0x{plan['gl_format']:04X}")
    print(f"  Deblock    : {'ON' if plan['deblock_enabled'] else 'off'}  "
          f"filter block={plan['filter_bw']}x{plan['filter_bh']}")

    texture = glGenTextures(1)
    glBindTexture(GL_TEXTURE_2D, texture)

    for lvl in range(plan['levels']):
        ow = t.get_level_orig_width(h, lvl)
        oh = t.get_level_orig_height(h, lvl)
        # Disable the transcoder's CPU deblocking: we deblock on the GPU in the
        # shader instead, so CPU deblocking here would double-filter the result.
        blocks = t.transcode_tfmt_handle(
            h, plan['tfmt'], level=lvl,
            decode_flags=TranscodeDecodeFlags.NO_DEBLOCK_FILTERING)
        data_arr = np.frombuffer(blocks, dtype=np.uint8)
        if plan['compressed']:
            # PyOpenGL derives imageSize from the array byte count; do NOT pass it
            # explicitly (doing so raises 'NumberHandler has no attribute arrayByteCount').
            glCompressedTexImage2D(GL_TEXTURE_2D, lvl, plan['gl_format'],
                                   ow, oh, 0, data_arr)
        else:
            glTexImage2D(GL_TEXTURE_2D, lvl, plan['gl_format'],
                         ow, oh, 0, GL_RGBA, GL_UNSIGNED_BYTE, data_arr)
        err = glGetError()
        if err != GL_NO_ERROR:
            t.close(h)
            print(f"ERROR: texture upload failed at level {lvl} "
                  f"({ow}x{oh}, {len(blocks)} bytes, fmt=0x{plan['gl_format']:04X}): "
                  f"GL error 0x{err:04X}")
            sys.exit(1)

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, plan['levels'] - 1)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE)

    t.close(h)
    print(f"  Uploaded {plan['levels']} mip level(s).")
    return texture, (plan['base_w'], plan['base_h']), plan


def set_filter_mode(mode):
    """Set texture filtering mode."""
    glBindTexture(GL_TEXTURE_2D, g_state['texture'])
    
    if mode == 'BILINEAR':
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
    elif mode == 'TRILINEAR':
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
    else:  # POINT
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST)
    
    g_state['filter_mode'] = mode
    g_state['debug_dirty'] = True


# -----------------------------------------------------------------------------
# Geometry
# -----------------------------------------------------------------------------
def create_quad(aspect_ratio):
    """Create a quad VAO centered at origin with given aspect ratio."""
    # Normalize so longest dimension is 1.0
    if aspect_ratio >= 1.0:
        half_w = 1.0
        half_h = 1.0 / aspect_ratio
    else:
        half_w = aspect_ratio
        half_h = 1.0
    
    # Position (x, y, z) + UV (u, v)
    vertices = np.array([
        -half_w, -half_h, 0.0,  0.0, 1.0,
         half_w, -half_h, 0.0,  1.0, 1.0,
         half_w,  half_h, 0.0,  1.0, 0.0,
        -half_w,  half_h, 0.0,  0.0, 0.0,
    ], dtype=np.float32)
    
    indices = np.array([0, 1, 2, 0, 2, 3], dtype=np.uint32)
    
    vao = glGenVertexArrays(1)
    vbo = glGenBuffers(1)
    ebo = glGenBuffers(1)
    
    glBindVertexArray(vao)
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo)
    glBufferData(GL_ARRAY_BUFFER, vertices.nbytes, vertices, GL_STATIC_DRAW)
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo)
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.nbytes, indices, GL_STATIC_DRAW)
    
    # Position attribute (location 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, ctypes.c_void_p(0))
    glEnableVertexAttribArray(0)
    
    # UV attribute (location 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, ctypes.c_void_p(12))
    glEnableVertexAttribArray(1)
    
    glBindVertexArray(0)
    
    return vao


def create_cube(size=1.0):
    """Create a textured cube VAO centered at origin."""
    h = size / 2.0
    
    # Each face: 4 vertices with position (x,y,z) + UV (u,v)
    # Front face (z = +h)
    front = [
        -h, -h,  h,  0.0, 1.0,
         h, -h,  h,  1.0, 1.0,
         h,  h,  h,  1.0, 0.0,
        -h,  h,  h,  0.0, 0.0,
    ]
    # Back face (z = -h)
    back = [
         h, -h, -h,  0.0, 1.0,
        -h, -h, -h,  1.0, 1.0,
        -h,  h, -h,  1.0, 0.0,
         h,  h, -h,  0.0, 0.0,
    ]
    # Right face (x = +h)
    right = [
         h, -h,  h,  0.0, 1.0,
         h, -h, -h,  1.0, 1.0,
         h,  h, -h,  1.0, 0.0,
         h,  h,  h,  0.0, 0.0,
    ]
    # Left face (x = -h)
    left = [
        -h, -h, -h,  0.0, 1.0,
        -h, -h,  h,  1.0, 1.0,
        -h,  h,  h,  1.0, 0.0,
        -h,  h, -h,  0.0, 0.0,
    ]
    # Top face (y = +h)
    top = [
        -h,  h,  h,  0.0, 1.0,
         h,  h,  h,  1.0, 1.0,
         h,  h, -h,  1.0, 0.0,
        -h,  h, -h,  0.0, 0.0,
    ]
    # Bottom face (y = -h)
    bottom = [
        -h, -h, -h,  0.0, 1.0,
         h, -h, -h,  1.0, 1.0,
         h, -h,  h,  1.0, 0.0,
        -h, -h,  h,  0.0, 0.0,
    ]
    
    vertices = np.array(front + back + right + left + top + bottom, dtype=np.float32)
    
    # 6 faces, each with 2 triangles (6 indices per face)
    indices = []
    for i in range(6):
        base = i * 4
        indices.extend([base, base+1, base+2, base, base+2, base+3])
    indices = np.array(indices, dtype=np.uint32)
    
    vao = glGenVertexArrays(1)
    vbo = glGenBuffers(1)
    ebo = glGenBuffers(1)
    
    glBindVertexArray(vao)
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo)
    glBufferData(GL_ARRAY_BUFFER, vertices.nbytes, vertices, GL_STATIC_DRAW)
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo)
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.nbytes, indices, GL_STATIC_DRAW)
    
    # Position attribute (location 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, ctypes.c_void_p(0))
    glEnableVertexAttribArray(0)
    
    # UV attribute (location 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, ctypes.c_void_p(12))
    glEnableVertexAttribArray(1)
    
    glBindVertexArray(0)
    
    return vao, len(indices)


def create_debug_quad():
    """Create a screen-space quad for debug text overlay."""
    # Screen-space quad at top-left
    # NDC: x=-1 is left, y=1 is top
    w = 680.0 / WINDOW_WIDTH * 2.0
    h = 80.0 / WINDOW_HEIGHT * 2.0
    
    vertices = np.array([
        -1.0,     1.0,      0.0, 0.0,
        -1.0 + w, 1.0,      1.0, 0.0,
        -1.0 + w, 1.0 - h,  1.0, 1.0,
        -1.0,     1.0 - h,  0.0, 1.0,
    ], dtype=np.float32)
    
    indices = np.array([0, 1, 2, 0, 2, 3], dtype=np.uint32)
    
    vao = glGenVertexArrays(1)
    vbo = glGenBuffers(1)
    ebo = glGenBuffers(1)
    
    glBindVertexArray(vao)
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo)
    glBufferData(GL_ARRAY_BUFFER, vertices.nbytes, vertices, GL_STATIC_DRAW)
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo)
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.nbytes, indices, GL_STATIC_DRAW)
    
    # Position attribute (location 0) - xy only
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, ctypes.c_void_p(0))
    glEnableVertexAttribArray(0)
    
    # UV attribute (location 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, ctypes.c_void_p(8))
    glEnableVertexAttribArray(1)
    
    glBindVertexArray(0)
    
    return vao


# -----------------------------------------------------------------------------
# Debug Text
# -----------------------------------------------------------------------------
DEBUG_VERTEX = """
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
"""

DEBUG_FRAGMENT = """
#version 330 core
uniform sampler2D tex;
in vec2 vUV;
out vec4 fragColor;
void main() {
    fragColor = texture(tex, vUV);
}
"""

g_debug_program = None


def init_debug_rendering():
    """Initialize debug text rendering resources."""
    global g_debug_program
    
    vs = compile_shader(DEBUG_VERTEX, GL_VERTEX_SHADER)
    fs = compile_shader(DEBUG_FRAGMENT, GL_FRAGMENT_SHADER)
    
    if vs is None or fs is None:
        print("ERROR: Failed to compile debug shaders")
        if vs:
            glDeleteShader(vs)
        if fs:
            glDeleteShader(fs)
        return
    
    g_debug_program = link_program(vs, fs)
    glDeleteShader(vs)
    glDeleteShader(fs)
    
    if g_debug_program is None:
        print("ERROR: Failed to link debug program")
        return
    
    g_state['debug_vao'] = create_debug_quad()
    g_state['debug_texture'] = glGenTextures(1)


def update_debug_text():
    """Render debug text to texture."""
    if not g_state['debug_dirty']:
        return
    
    c0 = g_state['const0']
    c1 = g_state['const1']
    
    # First line: source texture metadata.
    ti = g_state['tex_info']
    if ti:
        deblk = "-" if ti['deblock_id'] is None else str(ti['deblock_id'])
        line_info = (f"Res:{ti['orig_w']}x{ti['orig_h']}  Mips:{ti['mips']}  "
                     f"Block:{ti['block_w']}x{ti['block_h']}  DeblockID:{deblk}  "
                     f"Fmt:{ti['format']}")
    else:
        line_info = "(no texture loaded)"

    # Build status lines
    lines = [
        line_info,
        f"Mode:{g_state['mode']:4s} Filter:{g_state['filter_mode']:9s} Deblock:[{int(c0[0])}{int(c0[1])}{int(c0[2])}{int(c0[3])}][{int(c1[0])}{int(c1[1])}{int(c1[2])}{int(c1[3])}]",
        f"X:{g_state['x']:+5.1f} Y:{g_state['y']:+5.1f} Z:{g_state['z']:5.1f} Yaw:{g_state['yaw']:+6.1f} Pitch:{g_state['pitch']:+6.1f}",
        "Arrows:move, W/S:zoom, A/D:yaw, Q/E:pitch, C:cube, B/T/P:filter, 1=deblock, 2=edge vis, R:reload, Space:reset",
    ]

    img = Image.new('RGBA', (680, 80), (0, 0, 0, 180))
    draw = ImageDraw.Draw(img)
    
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 14)
    except:
        font = ImageFont.load_default()
    
    y = 4
    for line in lines:
        draw.text((6, y), line, fill=(255, 255, 255, 255), font=font)
        y += 18
    
    data = np.array(img, dtype=np.uint8)
    
    glBindTexture(GL_TEXTURE_2D, g_state['debug_texture'])
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
    
    g_state['debug_dirty'] = False


def draw_debug_text():
    """Draw debug text overlay."""
    if g_debug_program is None:
        return
    
    update_debug_text()
    
    glEnable(GL_BLEND)
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
    glDisable(GL_DEPTH_TEST)
    
    glUseProgram(g_debug_program)
    glBindTexture(GL_TEXTURE_2D, g_state['debug_texture'])
    glBindVertexArray(g_state['debug_vao'])
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, None)
    
    glEnable(GL_DEPTH_TEST)
    glDisable(GL_BLEND)


# -----------------------------------------------------------------------------
# Math
# -----------------------------------------------------------------------------
def perspective_matrix(fov_deg, aspect, near, far):
    """Create perspective projection matrix."""
    fov_rad = np.radians(fov_deg)
    f = 1.0 / np.tan(fov_rad / 2.0)
    
    m = np.zeros((4, 4), dtype=np.float32)
    m[0, 0] = f / aspect
    m[1, 1] = f
    m[2, 2] = (far + near) / (near - far)
    m[2, 3] = (2 * far * near) / (near - far)
    m[3, 2] = -1.0
    
    return m


def translation_matrix(x, y, z):
    """Create translation matrix."""
    m = np.eye(4, dtype=np.float32)
    m[0, 3] = x
    m[1, 3] = y
    m[2, 3] = z
    return m


def rotation_matrix_y(deg):
    """Create rotation matrix around Y axis (yaw)."""
    rad = np.radians(deg)
    c, s = np.cos(rad), np.sin(rad)
    m = np.eye(4, dtype=np.float32)
    m[0, 0] = c
    m[0, 2] = s
    m[2, 0] = -s
    m[2, 2] = c
    return m


def rotation_matrix_x(deg):
    """Create rotation matrix around X axis (pitch)."""
    rad = np.radians(deg)
    c, s = np.cos(rad), np.sin(rad)
    m = np.eye(4, dtype=np.float32)
    m[1, 1] = c
    m[1, 2] = -s
    m[2, 1] = s
    m[2, 2] = c
    return m


# -----------------------------------------------------------------------------
# Input
# -----------------------------------------------------------------------------
def framebuffer_size_callback(window, width, height):
    """Handle window resize."""
    global WINDOW_WIDTH, WINDOW_HEIGHT
    WINDOW_WIDTH = width
    WINDOW_HEIGHT = height
    glViewport(0, 0, width, height)
    g_state['debug_dirty'] = True


def key_callback(window, key, scancode, action, mods):
    if action == glfw.PRESS:
        if key == glfw.KEY_ESCAPE:
            glfw.set_window_should_close(window, True)
        elif key == glfw.KEY_R:
            reload_shader()
        elif key == glfw.KEY_B:
            set_filter_mode('BILINEAR')
            print("Filter: BILINEAR")
        elif key == glfw.KEY_P:
            set_filter_mode('POINT')
            print("Filter: POINT")
        elif key == glfw.KEY_T:
            set_filter_mode('TRILINEAR')
            print("Filter: TRILINEAR")
        # Toggle const0 components (keys 1-4)
        elif key == glfw.KEY_1:
            g_state['const0'][0] = 1.0 - g_state['const0'][0]
            print(f"const0: {g_state['const0']}")
            g_state['debug_dirty'] = True
        elif key == glfw.KEY_2:
            g_state['const0'][1] = 1.0 - g_state['const0'][1]
            print(f"const0: {g_state['const0']}")
            g_state['debug_dirty'] = True
        elif key == glfw.KEY_3:
            g_state['const0'][2] = 1.0 - g_state['const0'][2]
            print(f"const0: {g_state['const0']}")
            g_state['debug_dirty'] = True
        elif key == glfw.KEY_4:
            g_state['const0'][3] = 1.0 - g_state['const0'][3]
            print(f"const0: {g_state['const0']}")
            g_state['debug_dirty'] = True
        # Toggle const1 components (keys 5-8)
        elif key == glfw.KEY_5:
            g_state['const1'][0] = 1.0 - g_state['const1'][0]
            print(f"const1: {g_state['const1']}")
            g_state['debug_dirty'] = True
        elif key == glfw.KEY_6:
            g_state['const1'][1] = 1.0 - g_state['const1'][1]
            print(f"const1: {g_state['const1']}")
            g_state['debug_dirty'] = True
        elif key == glfw.KEY_7:
            g_state['const1'][2] = 1.0 - g_state['const1'][2]
            print(f"const1: {g_state['const1']}")
            g_state['debug_dirty'] = True
        elif key == glfw.KEY_8:
            g_state['const1'][3] = 1.0 - g_state['const1'][3]
            print(f"const1: {g_state['const1']}")
            g_state['debug_dirty'] = True
        elif key == glfw.KEY_C:
            g_state['mode'] = 'CUBE' if g_state['mode'] == 'QUAD' else 'QUAD'
            print(f"Mode: {g_state['mode']}")
            g_state['debug_dirty'] = True
        elif key == glfw.KEY_SPACE:
            g_state['x'] = INIT_X
            g_state['y'] = INIT_Y
            g_state['z'] = INIT_Z
            g_state['yaw'] = INIT_YAW
            g_state['pitch'] = INIT_PITCH
            g_state['const0'] = INIT_CONST0.copy()
            g_state['const1'] = INIT_CONST1.copy()
            g_state['debug_dirty'] = True
            print("Reset to initial state")


def process_held_keys(window, dt):
    """Process continuously held keys."""
    moved = False
    
    if glfw.get_key(window, glfw.KEY_LEFT_SHIFT) == glfw.PRESS or \
       glfw.get_key(window, glfw.KEY_RIGHT_SHIFT) == glfw.PRESS:
        dt *= 1.0 / 3.0
    if glfw.get_key(window, glfw.KEY_W) == glfw.PRESS:
        g_state['z'] += Z_SPEED * dt
        moved = True
    
    if glfw.get_key(window, glfw.KEY_S) == glfw.PRESS:
        g_state['z'] -= Z_SPEED * dt
        moved = True
    if glfw.get_key(window, glfw.KEY_LEFT) == glfw.PRESS:
        g_state['x'] += XY_SPEED * dt
        moved = True
    if glfw.get_key(window, glfw.KEY_RIGHT) == glfw.PRESS:
        g_state['x'] -= XY_SPEED * dt
        moved = True
    if glfw.get_key(window, glfw.KEY_UP) == glfw.PRESS:
        g_state['y'] += XY_SPEED * dt
        moved = True
    if glfw.get_key(window, glfw.KEY_DOWN) == glfw.PRESS:
        g_state['y'] -= XY_SPEED * dt
        moved = True
    
    # Rotation (A/D for yaw, Q/E for pitch)
    if glfw.get_key(window, glfw.KEY_A) == glfw.PRESS:
        g_state['yaw'] += ROT_SPEED * dt
        moved = True
    if glfw.get_key(window, glfw.KEY_D) == glfw.PRESS:
        g_state['yaw'] -= ROT_SPEED * dt
        moved = True
    if glfw.get_key(window, glfw.KEY_Q) == glfw.PRESS:
        g_state['pitch'] += ROT_SPEED * dt
        moved = True
    if glfw.get_key(window, glfw.KEY_E) == glfw.PRESS:
        g_state['pitch'] -= ROT_SPEED * dt
        moved = True
    
    # Clamp Z
    g_state['z'] = max(Z_MAX, min(Z_MIN, g_state['z']))
    
    if moved:
        g_state['debug_dirty'] = True


# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------
def main():
    global BLOCK_WIDTH, BLOCK_HEIGHT

    # Load the Basis Universal transcoder FIRST, before any GL/window setup.
    g_state['transcoder'] = load_transcoder()

    if len(sys.argv) < 2:
        print(__doc__)
        print("ERROR: Need either a .ktx2 file or PNG mip level(s)")
        print("    python testbed.py file.ktx2")
        print("    python testbed.py 12 12 mip0.png [mip1.png ...]")
        sys.exit(1)

    # The shader is always loaded from shader.glsl in the current directory.
    shader_path = "shader.glsl"
    g_state['shader_path'] = shader_path

    # Dispatch on the first argument: a single .ktx2, otherwise PNG mode.
    is_ktx2 = sys.argv[1].lower().endswith(".ktx2")
    ktx2_path = None
    mip_paths = None
    if is_ktx2:
        if len(sys.argv) != 2:
            print(__doc__)
            print("ERROR: KTX2 mode takes exactly one file: testbed.py file.ktx2")
            sys.exit(1)
        ktx2_path = sys.argv[1]
        print(f"KTX2 mode: {ktx2_path}")
    else:
        # PNG mode: block_w block_h mip0.png [mip1.png ...]
        if len(sys.argv) < 4:
            print(__doc__)
            print("ERROR: PNG mode needs block_w block_h and at least one PNG")
            print("Example: python testbed.py 12 12 mip0.png mip1.png")
            sys.exit(1)
        try:
            BLOCK_WIDTH = int(sys.argv[1])
            BLOCK_HEIGHT = int(sys.argv[2])
        except ValueError:
            print(f"ERROR: block_w and block_h must be integers, got '{sys.argv[1]}' '{sys.argv[2]}'")
            sys.exit(1)
        if BLOCK_WIDTH < 1 or BLOCK_HEIGHT < 1:
            print(f"ERROR: block size must be positive, got {BLOCK_WIDTH}x{BLOCK_HEIGHT}")
            sys.exit(1)
        mip_paths = sys.argv[3:]
        print(f"PNG mode: block size {BLOCK_WIDTH}x{BLOCK_HEIGHT}, {len(mip_paths)} level(s)")
    
    # Init GLFW
    if not glfw.init():
        print("ERROR: Failed to initialize GLFW")
        sys.exit(1)
    
    glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
    glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
    glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
    glfw.window_hint(glfw.OPENGL_FORWARD_COMPAT, glfw.TRUE)  # required for a core context on macOS
    glfw.window_hint(glfw.RESIZABLE, glfw.TRUE)
    glfw.window_hint(glfw.FOCUSED, glfw.TRUE)
    glfw.window_hint(glfw.FOCUS_ON_SHOW, glfw.TRUE)
    
    window = glfw.create_window(WINDOW_WIDTH, WINDOW_HEIGHT, "Deblock Shader Testbed", None, None)
    if not window:
        glfw.terminate()
        print("ERROR: Failed to create window")
        sys.exit(1)
    
    glfw.make_context_current(window)
    glfw.set_key_callback(window, key_callback)
    glfw.set_framebuffer_size_callback(window, framebuffer_size_callback)
    glfw.swap_interval(1)  # VSync
    glfw.focus_window(window)
    
    print(f"OpenGL: {glGetString(GL_VERSION).decode()}")

    # Detect which compressed GPU formats this context accepts (ASTC -> BC7 -> ETC2).
    g_state['gl_caps'] = detect_compressed_caps(query_gl_extensions())
    caps = g_state['gl_caps']
    print(f"GPU compressed format support:  ASTC={caps['ASTC']}  BC7={caps['BC7']}  ETC2={caps['ETC2']}")
    if is_ktx2 and not any(caps.values()):
        print("Note: no GPU-compressed format (ASTC/BC7/ETC2) available; the KTX2 "
              "will be transcoded to uncompressed RGBA8.")

    # Load shader (exit on failure at startup)
    g_state['program'] = load_shader(shader_path)
    if g_state['program'] is None:
        glfw.terminate()
        sys.exit(1)
    
    # Load the texture: KTX2 (transcode -> compressed) or PNG (uncompressed RGBA8).
    if is_ktx2:
        g_state['texture'], g_state['tex_size'], plan = load_ktx2_texture(
            g_state['transcoder'], caps, ktx2_path)
        g_state['mip_count'] = plan['levels']

        # Deblock filter block size feeds texSize.zw; default the shader's deblock
        # toggle (const0.x) from the file's DeblockFilterID. The '1' key still toggles it.
        BLOCK_WIDTH, BLOCK_HEIGHT = plan['filter_bw'], plan['filter_bh']
        deblock_default = 1.0 if plan['deblock_enabled'] else 0.0
        g_state['const0'][0] = deblock_default
        INIT_CONST0[0] = deblock_default
        g_state['tex_info'] = {
            'orig_w': plan['base_w'], 'orig_h': plan['base_h'],
            'mips': plan['levels'],
            'block_w': plan['filter_bw'], 'block_h': plan['filter_bh'],
            'deblock_id': plan['deblock_id'],
            'format': plan['format_name'],
        }
    else:
        # PNG mode: BLOCK_WIDTH/HEIGHT came from the CLI; deblock defaults off
        # (press '1' to enable), matching the original sample's behavior.
        g_state['texture'], g_state['tex_size'] = load_mipmap_texture(mip_paths)
        g_state['mip_count'] = len(mip_paths)
        g_state['tex_info'] = {
            'orig_w': g_state['tex_size'][0], 'orig_h': g_state['tex_size'][1],
            'mips': len(mip_paths),
            'block_w': BLOCK_WIDTH, 'block_h': BLOCK_HEIGHT,
            'deblock_id': None,
            'format': 'PNG RGBA8',
        }

    set_filter_mode('TRILINEAR')

    # Create quad
    aspect = g_state['tex_size'][0] / g_state['tex_size'][1]
    g_state['quad_vao'] = create_quad(aspect)
    
    # Create cube
    g_state['cube_vao'], g_state['cube_index_count'] = create_cube(1.0)
    
    # Init debug rendering
    init_debug_rendering()
    
    glEnable(GL_DEPTH_TEST)
    glClearColor(0.2, 0.2, 0.2, 1.0)
    
    g_state['last_time'] = glfw.get_time()
    
    # Main loop
    while not glfw.window_should_close(window):
        # Delta time
        now = glfw.get_time()
        dt = now - g_state['last_time']
        g_state['last_time'] = now
        
        # Input
        glfw.poll_events()
        process_held_keys(window, dt)
        
        # Projection matrix (recalculate for resize)
        proj = perspective_matrix(FOV_DEGREES, WINDOW_WIDTH / WINDOW_HEIGHT, 0.001, 100.0)
        
        # Clear
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
        
        # Draw quad or cube
        glUseProgram(g_state['program'])
        
        # MVP (include rotation for cube mode)
        trans = translation_matrix(g_state['x'], g_state['y'], g_state['z'])
        rot_y = rotation_matrix_y(g_state['yaw'])
        rot_x = rotation_matrix_x(g_state['pitch'])
        model = trans @ rot_y @ rot_x
        mvp = proj @ model
        
        loc = glGetUniformLocation(g_state['program'], "mvp")
        if loc >= 0:
            glUniformMatrix4fv(loc, 1, GL_TRUE, mvp)
        
        loc = glGetUniformLocation(g_state['program'], "tex")
        if loc >= 0:
            glUniform1i(loc, 0)
        
        loc = glGetUniformLocation(g_state['program'], "texSize")
        if loc >= 0:
            glUniform4f(loc, float(g_state['tex_size'][0]), float(g_state['tex_size'][1]), BLOCK_WIDTH, BLOCK_HEIGHT);
        
        loc = glGetUniformLocation(g_state['program'], "maxLod")
        if loc >= 0:
            glUniform1f(loc, float(g_state['mip_count'] - 1))
        loc = glGetUniformLocation(g_state['program'], "const0")
        if loc >= 0:
            c = g_state['const0']
            glUniform4f(loc, c[0], c[1], c[2], c[3])
        
        loc = glGetUniformLocation(g_state['program'], "const1")
        if loc >= 0:
            c = g_state['const1']
            glUniform4f(loc, c[0], c[1], c[2], c[3])
        
        glActiveTexture(GL_TEXTURE0)
        glBindTexture(GL_TEXTURE_2D, g_state['texture'])
        
        if g_state['mode'] == 'CUBE':
            glBindVertexArray(g_state['cube_vao'])
            glDrawElements(GL_TRIANGLES, g_state['cube_index_count'], GL_UNSIGNED_INT, None)
        else:
            glBindVertexArray(g_state['quad_vao'])
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, None)
        
        # Draw debug overlay
        draw_debug_text()
        
        glfw.swap_buffers(window)
    
    glfw.terminate()
    print("Done.")


if __name__ == "__main__":
    main()

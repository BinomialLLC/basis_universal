#!/usr/bin/env python3
"""
Mipmap Deblocking Shader Testbed 
Copyright (C) 2026 Binomial LLC. 
LICENSE: Apache 2.0

Usage:
    python testbed.py shader.glsl block_w block_h mip0.png mip1.png [mip2.png ...]
    block_w, block_h: Block size in texels (e.g. 8 8 for 8x8 DCT blocks)

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
# Globals
# -----------------------------------------------------------------------------
WINDOW_WIDTH = 1280
WINDOW_HEIGHT = 720
FOV_DEGREES = 90.0
Z_MIN = .40
Z_MAX = -50.0
Z_SPEED = 2.0
XY_SPEED = 1.5
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
    'filter_mode': 'BILINEAR',
    'shader_path': None,
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
    """Load PNG files as mipmap levels. Returns texture handle and base size."""
    images = []
    
    for i, path in enumerate(paths):
        img = Image.open(path).convert('RGBA')
        images.append(img)
        print(f"Loaded mip {i}: {path} ({img.width}x{img.height})")
    
    # Validate dimensions
    for i in range(1, len(images)):
        expected_w = images[i - 1].width // 2
        expected_h = images[i - 1].height // 2
        actual_w = images[i].width
        actual_h = images[i].height
        
        if actual_w != expected_w or actual_h != expected_h:
            print(f"ERROR: Mip {i} should be {expected_w}x{expected_h}, got {actual_w}x{actual_h}")
            sys.exit(1)
    
    # Create texture
    texture = glGenTextures(1)
    glBindTexture(GL_TEXTURE_2D, texture)
    
    # Upload each mip level
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
    h = 60.0 / WINDOW_HEIGHT * 2.0
    
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
    
    # Build status lines
    lines = [
        f"Mode:{g_state['mode']:4s} Filter:{g_state['filter_mode']:9s} Block:{BLOCK_WIDTH}x{BLOCK_HEIGHT} Deblock: [{int(c0[0])}{int(c0[1])}{int(c0[2])}{int(c0[3])}][{int(c1[0])}{int(c1[1])}{int(c1[2])}{int(c1[3])}]",
        f"X:{g_state['x']:+5.1f} Y:{g_state['y']:+5.1f} Z:{g_state['z']:5.1f} Yaw:{g_state['yaw']:+6.1f} Pitch:{g_state['pitch']:+6.1f}",
        "Arrows:move, W/S:zoom, A/D:yaw, Q/E:pitch, C:cube, B/T/P:filter, 1=deblocking toggle, 2=edge vis, R:reload, Space:reset",
    ]
    
    img = Image.new('RGBA', (680, 60), (0, 0, 0, 180))
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
    if len(sys.argv) < 5:
        print(__doc__)
        print("ERROR: Need shader, block_w, block_h, and at least one mipmap PNG")
        print("Example: python testbed.py shader.glsl 8 8 mip0.png mip1.png")
        sys.exit(1)
    
    shader_path = sys.argv[1]
    try:
        BLOCK_WIDTH = int(sys.argv[2])
        BLOCK_HEIGHT = int(sys.argv[3])
    except ValueError:
        print(f"ERROR: block_w and block_h must be integers, got '{sys.argv[2]}' '{sys.argv[3]}'")
        sys.exit(1)
    if BLOCK_WIDTH < 1 or BLOCK_HEIGHT < 1:
        print(f"ERROR: block size must be positive, got {BLOCK_WIDTH}x{BLOCK_HEIGHT}")
        sys.exit(1)
    mip_paths = sys.argv[4:]
    print(f"Block size: {BLOCK_WIDTH}x{BLOCK_HEIGHT}")
    
    g_state['shader_path'] = shader_path
    
    # Init GLFW
    if not glfw.init():
        print("ERROR: Failed to initialize GLFW")
        sys.exit(1)
    
    glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
    glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
    glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
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
    
    # Load shader (exit on failure at startup)
    g_state['program'] = load_shader(shader_path)
    if g_state['program'] is None:
        glfw.terminate()
        sys.exit(1)
    
    # Load texture
    g_state['texture'], g_state['tex_size'] = load_mipmap_texture(mip_paths)
    set_filter_mode('BILINEAR')
    
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

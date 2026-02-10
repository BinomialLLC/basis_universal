# Python+GLSL Shader Deblocking Sample

This sample demonstrates how to use a simple pixel shader to greatly reduce
ASTC block artifacts, which can be quite noticeable when the block size goes
beyond roughly 6x6. The basic idea: instead of always sampling the texture using 
1 tap, you sample the texture either 1 time or X times (with a simple low pass filter) 
depending on whether or not the sample location is near a block edge. 

The shader determines if it's going to sample near an edge,
and if so it samples a small vertical and horizontal region around the center sample
and applies a small low pass filter. The example shader is compatible with
mipmapping, bilinear filtering, trilinear filtering etc. 

It was written to be as simple as possible. It's also possible to add adaptivity 
to this shader, so it doesn't blindly blur across sharp edges - like we do while 
CPU deblocking before transcoding to BC7 or other LDR texture formats. It's also possible
to add deblocking filter awareness to our ASTC/XUASTC/etc. encoders.

You'll need these Python dependencies to run it:
```
pip install numpy Pillow glfw PyOpenGL
```

See `run.bat` for the command line on how to run the sample. Or run:

```
py -3.12 testbed.py shader.glsl 12 12 flower_unpacked_rgb_ASTC_LDR_12X12_RGBA_level_0_face_0_layer_0000.png flower_unpacked_rgb_ASTC_LDR_12X12_RGBA_level_1_face_0_layer_0000.png flower_unpacked_rgb_ASTC_LDR_12X12_RGBA_level_2_face_0_layer_0000.png flower_unpacked_rgb_ASTC_LDR_12X12_RGBA_level_3_face_0_layer_0000.png flower_unpacked_rgb_ASTC_LDR_12X12_RGBA_level_4_face_0_layer_0000.png
```

Depending on your setup you may need to use 'python' or 'python3' etc.

The shader can be easily simplified to sample the texture less by using less taps. The current shader uses a total of 9 taps, but 5 are possible.

Many variations and optimizations of this basic idea are possible.

---

## Screenshots - ASTC 12x12 Block Size

**Disabled:**
![Screenshot 1: Off](screenshots/1_off.png)

**Enabled:**
![Screenshot 1: On](screenshots/1_on.png)

---

**Disabled:**
![Screenshot 2: Off](screenshots/2_off.png)

**Enabled:**
![Screenshot 2: On](screenshots/2_on.png)

---

## Usage and Controls

The sample either renders a single textured quad or a cube. Press 'C' to toggle between the quad and the cube. The '1' key toggles shader deblocking (off by default). The '2' key enables edge visualization, which only works when deblocking is enabled. 

Other keys can be used to move around the camera, rotate the cube etc.:

```
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
```

---

## Credits

The included sunflower image is in the CC0/Public Domain, and was downloaded from here:

https://www.publicdomainpictures.net/en/view-image.php?image=756601&picture=large-yellow-sunflower

"License: CC0 Public Domain - Lynn Greyling has released this “Large Yellow Sunflower” image under Public Domain license."


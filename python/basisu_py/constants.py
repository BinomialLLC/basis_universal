# basisu_constants.py

# ============================================================
# .KTX2/.basis file types
# basist::basis_tex_format
# ============================================================
class BasisTexFormat:
    # Original LDR formats
    cETC1S = 0
    cUASTC_LDR_4x4 = 1

    # HDR
    cUASTC_HDR_4x4 = 2
    cASTC_HDR_6x6 = 3
    cUASTC_HDR_6x6 = 4

    # XUASTC supercompressed LDR formats
    cXUASTC_LDR_4x4  = 5
    cXUASTC_LDR_5x4  = 6
    cXUASTC_LDR_5x5  = 7
    cXUASTC_LDR_6x5  = 8
    cXUASTC_LDR_6x6  = 9
    cXUASTC_LDR_8x5  = 10
    cXUASTC_LDR_8x6  = 11
    cXUASTC_LDR_10x5 = 12
    cXUASTC_LDR_10x6 = 13
    cXUASTC_LDR_8x8  = 14
    cXUASTC_LDR_10x8 = 15
    cXUASTC_LDR_10x10= 16
    cXUASTC_LDR_12x10= 17
    cXUASTC_LDR_12x12= 18

    # Standard ASTC LDR
    cASTC_LDR_4x4  = 19
    cASTC_LDR_5x4  = 20
    cASTC_LDR_5x5  = 21
    cASTC_LDR_6x5  = 22
    cASTC_LDR_6x6  = 23
    cASTC_LDR_8x5  = 24
    cASTC_LDR_8x6  = 25
    cASTC_LDR_10x5 = 26
    cASTC_LDR_10x6 = 27
    cASTC_LDR_8x8  = 28
    cASTC_LDR_10x8 = 29
    cASTC_LDR_10x10= 30
    cASTC_LDR_12x10= 31
    cASTC_LDR_12x12= 32

# ============================================================
# Unified quality level: 1-100 (higher=better quality, 100 disables some codec options)
# ============================================================
class BasisQuality:
    MIN = 1
    MAX = 100

# ============================================================
# Unified effort level: 0-10 (0=fastest, 10=very slow, higher=slower but higher potential quality/more features utilized)
# ============================================================
class BasisEffort:
    MIN = 0
    MAX = 10

    SUPER_FAST = 0
    FAST = 2
    NORMAL = 5
    DEFAULT = 2
    SLOW = 8
    VERY_SLOW = 10

# ============================================================
# C-style API flags
# ============================================================
class BasisFlags:
    NONE = 0
    USE_OPENCL      = 1 << 8
    THREADED        = 1 << 9
    DEBUG_OUTPUT    = 1 << 10

    KTX2_OUTPUT     = 1 << 11
    KTX2_UASTC_ZSTD = 1 << 12

    SRGB            = 1 << 13
    GEN_MIPS_CLAMP  = 1 << 14
    GEN_MIPS_WRAP   = 1 << 15

    Y_FLIP          = 1 << 16

    PRINT_STATS     = 1 << 18
    PRINT_STATUS    = 1 << 19

    DEBUG_IMAGES    = 1 << 20

    REC2020         = 1 << 21
    VALIDATE_OUTPUT = 1 << 22

    XUASTC_LDR_FULL_ARITH  	= 0
    XUASTC_LDR_HYBRID      	= 1 << 23
    XUASTC_LDR_FULL_ZSTD   	= 2 << 23
    XUASTC_LDR_SYNTAX_SHIFT = 23
    XUASTC_LDR_SYNTAX_MASK  = 3
    
    TEXTURE_TYPE_2D             = 0 << 25
    TEXTURE_TYPE_2D_ARRAY       = 1 << 25
    TEXTURE_TYPE_CUBEMAP_ARRAY  = 2 << 25
    TEXTURE_TYPE_VIDEO_FRAMES   = 3 << 25
    TEXTURE_TYPE_SHIFT          = 25
    TEXTURE_TYPE_MASK           = 3

    VERBOSE = PRINT_STATS | PRINT_STATUS
    MIPMAP_CLAMP = GEN_MIPS_CLAMP
    MIPMAP_WRAP  = GEN_MIPS_WRAP

# ============================================================
# Transcoder Texture Formats (GPU block formats)
# basist::transcoder_texture_format
# ============================================================
class TranscoderTextureFormat:
    TF_ETC1_RGB       = 0
    TF_ETC2_RGBA      = 1
    TF_BC1_RGB        = 2
    TF_BC3_RGBA       = 3
    TF_BC4_R          = 4
    TF_BC5_RG         = 5
    TF_BC7_RGBA       = 6

    TF_PVRTC1_4_RGB   = 8
    TF_PVRTC1_4_RGBA  = 9

    TF_ASTC_LDR_4X4_RGBA = 10
    TF_ATC_RGB         = 11
    TF_ATC_RGBA        = 12

    # Uncompressed
    TF_RGBA32         = 13
    TF_RGB565         = 14
    TF_BGR565         = 15
    TF_RGBA4444       = 16

    TF_FXT1_RGB       = 17
    TF_PVRTC2_4_RGB   = 18
    TF_PVRTC2_4_RGBA  = 19

    TF_ETC2_EAC_R11   = 20
    TF_ETC2_EAC_RG11  = 21
    TF_BC6H           = 22

    TF_ASTC_HDR_4X4_RGBA = 23

    TF_RGB_HALF       = 24
    TF_RGBA_HALF      = 25
    TF_RGB_9E5        = 26
    TF_ASTC_HDR_6X6_RGBA = 27

    TF_ASTC_LDR_5X4_RGBA = 28
    TF_ASTC_LDR_5X5_RGBA = 29
    TF_ASTC_LDR_6X5_RGBA = 30
    TF_ASTC_LDR_6X6_RGBA = 31
    TF_ASTC_LDR_8X5_RGBA = 32
    TF_ASTC_LDR_8X6_RGBA = 33
    TF_ASTC_LDR_10X5_RGBA = 34
    TF_ASTC_LDR_10X6_RGBA = 35
    TF_ASTC_LDR_8X8_RGBA  = 36
    TF_ASTC_LDR_10X8_RGBA = 37
    TF_ASTC_LDR_10X10_RGBA= 38
    TF_ASTC_LDR_12X10_RGBA= 39
    TF_ASTC_LDR_12X12_RGBA= 40

    TOTAL = 41

# ============================================================
# Transcoder Decode Flags
# ============================================================
class TranscodeDecodeFlags:
    PVRTC_DECODE_TO_NEXT_POW2               = 2
    TRANSCODE_ALPHA_TO_OPAQUE               = 4
    BC1_FORBID_THREE_COLOR_BLOCKS           = 8
    OUTPUT_HAS_ALPHA_INDICES                = 16
    HIGH_QUALITY                             = 32
    NO_ETC1S_CHROMA_FILTERING                = 64
    NO_DEBLOCK_FILTERING                    = 128
    STRONGER_DEBLOCK_FILTERING              = 256
    FORCE_DEBLOCK_FILTERING                 = 512
    XUASTC_LDR_DISABLE_FAST_BC7_TRANSCODING = 1024

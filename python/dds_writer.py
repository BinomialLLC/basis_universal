# dds_writer.py
#
# Minimal DDS writer that mirrors the C/C++ save_dds() implementation you provided.
# It writes a DX9-style DDS header, and optionally a DX10 extension header,
# followed by the raw compressed blocks.
#
# No mipmaps, no cubes, no 3D volumes – exactly like the original C code.

import struct
import sys
from typing import Union


# ---------------------------------------------------------------------------
# FourCC helper (same as PIXEL_FMT_FOURCC macro)
# ---------------------------------------------------------------------------
def make_fourcc(a: str, b: str, c: str, d: str) -> int:
    return (ord(a) |
            (ord(b) << 8) |
            (ord(c) << 16) |
            (ord(d) << 24))


# ---------------------------------------------------------------------------
# DDS-related constants (only the ones we actually use)
# ---------------------------------------------------------------------------

# DDSD flags
DDSD_CAPS       = 0x00000001
DDSD_HEIGHT     = 0x00000002
DDSD_WIDTH      = 0x00000004
DDSD_PIXELFORMAT= 0x00001000
DDSD_LINEARSIZE = 0x00080000

# DDPF flags
DDPF_FOURCC     = 0x00000004

# DDSCAPS flags
DDSCAPS_TEXTURE = 0x00001000

# DXGI_FORMAT subset (values must match the C enum)
class DXGI_FORMAT:
    UNKNOWN        = 0
    BC1_UNORM      = 71
    BC3_UNORM      = 77
    BC4_UNORM      = 80
    BC5_UNORM      = 83
    # You can add more as needed; for DX10 header we just write the integer value.

# DX10 resource dimension
class D3D10_RESOURCE_DIMENSION:
    UNKNOWN   = 0
    BUFFER    = 1
    TEXTURE1D = 2
    TEXTURE2D = 3
    TEXTURE3D = 4


# ---------------------------------------------------------------------------
# DDS writer class
# ---------------------------------------------------------------------------
class DDSWriter:
    """
    Python port of the C save_dds() function.

    Usage:
        writer = DDSWriter()
        ok = writer.save_dds(
            filename="out.dds",
            width=width,
            height=height,
            blocks=bc_data,              # bytes or bytearray
            pixel_format_bpp=4,          # e.g. 4 for BC1, 8 for BC3/4/5/etc.
            dxgi_format=DXGI_FORMAT.BC1_UNORM,
            srgb=False,
            force_dx10_header=False,
        )
    """

    DDS_MAGIC = b"DDS "       # same as fwrite("DDS ", 4, 1, pFile);

    def save_dds(
        self,
        filename: str,
        width: int,
        height: int,
        blocks: Union[bytes, bytearray, memoryview],
        pixel_format_bpp: int,
        dxgi_format: int,
        srgb: bool = False,
        force_dx10_header: bool = False,
    ) -> bool:
        """
        Port of:
            bool save_dds(const char* pFilename,
                          uint32_t width, uint32_t height,
                          const void* pBlocks,
                          uint32_t pixel_format_bpp,
                          DXGI_FORMAT dxgi_format,
                          bool srgb,
                          bool force_dx10_header);

        The 'blocks' buffer is written as-is (up to computed linear size).
        """

        # srgb is intentionally unused in the original C code (commented logic).
        _ = srgb

        # Open file like the C code
        try:
            f = open(filename, "wb")
        except OSError:
            print(f"Failed creating file {filename}!", file=sys.stderr)
            return False

        try:
            # Write the "DDS " magic
            f.write(self.DDS_MAGIC)

            # -----------------------------------------------------------------
            # Build DDSURFACEDESC2 equivalent
            # -----------------------------------------------------------------
            # We'll pack DDSURFACEDESC2 as 31 uint32's (124 bytes) in little-endian:
            # struct DDSURFACEDESC2 {
            #   uint32 dwSize;
            #   uint32 dwFlags;
            #   uint32 dwHeight;
            #   uint32 dwWidth;
            #   uint32 lPitch_or_dwLinearSize;
            #   uint32 dwBackBufferCount;
            #   uint32 dwMipMapCount;
            #   uint32 dwAlphaBitDepth;
            #   uint32 dwUnused0;
            #   uint32 lpSurface;
            #   DDCOLORKEY unused0;  (2 * uint32)
            #   DDCOLORKEY unused1;  (2 * uint32)
            #   DDCOLORKEY unused2;  (2 * uint32)
            #   DDCOLORKEY unused3;  (2 * uint32)
            #   DDPIXELFORMAT ddpfPixelFormat; (8 * uint32)
            #   DDSCAPS2 ddsCaps; (4 * uint32)
            #   uint32 dwUnused1;
            # };

            dwSize = 124  # sizeof(DDSURFACEDESC2)

            dwFlags = (
                DDSD_WIDTH |
                DDSD_HEIGHT |
                DDSD_PIXELFORMAT |
                DDSD_CAPS
            )

            dwWidth = int(width)
            dwHeight = int(height)

            # lPitch (actually LinearSize for compressed formats), same as:
            # (((dwWidth + 3) & ~3) * ((dwHeight + 3) & ~3) * pixel_format_bpp) >> 3;
            lPitch = (
                ((dwWidth + 3) & ~3)
                * ((dwHeight + 3) & ~3)
                * int(pixel_format_bpp)
            ) >> 3

            dwFlags |= DDSD_LINEARSIZE

            dwBackBufferCount = 0
            dwMipMapCount = 0
            dwAlphaBitDepth = 0
            dwUnused0 = 0
            lpSurface = 0

            # DDCOLORKEY unused0..3, all zero
            ddcolorkey_zero = [0, 0] * 4  # 4 DDCOLORKEY structs

            # DDPIXELFORMAT
            # struct DDPIXELFORMAT {
            #   uint32 dwSize;
            #   uint32 dwFlags;
            #   uint32 dwFourCC;
            #   uint32 dwRGBBitCount;
            #   uint32 dwRBitMask;
            #   uint32 dwGBitMask;
            #   uint32 dwBBitMask;
            #   uint32 dwRGBAlphaBitMask;
            # };
            ddpf_dwSize = 32
            ddpf_dwFlags = DDPF_FOURCC
            ddpf_dwFourCC = 0
            ddpf_dwRGBBitCount = 0
            ddpf_dwRBitMask = 0
            ddpf_dwGBitMask = 0
            ddpf_dwBBitMask = 0
            ddpf_dwRGBAlphaBitMask = 0

            # DDSCAPS2
            # struct DDSCAPS2 {
            #   uint32 dwCaps;
            #   uint32 dwCaps2;
            #   uint32 dwCaps3;
            #   uint32 dwCaps4;
            # };
            ddsCaps_dwCaps = DDSCAPS_TEXTURE
            ddsCaps_dwCaps2 = 0
            ddsCaps_dwCaps3 = 0
            ddsCaps_dwCaps4 = 0

            dwUnused1 = 0

            # Decide whether to use legacy FourCC (DXT1/DXT5/ATI1/ATI2) or DX10 header
            use_legacy = (
                not force_dx10_header and
                dxgi_format in (
                    DXGI_FORMAT.BC1_UNORM,
                    DXGI_FORMAT.BC3_UNORM,
                    DXGI_FORMAT.BC4_UNORM,
                    DXGI_FORMAT.BC5_UNORM,
                )
            )

            if use_legacy:
                if dxgi_format == DXGI_FORMAT.BC1_UNORM:
                    ddpf_dwFourCC = make_fourcc('D', 'X', 'T', '1')
                elif dxgi_format == DXGI_FORMAT.BC3_UNORM:
                    ddpf_dwFourCC = make_fourcc('D', 'X', 'T', '5')
                elif dxgi_format == DXGI_FORMAT.BC4_UNORM:
                    ddpf_dwFourCC = make_fourcc('A', 'T', 'I', '1')
                elif dxgi_format == DXGI_FORMAT.BC5_UNORM:
                    ddpf_dwFourCC = make_fourcc('A', 'T', 'I', '2')
            else:
                # Write DX10 header, FourCC = "DX10"
                ddpf_dwFourCC = make_fourcc('D', 'X', '1', '0')

            # Build the 31 uint32's for DDSURFACEDESC2
            header_values = [
                dwSize,
                dwFlags,
                dwHeight,
                dwWidth,
                lPitch,
                dwBackBufferCount,
                dwMipMapCount,
                dwAlphaBitDepth,
                dwUnused0,
                lpSurface,
            ]

            header_values.extend(ddcolorkey_zero)  # 8 uint32's

            ddpf_values = [
                ddpf_dwSize,
                ddpf_dwFlags,
                ddpf_dwFourCC,
                ddpf_dwRGBBitCount,
                ddpf_dwRBitMask,
                ddpf_dwGBitMask,
                ddpf_dwBBitMask,
                ddpf_dwRGBAlphaBitMask,
            ]
            header_values.extend(ddpf_values)  # 8 uint32's

            ddsCaps_values = [
                ddsCaps_dwCaps,
                ddsCaps_dwCaps2,
                ddsCaps_dwCaps3,
                ddsCaps_dwCaps4,
            ]
            header_values.extend(ddsCaps_values)  # 4 uint32's

            header_values.append(dwUnused1)  # final uint32

            if len(header_values) != 31:
                raise RuntimeError("Internal error: DDSURFACEDESC2 must contain 31 uint32's")

            # Pack and write DDSURFACEDESC2
            dds_header = struct.pack("<31I", *header_values)
            f.write(dds_header)

            # If needed, write the DX10 header (DDS_HEADER_DXT10)
            if not use_legacy:
                # struct DDS_HEADER_DXT10 {
                #   DXGI_FORMAT              dxgiFormat;
                #   D3D10_RESOURCE_DIMENSION resourceDimension;
                #   uint32                  miscFlag;
                #   uint32                  arraySize;
                #   uint32                  miscFlags2;
                # };
                dxgiFormat = int(dxgi_format)
                resourceDimension = D3D10_RESOURCE_DIMENSION.TEXTURE2D
                miscFlag = 0
                arraySize = 1
                miscFlags2 = 0

                dxt10_header = struct.pack(
                    "<5I",
                    dxgiFormat,
                    resourceDimension,
                    miscFlag,
                    arraySize,
                    miscFlags2,
                )
                f.write(dxt10_header)

            # -----------------------------------------------------------------
            # Write the actual texture data blocks (pBlocks)
            # -----------------------------------------------------------------

            # C code: fwrite(pBlocks, desc.lPitch, 1, pFile);
            # i.e. write exactly lPitch bytes.
            data = memoryview(blocks)
            if len(data) < lPitch:
                raise ValueError(
                    f"blocks buffer too small: need at least {lPitch} bytes, got {len(data)}"
                )
            f.write(data[:lPitch])

        except Exception as e:
            # Mimic the C-style error reporting as much as practical
            print(f"Failed writing to DDS file {filename}: {e}", file=sys.stderr)
            try:
                f.close()
            except Exception:
                pass
            return False

        # Close file
        try:
            f.close()
        except OSError:
            print(f"Failed closing DDS file {filename}!", file=sys.stderr)
            return False

        return True

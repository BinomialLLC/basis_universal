"""
basisu_py
=========
Python bindings for the Basis Universal encoder and transcoder, with
automatic fallback between native C++ extensions and WASM modules.

Main entry points:
    - Transcoder  : basisu_py.transcoder.Transcoder
    - Encoder     : basisu_py.codec.Encoder
    - constants   : basisu_py.constants
"""

from .codec import Encoder
from .transcoder import Transcoder, KTX2Handle
from .constants import (
    BasisTexFormat,
    BasisQuality,
    BasisEffort,
    BasisFlags,
    TranscoderTextureFormat,
    TranscodeDecodeFlags,
)

# What the package publicly exposes
__all__ = [
    "Encoder",
    "Transcoder",
    "KTX2Handle",
    "BasisTexFormat",
    "BasisQuality",
    "BasisEffort",
    "BasisFlags",
    "TranscoderTextureFormat",
    "TranscodeDecodeFlags",
]

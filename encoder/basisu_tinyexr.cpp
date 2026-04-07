// File: basisu_tinyexr.cpp
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#define MINIZ_HEADER_FILE_ONLY
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "basisu_miniz.h"

// Force tinyexr to use zlib-style compression API's, then we'll direct them to our own customized copy of miniz. (Binomial wrote the original miniz library.)
// This allows us to use tinyexr.h without modify it at all, or relying on zlib.
#define TINYEXR_USE_MINIZ (0)

enum { Z_OK = 0, Z_STREAM_END = 1, Z_NEED_DICT = 2, Z_ERRNO = -1, Z_STREAM_ERROR = -2, Z_DATA_ERROR = -3, Z_MEM_ERROR = -4, Z_BUF_ERROR = -5, Z_VERSION_ERROR = -6, Z_PARAM_ERROR = -10000 };
typedef unsigned long uLongf;
typedef unsigned long uLong;
typedef unsigned char Byte;
typedef Byte Bytef;

uLong compressBound(uLong src_size)
{
    return buminiz::mz_compressBound(src_size);
}

int compress(Bytef* dest, uLongf* destLen, const Bytef* source, uLong sourceLen)
{
    return buminiz::mz_compress(dest, destLen, source, sourceLen);
}

int uncompress(Bytef* dest, uLongf* destLen, const Bytef* source, uLong sourceLen)
{
    return buminiz::mz_uncompress(dest, destLen, source, sourceLen);
}

#ifdef _MSC_VER
#pragma warning (disable: 4060)
#pragma warning (disable: 4100)
#pragma warning (disable: 4245)
#pragma warning (disable: 4505)
#pragma warning (disable: 4702)
#endif

#define TINYEXR_IMPLEMENTATION
#include "3rdparty/tinyexr.h"

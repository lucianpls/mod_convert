/*
 * JPEG_Codec.cpp
 * C++ Wrapper around libjpeg, providing encoding and decoding functions
 *
 * use setjmp, the only safe way to mix C++ and libjpeg and still get error messages
 *
 * (C)Lucian Plesea 2016-2018
 */

#include "JPEG_codec.h"

// Dispatcher for 8 or 12 bit jpeg decoder
const char *jpeg_stride_decode(codec_params &params, const TiledRaster &raster, storage_manager &src, void *buffer)
{
    return jpeg8_stride_decode(params, raster, src, buffer);
}


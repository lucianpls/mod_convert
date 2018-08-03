/*
 * mod_convert.h
 * 
 * Part of AHTSE, converts from one image format to another
 *
 * (C) Lucian Plesea 2018
 */

#if !defined(MOD_CONVERT_H)
#define MOD_CONVERT_H
#include <ahtse_util.h>

// Should get rid of this one, only needed for the png params
#include <png.h>
// #include <jpeglib.h>

 //
 // Any decoder needs a static place for an error message and a line stride when decoding
 // This structure is accepted by the decoders, regardless of type
 // For encoders, see format specific extensions below
 //

struct codec_params {
    // Line size in bytes
    apr_uint32_t line_stride;
    // A place for codec error message
    char error_message[1024];
};

// In JPEG_codec.cpp
// raster defines the expected tile
// src contains the input JPEG
// buffer is the location of the first byte on the first line of decoded data
// line_stride is the size of a line in buffer (larger or equal to decoded JPEG line)
// Returns NULL if everything looks fine, or an error message
struct jpeg_params : codec_params {
    int quality;
};

const char *jpeg_stride_decode(codec_params &params, const TiledRaster &raster, storage_manager &src,
    void *buffer);
const char *jpeg_encode(jpeg_params &params, const TiledRaster &raster, storage_manager &src,
    storage_manager &dst);

struct png_params : codec_params {
    // As defined by PNG
    int color_type, bit_depth;
    // 0 to 9
    int compression_level;

    // If true, NDV is the transparent color
    int has_transparency;

    // If has_transparency is on, this is the transparent color definition
    png_color_16 NDV;
};

// In PNG_codec.cpp
// raster defines the expected tile
// src contains the input PNG
// buffer is the location of the first byte on the first line of decoded data
// line_stride is the size of a line in buffer (larger or equal to decoded PNG line)
// Returns NULL if everything looks fine, or an error message
const char *png_stride_decode(codec_params &params, const TiledRaster &raster,
    storage_manager &src, void *buffer);
const char *png_encode(png_params &params, const TiledRaster &raster,
    storage_manager &src, storage_manager &dst);
// Based on the raster configuration, populates a png parameter structure
int set_png_params(const TiledRaster &raster, png_params *params);

struct convert_conf {
    // array of guard regexp pointers, one of them has to match
    apr_array_header_t *arr_rxp;

    TiledRaster raster, inraster;
    // const char *source, *postfix;

    // internal path of source
    char *source;

    apr_uint64_t seed;
    // The empty tile and the empty etag
    empty_conf_t empty;

    // Meaning depends on output format
    double quality;
};

#endif
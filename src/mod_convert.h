/*
 * mod_convert.h
 * 
 * Part of AHTSE, converts from one image format to another
 *
 * (C) Lucian Plesea 2018
 */

#if !defined(MOD_CONVERT_H)
#define MOD_CONVERT_H
#include <apr.h>
#include <png.h>

 // Conversion to and from network order, endianess depenent
#if (APR_IS_BIGENDIAN == 0) // Little endian
#if defined(WIN32) // Windows
#define ntoh16(v) _byteswap_ushort(v)
#define hton16(v) _byteswap_ushort(v)
#define ntoh32(v) _byteswap_ulong(v)
#define hton32(v) _byteswap_ulong(v)
#define ntoh64(v) _byteswap_uint64(v)
#define hton64(v) _byteswap_uint64(v)
#else // Assume linux
#define ntoh16(v) __builtin_bswap16(v)
#define hton16(v) __builtin_bswap16(v)
#define ntoh32(v) __builtin_bswap32(v)
#define hton32(v) __builtin_bswap32(v)
#define ntoh64(v) __builtin_bswap64(v)
#define hton64(v) __builtin_bswap64(v)
#endif
#else // Big endian, do nothing
#define ntoh16(v)  (v)
#define hton16(v)  (v)
#define ntoh32(v)  (v)
#define ntoh64(v)  (v)
#define hton32(v)  (v)
#define hton64(v)  (v)
#endif

#define PNG_SIG 0x89504e47
#define JPEG_SIG 0xffd8ffe0
#define LERC_SIG 0x436e745a

 // This one is not an image type, but an encoding
#define GZIP_SIG 0x1f8b0800

 // Pixel value data types
 // Copied and modified from GDAL
typedef enum {
    /*! Unknown or unspecified type */ 		GDT_Unknown = 0,
    /*! Eight bit unsigned integer */           GDT_Byte = 1,
    GDT_Char = 1,
    /*! Sixteen bit unsigned integer */         GDT_UInt16 = 2,
    /*! Sixteen bit signed integer */           GDT_Int16 = 3,
    GDT_Short = 3,
    /*! Thirty two bit unsigned integer */      GDT_UInt32 = 4,
    /*! Thirty two bit signed integer */        GDT_Int32 = 5,
    GDT_Int = 5,
    /*! Thirty two bit floating point */        GDT_Float32 = 6,
    GDT_Float = 6,
    /*! Sixty four bit floating point */        GDT_Float64 = 7,
    GDT_Double = 7,
    GDT_TypeCount = 8          /* maximum type # + 1 */
} GDALDataType;

 // Separate channels and level, just in case
struct sz {
    apr_int64_t x, y, z, c, l;
};

struct bbox_t {
    double xmin, ymin, xmax, ymax;
};

struct TiledRaster {
    // Size and pagesize of the raster
    struct sz size, pagesize;
    // width and height for each pyramid level
    struct rset *rsets;
    // how many levels from full size, computed
    int n_levels;
    // How many levels to skip at the top of the pyramid
    int skip;
    int datatype;

    // geographical projection
    const char *projection;
    struct bbox_t bbox;
};

typedef struct {
    char *buffer;
    int size;
} storage_manager;

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
    // If has_transparency, this is the transparent color definition
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

#endif
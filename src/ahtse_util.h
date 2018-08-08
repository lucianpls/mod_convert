/*
* ahtse_util.h
*
* Common parts of AHTSE
*
* (C) Lucian Plesea 2018
*/

#if !defined(AHTSE_UTIL_H)
#define AHTSE_UTIL_H
#include <httpd.h>
#include <apr.h>
#include <apr_tables.h>
#include <apr_pools.h>

// Always include httpd.h before other http* headers
// #include <httpd.h>
// #include <http_config.h>

// Byte swapping, linux style
#if defined(WIN32) // Windows
//#define __builtin_bswap16(v) _byteswap_ushort(v)
//#define __builtin_bswap32(v) _byteswap_ulong(v)
//#define __builtin_bswap64(v) _byteswap_uint64(v)
#endif

// Conversion to and from network order, endianess depenent
// Define 4cc signatures for known types, with the correct endianess
#if APR_IS_BIGENDIAN // Big endian, do nothing

// These values are big endian
#define PNG_SIG 0x89504e47
#define JPEG_SIG 0xffd8ffe0

// Lerc is only supported on little endian
// #define LERC_SIG 0x436e745a

// This one is not an image type, but an encoding
#define GZIP_SIG 0x1f8b0800

#else // Little endian

#define PNG_SIG  0x474e5089
#define JPEG_SIG 0xe0ffd8ff
#define LERC_SIG 0x5a746e43

// This one is not an image type, but an encoding
#define GZIP_SIG 0x00088b1f

#endif

#define READ_RIGHTS APR_FOPEN_READ | APR_FOPEN_BINARY | APR_FOPEN_LARGEFILE
// Accept empty tiles up to this size
#define MAX_READ_SIZE (1024*1024)

// removes and returns the value of the last element from an apr_array, as type
// will crash by dereferencing the null pointer if the array is empty
#define ARRAY_POP(arr, type) (*(type *)(apr_array_pop(arr)))

// Returns a bad request code if condition is met
#define RETURN_ERR_IF(X) if (X) { return HTTP_BAD_REQUEST;}

// It takes
#define SERVER_ERR_IF(X, r, msg) if (X) {\
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, msg);\
    return HTTP_INTERNAL_SERVER_ERROR;\
}

// Pixel value data types
// Copied and slightly modified from GDAL
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

struct rset {
    // Resolution, units per pixel
    double rx, ry;
    // In tiles
    int w, h;
};

typedef struct {
    char *buffer;
    int size;
} storage_manager;

struct empty_conf_t {
    // Buffer for the empty tile etag
    char eTag[16];

    // Empty tile in RAM, if defined
    storage_manager empty;
};

// Return a GDAL data type by name
GDALDataType getDT(const char *name);

// Populates size and returns null if it works, error message otherwise
// "x y", "x y z" or "x y z c"
const char *get_xyzc_size(struct sz *size, const char *s);

// Add the compiled pattern tot the regexp array.  It allocates the array if necessary
const char *add_regexp_to_array(apr_pool_t *pool, apr_array_header_t **parr, const char *pattern);

//
// Reads a text file and returns a table where the first token of each line is the key
// and the rest of the line is the value.  Empty lines and lines that start with # are ignored
//
apr_table_t *readAHTSEConfig(apr_pool_t *pool, const char *fname, const char **err_message);

// Initialize a raster from a kvp table
const char *configRaster(apr_pool_t *pool, apr_table_t *kvp, struct TiledRaster &raster);

// Return a GDALDataType from a name, or GDT_Byte
GDALDataType getDT(const char *name);

// Reads a bounding box, x,y,X,Y order.  Expects up to four numbers in C locale, comma separated
const char *getBBox(const char *line, bbox_t &bbox);

// From a string in base32 returns a 64bit int plus the extra boolean flag
apr_uint64_t base32decode(const char *is, int *flag);

// Encode a 64 bit integer to base32 string. Buffer should be at least 13 chars
void tobase32(apr_uint64_t value, char *buffer, int flag = 0);

//
// Read the empty file in a provided storage buffer
// the buffer gets alocated from the pool
// returns error message if something went wrong
// Maximum read size is set by MAX_READ_SIZE macro
// The line can contain the size and offset, white space separated, before the file name
//
char *readFile(apr_pool_t *pool, storage_manager &empty, const char *line);

// Returns true if one of the regexps compiled in the array match the full request, including args
bool requestMatches(request_rec *r, apr_array_header_t *arr);

// tokenize a string into an array, based on a character. Returns nullptr if unsuccessful
apr_array_header_t *tokenize(apr_pool_t *p, const char *src, char sep = '/');

// returns true if the If-None-Match request etag matches
int etagMatches(request_rec *r, const char *ETag);

// Returns an image and a 200 error code
// Sets the mime type if provided, but it doesn't overwrite an already set one\
// Also sets gzip encoding if the content is gzipped and the requester handles it
// Does not handle conditional response or setting ETags, those should already be set
// src.buffer should hold at least 4 bytes
int sendImage(request_rec *r, const storage_manager &src, const char *mime_type = nullptr);

// Called with an empty tile configuration, send the empty tile with the proper ETag
// Handles conditional requests
int sendEmptyTile(request_rec *r, const empty_conf_t &empty);

#endif

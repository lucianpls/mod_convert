/*
 * JPEG_Codec.cpp
 * C++ Wrapper around libjpeg, providing encoding and decoding functions
 * uses C++ throw-catch instead of setjmp
 *
 * (C)Lucian Plesea 2016-2017
 */

#include "mod_convert.h"
static void emitMessage(j_common_ptr cinfo, int msgLevel);
static void errorExit(j_common_ptr cinfo);

struct ErrorMgr {
    // The jpeg standard error manager
    struct jpeg_error_mgr jerr_mgr;
    // A place to hold a message
    char *message;
    jmp_buf setjmpBuffer;
};

static void emitMessage(j_common_ptr cinfo, int msgLevel)
{
    ErrorMgr* err = (ErrorMgr *)cinfo->err;
    if (msgLevel > 0) return; // No trace msgs

    // There can be many warnings, just store the first one
    if (err->jerr_mgr.num_warnings++ >1)
        return;
    err->jerr_mgr.format_message(cinfo, err->message);
}

// No return to caller
static void errorExit(j_common_ptr cinfo)
{
    ErrorMgr* err = (ErrorMgr*) cinfo->err;
    err->jerr_mgr.format_message(cinfo, err->message);
    longjmp(err->setjmpBuffer, 1);
}

/**
*\Brief Do nothing stub function for JPEG library, called
*/
static void stub_source_dec(j_decompress_ptr /* cinfo */) {}

/**
*\Brief: Called when libjpeg gets an unknwon chunk
* It should skip l bytes of input, otherwise jpeg will throw an error
*
*/
static void skip_input_data_dec(j_decompress_ptr cinfo, long l) {
    struct jpeg_source_mgr *src = cinfo->src;
    if (static_cast<size_t>(l) > src->bytes_in_buffer)
        l = static_cast<long>(src->bytes_in_buffer);
    src->bytes_in_buffer -= l;
    src->next_input_byte += l;
}

// Destination should be already set up
static void init_or_terminate_destination(j_compress_ptr /* cinfo */) {}

/**
*\Brief: Do nothing stub function for JPEG library, called?
*/
static boolean fill_input_buffer_dec(j_decompress_ptr /* cinfo */) { return TRUE; }

// Called if the buffer provided is too small
static boolean empty_output_buffer(j_compress_ptr /* cinfo */) { return FALSE; }

// IMPROVE: could reuse the cinfo, to save some memory allocation
// IMPROVE: Use a jpeg memory manager to link JPEG memory into apache's pool mechanism
const char *jpeg_stride_decode(codec_params &params, const TiledRaster &raster,
    storage_manager &src,
    void *buffer)
{
    char *rp[2]; // Two lines at a time
    static_assert(sizeof(params.error_message) >= JMSG_LENGTH_MAX, "Message buffer too small");
    params.error_message[0] = 0; // Clear errors

    jpeg_decompress_struct cinfo;
    ErrorMgr err;
    memset(&err, 0, sizeof(ErrorMgr));
    // JPEG error message goes directly in the parameter error message space
    err.message = params.error_message;
    struct jpeg_source_mgr s = { (JOCTET *)src.buffer, static_cast<size_t>(src.size) };

    cinfo.err = jpeg_std_error(& err.jerr_mgr);
    // Set these after hooking up the standard error methods
    err.jerr_mgr.error_exit = errorExit;
    err.jerr_mgr.emit_message = emitMessage;

    // And set our functions
    s.term_source = s.init_source = stub_source_dec;
    s.skip_input_data = skip_input_data_dec;
    s.fill_input_buffer = fill_input_buffer_dec;
    s.resync_to_restart = jpeg_resync_to_restart;

    if (setjmp(err.setjmpBuffer)) {
        // errorExit comes here
        jpeg_destroy_decompress(&cinfo);
        return params.error_message;
    }

    jpeg_create_decompress(&cinfo);

    cinfo.src = &s;
    jpeg_read_header(&cinfo, TRUE);
    cinfo.dct_method = JDCT_FLOAT;

    if (!(raster.pagesize.c == 1 || raster.pagesize.c == 3))
        sprintf(params.error_message, "JPEG with wrong number of components");

    if (cinfo.arith_code || !cinfo.is_baseline)
        sprintf(params.error_message, "Unsupported JPEG type, %s", 
            cinfo.arith_code ? "arithmetic encoding" : "not baseline");

    if (cinfo.data_precision != 8)
        sprintf(params.error_message, "JPEG with more than 8 bits of data");

    if (cinfo.image_width != raster.pagesize.x || cinfo.image_height != raster.pagesize.y)
        sprintf(params.error_message, "Wrong JPEG size on input");

    // TODO: Other checks, to make sure the output buffer is the proper size

    // Only if the error message hasn't been set already
    if (params.error_message[0] == 0) {

        // Looks good, go ahead and decode
        cinfo.out_color_space = (raster.pagesize.c == 3) ? JCS_RGB : JCS_GRAYSCALE;
        jpeg_start_decompress(&cinfo);

        while (cinfo.output_scanline < cinfo.image_height) {
            rp[0] = (char *)buffer + params.line_stride * cinfo.output_scanline;
            rp[1] = rp[0] + params.line_stride;
            jpeg_read_scanlines(&cinfo, JSAMPARRAY(rp), 2);
        }

        jpeg_finish_decompress(&cinfo);
    }

    jpeg_destroy_decompress(&cinfo);

    return params.error_message[0] != 0 ? params.error_message : nullptr; // Either nullptr or error message
}

const char *jpeg_encode(jpeg_params &params, const TiledRaster &raster, storage_manager &src, 
    storage_manager &dst)
{
    struct jpeg_compress_struct cinfo;
    ErrorMgr err;
    jpeg_destination_mgr mgr;
    int linesize;
    char *rp[2];

    mgr.next_output_byte = (JOCTET *)dst.buffer;
    mgr.free_in_buffer = dst.size;
    mgr.init_destination = init_or_terminate_destination;
    mgr.empty_output_buffer = empty_output_buffer;
    mgr.term_destination = init_or_terminate_destination;
    memset(&err, 0, sizeof(err));
    cinfo.err = jpeg_std_error(&err.jerr_mgr);
    err.message = params.error_message;

    if (setjmp(err.setjmpBuffer)) {
        jpeg_destroy_compress(&cinfo);
        return params.error_message;
    }

    jpeg_create_compress(&cinfo);
    cinfo.dest = &mgr;
    cinfo.image_width = static_cast<JDIMENSION>(raster.pagesize.x);
    cinfo.image_height = static_cast<JDIMENSION>(raster.pagesize.y);
    cinfo.input_components = static_cast<int>(raster.pagesize.c);
    cinfo.in_color_space = (raster.pagesize.c == 3) ? JCS_RGB : JCS_GRAYSCALE;

    jpeg_set_defaults(&cinfo);

    jpeg_set_quality(&cinfo, params.quality, TRUE);
    cinfo.dct_method = JDCT_FLOAT;
    linesize = cinfo.image_width * cinfo.num_components;

    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline != cinfo.image_height) {
        rp[0] = src.buffer + linesize * cinfo.next_scanline;
        rp[1] = rp[0] + linesize;
        jpeg_write_scanlines(&cinfo, JSAMPARRAY(rp), 2);
    }
    jpeg_finish_compress(&cinfo);

    jpeg_destroy_compress(&cinfo);
    dst.size -= mgr.free_in_buffer;

    return params.error_message;
}

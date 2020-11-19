/*
* mod_convert.cpp
*
* Part of AHTSE, converts from one image format to another
*
* (C) Lucian Plesea 2018-2020
*/

// #include <tuple>
// #include <vector>

#include <ahtse.h>

#include <http_main.h>
#include <http_protocol.h>
#include <http_core.h>
#include <http_request.h>
#include <http_log.h>

#include <apr_strings.h>

// From mod_receive
#include <receive_context.h>

// Standard headers
#include <cstdint>
#include <unordered_map>

NS_AHTSE_USE

extern module AP_MODULE_DECLARE_DATA convert_module;
#define USER_AGENT "AHTSE Convert"

#if defined(APLOG_USE_MODULE)
APLOG_USE_MODULE(convert);
#endif

struct convert_conf {
    // array of guard regexp pointers, one of them has to match
    apr_array_header_t *arr_rxp;

    TiledRaster raster, inraster;

    // internal path of source
    char *source;
    // append this to the end of request url to the input
    char *suffix;

    // the maximum size of an input tile
    apr_size_t max_input_size;

    // Table of doubles.
    // Three values per point, except for the last one
    // inputs, output, slope
    // Input values are in increasing order
    apr_array_header_t *lut;

    // Meaning depends on output format
    double quality;

    // Set if this module is to be used from indirect (not external) requests?
    int indirect;
};

using namespace std;

// Converstion of src from TFrom to TTo, as required by the configuration
template<typename TFrom, typename TTo> static void 
    conv_dt(const convert_conf *cfg, TFrom *src, TTo *dst)
{
    // Assume compact buffers, allocated with the right values
    int count = static_cast<int>(cfg->inraster.pagesize.x 
        * cfg->inraster.pagesize.y * cfg->inraster.pagesize.c);
    const apr_array_header_t *arr = cfg->lut;
    while (count--) {
        double in_val = *src++;
        int i = 0;

        // Find the segment that contains in_val, or use last point
        while (in_val > APR_ARRAY_IDX(arr, i, double) 
            && arr->nelts > i + 3 
            && in_val >= APR_ARRAY_IDX(arr, i + 3, double))
            i += 3;

        const double segment = in_val - APR_ARRAY_IDX(arr, i, double);
        const double offset = APR_ARRAY_IDX(arr, i + 1, double);

        // A shortcut, when the input value matches the point
        if (segment <= 0) {
            *dst++ = static_cast<TTo>(offset);
            continue;
        }

        const double slope = APR_ARRAY_IDX(arr, i + 2, double);
        // No over/under flow checks
        *dst++ = static_cast<TTo>(offset + segment * slope);
    }
}

// Convert src as required by the configuration
// returns pointer to where the output is, could be different from source
// Returns nullptr in case of errors
// cfg->lut is always valid
static void *convert_dt(const convert_conf *cfg, void *src) {
    // Partial implementation
    void *result = nullptr; // Assume error, set result to non-null otherwise

// In place conversions, with LUT, when the output type is <= input type
#define CONV(T_src, T_dst) conv_dt(cfg, reinterpret_cast<T_src *>(src), reinterpret_cast<T_dst *>(src)); result = src; break;

    switch (cfg->inraster.datatype) {
    case GDT_Int32:
        switch (cfg->raster.datatype) {
        case GDT_Float: CONV(int32_t, float);
        case GDT_UInt32: CONV(int32_t, uint32_t);
        case GDT_Int32: CONV(int32_t, int32_t);
        case GDT_UInt16: CONV(int32_t, uint16_t);
        case GDT_Int16: CONV(int32_t, int16_t);
        case GDT_Byte: CONV(int32_t, uint8_t);
        default:;
        }
        break;
    case GDT_UInt32:
        switch (cfg->raster.datatype) {
        case GDT_Float: CONV(uint32_t, float);
        case GDT_UInt32: CONV(uint32_t, uint32_t);
        case GDT_Int32: CONV(uint32_t, int32_t);
        case GDT_UInt16: CONV(uint32_t, uint16_t);
        case GDT_Int16: CONV(uint32_t, int16_t);
        case GDT_Byte: CONV(uint32_t, uint8_t);
        default:;
        }
        break;
    case GDT_Int16:
        switch (cfg->raster.datatype) {
        case GDT_UInt16: CONV(int16_t, uint16_t);
        case GDT_Int16: CONV(int16_t, int16_t);
        case GDT_Byte: CONV(int16_t, uint8_t);
        default:;
        }
        break;
    case GDT_UInt16:
        switch (cfg->raster.datatype) {
        case GDT_UInt16: CONV(uint16_t, uint16_t);
        case GDT_Int16: CONV(uint16_t, int16_t);
        case GDT_Byte: CONV(uint16_t, uint8_t);
        default:;
        }
        break;
    case GDT_Byte:
        switch (cfg->raster.datatype) {
        case GDT_Byte: CONV(uint8_t, uint8_t);
        default:;
        }
        break;
    case GDT_Float:
        switch (cfg->raster.datatype) {
        case GDT_Float: CONV(float, float);
        case GDT_UInt32: CONV(float, uint32_t);
        case GDT_Int32: CONV(float, int32_t);
        case GDT_UInt16: CONV(float, uint16_t);
        case GDT_Int16: CONV(float, int16_t);
        case GDT_Byte: CONV(float, uint8_t);
        default:;
        }
    default:;
    }

#undef CONV

    // If the conversion wasn't done, it can't be done in place
    if (result == nullptr) {
        // TODO: allocate a destinaton buffer and do the conversion to that buffer
    }

    return result;
}

static int handler(request_rec *r)
{
    const char *message;
    if (r->method_number != M_GET)
        return DECLINED;

    auto *cfg = get_conf<convert_conf>(r, &convert_module);

    // If indirect is set, only activate on subrequests
    if (cfg->indirect && r->main == nullptr)
        return DECLINED;

    if (!cfg || !cfg->arr_rxp || !requestMatches(r, cfg->arr_rxp))
        return DECLINED;

    apr_array_header_t *tokens = tokenize(r->pool, r->uri);
    if (tokens->nelts < 3)
        return DECLINED; // At least three values, for RLC

    // This is a request to be handled here

    // server configuration error ?
    SERVER_ERR_IF(!ap_get_output_filter_handle("Receive"),
        r, "mod_receive not found");

    sz tile;
    memset(&tile, 0, sizeof(sz));

    tile.x = apr_atoi64(ARRAY_POP(tokens, char *)); RETURN_ERR_IF(errno);
    tile.y = apr_atoi64(ARRAY_POP(tokens, char *)); RETURN_ERR_IF(errno);
    tile.l = apr_atoi64(ARRAY_POP(tokens, char *)); RETURN_ERR_IF(errno);

    // Ignore the error on the M, it defaults to zero
    if (cfg->raster.size.z != 1 && tokens->nelts > 0)
        tile.z = apr_atoi64(ARRAY_POP(tokens, char *));

    // But we still need to check the results
    if (tile.x < 0 || tile.y < 0 || tile.l < 0)
        return sendEmptyTile(r, cfg->raster.missing);

    // Adjust the level to the full pyramid one
    tile.l += cfg->raster.skip;

    // Outside of bounds tile
    if (tile.l >= cfg->raster.n_levels ||
        tile.x >= cfg->raster.rsets[tile.l].w ||
        tile.y >= cfg->raster.rsets[tile.l].h)
        return sendEmptyTile(r, cfg->raster.missing);

    // Same is true for outside of input bounds
    if (tile.l >= cfg->inraster.n_levels ||
        tile.x >= cfg->inraster.rsets[tile.l].w ||
        tile.y >= cfg->inraster.rsets[tile.l].h)
        return sendEmptyTile(r, cfg->raster.missing);

    // Convert to true input level
    tile.l -= cfg->inraster.skip;

    // Incoming tile buffer
    receive_ctx rctx;
    rctx.maxsize = static_cast<int>(cfg->max_input_size);
    rctx.buffer = reinterpret_cast<char *>(apr_palloc(r->pool, rctx.maxsize));

    // Create the subrequest
    char *sub_uri = apr_pstrcat(r->pool, cfg->source,
        apr_psprintf(r->pool, tile.z ? "%d/%d/%d/%d" : "/%d/%d/%d",
            static_cast<int>(tile.z), static_cast<int>(tile.l),
            static_cast<int>(tile.y), static_cast<int>(tile.x)),
        cfg->suffix, NULL);

    request_rec *sr = ap_sub_req_lookup_uri(sub_uri, r, r->output_filters);

    const char *user_agent = apr_table_get(r->headers_in, "User-Agent");
    user_agent = (nullptr == user_agent) ?
        USER_AGENT : apr_pstrcat(r->pool, USER_AGENT ", ", user_agent, NULL);
    apr_table_setn(sr->headers_in, "User-Agent", user_agent);

    rctx.size = 0;
    // Start hooking up the input
    ap_filter_t *rf = ap_add_output_filter("Receive", &rctx, sr, sr->connection);

    int rr_status = ap_run_sub_req(sr);
    ap_remove_output_filter(rf);

    // Get a copy of the source ETag, otherwise it goes away with the subrequest
    const char *sETag = apr_table_get(sr->headers_out, "ETag");
    if (sETag != nullptr)
        sETag = apr_pstrdup(r->pool, sETag);

    ap_destroy_sub_req(sr);
    if (rr_status != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_NOTICE, rr_status, r,
            "Receive failed with code %d for %s", rr_status, sub_uri);
        return sendEmptyTile(r, cfg->raster.missing);
    }

    // If the input tile is the empty tile, send the output empty tile right now
    apr_uint64_t seed = 0;
    int missing = 0;
    if (sETag != nullptr) {
        seed = base32decode(sETag, &missing);
        if (missing || !ap_cstr_casecmp(sETag, cfg->inraster.missing.eTag))
            return sendEmptyTile(r, cfg->raster.missing);
    }

    // Input wasn't missing, compute an ETag, there is only one input ETag to use
    seed ^= cfg->raster.seed;
    if (seed == cfg->raster.seed) // Likely the input didn't provide an ETag
    { // Use some of the input bytes to make a better hash
        const int values = 32;
        seed = cfg->raster.seed;
        for (int i = 0; i < values; i++)
            seed ^= ((apr_uint64_t)rctx.buffer[(rctx.size / values) * i]) << ((i * 8) % 64);
    }

    char ETag[16];
    tobase32(seed, ETag);
    if (etagMatches(r, ETag))
        return HTTP_NOT_MODIFIED;

    // What format is the source, and what is the compression we want?
    apr_uint32_t in_format;
    memcpy(&in_format, rctx.buffer, 4);

    codec_params params;
    memset(&params, 0, sizeof(params));
    int pixel_size = GDTGetSize(cfg->inraster.datatype);
    int input_line_width = static_cast<int>(
        cfg->inraster.pagesize.x *  cfg->inraster.pagesize.c * pixel_size);
    int pagesize = static_cast<int>(input_line_width * cfg->inraster.pagesize.y);
    params.line_stride = input_line_width;
    storage_manager src(rctx.buffer, rctx.size);
    void *buffer = apr_pcalloc(r->pool, pagesize);

    if (JPEG_SIG == in_format)
        message = jpeg_stride_decode(params, src, buffer);
    else // format error
        message = "Unsupported input format";

    if (message) {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "%s from %s", message, sub_uri);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "raster type is %d size %d", 
            static_cast<int>(cfg->inraster.datatype), pixel_size);
        return HTTP_NOT_FOUND;
    }

    storage_manager raw(buffer, pagesize);

    // LUT presence implies a data conversion, otherwise the source is ready
    if (cfg->lut) {
        buffer = convert_dt(cfg, buffer);
        SERVER_ERR_IF(buffer == nullptr, r, "Conversion error, likely not implemented");
        raw.buffer = reinterpret_cast<char *>(buffer);
        params.modified = 1; // Force PNG out when converting type
    }

    // This part is only for converting Zen JPEGs to JPNG, as needed
    if (JPEG_SIG == in_format && params.modified == 0) // Zen mask absent or superfluous
        return sendImage(r, src, "image/jpeg");

    png_params out_params;
    set_png_params(cfg->raster, &out_params);

    // By default the NDV is zero, and the NVD field is zero
    // Check one more time that we had a Zen mask before turning the transparency on
    if (params.modified)
        out_params.has_transparency = true;


    storage_manager dst(
        apr_palloc(r->pool, cfg->max_input_size),
        cfg->max_input_size);
    SERVER_ERR_IF(dst.buffer == nullptr, r, "Memmory allocation error");

    message = png_encode(out_params, raw, dst);
    SERVER_ERR_IF(message != nullptr, r, "%s from %s", message, r->uri);

    apr_table_set(r->headers_out, "ETag", ETag);
    return sendImage(r, dst, "image/png");
}

static void *create_dir_config(apr_pool_t *p, char * /* path */) {
    convert_conf *c = reinterpret_cast<convert_conf *>(apr_pcalloc(p, sizeof(convert_conf)));
    return c;
}

// Reads a sequence of in:out floating point pairs, separated by commas.
// Input values should be in increasing order
// Might need "C" locale
static const char *read_lut(cmd_parms *cmd, convert_conf *c, const char *lut) {
    char *lut_string = apr_pstrdup(cmd->temp_pool, lut);
    char *last = nullptr;
    char *token = apr_strtok(lut_string, ",", &last);

    if (c->lut != nullptr)
        return "LUT redefined";

    // Start with sufficient space for 4 points
    apr_array_header_t *arr = apr_array_make(cmd->pool, 12, sizeof(double));

    char *sep=nullptr;
    while (token != nullptr) {
        double value_in = strtod(token, &sep);
        if (*sep++ != ':')
            return apr_psprintf(cmd->temp_pool, "Error in LUT token %s", token);
        if (arr->nelts > 1 && APR_ARRAY_IDX(arr, arr->nelts - 2, double) >= value_in)
            return "Incorrect LUT, input values should be increasing";

        // 0.5 is rounding correction for integer types
        double value_out = strtod(sep, &sep) + 0.5;
        if (*sep != 0)
            return apr_psprintf(cmd->temp_pool, 
                "Extra characters in LUT token %s", token);

        if (arr->nelts > 1) { // Fill in slope for the previous pair
            double slope =
                (value_out - APR_ARRAY_IDX(arr, arr->nelts - 1, double))
                / (value_in - APR_ARRAY_IDX(arr, arr->nelts - 2, double));
            APR_ARRAY_PUSH(arr, double) = slope;
        }

        APR_ARRAY_PUSH(arr, double) = value_in;
        APR_ARRAY_PUSH(arr, double) = value_out;
        token = apr_strtok(NULL, ",", &last);
    }
    // Push a zero for the last slope value, it will keep output values from overflowing
    APR_ARRAY_PUSH(arr, double) = 0.0;
    c->lut = arr;
    return nullptr;
}

static const char *read_config(cmd_parms *cmd, convert_conf *c, const char *src, const char *conf_name) {
    const char *err_message;
    const char *line; // temporary input
    // The input configuration file
    apr_table_t *kvp = readAHTSEConfig(cmd->temp_pool, src, &err_message);
    if (nullptr == kvp)
        return err_message;
    err_message = configRaster(cmd->pool, kvp, c->inraster);
    if (err_message)
        return err_message;

    // The output configuration file
    kvp = readAHTSEConfig(cmd->temp_pool, conf_name, &err_message);
    if (nullptr == kvp)
        return err_message;
    err_message = configRaster(cmd->pool, kvp, c->raster);
    if (err_message)
        return err_message;

    line = apr_table_get(kvp, "EmptyTile");
    if (nullptr != line) {
        err_message = readFile(cmd->pool, c->raster.missing.data, line);
        if (err_message)
            return err_message;
    }

    c->max_input_size = MAX_TILE_SIZE;
    line = apr_table_get(kvp, "InputBufferSize");
    if (line)
        c->max_input_size = static_cast<apr_size_t>(apr_strtoi64(line, nullptr, 0));

    // Single band, comma separated in:out value pairs
    if (nullptr != (line = apr_table_get(kvp, "LUT")) &&
        (err_message = read_lut(cmd, c, line)))
        return err_message;

    if (c->raster.datatype != c->inraster.datatype &&
        c->lut == nullptr)
        return "Data type conversion without LUT defined";

    return nullptr;
}

static const char *set_regexp(cmd_parms *cmd, convert_conf *c, const char *pattern) {
    return add_regexp_to_array(cmd->pool, &c->arr_rxp, pattern);
}

// Directive: Convert
static const char *check_config(cmd_parms *cmd, convert_conf *c, const char *value) {
    // Check the basic requirements
    if (!c->source)
        return "Convert_Source directive is required";

    // Dump the configuration in a string and return it, debug help
    if (!apr_strnatcasecmp(value, "verbose")) {
        return "Unimplemented";
    }

    return nullptr;
}

static const command_rec cmds[] =
{
    AP_INIT_TAKE2(
        "Convert_ConfigurationFiles",
        (cmd_func) read_config, // Callback
        0, // user_data
        ACCESS_CONF, // availability
        "Source and output configuration files"
    ),

    AP_INIT_TAKE1(
        "Convert_RegExp",
        (cmd_func) set_regexp,
        0, // user_data
        ACCESS_CONF, // availability
        "Regular expression for triggering mod_convert"
    ),

    AP_INIT_TAKE12(
        "Convert_Source",
        (cmd_func) set_source<convert_conf>,
        0,
        ACCESS_CONF,
        "Required, internal redirect path for the source"
    ),

    AP_INIT_FLAG(
        "Convert_Indirect",
        (cmd_func) ap_set_flag_slot,
        (void *)APR_OFFSETOF(convert_conf, indirect),
        ACCESS_CONF,
        "If set, the module does not respond to external requests, only to internal redirects"
    ),

    AP_INIT_TAKE1(
        "Convert",
        (cmd_func)check_config,
        0,
        ACCESS_CONF,
        "On to check the configuration, it should be the last Reproject directive in a given location."
        " Setting it to verbose will dump the configuration"
    ),

    { NULL }
};

static void register_hooks(apr_pool_t *p) {
    ap_hook_handler(handler, nullptr, nullptr, APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA convert_module = {
    STANDARD20_MODULE_STUFF,
    create_dir_config,
    NULL, // dir merge
    NULL, // server config
    NULL, // server merge
    cmds, // configuration directives
    register_hooks // processing hooks
};

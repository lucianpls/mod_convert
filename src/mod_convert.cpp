/*
* mod_convert.cpp
*
* Part of AHTSE, converts from one image format to another
*
* (C) Lucian Plesea 2018
*/

// #include <tuple>
// #include <vector>

#include <httpd.h>
#include <http_main.h>
#include <http_protocol.h>
#include <http_core.h>
#include <http_request.h>
#include <http_log.h>

#include <apr_strings.h>

#include "ahtse_util.h"

// From mod_receive
#include <receive_context.h>

// Standard headers
#include <unordered_map>
// #include <clocale>

extern module AP_MODULE_DECLARE_DATA convert_module;

#if defined(APLOG_USE_MODULE)
APLOG_USE_MODULE(convert);
#endif

// Max compressed input tile is 1MB
#define DEFAULT_INPUT_SIZE (1024 * 1024)

struct convert_conf {
    // Set if this module is to be used from indirect (not external) requests?
    int indirect;
    // array of guard regexp pointers, one of them has to match
    apr_array_header_t *arr_rxp;

    TiledRaster raster, inraster;
    // const char *source, *postfix;

    // internal path of source
    char *source;
    // append this to the end of request url to the input
    char *postfix;

    // the maximum size of an input tile
    apr_size_t max_input_size;

    // Table of doubles. In pairs, even entries are inputs, odd entries are outputs, in increasing order of input values
    apr_array_header_t *lut;

    // Meaning depends on output format
    double quality;
};

using namespace std;

// mapping of mime-types to known formats
// mime types, subtypes and parameters are case insensitive
static unordered_map<const char *, img_fmt> formats = {
    {"image/jpeg", IMG_JPEG},
    {"image/png", IMG_PNG},
    // TODO: This one is not right at all.  Proper media types seem to require a full parser
    // Also, the parameter is in key=value format, with the key being case insensitive
    // There is also the issue of where white-spaces are allowed
    {"image/jpeg; zen=true", IMG_JPEG_ZEN}
};

#define USER_AGENT "AHTSE Convert"

static int handler(request_rec *r)
{
    const char *message;
    if (r->method_number != M_GET || nullptr != r->args)
        return DECLINED;

    convert_conf *cfg = reinterpret_cast<convert_conf *>(
        ap_get_module_config(r->per_dir_config, &convert_module));

    // If indirect is set, only activate on subrequests
    if (cfg->indirect && r->main == nullptr)
        return DECLINED;

    if (nullptr == cfg || nullptr == cfg->arr_rxp || !requestMatches(r, cfg->arr_rxp))
        return DECLINED;

    apr_array_header_t *tokens = tokenize(r->pool, r->uri);
    if (tokens->nelts < 3)
        return DECLINED; // At least three values, for RLC

    // This is a request to be handled here

    // This is a server configuration error
    SERVER_ERR_IF(!ap_get_output_filter_handle("Receive"), r, "mod_receive not found");

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
        tile.y >= cfg->inraster.rsets[tile.l].w)
        return sendEmptyTile(r, cfg->raster.missing);

    // Convert to input level
    tile.l -= cfg->inraster.skip;

    // Incoming tile buffer
    receive_ctx rctx;
    rctx.maxsize = cfg->max_input_size;
    rctx.buffer = reinterpret_cast<char *>(apr_palloc(r->pool, rctx.maxsize));

    // Create the subrequest
    char *sub_uri = apr_pstrcat(r->pool,
        cfg->source,
        (tile.z == 0) ?
        apr_psprintf(r->pool, "/%d/%d/%d",
            static_cast<int>(tile.l),
            static_cast<int>(tile.y),
            static_cast<int>(tile.x)) :
        apr_psprintf(r->pool, "/%d/%d/%d/%d",
            static_cast<int>(tile.z),
            static_cast<int>(tile.l),
            static_cast<int>(tile.y),
            static_cast<int>(tile.x)),
        cfg->postfix,
        NULL);

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
    const char *ETag = apr_table_get(sr->headers_out, "ETag");
    if (ETag != nullptr)
        ETag = apr_pstrdup(r->pool, ETag);

    ap_destroy_sub_req(sr);
    if (rr_status != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_NOTICE, rr_status, r,
            "Receive failed with code %d for %s", rr_status, sub_uri);
        return sendEmptyTile(r, cfg->raster.missing);
    }

    // If the input tile is the empty tile, send the output empty tile right now
    if (ETag != nullptr && !ap_cstr_casecmp(ETag, cfg->inraster.missing.eTag))
        return sendEmptyTile(r, cfg->raster.missing);

    // What format is the source, and what is the compression we want?
    apr_uint32_t in_format;
    memcpy(&in_format, rctx.buffer, 4);

    codec_params params;
    memset(&params, 0, sizeof(params));
    int pixel_size = GDTGetSize(cfg->inraster.datatype);
    int input_line_width = static_cast<int>(cfg->inraster.pagesize.x *  cfg->inraster.pagesize.c * pixel_size);
    int pagesize = static_cast<int>(input_line_width * cfg->inraster.pagesize.y);
    params.line_stride = input_line_width;
    storage_manager src = { rctx.buffer, rctx.size };
    void *buffer = apr_pcalloc(r->pool, pagesize);

    if (JPEG_SIG == in_format) {
        message = jpeg_stride_decode(params, cfg->inraster, src, buffer);
    } 
    else { // format error
        message = "Unsupported input format";
    }

    if (message) {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "%s from %s", message, sub_uri);
        return HTTP_NOT_FOUND;
    }

    // If datatype conversion is needed, branch to that part
    //if (cfg->inraster.datatype != cfg->raster.datatype)
    //    return convert_dt(r, buffer);

    // This part is only for converting Zen JPEGs to JPNG, as needed
    if (JPEG_SIG == in_format && params.modified == 0) // Zen mask absent or superfluous
        return sendImage(r, src, "image/jpeg");

    png_params out_params;
    set_def_png_params(cfg->raster, &out_params);

    // By default the NDV is zero, and the NVD field is zero
    // Check one more time that we had a Zen mask before turning the transparency on
    if (params.modified)
        out_params.has_transparency = true;

    storage_manager raw = {reinterpret_cast<char *>(buffer), pagesize};
    storage_manager dst = {
        reinterpret_cast<char *>(apr_palloc(r->pool, cfg->max_input_size)),
        static_cast<int>(cfg->max_input_size)
    };
    SERVER_ERR_IF(dst.buffer == nullptr, r, "Memmory allocation error");

    message = png_encode(out_params, cfg->raster, raw, dst);
    SERVER_ERR_IF(message != nullptr, r, "%s from %s", message, r->uri);

    return sendImage(r, dst, "image/png");
}

static void *create_dir_config(apr_pool_t *p, char * /* path */)
{
    convert_conf *c = reinterpret_cast<convert_conf *>(apr_pcalloc(p, sizeof(convert_conf)));
    return c;
}

// Reads a sequence of in:out floating point pairs, separated by commas.
// Input values should be in increasing order
// Might need "C" locale
static const char *read_lut(cmd_parms *cmd, convert_conf *c, const char *lut) {
    apr_table_t *tokens = apr_table_make(cmd->temp_pool, 8);
    char *lut_string = apr_pstrdup(cmd->temp_pool, lut);
    char *last = nullptr;
    char *token = apr_strtok(lut_string, ",", &last);

    if (c->lut != nullptr)
        return "LUT redefined";

    apr_array_header_t *arr = apr_array_make(cmd->pool, 10, sizeof(double));

    char *sep=nullptr;
    while (token != nullptr) {
        double value_in = strtod(token, &sep);
        if (*sep++ != ':')
            return apr_psprintf(cmd->temp_pool, "Malformed LUT token %s", token);
        if (arr->nelts > 1 && APR_ARRAY_IDX(arr, arr->nelts - 2, double) >= value_in)
            return "Incorrect LUT, input values should be increasing";
        double value_out = strtod(sep, &sep);

        APR_ARRAY_PUSH(arr, double) = value_in;
        APR_ARRAY_PUSH(arr, double) = value_out;
        token = apr_strtok(NULL, ",", &last);
    }
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
    if (err_message != nullptr)
        return err_message;

    // The output configuration file
    kvp = readAHTSEConfig(cmd->temp_pool, conf_name, &err_message);
    if (nullptr == kvp)
        return err_message;

    err_message = configRaster(cmd->pool, kvp, c->raster);
    if (err_message != nullptr)
        return err_message;

    // Mandatory fields for convert
    if (nullptr == (line = apr_table_get(kvp, "SourcePath")))
        return "SourcePath missing";
    c->source = apr_pstrdup(cmd->pool, line);

    if (nullptr != (line = apr_table_get(kvp, "SourcePostfix")))
        c->postfix = apr_pstrdup(cmd->pool, line);

    if (nullptr != (line = apr_table_get(kvp, "EmptyTile"))
        && nullptr != (err_message = readFile(cmd->pool, c->raster.missing.empty, line)))
            return err_message;

    line = apr_table_get(kvp, "InputBufferSize");
    c->max_input_size = line ? static_cast<apr_size_t>(apr_strtoi64(line, NULL, 0)) :
        DEFAULT_INPUT_SIZE;

    // Single band, comma separated in:out value pairs
    if (nullptr != (line = apr_table_get(kvp, "LUT")) &&
        (err_message = read_lut(cmd, c, line)))
        return err_message;

    return nullptr;
}

static const char *set_regexp(cmd_parms *cmd, convert_conf *c, const char *pattern)
{
    return add_regexp_to_array(cmd->pool, &c->arr_rxp, pattern);
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
    AP_INIT_FLAG(
        "Convert_Indirect",
        (cmd_func) ap_set_flag_slot,
        (void *)APR_OFFSETOF(convert_conf, indirect),
        ACCESS_CONF,
        "If set, the module does not respond to external requests, only to internal redirects"
    ),
    { NULL }
};

static void register_hooks(apr_pool_t *p) {
    ap_hook_handler(handler, NULL, NULL, APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA convert_module = {
    STANDARD20_MODULE_STUFF,
    create_dir_config,
    0, // dir merge
    0, // server config
    0, // server merge
    cmds, // configuration directives
    register_hooks // processing hooks
};
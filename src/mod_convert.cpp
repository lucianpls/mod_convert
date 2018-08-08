/*
* mod_convert.cpp
*
* Part of AHTSE, converts from one image format to another
*
* (C) Lucian Plesea 2018
*/

#include <httpd.h>
#include <http_main.h>
#include <http_protocol.h>
#include <http_core.h>
#include <http_request.h>
#include <http_log.h>

#include <apr_strings.h>

#include "mod_convert.h"

// From mod_receive
#include <receive_context.h>

extern module AP_MODULE_DECLARE_DATA convert_module;

#if defined(APLOG_USE_MODULE)
APLOG_USE_MODULE(convert);
#endif

// using namespace std;

#define USER_AGENT "AHTSE Convert"
static int handler(request_rec *r)
{
    const char *message;
    if (r->method_number != M_GET || nullptr != r->args)
        return DECLINED;

    convert_conf *cfg = reinterpret_cast<convert_conf *>(
        ap_get_module_config(r->per_dir_config, &convert_module));

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
        return sendEmptyTile(r, cfg->empty);

    // Adjust the level to the full pyramid one
    tile.l += cfg->raster.skip;

    // Outside of bounds tile
    if (tile.l >= cfg->raster.n_levels ||
        tile.x >= cfg->raster.rsets[tile.l].w ||
        tile.y >= cfg->raster.rsets[tile.l].h)
        return sendEmptyTile(r, cfg->empty);

    // Same is true for outside of input bounds
    if (tile.l >= cfg->inraster.n_levels ||
        tile.x >= cfg->inraster.rsets[tile.l].w ||
        tile.y >= cfg->inraster.rsets[tile.l].w)
        return sendEmptyTile(r, cfg->empty);

    // Convert to input level
    tile.l -= cfg->inraster.skip;

    // Incoming tile buffer
    receive_ctx rctx;
    rctx.maxsize = cfg->max_input_size;
    rctx.buffer = reinterpret_cast<char *>(apr_palloc(r->pool, rctx.maxsize));

    // Start hooking up the input
    ap_filter_t *rf = ap_add_output_filter("Receive", &rctx, r, r->connection);

    // Create the subrequest
    char *sub_uri = apr_pstrcat(r->pool,
        cfg->source,
        (tile.z == 0) ?
        apr_psprintf(r->pool, "/%d/%d/%d",
            static_cast<int>(tile.l),
            static_cast<int>(tile.x),
            static_cast<int>(tile.y)) :
        apr_psprintf(r->pool, "/%d/%d/%d/%d",
            static_cast<int>(tile.l),
            static_cast<int>(tile.x),
            static_cast<int>(tile.y),
            static_cast<int>(tile.z)),
        cfg->postfix,
        NULL);

    request_rec *rr = ap_sub_req_lookup_uri(sub_uri, r, r->output_filters);

    const char *user_agent = apr_table_get(r->headers_in, "User-Agent");
    user_agent = (nullptr == user_agent) ?
        USER_AGENT : apr_pstrcat(r->pool, USER_AGENT ", ", user_agent, NULL);
    apr_table_setn(rr->headers_in, "User-Agent", user_agent);

    rctx.size = 0;
    int rr_status = ap_run_sub_req(rr);
    ap_remove_output_filter(rf);
    if (rr_status != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rr_status, r,
            "Receive failed for %s", sub_uri);
        // Pass error status along
        return rr_status;
    }

    // TODO : Build and compare ETags

    return DECLINED;
}

static void *create_dir_config(apr_pool_t *p, char * /* path */)
{
    convert_conf *c = reinterpret_cast<convert_conf *>(apr_pcalloc(p, sizeof(convert_conf)));
    return c;
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

    // Optional fields for convert
    int flag;
    line = apr_table_get(kvp, "ETagSeed");
    // Ignore the flag in the seed
    c->seed = line == nullptr ? 0 : base32decode(line, &flag);
    // Set the missing tile etag, with the flag set because it is the empty tile etag
    tobase32(c->seed, c->empty.eTag, 1);

    if (nullptr != (line = apr_table_get(kvp, "EmptyTile"))
        && nullptr != (err_message = readFile(cmd->pool, c->empty.empty, line)))
            return err_message;

    line = apr_table_get(kvp, "InputBufferSize");
    c->max_input_size = (line == nullptr) ? DEFAULT_INPUT_SIZE :
        static_cast<apr_size_t>(apr_strtoi64(line, NULL, 0));

    if (nullptr != (line = apr_table_get(kvp, "SourcePostfix")))
        c->postfix = apr_pstrdup(cmd->pool, line);

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
        "Regular expression for triggering mod_convert"),
    { NULL }
};

static void register_hooks(apr_pool_t *p) {
//    ap_hook_handler(handler, NULL, NULL, APR_HOOK_MIDDLE);
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
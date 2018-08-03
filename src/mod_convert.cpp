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

extern module AP_MODULE_DECLARE_DATA convert_module;

#if defined(APLOG_USE_MODULE)
APLOG_USE_MODULE(convert);
#endif

// From mod_receive
#include <receive_context.h>

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

    sz tile;
    memset(&tile, 0, sizeof(sz));

    tile.x = apr_atoi64(ARRAY_POP(tokens, char *)); RETURN_ERR_IF(errno);
    tile.y = apr_atoi64(ARRAY_POP(tokens, char *)); RETURN_ERR_IF(errno);
    tile.l = apr_atoi64(ARRAY_POP(tokens, char *)); RETURN_ERR_IF(errno);

    // Ignore the error on the M, it defaults to zero
    if (cfg->raster.size.z != 1 && tokens->nelts > 0)
        tile.z = apr_atoi64(ARRAY_POP(tokens, char *));

    // But we still need to check the results a bit
    if (tile.x < 0 || tile.y < 0 || tile.l < 0)
        return sendEmptyTile(r, cfg->empty);

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

    int flag;
    line = apr_table_get(kvp, "ETagSeed");
    // Ignore the flag in the seed
    c->seed = line == nullptr ? 0 : base32decode(line, &flag);
    // Set the missing tile etag, with the flag set because it is the empty tile etag
    tobase32(c->seed, c->empty.eTag, 1);

    if (nullptr != (line = apr_table_get(kvp, "EmptyTile"))
        && nullptr != (err_message = readFile(cmd->pool, c->empty.empty, line)))
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
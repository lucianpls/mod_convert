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
    return DECLINED;
}

static void *create_dir_config(apr_pool_t *p, char * /* path */)
{
    convert_conf *c = reinterpret_cast<convert_conf *>(apr_pcalloc(p, sizeof(convert_conf)));
    return c;
}

static const char *read_config(cmd_parms *cmd, convert_conf *c, const char *src, const char *conf_name) {
    const char *err_message;
    apr_table_t *kvp = read_pKVP_from_file(cmd->temp_pool, src, &err_message);
    if (nullptr == kvp)
        return err_message;

    err_message = configRaster(cmd->pool, kvp, c->inraster);
    if (err_message != nullptr)
        return err_message;

    return nullptr;
}

static const char *set_regexp(cmd_parms *cmd, convert_conf *c, const char *pattern)
{
    return add_regexp_to_array(cmd->pool, &c->arr_rxp, pattern);
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
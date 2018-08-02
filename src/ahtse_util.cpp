#define NOMINMAX 1
#include "ahtse_util.h"

// httpd.h includes the ap_ headers in the right order
// It should not be needed here
#include <httpd.h>
#include <http_config.h>
#include <apr_strings.h>
#include <ap_regex.h>

// strod
#include <cstdlib>
// setlocale
#include <clocale>

#include <algorithm>

// Given a data type name, returns a data type
GDALDataType getDT(const char *name)
{
    if (name == nullptr) return GDT_Byte;
    if (!apr_strnatcasecmp(name, "UINT16"))
        return GDT_UInt16;
    else if (!apr_strnatcasecmp(name, "INT16") || !apr_strnatcasecmp(name, "INT"))
        return GDT_Int16;
    else if (!apr_strnatcasecmp(name, "UINT32"))
        return GDT_UInt32;
    else if (!apr_strnatcasecmp(name, "INT32") || !apr_strnatcasecmp(name, "INT"))
        return GDT_Int32;
    else if (!apr_strnatcasecmp(name, "FLOAT32") || !apr_strnatcasecmp(name, "FLOAT"))
        return GDT_Float32;
    else if (!apr_strnatcasecmp(name, "FLOAT64") || !apr_strnatcasecmp(name, "DOUBLE"))
        return GDT_Float64;
    else
        return GDT_Byte;
}

// Returns NULL if it worked as expected, returns a four integer value from 
// "x y", "x y z" or "x y z c"
const char *get_xyzc_size(struct sz *size, const char *value)
{
    char *s;
    if (!(size && value))
        return " values missing";
    size->x = apr_strtoi64(value, &s, 0);
    size->y = apr_strtoi64(s, &s, 0);
    size->c = 3;
    size->z = 1;
    if (errno == 0 && *s != 0) {
        // Read optional third and fourth integers
        size->z = apr_strtoi64(s, &s, 0);
        if (*s != 0)
            size->c = apr_strtoi64(s, &s, 0);
    }
    if (errno != 0 || *s != 0) {
        // Raster size is 4 params max
        return " incorrect format";
    }
    return nullptr;
}

const char *add_regexp_to_array(apr_pool_t *p, apr_array_header_t **parr, const char *pattern)
{
    if (nullptr == *parr)
        *parr = apr_array_make(p, 2, sizeof(ap_rxplus_t *));
    ap_rxplus_t **m = reinterpret_cast<ap_rxplus_t **>(apr_array_push(*parr));
    *m = ap_rxplus_compile(p, pattern);
    return (nullptr != *m) ? nullptr : "Bad regular expression";
}

apr_table_t *read_pKVP_from_file(apr_pool_t *pool, const char *fname, const char **err_message)
{
    *err_message = nullptr;
    ap_configfile_t *cfg_file;

    apr_status_t s = ap_pcfg_openfile(&cfg_file, pool, fname);
    if (APR_SUCCESS != s) { // %pm prints error string from the status
        *err_message = apr_psprintf(pool, "%s - %pm", fname, &s);
        return nullptr;
    }

    char buffer[MAX_STRING_LEN]; // MAX_STRING_LEN is from httpd.h, 8192
    apr_table_t *table = apr_table_make(pool, 8);
    // This can return ENOSPC if input lines are too long
    while (APR_SUCCESS == (s = ap_cfg_getline(buffer, MAX_STRING_LEN, cfg_file))) {
        if (strlen(buffer) == 0 || buffer[0] == '#')
            continue;
        const char *value = buffer;
        char *key = ap_getword_white(pool, &value);
        apr_table_add(table, key, value);
    }

    ap_cfg_closefile(cfg_file);
    if (s == APR_ENOSPC) {
        *err_message = apr_psprintf(pool, "input line longer than %d", MAX_STRING_LEN);
        return nullptr;
    }

    return table;
}

static void init_rsets(apr_pool_t *pool, TiledRaster &raster) {
    ap_assert(raster.pagesize.z == 1);

    rset level;
    level.rx = (raster.bbox.xmax - raster.bbox.xmin) / raster.pagesize.x;
    level.ry = (raster.bbox.ymax - raster.bbox.ymin) / raster.pagesize.y;
    level.w = static_cast<int>(1 + (raster.size.x - 1) / raster.pagesize.x);
    level.h = static_cast<int>(1 + (raster.size.y - 1) / raster.pagesize.y);

    // How many levels are there?
    raster.n_levels = 2 + ilogb(std::max(level.w, level.h) - 1);
    raster.rsets = reinterpret_cast<rset *>(apr_pcalloc(pool,
        sizeof(rset) * raster.n_levels));

    // Populate rsets from the bottom up, the way tile protocols count levels
    // That way rset[0] matches the level 0
    // These are the MRF levels, some of the top ones might be skipped
    rset *r = &raster.rsets[raster.n_levels - 1];
    for (int i = 0; i < raster.n_levels; i++) {
        *r-- = level;
        level.w = 1 + (level.w - 1) / 2;
        level.h = 1 + (level.h - 1) / 2;
        level.rx *= 2;
        level.ry *= 2;
    }

    // MRF has to have only one tile on top
    ap_assert(raster.rsets[0].h * raster.rsets[0].w == 1);
    ap_assert(raster.n_levels > raster.skip);
}

// Initialize a raster from a kvp table
const char *configRaster(apr_pool_t *pool, apr_table_t *kvp, TiledRaster &raster)
{
    const char *line;
    const char *err_message;
    if (nullptr == (line = apr_table_get(kvp, "Size")))
        return "Size directive is mandatory";

    if (nullptr != (err_message = get_xyzc_size(&raster.size, line)))
        return apr_pstrcat(pool, "Size ", err_message, NULL);

    // Optional page size, default to 512x512x1xc
    raster.pagesize = {512, 512, 1, raster.size.c, raster.size.l};

    if (nullptr != (line = apr_table_get(kvp, "PageSize"))
        && nullptr != (err_message = get_xyzc_size(&raster.pagesize, line)))
            return apr_pstrcat(pool, "PageSize ", err_message, NULL);

    raster.datatype = getDT(apr_table_get(kvp, "DataType"));

    if (nullptr != (line = apr_table_get(kvp, "SkippedLevels")))
        raster.skip = int(apr_atoi64(line));

    if (nullptr != (line = apr_table_get(kvp, "Projection")))
        raster.projection = line ? apr_pstrdup(pool, line) : "WM";

    raster.bbox.xmin = raster.bbox.ymin = 0.0;
    raster.bbox.xmax = raster.bbox.ymax = 1.0;
    if (nullptr != (line = apr_table_get(kvp, "BoundingBox"))
        && nullptr != (err_message = getBBox(line, raster.bbox)))
            return apr_pstrcat(pool, "BoundingBox ", err_message, NULL);

    init_rsets(pool, raster);

    return nullptr;
}

const char *getBBox(const char *line, bbox_t &bbox)
{
    const char *lcl = setlocale(LC_NUMERIC, NULL);
    const char *message = "incorrect format, expecting four comma separated C locale numbers";
    char *l;
    setlocale(LC_NUMERIC, "C");
    bbox.xmin = strtod(line, &l); if (*l++ != ',') goto done;
    bbox.ymin = strtod(l, &l);    if (*l++ != ',') goto done;
    bbox.xmax = strtod(l, &l);    if (*l++ != ',') goto done;
    bbox.ymax = strtod(l, &l);    if (*l++ != ',') goto done;
    message = nullptr;

done:
    setlocale(LC_NUMERIC, lcl);
    return message;
}

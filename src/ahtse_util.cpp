#include "ahtse_util.h"

// httpd.h includes the ap_ headers in the right order
// It should not be needed here
#include <httpd.h>
#include <apr_strings.h>
#include <ap_regex.h>

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

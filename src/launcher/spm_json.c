/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: minimal ordered JSON DOM, parser and serializer.
 */

#include "spm_json.h"
#include "spm_util.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static spm_json *spm_json_new(spm_jtype t)
{
    spm_json *v = calloc(1, sizeof(*v));
    if (!v)
        abort();
    v->type = t;
    return v;
}

spm_json *spm_json_null(void)
{
    return spm_json_new(SPM_JNULL);
}

spm_json *spm_json_bool(int value)
{
    spm_json *v = spm_json_new(SPM_JBOOL);
    v->boolean = value ? 1 : 0;
    return v;
}

spm_json *spm_json_int(long long value)
{
    spm_json *v = spm_json_new(SPM_JNUM);
    v->is_int = 1;
    v->ival = value;
    v->dval = (double)value;
    return v;
}

spm_json *spm_json_double(double value)
{
    spm_json *v = spm_json_new(SPM_JNUM);
    v->dval = value;
    v->ival = (long long)value;
    return v;
}

spm_json *spm_json_string(const char *value)
{
    spm_json *v = spm_json_new(SPM_JSTR);
    v->str = spm_strdup(value ? value : "");
    return v;
}

spm_json *spm_json_array(void)
{
    return spm_json_new(SPM_JARR);
}

spm_json *spm_json_object(void)
{
    return spm_json_new(SPM_JOBJ);
}

void spm_json_free(spm_json *v)
{
    if (!v)
        return;
    free(v->str);
    for (size_t i = 0; i < v->count; i++) {
        spm_json_free(v->items[i]);
        if (v->keys)
            free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v);
}

static void spm_json_grow(spm_json *c)
{
    if (c->count < c->cap)
        return;
    size_t cap = c->cap ? c->cap * 2 : 8;
    spm_json **items = realloc(c->items, cap * sizeof(*items));
    if (!items)
        abort();
    c->items = items;
    if (c->type == SPM_JOBJ) {
        char **keys = realloc(c->keys, cap * sizeof(*keys));
        if (!keys)
            abort();
        c->keys = keys;
    }
    c->cap = cap;
}

spm_json *spm_json_clone(const spm_json *v)
{
    if (!v)
        return NULL;
    spm_json *c = spm_json_new(v->type);
    c->boolean = v->boolean;
    c->is_int = v->is_int;
    c->ival = v->ival;
    c->dval = v->dval;
    if (v->str)
        c->str = spm_strdup(v->str);
    for (size_t i = 0; i < v->count; i++) {
        if (v->type == SPM_JOBJ)
            spm_json_set(c, v->keys[i], spm_json_clone(v->items[i]));
        else
            spm_json_append(c, spm_json_clone(v->items[i]));
    }
    return c;
}

void spm_json_append(spm_json *arr, spm_json *item)
{
    if (!arr || arr->type != SPM_JARR) {
        spm_json_free(item);
        return;
    }
    spm_json_grow(arr);
    arr->items[arr->count++] = item;
}

void spm_json_set(spm_json *obj, const char *key, spm_json *item)
{
    if (!obj || obj->type != SPM_JOBJ) {
        spm_json_free(item);
        return;
    }
    for (size_t i = 0; i < obj->count; i++) {
        if (strcmp(obj->keys[i], key) == 0) {
            spm_json_free(obj->items[i]);
            obj->items[i] = item;
            return;
        }
    }
    spm_json_grow(obj);
    obj->keys[obj->count] = spm_strdup(key);
    obj->items[obj->count++] = item;
}

void spm_json_set_str(spm_json *obj, const char *key, const char *value)
{
    spm_json_set(obj, key, spm_json_string(value));
}

void spm_json_set_bool(spm_json *obj, const char *key, int value)
{
    spm_json_set(obj, key, spm_json_bool(value));
}

void spm_json_set_int(spm_json *obj, const char *key, long long value)
{
    spm_json_set(obj, key, spm_json_int(value));
}

spm_json *spm_json_get(const spm_json *obj, const char *key)
{
    if (!obj || obj->type != SPM_JOBJ)
        return NULL;
    for (size_t i = 0; i < obj->count; i++) {
        if (strcmp(obj->keys[i], key) == 0)
            return obj->items[i];
    }
    return NULL;
}

spm_json *spm_json_path(const spm_json *root, const char *dotted_path)
{
    const spm_json *cur = root;
    const char *p = dotted_path;
    while (cur && *p) {
        const char *dot = strchr(p, '.');
        size_t n = dot ? (size_t)(dot - p) : strlen(p);
        char *key = spm_strndup(p, n);
        cur = spm_json_get(cur, key);
        free(key);
        p = dot ? dot + 1 : p + n;
    }
    return (spm_json *)cur;
}

int spm_json_truthy(const spm_json *v, int fallback)
{
    if (!v || v->type == SPM_JNULL)
        return v ? 0 : fallback;
    switch (v->type) {
    case SPM_JBOOL:
        return v->boolean;
    case SPM_JNUM:
        return v->is_int ? v->ival != 0 : v->dval != 0.0;
    case SPM_JSTR:
        return v->str[0] != '\0';
    case SPM_JARR:
    case SPM_JOBJ:
        return v->count > 0;
    default:
        return fallback;
    }
}

const char *spm_json_str(const spm_json *v, const char *fallback)
{
    if (v && v->type == SPM_JSTR)
        return v->str;
    return fallback;
}

long long spm_json_num(const spm_json *v, long long fallback)
{
    if (v && v->type == SPM_JNUM)
        return v->is_int ? v->ival : (long long)v->dval;
    if (v && v->type == SPM_JSTR) {
        char *end = NULL;
        long long n = strtoll(v->str, &end, 10);
        if (end && end != v->str && *end == '\0')
            return n;
    }
    return fallback;
}

int spm_json_equal(const spm_json *a, const spm_json *b)
{
    if (!a || !b)
        return a == b;
    if (a->type != b->type)
        return 0;
    switch (a->type) {
    case SPM_JNULL:
        return 1;
    case SPM_JBOOL:
        return a->boolean == b->boolean;
    case SPM_JNUM:
        return a->dval == b->dval;
    case SPM_JSTR:
        return strcmp(a->str, b->str) == 0;
    case SPM_JARR:
        if (a->count != b->count)
            return 0;
        for (size_t i = 0; i < a->count; i++)
            if (!spm_json_equal(a->items[i], b->items[i]))
                return 0;
        return 1;
    case SPM_JOBJ:
        if (a->count != b->count)
            return 0;
        for (size_t i = 0; i < a->count; i++)
            if (!spm_json_equal(a->items[i], spm_json_get(b, a->keys[i])))
                return 0;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- parser */

typedef struct
{
    const char *s;
    size_t len;
    size_t pos;
    int depth;
} spm_parser;

static void spm_skip_ws(spm_parser *p)
{
    while (p->pos < p->len && (p->s[p->pos] == ' ' || p->s[p->pos] == '\t' ||
                               p->s[p->pos] == '\n' || p->s[p->pos] == '\r'))
        p->pos++;
}

static spm_json *spm_parse_value(spm_parser *p);

static int spm_hex4(spm_parser *p, unsigned *out)
{
    if (p->pos + 4 > p->len)
        return -1;
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p->s[p->pos + (size_t)i];
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= (unsigned)(c - 'A' + 10);
        else
            return -1;
    }
    p->pos += 4;
    *out = v;
    return 0;
}

static void spm_put_utf8(spm_buf *b, unsigned cp)
{
    if (cp < 0x80)
        spm_buf_putc(b, (char)cp);
    else if (cp < 0x800) {
        spm_buf_putc(b, (char)(0xC0 | (cp >> 6)));
        spm_buf_putc(b, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        spm_buf_putc(b, (char)(0xE0 | (cp >> 12)));
        spm_buf_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        spm_buf_putc(b, (char)(0x80 | (cp & 0x3F)));
    } else {
        spm_buf_putc(b, (char)(0xF0 | (cp >> 18)));
        spm_buf_putc(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
        spm_buf_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        spm_buf_putc(b, (char)(0x80 | (cp & 0x3F)));
    }
}

static char *spm_parse_string_raw(spm_parser *p)
{
    if (p->pos >= p->len || p->s[p->pos] != '"')
        return NULL;
    p->pos++;
    spm_buf b;
    spm_buf_init(&b);
    while (p->pos < p->len) {
        char c = p->s[p->pos++];
        if (c == '"')
            return spm_buf_steal(&b);
        if (c != '\\') {
            spm_buf_putc(&b, c);
            continue;
        }
        if (p->pos >= p->len)
            break;
        char e = p->s[p->pos++];
        switch (e) {
        case '"':
        case '\\':
        case '/':
            spm_buf_putc(&b, e);
            break;
        case 'b':
            spm_buf_putc(&b, '\b');
            break;
        case 'f':
            spm_buf_putc(&b, '\f');
            break;
        case 'n':
            spm_buf_putc(&b, '\n');
            break;
        case 'r':
            spm_buf_putc(&b, '\r');
            break;
        case 't':
            spm_buf_putc(&b, '\t');
            break;
        case 'u': {
            unsigned cp;
            if (spm_hex4(p, &cp) != 0)
                goto fail;
            if (cp >= 0xD800 && cp <= 0xDBFF && p->pos + 6 <= p->len && p->s[p->pos] == '\\' &&
                p->s[p->pos + 1] == 'u') {
                unsigned lo;
                p->pos += 2;
                if (spm_hex4(p, &lo) != 0)
                    goto fail;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
            spm_put_utf8(&b, cp);
            break;
        }
        default:
            goto fail;
        }
    }
fail:
    spm_buf_free(&b);
    return NULL;
}

static spm_json *spm_parse_value(spm_parser *p)
{
    spm_skip_ws(p);
    if (p->pos >= p->len || p->depth > 200)
        return NULL;
    char c = p->s[p->pos];
    if (c == '{') {
        p->pos++;
        p->depth++;
        spm_json *obj = spm_json_object();
        spm_skip_ws(p);
        if (p->pos < p->len && p->s[p->pos] == '}') {
            p->pos++;
            p->depth--;
            return obj;
        }
        for (;;) {
            spm_skip_ws(p);
            char *key = spm_parse_string_raw(p);
            if (!key)
                goto obj_fail;
            spm_skip_ws(p);
            if (p->pos >= p->len || p->s[p->pos] != ':') {
                free(key);
                goto obj_fail;
            }
            p->pos++;
            spm_json *val = spm_parse_value(p);
            if (!val) {
                free(key);
                goto obj_fail;
            }
            spm_json_set(obj, key, val);
            free(key);
            spm_skip_ws(p);
            if (p->pos < p->len && p->s[p->pos] == ',') {
                p->pos++;
                continue;
            }
            if (p->pos < p->len && p->s[p->pos] == '}') {
                p->pos++;
                p->depth--;
                return obj;
            }
            goto obj_fail;
        }
    obj_fail:
        spm_json_free(obj);
        return NULL;
    }
    if (c == '[') {
        p->pos++;
        p->depth++;
        spm_json *arr = spm_json_array();
        spm_skip_ws(p);
        if (p->pos < p->len && p->s[p->pos] == ']') {
            p->pos++;
            p->depth--;
            return arr;
        }
        for (;;) {
            spm_json *val = spm_parse_value(p);
            if (!val) {
                spm_json_free(arr);
                return NULL;
            }
            spm_json_append(arr, val);
            spm_skip_ws(p);
            if (p->pos < p->len && p->s[p->pos] == ',') {
                p->pos++;
                continue;
            }
            if (p->pos < p->len && p->s[p->pos] == ']') {
                p->pos++;
                p->depth--;
                return arr;
            }
            spm_json_free(arr);
            return NULL;
        }
    }
    if (c == '"') {
        char *s = spm_parse_string_raw(p);
        if (!s)
            return NULL;
        spm_json *v = spm_json_new(SPM_JSTR);
        v->str = s;
        return v;
    }
    if (p->len - p->pos >= 4 && strncmp(p->s + p->pos, "true", 4) == 0) {
        p->pos += 4;
        return spm_json_bool(1);
    }
    if (p->len - p->pos >= 5 && strncmp(p->s + p->pos, "false", 5) == 0) {
        p->pos += 5;
        return spm_json_bool(0);
    }
    if (p->len - p->pos >= 4 && strncmp(p->s + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return spm_json_null();
    }
    if (c == '-' || isdigit((unsigned char)c)) {
        size_t start = p->pos;
        int is_float = 0;
        p->pos++;
        while (p->pos < p->len) {
            char d = p->s[p->pos];
            if (isdigit((unsigned char)d))
                p->pos++;
            else if (d == '.' || d == 'e' || d == 'E' || d == '+' || d == '-') {
                is_float = 1;
                p->pos++;
            } else
                break;
        }
        char *num = spm_strndup(p->s + start, p->pos - start);
        spm_json *v = is_float ? spm_json_double(strtod(num, NULL)) : spm_json_int(strtoll(num, NULL, 10));
        free(num);
        return v;
    }
    return NULL;
}

spm_json *spm_json_parse(const char *text, size_t len)
{
    if (!text)
        return NULL;
    spm_parser p = { text, len, 0, 0 };
    spm_json *v = spm_parse_value(&p);
    if (!v)
        return NULL;
    spm_skip_ws(&p);
    if (p.pos != p.len) {
        spm_json_free(v);
        return NULL;
    }
    return v;
}

/* ------------------------------------------------------------ serializer */

static void spm_dump_string(spm_buf *b, const char *s)
{
    spm_buf_putc(b, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':
            spm_buf_puts(b, "\\\"");
            break;
        case '\\':
            spm_buf_puts(b, "\\\\");
            break;
        case '\n':
            spm_buf_puts(b, "\\n");
            break;
        case '\r':
            spm_buf_puts(b, "\\r");
            break;
        case '\t':
            spm_buf_puts(b, "\\t");
            break;
        case '\b':
            spm_buf_puts(b, "\\b");
            break;
        case '\f':
            spm_buf_puts(b, "\\f");
            break;
        default:
            if (*p < 0x20)
                spm_buf_printf(b, "\\u%04x", *p);
            else
                spm_buf_putc(b, (char)*p);
        }
    }
    spm_buf_putc(b, '"');
}

static void spm_dump_indent(spm_buf *b, int indent, int level)
{
    if (indent <= 0)
        return;
    spm_buf_putc(b, '\n');
    for (int i = 0; i < indent * level; i++)
        spm_buf_putc(b, ' ');
}

static void spm_dump_value(spm_buf *b, const spm_json *v, int indent, int level)
{
    if (!v) {
        spm_buf_puts(b, "null");
        return;
    }
    switch (v->type) {
    case SPM_JNULL:
        spm_buf_puts(b, "null");
        break;
    case SPM_JBOOL:
        spm_buf_puts(b, v->boolean ? "true" : "false");
        break;
    case SPM_JNUM:
        if (v->is_int)
            spm_buf_printf(b, "%lld", v->ival);
        else if (!isfinite(v->dval))
            spm_buf_puts(b, "null");
        else {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%.10g", v->dval);
            spm_buf_puts(b, tmp);
            if (!strpbrk(tmp, ".eE"))
                spm_buf_puts(b, ".0");
        }
        break;
    case SPM_JSTR:
        spm_dump_string(b, v->str);
        break;
    case SPM_JARR:
        if (v->count == 0) {
            spm_buf_puts(b, "[]");
            break;
        }
        spm_buf_putc(b, '[');
        for (size_t i = 0; i < v->count; i++) {
            if (i)
                spm_buf_puts(b, indent > 0 ? "," : ", ");
            spm_dump_indent(b, indent, level + 1);
            spm_dump_value(b, v->items[i], indent, level + 1);
        }
        spm_dump_indent(b, indent, level);
        spm_buf_putc(b, ']');
        break;
    case SPM_JOBJ:
        if (v->count == 0) {
            spm_buf_puts(b, "{}");
            break;
        }
        spm_buf_putc(b, '{');
        for (size_t i = 0; i < v->count; i++) {
            if (i)
                spm_buf_puts(b, indent > 0 ? "," : ", ");
            spm_dump_indent(b, indent, level + 1);
            spm_dump_string(b, v->keys[i]);
            spm_buf_puts(b, ": ");
            spm_dump_value(b, v->items[i], indent, level + 1);
        }
        spm_dump_indent(b, indent, level);
        spm_buf_putc(b, '}');
        break;
    }
}

char *spm_json_dump(const spm_json *v, int indent)
{
    spm_buf b;
    spm_buf_init(&b);
    spm_dump_value(&b, v, indent, 0);
    return spm_buf_steal(&b);
}

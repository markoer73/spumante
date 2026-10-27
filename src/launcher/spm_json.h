/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: minimal ordered JSON DOM, parser and serializer.
 */

#ifndef SPUMANTE_SPM_JSON_H
#define SPUMANTE_SPM_JSON_H

#include <stddef.h>

typedef enum
{
    SPM_JNULL,
    SPM_JBOOL,
    SPM_JNUM,
    SPM_JSTR,
    SPM_JARR,
    SPM_JOBJ
} spm_jtype;

typedef struct spm_json spm_json;

struct spm_json
{
    spm_jtype type;
    int boolean;
    int is_int;
    long long ival;
    double dval;
    char *str;
    size_t count;
    size_t cap;
    spm_json **items;
    char **keys;
};

spm_json *spm_json_null(void);
spm_json *spm_json_bool(int value);
spm_json *spm_json_int(long long value);
spm_json *spm_json_double(double value);
spm_json *spm_json_string(const char *value);
spm_json *spm_json_array(void);
spm_json *spm_json_object(void);
void spm_json_free(spm_json *v);
spm_json *spm_json_clone(const spm_json *v);

/* Ownership of `item` is transferred to the container. */
void spm_json_append(spm_json *arr, spm_json *item);
void spm_json_set(spm_json *obj, const char *key, spm_json *item);
void spm_json_set_str(spm_json *obj, const char *key, const char *value);
void spm_json_set_bool(spm_json *obj, const char *key, int value);
void spm_json_set_int(spm_json *obj, const char *key, long long value);

spm_json *spm_json_get(const spm_json *obj, const char *key);
spm_json *spm_json_path(const spm_json *root, const char *dotted_path);
int spm_json_truthy(const spm_json *v, int fallback);
const char *spm_json_str(const spm_json *v, const char *fallback);
long long spm_json_num(const spm_json *v, long long fallback);
int spm_json_equal(const spm_json *a, const spm_json *b);

spm_json *spm_json_parse(const char *text, size_t len);
/* indent <= 0 produces compact output; indent 2 mirrors Python's json.dumps(indent=2). */
char *spm_json_dump(const spm_json *v, int indent);

#endif /* SPUMANTE_SPM_JSON_H */

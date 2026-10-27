/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: string buffers, filesystem and process helpers.
 */

#ifndef SPUMANTE_SPM_UTIL_H
#define SPUMANTE_SPM_UTIL_H

#include <stddef.h>
#include <sys/types.h>

typedef struct
{
    char *data;
    size_t len;
    size_t cap;
} spm_buf;

void spm_buf_init(spm_buf *b);
void spm_buf_append(spm_buf *b, const char *s, size_t n);
void spm_buf_puts(spm_buf *b, const char *s);
void spm_buf_putc(spm_buf *b, char c);
void spm_buf_printf(spm_buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
char *spm_buf_steal(spm_buf *b);
void spm_buf_free(spm_buf *b);

char *spm_strdup(const char *s);
char *spm_strndup(const char *s, size_t n);
char *spm_asprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *spm_path_join(const char *a, const char *b);
char *spm_dirname(const char *path);
const char *spm_basename(const char *path);
int spm_has_suffix_ci(const char *str, const char *suffix);
int spm_str_contains_ci(const char *hay, size_t hay_len, const char *needle);
void *spm_memmem(const void *hay, size_t hay_len, const void *needle, size_t needle_len);

const char *spm_home_dir(void);
char *spm_expand_user(const char *path);
char *spm_abs_path(const char *path);

int spm_is_file(const char *path);
int spm_is_dir(const char *path);
int spm_is_executable_file(const char *path);
int spm_mkdir_p(const char *path, mode_t mode);
unsigned char *spm_read_file(const char *path, size_t *out_len);
int spm_write_file(const char *path, const void *data, size_t len, mode_t mode);

/*
 * Run argv (argv[0] resolved via PATH when it has no slash) and capture stdout.
 * envp may be NULL to inherit the environment. stdin_data (may be NULL) is written to the child's stdin.
 * timeout_sec <= 0 waits forever. Returns 0 when the child was spawned and reaped; *out_status receives the
 * wait status (or -1 on timeout). Returns -1 when the child could not be spawned.
 */
int spm_run_capture(char *const argv[], char *const envp[], const char *cwd, const char *stdin_data,
                    int timeout_sec, char **out_stdout, int *out_status);

/* Build a NULL-terminated environment array from the current environment with overrides ("KEY=VALUE"). */
char **spm_env_with(const char *const *overrides, size_t n_overrides);
void spm_env_free(char **envp);

#endif /* SPUMANTE_SPM_UTIL_H */

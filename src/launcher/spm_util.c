/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: string buffers, filesystem and process helpers.
 */

#include "spm_util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <crt_externs.h>
#define SPM_ENVIRON (*_NSGetEnviron())
#else
extern char **environ;
#define SPM_ENVIRON environ
#endif

void spm_buf_init(spm_buf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void spm_buf_reserve(spm_buf *b, size_t extra)
{
    size_t need = b->len + extra + 1;
    if (need <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < need)
        cap *= 2;
    char *p = realloc(b->data, cap);
    if (!p)
        abort();
    b->data = p;
    b->cap = cap;
}

void spm_buf_append(spm_buf *b, const char *s, size_t n)
{
    spm_buf_reserve(b, n);
    if (n)
        memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void spm_buf_puts(spm_buf *b, const char *s)
{
    spm_buf_append(b, s, strlen(s));
}

void spm_buf_putc(spm_buf *b, char c)
{
    spm_buf_append(b, &c, 1);
}

void spm_buf_printf(spm_buf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n > 0) {
        spm_buf_reserve(b, (size_t)n);
        vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap2);
        b->len += (size_t)n;
    }
    va_end(ap2);
}

char *spm_buf_steal(spm_buf *b)
{
    if (!b->data) {
        char *empty = malloc(1);
        if (!empty)
            abort();
        empty[0] = '\0';
        return empty;
    }
    char *d = b->data;
    b->data = NULL;
    b->len = b->cap = 0;
    return d;
}

void spm_buf_free(spm_buf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

char *spm_strdup(const char *s)
{
    return spm_strndup(s ? s : "", s ? strlen(s) : 0);
}

char *spm_strndup(const char *s, size_t n)
{
    char *d = malloc(n + 1);
    if (!d)
        abort();
    if (n)
        memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

char *spm_asprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0)
        n = 0;
    char *d = malloc((size_t)n + 1);
    if (!d)
        abort();
    vsnprintf(d, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return d;
}

char *spm_path_join(const char *a, const char *b)
{
    size_t la = strlen(a);
    if (la && a[la - 1] == '/')
        return spm_asprintf("%s%s", a, b);
    return spm_asprintf("%s/%s", a, b);
}

char *spm_dirname(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash)
        return spm_strdup(".");
    if (slash == path)
        return spm_strdup("/");
    return spm_strndup(path, (size_t)(slash - path));
}

const char *spm_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int spm_has_suffix_ci(const char *str, const char *suffix)
{
    size_t ls = strlen(str), lf = strlen(suffix);
    return ls >= lf && strcasecmp(str + ls - lf, suffix) == 0;
}

int spm_str_contains_ci(const char *hay, size_t hay_len, const char *needle)
{
    size_t nl = strlen(needle);
    if (nl == 0)
        return 1;
    if (hay_len < nl)
        return 0;
    for (size_t i = 0; i + nl <= hay_len; i++) {
        size_t j = 0;
        while (j < nl && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j]))
            j++;
        if (j == nl)
            return 1;
    }
    return 0;
}

void *spm_memmem(const void *hay, size_t hay_len, const void *needle, size_t needle_len)
{
    if (needle_len == 0)
        return (void *)hay;
    if (hay_len < needle_len)
        return NULL;
    const unsigned char *h = hay;
    const unsigned char *n = needle;
    for (size_t i = 0; i + needle_len <= hay_len; i++) {
        if (h[i] == n[0] && memcmp(h + i, n, needle_len) == 0)
            return (void *)(h + i);
    }
    return NULL;
}

const char *spm_home_dir(void)
{
    const char *home = getenv("HOME");
    if (home && home[0])
        return home;
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir)
        return pw->pw_dir;
    return "/";
}

char *spm_expand_user(const char *path)
{
    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0'))
        return spm_asprintf("%s%s", spm_home_dir(), path + 1);
    return spm_strdup(path);
}

char *spm_abs_path(const char *path)
{
    char *expanded = spm_expand_user(path);
    char resolved[PATH_MAX];
    if (realpath(expanded, resolved)) {
        free(expanded);
        return spm_strdup(resolved);
    }
    if (expanded[0] == '/')
        return expanded;
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        return expanded;
    }
    char *joined = spm_path_join(cwd, expanded);
    free(expanded);
    return joined;
}

int spm_is_file(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int spm_is_dir(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int spm_is_executable_file(const char *path)
{
    return spm_is_file(path) && access(path, X_OK) == 0;
}

int spm_mkdir_p(const char *path, mode_t mode)
{
    char *tmp = spm_strdup(path);
    size_t len = strlen(tmp);
    if (len && tmp[len - 1] == '/')
        tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                free(tmp);
                return -1;
            }
            *p = '/';
        }
    }
    int rc = (mkdir(tmp, mode) != 0 && errno != EEXIST) ? -1 : 0;
    free(tmp);
    return rc;
}

unsigned char *spm_read_file(const char *path, size_t *out_len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    spm_buf b;
    spm_buf_init(&b);
    char chunk[65536];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            spm_buf_free(&b);
            return NULL;
        }
        if (n == 0)
            break;
        spm_buf_append(&b, chunk, (size_t)n);
    }
    close(fd);
    if (out_len)
        *out_len = b.len;
    return (unsigned char *)spm_buf_steal(&b);
}

int spm_write_file(const char *path, const void *data, size_t len, mode_t mode)
{
    char *dir = spm_dirname(path);
    spm_mkdir_p(dir, 0755);
    free(dir);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0)
        return -1;
    const char *p = data;
    size_t left = len;
    while (left) {
        ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return -1;
        }
        p += n;
        left -= (size_t)n;
    }
    close(fd);
    chmod(path, mode);
    return 0;
}

static double spm_now_seconds(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

int spm_run_capture(char *const argv[], char *const envp[], const char *cwd, const char *stdin_data,
                    int timeout_sec, char **out_stdout, int *out_status)
{
    int out_pipe[2], in_pipe[2] = { -1, -1 };
    if (pipe(out_pipe) != 0)
        return -1;
    if (stdin_data && pipe(in_pipe) != 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        if (stdin_data) {
            close(in_pipe[0]);
            close(in_pipe[1]);
        }
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        if (stdin_data) {
            dup2(in_pipe[0], STDIN_FILENO);
            close(in_pipe[0]);
            close(in_pipe[1]);
        } else {
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        }
        int devnull_err = open("/dev/null", O_WRONLY);
        if (devnull_err >= 0) {
            dup2(devnull_err, STDERR_FILENO);
            close(devnull_err);
        }
        if (cwd && chdir(cwd) != 0)
            _exit(127);
        char *const *env = envp ? envp : SPM_ENVIRON;
        if (strchr(argv[0], '/'))
            execve(argv[0], argv, env);
        else {
            const char *path_env = getenv("PATH");
            char *paths = spm_strdup(path_env ? path_env : "/usr/bin:/bin");
            for (char *tok = strtok(paths, ":"); tok; tok = strtok(NULL, ":")) {
                char *cand = spm_path_join(tok, argv[0]);
                if (access(cand, X_OK) == 0)
                    execve(cand, argv, env);
                free(cand);
            }
        }
        _exit(127);
    }

    close(out_pipe[1]);
    if (stdin_data) {
        close(in_pipe[0]);
        size_t left = strlen(stdin_data);
        const char *p = stdin_data;
        signal(SIGPIPE, SIG_IGN);
        while (left) {
            ssize_t n = write(in_pipe[1], p, left);
            if (n <= 0)
                break;
            p += n;
            left -= (size_t)n;
        }
        close(in_pipe[1]);
    }

    spm_buf out;
    spm_buf_init(&out);
    double deadline = timeout_sec > 0 ? spm_now_seconds() + timeout_sec : 0;
    int timed_out = 0;
    int child_reaped = 0;
    int status = 0;
    char chunk[16384];
    for (;;) {
        int wait_ms = 50;
        if (timeout_sec > 0) {
            double remaining = deadline - spm_now_seconds();
            if (remaining <= 0) {
                timed_out = 1;
                break;
            }
            int rem_ms = (int)(remaining * 1000.0) + 1;
            if (rem_ms < wait_ms)
                wait_ms = rem_ms;
        }
        struct pollfd pfd = { out_pipe[0], POLLIN, 0 };
        int pr = poll(&pfd, 1, wait_ms);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (pr > 0) {
            ssize_t n = read(out_pipe[0], chunk, sizeof(chunk));
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            spm_buf_append(&out, chunk, (size_t)n);
        }
        if (!child_reaped) {
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) {
                child_reaped = 1;
                for (;;) {
                    struct pollfd dpfd = { out_pipe[0], POLLIN, 0 };
                    if (poll(&dpfd, 1, 0) <= 0 || !(dpfd.revents & POLLIN))
                        break;
                    ssize_t dn = read(out_pipe[0], chunk, sizeof(chunk));
                    if (dn <= 0)
                        break;
                    spm_buf_append(&out, chunk, (size_t)dn);
                }
                break;
            }
        }
    }
    close(out_pipe[0]);

    if (timed_out) {
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
        if (!child_reaped)
            waitpid(pid, &status, 0);
        status = -1;
    } else if (!child_reaped) {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    }
    if (out_status)
        *out_status = status;
    if (out_stdout)
        *out_stdout = spm_buf_steal(&out);
    else
        spm_buf_free(&out);
    return 0;
}

char **spm_env_with(const char *const *overrides, size_t n_overrides)
{
    size_t count = 0;
    for (char **e = SPM_ENVIRON; e && *e; e++)
        count++;
    char **env = calloc(count + n_overrides + 1, sizeof(char *));
    if (!env)
        abort();
    size_t idx = 0;
    for (char **e = SPM_ENVIRON; e && *e; e++) {
        const char *eq = strchr(*e, '=');
        size_t key_len = eq ? (size_t)(eq - *e) : strlen(*e);
        int overridden = 0;
        for (size_t i = 0; i < n_overrides; i++) {
            const char *ov = overrides[i][0] == '!' ? overrides[i] + 1 : overrides[i];
            const char *oeq = strchr(ov, '=');
            size_t okey = oeq ? (size_t)(oeq - ov) : strlen(ov);
            if (okey == key_len && strncmp(*e, ov, key_len) == 0) {
                overridden = 1;
                break;
            }
        }
        if (!overridden)
            env[idx++] = spm_strdup(*e);
    }
    for (size_t i = 0; i < n_overrides; i++) {
        /* A leading '!' ("!KEY") removes the variable instead of setting it */
        if (overrides[i][0] == '!')
            continue;
        env[idx++] = spm_strdup(overrides[i]);
    }
    env[idx] = NULL;
    return env;
}

void spm_env_free(char **envp)
{
    if (!envp)
        return;
    for (char **e = envp; *e; e++)
        free(*e);
    free(envp);
}

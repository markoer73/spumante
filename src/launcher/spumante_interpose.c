/*
 * SPDX-License-Identifier: Apache-2.0
 * Mach-O __DATA,__interpose library for transparent POSIX execve/posix_spawn of .exe/.msi files.
 * Strictly enforces the +x executable bit before dispatching to Spumante.
 *
 * The Spumante launcher binary is discovered at runtime (never compiled in):
 *   1. $SPUMANTE_APP_BIN
 *   2. <directory of this dylib>/Spumante.app/Contents/MacOS/Spumante   (builds/ layout)
 *   3. <directory of this dylib>/../MacOS/Spumante                      (dylib shipped inside the bundle)
 *   4. /Applications/Spumante.app/Contents/MacOS/Spumante                (standard install location)
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

struct interpose_s
{
    void *replacement;
    void *original;
};

static const char *spumante_app_bin(void)
{
    static char path[PATH_MAX];
    static int resolved;
    if (resolved)
        return path[0] ? path : NULL;
    resolved = 1;

    const char *env = getenv("SPUMANTE_APP_BIN");
    if (env && env[0]) {
        snprintf(path, sizeof(path), "%s", env);
        return path;
    }
    Dl_info info;
    if (dladdr((const void *)&spumante_app_bin, &info) && info.dli_fname) {
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", info.dli_fname);
        char *slash = strrchr(dir, '/');
        if (slash)
            *slash = '\0';
        snprintf(path, sizeof(path), "%s/Spumante.app/Contents/MacOS/Spumante", dir);
        if (access(path, X_OK) == 0)
            return path;
        snprintf(path, sizeof(path), "%s/../MacOS/Spumante", dir);
        if (access(path, X_OK) == 0)
            return path;
    }
    snprintf(path, sizeof(path), "/Applications/Spumante.app/Contents/MacOS/Spumante");
    if (access(path, X_OK) == 0)
        return path;
    path[0] = '\0';
    return NULL;
}

static int has_win_ext(const char *path)
{
    if (!path)
        return 0;
    size_t len = strlen(path);
    if (len < 4)
        return 0;
    const char *ext = path + len - 4;
    return (strcasecmp(ext, ".exe") == 0 ||
            strcasecmp(ext, ".msi") == 0 ||
            strcasecmp(ext, ".com") == 0 ||
            strcasecmp(ext, ".scr") == 0);
}

static int is_pe_or_msi_file(const char *path, int *out_has_plus_x)
{
    *out_has_plus_x = 0;
    if (!path)
        return 0;

    const char *bypass = getenv("SPUMANTE_INTERPOSE_BYPASS");
    if (bypass && strcmp(bypass, "1") == 0)
        return 0;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return 0;

    if ((st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0 && access(path, X_OK) == 0) {
        *out_has_plus_x = 1;
    }

    if (has_win_ext(path))
        return 1;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    unsigned char hdr[4];
    ssize_t n = read(fd, hdr, sizeof(hdr));
    close(fd);
    if (n >= 2 && hdr[0] == 'M' && hdr[1] == 'Z')
        return 1;
    return 0;
}

static char **build_wrapped_argv(const char *app_bin, const char *path, char *const argv[])
{
    size_t count = 0;
    if (argv) {
        while (argv[count] != NULL)
            count++;
    }
    char **new_argv = (char **)calloc(count + 2, sizeof(char *));
    if (!new_argv)
        return NULL;
    new_argv[0] = (char *)app_bin;
    new_argv[1] = (char *)path;
    for (size_t i = 1; i < count; ++i) {
        new_argv[i + 1] = argv[i];
    }
    new_argv[count + 1] = NULL;
    return new_argv;
}

static int wp_posix_spawn(
    pid_t *restrict pid,
    const char *restrict path,
    const posix_spawn_file_actions_t *file_actions,
    const posix_spawnattr_t *restrict attrp,
    char *const argv[restrict],
    char *const envp[restrict])
{
    int has_x = 0;
    const char *app_bin = NULL;
    if (is_pe_or_msi_file(path, &has_x) && (app_bin = spumante_app_bin())) {
        if (!has_x) {
            return EACCES;
        }
        char **wrapped = build_wrapped_argv(app_bin, path, argv);
        if (!wrapped)
            return ENOMEM;
        int rc = posix_spawn(pid, app_bin, file_actions, attrp, wrapped, envp);
        free(wrapped);
        return rc;
    }
    return posix_spawn(pid, path, file_actions, attrp, argv, envp);
}

static int wp_execve(const char *path, char *const argv[], char *const envp[])
{
    int has_x = 0;
    const char *app_bin = NULL;
    if (is_pe_or_msi_file(path, &has_x) && (app_bin = spumante_app_bin())) {
        if (!has_x) {
            errno = EACCES;
            return -1;
        }
        char **wrapped = build_wrapped_argv(app_bin, path, argv);
        if (!wrapped) {
            errno = ENOMEM;
            return -1;
        }
        int rc = execve(app_bin, wrapped, envp);
        free(wrapped);
        return rc;
    }
    return execve(path, argv, envp);
}

static int wp_execv(const char *path, char *const argv[])
{
    extern char **environ;
    return wp_execve(path, argv, environ);
}

__attribute__((used, section("__DATA,__interpose")))
static struct interpose_s interpose_table[] = {
    { (void *)wp_posix_spawn, (void *)posix_spawn },
    { (void *)wp_execve, (void *)execve },
    { (void *)wp_execv, (void *)execv },
};

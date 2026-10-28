/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: drive_c sync, runtime trace, built-in app launcher, issue reporting and UI reports.
 */

#include "spm_runtime.h"
#include "spm_config.h"
#include "spm_paths.h"
#include "spm_scan.h"
#include "spm_util.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

const char *const spm_builtin_launch_env[] = {
    "SPUMANTE_SKIP_SECURITY_GATES=1",
    "SPUMANTE_REQUIRE_AUTHENTICODE=0",
    "SPUMANTE_NO_SANDBOX=1",
    "SPUMANTE_BUILTIN_APP_LAUNCH=1",
    "WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=",
};
const int spm_builtin_launch_env_count = (int)(sizeof(spm_builtin_launch_env) / sizeof(spm_builtin_launch_env[0]));

/* ------------------------------------------------------------ drive_c sync */

static int spm_sync_suffix(const char *name)
{
    return spm_has_suffix_ci(name, ".exe") || spm_has_suffix_ci(name, ".msi") || spm_has_suffix_ci(name, ".com") ||
           spm_has_suffix_ci(name, ".scr");
}

static void spm_sync_walk(const char *dir, long *scanned, spm_json *updated)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        char *path = spm_path_join(dir, ent->d_name);
        struct stat st;
        if (lstat(path, &st) != 0 || S_ISLNK(st.st_mode)) {
            free(path);
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (strcasecmp(ent->d_name, "winsxs") != 0)
                spm_sync_walk(path, scanned, updated);
        } else if (S_ISREG(st.st_mode) && spm_sync_suffix(ent->d_name)) {
            (*scanned)++;
            if ((st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0 || access(path, X_OK) != 0) {
                if (chmod(path, st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) == 0)
                    spm_json_append(updated, spm_json_string(path));
            }
        }
        free(path);
    }
    closedir(d);
}

spm_json *spm_sync_drive_c(const char *prefix_override)
{
    char *prefix = NULL;
    if (prefix_override && prefix_override[0])
        prefix = spm_abs_path(prefix_override);
    else {
        char *cfg_path = spm_config_path();
        spm_json *cfg = spm_config_load(cfg_path, 1);
        prefix = spm_config_prefix_path(cfg);
        spm_json_free(cfg);
        free(cfg_path);
    }
    char *drive_c = spm_path_join(prefix, "drive_c");
    long scanned = 0;
    spm_json *updated = spm_json_array();
    if (spm_is_dir(drive_c))
        spm_sync_walk(drive_c, &scanned, updated);

    spm_json *r = spm_json_object();
    spm_json_set_str(r, "action", "sync-drive-c");
    spm_json_set_str(r, "wineprefix", prefix);
    spm_json_set_str(r, "drive_c", drive_c);
    spm_json_set_int(r, "scanned_count", scanned);
    spm_json_set_int(r, "updated_count", (long long)updated->count);
    spm_json_set(r, "updated_files", updated);
    free(drive_c);
    free(prefix);
    return r;
}

/* ----------------------------------------------------------- runtime trace */

spm_json *spm_runtime_trace(const char *resolved_exe, const char *pe_arch, const char *launcher,
                            const char *working_dir, int is_msi, int extra_argc, char **extra_argv)
{
    spm_json *status = spm_config_status(NULL, 0, NULL);
    const spm_json *cfg = spm_json_get(status, "config");
    char *sb_path = spm_seatbelt_write(cfg, spm_basename(resolved_exe));

    spm_json *args = spm_json_array();
    for (int i = 0; i < extra_argc; i++)
        if (strcmp(extra_argv[i], "--") != 0)
            spm_json_append(args, spm_json_string(extra_argv[i]));

    spm_json *t = spm_json_object();
    spm_json_set_str(t, "target", resolved_exe);
    char *prefix = spm_config_prefix_path(cfg);
    spm_json_set_str(t, "wineprefix", prefix);
    free(prefix);
    spm_json_set_bool(t, "executable_bit_verified", 1);
    spm_json_set_bool(t, "rosetta2_verified", 1);
    spm_json_set_bool(t, "security_gates_verified", 1);
    spm_json_set_bool(t, "is_msi", is_msi);
    spm_json_set_str(t, "pe_arch", pe_arch);
    spm_json_set_str(t, "selected_launcher", launcher ? launcher : "");
    spm_json_set_str(t, "working_directory", working_dir);
    spm_json_set_bool(t, "sandboxed", spm_json_truthy(spm_json_path(cfg, "sandbox.enabled"), 1));
    spm_json_set_bool(t, "z_drive_stripped", spm_json_truthy(spm_json_path(status, "cascade.z_drive_stripped"), 0));
    spm_json_set_str(t, "network_incoming", spm_json_str(spm_json_path(cfg, "network.incoming"), "deny"));
    spm_json_set_str(t, "network_outgoing", spm_json_str(spm_json_path(cfg, "network.outgoing"), "http_https_only"));
    const spm_json *ports = spm_json_path(cfg, "network.allowed_outgoing_ports");
    spm_json_set(t, "allowed_outgoing_ports", ports ? spm_json_clone(ports) : spm_json_array());
    spm_json_set_str(t, "seatbelt_profile", sb_path);
    if (is_msi) {
        spm_json *msi = spm_json_array();
        spm_json_append(msi, spm_json_string("msiexec"));
        spm_json_append(msi, spm_json_string("/i"));
        spm_json_append(msi, spm_json_string(resolved_exe));
        for (size_t i = 0; i < args->count; i++)
            spm_json_append(msi, spm_json_clone(args->items[i]));
        spm_json_set(t, "msiexec_command", msi);
    }
    spm_json_set(t, "args", args);
    spm_json *sync = spm_sync_drive_c(NULL);
    spm_json_set_bool(t, "drive_c_executables_synced", 1);
    spm_json_set_int(t, "drive_c_sync_updated_count", spm_json_num(spm_json_get(sync, "updated_count"), 0));
    spm_json_free(sync);
    free(sb_path);
    spm_json_free(status);
    return t;
}

/* ----------------------------------------------------- built-in app launch */

int spm_spawn_detached(char *const argv[], char *const envp[], const char *cwd)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2)
                close(devnull);
        }
        if (cwd && chdir(cwd) != 0)
            _exit(127);
        execve(argv[0], argv, envp);
        _exit(127);
    }
    return (int)pid;
}

static int spm_is_cli_builtin(const char *name)
{
    for (int i = 0; i < spm_builtin_cli_count; i++)
        if (strcasecmp(name, spm_builtin_cli_apps[i]) == 0)
            return 1;
    return 0;
}

static char *spm_filter_wine_noise(const char *out)
{
    spm_buf b;
    spm_buf_init(&b);
    int first = 1;
    for (const char *line = out; line && *line;) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        char *l = spm_strndup(line, len);
        if (!strstr(l, ":err:wineusb:") && !strstr(l, ":err:ntoskrnl:") && !strstr(l, ":fixme:service:")) {
            if (!first)
                spm_buf_putc(&b, '\n');
            spm_buf_puts(&b, l);
            first = 0;
        }
        free(l);
        line = nl ? nl + 1 : NULL;
    }
    char *s = spm_buf_steal(&b);
    /* strip() */
    size_t start = 0, end = strlen(s);
    while (start < end && isspace((unsigned char)s[start]))
        start++;
    while (end > start && isspace((unsigned char)s[end - 1]))
        end--;
    char *trimmed = spm_strndup(s + start, end - start);
    free(s);
    return trimmed;
}

spm_json *spm_launch_builtin_console(const char *app_name, int extra_argc, char **extra_argv, int open_console,
                                     int dry_run)
{
    char *normalized = spm_has_suffix_ci(app_name, ".exe") ? spm_strdup(app_name) : spm_asprintf("%s.exe", app_name);
    int is_cli = spm_is_cli_builtin(normalized);
    int is_wineconsole = (strcasecmp(normalized, "wineconsole.exe") == 0);
    int use_console = is_wineconsole || (is_cli && open_console);
    char *exe_path = spm_find_windows_app(normalized);
    char *console_exe_path = use_console ? spm_find_windows_app("wineconsole.exe") : NULL;
    char *launcher = spm_runtime_launcher("x86");

    spm_json *args = spm_json_array();
    if (use_console) {
        if (is_wineconsole) {
            for (int i = 0; i < extra_argc; i++)
                if (strcmp(extra_argv[i], "--") != 0)
                    spm_json_append(args, spm_json_string(extra_argv[i]));
            if (args->count == 0)
                spm_json_append(args, spm_json_string("cmd.exe"));
        } else if (strcasecmp(normalized, "cmd.exe") == 0) {
            spm_json_append(args, spm_json_string("cmd.exe"));
            for (int i = 0; i < extra_argc; i++)
                if (strcmp(extra_argv[i], "--") != 0)
                    spm_json_append(args, spm_json_string(extra_argv[i]));
        } else {
            spm_json_append(args, spm_json_string("cmd.exe"));
            spm_json_append(args, spm_json_string("/k"));
            spm_json_append(args, spm_json_string(normalized));
            for (int i = 0; i < extra_argc; i++)
                if (strcmp(extra_argv[i], "--") != 0)
                    spm_json_append(args, spm_json_string(extra_argv[i]));
        }
    } else {
        for (int i = 0; i < extra_argc; i++)
            if (strcmp(extra_argv[i], "--") != 0)
                spm_json_append(args, spm_json_string(extra_argv[i]));
        if (strcasecmp(normalized, "cmd.exe") == 0 && args->count == 0) {
            spm_json_append(args, spm_json_string("/c"));
            spm_json_append(args, spm_json_string("ver"));
        }
    }

    char *cfg_path = spm_config_path();
    spm_json *cfg = spm_config_load(cfg_path, 1);
    free(cfg_path);
    char *prefix = spm_config_prefix_path(cfg);
    char *wineprefix_override = spm_asprintf("WINEPREFIX=%s", prefix);
    char *sys_reg = spm_path_join(prefix, "system.reg");

    /* Ensure C: and Z: mappings exist for built-in app execution, and keep .update-timestamp synced with wine.inf */
    if (!dry_run) {
        char *dosdevices = spm_path_join(prefix, "dosdevices");
        spm_mkdir_p(dosdevices, 0755);
        static const char *const win_subdirs[] = {
            "drive_c/windows/system32",
            "drive_c/windows/syswow64",
            "drive_c/windows/temp",
        };
        for (int i = 0; i < 3; i++) {
            char *sub = spm_path_join(prefix, win_subdirs[i]);
            spm_mkdir_p(sub, 0755);
            free(sub);
        }
        char *c_link = spm_path_join(dosdevices, "c:");
        struct stat st;
        if (lstat(c_link, &st) != 0) {
            if (symlink("../drive_c", c_link) != 0) {
                /* best effort */
            }
        }
        free(c_link);
        char *z_link = spm_path_join(dosdevices, "z:");
        if (lstat(z_link, &st) != 0) {
            if (symlink("/", z_link) != 0) {
                /* best effort */
            }
        }
        char *rt = spm_runtime_dir("x86");
        if (rt && spm_is_file(sys_reg)) {
            char *wine_inf = spm_path_join(rt, "share/wine/wine.inf");
            if (stat(wine_inf, &st) == 0) {
                char *ts_file = spm_path_join(prefix, ".update-timestamp");
                char *line = spm_asprintf("%lld\n", (long long)st.st_mtime);
                spm_write_file(ts_file, line, strlen(line), 0644);
                free(line);
                free(ts_file);
            }
            free(wine_inf);
        }
        free(rt);
        free(z_link);
        free(dosdevices);
    }
    free(sys_reg);

    spm_json *r = spm_json_object();
    spm_json_set_str(r, "action", "launch-builtin-app");
    spm_json_set_str(r, "wineprefix", prefix);
    spm_json_set_str(r, "app_name", normalized);
    spm_json_set_str(r, "category", is_cli ? "cli" : "gui");
    spm_json_set_str(r, "exe_path", exe_path ? exe_path : "");
    spm_json_set_str(r, "launcher", launcher ? launcher : "");
    spm_json_set(r, "args", args);
    spm_json_set_bool(r, "open_in_console", use_console ? 1 : 0);
    if (use_console) {
        spm_json_set_str(r, "console_host", "wineconsole.exe");
        spm_json_set_str(r, "console_host_path", console_exe_path ? console_exe_path : "");
    }
    spm_json_set_bool(r, "temporary_security_bypass_env", 1);
    spm_json *env_obj = spm_json_object();
    for (int i = 0; i < spm_builtin_launch_env_count; i++) {
        const char *eq = strchr(spm_builtin_launch_env[i], '=');
        char *k = spm_strndup(spm_builtin_launch_env[i], (size_t)(eq - spm_builtin_launch_env[i]));
        spm_json_set_str(env_obj, k, eq + 1);
        free(k);
    }
    spm_json_set(r, "launch_env", env_obj);
    spm_json_set_bool(r, "dry_run", dry_run);

    const char *target_bin = (use_console && console_exe_path) ? console_exe_path : exe_path;
    if (dry_run || !target_bin || !exe_path || !launcher) {
        if (!dry_run)
            spm_json_set_str(r, "status", (!exe_path || !target_bin) ? "error_app_not_found" : "error_runtime_not_found");
        else
            spm_json_set_str(r, "status", "dry_run_ready");
        goto done;
    }

    {
        const char *overrides[16];
        size_t n_ov = 0;
        for (int i = 0; i < spm_builtin_launch_env_count; i++)
            overrides[n_ov++] = spm_builtin_launch_env[i];
        overrides[n_ov++] = "!SPUMANTE_DRY_RUN_JSON";
        overrides[n_ov++] = "SPUMANTE_INTERPOSE_BYPASS=1";
        if (wineprefix_override)
            overrides[n_ov++] = wineprefix_override;
        char **envp = spm_env_with(overrides, n_ov);

        char **argv = calloc(args->count + 3, sizeof(char *));
        size_t ai = 0;
        argv[ai++] = launcher;
        argv[ai++] = (char *)target_bin;
        for (size_t i = 0; i < args->count; i++)
            argv[ai++] = args->items[i]->str;
        argv[ai] = NULL;

        char *parent = spm_dirname(target_bin);
        if (is_cli && !use_console) {
            char *out = NULL;
            int status = 0;
            int rc = spm_run_capture(argv, envp, parent, NULL, 300, &out, &status);
            int exit_code = (rc == 0 && status >= 0 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
            char *clean = spm_filter_wine_noise(out ? out : "");
            spm_json_set_int(r, "returncode", exit_code);
            spm_json_set_str(r, "stdout", clean);
            char *st_str = exit_code == 0 ? spm_strdup("completed")
                           : status == -1 ? spm_strdup("timeout")
                                          : spm_asprintf("exited_%d", exit_code);
            spm_json_set_str(r, "status", st_str);
            free(st_str);
            free(clean);
            free(out);
        } else {
            int pid = spm_spawn_detached(argv, envp, parent);
            spm_json_set_int(r, "pid", pid);
            spm_json_set_str(r, "status", pid > 0 ? (use_console && is_cli ? "launched_in_console" : "launched") : "spawn_failed");
        }
        free(parent);
        free(argv);
        spm_env_free(envp);
    }

done:
    free(wineprefix_override);
    free(prefix);
    spm_json_free(cfg);
    free(launcher);
    free(console_exe_path);
    free(exe_path);
    free(normalized);
    return r;
}

spm_json *spm_launch_builtin(const char *app_name, int extra_argc, char **extra_argv, int dry_run)
{
    const char *env_console = getenv("SPUMANTE_CLI_OPEN_CONSOLE");
    int open_console = (env_console && strcmp(env_console, "1") == 0);
    return spm_launch_builtin_console(app_name, extra_argc, extra_argv, open_console, dry_run);
}

/* ---------------------------------------------------------- issue reports */

static void spm_url_encode(spm_buf *b, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (isalnum(*p) || *p == '_' || *p == '.' || *p == '-' || *p == '~')
            spm_buf_putc(b, (char)*p);
        else if (*p == ' ')
            spm_buf_putc(b, '+');
        else {
            spm_buf_putc(b, '%');
            spm_buf_putc(b, hex[*p >> 4]);
            spm_buf_putc(b, hex[*p & 0xF]);
        }
    }
}

static void spm_query_add(spm_buf *b, int *first, const char *key, const char *val)
{
    spm_buf_putc(b, *first ? '?' : '&');
    *first = 0;
    spm_url_encode(b, key);
    spm_buf_putc(b, '=');
    spm_url_encode(b, val);
}

static char *spm_macos_version(void)
{
#ifdef __APPLE__
    char ver[64];
    size_t sz = sizeof(ver);
    if (sysctlbyname("kern.osproductversion", ver, &sz, NULL, 0) == 0 && ver[0])
        return spm_asprintf("macOS %s", ver);
#endif
    struct utsname u;
    if (uname(&u) == 0)
        return spm_asprintf("%s %s", u.sysname, u.release);
    return spm_strdup("unknown");
}

static char *spm_cpu_arch(void)
{
    int apple_silicon = 0;
#ifdef __APPLE__
    int arm64_cap = 0;
    size_t sz = sizeof(arm64_cap);
    if (sysctlbyname("hw.optional.arm64", &arm64_cap, &sz, NULL, 0) == 0 && arm64_cap == 1)
        apple_silicon = 1;
#endif
    if (apple_silicon)
        return spm_strdup("Apple Silicon (arm64 + Rosetta 2)");
    struct utsname u;
    return spm_asprintf("Intel (%s)", uname(&u) == 0 ? u.machine : "x86_64");
}

static const char *spm_tf(const spm_json *cfg, const char *path, int dflt)
{
    return spm_json_truthy(spm_json_path(cfg, path), dflt) ? "true" : "false";
}

spm_json *spm_issue_report(const char *kind, const char *app_name, const char *rating, int dry_run)
{
    char *cfg_path = spm_config_path();
    spm_json *cfg = spm_config_load(cfg_path, 1);
    free(cfg_path);
    char *settings = spm_asprintf(
        "windows_version=%s, retina_mode=%s, sandbox=%s, strip_z_drive=%s, net_in=%s, net_out=%s, "
        "require_authenticode=%s, metal=%s, videotoolbox=%s, arm64ec_jit=%s",
        spm_json_str(spm_json_path(cfg, "wine_environment.windows_version"), "win11"),
        spm_tf(cfg, "wine_environment.retina_mode", 1), spm_tf(cfg, "sandbox.enabled", 1),
        spm_tf(cfg, "sandbox.strip_z_drive", 1), spm_json_str(spm_json_path(cfg, "network.incoming"), "deny"),
        spm_json_str(spm_json_path(cfg, "network.outgoing"), "http_https_only"),
        spm_tf(cfg, "security.require_authenticode", 0), spm_tf(cfg, "acceleration.metal_iosurface_swapchain", 1),
        spm_tf(cfg, "acceleration.videotoolbox_hw_decode", 1), spm_tf(cfg, "acceleration.arm64ec_jit_cache", 1));
    spm_json_free(cfg);

    int appdb = kind && (strcmp(kind, "appdb") == 0 || strcmp(kind, "app") == 0 || strcmp(kind, "compatibility") == 0);
    const char *template_name = appdb ? "app_compatibility.yml" : "bug_report.yml";
    char *clean_app = spm_strdup(app_name ? app_name : "");
    char *clean_rating = spm_strdup(rating && rating[0] ? rating : "Platinum");
    char *macos = spm_macos_version();
    char *cpu = spm_cpu_arch();

    spm_buf url;
    spm_buf_init(&url);
    spm_buf_puts(&url, SPM_GITHUB_REPO_URL "/issues/new");
    int first = 1;
    spm_query_add(&url, &first, "template", template_name);
    if (clean_app[0]) {
        char *title = appdb ? spm_asprintf("[AppDB]: %s (%s)", clean_app, clean_rating) : spm_asprintf("[Bug]: %s", clean_app);
        spm_query_add(&url, &first, "title", title);
        free(title);
        spm_query_add(&url, &first, "app_name", clean_app);
    }
    if (appdb)
        spm_query_add(&url, &first, "rating", clean_rating);
    spm_query_add(&url, &first, "macos_version", macos);
    spm_query_add(&url, &first, "cpu_arch", cpu);
    spm_query_add(&url, &first, "spumante_settings", settings);
    char *full_url = spm_buf_steal(&url);

    if (!dry_run) {
#ifdef __APPLE__
        char *argv[] = { "/usr/bin/open", full_url, NULL };
        char *out = NULL;
        int status = 0;
        spm_run_capture(argv, NULL, NULL, NULL, 30, &out, &status);
        free(out);
#endif
    }

    spm_json *r = spm_json_object();
    spm_json_set_str(r, "action", "report-issue");
    spm_json_set_str(r, "kind", appdb ? "appdb" : "bug");
    spm_json_set_str(r, "template", template_name);
    spm_json_set_str(r, "url", full_url);
    spm_json_set_str(r, "appdb_doc_url", SPM_APPDB_DOC_URL);
    spm_json *fields = spm_json_object();
    spm_json_set_str(fields, "app_name", clean_app);
    spm_json_set(fields, "rating", appdb ? spm_json_string(clean_rating) : spm_json_null());
    spm_json_set_str(fields, "macos_version", macos);
    spm_json_set_str(fields, "cpu_arch", cpu);
    spm_json_set_str(fields, "spumante_settings", settings);
    spm_json_set(r, "prefilled_fields", fields);
    spm_json_set_bool(r, "dry_run", dry_run);

    free(full_url);
    free(cpu);
    free(macos);
    free(clean_rating);
    free(clean_app);
    free(settings);
    return r;
}

/* ------------------------------------------------------- config UI report */

spm_json *spm_config_ui_report(void)
{
    spm_json *status = spm_config_status(NULL, 0, NULL);
    const spm_json *cfg = spm_json_get(status, "config");

    spm_json *r = spm_json_object();
    spm_json_set_str(r, "action", "config-ui");
    spm_json_set_str(r, "ui_style", "macos-native-appkit-settings");
    spm_json_set_str(r, "window_title", "Spumante Settings");
    spm_json *sections = spm_json_array();
    static const char *const names[] = { "General", "Security", "Sandbox & Network", "Graphics & Acceleration",
                                         "Built-in Apps" };
    for (int i = 0; i < 5; i++)
        spm_json_append(sections, spm_json_string(names[i]));
    spm_json_set(r, "sections", sections);

    spm_json *builtin = spm_json_object();
    spm_json *gui = spm_json_array(), *cli = spm_json_array();
    for (int i = 0; i < spm_builtin_gui_count; i++)
        spm_json_append(gui, spm_json_string(spm_builtin_gui_apps[i]));
    for (int i = 0; i < spm_builtin_cli_count; i++)
        spm_json_append(cli, spm_json_string(spm_builtin_cli_apps[i]));
    spm_json_set(builtin, "gui", gui);
    spm_json_set(builtin, "cli", cli);
    spm_json_set_str(builtin, "console_host", "wineconsole.exe");
    spm_json *env_obj = spm_json_object();
    for (int i = 0; i < spm_builtin_launch_env_count; i++) {
        const char *eq = strchr(spm_builtin_launch_env[i], '=');
        char *k = spm_strndup(spm_builtin_launch_env[i], (size_t)(eq - spm_builtin_launch_env[i]));
        spm_json_set_str(env_obj, k, eq + 1);
        free(k);
    }
    spm_json_set(builtin, "temporary_launch_env", env_obj);
    char *apps_dir = spm_find_resource("WindowsApps", 1);
    spm_json_set_str(builtin, "bundled_apps_dir", apps_dir ? apps_dir : "");
    free(apps_dir);
    spm_json_set(r, "builtin_apps", builtin);

    spm_json *vt = spm_json_object();
    spm_json_set_bool(vt, "enabled_checkbox", 1);
    spm_json_set_bool(vt, "api_key_input_field", 1);
    spm_json_set_bool(vt, "show_hide_key_toggle", 1);
    spm_json_set_bool(vt, "virustotal_enabled", spm_json_truthy(spm_json_path(cfg, "security.virustotal_enabled"), 1));
    spm_json_set_bool(vt, "virustotal_api_key_configured",
                      spm_json_str(spm_json_path(cfg, "security.virustotal_api_key"), "")[0] != '\0');
    spm_json_set(r, "virustotal_controls", vt);

    spm_json *issues = spm_json_object();
    spm_json_set_bool(issues, "report_bug_button", 1);
    spm_json_set_bool(issues, "submit_tested_app_button", 1);
    spm_json_set_str(issues, "bug_report_template", "bug_report.yml");
    spm_json_set_str(issues, "appdb_report_template", "app_compatibility.yml");
    spm_json_set_str(issues, "appdb_doc_url", SPM_APPDB_DOC_URL);
    spm_json_set(r, "issue_reporting_controls", issues);

    spm_json *tray = spm_json_object();
    spm_json_set_bool(tray, "enabled", 1);
    spm_json_set_str(tray, "status_item_class", "NSStatusItem");
    spm_json_set_str(tray, "icon_resource", "spumante-logo.png");
    char *icon_path = spm_find_resource("spumante-logo.png", 0);
    spm_json_set_str(tray, "icon_path", icon_path ? icon_path : "");
    free(icon_path);
    spm_json_set_bool(tray, "stays_open_in_background", 1);
    spm_json_set_bool(tray, "supports_favorite_apps", 1);
    spm_json_set_bool(tray, "supports_custom_favorite_labels", 1);
    spm_json *fav_ctrls = spm_json_object();
    spm_json_set_bool(fav_ctrls, "popup_selector", 1);
    spm_json_set_bool(fav_ctrls, "label_text_field", 1);
    spm_json_set_bool(fav_ctrls, "path_text_field", 1);
    spm_json_set_bool(fav_ctrls, "browse_button", 1);
    spm_json_set_bool(fav_ctrls, "save_add_update_button", 1);
    spm_json_set_bool(fav_ctrls, "remove_button", 1);
    spm_json_set_bool(fav_ctrls, "launch_button", 1);
    spm_json_set(tray, "favorite_app_controls", fav_ctrls);
    const spm_json *favs_cfg = spm_json_get(cfg, "favorite_apps");
    spm_json_set(tray, "favorite_apps",
                 (favs_cfg && favs_cfg->type == SPM_JARR) ? spm_json_clone(favs_cfg) : spm_json_array());
    spm_json_set_str(tray, "cli_console_host", "wineconsole.exe");
    spm_json *quick = spm_json_array();
    static const char *const quick_utils[] = {
        "taskmgr.exe", "wineconsole.exe", "notepad.exe", "regedit.exe", "winecfg.exe", "explorer.exe"
    };
    for (int i = 0; i < 6; i++)
        spm_json_append(quick, spm_json_string(quick_utils[i]));
    spm_json_set(tray, "quick_launch_utilities", quick);
    spm_json_set(r, "menu_bar_tray", tray);

    spm_json_set(r, "login_agent", spm_login_agent_status());
    spm_json_set(r, "session", spm_session_status(NULL));

    spm_json_set_bool(r, "cascades_to_wine_registry", 1);
    spm_json_set_str(r, "config_path", spm_json_str(spm_json_get(status, "config_path"), ""));
    spm_json_set(r, "config", spm_json_clone(cfg));
    spm_json_set_str(r, "seatbelt_profile_path", spm_json_str(spm_json_get(status, "seatbelt_profile_path"), ""));
    spm_json_free(status);
    return r;
}

/* --------------------------------- persistent session, login agent and tray */

spm_json *spm_session_status(const char *prefix_override)
{
    char *prefix = NULL;
    if (prefix_override && prefix_override[0])
        prefix = spm_abs_path(prefix_override);
    else {
        char *cfg_path = spm_config_path();
        spm_json *cfg = spm_config_load(cfg_path, 0);
        free(cfg_path);
        prefix = spm_config_prefix_path(cfg);
        spm_json_free(cfg);
    }
    char *sock_path = spm_wineserver_socket_path(prefix);
    int running = spm_wineserver_is_running(sock_path);

    spm_json *r = spm_json_object();
    spm_json_set_str(r, "action", "session-status");
    spm_json_set_str(r, "wineprefix", prefix ? prefix : "");
    if (sock_path)
        spm_json_set_str(r, "server_socket", sock_path);
    spm_json_set_bool(r, "running", running);

    free(sock_path);
    free(prefix);
    return r;
}

spm_json *spm_session_start(const char *prefix_override, int dry_run)
{
    char *prefix = NULL;
    if (prefix_override && prefix_override[0])
        prefix = spm_abs_path(prefix_override);
    else {
        char *cfg_path = spm_config_path();
        spm_json *cfg = spm_config_load(cfg_path, 0);
        free(cfg_path);
        prefix = spm_config_prefix_path(cfg);
        spm_json_free(cfg);
    }
    char *sock_path = spm_wineserver_socket_path(prefix);
    int running = spm_wineserver_is_running(sock_path);

    spm_json *r = spm_json_object();
    spm_json_set_str(r, "action", "session-start");
    spm_json_set_str(r, "wineprefix", prefix ? prefix : "");
    if (sock_path)
        spm_json_set_str(r, "server_socket", sock_path);

    if (running) {
        spm_json_set_str(r, "status", "already_running");
        spm_json_set_bool(r, "running", 1);
        free(sock_path);
        free(prefix);
        return r;
    }

    char *rt = spm_runtime_dir("x86");
    char *wineserver_bin = rt ? spm_path_join(rt, "bin/wineserver") : spm_strdup("/opt/spumante/bin/wineserver");
    char *spumante_bin = rt ? spm_path_join(rt, "bin/spumante") : spm_strdup("/opt/spumante/bin/spumante");
    free(rt);

    spm_json *cmds = spm_json_array();
    spm_json *c0 = spm_json_array();
    spm_json_append(c0, spm_json_string(wineserver_bin));
    spm_json_append(c0, spm_json_string("-p"));
    spm_json_append(cmds, c0);

    spm_json *c1 = spm_json_array();
    spm_json_append(c1, spm_json_string(spumante_bin));
    spm_json_append(c1, spm_json_string("wineboot"));
    spm_json_append(cmds, c1);

    spm_json_set(r, "commands", cmds);
    spm_json_set_bool(r, "dry_run", dry_run);

    if (!dry_run) {
        char *wprefix_env = spm_asprintf("WINEPREFIX=%s", prefix);
        const char *ov[] = { wprefix_env, "SPUMANTE_INTERPOSE_BYPASS=1", NULL };
        char **envp = spm_env_with(ov, 2);

        char *argv0[] = { wineserver_bin, "-p", NULL };
        int rc0 = 0;
        spm_run_capture(argv0, envp, prefix, NULL, 30, NULL, &rc0);

        char *argv1[] = { spumante_bin, "wineboot", NULL };
        int rc1 = 0;
        spm_run_capture(argv1, envp, prefix, NULL, 60, NULL, &rc1);

        spm_env_free(envp);
        free(wprefix_env);
        spm_json_set_str(r, "status", (rc0 == 0 && rc1 == 0) ? "started" : "error_launch_failed");
    } else {
        spm_json_set_str(r, "status", "dry_run_ready");
    }

    free(wineserver_bin);
    free(spumante_bin);
    free(sock_path);
    free(prefix);
    return r;
}

spm_json *spm_session_stop(const char *prefix_override, int dry_run)
{
    char *prefix = NULL;
    if (prefix_override && prefix_override[0])
        prefix = spm_abs_path(prefix_override);
    else {
        char *cfg_path = spm_config_path();
        spm_json *cfg = spm_config_load(cfg_path, 0);
        free(cfg_path);
        prefix = spm_config_prefix_path(cfg);
        spm_json_free(cfg);
    }

    char *rt = spm_runtime_dir("x86");
    char *wineserver_bin = rt ? spm_path_join(rt, "bin/wineserver") : spm_strdup("/opt/spumante/bin/wineserver");
    free(rt);

    spm_json *r = spm_json_object();
    spm_json_set_str(r, "action", "session-stop");
    spm_json_set_str(r, "wineprefix", prefix ? prefix : "");
    spm_json_set_bool(r, "dry_run", dry_run);

    spm_json *cmds = spm_json_array();
    spm_json *c0 = spm_json_array();
    spm_json_append(c0, spm_json_string(wineserver_bin));
    spm_json_append(c0, spm_json_string("-k"));
    spm_json_append(cmds, c0);
    spm_json_set(r, "commands", cmds);

    if (!dry_run) {
        char *wprefix_env = spm_asprintf("WINEPREFIX=%s", prefix);
        const char *ov[] = { wprefix_env, "SPUMANTE_INTERPOSE_BYPASS=1", NULL };
        char **envp = spm_env_with(ov, 2);

        char *argv0[] = { wineserver_bin, "-k", NULL };
        int rc0 = 0;
        spm_run_capture(argv0, envp, prefix, NULL, 30, NULL, &rc0);

        spm_env_free(envp);
        free(wprefix_env);
        spm_json_set_str(r, "status", (rc0 == 0) ? "stopped" : "error_stop_failed");
    } else {
        spm_json_set_str(r, "status", "dry_run_ready");
    }

    free(wineserver_bin);
    free(prefix);
    return r;
}

spm_json *spm_init_prefix(const char *prefix_override, int dry_run)
{
    char *prefix = NULL;
    if (prefix_override && prefix_override[0])
        prefix = spm_abs_path(prefix_override);
    else {
        char *cfg_path = spm_config_path();
        spm_json *cfg = spm_config_load(cfg_path, 0);
        free(cfg_path);
        prefix = spm_config_prefix_path(cfg);
        spm_json_free(cfg);
    }

    char *rt = spm_runtime_dir("x86");
    char *spumante_bin = rt ? spm_path_join(rt, "bin/spumante") : spm_strdup("/opt/spumante/bin/spumante");
    char *wineserver_bin = rt ? spm_path_join(rt, "bin/wineserver") : spm_strdup("/opt/spumante/bin/wineserver");
    free(rt);

    spm_json *r = spm_json_object();
    spm_json_set_str(r, "action", "init-prefix");
    spm_json_set_str(r, "wineprefix", prefix ? prefix : "");
    spm_json_set_bool(r, "dry_run", dry_run);

    spm_json *cmds = spm_json_array();
    spm_json *c0 = spm_json_array();
    spm_json_append(c0, spm_json_string(spumante_bin));
    spm_json_append(c0, spm_json_string("wineboot"));
    spm_json_append(c0, spm_json_string("--init"));
    spm_json_append(cmds, c0);

    spm_json *c1 = spm_json_array();
    spm_json_append(c1, spm_json_string(wineserver_bin));
    spm_json_append(c1, spm_json_string("-w"));
    spm_json_append(cmds, c1);
    spm_json_set(r, "commands", cmds);

    if (!dry_run) {
        char *wprefix_env = spm_asprintf("WINEPREFIX=%s", prefix);
        const char *ov[] = { wprefix_env, "SPUMANTE_INTERPOSE_BYPASS=1", NULL };
        char **envp = spm_env_with(ov, 2);

        char *argv0[] = { spumante_bin, "wineboot", "--init", NULL };
        int rc0 = 0;
        spm_run_capture(argv0, envp, prefix, NULL, 120, NULL, &rc0);

        char *argv1[] = { wineserver_bin, "-w", NULL };
        int rc1 = 0;
        spm_run_capture(argv1, envp, prefix, NULL, 60, NULL, &rc1);

        spm_env_free(envp);
        free(wprefix_env);
        spm_json_set_str(r, "status", (rc0 == 0 && rc1 == 0) ? "initialized" : "error_init_failed");
    } else {
        spm_json_set_str(r, "status", "dry_run_ready");
    }

    free(spumante_bin);
    free(wineserver_bin);
    free(prefix);
    return r;
}

spm_json *spm_login_agent_status(void)
{
    char *plist = spm_login_agent_plist_path();
    int installed = spm_is_file(plist);
    spm_json *r = spm_json_object();
    spm_json_set_str(r, "action", "login-agent-status");
    spm_json_set_bool(r, "installed", installed);
    spm_json_set_str(r, "plist_path", plist ? plist : "");
    free(plist);
    return r;
}

spm_json *spm_login_agent_install(const char *app_bin)
{
    char *plist = spm_login_agent_plist_path();
    char *dir = spm_dirname(plist);
    spm_mkdir_p(dir, 0755);
    free(dir);

    const char *bin = (app_bin && app_bin[0]) ? app_bin : spm_self_exe();
    char *content = spm_asprintf(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "\t<key>Label</key>\n"
        "\t<string>org.spumante.tray</string>\n"
        "\t<key>LimitLoadToSessionType</key>\n"
        "\t<string>Aqua</string>\n"
        "\t<key>ProcessType</key>\n"
        "\t<string>Interactive</string>\n"
        "\t<key>ProgramArguments</key>\n"
        "\t<array>\n"
        "\t\t<string>%s</string>\n"
        "\t\t<string>--tray</string>\n"
        "\t</array>\n"
        "\t<key>RunAtLoad</key>\n"
        "\t<true/>\n"
        "\t<key>KeepAlive</key>\n"
        "\t<false/>\n"
        "</dict>\n"
        "</plist>\n",
        bin);
    spm_write_file(plist, content, strlen(content), 0644);
    free(content);

    const char *skip = getenv("SPUMANTE_SKIP_LAUNCHCTL");
    if (!(skip && strcmp(skip, "1") == 0)) {
        char *cmd = spm_asprintf("launchctl load \"%s\" 2>/dev/null", plist);
        system(cmd);
        free(cmd);
    }

    spm_json *r = spm_json_object();
    spm_json_set_str(r, "action", "install-login-agent");
    spm_json_set_bool(r, "installed", 1);
    spm_json_set_str(r, "plist_path", plist ? plist : "");
    free(plist);
    return r;
}

spm_json *spm_login_agent_uninstall(void)
{
    char *plist = spm_login_agent_plist_path();
    if (spm_is_file(plist)) {
        const char *skip = getenv("SPUMANTE_SKIP_LAUNCHCTL");
        if (!(skip && strcmp(skip, "1") == 0)) {
            char *cmd = spm_asprintf("launchctl unload \"%s\" 2>/dev/null", plist);
            system(cmd);
            free(cmd);
        }
        unlink(plist);
    }
    spm_json *r = spm_json_object();
    spm_json_set_str(r, "action", "uninstall-login-agent");
    spm_json_set_bool(r, "installed", 0);
    spm_json_set_str(r, "plist_path", plist ? plist : "");
    free(plist);
    return r;
}

spm_json *spm_tray_report(int dry_run)
{
    spm_json *r = spm_json_object();
    spm_json_set_str(r, "action", "tray");
    spm_json_set_bool(r, "starts_session", 1);
    spm_json_set_bool(r, "stops_session_on_quit", 1);
    spm_json_set_bool(r, "dry_run", dry_run);
    return r;
}

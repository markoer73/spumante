/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: bundle-relative path resolution.
 */

#include "spm_paths.h"
#include "spm_util.h"

#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <mach-o/dyld.h>
#endif

static char *g_self_exe;
static char *g_self_bundle;
static int g_bundle_resolved;

const char *spm_self_exe(void)
{
    if (g_self_exe)
        return g_self_exe;
    char raw[PATH_MAX];
    raw[0] = '\0';
#ifdef __APPLE__
    uint32_t size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0)
        raw[0] = '\0';
#else
    ssize_t n = readlink("/proc/self/exe", raw, sizeof(raw) - 1);
    raw[n > 0 ? n : 0] = '\0';
#endif
    char resolved[PATH_MAX];
    if (raw[0] && realpath(raw, resolved))
        g_self_exe = spm_strdup(resolved);
    else
        g_self_exe = spm_strdup(raw);
    return g_self_exe;
}

const char *spm_self_bundle(void)
{
    if (g_bundle_resolved)
        return g_self_bundle;
    g_bundle_resolved = 1;
    const char *exe = spm_self_exe();
    const char *marker = strstr(exe, ".app/Contents/MacOS/");
    if (marker)
        g_self_bundle = spm_strndup(exe, (size_t)(marker - exe) + 4);
    return g_self_bundle;
}

char *spm_bundle_parent(void)
{
    const char *bundle = spm_self_bundle();
    return spm_dirname(bundle ? bundle : spm_self_exe());
}

static int spm_exists_kind(const char *path, int want_dir)
{
    return want_dir ? spm_is_dir(path) : spm_is_file(path);
}

#ifdef __APPLE__
static char *spm_launchservices_bundle(void)
{
    char *found = NULL;
    CFArrayRef urls = LSCopyApplicationURLsForBundleIdentifier(CFSTR(SPM_BUNDLE_ID), NULL);
    if (!urls)
        return NULL;
    for (CFIndex i = 0; i < CFArrayGetCount(urls) && !found; i++) {
        CFURLRef url = CFArrayGetValueAtIndex(urls, i);
        char buf[PATH_MAX];
        if (CFURLGetFileSystemRepresentation(url, true, (UInt8 *)buf, sizeof(buf)))
            found = spm_strdup(buf);
    }
    CFRelease(urls);
    return found;
}
#endif

char *spm_find_resource(const char *rel_name, int want_dir)
{
    const char *bundle = spm_self_bundle();
    char *parent = spm_bundle_parent();
    char *candidates[4] = { NULL, NULL, NULL, NULL };
    int n = 0;

    if (bundle)
        candidates[n++] = spm_asprintf("%s/Contents/Resources/%s", bundle, rel_name);
    candidates[n++] = spm_asprintf("%s/Spumante.app/Contents/Resources/%s", parent, rel_name);
    candidates[n++] = spm_path_join(parent, rel_name);
    free(parent);

    char *result = NULL;
    for (int i = 0; i < n; i++) {
        if (!result && spm_exists_kind(candidates[i], want_dir))
            result = candidates[i];
        else
            free(candidates[i]);
    }
#ifdef __APPLE__
    if (!result) {
        char *ls_bundle = spm_launchservices_bundle();
        if (ls_bundle) {
            char *cand = spm_asprintf("%s/Contents/Resources/%s", ls_bundle, rel_name);
            if (spm_exists_kind(cand, want_dir))
                result = cand;
            else
                free(cand);
            free(ls_bundle);
        }
    }
#endif
    return result;
}

char *spm_runtime_dir(const char *arch)
{
    char *name = spm_asprintf("wine-install-%s", arch);
    char *bundle_rt = NULL;
    /* A runtime directory is only valid when it carries the launcher script */
    char *dir = spm_find_resource(name, 1);
    if (dir) {
        char *launcher = spm_path_join(dir, "bin/spumante");
        if (spm_is_executable_file(launcher))
            bundle_rt = dir;
        else
            free(dir);
        free(launcher);
    }
    free(name);
    return bundle_rt;
}

char *spm_runtime_launcher(const char *arch)
{
    const char *env_override = getenv("SPUMANTE_LAUNCHER");
    if (env_override && env_override[0])
        return spm_strdup(env_override);
    char *dir = spm_runtime_dir(arch);
    if (!dir)
        return NULL;
    char *launcher = spm_path_join(dir, "bin/spumante");
    free(dir);
    return launcher;
}

char *spm_launcher_app_bundle(void)
{
    const char *bundle = spm_self_bundle();
    if (bundle && spm_has_suffix_ci(bundle, "/Spumante.app"))
        return spm_strdup(bundle);
    char *parent = spm_bundle_parent();
    char *sibling = spm_path_join(parent, "Spumante.app");
    free(parent);
    if (spm_is_dir(sibling))
        return sibling;
    free(sibling);
#ifdef __APPLE__
    char *ls_bundle = spm_launchservices_bundle();
    if (ls_bundle)
        return ls_bundle;
#endif
    return bundle ? spm_strdup(bundle) : NULL;
}

char *spm_launcher_app_binary(void)
{
    char *bundle = spm_launcher_app_bundle();
    if (bundle) {
        char *bin = spm_path_join(bundle, "Contents/MacOS/Spumante");
        free(bundle);
        if (spm_is_executable_file(bin))
            return bin;
        free(bin);
    }
    return spm_strdup(spm_self_exe());
}

static char *spm_find_ci_in_dir(const char *dir, const char *name)
{
    char *exact = spm_path_join(dir, name);
    if (spm_is_file(exact))
        return exact;
    free(exact);
    DIR *d = opendir(dir);
    if (!d)
        return NULL;
    char *found = NULL;
    struct dirent *ent;
    while ((ent = readdir(d)) && !found) {
        if (strcasecmp(ent->d_name, name) == 0)
            found = spm_path_join(dir, ent->d_name);
    }
    closedir(d);
    return found;
}

char *spm_find_windows_app(const char *exe_name)
{
    char *found = NULL;
    char *apps_dir = spm_find_resource("WindowsApps", 1);
    if (apps_dir) {
        found = spm_find_ci_in_dir(apps_dir, exe_name);
        free(apps_dir);
    }
    if (!found) {
        char *rt = spm_runtime_dir("x86");
        if (rt) {
            char *win_dir = spm_path_join(rt, "lib/wine/x86_64-windows");
            found = spm_find_ci_in_dir(win_dir, exe_name);
            free(win_dir);
            free(rt);
        }
    }
    return found;
}

char *spm_wineserver_socket_path(const char *prefix)
{
    if (!prefix || !prefix[0])
        return NULL;
    struct stat st;
    if (stat(prefix, &st) != 0)
        return NULL;
    return spm_asprintf("/tmp/.wine-%u/server-%llx-%llx/socket",
                        (unsigned int)getuid(),
                        (unsigned long long)(uint64_t)st.st_dev,
                        (unsigned long long)(uint64_t)st.st_ino);
}

int spm_wineserver_is_running(const char *socket_path)
{
    if (!socket_path || !socket_path[0])
        return 0;
    struct stat st;
    if (stat(socket_path, &st) != 0)
        return 0;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
    return (rc == 0) ? 1 : 0;
}

char *spm_login_agent_plist_path(void)
{
    const char *custom_dir = getenv("SPUMANTE_LAUNCH_AGENTS_DIR");
    if (custom_dir && custom_dir[0])
        return spm_path_join(custom_dir, "org.spumante.tray.plist");
    char *agents_dir = spm_path_join(spm_home_dir(), "Library/LaunchAgents");
    char *p = spm_path_join(agents_dir, "org.spumante.tray.plist");
    free(agents_dir);
    return p;
}

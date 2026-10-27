/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: user configuration, Wine registry cascade and Seatbelt profile generation.
 */

#include "spm_config.h"
#include "spm_paths.h"
#include "spm_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static spm_json *spm_int_list(const int *vals, int n)
{
    spm_json *arr = spm_json_array();
    for (int i = 0; i < n; i++)
        spm_json_append(arr, spm_json_int(vals[i]));
    return arr;
}

spm_json *spm_config_defaults(void)
{
    spm_json *cfg = spm_json_object();
    spm_json_set_int(cfg, "schema_version", 1);

    spm_json *env = spm_json_object();
    spm_json_set_str(env, "windows_version", "win11");
    spm_json_set_bool(env, "retina_mode", 1);
    spm_json_set_bool(env, "virtual_desktop", 0);
    spm_json_set_str(env, "virtual_desktop_resolution", "1920x1080");
    spm_json_set_str(env, "prefix_mode", "shared");
    spm_json_set_str(env, "default_prefix", "~/.spumante-prefix");
    spm_json_set(env, "dll_overrides", spm_json_object());
    spm_json_set(cfg, "wine_environment", env);

    spm_json *sec = spm_json_object();
    spm_json_set_bool(sec, "require_executable_bit", 1);
    spm_json_set_bool(sec, "block_quarantined", 1);
    spm_json_set_bool(sec, "require_authenticode", 0);
    spm_json_set_bool(sec, "block_wx_sections", 1);
    spm_json_set_bool(sec, "block_suspicious_iat", 1);
    spm_json_set_bool(sec, "virustotal_enabled", 1);
    spm_json_set_str(sec, "virustotal_api_key", "");
    spm_json_set(cfg, "security", sec);

    spm_json *sb = spm_json_object();
    spm_json_set_bool(sb, "enabled", 1);
    spm_json_set_bool(sb, "strip_z_drive", 1);
    spm_json_set_bool(sb, "disable_winemenubuilder", 1);
    spm_json_set_bool(sb, "protect_sensitive_host_paths", 1);
    spm_json_set(sb, "extra_denied_paths", spm_json_array());
    spm_json_set(sb, "extra_allowed_paths", spm_json_array());
    spm_json_set(cfg, "sandbox", sb);

    static const int default_ports[] = { 80, 443, 53 };
    spm_json *net = spm_json_object();
    spm_json_set_str(net, "incoming", "deny");
    spm_json_set(net, "allowed_incoming_ports", spm_json_array());
    spm_json_set_str(net, "outgoing", "http_https_only");
    spm_json_set(net, "allowed_outgoing_ports", spm_int_list(default_ports, 3));
    spm_json_set(cfg, "network", net);

    spm_json *acc = spm_json_object();
    spm_json_set_bool(acc, "metal_iosurface_swapchain", 1);
    spm_json_set_bool(acc, "videotoolbox_hw_decode", 1);
    spm_json_set_bool(acc, "arm64ec_jit_cache", 1);
    spm_json_set(cfg, "acceleration", acc);

    spm_json_set(cfg, "app_profiles", spm_json_object());
    spm_json_set(cfg, "favorite_apps", spm_json_array());
    return cfg;
}

char *spm_config_path(void)
{
    const char *env_cfg = getenv("SPUMANTE_CONFIG");
    if (env_cfg && env_cfg[0])
        return spm_abs_path(env_cfg);
    char *def = spm_asprintf("%s/.config/spumante/config.json", spm_home_dir());
    char *abs = spm_abs_path(def);
    free(def);
    return abs;
}

char *spm_config_sandbox_dir(void)
{
    char *cfg = spm_config_path();
    char *dir = spm_dirname(cfg);
    char *sb = spm_path_join(dir, "sandbox");
    free(cfg);
    free(dir);
    return sb;
}

/* Python-compatible deep merge: nested objects merge except app_profiles, everything else replaces. */
static void spm_deep_merge(spm_json *base, const spm_json *override)
{
    for (size_t i = 0; i < override->count; i++) {
        const char *k = override->keys[i];
        const spm_json *ov = override->items[i];
        spm_json *cur = spm_json_get(base, k);
        if (ov->type == SPM_JOBJ && cur && cur->type == SPM_JOBJ && strcmp(k, "app_profiles") != 0)
            spm_deep_merge(cur, ov);
        else
            spm_json_set(base, k, spm_json_clone(ov));
    }
}

int spm_config_save(const spm_json *cfg, const char *path)
{
    char *text = spm_json_dump(cfg, 2);
    size_t len = strlen(text);
    char *with_nl = spm_asprintf("%s\n", text);
    free(text);
    int rc = spm_write_file(path, with_nl, len + 1, 0600);
    chmod(path, 0600);
    free(with_nl);
    return rc;
}

spm_json *spm_config_load(const char *path, int create_if_missing)
{
    spm_json *cfg = spm_config_defaults();
    if (!spm_is_file(path)) {
        if (create_if_missing)
            spm_config_save(cfg, path);
        return cfg;
    }
    size_t len = 0;
    unsigned char *raw = spm_read_file(path, &len);
    spm_json *parsed = raw ? spm_json_parse((const char *)raw, len) : NULL;
    free(raw);
    if (parsed && parsed->type == SPM_JOBJ)
        spm_deep_merge(cfg, parsed);
    spm_json_free(parsed);
    return cfg;
}

static char *spm_trim(const char *s, size_t n)
{
    while (n && isspace((unsigned char)*s)) {
        s++;
        n--;
    }
    while (n && isspace((unsigned char)s[n - 1]))
        n--;
    return spm_strndup(s, n);
}

static int spm_is_int_str(const char *s)
{
    if (*s == '-')
        s++;
    if (!*s)
        return 0;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s))
            return 0;
    return 1;
}

static spm_json *spm_parse_scalar_or_list(const char *raw)
{
    char *s = spm_trim(raw, strlen(raw));
    spm_json *result = NULL;
    if (strcasecmp(s, "true") == 0)
        result = spm_json_bool(1);
    else if (strcasecmp(s, "false") == 0)
        result = spm_json_bool(0);
    else if (spm_is_int_str(s))
        result = spm_json_int(strtoll(s, NULL, 10));
    else if (strchr(s, ',')) {
        spm_json *arr = spm_json_array();
        int ok = 1;
        char *copy = spm_strdup(s);
        char *save = NULL;
        for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            char *part = spm_trim(tok, strlen(tok));
            if (part[0]) {
                if (spm_is_int_str(part) && part[0] != '-')
                    spm_json_append(arr, spm_json_int(strtoll(part, NULL, 10)));
                else
                    ok = 0;
            }
            free(part);
        }
        free(copy);
        if (ok)
            result = arr;
        else
            spm_json_free(arr);
    }
    if (!result)
        result = spm_json_string(s);
    free(s);
    return result;
}

static int spm_ports_equal(const spm_json *arr, const int *vals, int n)
{
    if (!arr || arr->type != SPM_JARR || (int)arr->count != n)
        return 0;
    for (int i = 0; i < n; i++)
        if (spm_json_num(arr->items[i], -1) != vals[i])
            return 0;
    return 1;
}

void spm_config_apply_updates(spm_json *cfg, const char *const *updates, int n_updates)
{
    for (int u = 0; u < n_updates; u++) {
        const char *item = updates[u];
        const char *eq = strchr(item, '=');
        if (!eq)
            continue;
        char *key_expr = spm_trim(item, (size_t)(eq - item));
        spm_json *val = spm_parse_scalar_or_list(eq + 1);

        if (strcmp(key_expr, "favorite_apps.add") == 0 ||
            strcmp(key_expr, "favorite_apps.rename") == 0 ||
            strcmp(key_expr, "favorite_apps.remove") == 0 ||
            strcmp(key_expr, "favorite_apps.clear") == 0) {
            char *raw_str = spm_trim(eq + 1, strlen(eq + 1));
            spm_json *favs = spm_json_get(cfg, "favorite_apps");
            if (!favs || favs->type != SPM_JARR || strcmp(key_expr, "favorite_apps.clear") == 0) {
                spm_json_set(cfg, "favorite_apps", spm_json_array());
                favs = spm_json_get(cfg, "favorite_apps");
            }
            if ((strcmp(key_expr, "favorite_apps.add") == 0 ||
                 strcmp(key_expr, "favorite_apps.rename") == 0) && raw_str[0]) {
                const char *sep = strchr(raw_str, '|');
                char *label = NULL;
                char *app_path = NULL;
                if (sep) {
                    label = spm_trim(raw_str, (size_t)(sep - raw_str));
                    app_path = spm_trim(sep + 1, strlen(sep + 1));
                } else {
                    app_path = spm_strdup(raw_str);
                }
                if (!label || !label[0]) {
                    free(label);
                    const char *base = app_path;
                    for (const char *p = app_path; *p; p++) {
                        if (*p == '/' || *p == '\\')
                            base = p + 1;
                    }
                    label = spm_strdup(base[0] ? base : app_path);
                }
                if (app_path[0]) {
                    int updated_existing = 0;
                    for (size_t i = 0; i < favs->count; i++) {
                        spm_json *elem = favs->items[i];
                        const char *cur_path = (elem && elem->type == SPM_JOBJ)
                            ? spm_json_str(spm_json_get(elem, "path"), "")
                            : spm_json_str(elem, "");
                        if (strcmp(cur_path, app_path) == 0) {
                            if (elem->type == SPM_JOBJ) {
                                spm_json_set_str(elem, "label", label);
                                spm_json_set_str(elem, "path", app_path);
                            } else {
                                spm_json *obj = spm_json_object();
                                spm_json_set_str(obj, "label", label);
                                spm_json_set_str(obj, "path", app_path);
                                spm_json_free(favs->items[i]);
                                favs->items[i] = obj;
                            }
                            updated_existing = 1;
                            break;
                        }
                    }
                    if (!updated_existing) {
                        spm_json *obj = spm_json_object();
                        spm_json_set_str(obj, "label", label);
                        spm_json_set_str(obj, "path", app_path);
                        spm_json_append(favs, obj);
                    }
                }
                free(label);
                free(app_path);
            } else if (strcmp(key_expr, "favorite_apps.remove") == 0 && raw_str[0]) {
                spm_json *filtered = spm_json_array();
                for (size_t i = 0; i < favs->count; i++) {
                    spm_json *elem = favs->items[i];
                    const char *cur_path = (elem && elem->type == SPM_JOBJ)
                        ? spm_json_str(spm_json_get(elem, "path"), "")
                        : spm_json_str(elem, "");
                    const char *cur_label = (elem && elem->type == SPM_JOBJ)
                        ? spm_json_str(spm_json_get(elem, "label"), "")
                        : "";
                    if (strcmp(cur_path, raw_str) != 0 && strcmp(cur_label, raw_str) != 0)
                        spm_json_append(filtered, spm_json_clone(elem));
                }
                spm_json_set(cfg, "favorite_apps", filtered);
            }
            free(raw_str);
            spm_json_free(val);
            free(key_expr);
            continue;
        }

        if (strncmp(key_expr, "app_profiles.", 13) == 0) {
            char *rest = spm_trim(key_expr + 13, strlen(key_expr + 13));
            char *last_dot = strrchr(rest, '.');
            if (last_dot) {
                char *app_name = spm_trim(rest, (size_t)(last_dot - rest));
                char *field = spm_trim(last_dot + 1, strlen(last_dot + 1));
                spm_json *profiles = spm_json_get(cfg, "app_profiles");
                if (!profiles || profiles->type != SPM_JOBJ) {
                    spm_json_set(cfg, "app_profiles", spm_json_object());
                    profiles = spm_json_get(cfg, "app_profiles");
                }
                spm_json *prof = spm_json_get(profiles, app_name);
                if (!prof || prof->type != SPM_JOBJ) {
                    spm_json_set(profiles, app_name, spm_json_object());
                    prof = spm_json_get(profiles, app_name);
                }
                spm_json_set(prof, field, val);
                val = NULL;
                free(app_name);
                free(field);
            }
            free(rest);
            spm_json_free(val);
            free(key_expr);
            continue;
        }

        /* Collect non-empty dotted parts */
        char *copy = spm_strdup(key_expr);
        char *parts[32];
        int n_parts = 0;
        char *save = NULL;
        for (char *tok = strtok_r(copy, ".", &save); tok && n_parts < 32; tok = strtok_r(NULL, ".", &save)) {
            char *t = spm_trim(tok, strlen(tok));
            if (t[0])
                parts[n_parts++] = t;
            else
                free(t);
        }
        spm_json *cur = cfg;
        for (int i = 0; i < n_parts - 1; i++) {
            spm_json *next = spm_json_get(cur, parts[i]);
            if (!next || next->type != SPM_JOBJ) {
                spm_json_set(cur, parts[i], spm_json_object());
                next = spm_json_get(cur, parts[i]);
            }
            cur = next;
        }
        if (n_parts > 0) {
            spm_json_set(cur, parts[n_parts - 1], val);
            val = NULL;
        }
        for (int i = 0; i < n_parts; i++)
            free(parts[i]);
        free(copy);
        spm_json_free(val);
        free(key_expr);
    }

    /* Keep allowed_outgoing_ports synchronized when the outgoing mode changes */
    spm_json *net = spm_json_get(cfg, "network");
    if (net && net->type == SPM_JOBJ) {
        const char *out = spm_json_str(spm_json_get(net, "outgoing"), "");
        spm_json *ports = spm_json_get(net, "allowed_outgoing_ports");
        static const int web[] = { 80, 443, 53 }, web2[] = { 80, 443 }, dns[] = { 53 };
        if (strcmp(out, "http_https_only") == 0) {
            if (!spm_json_truthy(ports, 0) || spm_ports_equal(ports, web2, 2) || spm_ports_equal(ports, dns, 1))
                spm_json_set(net, "allowed_outgoing_ports", spm_int_list(web, 3));
        } else if (strcmp(out, "dns_only") == 0)
            spm_json_set(net, "allowed_outgoing_ports", spm_int_list(dns, 1));
        else if (strcmp(out, "deny_all") == 0)
            spm_json_set(net, "allowed_outgoing_ports", spm_json_array());
    }
}

char *spm_config_prefix_path(const spm_json *cfg)
{
    const char *env_prefix = getenv("WINEPREFIX");
    if (env_prefix && env_prefix[0])
        return spm_abs_path(env_prefix);
    const char *def = spm_json_str(spm_json_path(cfg, "wine_environment.default_prefix"), "~/.spumante-prefix");
    return spm_abs_path(def);
}

/*
 * Non-destructively update or insert keys in a Wine .reg section. Mirrors the previous Python regex
 * ^(\[SECTION\][^\n]*\n)(.*?)(?=^\[|\Z) with MULTILINE|DOTALL semantics.
 */
static char *spm_upsert_reg_section(const char *reg_text, const char *section, const char *const *keys,
                                    const char *const *vals, int n)
{
    size_t text_len = strlen(reg_text);
    char *header_prefix = spm_asprintf("[%s]", section);
    size_t hp_len = strlen(header_prefix);
    const char *match = NULL;
    for (const char *line = reg_text; line && *line;) {
        if (strncmp(line, header_prefix, hp_len) == 0) {
            match = line;
            break;
        }
        const char *nl = strchr(line, '\n');
        line = nl ? nl + 1 : NULL;
    }
    free(header_prefix);

    spm_buf out;
    spm_buf_init(&out);
    if (match) {
        const char *header_end = strchr(match, '\n');
        if (!header_end) {
            /* Header without trailing newline at EOF: treat as empty body */
            spm_buf_append(&out, reg_text, text_len);
            spm_buf_putc(&out, '\n');
            for (int i = 0; i < n; i++)
                spm_buf_printf(&out, "\"%s\"=%s\n", keys[i], vals[i]);
            spm_buf_putc(&out, '\n');
            return spm_buf_steal(&out);
        }
        header_end++;
        const char *body = header_end;
        const char *body_end = body;
        while (*body_end) {
            if (*body_end == '[' && (body_end == reg_text || body_end[-1] == '\n'))
                break;
            const char *nl = strchr(body_end, '\n');
            body_end = nl ? nl + 1 : body_end + strlen(body_end);
        }
        int *used = calloc((size_t)n ? (size_t)n : 1, sizeof(int));
        spm_buf section_buf;
        spm_buf_init(&section_buf);
        spm_buf_append(&section_buf, match, (size_t)(header_end - match));
        spm_buf lines;
        spm_buf_init(&lines);
        int first = 1;
        for (const char *ln = body; ln < body_end;) {
            const char *nl = memchr(ln, '\n', (size_t)(body_end - ln));
            size_t l_len = nl ? (size_t)(nl - ln) : (size_t)(body_end - ln);
            char *line = spm_strndup(ln, l_len);
            if (l_len && line[l_len - 1] == '\r')
                line[l_len - 1] = '\0';
            const char *stripped = line;
            while (isspace((unsigned char)*stripped))
                stripped++;
            int replaced = 0;
            for (int i = 0; i < n; i++) {
                if (used[i])
                    continue;
                char *kp = spm_asprintf("\"%s\"=", keys[i]);
                if (strncmp(stripped, kp, strlen(kp)) == 0) {
                    if (!first)
                        spm_buf_putc(&lines, '\n');
                    spm_buf_printf(&lines, "\"%s\"=%s", keys[i], vals[i]);
                    used[i] = 1;
                    replaced = 1;
                }
                free(kp);
                if (replaced)
                    break;
            }
            if (!replaced) {
                if (!first)
                    spm_buf_putc(&lines, '\n');
                spm_buf_puts(&lines, line);
            }
            first = 0;
            free(line);
            ln = nl ? nl + 1 : body_end;
        }
        for (int i = 0; i < n; i++) {
            if (used[i])
                continue;
            if (!first)
                spm_buf_putc(&lines, '\n');
            spm_buf_printf(&lines, "\"%s\"=%s", keys[i], vals[i]);
            first = 0;
        }
        /* rstrip("\n") then "\n\n" */
        while (lines.len && lines.data[lines.len - 1] == '\n')
            lines.data[--lines.len] = '\0';
        if (lines.data)
            spm_buf_append(&section_buf, lines.data, lines.len);
        spm_buf_puts(&section_buf, "\n\n");
        spm_buf_free(&lines);
        free(used);

        spm_buf_append(&out, reg_text, (size_t)(match - reg_text));
        spm_buf_append(&out, section_buf.data, section_buf.len);
        spm_buf_puts(&out, body_end);
        spm_buf_free(&section_buf);
        return spm_buf_steal(&out);
    }

    size_t keep = text_len;
    if (!(text_len >= 2 && reg_text[text_len - 1] == '\n' && reg_text[text_len - 2] == '\n')) {
        while (keep && reg_text[keep - 1] == '\n')
            keep--;
        spm_buf_append(&out, reg_text, keep);
        spm_buf_puts(&out, "\n\n");
    } else
        spm_buf_append(&out, reg_text, text_len);
    spm_buf_printf(&out, "[%s] 1700000000\n", section);
    for (int i = 0; i < n; i++)
        spm_buf_printf(&out, "\"%s\"=%s\n", keys[i], vals[i]);
    spm_buf_putc(&out, '\n');
    return spm_buf_steal(&out);
}

static char *spm_upsert1(char *reg, const char *section, const char *key, const char *val)
{
    const char *keys[1] = { key };
    const char *vals[1] = { val };
    char *updated = spm_upsert_reg_section(reg, section, keys, vals, 1);
    free(reg);
    return updated;
}

static int spm_path_present(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

spm_json *spm_config_cascade(const spm_json *cfg, const char *prefix)
{
    char *target_prefix = prefix ? spm_strdup(prefix) : spm_config_prefix_path(cfg);
    char *dosdevices = spm_path_join(target_prefix, "dosdevices");
    spm_mkdir_p(dosdevices, 0755);
    {
        static const char *const win_subdirs[] = {
            "drive_c/windows/system32",
            "drive_c/windows/syswow64",
            "drive_c/windows/temp",
        };
        for (int i = 0; i < 3; i++) {
            char *sub = spm_path_join(target_prefix, win_subdirs[i]);
            spm_mkdir_p(sub, 0755);
            free(sub);
        }
        char *c_link = spm_path_join(dosdevices, "c:");
        if (!spm_path_present(c_link)) {
            if (symlink("../drive_c", c_link) != 0) {
                /* best effort */
            }
        }
        free(c_link);
    }

    int z_stripped = 0;
    if (spm_json_truthy(spm_json_path(cfg, "sandbox.strip_z_drive"), 1)) {
        static const char *const z_names[] = { "z:", "Z:" };
        for (int i = 0; i < 2; i++) {
            char *zp = spm_path_join(dosdevices, z_names[i]);
            if (spm_path_present(zp))
                unlink(zp);
            free(zp);
        }
        char *z_lower = spm_path_join(dosdevices, "z:");
        struct stat st;
        z_stripped = stat(z_lower, &st) != 0;
        free(z_lower);
    }

    /* Keep .update-timestamp synchronized with the bundled wine.inf so wineboot does not hang when Z: is stripped */
    char *system_reg = spm_path_join(target_prefix, "system.reg");
    char *rt = spm_runtime_dir("x86");
    if (rt && spm_is_file(system_reg)) {
        char *wine_inf = spm_path_join(rt, "share/wine/wine.inf");
        struct stat st;
        if (stat(wine_inf, &st) == 0) {
            char *ts_file = spm_path_join(target_prefix, ".update-timestamp");
            char *want = spm_asprintf("%lld", (long long)st.st_mtime);
            size_t cur_len = 0;
            unsigned char *cur = spm_read_file(ts_file, &cur_len);
            char *cur_trim = cur ? spm_trim((const char *)cur, cur_len) : spm_strdup("");
            if (strcmp(cur_trim, want) != 0) {
                char *line = spm_asprintf("%s\n", want);
                spm_write_file(ts_file, line, strlen(line), 0644);
                free(line);
            }
            free(cur);
            free(cur_trim);
            free(want);
            free(ts_file);
        }
        free(wine_inf);
    }
    free(rt);
    free(system_reg);

    char *user_reg = spm_path_join(target_prefix, "user.reg");
    char *reg = NULL;
    if (spm_is_file(user_reg)) {
        size_t len = 0;
        reg = (char *)spm_read_file(user_reg, &len);
    }
    if (!reg)
        reg = spm_strdup("WINE REGISTRY Version 2\n;; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000\n\n");

    const char *win_ver = spm_json_str(spm_json_path(cfg, "wine_environment.windows_version"), "win11");
    int retina = spm_json_truthy(spm_json_path(cfg, "wine_environment.retina_mode"), 1);
    char *ver_val = spm_asprintf("\"%s\"", win_ver);
    reg = spm_upsert1(reg, "Software\\\\Wine", "Version", ver_val);
    free(ver_val);
    reg = spm_upsert1(reg, "Software\\\\Wine\\\\Mac Driver", "RetinaMode", retina ? "\"y\"" : "\"n\"");
    reg = spm_upsert1(reg, "Software\\\\Wine\\\\Direct3D", "csmt", "dword:00000003");

    /* DllOverrides */
    const char *ov_keys[64];
    char *ov_vals[64];
    int n_ov = 0;
    if (spm_json_truthy(spm_json_path(cfg, "sandbox.disable_winemenubuilder"), 1)) {
        ov_keys[n_ov] = "winemenubuilder.exe";
        ov_vals[n_ov++] = spm_strdup("\"\"");
    }
    const spm_json *dll_ov = spm_json_path(cfg, "wine_environment.dll_overrides");
    if (dll_ov && dll_ov->type == SPM_JOBJ) {
        for (size_t i = 0; i < dll_ov->count && n_ov < 64; i++) {
            const char *val = spm_json_str(dll_ov->items[i], "");
            int replaced = 0;
            for (int j = 0; j < n_ov; j++) {
                if (strcmp(ov_keys[j], dll_ov->keys[i]) == 0) {
                    free(ov_vals[j]);
                    ov_vals[j] = spm_asprintf("\"%s\"", val);
                    replaced = 1;
                }
            }
            if (!replaced) {
                ov_keys[n_ov] = dll_ov->keys[i];
                ov_vals[n_ov++] = spm_asprintf("\"%s\"", val);
            }
        }
    }
    if (n_ov) {
        char *updated = spm_upsert_reg_section(reg, "Software\\\\Wine\\\\DllOverrides", ov_keys,
                                               (const char *const *)ov_vals, n_ov);
        free(reg);
        reg = updated;
    }
    for (int i = 0; i < n_ov; i++)
        free(ov_vals[i]);

    const spm_json *profiles = spm_json_get(cfg, "app_profiles");
    if (profiles && profiles->type == SPM_JOBJ) {
        for (size_t i = 0; i < profiles->count; i++) {
            const spm_json *prof = profiles->items[i];
            const spm_json *pv = spm_json_get(prof, "windows_version");
            if (prof->type != SPM_JOBJ || !pv)
                continue;
            char *section = spm_asprintf("Software\\\\Wine\\\\AppDefaults\\\\%s", profiles->keys[i]);
            char *val = pv->type == SPM_JSTR ? spm_asprintf("\"%s\"", pv->str) : spm_asprintf("\"%lld\"", spm_json_num(pv, 0));
            reg = spm_upsert1(reg, section, "Version", val);
            free(section);
            free(val);
        }
    }

    spm_write_file(user_reg, reg, strlen(reg), 0644);
    free(reg);

    spm_json *info = spm_json_object();
    spm_json_set_str(info, "prefix_dir", target_prefix);
    spm_json_set_str(info, "user_reg", user_reg);
    spm_json_set_bool(info, "z_drive_stripped", z_stripped);
    free(user_reg);
    free(dosdevices);
    free(target_prefix);
    return info;
}

static spm_json *spm_effective_config(const spm_json *cfg, const char *exe_name)
{
    spm_json *eff = spm_json_clone(cfg);
    if (exe_name) {
        const spm_json *prof = spm_json_get(spm_json_get(cfg, "app_profiles"), exe_name);
        if (prof && prof->type == SPM_JOBJ)
            spm_deep_merge(eff, prof);
    }
    return eff;
}

char *spm_seatbelt_generate(const spm_json *cfg, const char *exe_name)
{
    spm_json *eff = spm_effective_config(cfg, exe_name);
    const spm_json *sb = spm_json_get(eff, "sandbox");
    const spm_json *net = spm_json_get(eff, "network");
    const char *home = spm_home_dir();

    spm_buf b;
    spm_buf_init(&b);
    spm_buf_puts(&b, ";; Auto-generated by Spumante (native runtime)\n(version 1)\n(allow default)\n\n");
    spm_buf_puts(&b, ";; Always allow local Unix domain sockets for wineserver IPC\n(allow network* (local unix))\n");

    if (spm_json_truthy(spm_json_get(sb, "protect_sensitive_host_paths"), 1)) {
        spm_buf_puts(&b, "\n;; Protect sensitive macOS host credential and configuration directories\n(deny file-read* file-write*\n");
        static const char *const rel[] = { ".ssh", ".aws", ".gnupg", "Library/Keychains", ".config/spumante" };
        for (int i = 0; i < 5; i++)
            spm_buf_printf(&b, "    (subpath \"%s/%s\")\n", home, rel[i]);
        const spm_json *extra = spm_json_get(sb, "extra_denied_paths");
        if (extra && extra->type == SPM_JARR)
            for (size_t i = 0; i < extra->count; i++)
                spm_buf_printf(&b, "    (subpath \"%s\")\n", spm_json_str(extra->items[i], ""));
        spm_buf_puts(&b, ")\n");
    }

    char incoming[64];
    snprintf(incoming, sizeof(incoming), "%s", spm_json_str(spm_json_get(net, "incoming"), "deny"));
    for (char *p = incoming; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    spm_buf_printf(&b, "\n;; Incoming network policy: %s\n", incoming);
    if (strcmp(incoming, "allow") == 0)
        spm_buf_puts(&b, "(allow network-inbound)\n");
    else if (strcmp(incoming, "custom_ports") == 0) {
        spm_buf_puts(&b, "(deny network-inbound)\n");
        const spm_json *ports = spm_json_get(net, "allowed_incoming_ports");
        if (ports && ports->type == SPM_JARR)
            for (size_t i = 0; i < ports->count; i++)
                spm_buf_printf(&b, "(allow network-inbound (local tcp \"*:%lld\"))\n", spm_json_num(ports->items[i], 0));
    } else
        spm_buf_puts(&b, "(deny network-inbound)\n");

    char outgoing[64];
    snprintf(outgoing, sizeof(outgoing), "%s", spm_json_str(spm_json_get(net, "outgoing"), "http_https_only"));
    for (char *p = outgoing; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    spm_buf_printf(&b, "\n;; Outgoing network policy: %s\n", outgoing);
    if (strcmp(outgoing, "allow_all") == 0)
        spm_buf_puts(&b, "(allow network-outbound)\n");
    else if (strcmp(outgoing, "deny_all") == 0)
        spm_buf_puts(&b, "(deny network-outbound)\n(allow network-outbound (local unix))\n");
    else if (strcmp(outgoing, "dns_only") == 0)
        spm_buf_puts(&b, "(deny network-outbound)\n(allow network-outbound (local unix))\n"
                         "(allow network-outbound (remote udp \"*:53\") (remote tcp \"*:53\"))\n");
    else {
        spm_buf_puts(&b, "(deny network-outbound)\n(allow network-outbound (local unix))\n"
                         "(allow network-outbound (remote udp \"*:53\") (remote tcp \"*:53\"))\n");
        const spm_json *ports = spm_json_get(net, "allowed_outgoing_ports");
        int any = ports && ports->type == SPM_JARR && ports->count > 0;
        if (any) {
            for (size_t i = 0; i < ports->count; i++) {
                long long port = spm_json_num(ports->items[i], 0);
                if (port != 53)
                    spm_buf_printf(&b, "(allow network-outbound (remote tcp \"*:%lld\"))\n", port);
            }
        } else if (strcmp(outgoing, "http_https_only") == 0)
            spm_buf_puts(&b, "(allow network-outbound (remote tcp \"*:80\"))\n(allow network-outbound (remote tcp \"*:443\"))\n");
    }
    spm_json_free(eff);
    return spm_buf_steal(&b);
}

char *spm_seatbelt_write(const spm_json *cfg, const char *exe_name)
{
    char *dir = spm_config_sandbox_dir();
    spm_mkdir_p(dir, 0700);
    char *tag = spm_strdup(exe_name ? exe_name : "default");
    for (char *p = tag; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '.' || *p == '-'))
            *p = '_';
    char *fname = spm_asprintf("spumante_%s.sb", tag);
    char *path = spm_path_join(dir, fname);
    char *profile = spm_seatbelt_generate(cfg, exe_name);
    spm_write_file(path, profile, strlen(profile), 0600);
    free(profile);
    free(fname);
    free(tag);
    free(dir);
    return path;
}

spm_json *spm_config_status(const char *const *updates, int n_updates, const char *exe_name)
{
    char *cfg_path = spm_config_path();
    spm_json *cfg = spm_config_load(cfg_path, 1);
    if (n_updates > 0) {
        spm_config_apply_updates(cfg, updates, n_updates);
        spm_config_save(cfg, cfg_path);
    }
    spm_json *cascade = spm_config_cascade(cfg, NULL);
    char *sb_path = spm_seatbelt_write(cfg, exe_name);
    size_t sb_len = 0;
    unsigned char *sb_text = spm_read_file(sb_path, &sb_len);

    spm_json *status = spm_json_object();
    spm_json_set_str(status, "action", n_updates > 0 ? "set-config" : "config-status");
    spm_json_set_str(status, "config_path", cfg_path);
    spm_json_set(status, "config", cfg);
    spm_json_set(status, "cascade", cascade);
    spm_json_set_str(status, "seatbelt_profile_path", sb_path);
    spm_json_set_str(status, "seatbelt_profile_preview", sb_text ? (const char *)sb_text : "");
    free(sb_text);
    free(sb_path);
    free(cfg_path);
    return status;
}

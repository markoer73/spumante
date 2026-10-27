/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: 3-gate pre-execution PE security scanner.
 */

#include "spm_scan.h"
#include "spm_config.h"
#include "spm_sha256.h"
#include "spm_util.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>

#ifdef __APPLE__
#include <sys/xattr.h>
#endif

const char *const spm_builtin_gui_apps[] = {
    "notepad.exe", "winmine.exe", "clock.exe", "taskmgr.exe", "wordpad.exe",
    "regedit.exe", "winecfg.exe", "progman.exe", "explorer.exe",
};
const char *const spm_builtin_cli_apps[] = {
    "cmd.exe", "hostname.exe", "ipconfig.exe", "systeminfo.exe", "tasklist.exe",
};
const int spm_builtin_gui_count = (int)(sizeof(spm_builtin_gui_apps) / sizeof(spm_builtin_gui_apps[0]));
const int spm_builtin_cli_count = (int)(sizeof(spm_builtin_cli_apps) / sizeof(spm_builtin_cli_apps[0]));

int spm_is_builtin_app_name(const char *name)
{
    for (int i = 0; i < spm_builtin_gui_count; i++)
        if (strcasecmp(name, spm_builtin_gui_apps[i]) == 0)
            return 1;
    for (int i = 0; i < spm_builtin_cli_count; i++)
        if (strcasecmp(name, spm_builtin_cli_apps[i]) == 0)
            return 1;
    return strcasecmp(name, "winemine.exe") == 0;
}

static int spm_env_is(const char *name, const char *value)
{
    const char *v = getenv(name);
    return v && strcmp(v, value) == 0;
}

static uint16_t rd16(const unsigned char *d, size_t off)
{
    return (uint16_t)(d[off] | (d[off + 1] << 8));
}

static uint32_t rd32(const unsigned char *d, size_t off)
{
    return (uint32_t)d[off] | ((uint32_t)d[off + 1] << 8) | ((uint32_t)d[off + 2] << 16) | ((uint32_t)d[off + 3] << 24);
}

static int spm_is_trusted_builtin(const char *path, const unsigned char *data, size_t len)
{
    if (spm_env_is("SPUMANTE_SKIP_SECURITY_GATES", "1") || spm_env_is("SPUMANTE_BUILTIN_APP_LAUNCH", "1"))
        return 1;
    if (len >= 0x4C && memcmp(data + 0x40, "Wine builtin", 12) == 0)
        return 1;
    char resolved[PATH_MAX];
    const char *rp = realpath(path, resolved) ? resolved : path;
    static const char *const markers[] = {
        "/builds/wine-install-", "/builds/wine-full-", "/Contents/Resources/WindowsApps/",
        "/Contents/Resources/wine-install-", "/builds/dmg_staging/Windows Apps/", "/Volumes/Spumante/Windows Apps/",
    };
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++)
        if (strstr(rp, markers[i]) && spm_is_builtin_app_name(spm_basename(rp)))
            return 1;
    return 0;
}

static double spm_entropy(const unsigned char *blob, size_t len)
{
    if (!len)
        return 0.0;
    size_t counts[256] = { 0 };
    for (size_t i = 0; i < len; i++)
        counts[blob[i]]++;
    double ent = 0.0;
    for (int i = 0; i < 256; i++) {
        if (counts[i]) {
            double p = (double)counts[i] / (double)len;
            ent -= p * log2(p);
        }
    }
    return round(ent * 10000.0) / 10000.0;
}

static spm_json *spm_gate1(const char *path)
{
    spm_json *findings = spm_json_array();
    char *quarantine = spm_strdup("");
    char *where_froms = spm_strdup("");
#ifdef __APPLE__
    char qbuf[1024];
    ssize_t qlen = getxattr(path, "com.apple.quarantine", qbuf, sizeof(qbuf) - 1, 0, 0);
    if (qlen > 0) {
        qbuf[qlen] = '\0';
        free(quarantine);
        quarantine = spm_strdup(qbuf);
        char *f = spm_asprintf("macOS quarantine attribute present (com.apple.quarantine=%s)", quarantine);
        spm_json_append(findings, spm_json_string(f));
        free(f);
    }
    unsigned char wbuf[2048];
    ssize_t wlen = getxattr(path, "com.apple.metadata:kMDItemWhereFroms", wbuf, sizeof(wbuf), 0, 0);
    if (wlen > 0) {
        char *hex = malloc((size_t)wlen * 2 + 1);
        spm_hex_encode(wbuf, (size_t)wlen, hex);
        free(where_froms);
        where_froms = hex;
    }
#endif
    char *zone = spm_asprintf("%s:Zone.Identifier", path);
    size_t zlen = 0;
    unsigned char *ztxt = spm_is_file(zone) ? spm_read_file(zone, &zlen) : NULL;
    if (ztxt && (spm_memmem(ztxt, zlen, "ZoneId=3", 8) || spm_memmem(ztxt, zlen, "ZoneId=4", 8))) {
        if (!quarantine[0]) {
            free(quarantine);
            quarantine = spm_strdup("Zone.Identifier:ZoneId=3");
        }
        spm_json_append(findings, spm_json_string("Windows Mark-of-the-Web (Zone.Identifier ZoneId=3) present"));
    }
    free(ztxt);
    free(zone);

    int quarantined = findings->count > 0;
    spm_json *g = spm_json_object();
    spm_json_set_bool(g, "passed", !quarantined);
    spm_json_set_bool(g, "quarantined", quarantined);
    spm_json_set_str(g, "quarantine_attribute", quarantine);
    spm_json_set_str(g, "where_froms", where_froms);
    spm_json_set(g, "findings", findings);
    free(quarantine);
    free(where_froms);
    return g;
}

static spm_json *spm_gate2(const unsigned char *data, size_t len, int require_signature)
{
    spm_json *findings = spm_json_array();
    int is_signed = 0, valid = 0, tampered = 0;
    unsigned char digest[32];
    spm_sha256(data, len, digest);

    if (len >= 0x46 && data[0] == 'M' && data[1] == 'Z') {
        uint32_t e_lfanew = rd32(data, 0x3C);
        if ((size_t)e_lfanew + 24 <= len && memcmp(data + e_lfanew, "PE\0\0", 4) == 0) {
            uint16_t opt_size = rd16(data, e_lfanew + 20);
            size_t opt_off = (size_t)e_lfanew + 24;
            if (opt_off + opt_size <= len && opt_size >= 96) {
                uint16_t magic = rd16(data, opt_off);
                size_t chk_off = opt_off + 64;
                size_t dd4_off = opt_off + (magic == 0x020B ? 144 : 128);
                uint32_t cert_offset = 0, cert_size = 0;
                if (dd4_off + 8 <= opt_off + opt_size) {
                    cert_offset = rd32(data, dd4_off);
                    cert_size = rd32(data, dd4_off + 4);
                }
                size_t hash_end = (cert_offset > 0 && cert_offset <= len) ? cert_offset : len;
                if (dd4_off + 8 <= hash_end) {
                    spm_sha256_ctx ctx;
                    spm_sha256_init(&ctx);
                    spm_sha256_update(&ctx, data, chk_off);
                    spm_sha256_update(&ctx, data + chk_off + 4, dd4_off - (chk_off + 4));
                    spm_sha256_update(&ctx, data + dd4_off + 8, hash_end - (dd4_off + 8));
                    spm_sha256_final(&ctx, digest);
                }
                if (cert_offset > 0 && cert_size >= 8) {
                    is_signed = 1;
                    if ((size_t)cert_offset + cert_size > len) {
                        tampered = 1;
                        spm_json_append(findings, spm_json_string("PE Certificate Table extends past end of file"));
                    } else {
                        uint32_t dw_len = rd32(data, cert_offset);
                        uint16_t w_rev = rd16(data, cert_offset + 4);
                        uint16_t w_type = rd16(data, cert_offset + 6);
                        size_t end_a = (size_t)cert_offset + dw_len, end_b = (size_t)cert_offset + cert_size;
                        size_t pkcs7_end = end_a < end_b ? end_a : end_b;
                        size_t der_off = (size_t)cert_offset + 8;
                        size_t der_len = pkcs7_end > der_off ? pkcs7_end - der_off : 0;
                        static const unsigned char oid_signed_data[] = { 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
                                                                         0xf7, 0x0d, 0x01, 0x07, 0x02 };
                        if (w_rev == 0x0200 && w_type == 0x0002 && der_len >= 16 && data[der_off] == 0x30 &&
                            spm_memmem(data + der_off, der_len, oid_signed_data, sizeof(oid_signed_data)) &&
                            spm_memmem(data + der_off, der_len, digest, 32))
                            valid = 1;
                        else {
                            tampered = 1;
                            spm_json_append(findings, spm_json_string(
                                "PE Authenticode signature verification failed: digest mismatch or tampered binary"));
                        }
                    }
                }
            }
        }
    }
    if (!is_signed && require_signature)
        spm_json_append(findings, spm_json_string("PE binary is unsigned and --require-signature policy is active"));

    char hex[65];
    spm_hex_encode(digest, 32, hex);
    spm_json *g = spm_json_object();
    spm_json_set_bool(g, "passed", !tampered && (require_signature ? valid : 1));
    spm_json_set_bool(g, "signed", is_signed);
    spm_json_set_bool(g, "signature_valid", valid);
    spm_json_set_bool(g, "tampered", tampered);
    spm_json_set_str(g, "authenticode_sha256", hex);
    spm_json_set(g, "findings", findings);
    return g;
}

static spm_json *spm_vt_result(int scanned, const char *status, long long mal, long long sus, long long harm,
                               long long und)
{
    spm_json *r = spm_json_object();
    spm_json_set_bool(r, "scanned", scanned);
    spm_json_set_str(r, "status", status);
    spm_json_set_int(r, "malicious", mal);
    spm_json_set_int(r, "suspicious", sus);
    spm_json_set_int(r, "harmless", harm);
    spm_json_set_int(r, "undetected", und);
    return r;
}

/* VirusTotal v3 lookup via the system curl binary; the API key is passed on stdin, never on the command line. */
static spm_json *spm_query_virustotal(const char *sha256_hex, const char *api_key)
{
    if (!api_key || !api_key[0])
        return spm_vt_result(0, "skipped_no_api_key", 0, 0, 0, 0);
    const char *base = getenv("VT_API_BASE_URL");
    if (!base || !base[0])
        base = "https://www.virustotal.com";
    char *base_trim = spm_strdup(base);
    size_t bl = strlen(base_trim);
    while (bl && base_trim[bl - 1] == '/')
        base_trim[--bl] = '\0';
    char *url = spm_asprintf("%s/api/v3/files/%s", base_trim, sha256_hex);
    free(base_trim);
    const char *curl = spm_is_executable_file("/usr/bin/curl") ? "/usr/bin/curl" : "curl";
    char *argv[] = { (char *)curl, "-sS", "-m", "10", "-H", "@-", "-H", "Accept: application/json",
                     "-A", "spumante-security-scanner/1.0", "-w", "\n%{http_code}", url, NULL };
    char *header = spm_asprintf("x-apikey: %s\n", api_key);
    char *out = NULL;
    int status = 0;
    int rc = spm_run_capture(argv, NULL, NULL, header, 20, &out, &status);
    free(header);
    free(url);
    if (rc != 0 || !out) {
        free(out);
        return spm_vt_result(0, "network_error:curl_unavailable", 0, 0, 0, 0);
    }
    char *last_nl = strrchr(out, '\n');
    long code = last_nl ? strtol(last_nl + 1, NULL, 10) : 0;
    if (last_nl)
        *last_nl = '\0';
    spm_json *result;
    if (code == 200) {
        spm_json *payload = spm_json_parse(out, strlen(out));
        const spm_json *stats = spm_json_path(payload, "data.attributes.last_analysis_stats");
        result = spm_vt_result(1, "found", spm_json_num(spm_json_get(stats, "malicious"), 0),
                               spm_json_num(spm_json_get(stats, "suspicious"), 0),
                               spm_json_num(spm_json_get(stats, "harmless"), 0),
                               spm_json_num(spm_json_get(stats, "undetected"), 0));
        spm_json_free(payload);
    } else if (code == 404)
        result = spm_vt_result(1, "not_found_in_vt", 0, 0, 0, 0);
    else if (code > 0) {
        char *st = spm_asprintf("http_error_%ld", code);
        result = spm_vt_result(0, st, 0, 0, 0, 0);
        free(st);
    } else {
        int exit_code = (status >= 0 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
        char *st = spm_asprintf("network_error:curl_exit_%d", exit_code);
        result = spm_vt_result(0, st, 0, 0, 0, 0);
        free(st);
    }
    free(out);
    return result;
}

static spm_json *spm_gate3(const unsigned char *data, size_t len, const char *sha256_hex, const char *vt_key)
{
    spm_json *findings = spm_json_array();
    spm_json *wx = spm_json_array();
    spm_json *high_ent = spm_json_array();
    double max_ent = 0.0;
    uint16_t dll_chars = 0;

    if (len >= 0x46 && data[0] == 'M' && data[1] == 'Z') {
        uint32_t e_lfanew = rd32(data, 0x3C);
        if ((size_t)e_lfanew + 24 <= len && memcmp(data + e_lfanew, "PE\0\0", 4) == 0) {
            uint16_t num_sections = rd16(data, e_lfanew + 6);
            uint16_t opt_size = rd16(data, e_lfanew + 20);
            size_t opt_off = (size_t)e_lfanew + 24;
            if (opt_off + 72 <= len)
                dll_chars = rd16(data, opt_off + 70);
            size_t sec_base = opt_off + opt_size;
            for (unsigned idx = 0; idx < num_sections; idx++) {
                size_t so = sec_base + (size_t)idx * 40;
                if (so + 40 > len)
                    break;
                char name[9];
                memcpy(name, data + so, 8);
                name[8] = '\0';
                char *sec_name = name[0] ? spm_strdup(name) : spm_asprintf("sec_%u", idx);
                uint32_t raw_size = rd32(data, so + 16);
                uint32_t raw_ptr = rd32(data, so + 20);
                uint32_t chars = rd32(data, so + 36);
                if ((chars & 0xA0000000u) == 0xA0000000u) {
                    spm_json_append(wx, spm_json_string(sec_name));
                    char *f = spm_asprintf("W^X section violation: section '%s' has both WRITE and EXECUTE permissions", sec_name);
                    spm_json_append(findings, spm_json_string(f));
                    free(f);
                }
                if (raw_size > 0 && (size_t)raw_ptr + raw_size <= len) {
                    double ent = spm_entropy(data + raw_ptr, raw_size);
                    if (ent > max_ent)
                        max_ent = ent;
                    if (ent > 7.2 && raw_size >= 64)
                        spm_json_append(high_ent, spm_json_string(sec_name));
                }
                free(sec_name);
            }
        }
    }

    spm_json *dc = spm_json_object();
    spm_json_set_int(dc, "raw", dll_chars);
    spm_json_set_bool(dc, "high_entropy_va", (dll_chars & 0x0020) != 0);
    spm_json_set_bool(dc, "dynamic_base_aslr", (dll_chars & 0x0040) != 0);
    spm_json_set_bool(dc, "force_integrity", (dll_chars & 0x0080) != 0);
    spm_json_set_bool(dc, "nx_compat_dep", (dll_chars & 0x0100) != 0);
    spm_json_set_bool(dc, "guard_cf", (dll_chars & 0x4000) != 0);

#define HAS(s) (spm_memmem(data, len, s, sizeof(s) - 1) != NULL)
    spm_json *susp = spm_json_array();
    if (HAS("VirtualAllocEx") && HAS("WriteProcessMemory") &&
        (HAS("CreateRemoteThread") || HAS("NtCreateThreadEx") || HAS("QueueUserAPC"))) {
        spm_json_append(susp, spm_json_string("process_injection:VirtualAllocEx+WriteProcessMemory+CreateRemoteThread"));
        spm_json_append(findings, spm_json_string("Suspicious IAT combination detected: remote process injection primitives "
                                                  "(VirtualAllocEx + WriteProcessMemory + CreateRemoteThread)"));
    }
    if (HAS("NtUnmapViewOfSection") && HAS("WriteProcessMemory")) {
        spm_json_append(susp, spm_json_string("process_hollowing:NtUnmapViewOfSection+WriteProcessMemory"));
        spm_json_append(findings, spm_json_string("Suspicious IAT combination detected: process hollowing primitives "
                                                  "(NtUnmapViewOfSection + WriteProcessMemory)"));
    }
    if (HAS("MiniDumpWriteDump") && spm_str_contains_ci((const char *)data, len, "lsass")) {
        spm_json_append(susp, spm_json_string("credential_dumping:MiniDumpWriteDump+lsass"));
        spm_json_append(findings, spm_json_string("Suspicious IAT combination detected: LSASS credential dumping primitives"));
    }
#undef HAS

    spm_json *vt = spm_query_virustotal(sha256_hex, vt_key);
    long long malicious = spm_json_num(spm_json_get(vt, "malicious"), 0);
    if (malicious > 0) {
        char *f = spm_asprintf("VirusTotal v3 flagged executable as malicious (%lld detections)", malicious);
        spm_json_append(findings, spm_json_string(f));
        free(f);
    }

    spm_json *g = spm_json_object();
    spm_json_set_bool(g, "passed", wx->count == 0 && susp->count == 0 && malicious == 0);
    spm_json_set(g, "wx_sections", wx);
    spm_json_set(g, "high_entropy_sections", high_ent);
    spm_json_set(g, "max_section_entropy", spm_json_double(max_ent));
    spm_json_set(g, "dll_characteristics", dc);
    spm_json_set(g, "suspicious_imports", susp);
    spm_json_set(g, "virustotal", vt);
    spm_json_set(g, "findings", findings);
    return g;
}

static void spm_mark_passed(spm_json *gate, int clear_findings)
{
    spm_json_set_bool(gate, "passed", 1);
    if (clear_findings)
        spm_json_set(gate, "findings", spm_json_array());
}

spm_json *spm_scan_pe(const char *path, int require_signature, const char *vt_api_key, int *out_passed)
{
    *out_passed = 0;
    if (!spm_is_file(path)) {
        spm_json *r = spm_json_object();
        spm_json_set_bool(r, "passed", 0);
        spm_json_set_str(r, "target", path);
        char *err = spm_asprintf("File not found: %s", path);
        spm_json_set_str(r, "error", err);
        free(err);
        spm_json_set(r, "findings", spm_json_array());
        spm_json_set(r, "gates", spm_json_object());
        return r;
    }
    size_t len = 0;
    unsigned char *data = spm_read_file(path, &len);
    if (!data)
        data = (unsigned char *)spm_strdup("");
    unsigned char sha[32];
    spm_sha256(data, len, sha);
    char sha_hex[65];
    spm_hex_encode(sha, 32, sha_hex);

    char *cfg_path = spm_config_path();
    spm_json *cfg = spm_config_load(cfg_path, 1);
    free(cfg_path);
    const spm_json *sec = spm_json_get(cfg, "security");
    int trusted = spm_is_trusted_builtin(path, data, len);
    int skip = spm_env_is("SPUMANTE_SKIP_SECURITY_GATES", "1");

    int cfg_req_auth;
    if (spm_env_is("SPUMANTE_REQUIRE_AUTHENTICODE", "0"))
        cfg_req_auth = 0;
    else if (spm_env_is("SPUMANTE_REQUIRE_AUTHENTICODE", "1"))
        cfg_req_auth = 1;
    else
        cfg_req_auth = spm_json_truthy(spm_json_get(sec, "require_authenticode"), 0);
    int require_sig = require_signature || (cfg_req_auth && !trusted);

    int vt_enabled = spm_json_truthy(spm_json_get(sec, "virustotal_enabled"), 1) && !skip;
    const char *vt_key = vt_api_key;
    if (!vt_key || !vt_key[0])
        vt_key = getenv("VT_API_KEY");
    if (!vt_key || !vt_key[0])
        vt_key = getenv("VIRUSTOTAL_API_KEY");
    if ((!vt_key || !vt_key[0]) && vt_enabled)
        vt_key = spm_json_str(spm_json_get(sec, "virustotal_api_key"), "");

    spm_json *g1 = spm_gate1(path);
    if (!spm_json_truthy(spm_json_get(sec, "block_quarantined"), 1) || skip || trusted)
        spm_mark_passed(g1, skip || trusted);
    spm_json *g2 = spm_gate2(data, len, require_sig);
    if ((skip || trusted) && !require_signature)
        spm_mark_passed(g2, 1);
    spm_json *g3 = spm_gate3(data, len, sha_hex, skip ? NULL : vt_key);
    if (skip)
        spm_mark_passed(g3, 1);

    spm_json *cascade = spm_config_cascade(cfg, NULL);
    const char *exe_name = spm_basename(path);
    char *sb_path = spm_seatbelt_write(cfg, exe_name);
    int sandbox_on = spm_json_truthy(spm_json_path(cfg, "sandbox.enabled"), 1) &&
                     !spm_env_is("SPUMANTE_NO_SANDBOX", "1") && !skip;

    int passed = spm_json_truthy(spm_json_get(g1, "passed"), 0) && spm_json_truthy(spm_json_get(g2, "passed"), 0) &&
                 spm_json_truthy(spm_json_get(g3, "passed"), 0);
    spm_json *all = spm_json_array();
    spm_json *gates_list[3] = { g1, g2, g3 };
    for (int i = 0; i < 3; i++) {
        const spm_json *f = spm_json_get(gates_list[i], "findings");
        for (size_t j = 0; f && j < f->count; j++)
            spm_json_append(all, spm_json_clone(f->items[j]));
    }

    char resolved[PATH_MAX];
    spm_json *r = spm_json_object();
    spm_json_set_bool(r, "passed", passed);
    spm_json_set_str(r, "target", realpath(path, resolved) ? resolved : path);
    spm_json_set_str(r, "file_sha256", sha_hex);
    spm_json_set_str(r, "authenticode_sha256", spm_json_str(spm_json_get(g2, "authenticode_sha256"), ""));
    spm_json_set_bool(r, "require_executable_bit", 1);
    spm_json_set_bool(r, "trusted_builtin_wine_pe", trusted);
    spm_json_set_bool(r, "security_gates_skipped", skip);
    spm_json_set_bool(r, "sandbox_enabled", sandbox_on);
    spm_json_set_str(r, "seatbelt_profile", sb_path);
    spm_json_set_bool(r, "z_drive_stripped", spm_json_truthy(spm_json_get(cascade, "z_drive_stripped"), 0));
    spm_json_set(r, "findings", all);
    spm_json *gates = spm_json_object();
    spm_json_set(gates, "gate1_quarantine_provenance", g1);
    spm_json_set(gates, "gate2_authenticode_signature", g2);
    spm_json_set(gates, "gate3_static_pe_and_virustotal", g3);
    spm_json_set(r, "gates", gates);

    spm_json_free(cascade);
    spm_json_free(cfg);
    free(sb_path);
    free(data);
    *out_passed = passed;
    return r;
}

void spm_scan_print_findings(const spm_json *report, const char *path)
{
    const spm_json *findings = spm_json_get(report, "findings");
    const char *err = spm_json_str(spm_json_get(report, "error"), NULL);
    if (err)
        fprintf(stderr, "[Spumante Security Gate] BLOCKED '%s': %s\n", path, err);
    for (size_t i = 0; findings && i < findings->count; i++)
        fprintf(stderr, "[Spumante Security Gate] BLOCKED '%s': %s\n", path, spm_json_str(findings->items[i], ""));
}

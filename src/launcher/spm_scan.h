/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: 3-gate pre-execution PE security scanner.
 *   Gate 1: macOS quarantine / provenance (com.apple.quarantine, kMDItemWhereFroms, Zone.Identifier)
 *   Gate 2: PE Authenticode digest + WIN_CERTIFICATE PKCS#7 SignedData verification
 *   Gate 3: static PE hardening (W^X, entropy, DllCharacteristics, IAT threats) + optional VirusTotal v3
 */

#ifndef SPUMANTE_SPM_SCAN_H
#define SPUMANTE_SPM_SCAN_H

#include "spm_json.h"

extern const char *const spm_builtin_gui_apps[];
extern const char *const spm_builtin_cli_apps[];
extern const int spm_builtin_gui_count;
extern const int spm_builtin_cli_count;
int spm_is_builtin_app_name(const char *name);

/* Returns the full scan report; *out_passed receives the overall verdict. */
spm_json *spm_scan_pe(const char *path, int require_signature, const char *vt_api_key, int *out_passed);
/* Print each finding as "[Spumante Security Gate] BLOCKED '<path>': <finding>" on stderr. */
void spm_scan_print_findings(const spm_json *report, const char *path);

#endif /* SPUMANTE_SPM_SCAN_H */

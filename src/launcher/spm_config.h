/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: user configuration, Wine registry cascade and Seatbelt profile generation.
 */

#ifndef SPUMANTE_SPM_CONFIG_H
#define SPUMANTE_SPM_CONFIG_H

#include "spm_json.h"

spm_json *spm_config_defaults(void);
/* $SPUMANTE_CONFIG or ~/.config/spumante/config.json (absolute). Caller frees. */
char *spm_config_path(void);
/* Directory for generated Seatbelt profiles (<config dir>/sandbox). Caller frees. */
char *spm_config_sandbox_dir(void);
spm_json *spm_config_load(const char *path, int create_if_missing);
int spm_config_save(const spm_json *cfg, const char *path);
void spm_config_apply_updates(spm_json *cfg, const char *const *updates, int n_updates);
/* $WINEPREFIX or wine_environment.default_prefix (absolute). Caller frees. */
char *spm_config_prefix_path(const spm_json *cfg);
/* Cascade config into <prefix>/user.reg and dosdevices/. prefix may be NULL. Returns a cascade info object. */
spm_json *spm_config_cascade(const spm_json *cfg, const char *prefix);
char *spm_seatbelt_generate(const spm_json *cfg, const char *exe_name);
/* Write the profile for exe_name (NULL = default) and return its path. Caller frees. */
char *spm_seatbelt_write(const spm_json *cfg, const char *exe_name);
/* Load (and optionally update) config, cascade, write profile, and build the full status report. */
spm_json *spm_config_status(const char *const *updates, int n_updates, const char *exe_name);

#endif /* SPUMANTE_SPM_CONFIG_H */

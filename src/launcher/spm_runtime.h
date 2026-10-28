/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: drive_c sync, runtime trace, built-in app launcher, issue reporting and UI reports.
 */

#ifndef SPUMANTE_SPM_RUNTIME_H
#define SPUMANTE_SPM_RUNTIME_H

#include "spm_json.h"

#define SPM_GITHUB_REPO_URL "https://github.com/markoer73/Spumante"
#define SPM_APPDB_DOC_URL SPM_GITHUB_REPO_URL "/blob/main/docs/APPDB.md"

/* Environment applied to built-in app launches (security gates and sandbox temporarily disabled). */
extern const char *const spm_builtin_launch_env[];
extern const int spm_builtin_launch_env_count;

spm_json *spm_sync_drive_c(const char *prefix_override);
spm_json *spm_runtime_trace(const char *resolved_exe, const char *pe_arch, const char *launcher,
                            const char *working_dir, int is_msi, int extra_argc, char **extra_argv);
spm_json *spm_launch_builtin(const char *app_name, int extra_argc, char **extra_argv, int dry_run);
spm_json *spm_launch_builtin_console(const char *app_name, int extra_argc, char **extra_argv, int open_console,
                                     int dry_run);
spm_json *spm_issue_report(const char *kind, const char *app_name, const char *rating, int dry_run);
spm_json *spm_config_ui_report(void);

/* Persistent Wine session management (wineserver -p) */
spm_json *spm_session_status(const char *prefix_override);
spm_json *spm_session_start(const char *prefix_override, int dry_run);
spm_json *spm_session_stop(const char *prefix_override, int dry_run);

/* Prefix initialization (spumante wineboot --init && wineserver -w) */
spm_json *spm_init_prefix(const char *prefix_override, int dry_run);

/* macOS launchd Login Agent (org.spumante.tray.plist) */
spm_json *spm_login_agent_status(void);
spm_json *spm_login_agent_install(const char *app_bin);
spm_json *spm_login_agent_uninstall(void);

/* Tray mode dry-run / execution report */
spm_json *spm_tray_report(int dry_run);

/* Spawn argv detached (new session, stdio -> /dev/null). Returns pid or -1. */
int spm_spawn_detached(char *const argv[], char *const envp[], const char *cwd);

#endif /* SPUMANTE_SPM_RUNTIME_H */

/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: bundle-relative path resolution.
 *
 * No absolute build-machine path is ever compiled in. Every resource is located relative to the
 * running executable (Spumante.app/Contents/MacOS/<exe>):
 *   1. <this bundle>/Contents/Resources/<name>                (DMG / installed layout)
 *   2. <parent dir>/Spumante.app/Contents/Resources/<name>    (Spumante Config.app next to Spumante.app)
 *   3. <parent dir>/<name>                                    (developer layout: builds/Spumante.app + builds/<name>)
 *   4. LaunchServices lookup of org.spumante.launcher          (macOS only, last resort)
 */

#ifndef SPUMANTE_SPM_PATHS_H
#define SPUMANTE_SPM_PATHS_H

#define SPM_BUNDLE_ID "org.spumante.launcher"
#define SPM_CONFIG_BUNDLE_ID "org.spumante.config"

/* Absolute, symlink-resolved path of the running executable (cached, do not free). */
const char *spm_self_exe(void);
/* Path of the enclosing .app bundle, or NULL when not running from a bundle (cached, do not free). */
const char *spm_self_bundle(void);
/* Directory containing the enclosing bundle (or the executable's directory). Caller frees. */
char *spm_bundle_parent(void);

/* Locate a resource directory/file by relative name using the search order above. Caller frees; NULL if absent. */
char *spm_find_resource(const char *rel_name, int want_dir);

/* Wine runtime directory for "x86" or "arm64" (wine-install-<arch>), or NULL. Caller frees. */
char *spm_runtime_dir(const char *arch);
/* Runtime launcher script (<runtime>/bin/spumante) honoring SPUMANTE_LAUNCHER. Caller frees; NULL if absent. */
char *spm_runtime_launcher(const char *arch);
/* Path to the Spumante.app main executable (this binary when it is the launcher). Caller frees. */
char *spm_launcher_app_binary(void);
/* Path to the main Spumante.app bundle, preferring this bundle. Caller frees. */
char *spm_launcher_app_bundle(void);
/* Locate a bundled built-in Windows program by file name (case-insensitive). Caller frees; NULL if absent. */
char *spm_find_windows_app(const char *exe_name);

/* Wineserver Unix domain socket path for a prefix directory. Caller frees; NULL if stat fails. */
char *spm_wineserver_socket_path(const char *prefix);
/* Check if a wineserver process is live and accepting connections on the socket. */
int spm_wineserver_is_running(const char *socket_path);
/* Path to the macOS LaunchAgent plist (org.spumante.tray.plist). Caller frees. */
char *spm_login_agent_plist_path(void);

#endif /* SPUMANTE_SPM_PATHS_H */

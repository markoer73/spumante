/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: native AppKit Installer and Uninstaller GUI applications.
 */

#ifndef SPUMANTE_SPM_INSTALLER_H
#define SPUMANTE_SPM_INSTALLER_H

#ifdef __cplusplus
extern "C" {
#endif

int handle_installer_ui(int argc, char *argv[], int dry_run);
int handle_uninstaller_ui(int argc, char *argv[], int dry_run);

#ifdef __cplusplus
}
#endif

#endif /* SPUMANTE_SPM_INSTALLER_H */

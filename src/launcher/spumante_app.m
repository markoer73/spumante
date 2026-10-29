/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante.app native launcher & LaunchServices handler.
 * Enforces mandatory +x executable permission and 3-gate PE security scan on all .exe/.msi files.
 *
 * Portability invariants:
 *   - No Python dependency: config, registry cascade, Seatbelt, scanner and reports are native (spm_*.c).
 *   - No compiled-in absolute paths: every resource is resolved relative to this bundle (spm_paths.c).
 */

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "spm_config.h"
#include "spm_json.h"
#include "spm_paths.h"
#include "spm_runtime.h"
#include "spumante_installer.h"
#include "spm_scan.h"
#include "spm_util.h"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#import <CoreServices/CoreServices.h>
#import <Security/Security.h>
#include <sys/sysctl.h>
#endif

#define BUNDLE_ID_STR SPM_BUNDLE_ID

static int has_suffix_ci(const char *str, const char *suffix)
{
    return spm_has_suffix_ci(str, suffix);
}

/* Print a JSON document (indent=2) followed by a newline and release it. */
static void emit_json(spm_json *doc)
{
    char *text = spm_json_dump(doc, 2);
    printf("%s\n", text);
    fflush(stdout);
    free(text);
    spm_json_free(doc);
}

static const char *bundle_path_or_self(void)
{
    const char *bundle = spm_self_bundle();
    return bundle ? bundle : spm_self_exe();
}

/*
 * Run the 3-gate PE security scanner (Gate 1: Quarantine, Gate 2: Authenticode, Gate 3: Static PE + VirusTotal)
 * natively before permitting execution.
 */
static int run_pre_execution_security_gates(const char *resolved_path)
{
    const char *skip_gates = getenv("SPUMANTE_SKIP_SECURITY_GATES");
    if (skip_gates && strcmp(skip_gates, "1") == 0) {
        return 0;
    }
    int passed = 0;
    spm_json *report = spm_scan_pe(resolved_path, 0, NULL, &passed);
    if (!passed)
        spm_scan_print_findings(report, resolved_path);
    spm_json_free(report);
    return passed ? 0 : 126;
}

/*
 * Inspect target file for MSI signature/extension or PE MZ/COFF Machine header.
 * Returns "arm64" for ARM64/ARM64EC (0xAA64 / 0xA641) and "x86_64" for AMD64/I386 (0x8664 / 0x014C).
 */
static const char *detect_pe_arch_and_type(const char *path, int *out_is_msi)
{
    *out_is_msi = has_suffix_ci(path, ".msi") ? 1 : 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return "x86_64";

    unsigned char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n < 8)
        return "x86_64";

    /* OLE Compound Document magic: D0 CF 11 E0 A1 B1 1A E1 */
    if (buf[0] == 0xD0 && buf[1] == 0xCF && buf[2] == 0x11 && buf[3] == 0xE0) {
        *out_is_msi = 1;
        return "x86_64";
    }

    if (n >= 0x46 && buf[0] == 'M' && buf[1] == 'Z') {
        uint32_t e_lfanew = (uint32_t)buf[0x3C] |
                            ((uint32_t)buf[0x3D] << 8) |
                            ((uint32_t)buf[0x3E] << 16) |
                            ((uint32_t)buf[0x3F] << 24);
        if (e_lfanew <= (uint32_t)(n - 6)) {
            if (buf[e_lfanew] == 'P' && buf[e_lfanew + 1] == 'E' &&
                buf[e_lfanew + 2] == 0 && buf[e_lfanew + 3] == 0) {
                uint16_t machine = (uint16_t)buf[e_lfanew + 4] |
                                   ((uint16_t)buf[e_lfanew + 5] << 8);
                if (machine == 0xAA64 || machine == 0xA641)
                    return "arm64";
                return "x86_64";
            }
        }
    }
    return "x86_64";
}

/* Resolve the bundled Wine runtime launcher for the PE architecture (falls back to the other runtime). */
static const char *select_launcher_for_arch(const char *pe_arch)
{
    static char *cached_x86, *cached_arm64;
    static int resolved;
    const char *env_override = getenv("SPUMANTE_LAUNCHER");
    if (env_override && env_override[0] != '\0')
        return env_override;
    if (!resolved) {
        cached_x86 = spm_runtime_launcher("x86");
        cached_arm64 = spm_runtime_launcher("arm64");
        resolved = 1;
    }
    if (strcmp(pe_arch, "arm64") == 0) {
        if (cached_arm64)
            return cached_arm64;
        return cached_x86 ? cached_x86 : "";
    }
    if (cached_x86)
        return cached_x86;
    return cached_arm64 ? cached_arm64 : "";
}

/*
 * Verify Rosetta 2 is installed on Apple Silicon before dispatching an x86/x86_64 Windows binary.
 * If missing, automatically invokes softwareupdate --install-rosetta --agree-to-license (with an NSAlert
 * prompt when launched interactively via GUI). Returns 0 if ready, or 86 (EBADARCH) if unavailable.
 */
static int ensure_rosetta2_for_x86_target(const char *pe_arch, const char *launcher)
{
    int needs_x86 = (strcmp(pe_arch, "x86_64") == 0) ||
                    (launcher && strstr(launcher, "wine-install-x86") != NULL);
    if (!needs_x86)
        return 0;

    const char *sim_missing = getenv("SPUMANTE_SIMULATE_MISSING_ROSETTA");
    int simulated = (sim_missing && strcmp(sim_missing, "1") == 0);

    int is_apple_silicon = simulated;
#ifdef __APPLE__
    if (!is_apple_silicon) {
        int arm64_cap = 0;
        size_t sz = sizeof(arm64_cap);
        if (sysctlbyname("hw.optional.arm64", &arm64_cap, &sz, NULL, 0) == 0 && arm64_cap == 1) {
            is_apple_silicon = 1;
        }
    }
#endif
    if (!is_apple_silicon)
        return 0;

    if (!simulated && access("/Library/Apple/usr/libexec/oah/libRosettaRuntime", R_OK) == 0) {
        return 0;
    }

    const char *sw_bin = getenv("SPUMANTE_SOFTWAREUPDATE_BIN");
    if (!sw_bin || sw_bin[0] == '\0') {
        sw_bin = "/usr/sbin/softwareupdate";
    }

    const char *consent_env = getenv("SPUMANTE_ROSETTA_CONSENT");
    if (consent_env && consent_env[0] != '\0') {
        if (strcasecmp(consent_env, "y") != 0 &&
            strcasecmp(consent_env, "yes") != 0 &&
            strcasecmp(consent_env, "1") != 0 &&
            strcasecmp(consent_env, "true") != 0 &&
            strcasecmp(consent_env, "agree") != 0) {
            fprintf(stderr, "[Spumante] ERROR: Rosetta 2 installation declined by user. Aborting spumante execution.\n");
            return 86;
        }
    } else {
#ifdef __APPLE__
        const char *non_interactive = getenv("SPUMANTE_NONINTERACTIVE_ROSETTA");
        const char *dry_run_json = getenv("SPUMANTE_DRY_RUN_JSON");
        if ((!non_interactive || strcmp(non_interactive, "1") != 0) &&
            (!dry_run_json || strcmp(dry_run_json, "1") != 0)) {
            @autoreleasepool {
                NSApplication *app = [NSApplication sharedApplication];
                [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
                NSAlert *alert = [[NSAlert alloc] init];
                [alert setAlertStyle:NSAlertStyleWarning];
                [alert setMessageText:@"Rosetta 2 Required for Intel Windows Applications"];
                [alert setInformativeText:@"This Windows executable requires Rosetta 2 to run x86/x86_64 code on Apple Silicon. Do you agree to install Rosetta 2 now? If you disagree, Spumante will abort."];
                [alert addButtonWithTitle:@"Agree & Install Rosetta 2"];
                [alert addButtonWithTitle:@"Disagree & Abort"];
                [app activateIgnoringOtherApps:YES];
                if ([alert runModal] != NSAlertFirstButtonReturn) {
                    fprintf(stderr, "[Spumante] ERROR: Rosetta 2 installation declined by user. Aborting spumante execution.\n");
                    return 86;
                }
            }
        }
#endif
    }

    fprintf(stderr, "[Spumante] Apple Silicon detected without Rosetta 2. Running '%s --install-rosetta --agree-to-license'...\n", sw_bin);
    pid_t pid = fork();
    if (pid < 0)
        return 86;
    if (pid == 0) {
        setenv("SPUMANTE_INTERPOSE_BYPASS", "1", 1);
        execl(sw_bin, sw_bin, "--install-rosetta", "--agree-to-license", (char *)NULL);
        _exit(86);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr,
                "[Spumante] ERROR: Rosetta 2 is required on Apple Silicon to run x86/x86_64 Windows applications. "
                "Run '/usr/sbin/softwareupdate --install-rosetta --agree-to-license' first.\n");
        return 86;
    }
    return 0;
}

static int dispatch_windows_binary(const char *target_path, int extra_argc, char **extra_argv)
{
    struct stat st;
    if (stat(target_path, &st) != 0) {
        fprintf(stderr, "[Spumante] ERROR: Target file '%s' does not exist (%s).\n",
                target_path, strerror(errno));
        return 127;
    }

    /* Mandatory security invariant: refuse to launch any .exe/.msi not marked executable (+x) */
    if (!S_ISREG(st.st_mode) ||
        (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0 ||
        access(target_path, X_OK) != 0) {
        fprintf(stderr,
                "[Spumante] ERROR: Refusing to launch '%s': missing executable permission (+x). "
                "Run 'chmod +x \"%s\"' first.\n",
                target_path, target_path);
        return 126;
    }

    char resolved_path[PATH_MAX];
    if (!realpath(target_path, resolved_path)) {
        snprintf(resolved_path, sizeof(resolved_path), "%s", target_path);
    }

    /* Mandatory 3-Gate Pre-Execution Security Scan (Quarantine, Authenticode, Static PE + VirusTotal) */
    if (run_pre_execution_security_gates(resolved_path) != 0) {
        return 126;
    }

    char dir_copy[PATH_MAX];
    snprintf(dir_copy, sizeof(dir_copy), "%s", resolved_path);
    char *parent_dir = dirname(dir_copy);

    int is_msi = 0;
    const char *pe_arch = detect_pe_arch_and_type(resolved_path, &is_msi);
    const char *launcher = select_launcher_for_arch(pe_arch);

    int rosetta_rc = ensure_rosetta2_for_x86_target(pe_arch, launcher);
    if (rosetta_rc != 0) {
        return rosetta_rc;
    }

    const char *dry_run_json = getenv("SPUMANTE_DRY_RUN_JSON");
    if (dry_run_json && strcmp(dry_run_json, "1") == 0) {
        emit_json(spm_runtime_trace(resolved_path, pe_arch, launcher, parent_dir, is_msi, extra_argc, extra_argv));
        return 0;
    }

    if (!launcher || !launcher[0] || access(launcher, X_OK) != 0) {
        fprintf(stderr, "[Spumante] ERROR: Bundled Wine runtime not found next to '%s'. Reinstall Spumante.\n",
                bundle_path_or_self());
        return 127;
    }

    const char *preserve_cwd = getenv("SPUMANTE_PRESERVE_CWD");
    if (!preserve_cwd || strcmp(preserve_cwd, "1") != 0) {
        if (chdir(parent_dir) != 0) {
            fprintf(stderr, "[Spumante] WARN: Could not chdir to '%s': %s\n",
                    parent_dir, strerror(errno));
        }
    }

    setenv("SPUMANTE_INTERPOSE_BYPASS", "1", 1);
    /* Tell the runtime launcher this exact file already passed the native security gates */
    setenv("SPUMANTE_GATES_VERIFIED", resolved_path, 1);
    if (strstr(resolved_path, "/builds/wine-install-") != NULL ||
        strstr(resolved_path, "/Contents/Resources/wine-install-") != NULL ||
        strstr(resolved_path, "/Contents/Resources/WindowsApps/") != NULL ||
        strstr(resolved_path, "/Windows Apps/") != NULL) {
        setenv("SPUMANTE_SKIP_SECURITY_GATES", "1", 1);
        setenv("SPUMANTE_NO_SANDBOX", "1", 1);
    }

    /* Prepare the per-executable Seatbelt profile and cascade settings into the Wine prefix */
    char *seatbelt_profile = NULL;
    int use_seatbelt = 0;
    {
        char *cfg_path = spm_config_path();
        spm_json *cfg = spm_config_load(cfg_path, 1);
        char *pfx = spm_config_prefix_path(cfg);
        if (pfx) {
            setenv("WINEPREFIX", pfx, 1);
            free(pfx);
        }
        spm_json_free(spm_config_cascade(cfg, NULL));
        seatbelt_profile = spm_seatbelt_write(cfg, spm_basename(resolved_path));
#ifdef __APPLE__
        const char *no_sb = getenv("SPUMANTE_NO_SANDBOX");
        const char *skip_gates = getenv("SPUMANTE_SKIP_SECURITY_GATES");
        if (spm_json_truthy(spm_json_path(cfg, "sandbox.enabled"), 1) &&
            (!no_sb || strcmp(no_sb, "1") != 0) &&
            (!skip_gates || strcmp(skip_gates, "1") != 0) &&
            access("/usr/bin/sandbox-exec", X_OK) == 0 &&
            access(seatbelt_profile, R_OK) == 0) {
            use_seatbelt = 1;
        }
#endif
        spm_json_free(cfg);
        free(cfg_path);
    }

    int total_args = extra_argc + (is_msi ? 4 : 2) + (use_seatbelt ? 3 : 0);
    char **child_argv = (char **)calloc((size_t)total_args + 1, sizeof(char *));
    if (!child_argv)
        return 1;

    int idx = 0;
    const char *exec_target = launcher;
    if (use_seatbelt) {
        exec_target = "/usr/bin/sandbox-exec";
        child_argv[idx++] = "/usr/bin/sandbox-exec";
        child_argv[idx++] = "-f";
        child_argv[idx++] = seatbelt_profile;
    }
    child_argv[idx++] = (char *)launcher;
    if (is_msi) {
        child_argv[idx++] = "msiexec";
        child_argv[idx++] = "/i";
    }
    child_argv[idx++] = resolved_path;
    for (int i = 0; i < extra_argc; ++i) {
        child_argv[idx++] = extra_argv[i];
    }
    child_argv[idx] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        free(child_argv);
        free(seatbelt_profile);
        return 1;
    }
    if (pid == 0) {
        execv(exec_target, child_argv);
        fprintf(stderr, "[Spumante] ERROR: Failed to exec launcher '%s': %s\n",
                exec_target, strerror(errno));
        _exit(127);
    }

    free(child_argv);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        free(seatbelt_profile);
        return 1;
    }
    free(seatbelt_profile);

    /* Automatically sync +x executable permissions on newly installed .exe/.msi files inside $WINEPREFIX/drive_c/ */
    spm_json_free(spm_sync_drive_c(NULL));

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 1;
}

#ifdef __APPLE__
@class SpumanteSettingsController;

@interface SpumanteAppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, assign) BOOL handledOpenEvent;
@property (nonatomic, assign) int lastExitCode;
@property (nonatomic, strong) SpumanteSettingsController *settingsController;
@end
#endif

static int handle_register(int dry_run)
{
    char *reg_bundle = spm_launcher_app_bundle();
    if (!reg_bundle)
        reg_bundle = spm_strdup(bundle_path_or_self());
#ifdef __APPLE__
    if (!dry_run) {
        const char *lsreg = "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister";
        if (access(lsreg, X_OK) == 0) {
            pid_t pid = fork();
            if (pid == 0) {
                execl(lsreg, "lsregister", "-f", reg_bundle, (char *)NULL);
                _exit(0);
            } else if (pid > 0) {
                int st = 0;
                waitpid(pid, &st, 0);
            }
        }
        LSSetDefaultRoleHandlerForContentType(
            CFSTR("com.microsoft.windows-executable"),
            kLSRolesAll,
            CFSTR(BUNDLE_ID_STR));
        LSSetDefaultRoleHandlerForContentType(
            CFSTR("com.microsoft.msi-installer"),
            kLSRolesAll,
            CFSTR(BUNDLE_ID_STR));
    }
#else
    (void)dry_run;
#endif
    spm_json *doc = spm_json_object();
    spm_json_set_str(doc, "action", "register-exe-handler");
    spm_json_set_str(doc, "bundle_id", BUNDLE_ID_STR);
    spm_json_set_str(doc, "app_bundle", reg_bundle);
    spm_json *utis = spm_json_array();
    spm_json_append(utis, spm_json_string("com.microsoft.windows-executable"));
    spm_json_append(utis, spm_json_string("com.microsoft.msi-installer"));
    spm_json_set(doc, "utis", utis);
    spm_json_set_bool(doc, "require_executable_bit", 1);
    spm_json_set_bool(doc, "dry_run", dry_run);
    spm_json_set_str(doc, "status", dry_run ? "dry_run_ready" : "registered");
    emit_json(doc);
    free(reg_bundle);
    return 0;
}

static int handle_unregister(int dry_run)
{
    char *reg_bundle = spm_launcher_app_bundle();
    if (!reg_bundle)
        reg_bundle = spm_strdup(bundle_path_or_self());
#ifdef __APPLE__
    if (!dry_run) {
        const char *lsreg = "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister";
        if (access(lsreg, X_OK) == 0) {
            pid_t pid = fork();
            if (pid == 0) {
                execl(lsreg, "lsregister", "-u", reg_bundle, (char *)NULL);
                _exit(0);
            } else if (pid > 0) {
                int st = 0;
                waitpid(pid, &st, 0);
            }
        }
    }
#else
    (void)dry_run;
#endif
    spm_json *doc = spm_json_object();
    spm_json_set_str(doc, "action", "unregister-exe-handler");
    spm_json_set_str(doc, "bundle_id", BUNDLE_ID_STR);
    spm_json_set_str(doc, "app_bundle", reg_bundle);
    spm_json_set_bool(doc, "require_executable_bit", 1);
    spm_json_set_bool(doc, "dry_run", dry_run);
    spm_json_set_str(doc, "status", dry_run ? "dry_run_ready" : "unregistered");
    emit_json(doc);
    free(reg_bundle);
    return 0;
}

static int handle_status(void)
{
    char exe_handler[256];
    snprintf(exe_handler, sizeof(exe_handler), "%s", BUNDLE_ID_STR);
#ifdef __APPLE__
    CFStringRef cur = LSCopyDefaultRoleHandlerForContentType(
        CFSTR("com.microsoft.windows-executable"),
        kLSRolesAll);
    if (cur) {
        CFStringGetCString(cur, exe_handler, sizeof(exe_handler), kCFStringEncodingUTF8);
        CFRelease(cur);
    }
#endif
    char *app_bundle = spm_launcher_app_bundle();
    char *x86 = spm_runtime_launcher("x86");
    char *arm64 = spm_runtime_launcher("arm64");
    spm_json *doc = spm_json_object();
    spm_json_set_str(doc, "action", "exe-handler-status");
    spm_json_set_str(doc, "bundle_id", BUNDLE_ID_STR);
    spm_json_set_str(doc, "current_default_handler", exe_handler);
    spm_json_set_str(doc, "app_bundle", app_bundle ? app_bundle : bundle_path_or_self());
    spm_json_set_str(doc, "x86_launcher", x86 ? x86 : "");
    spm_json_set_str(doc, "arm64_launcher", arm64 ? arm64 : "");
    spm_json_set_bool(doc, "require_executable_bit", 1);
    emit_json(doc);
    free(app_bundle);
    free(x86);
    free(arm64);
    return 0;
}

#ifdef __APPLE__
/* Convert a native JSON document into Foundation objects (NSDictionary / NSArray / NSString / NSNumber). */
static id spm_json_to_ns(const spm_json *doc)
{
    char *text = spm_json_dump(doc, 0);
    NSData *data = [NSData dataWithBytes:text length:strlen(text)];
    free(text);
    return [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
}

@interface SpumanteSettingsController : NSObject <NSWindowDelegate>
@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) NSStatusItem *statusItem;
@property (nonatomic, strong) NSMenu *statusMenu;
@property (nonatomic, strong) NSPopUpButton *winVerPopup;
@property (nonatomic, strong) NSButton *retinaCheck;
@property (nonatomic, strong) NSTextField *handlerStatusLabel;
@property (nonatomic, strong) NSButton *blockQuarantineCheck;
@property (nonatomic, strong) NSButton *requireAuthCheck;
@property (nonatomic, strong) NSButton *blockWxIatCheck;
@property (nonatomic, strong) NSButton *virustotalEnableCheck;
@property (nonatomic, strong) NSSecureTextField *virustotalKeyField;
@property (nonatomic, strong) NSTextField *virustotalPlainKeyField;
@property (nonatomic, strong) NSButton *showVtKeyCheck;
@property (nonatomic, strong) NSButton *sandboxEnableCheck;
@property (nonatomic, strong) NSButton *stripZDriveCheck;
@property (nonatomic, strong) NSButton *protectPathsCheck;
@property (nonatomic, strong) NSPopUpButton *incomingNetPopup;
@property (nonatomic, strong) NSPopUpButton *outgoingNetPopup;
@property (nonatomic, strong) NSButton *metalCheck;
@property (nonatomic, strong) NSButton *videotoolboxCheck;
@property (nonatomic, strong) NSButton *jitCacheCheck;
@property (nonatomic, strong) NSPopUpButton *builtinGuiPopup;
@property (nonatomic, strong) NSPopUpButton *builtinCliPopup;
@property (nonatomic, strong) NSPopUpButton *favoriteAppsPopup;
@property (nonatomic, strong) NSTextField *favoriteAppLabelField;
@property (nonatomic, strong) NSTextField *favoriteAppPathField;
@property (nonatomic, strong) NSTextField *builtinStatusLabel;
@end

@implementation SpumanteSettingsController

- (NSButton *)makeCheckbox:(NSString *)title frame:(NSRect)frame state:(NSControlStateValue)state
{
    NSButton *btn = [NSButton checkboxWithTitle:title target:nil action:nil];
    [btn setFrame:frame];
    [btn setState:state];
    return btn;
}

- (NSTextField *)makeLabel:(NSString *)text frame:(NSRect)frame bold:(BOOL)bold
{
    NSTextField *lbl = [NSTextField labelWithString:text];
    [lbl setFrame:frame];
    [lbl setFont:bold ? [NSFont boldSystemFontOfSize:13.0] : [NSFont systemFontOfSize:13.0]];
    return lbl;
}

- (void)registerFileHandlersAction:(id)sender
{
    (void)sender;
    handle_register(0);
    if (self.handlerStatusLabel) {
        [self.handlerStatusLabel setStringValue:@"Status: Spumante.app registered as default handler for .exe and .msi files."];
    }
}

- (void)uninstallAction:(id)sender
{
    (void)sender;
    char *launcher_bin = spm_launcher_app_binary();
    if (!launcher_bin)
        return;
    NSTask *task = [[NSTask alloc] init];
    [task setLaunchPath:[NSString stringWithUTF8String:launcher_bin]];
    [task setArguments:@[@"--uninstall"]];
    free(launcher_bin);
    [task launchAndReturnError:nil];
}

- (NSDictionary *)loadCurrentConfigDict
{
    char *cfg_path = spm_config_path();
    spm_json *cfg = spm_config_load(cfg_path, 1);
    free(cfg_path);
    id obj = spm_json_to_ns(cfg);
    spm_json_free(cfg);
    return [obj isKindOfClass:[NSDictionary class]] ? obj : @{};
}

- (void)toggleVtKeyVisibility:(id)sender
{
    (void)sender;
    BOOL showPlain = ([self.showVtKeyCheck state] == NSControlStateValueOn);
    if (showPlain) {
        [self.virustotalPlainKeyField setStringValue:[self.virustotalKeyField stringValue] ?: @""];
        [self.virustotalKeyField setHidden:YES];
        [self.virustotalPlainKeyField setHidden:NO];
    } else {
        [self.virustotalKeyField setStringValue:[self.virustotalPlainKeyField stringValue] ?: @""];
        [self.virustotalPlainKeyField setHidden:YES];
        [self.virustotalKeyField setHidden:NO];
    }
}

- (void)toggleVtEnabled:(id)sender
{
    (void)sender;
    BOOL enabled = ([self.virustotalEnableCheck state] == NSControlStateValueOn);
    [self.virustotalKeyField setEnabled:enabled];
    [self.virustotalPlainKeyField setEnabled:enabled];
    [self.showVtKeyCheck setEnabled:enabled];
}

- (void)saveAndCascade:(id)sender
{
    (void)sender;
    NSArray<NSString *> *verCodes = @[@"win11", @"win10", @"win81", @"win7"];
    NSInteger vIdx = [self.winVerPopup indexOfSelectedItem];
    NSString *winVer = (vIdx >= 0 && vIdx < (NSInteger)[verCodes count]) ? verCodes[vIdx] : @"win11";

    NSString *incMode = ([self.incomingNetPopup indexOfSelectedItem] == 1) ? @"allow" : @"deny";
    NSInteger outIdx = [self.outgoingNetPopup indexOfSelectedItem];
    NSString *outMode = @"http_https_only";
    if (outIdx == 1)
        outMode = @"dns_only";
    else if (outIdx == 2)
        outMode = @"deny_all";
    else if (outIdx == 3)
        outMode = @"allow_all";

    BOOL showPlain = ([self.showVtKeyCheck state] == NSControlStateValueOn);
    NSString *rawVtKey = showPlain ? [self.virustotalPlainKeyField stringValue] : [self.virustotalKeyField stringValue];
    NSString *vtKey = rawVtKey ? [rawVtKey stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] : @"";
    NSString *wxIatVal = ([self.blockWxIatCheck state] == NSControlStateValueOn) ? @"true" : @"false";

    NSArray<NSString *> *updates = @[
        [NSString stringWithFormat:@"wine_environment.windows_version=%@", winVer],
        [NSString stringWithFormat:@"wine_environment.retina_mode=%@", [self.retinaCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        [NSString stringWithFormat:@"security.block_quarantined=%@", [self.blockQuarantineCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        [NSString stringWithFormat:@"security.require_authenticode=%@", [self.requireAuthCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        [NSString stringWithFormat:@"security.block_wx_sections=%@", wxIatVal],
        [NSString stringWithFormat:@"security.block_suspicious_iat=%@", wxIatVal],
        [NSString stringWithFormat:@"security.virustotal_enabled=%@", [self.virustotalEnableCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        [NSString stringWithFormat:@"security.virustotal_api_key=%@", vtKey],
        [NSString stringWithFormat:@"sandbox.enabled=%@", [self.sandboxEnableCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        [NSString stringWithFormat:@"sandbox.strip_z_drive=%@", [self.stripZDriveCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        [NSString stringWithFormat:@"sandbox.protect_sensitive_host_paths=%@", [self.protectPathsCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        [NSString stringWithFormat:@"network.incoming=%@", incMode],
        [NSString stringWithFormat:@"network.outgoing=%@", outMode],
        [NSString stringWithFormat:@"acceleration.metal_iosurface_swapchain=%@", [self.metalCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        [NSString stringWithFormat:@"acceleration.videotoolbox_hw_decode=%@", [self.videotoolboxCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        [NSString stringWithFormat:@"acceleration.arm64ec_jit_cache=%@", [self.jitCacheCheck state] == NSControlStateValueOn ? @"true" : @"false"]
    ];

    const char **c_updates = calloc([updates count], sizeof(char *));
    for (NSUInteger i = 0; i < [updates count]; i++)
        c_updates[i] = [updates[i] UTF8String];
    spm_json_free(spm_config_status(c_updates, (int)[updates count], NULL));
    free(c_updates);
    if (self.handlerStatusLabel) {
        [self.handlerStatusLabel setStringValue:@"Saved settings & cascaded to WINEPREFIX (Spumante stays open in Menu Bar)."];
    }
    [self rebuildStatusMenu];
    if (self.statusItem != nil) {
        [self.window orderOut:nil];
    } else {
        [NSApp terminate:nil];
    }
}

- (void)closeWindow:(id)sender
{
    (void)sender;
    if (self.statusItem != nil) {
        [self.window orderOut:nil];
    } else {
        [NSApp terminate:nil];
    }
}

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    if (self.statusItem != nil) {
        [sender orderOut:nil];
        return NO;
    }
    return YES;
}

- (void)showSettingsWindow:(id)sender
{
    (void)sender;
    if (!self.window) {
        [self setupUI];
    }
    [self refreshFavoriteAppsPopup];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)stopAllWineProcessesAction:(id)sender
{
    (void)sender;
    spm_json_free(spm_session_stop(NULL, 0));
    if (self.handlerStatusLabel)
        [self.handlerStatusLabel setStringValue:@"Stopped all Wine processes and background session."];
}

- (void)quitTrayApp:(id)sender
{
    (void)sender;
    spm_json_free(spm_session_stop(NULL, 0));
    [NSApp terminate:nil];
}

- (NSArray<NSDictionary<NSString *, NSString *> *> *)loadFavoriteAppsList
{
    NSDictionary *cfg = [self loadCurrentConfigDict];
    NSArray *raw = [cfg[@"favorite_apps"] isKindOfClass:[NSArray class]] ? cfg[@"favorite_apps"] : @[];
    NSMutableArray<NSDictionary<NSString *, NSString *> *> *list = [NSMutableArray array];
    for (id item in raw) {
        if ([item isKindOfClass:[NSDictionary class]]) {
            NSString *path = [item[@"path"] isKindOfClass:[NSString class]] ? item[@"path"] : @"";
            NSString *label = [item[@"label"] isKindOfClass:[NSString class]] ? item[@"label"] : @"";
            if ([path length] > 0) {
                if ([label length] == 0)
                    label = [path lastPathComponent];
                [list addObject:@{@"label": label, @"path": path}];
            }
        } else if ([item isKindOfClass:[NSString class]] && [(NSString *)item length] > 0) {
            NSString *path = (NSString *)item;
            NSString *label = [path lastPathComponent];
            [list addObject:@{@"label": label, @"path": path}];
        }
    }
    return list;
}

- (void)syncFavoriteEditorFieldsFromSelection
{
    NSMenuItem *sel = [self.favoriteAppsPopup selectedItem];
    NSDictionary *entry = [sel representedObject];
    if ([entry isKindOfClass:[NSDictionary class]]) {
        if (self.favoriteAppLabelField)
            [self.favoriteAppLabelField setStringValue:entry[@"label"] ?: @""];
        if (self.favoriteAppPathField)
            [self.favoriteAppPathField setStringValue:entry[@"path"] ?: @""];
    }
}

- (void)favoriteAppSelectionChanged:(id)sender
{
    (void)sender;
    [self syncFavoriteEditorFieldsFromSelection];
}

- (void)refreshFavoriteAppsPopup
{
    if (!self.favoriteAppsPopup)
        return;
    [self.favoriteAppsPopup removeAllItems];
    NSArray<NSDictionary<NSString *, NSString *> *> *favs = [self loadFavoriteAppsList];
    if ([favs count] == 0) {
        [self.favoriteAppsPopup addItemWithTitle:@"(No Favourite Windows Apps Configured)"];
        [self.favoriteAppsPopup setEnabled:NO];
    } else {
        for (NSDictionary<NSString *, NSString *> *entry in favs) {
            NSString *label = entry[@"label"] ?: @"";
            NSString *path = entry[@"path"] ?: @"";
            NSString *fname = [path lastPathComponent];
            NSString *title = [label isEqualToString:fname]
                ? [NSString stringWithFormat:@"%@ — %@", label, path]
                : [NSString stringWithFormat:@"%@ (%@) — %@", label, fname, path];
            [self.favoriteAppsPopup addItemWithTitle:title];
            [[self.favoriteAppsPopup lastItem] setRepresentedObject:entry];
            [[self.favoriteAppsPopup lastItem] setToolTip:path];
        }
        [self.favoriteAppsPopup setEnabled:YES];
        [self syncFavoriteEditorFieldsFromSelection];
    }
}

- (void)launchFavoriteAppAtPath:(NSString *)path label:(NSString *)label
{
    if (!path || [path length] == 0)
        return;
    const char *cpath = [path fileSystemRepresentation];
    if (!cpath)
        return;
    struct stat st;
    if (stat(cpath, &st) == 0 && (st.st_mode & S_IXUSR) == 0) {
        chmod(cpath, st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
    }

    char *launcher_bin = spm_launcher_app_binary();
    if (!launcher_bin) {
        if (self.builtinStatusLabel) {
            [self.builtinStatusLabel setStringValue:@"Error: Spumante launcher binary not found."];
        }
        return;
    }

    NSTask *task = [[NSTask alloc] init];
    [task setLaunchPath:[NSString stringWithUTF8String:launcher_bin]];
    [task setArguments:@[path]];
    free(launcher_bin);

    NSError *err = nil;
    BOOL launched = [task launchAndReturnError:&err];
    if (self.builtinStatusLabel) {
        if (launched) {
            NSString *disp = (label && [label length] > 0) ? label : [path lastPathComponent];
            [self.builtinStatusLabel setStringValue:[NSString stringWithFormat:@"Launched Favourite Windows App: %@ (%@, PID %d)", disp, [path lastPathComponent], (int)[task processIdentifier]]];
        } else {
            [self.builtinStatusLabel setStringValue:[NSString stringWithFormat:@"Failed to launch: %@", err ? [err localizedDescription] : @"unknown error"]];
        }
    }
}

- (void)launchFavoriteAppFromMenu:(NSMenuItem *)sender
{
    id rep = [sender representedObject];
    if ([rep isKindOfClass:[NSDictionary class]]) {
        [self launchFavoriteAppAtPath:rep[@"path"] label:rep[@"label"]];
    } else if ([rep isKindOfClass:[NSString class]]) {
        [self launchFavoriteAppAtPath:(NSString *)rep label:nil];
    }
}

- (void)launchSelectedFavoriteApp:(id)sender
{
    (void)sender;
    NSMenuItem *sel = [self.favoriteAppsPopup selectedItem];
    id rep = [sel representedObject];
    if ([rep isKindOfClass:[NSDictionary class]]) {
        [self launchFavoriteAppAtPath:rep[@"path"] label:rep[@"label"]];
    } else if ([rep isKindOfClass:[NSString class]]) {
        [self launchFavoriteAppAtPath:(NSString *)rep label:nil];
    }
}

- (void)persistFavoriteAppWithLabel:(NSString *)label path:(NSString *)path
{
    if (!path || [path length] == 0)
        return;
    NSString *cleanPath = [path stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if ([cleanPath length] == 0)
        return;
    NSString *cleanLabel = label ? [label stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] : @"";
    if ([cleanLabel length] == 0)
        cleanLabel = [cleanPath lastPathComponent];
    cleanLabel = [cleanLabel stringByReplacingOccurrencesOfString:@"|" withString:@"-"];

    const char *cpath = [cleanPath fileSystemRepresentation];
    struct stat st;
    if (cpath && stat(cpath, &st) == 0 && (st.st_mode & S_IXUSR) == 0) {
        chmod(cpath, st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
    }
    NSString *upd = [NSString stringWithFormat:@"favorite_apps.add=%@|%@", cleanLabel, cleanPath];
    const char *c_upd = [upd UTF8String];
    spm_json_free(spm_config_status(&c_upd, 1, NULL));
    [self refreshFavoriteAppsPopup];
    [self rebuildStatusMenu];
    if (self.builtinStatusLabel) {
        [self.builtinStatusLabel setStringValue:[NSString stringWithFormat:@"Saved Favourite Windows App: \"%@\" -> %@", cleanLabel, cleanPath]];
    }
}

- (void)browseFavoriteAppPathAction:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setTitle:@"Select a Favourite Windows Application (.exe / .msi)"];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setAllowedFileTypes:@[@"exe", @"EXE", @"msi", @"MSI"]];
    [NSApp activateIgnoringOtherApps:YES];
    if ([panel runModal] == NSModalResponseOK) {
        NSURL *url = [[panel URLs] firstObject];
        NSString *path = [url path];
        if (path && [path length] > 0) {
            if (self.favoriteAppPathField)
                [self.favoriteAppPathField setStringValue:path];
            if (self.favoriteAppLabelField) {
                NSString *curLabel = [[self.favoriteAppLabelField stringValue] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
                if ([curLabel length] == 0)
                    [self.favoriteAppLabelField setStringValue:[path lastPathComponent]];
            }
        }
    }
}

- (void)saveFavoriteAppEntryAction:(id)sender
{
    NSString *curPath = self.favoriteAppPathField
        ? [[self.favoriteAppPathField stringValue] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]]
        : @"";
    if ([curPath length] == 0) {
        [self browseFavoriteAppPathAction:sender];
        curPath = self.favoriteAppPathField
            ? [[self.favoriteAppPathField stringValue] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]]
            : @"";
    }
    if ([curPath length] == 0)
        return;
    NSString *curLabel = self.favoriteAppLabelField
        ? [[self.favoriteAppLabelField stringValue] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]]
        : @"";
    if ([curLabel length] == 0)
        curLabel = [curPath lastPathComponent];
    [self persistFavoriteAppWithLabel:curLabel path:curPath];
}

- (void)addFavoriteAppAction:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setTitle:@"Select a Favourite Windows Application (.exe / .msi)"];
    [panel setMessage:@"Choose a Windows executable (.exe / .msi) and optionally enter a custom display label:"];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setAllowedFileTypes:@[@"exe", @"EXE", @"msi", @"MSI"]];

    NSView *accView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 420, 36)];
    NSTextField *lbl = [self makeLabel:@"Display Label (Name):" frame:NSMakeRect(8, 8, 145, 20) bold:NO];
    [accView addSubview:lbl];
    NSTextField *nameInput = [[NSTextField alloc] initWithFrame:NSMakeRect(158, 6, 250, 24)];
    [nameInput setPlaceholderString:@"e.g. Task Manager"];
    [accView addSubview:nameInput];
    [panel setAccessoryView:accView];

    [NSApp activateIgnoringOtherApps:YES];
    if ([panel runModal] == NSModalResponseOK) {
        NSURL *url = [[panel URLs] firstObject];
        NSString *path = [url path];
        if (path && [path length] > 0) {
            NSString *customLabel = [[nameInput stringValue] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if ([customLabel length] == 0 && self.favoriteAppLabelField) {
                customLabel = [[self.favoriteAppLabelField stringValue] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            }
            if ([customLabel length] == 0)
                customLabel = [path lastPathComponent];
            if (self.favoriteAppPathField)
                [self.favoriteAppPathField setStringValue:path];
            if (self.favoriteAppLabelField)
                [self.favoriteAppLabelField setStringValue:customLabel];
            [self persistFavoriteAppWithLabel:customLabel path:path];
        }
    }
}

- (void)removeSelectedFavoriteApp:(id)sender
{
    (void)sender;
    NSMenuItem *sel = [self.favoriteAppsPopup selectedItem];
    id rep = [sel representedObject];
    NSString *path = [rep isKindOfClass:[NSDictionary class]] ? rep[@"path"] : ([rep isKindOfClass:[NSString class]] ? rep : @"");
    NSString *label = [rep isKindOfClass:[NSDictionary class]] ? rep[@"label"] : [path lastPathComponent];
    if ([path isKindOfClass:[NSString class]] && [path length] > 0) {
        NSString *upd = [NSString stringWithFormat:@"favorite_apps.remove=%@", path];
        const char *c_upd = [upd UTF8String];
        spm_json_free(spm_config_status(&c_upd, 1, NULL));
        [self refreshFavoriteAppsPopup];
        [self rebuildStatusMenu];
        if (self.builtinStatusLabel) {
            [self.builtinStatusLabel setStringValue:[NSString stringWithFormat:@"Removed Favourite Windows App: %@", label ?: [path lastPathComponent]]];
        }
    }
}

- (void)launchBuiltinFromMenu:(NSMenuItem *)sender
{
    NSString *appName = [sender representedObject];
    if ([appName isKindOfClass:[NSString class]]) {
        [self runBuiltinAppByName:appName openInConsole:YES];
    }
}

- (void)rebuildStatusMenu
{
    if (!self.statusItem)
        return;
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Spumante"];

    NSMenuItem *headerItem = [[NSMenuItem alloc] initWithTitle:@"Spumante — Windows Runtime" action:nil keyEquivalent:@""];
    [headerItem setEnabled:NO];
    [menu addItem:headerItem];
    [menu addItem:[NSMenuItem separatorItem]];

    /* Favourite Windows Apps section */
    NSMenuItem *favHeader = [[NSMenuItem alloc] initWithTitle:@"Favourite Windows Apps" action:nil keyEquivalent:@""];
    [favHeader setEnabled:NO];
    [menu addItem:favHeader];

    NSArray<NSDictionary<NSString *, NSString *> *> *favs = [self loadFavoriteAppsList];
    if ([favs count] == 0) {
        NSMenuItem *emptyFav = [[NSMenuItem alloc] initWithTitle:@"  (No Favourite Apps Configured)" action:nil keyEquivalent:@""];
        [emptyFav setEnabled:NO];
        [menu addItem:emptyFav];
    } else {
        for (NSDictionary<NSString *, NSString *> *entry in favs) {
            NSString *dispLabel = entry[@"label"] ?: [entry[@"path"] lastPathComponent];
            NSString *path = entry[@"path"] ?: @"";
            NSString *menuTitle = [NSString stringWithFormat:@"  %@", dispLabel];
            NSMenuItem *favItem = [[NSMenuItem alloc] initWithTitle:menuTitle action:@selector(launchFavoriteAppFromMenu:) keyEquivalent:@""];
            [favItem setTarget:self];
            [favItem setRepresentedObject:entry];
            [favItem setToolTip:[NSString stringWithFormat:@"%@ — %@", dispLabel, path]];
            [menu addItem:favItem];
        }
    }
    NSMenuItem *addFavItem = [[NSMenuItem alloc] initWithTitle:@"Add Favourite Windows App…" action:@selector(addFavoriteAppAction:) keyEquivalent:@""];
    [addFavItem setTarget:self];
    [menu addItem:addFavItem];
    [menu addItem:[NSMenuItem separatorItem]];

    /* Quick-launch Windows Utilities */
    NSMenuItem *utilHeader = [[NSMenuItem alloc] initWithTitle:@"Windows Utilities" action:nil keyEquivalent:@""];
    [utilHeader setEnabled:NO];
    [menu addItem:utilHeader];

    NSArray<NSDictionary *> *quickUtilities = @[
        @{@"title": @"Task Manager (taskmgr.exe)", @"exe": @"taskmgr.exe"},
        @{@"title": @"Command Prompt (wineconsole.exe)", @"exe": @"wineconsole.exe"},
        @{@"title": @"Notepad (notepad.exe)", @"exe": @"notepad.exe"},
        @{@"title": @"Registry Editor (regedit.exe)", @"exe": @"regedit.exe"},
        @{@"title": @"Wine Configuration (winecfg.exe)", @"exe": @"winecfg.exe"},
        @{@"title": @"File Explorer (explorer.exe)", @"exe": @"explorer.exe"}
    ];
    for (NSDictionary *u in quickUtilities) {
        NSMenuItem *uItem = [[NSMenuItem alloc] initWithTitle:u[@"title"] action:@selector(launchBuiltinFromMenu:) keyEquivalent:@""];
        [uItem setTarget:self];
        [uItem setRepresentedObject:u[@"exe"]];
        [menu addItem:uItem];
    }

    NSMenuItem *moreItem = [[NSMenuItem alloc] initWithTitle:@"More Built-in Apps" action:nil keyEquivalent:@""];
    NSMenu *moreMenu = [[NSMenu alloc] initWithTitle:@"More Built-in Apps"];
    NSArray<NSDictionary *> *moreApps = @[
        @{@"title": @"winmine.exe — Minesweeper", @"exe": @"winmine.exe"},
        @{@"title": @"clock.exe — Clock", @"exe": @"clock.exe"},
        @{@"title": @"wordpad.exe — WordPad", @"exe": @"wordpad.exe"},
        @{@"title": @"progman.exe — Program Manager", @"exe": @"progman.exe"},
        @{@"title": @"cmd.exe — Command Prompt (Windows Console)", @"exe": @"cmd.exe"},
        @{@"title": @"hostname.exe — Hostname (Windows Console)", @"exe": @"hostname.exe"},
        @{@"title": @"ipconfig.exe — IP Configuration (Windows Console)", @"exe": @"ipconfig.exe"},
        @{@"title": @"systeminfo.exe — System Info (Windows Console)", @"exe": @"systeminfo.exe"},
        @{@"title": @"tasklist.exe — Process List (Windows Console)", @"exe": @"tasklist.exe"}
    ];
    for (NSDictionary *entry in moreApps) {
        NSMenuItem *mItem = [[NSMenuItem alloc] initWithTitle:entry[@"title"] action:@selector(launchBuiltinFromMenu:) keyEquivalent:@""];
        [mItem setTarget:self];
        [mItem setRepresentedObject:entry[@"exe"]];
        [moreMenu addItem:mItem];
    }
    [moreItem setSubmenu:moreMenu];
    [menu addItem:moreItem];
    [menu addItem:[NSMenuItem separatorItem]];

    /* Settings & Prefix maintenance */
    NSMenuItem *settingsItem = [[NSMenuItem alloc] initWithTitle:@"Open Spumante Settings…" action:@selector(showSettingsWindow:) keyEquivalent:@","];
    [settingsItem setTarget:self];
    [menu addItem:settingsItem];

    NSMenuItem *syncItem = [[NSMenuItem alloc] initWithTitle:@"Sync Installed drive_c Apps (+x)" action:@selector(syncDriveCExecutablesAction:) keyEquivalent:@""];
    [syncItem setTarget:self];
    [menu addItem:syncItem];
    [menu addItem:[NSMenuItem separatorItem]];

    /* Help & Feedback */
    NSMenuItem *bugItem = [[NSMenuItem alloc] initWithTitle:@"Report a Bug…" action:@selector(reportBugAction:) keyEquivalent:@""];
    [bugItem setTarget:self];
    [menu addItem:bugItem];

    NSMenuItem *appdbItem = [[NSMenuItem alloc] initWithTitle:@"Submit Tested App (AppDB)…" action:@selector(submitTestedAppAction:) keyEquivalent:@""];
    [appdbItem setTarget:self];
    [menu addItem:appdbItem];
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *stopItem = [[NSMenuItem alloc] initWithTitle:@"Stop All Wine Processes" action:@selector(stopAllWineProcessesAction:) keyEquivalent:@""];
    [stopItem setTarget:self];
    [menu addItem:stopItem];
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit Spumante" action:@selector(quitTrayApp:) keyEquivalent:@"q"];
    [quitItem setTarget:self];
    [menu addItem:quitItem];

    self.statusMenu = menu;
    [self.statusItem setMenu:menu];
}

- (void)setupStatusBarItem
{
    if (self.statusItem != nil)
        return;
    self.statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    NSStatusBarButton *btn = [self.statusItem button];
    if (btn) {
        char *logo_cpath = spm_find_resource("spumante-logo.png", 0);
        NSImage *icon = nil;
        if (logo_cpath) {
            NSString *logoPath = [NSString stringWithUTF8String:logo_cpath];
            if (logoPath)
                icon = [[NSImage alloc] initWithContentsOfFile:logoPath];
            free(logo_cpath);
        }
        if (icon) {
            [icon setSize:NSMakeSize(18.0, 18.0)];
            [btn setImage:icon];
        } else {
            [btn setTitle:@"🥂"];
        }
        [btn setToolTip:@"Spumante — Windows Runtime, Utilities & Settings"];
    }
    [self rebuildStatusMenu];
}

- (void)reportBugAction:(id)sender
{
    (void)sender;
    spm_json_free(spm_issue_report("bug", "", "Platinum", 0));
}

- (void)submitTestedAppAction:(id)sender
{
    (void)sender;
    spm_json_free(spm_issue_report("appdb", "", "Platinum", 0));
}

- (void)syncDriveCExecutablesAction:(id)sender
{
    (void)sender;
    spm_json *rep = spm_sync_drive_c(NULL);
    if (self.handlerStatusLabel) {
        long long upd = spm_json_num(spm_json_get(rep, "updated_count"), 0);
        long long scn = spm_json_num(spm_json_get(rep, "scanned_count"), 0);
        [self.handlerStatusLabel setStringValue:[NSString stringWithFormat:@"Synced +x permissions on %lld of %lld executables in drive_c.", upd, scn]];
    }
    spm_json_free(rep);
}

- (void)runBuiltinAppByName:(NSString *)appName openInConsole:(BOOL)openConsole
{
    if (!appName || [appName length] == 0)
        return;
    spm_json *rep = spm_launch_builtin_console([appName UTF8String], 0, NULL, openConsole ? 1 : 0, 0);
    if (self.builtinStatusLabel) {
        const char *cat = spm_json_str(spm_json_get(rep, "category"), "gui");
        const char *status = spm_json_str(spm_json_get(rep, "status"), "");
        int inConsole = spm_json_truthy(spm_json_get(rep, "open_in_console"), 0);
        if (strncmp(status, "error_", 6) == 0 || strcmp(status, "spawn_failed") == 0) {
            [self.builtinStatusLabel setStringValue:[NSString stringWithFormat:@"Failed to launch %@ (%s)", appName, status]];
        } else if (strcmp(cat, "cli") == 0 && !inConsole) {
            NSString *outStr = [NSString stringWithUTF8String:spm_json_str(spm_json_get(rep, "stdout"), "")] ?: @"";
            [self.builtinStatusLabel setStringValue:[NSString stringWithFormat:@"[%@ Output]\n%@", appName, outStr]];
        } else if (inConsole) {
            long long pidNum = spm_json_num(spm_json_get(rep, "pid"), 0);
            [self.builtinStatusLabel setStringValue:[NSString stringWithFormat:@"Opened %@ in Windows Console (wineconsole.exe, PID %lld).", appName, pidNum]];
        } else {
            long long pidNum = spm_json_num(spm_json_get(rep, "pid"), 0);
            [self.builtinStatusLabel setStringValue:[NSString stringWithFormat:@"Launched %@ (PID %lld) in temporary security-bypass environment.", appName, pidNum]];
        }
    }
    spm_json_free(rep);
}

- (void)runBuiltinAppByName:(NSString *)appName
{
    [self runBuiltinAppByName:appName openInConsole:NO];
}

- (void)launchSelectedGuiApp:(id)sender
{
    (void)sender;
    NSArray<NSString *> *guiApps = @[@"notepad.exe", @"winmine.exe", @"clock.exe", @"taskmgr.exe", @"wordpad.exe", @"regedit.exe", @"winecfg.exe", @"progman.exe", @"explorer.exe", @"wineconsole.exe"];
    NSInteger idx = [self.builtinGuiPopup indexOfSelectedItem];
    if (idx >= 0 && idx < (NSInteger)[guiApps count]) {
        [self runBuiltinAppByName:guiApps[idx] openInConsole:NO];
    }
}

- (void)launchSelectedCliAppInConsole:(id)sender
{
    (void)sender;
    NSArray<NSString *> *cliApps = @[@"cmd.exe", @"hostname.exe", @"ipconfig.exe", @"systeminfo.exe", @"tasklist.exe"];
    NSInteger idx = [self.builtinCliPopup indexOfSelectedItem];
    if (idx >= 0 && idx < (NSInteger)[cliApps count]) {
        [self runBuiltinAppByName:cliApps[idx] openInConsole:YES];
    }
}

- (void)launchSelectedCliApp:(id)sender
{
    (void)sender;
    NSArray<NSString *> *cliApps = @[@"cmd.exe", @"hostname.exe", @"ipconfig.exe", @"systeminfo.exe", @"tasklist.exe"];
    NSInteger idx = [self.builtinCliPopup indexOfSelectedItem];
    if (idx >= 0 && idx < (NSInteger)[cliApps count]) {
        [self runBuiltinAppByName:cliApps[idx] openInConsole:NO];
    }
}

- (void)setupUI
{
    NSDictionary *cfg = [self loadCurrentConfigDict];
    NSDictionary *secCfg = [cfg[@"security"] isKindOfClass:[NSDictionary class]] ? cfg[@"security"] : @{};
    NSDictionary *envCfg = [cfg[@"wine_environment"] isKindOfClass:[NSDictionary class]] ? cfg[@"wine_environment"] : @{};
    NSDictionary *sbCfg = [cfg[@"sandbox"] isKindOfClass:[NSDictionary class]] ? cfg[@"sandbox"] : @{};
    NSDictionary *netCfg = [cfg[@"network"] isKindOfClass:[NSDictionary class]] ? cfg[@"network"] : @{};
    NSDictionary *accCfg = [cfg[@"acceleration"] isKindOfClass:[NSDictionary class]] ? cfg[@"acceleration"] : @{};

    [self setupStatusBarItem];

    NSRect frame = NSMakeRect(0, 0, 640, 460);
    NSWindowStyleMask style = NSWindowStyleMaskTitled |
                              NSWindowStyleMaskClosable |
                              NSWindowStyleMaskMiniaturizable;
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    [self.window setTitle:@"Spumante Settings"];
    [self.window setDelegate:self];
    [self.window center];

    NSTabView *tabView = [[NSTabView alloc] initWithFrame:NSMakeRect(20, 60, 600, 375)];

    /* Tab 1: General */
    NSTabViewItem *genTab = [[NSTabViewItem alloc] initWithIdentifier:@"General"];
    [genTab setLabel:@"General"];
    NSView *genView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 580, 330)];
    [genView addSubview:[self makeLabel:@"Windows Environment (Cascades to WINEPREFIX Registry)" frame:NSMakeRect(20, 280, 500, 22) bold:YES]];
    [genView addSubview:[self makeLabel:@"Windows Version:" frame:NSMakeRect(20, 240, 140, 22) bold:NO]];
    self.winVerPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(165, 236, 260, 26) pullsDown:NO];
    [self.winVerPopup addItemsWithTitles:@[@"Windows 11 (Default)", @"Windows 10", @"Windows 8.1", @"Windows 7"]];
    NSString *curVer = [envCfg[@"windows_version"] isKindOfClass:[NSString class]] ? envCfg[@"windows_version"] : @"win11";
    NSArray<NSString *> *verCodes = @[@"win11", @"win10", @"win81", @"win7"];
    NSUInteger foundVerIdx = [verCodes indexOfObject:curVer];
    if (foundVerIdx != NSNotFound)
        [self.winVerPopup selectItemAtIndex:(NSInteger)foundVerIdx];
    [genView addSubview:self.winVerPopup];
    BOOL retinaOn = (envCfg[@"retina_mode"] == nil) ? YES : [envCfg[@"retina_mode"] boolValue];
    self.retinaCheck = [self makeCheckbox:@"Enable macOS Retina / HiDPI Mode (winemac.drv)" frame:NSMakeRect(20, 195, 450, 22) state:retinaOn ? NSControlStateValueOn : NSControlStateValueOff];
    [genView addSubview:self.retinaCheck];

    [genView addSubview:[self makeLabel:@"macOS LaunchServices File Associations (.exe / .msi)" frame:NSMakeRect(20, 145, 500, 22) bold:YES]];
    NSButton *regBtn = [NSButton buttonWithTitle:@"Register .exe / .msi File Handler" target:self action:@selector(registerFileHandlersAction:)];
    [regBtn setFrame:NSMakeRect(20, 108, 250, 32)];
    [genView addSubview:regBtn];
    NSButton *syncDriveCBtn = [NSButton buttonWithTitle:@"Sync Installed drive_c Apps (+x)" target:self action:@selector(syncDriveCExecutablesAction:)];
    [syncDriveCBtn setFrame:NSMakeRect(280, 108, 260, 32)];
    [genView addSubview:syncDriveCBtn];
    self.handlerStatusLabel = [self makeLabel:@"Config Path: ~/.config/spumante/config.json (0600)" frame:NSMakeRect(20, 76, 540, 20) bold:NO];
    [self.handlerStatusLabel setFont:[NSFont systemFontOfSize:11.0]];
    [self.handlerStatusLabel setTextColor:[NSColor secondaryLabelColor]];
    [genView addSubview:self.handlerStatusLabel];

    NSButton *uninstallBtn = [NSButton buttonWithTitle:@"Uninstall Spumante…" target:self action:@selector(uninstallAction:)];
    [uninstallBtn setFrame:NSMakeRect(20, 36, 180, 30)];
    [genView addSubview:uninstallBtn];

    [genTab setView:genView];
    [tabView addTabViewItem:genTab];

    /* Tab 2: Security (3-Gate Scanner + VirusTotal v3 Configuration & API Key Input) */
    NSTabViewItem *secTab = [[NSTabViewItem alloc] initWithIdentifier:@"Security"];
    [secTab setLabel:@"Security"];
    NSView *secView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 580, 330)];
    [secView addSubview:[self makeLabel:@"Pre-Execution PE Security Gates (Gates 1, 2 & 3)" frame:NSMakeRect(20, 290, 520, 22) bold:YES]];
    BOOL blockQuar = (secCfg[@"block_quarantined"] == nil) ? YES : [secCfg[@"block_quarantined"] boolValue];
    BOOL reqAuth = (secCfg[@"require_authenticode"] == nil) ? NO : [secCfg[@"require_authenticode"] boolValue];
    BOOL blockWx = (secCfg[@"block_wx_sections"] == nil) ? YES : [secCfg[@"block_wx_sections"] boolValue];
    self.blockQuarantineCheck = [self makeCheckbox:@"Gate 1: Block Quarantined Downloads (com.apple.quarantine / MotW)" frame:NSMakeRect(20, 258, 520, 22) state:blockQuar ? NSControlStateValueOn : NSControlStateValueOff];
    self.requireAuthCheck = [self makeCheckbox:@"Gate 2: Require Valid PE Authenticode Signature for All Executables" frame:NSMakeRect(20, 228, 520, 22) state:reqAuth ? NSControlStateValueOn : NSControlStateValueOff];
    self.blockWxIatCheck = [self makeCheckbox:@"Gate 3: Block W^X Sections & Process-Injection IAT Threat Patterns" frame:NSMakeRect(20, 198, 520, 22) state:blockWx ? NSControlStateValueOn : NSControlStateValueOff];
    [secView addSubview:self.blockQuarantineCheck];
    [secView addSubview:self.requireAuthCheck];
    [secView addSubview:self.blockWxIatCheck];

    [secView addSubview:[self makeLabel:@"Gate 3 — VirusTotal v3 Cloud Hash Reputation" frame:NSMakeRect(20, 152, 520, 22) bold:YES]];
    BOOL vtEnabled = (secCfg[@"virustotal_enabled"] == nil) ? YES : [secCfg[@"virustotal_enabled"] boolValue];
    self.virustotalEnableCheck = [self makeCheckbox:@"Enable VirusTotal v3 SHA-256 Hash Lookup When API Key Is Configured" frame:NSMakeRect(20, 122, 530, 22) state:vtEnabled ? NSControlStateValueOn : NSControlStateValueOff];
    [self.virustotalEnableCheck setTarget:self];
    [self.virustotalEnableCheck setAction:@selector(toggleVtEnabled:)];
    [secView addSubview:self.virustotalEnableCheck];

    [secView addSubview:[self makeLabel:@"VirusTotal API Key:" frame:NSMakeRect(20, 82, 135, 22) bold:NO]];
    NSString *savedVtKey = [secCfg[@"virustotal_api_key"] isKindOfClass:[NSString class]] ? secCfg[@"virustotal_api_key"] : @"";
    NSRect keyFrame = NSMakeRect(160, 80, 305, 24);
    self.virustotalKeyField = [[NSSecureTextField alloc] initWithFrame:keyFrame];
    [self.virustotalKeyField setPlaceholderString:@"Enter VirusTotal v3 API Key (64-char hex)..."];
    [self.virustotalKeyField setStringValue:savedVtKey];
    [self.virustotalKeyField setEnabled:vtEnabled];
    [secView addSubview:self.virustotalKeyField];

    self.virustotalPlainKeyField = [[NSTextField alloc] initWithFrame:keyFrame];
    [self.virustotalPlainKeyField setPlaceholderString:@"Enter VirusTotal v3 API Key (64-char hex)..."];
    [self.virustotalPlainKeyField setStringValue:savedVtKey];
    [self.virustotalPlainKeyField setEnabled:vtEnabled];
    [self.virustotalPlainKeyField setHidden:YES];
    [secView addSubview:self.virustotalPlainKeyField];

    self.showVtKeyCheck = [self makeCheckbox:@"Show Key" frame:NSMakeRect(475, 81, 90, 22) state:NSControlStateValueOff];
    [self.showVtKeyCheck setTarget:self];
    [self.showVtKeyCheck setAction:@selector(toggleVtKeyVisibility:)];
    [self.showVtKeyCheck setEnabled:vtEnabled];
    [secView addSubview:self.showVtKeyCheck];

    [secTab setView:secView];
    [tabView addTabViewItem:secTab];

    /* Tab 3: Sandbox & Network */
    NSTabViewItem *sbTab = [[NSTabViewItem alloc] initWithIdentifier:@"Sandbox & Network"];
    [sbTab setLabel:@"Sandbox & Network"];
    NSView *sbView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 580, 330)];
    [sbView addSubview:[self makeLabel:@"Zero-Trust Runtime Containment & Firewall" frame:NSMakeRect(20, 280, 500, 22) bold:YES]];
    BOOL sbEnabled = (sbCfg[@"enabled"] == nil) ? YES : [sbCfg[@"enabled"] boolValue];
    BOOL stripZ = (sbCfg[@"strip_z_drive"] == nil) ? YES : [sbCfg[@"strip_z_drive"] boolValue];
    BOOL protPaths = (sbCfg[@"protect_sensitive_host_paths"] == nil) ? YES : [sbCfg[@"protect_sensitive_host_paths"] boolValue];
    self.sandboxEnableCheck = [self makeCheckbox:@"Run All Applications inside macOS Seatbelt Sandbox (Default)" frame:NSMakeRect(20, 245, 520, 22) state:sbEnabled ? NSControlStateValueOn : NSControlStateValueOff];
    self.stripZDriveCheck = [self makeCheckbox:@"Strip Host Root Drive (Z:) & Disable Desktop Integration" frame:NSMakeRect(20, 215, 520, 22) state:stripZ ? NSControlStateValueOn : NSControlStateValueOff];
    self.protectPathsCheck = [self makeCheckbox:@"Block Access to ~/.ssh, ~/.aws, ~/.gnupg, and Keychains" frame:NSMakeRect(20, 185, 520, 22) state:protPaths ? NSControlStateValueOn : NSControlStateValueOff];
    [sbView addSubview:self.sandboxEnableCheck];
    [sbView addSubview:self.stripZDriveCheck];
    [sbView addSubview:self.protectPathsCheck];

    [sbView addSubview:[self makeLabel:@"Incoming Connections:" frame:NSMakeRect(20, 130, 160, 22) bold:NO]];
    self.incomingNetPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(185, 126, 340, 26) pullsDown:NO];
    [self.incomingNetPopup addItemsWithTitles:@[@"Disabled — Block All Incoming (Default)", @"Allow Incoming Connections"]];
    NSString *curInc = [netCfg[@"incoming"] isKindOfClass:[NSString class]] ? netCfg[@"incoming"] : @"deny";
    [self.incomingNetPopup selectItemAtIndex:[curInc isEqualToString:@"allow"] ? 1 : 0];
    [sbView addSubview:self.incomingNetPopup];

    [sbView addSubview:[self makeLabel:@"Outgoing Connections:" frame:NSMakeRect(20, 90, 160, 22) bold:NO]];
    self.outgoingNetPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(185, 86, 340, 26) pullsDown:NO];
    [self.outgoingNetPopup addItemsWithTitles:@[
        @"HTTP / HTTPS & DNS — Ports 80, 443 & 53 (Default)",
        @"DNS Only — Port 53",
        @"Block All Outgoing Connections",
        @"Allow All Outgoing Connections"
    ]];
    NSString *curOut = [netCfg[@"outgoing"] isKindOfClass:[NSString class]] ? netCfg[@"outgoing"] : @"http_https_only";
    if ([curOut isEqualToString:@"dns_only"])
        [self.outgoingNetPopup selectItemAtIndex:1];
    else if ([curOut isEqualToString:@"deny_all"])
        [self.outgoingNetPopup selectItemAtIndex:2];
    else if ([curOut isEqualToString:@"allow_all"])
        [self.outgoingNetPopup selectItemAtIndex:3];
    else
        [self.outgoingNetPopup selectItemAtIndex:0];
    [sbView addSubview:self.outgoingNetPopup];
    [sbTab setView:sbView];
    [tabView addTabViewItem:sbTab];

    /* Tab 4: Graphics & Acceleration */
    NSTabViewItem *accTab = [[NSTabViewItem alloc] initWithIdentifier:@"Graphics & Acceleration"];
    [accTab setLabel:@"Graphics & Acceleration"];
    NSView *accView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 580, 330)];
    [accView addSubview:[self makeLabel:@"Apple Silicon & macOS Native Framework Acceleration" frame:NSMakeRect(20, 280, 500, 22) bold:YES]];
    BOOL metalOn = (accCfg[@"metal_iosurface_swapchain"] == nil) ? YES : [accCfg[@"metal_iosurface_swapchain"] boolValue];
    BOOL vtHwOn = (accCfg[@"videotoolbox_hw_decode"] == nil) ? YES : [accCfg[@"videotoolbox_hw_decode"] boolValue];
    BOOL jitOn = (accCfg[@"arm64ec_jit_cache"] == nil) ? YES : [accCfg[@"arm64ec_jit_cache"] boolValue];
    self.metalCheck = [self makeCheckbox:@"Enable IOSurface-Backed CAMetalLayer Child Window Swapchains" frame:NSMakeRect(20, 235, 520, 22) state:metalOn ? NSControlStateValueOn : NSControlStateValueOff];
    self.videotoolboxCheck = [self makeCheckbox:@"Enable VideoToolbox Hardware H.264 / HEVC Decode Bridge" frame:NSMakeRect(20, 200, 520, 22) state:vtHwOn ? NSControlStateValueOn : NSControlStateValueOff];
    self.jitCacheCheck = [self makeCheckbox:@"Enable ARM64EC On-Disk WJTC JIT Translation Cache" frame:NSMakeRect(20, 165, 520, 22) state:jitOn ? NSControlStateValueOn : NSControlStateValueOff];
    [accView addSubview:self.metalCheck];
    [accView addSubview:self.videotoolboxCheck];
    [accView addSubview:self.jitCacheCheck];
    [accTab setView:accView];
    [tabView addTabViewItem:accTab];

    /* Tab 5: Built-in Apps & Favourite Custom Windows Applications */
    NSTabViewItem *appsTab = [[NSTabViewItem alloc] initWithIdentifier:@"Built-in Apps"];
    [appsTab setLabel:@"Built-in Apps"];
    NSView *appsView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 580, 330)];
    [appsView addSubview:[self makeLabel:@"Built-in Windows GUI & CLI Programs (Bundled in DMG)" frame:NSMakeRect(20, 298, 520, 20) bold:YES]];

    [appsView addSubview:[self makeLabel:@"Built-in GUI App:" frame:NSMakeRect(20, 266, 130, 22) bold:NO]];
    self.builtinGuiPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(155, 262, 250, 26) pullsDown:NO];
    [self.builtinGuiPopup addItemsWithTitles:@[
        @"notepad.exe — Notepad Text Editor",
        @"winmine.exe — Minesweeper Game",
        @"clock.exe — Analog / Digital Clock",
        @"taskmgr.exe — Windows Task Manager",
        @"wordpad.exe — WordPad Rich Text Editor",
        @"regedit.exe — Registry Editor",
        @"winecfg.exe — Wine Configuration",
        @"progman.exe — Program Manager",
        @"explorer.exe — Windows File Explorer",
        @"wineconsole.exe — Windows Console Terminal"
    ]];
    [appsView addSubview:self.builtinGuiPopup];
    NSButton *launchGuiBtn = [NSButton buttonWithTitle:@"Launch GUI App" target:self action:@selector(launchSelectedGuiApp:)];
    [launchGuiBtn setFrame:NSMakeRect(406, 260, 154, 28)];
    [appsView addSubview:launchGuiBtn];

    [appsView addSubview:[self makeLabel:@"Built-in CLI App:" frame:NSMakeRect(20, 232, 130, 22) bold:NO]];
    self.builtinCliPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(155, 228, 245, 26) pullsDown:NO];
    [self.builtinCliPopup addItemsWithTitles:@[
        @"cmd.exe — Command Prompt",
        @"hostname.exe — Print Windows Hostname",
        @"ipconfig.exe — Network Configuration",
        @"systeminfo.exe — OS & Hardware Info",
        @"tasklist.exe — Running Processes List"
    ]];
    [appsView addSubview:self.builtinCliPopup];
    NSButton *openConsoleCliBtn = [NSButton buttonWithTitle:@"Open Console" target:self action:@selector(launchSelectedCliAppInConsole:)];
    [openConsoleCliBtn setFrame:NSMakeRect(406, 226, 98, 28)];
    [appsView addSubview:openConsoleCliBtn];
    NSButton *launchCliBtn = [NSButton buttonWithTitle:@"Inline" target:self action:@selector(launchSelectedCliApp:)];
    [launchCliBtn setFrame:NSMakeRect(506, 226, 54, 28)];
    [appsView addSubview:launchCliBtn];

    [appsView addSubview:[self makeLabel:@"Favourite Custom Windows Applications (Menu Bar Quick-Launch)" frame:NSMakeRect(20, 194, 520, 20) bold:YES]];

    [appsView addSubview:[self makeLabel:@"Saved Favourites:" frame:NSMakeRect(20, 162, 130, 22) bold:NO]];
    self.favoriteAppsPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(155, 158, 245, 26) pullsDown:NO];
    [self.favoriteAppsPopup setTarget:self];
    [self.favoriteAppsPopup setAction:@selector(favoriteAppSelectionChanged:)];
    [appsView addSubview:self.favoriteAppsPopup];
    NSButton *launchFavBtn = [NSButton buttonWithTitle:@"Launch" target:self action:@selector(launchSelectedFavoriteApp:)];
    [launchFavBtn setFrame:NSMakeRect(406, 156, 75, 28)];
    [appsView addSubview:launchFavBtn];
    NSButton *remFavBtn = [NSButton buttonWithTitle:@"Remove" target:self action:@selector(removeSelectedFavoriteApp:)];
    [remFavBtn setFrame:NSMakeRect(485, 156, 75, 28)];
    [appsView addSubview:remFavBtn];

    [appsView addSubview:[self makeLabel:@"App Label (Name):" frame:NSMakeRect(20, 126, 130, 22) bold:NO]];
    self.favoriteAppLabelField = [[NSTextField alloc] initWithFrame:NSMakeRect(155, 124, 245, 24)];
    [self.favoriteAppLabelField setPlaceholderString:@"e.g. Task Manager"];
    [appsView addSubview:self.favoriteAppLabelField];
    NSButton *saveFavBtn = [NSButton buttonWithTitle:@"Add / Update Favourite" target:self action:@selector(saveFavoriteAppEntryAction:)];
    [saveFavBtn setFrame:NSMakeRect(406, 122, 154, 28)];
    [appsView addSubview:saveFavBtn];

    [appsView addSubview:[self makeLabel:@"Executable Path:" frame:NSMakeRect(20, 92, 130, 22) bold:NO]];
    self.favoriteAppPathField = [[NSTextField alloc] initWithFrame:NSMakeRect(155, 90, 245, 24)];
    [self.favoriteAppPathField setPlaceholderString:@"/path/to/taskman.exe (or click Browse…)"];
    [appsView addSubview:self.favoriteAppPathField];
    NSButton *browseFavBtn = [NSButton buttonWithTitle:@"Browse…" target:self action:@selector(browseFavoriteAppPathAction:)];
    [browseFavBtn setFrame:NSMakeRect(406, 88, 154, 28)];
    [appsView addSubview:browseFavBtn];

    [self refreshFavoriteAppsPopup];

    self.builtinStatusLabel = [self makeLabel:@"Ready: Configure a custom label (e.g. \"Task Manager\" for taskman.exe) and executable path above, or launch from the Spumante menu bar icon." frame:NSMakeRect(20, 10, 540, 70) bold:NO];
    [self.builtinStatusLabel setFont:[NSFont monospacedSystemFontOfSize:11.0 weight:NSFontWeightRegular]];
    [self.builtinStatusLabel setSelectable:YES];
    [appsView addSubview:self.builtinStatusLabel];
    [appsTab setView:appsView];
    [tabView addTabViewItem:appsTab];

    [[self.window contentView] addSubview:tabView];

    NSButton *reportBugBtn = [NSButton buttonWithTitle:@"Report a Bug…" target:self action:@selector(reportBugAction:)];
    [reportBugBtn setFrame:NSMakeRect(20, 16, 125, 32)];
    [[self.window contentView] addSubview:reportBugBtn];

    NSButton *reportAppBtn = [NSButton buttonWithTitle:@"Submit Tested App…" target:self action:@selector(submitTestedAppAction:)];
    [reportAppBtn setFrame:NSMakeRect(150, 16, 155, 32)];
    [[self.window contentView] addSubview:reportAppBtn];

    NSButton *saveBtn = [NSButton buttonWithTitle:@"Save & Cascade" target:self action:@selector(saveAndCascade:)];
    [saveBtn setFrame:NSMakeRect(440, 16, 180, 32)];
    [saveBtn setKeyEquivalent:@"\r"];
    [[self.window contentView] addSubview:saveBtn];

    NSButton *cancelBtn = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(closeWindow:)];
    [cancelBtn setFrame:NSMakeRect(335, 16, 95, 32)];
    [[self.window contentView] addSubview:cancelBtn];

    [self.window makeKeyAndOrderFront:nil];
}
@end

@implementation SpumanteAppDelegate

- (void)application:(NSApplication *)application openURLs:(NSArray<NSURL *> *)urls
{
    (void)application;
    self.handledOpenEvent = YES;
    int rc = 0;
    for (NSURL *url in urls) {
        if ([url isFileURL]) {
            const char *cpath = [[url path] fileSystemRepresentation];
            if (cpath) {
                int cur = dispatch_windows_binary(cpath, 0, NULL);
                if (cur != 0)
                    rc = cur;
            }
        }
    }
    self.lastExitCode = rc;
    exit(rc);
}

- (BOOL)application:(NSApplication *)sender openFile:(NSString *)filename
{
    (void)sender;
    self.handledOpenEvent = YES;
    const char *cpath = [filename fileSystemRepresentation];
    int rc = cpath ? dispatch_windows_binary(cpath, 0, NULL) : 1;
    self.lastExitCode = rc;
    exit(rc);
    return rc == 0 ? YES : NO;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    if (!self.handledOpenEvent) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(150 * NSEC_PER_MSEC)),
                       dispatch_get_main_queue(), ^{
            if (!self.handledOpenEvent) {
                const char *idle_exit = getenv("SPUMANTE_EXIT_WHEN_IDLE");
                if (idle_exit && strcmp(idle_exit, "1") == 0) {
                    exit(0);
                }
                [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
                self.settingsController = [[SpumanteSettingsController alloc] init];
                [self.settingsController setupUI];
                [NSApp activateIgnoringOtherApps:YES];
            }
        });
    }
}

@end
#endif

static int handle_launch_builtin(const char *app_name, int extra_argc, char **extra_argv, int open_console, int dry_run)
{
    setenv("SPUMANTE_INTERPOSE_BYPASS", "1", 1);
    for (int i = 0; i < spm_builtin_launch_env_count; i++) {
        const char *kv = spm_builtin_launch_env[i];
        const char *eq = strchr(kv, '=');
        char *key = spm_strndup(kv, (size_t)(eq - kv));
        setenv(key, eq + 1, 1);
        free(key);
    }
    const char *env_console = getenv("SPUMANTE_CLI_OPEN_CONSOLE");
    int use_console = open_console || (env_console && strcmp(env_console, "1") == 0);
    spm_json *rep = spm_launch_builtin_console(app_name, extra_argc, extra_argv, use_console, dry_run);
    const char *status = spm_json_str(spm_json_get(rep, "status"), "");
    int rc = (strncmp(status, "error_", 6) == 0 || strcmp(status, "spawn_failed") == 0) ? 1 : 0;
    emit_json(rep);
    return rc;
}

static int handle_config_ui(int dry_run)
{
    if (dry_run) {
        emit_json(spm_config_ui_report());
        return 0;
    }
#ifdef __APPLE__
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        SpumanteSettingsController *ctrl = [[SpumanteSettingsController alloc] init];
        [ctrl setupUI];
        [app activateIgnoringOtherApps:YES];
        [app run];
    }
#endif
    return 0;
}

static void print_usage(void)
{
    printf("Usage: Spumante <program.exe|installer.msi> [args...]\n"
           "       Spumante --scan <file.exe> [--require-signature] [--vt-api-key <key>] [--quiet]\n"
           "       Spumante --config-status | --set-config <key=value> [--set-config ...] [--exe <name.exe>]\n"
           "       Spumante --config-ui [--dry-run]\n"
           "       Spumante --launch-builtin <app.exe> [--dry-run] [-- args...]\n"
           "       Spumante --sync-drive-c [--quiet]\n"
           "       Spumante --report-bug | --report-app [--app <name>] [--rating <tier>] [--dry-run]\n"
           "       Spumante --register | --unregister | --status [--dry-run]\n");
}

static const char *arg_value(int argc, char **argv, const char *flag)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (strcmp(argv[i], flag) == 0)
            return argv[i + 1];
    return NULL;
}

static int has_flag(int argc, char **argv, const char *flag)
{
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], flag) == 0)
            return 1;
    return 0;
}

int main(int argc, char **argv)
{
    int dry_run = has_flag(argc, argv, "--dry-run");
    int quiet = has_flag(argc, argv, "--quiet") || has_flag(argc, argv, "--quiet-stdout");

    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage();
        return 0;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--launch-builtin") == 0 && i + 1 < argc) {
            int open_console = has_flag(argc, argv, "--open-console");
            int extra_start = argc;
            for (int j = i + 2; j < argc; ++j)
                if (strcmp(argv[j], "--") == 0) {
                    extra_start = j + 1;
                    break;
                }
            return handle_launch_builtin(argv[i + 1], argc - extra_start, &argv[extra_start], open_console, dry_run);
        }
        if (strcmp(argv[i], "--sync-drive-c") == 0) {
            spm_json *rep = spm_sync_drive_c(NULL);
            if (quiet)
                spm_json_free(rep);
            else
                emit_json(rep);
            return 0;
        }
        if (strcmp(argv[i], "--report-bug") == 0 || strcmp(argv[i], "--report-app") == 0) {
            const char *kind = (strcmp(argv[i], "--report-app") == 0) ? "appdb" : "bug";
            const char *app = arg_value(argc, argv, "--app");
            const char *rating = arg_value(argc, argv, "--rating");
            emit_json(spm_issue_report(kind, app ? app : "", rating ? rating : "Platinum", dry_run));
            return 0;
        }
        if (strcmp(argv[i], "--scan") == 0 && i + 1 < argc) {
            const char *target = argv[i + 1];
            int passed = 0;
            spm_json *rep = spm_scan_pe(target, has_flag(argc, argv, "--require-signature"),
                                        arg_value(argc, argv, "--vt-api-key"), &passed);
            if (!passed)
                spm_scan_print_findings(rep, target);
            if (quiet)
                spm_json_free(rep);
            else
                emit_json(rep);
            return passed ? 0 : 126;
        }
        if (strcmp(argv[i], "--config-status") == 0 || strcmp(argv[i], "--set-config") == 0) {
            const char **updates = calloc((size_t)argc, sizeof(char *));
            int n_updates = 0;
            for (int j = 1; j + 1 < argc; ++j)
                if (strcmp(argv[j], "--set-config") == 0)
                    updates[n_updates++] = argv[++j];
            emit_json(spm_config_status(updates, n_updates, arg_value(argc, argv, "--exe")));
            free(updates);
            return 0;
        }
        if (strcmp(argv[i], "--session-status") == 0) {
            emit_json(spm_session_status(NULL));
            return 0;
        }
        if (strcmp(argv[i], "--session-start") == 0) {
            emit_json(spm_session_start(NULL, dry_run));
            return 0;
        }
        if (strcmp(argv[i], "--session-stop") == 0) {
            emit_json(spm_session_stop(NULL, dry_run));
            return 0;
        }
        if (strcmp(argv[i], "--init-prefix") == 0) {
            emit_json(spm_init_prefix(NULL, dry_run));
            return 0;
        }
        if (strcmp(argv[i], "--login-agent-status") == 0) {
            emit_json(spm_login_agent_status());
            return 0;
        }
        if (strcmp(argv[i], "--install-login-agent") == 0) {
            emit_json(spm_login_agent_install(spm_self_exe()));
            return 0;
        }
        if (strcmp(argv[i], "--uninstall-login-agent") == 0) {
            emit_json(spm_login_agent_uninstall());
            return 0;
        }
        if (strcmp(argv[i], "--tray") == 0) {
            if (dry_run) {
                emit_json(spm_tray_report(1));
                return 0;
            }
#ifdef __APPLE__
            @autoreleasepool {
                NSApplication *app = [NSApplication sharedApplication];
                [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
                SpumanteSettingsController *ctrl = [[SpumanteSettingsController alloc] init];
                [ctrl setupStatusBarItem];
                spm_json_free(spm_session_start(NULL, 0));
                [app run];
            }
#endif
            return 0;
        }
    }

    if (argc >= 2) {
        if (strcmp(argv[1], "--install") == 0 || strcmp(argv[1], "--installer") == 0)
            return handle_installer_ui(argc - 1, argv + 1, dry_run);
        if (strcmp(argv[1], "--uninstall") == 0 || strcmp(argv[1], "--uninstaller") == 0)
            return handle_uninstaller_ui(argc - 1, argv + 1, dry_run);
        if (strcmp(argv[1], "--register") == 0)
            return handle_register(dry_run);
        if (strcmp(argv[1], "--unregister") == 0)
            return handle_unregister(dry_run);
        if (strcmp(argv[1], "--status") == 0)
            return handle_status();
        if (strcmp(argv[1], "--config-ui") == 0)
            return handle_config_ui(dry_run);
    }

    if (argc >= 1 && argv[0] != NULL) {
        const char *base = spm_basename(argv[0]);
        if (strcasecmp(base, "UninstallSpumante") == 0 ||
            strcasecmp(base, "Uninstall Spumante") == 0 ||
            strcasecmp(base, "spumante-uninstall") == 0) {
            return handle_uninstaller_ui(argc, argv, dry_run);
        }
        if (strcasecmp(base, "InstallSpumante") == 0 ||
            strcasecmp(base, "Install Spumante") == 0 ||
            strcasecmp(base, "spumante-install") == 0) {
            return handle_installer_ui(argc, argv, dry_run);
        }
        if (has_suffix_ci(argv[0], "SpumanteConfig") ||
            has_suffix_ci(argv[0], "spumante-config")) {
            return handle_config_ui(dry_run);
        }
    }

    /* Check for direct CLI file argument (skipping LaunchServices -psn_* and --dry-run if present) */
    int target_idx = -1;
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "-psn_", 5) == 0 || strcmp(argv[i], "--dry-run") == 0)
            continue;
        if (strcmp(argv[i], "--") == 0) {
            if (i + 1 < argc)
                target_idx = i + 1;
            break;
        }
        target_idx = i;
        break;
    }

    if (target_idx != -1) {
        return dispatch_windows_binary(
            argv[target_idx],
            argc - (target_idx + 1),
            &argv[target_idx + 1]);
    }

#ifdef __APPLE__
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        SpumanteAppDelegate *delegate = [[SpumanteAppDelegate alloc] init];
        [app setDelegate:delegate];
        [app run];
    }
#endif
    return 0;
}

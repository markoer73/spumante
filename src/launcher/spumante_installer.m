/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: native AppKit Installer & Uninstaller GUI applications.
 */

#import <Cocoa/Cocoa.h>
#include <sys/stat.h>
#include <unistd.h>
#include "spm_util.h"
#include "spm_paths.h"
#include "spm_config.h"
#include "spm_runtime.h"
#include "spumante_installer.h"

/* Print a JSON document (indent=2) followed by a newline and release it. */
static void emit_json(spm_json *doc)
{
    char *text = spm_json_dump(doc, 2);
    printf("%s\n", text);
    fflush(stdout);
    free(text);
    spm_json_free(doc);
}

#ifdef __APPLE__

/* -------------------------------------------------------------------------- */
/* Shared Helpers                                                             */
/* -------------------------------------------------------------------------- */

static NSString *spm_effective_home_dir(void)
{
    const char *h = spm_home_dir();
    return h ? [NSString stringWithUTF8String:h] : spm_effective_home_dir();
}

static NSString *find_source_dir(void)
{
    const char *env_src = getenv("SPUMANTE_SOURCE_DIR");
    if (env_src && spm_is_dir(env_src)) {
        return [NSString stringWithUTF8String:env_src];
    }

    /* 1. If running from .../Install Spumante.app, the source disk image root is 2 levels up */
    NSURL *bundleURL = [[NSBundle mainBundle] bundleURL];
    if (bundleURL) {
        NSURL *volURL = [bundleURL URLByDeletingLastPathComponent];
        NSString *p = [volURL path];
        NSString *spm = [p stringByAppendingPathComponent:@"Spumante.app"];
        if ([[NSFileManager defaultManager] fileExistsAtPath:spm]) {
            return p;
        }
    }

    /* 2. Check current working directory */
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        NSString *p = [NSString stringWithUTF8String:cwd];
        NSString *spm = [p stringByAppendingPathComponent:@"Spumante.app"];
        if ([[NSFileManager defaultManager] fileExistsAtPath:spm]) {
            return p;
        }
    }

    /* 3. Check Resources of bundle */
    NSString *res = [[NSBundle mainBundle] resourcePath];
    if (res) {
        NSString *spm = [res stringByAppendingPathComponent:@"Spumante.app"];
        if ([[NSFileManager defaultManager] fileExistsAtPath:spm]) {
            return res;
        }
    }

    return @"/Volumes/Spumante";
}

static BOOL copy_app_bundle(NSString *srcDir, NSString *dstDir, NSString *appName, NSString **errOut)
{
    NSString *src = [srcDir stringByAppendingPathComponent:appName];
    NSString *dst = [dstDir stringByAppendingPathComponent:appName];
    NSFileManager *fm = [NSFileManager defaultManager];

    if (![fm fileExistsAtPath:src]) {
        if (errOut) {
            *errOut = [NSString stringWithFormat:@"Source '%@' not found in %@", appName, srcDir];
        }
        return NO;
    }

    [fm createDirectoryAtPath:dstDir withIntermediateDirectories:YES attributes:nil error:nil];

    /* Remove existing copy if present */
    if ([fm fileExistsAtPath:dst]) {
        NSError *rmErr = nil;
        if (![fm removeItemAtPath:dst error:&rmErr]) {
            if (errOut) {
                *errOut = [NSString stringWithFormat:@"Could not replace existing %@: %@", dst, [rmErr localizedDescription]];
            }
            return NO;
        }
    }

    /* Use ditto to preserve resource forks, extended attributes, and codesign signatures */
    NSTask *dittoTask = [[NSTask alloc] init];
    [dittoTask setLaunchPath:@"/usr/bin/ditto"];
    [dittoTask setArguments:@[@"--rsrc", @"--extattr", src, dst]];
    NSError *dittoErr = nil;
    if (![dittoTask launchAndReturnError:&dittoErr]) {
        if (errOut) {
            *errOut = [NSString stringWithFormat:@"Failed to copy %@: %@", appName, [dittoErr localizedDescription]];
        }
        return NO;
    }
    [dittoTask waitUntilExit];
    if ([dittoTask terminationStatus] != 0) {
        if (errOut) {
            *errOut = [NSString stringWithFormat:@"ditto failed with status %d copying %@", [dittoTask terminationStatus], appName];
        }
        return NO;
    }
    return YES;
}

static BOOL install_cli_symlinks(NSString *targetAppsDir, NSString *binDir, NSString **errOut)
{
    NSFileManager *fm = [NSFileManager defaultManager];
    [fm createDirectoryAtPath:binDir withIntermediateDirectories:YES attributes:nil error:nil];

    NSString *spumanteBin = [targetAppsDir stringByAppendingPathComponent:@"Spumante.app/Contents/Resources/wine-install-x86/bin/spumante"];
    NSString *spumanteConfigBin = [targetAppsDir stringByAppendingPathComponent:@"Spumante Config.app/Contents/MacOS/SpumanteConfig"];

    if (![fm fileExistsAtPath:spumanteBin]) {
        spumanteBin = [targetAppsDir stringByAppendingPathComponent:@"Spumante.app/Contents/MacOS/Spumante"];
    }

    NSString *link1 = [binDir stringByAppendingPathComponent:@"spumante"];
    NSString *link2 = [binDir stringByAppendingPathComponent:@"spumante-config"];

    [fm removeItemAtPath:link1 error:nil];
    [fm removeItemAtPath:link2 error:nil];

    NSError *e1 = nil, *e2 = nil;
    BOOL ok1 = [fm createSymbolicLinkAtPath:link1 withDestinationPath:spumanteBin error:&e1];
    BOOL ok2 = [fm createSymbolicLinkAtPath:link2 withDestinationPath:spumanteConfigBin error:&e2];

    if (!ok1 || !ok2) {
        if (errOut) {
            *errOut = [NSString stringWithFormat:@"Could not create symlinks in %@: %@", binDir, e1 ? [e1 localizedDescription] : (e2 ? [e2 localizedDescription] : @"unknown error")];
        }
        return NO;
    }
    return YES;
}

/* -------------------------------------------------------------------------- */
/* Native AppKit Installer                                                    */
/* -------------------------------------------------------------------------- */

@interface SpumanteInstallerController : NSObject <NSWindowDelegate>

@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) NSButton *installCliCheck;
@property (nonatomic, strong) NSButton *installAgentCheck;
@property (nonatomic, strong) NSButton *prefixInitCheck;
@property (nonatomic, strong) NSProgressIndicator *progressIndicator;
@property (nonatomic, strong) NSTextField *statusLabel;
@property (nonatomic, strong) NSButton *cancelButton;
@property (nonatomic, strong) NSButton *actionButton;

@property (nonatomic, copy) NSString *sourceDir;
@property (nonatomic, copy) NSString *targetAppsDir;
@property (nonatomic, copy) NSString *binDir;
@property (nonatomic, assign) BOOL isInstalling;
@property (nonatomic, assign) BOOL isDone;

- (void)setupUI;
- (void)startInstallation;

@end

@implementation SpumanteInstallerController

- (instancetype)init
{
    self = [super init];
    if (self) {
        _sourceDir = find_source_dir();
        _targetAppsDir = @"/Applications";
        _binDir = @"/usr/local/bin";
        NSFileManager *fm = [NSFileManager defaultManager];
        if (![fm isWritableFileAtPath:_binDir] && ![fm isWritableFileAtPath:@"/usr/local"]) {
            NSString *userLocal = [spm_effective_home_dir() stringByAppendingPathComponent:@".local/bin"];
            _binDir = userLocal;
        }
    }
    return self;
}

- (void)setupUI
{
    NSRect frame = NSMakeRect(0, 0, 520, 390);
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    self.window = [[NSWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
    [self.window setTitle:@"Spumante Installer"];
    [self.window center];
    [self.window setDelegate:self];

    NSView *content = [self.window contentView];

    /* Header: Icon + Title + Subtitle */
    NSImage *appIcon = [NSApp applicationIconImage];
    if (!appIcon) {
        NSString *icnsPath = [[NSBundle mainBundle] pathForResource:@"spumante" ofType:@"icns"];
        if (icnsPath) {
            appIcon = [[NSImage alloc] initWithContentsOfFile:icnsPath];
        }
    }
    NSImageView *iconView = [[NSImageView alloc] initWithFrame:NSMakeRect(24, 304, 64, 64)];
    if (appIcon) {
        [iconView setImage:appIcon];
    }
    [content addSubview:iconView];

    NSTextField *titleLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(104, 334, 390, 26)];
    [titleLabel setStringValue:@"Install Spumante for macOS"];
    [titleLabel setFont:[NSFont boldSystemFontOfSize:18.0]];
    [titleLabel setBezeled:NO];
    [titleLabel setDrawsBackground:NO];
    [titleLabel setEditable:NO];
    [titleLabel setSelectable:NO];
    [content addSubview:titleLabel];

    NSTextField *subLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(104, 310, 390, 18)];
    [subLabel setStringValue:@"High-performance Windows app compatibility on Apple Silicon & Intel"];
    [subLabel setFont:[NSFont systemFontOfSize:12.0]];
    [subLabel setTextColor:[NSColor secondaryLabelColor]];
    [subLabel setBezeled:NO];
    [subLabel setDrawsBackground:NO];
    [subLabel setEditable:NO];
    [subLabel setSelectable:NO];
    [content addSubview:subLabel];

    NSBox *sep = [[NSBox alloc] initWithFrame:NSMakeRect(24, 292, 472, 1)];
    [sep setBoxType:NSBoxSeparator];
    [content addSubview:sep];

    /* Options Box */
    NSBox *box = [[NSBox alloc] initWithFrame:NSMakeRect(24, 150, 472, 130)];
    [box setTitle:@"Installation Options"];
    [box setTitleFont:[NSFont boldSystemFontOfSize:12.0]];
    NSView *boxContent = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 450, 105)];

    NSButton *appCheck = [NSButton checkboxWithTitle:@"Install Spumante.app & Spumante Config.app to /Applications" target:nil action:nil];
    [appCheck setFrame:NSMakeRect(12, 78, 430, 22)];
    [appCheck setState:NSControlStateValueOn];
    [appCheck setEnabled:NO];
    [boxContent addSubview:appCheck];

    self.installCliCheck = [NSButton checkboxWithTitle:@"Install command-line tools (spumante in /usr/local/bin)" target:nil action:nil];
    [self.installCliCheck setFrame:NSMakeRect(12, 54, 430, 22)];
    [self.installCliCheck setState:NSControlStateValueOn];
    [boxContent addSubview:self.installCliCheck];

    self.installAgentCheck = [NSButton checkboxWithTitle:@"Start menu bar tray at login (keeps Wine session warm for <1s launches)" target:nil action:nil];
    [self.installAgentCheck setFrame:NSMakeRect(12, 30, 430, 22)];
    [self.installAgentCheck setState:NSControlStateValueOn];
    [boxContent addSubview:self.installAgentCheck];

    self.prefixInitCheck = [NSButton checkboxWithTitle:@"Initialize dedicated Wine prefix (~/.spumante)" target:nil action:nil];
    [self.prefixInitCheck setFrame:NSMakeRect(12, 6, 430, 22)];
    [self.prefixInitCheck setState:NSControlStateValueOn];
    [boxContent addSubview:self.prefixInitCheck];

    [box setContentView:boxContent];
    [content addSubview:box];

    /* Progress & Status */
    self.progressIndicator = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(24, 110, 472, 14)];
    [self.progressIndicator setStyle:NSProgressIndicatorStyleBar];
    [self.progressIndicator setIndeterminate:YES];
    [self.progressIndicator setHidden:YES];
    [content addSubview:self.progressIndicator];

    self.statusLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(24, 78, 472, 22)];
    [self.statusLabel setStringValue:@"Click Install to set up Spumante on your Mac."];
    [self.statusLabel setFont:[NSFont systemFontOfSize:12.0]];
    [self.statusLabel setTextColor:[NSColor secondaryLabelColor]];
    [self.statusLabel setBezeled:NO];
    [self.statusLabel setDrawsBackground:NO];
    [self.statusLabel setEditable:NO];
    [self.statusLabel setSelectable:NO];
    [content addSubview:self.statusLabel];

    /* Bottom Buttons */
    self.cancelButton = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancelAction:)];
    [self.cancelButton setFrame:NSMakeRect(306, 20, 96, 32)];
    [content addSubview:self.cancelButton];

    self.actionButton = [NSButton buttonWithTitle:@"Install" target:self action:@selector(actionButtonAction:)];
    [self.actionButton setFrame:NSMakeRect(402, 20, 96, 32)];
    [self.actionButton setKeyEquivalent:@"\r"];
    [content addSubview:self.actionButton];

    [self.window makeKeyAndOrderFront:nil];
}

- (void)cancelAction:(id)sender
{
    (void)sender;
    [NSApp terminate:nil];
}

- (void)actionButtonAction:(id)sender
{
    (void)sender;
    if (self.isDone) {
        NSString *cfgApp = [self.targetAppsDir stringByAppendingPathComponent:@"Spumante Config.app"];
        [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:cfgApp] configuration:[NSWorkspaceOpenConfiguration configuration] completionHandler:nil];
        [NSApp terminate:nil];
        return;
    }
    if (self.isInstalling)
        return;
    [self startInstallation];
}

- (void)startInstallation
{
    self.isInstalling = YES;
    [self.actionButton setEnabled:NO];
    [self.cancelButton setEnabled:NO];
    [self.installCliCheck setEnabled:NO];
    [self.installAgentCheck setEnabled:NO];
    [self.prefixInitCheck setEnabled:NO];

    [self.progressIndicator setHidden:NO];
    [self.progressIndicator startAnimation:nil];
    [self.statusLabel setTextColor:[NSColor labelColor]];
    [self.statusLabel setStringValue:@"Installing Spumante..."];

    BOOL doCli = (self.installCliCheck.state == NSControlStateValueOn);
    BOOL doAgent = (self.installAgentCheck.state == NSControlStateValueOn);
    BOOL doInitPrefix = (self.prefixInitCheck.state == NSControlStateValueOn);
    NSString *src = self.sourceDir;
    NSString *dst = self.targetAppsDir;
    NSString *bin = self.binDir;

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSString *err = nil;

        dispatch_async(dispatch_get_main_queue(), ^{
            [self.statusLabel setStringValue:@"Copying Spumante.app to /Applications..."];
        });
        if (!copy_app_bundle(src, dst, @"Spumante.app", &err)) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self finishWithError:err];
            });
            return;
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            [self.statusLabel setStringValue:@"Copying Spumante Config.app to /Applications..."];
        });
        if (!copy_app_bundle(src, dst, @"Spumante Config.app", &err)) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self finishWithError:err];
            });
            return;
        }

        if (doCli) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self.statusLabel setStringValue:@"Installing command-line tools..."];
            });
            install_cli_symlinks(dst, bin, nil);
        }

        NSString *installedSpumante = [dst stringByAppendingPathComponent:@"Spumante.app/Contents/MacOS/Spumante"];
        if (doAgent && [[NSFileManager defaultManager] fileExistsAtPath:installedSpumante]) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self.statusLabel setStringValue:@"Configuring menu bar login agent..."];
            });
            NSTask *agentTask = [NSTask launchedTaskWithLaunchPath:installedSpumante arguments:@[@"--install-login-agent"]];
            [agentTask waitUntilExit];
        }

        if (doInitPrefix && [[NSFileManager defaultManager] fileExistsAtPath:installedSpumante]) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self.statusLabel setStringValue:@"Initializing dedicated Wine prefix (~/.spumante)..."];
            });
            NSTask *prefixTask = [NSTask launchedTaskWithLaunchPath:installedSpumante arguments:@[@"--init-prefix"]];
            [prefixTask waitUntilExit];
        }

        /* Register LaunchServices */
        if ([[NSFileManager defaultManager] fileExistsAtPath:installedSpumante]) {
            NSTask *regTask = [NSTask launchedTaskWithLaunchPath:installedSpumante arguments:@[@"--register"]];
            [regTask waitUntilExit];
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            [self finishWithSuccess];
        });
    });
}

- (void)finishWithError:(NSString *)errMsg
{
    self.isInstalling = NO;
    [self.progressIndicator stopAnimation:nil];
    [self.progressIndicator setHidden:YES];
    [self.statusLabel setTextColor:[NSColor systemRedColor]];
    [self.statusLabel setStringValue:[NSString stringWithFormat:@"Installation failed: %@", errMsg]];
    [self.cancelButton setTitle:@"Close"];
    [self.cancelButton setEnabled:YES];
}

- (void)finishWithSuccess
{
    self.isInstalling = NO;
    self.isDone = YES;
    [self.progressIndicator stopAnimation:nil];
    [self.progressIndicator setHidden:YES];
    [self.statusLabel setTextColor:[NSColor systemGreenColor]];
    [self.statusLabel setStringValue:@"Spumante was installed successfully!"];
    [self.cancelButton setTitle:@"Close"];
    [self.cancelButton setEnabled:YES];
    [self.actionButton setTitle:@"Open Settings"];
    [self.actionButton setEnabled:YES];
}

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    (void)sender;
    [NSApp terminate:nil];
    return YES;
}

@end

/* -------------------------------------------------------------------------- */
/* Native AppKit Uninstaller                                                  */
/* -------------------------------------------------------------------------- */

@interface SpumanteUninstallerController : NSObject <NSWindowDelegate>

@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) NSButton *purgeDataCheck;
@property (nonatomic, strong) NSProgressIndicator *progressIndicator;
@property (nonatomic, strong) NSTextField *statusLabel;
@property (nonatomic, strong) NSButton *cancelButton;
@property (nonatomic, strong) NSButton *actionButton;

@property (nonatomic, copy) NSString *targetAppsDir;
@property (nonatomic, copy) NSString *binDir;
@property (nonatomic, assign) BOOL isUninstalling;
@property (nonatomic, assign) BOOL isDone;

- (void)setupUI;
- (void)startUninstallation;

@end

@implementation SpumanteUninstallerController

- (instancetype)init
{
    self = [super init];
    if (self) {
        _targetAppsDir = @"/Applications";
        _binDir = @"/usr/local/bin";
    }
    return self;
}

- (void)setupUI
{
    NSRect frame = NSMakeRect(0, 0, 520, 360);
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    self.window = [[NSWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
    [self.window setTitle:@"Spumante Uninstaller"];
    [self.window center];
    [self.window setDelegate:self];

    NSView *content = [self.window contentView];

    /* Header */
    NSImage *appIcon = [NSApp applicationIconImage];
    if (!appIcon) {
        NSString *icnsPath = [[NSBundle mainBundle] pathForResource:@"spumante" ofType:@"icns"];
        if (icnsPath) {
            appIcon = [[NSImage alloc] initWithContentsOfFile:icnsPath];
        }
    }
    NSImageView *iconView = [[NSImageView alloc] initWithFrame:NSMakeRect(24, 274, 64, 64)];
    if (appIcon) {
        [iconView setImage:appIcon];
    }
    [content addSubview:iconView];

    NSTextField *titleLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(104, 304, 390, 26)];
    [titleLabel setStringValue:@"Uninstall Spumante for macOS"];
    [titleLabel setFont:[NSFont boldSystemFontOfSize:18.0]];
    [titleLabel setBezeled:NO];
    [titleLabel setDrawsBackground:NO];
    [titleLabel setEditable:NO];
    [titleLabel setSelectable:NO];
    [content addSubview:titleLabel];

    NSTextField *subLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(104, 280, 390, 18)];
    [subLabel setStringValue:@"Remove Spumante and its components from your Mac"];
    [subLabel setFont:[NSFont systemFontOfSize:12.0]];
    [subLabel setTextColor:[NSColor secondaryLabelColor]];
    [subLabel setBezeled:NO];
    [subLabel setDrawsBackground:NO];
    [subLabel setEditable:NO];
    [subLabel setSelectable:NO];
    [content addSubview:subLabel];

    NSBox *sep = [[NSBox alloc] initWithFrame:NSMakeRect(24, 262, 472, 1)];
    [sep setBoxType:NSBoxSeparator];
    [content addSubview:sep];

    /* Details Box */
    NSBox *box = [[NSBox alloc] initWithFrame:NSMakeRect(24, 130, 472, 120)];
    [box setTitle:@"Components to Remove"];
    [box setTitleFont:[NSFont boldSystemFontOfSize:12.0]];
    NSView *boxContent = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 450, 95)];

    NSTextField *detailsLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 42, 430, 50)];
    [detailsLabel setStringValue:@"• Spumante.app and Spumante Config.app from /Applications\n• Command-line tools (spumante, spumante-config) from /usr/local/bin\n• Menu bar tray login agent (org.spumante.tray.plist)"];
    [detailsLabel setFont:[NSFont systemFontOfSize:11.5]];
    [detailsLabel setTextColor:[NSColor secondaryLabelColor]];
    [detailsLabel setBezeled:NO];
    [detailsLabel setDrawsBackground:NO];
    [detailsLabel setEditable:NO];
    [detailsLabel setSelectable:NO];
    [boxContent addSubview:detailsLabel];

    self.purgeDataCheck = [NSButton checkboxWithTitle:@"Also delete Wine prefix (~/.spumante) & settings (~/.config/spumante)" target:nil action:nil];
    [self.purgeDataCheck setFrame:NSMakeRect(12, 10, 430, 22)];
    [self.purgeDataCheck setState:NSControlStateValueOff];
    [boxContent addSubview:self.purgeDataCheck];

    [box setContentView:boxContent];
    [content addSubview:box];

    /* Progress & Status */
    self.progressIndicator = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(24, 98, 472, 14)];
    [self.progressIndicator setStyle:NSProgressIndicatorStyleBar];
    [self.progressIndicator setIndeterminate:YES];
    [self.progressIndicator setHidden:YES];
    [content addSubview:self.progressIndicator];

    self.statusLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(24, 68, 472, 22)];
    [self.statusLabel setStringValue:@"Click Uninstall to remove Spumante from your Mac."];
    [self.statusLabel setFont:[NSFont systemFontOfSize:12.0]];
    [self.statusLabel setTextColor:[NSColor secondaryLabelColor]];
    [self.statusLabel setBezeled:NO];
    [self.statusLabel setDrawsBackground:NO];
    [self.statusLabel setEditable:NO];
    [self.statusLabel setSelectable:NO];
    [content addSubview:self.statusLabel];

    /* Bottom Buttons */
    self.cancelButton = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancelAction:)];
    [self.cancelButton setFrame:NSMakeRect(306, 20, 96, 32)];
    [content addSubview:self.cancelButton];

    self.actionButton = [NSButton buttonWithTitle:@"Uninstall" target:self action:@selector(actionButtonAction:)];
    [self.actionButton setFrame:NSMakeRect(402, 20, 96, 32)];
    [self.actionButton setKeyEquivalent:@"\r"];
    [content addSubview:self.actionButton];

    [self.window makeKeyAndOrderFront:nil];
}

- (void)cancelAction:(id)sender
{
    (void)sender;
    [NSApp terminate:nil];
}

- (void)actionButtonAction:(id)sender
{
    (void)sender;
    if (self.isDone) {
        [NSApp terminate:nil];
        return;
    }
    if (self.isUninstalling)
        return;
    [self startUninstallation];
}

- (void)startUninstallation
{
    self.isUninstalling = YES;
    [self.actionButton setEnabled:NO];
    [self.cancelButton setEnabled:NO];
    [self.purgeDataCheck setEnabled:NO];

    [self.progressIndicator setHidden:NO];
    [self.progressIndicator startAnimation:nil];
    [self.statusLabel setTextColor:[NSColor labelColor]];
    [self.statusLabel setStringValue:@"Stopping running Spumante & Wine sessions..."];

    BOOL doPurge = (self.purgeDataCheck.state == NSControlStateValueOn);
    NSString *dst = self.targetAppsDir;
    NSString *bin = self.binDir;

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        /* 1. Stop sessions */
        NSString *installedSpumante = [dst stringByAppendingPathComponent:@"Spumante.app/Contents/MacOS/Spumante"];
        if ([[NSFileManager defaultManager] fileExistsAtPath:installedSpumante]) {
            NSTask *stopTask = [NSTask launchedTaskWithLaunchPath:installedSpumante arguments:@[@"--session-stop"]];
            [stopTask waitUntilExit];
            NSTask *unregTask = [NSTask launchedTaskWithLaunchPath:installedSpumante arguments:@[@"--unregister"]];
            [unregTask waitUntilExit];
        }
        spm_session_stop(NULL, 0);

        /* 2. Unload login agent */
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.statusLabel setStringValue:@"Removing login agent..."];
        });
        spm_login_agent_uninstall();
        NSString *plistPath = [spm_effective_home_dir() stringByAppendingPathComponent:@"Library/LaunchAgents/org.spumante.tray.plist"];
        [[NSFileManager defaultManager] removeItemAtPath:plistPath error:nil];

        /* 3. Remove CLI symlinks */
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.statusLabel setStringValue:@"Removing command-line symlinks..."];
        });
        [[NSFileManager defaultManager] removeItemAtPath:[bin stringByAppendingPathComponent:@"spumante"] error:nil];
        [[NSFileManager defaultManager] removeItemAtPath:[bin stringByAppendingPathComponent:@"spumante-config"] error:nil];
        NSString *userBin = [spm_effective_home_dir() stringByAppendingPathComponent:@".local/bin"];
        [[NSFileManager defaultManager] removeItemAtPath:[userBin stringByAppendingPathComponent:@"spumante"] error:nil];
        [[NSFileManager defaultManager] removeItemAtPath:[userBin stringByAppendingPathComponent:@"spumante-config"] error:nil];

        /* 4. Remove Apps */
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.statusLabel setStringValue:@"Removing applications from /Applications..."];
        });
        [[NSFileManager defaultManager] removeItemAtPath:[dst stringByAppendingPathComponent:@"Spumante.app"] error:nil];
        [[NSFileManager defaultManager] removeItemAtPath:[dst stringByAppendingPathComponent:@"Spumante Config.app"] error:nil];

        /* 5. Optional purge */
        if (doPurge) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self.statusLabel setStringValue:@"Removing Wine prefix and configurations..."];
            });
            [[NSFileManager defaultManager] removeItemAtPath:[spm_effective_home_dir() stringByAppendingPathComponent:@".spumante"] error:nil];
            [[NSFileManager defaultManager] removeItemAtPath:[spm_effective_home_dir() stringByAppendingPathComponent:@".spumante-prefix"] error:nil];
            [[NSFileManager defaultManager] removeItemAtPath:[spm_effective_home_dir() stringByAppendingPathComponent:@".config/spumante"] error:nil];
            [[NSFileManager defaultManager] removeItemAtPath:[spm_effective_home_dir() stringByAppendingPathComponent:@"Library/Logs/Spumante"] error:nil];
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            self.isUninstalling = NO;
            self.isDone = YES;
            [self.progressIndicator stopAnimation:nil];
            [self.progressIndicator setHidden:YES];
            [self.statusLabel setTextColor:[NSColor systemGreenColor]];
            [self.statusLabel setStringValue:@"Spumante has been completely removed from your Mac."];
            [self.cancelButton setHidden:YES];
            [self.actionButton setTitle:@"Done"];
            [self.actionButton setEnabled:YES];
        });
    });
}

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    (void)sender;
    [NSApp terminate:nil];
    return YES;
}

@end

#endif /* __APPLE__ */

/* -------------------------------------------------------------------------- */
/* Public Entrypoints                                                         */
/* -------------------------------------------------------------------------- */

int handle_installer_ui(int argc, char *argv[], int dry_run)
{
    const char *target_dir = "/Applications";
    const char *bin_dir = "/usr/local/bin";
    const char *src_dir = NULL;
    int unattended = 0;
    int do_cli = 1;
    int do_agent = 1;
    int do_init_prefix = 1;
    int open_settings = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--unattended") == 0 || strcmp(argv[i], "--cli") == 0 || strcmp(argv[i], "--headless") == 0)
            unattended = 1;
        else if (strcmp(argv[i], "--target-dir") == 0 && i + 1 < argc)
            target_dir = argv[++i];
        else if (strcmp(argv[i], "--bin-dir") == 0 && i + 1 < argc)
            bin_dir = argv[++i];
        else if (strcmp(argv[i], "--source-dir") == 0 && i + 1 < argc)
            src_dir = argv[++i];
        else if (strcmp(argv[i], "--no-cli-tools") == 0)
            do_cli = 0;
        else if (strcmp(argv[i], "--no-login-agent") == 0)
            do_agent = 0;
        else if (strcmp(argv[i], "--skip-prefix-init") == 0)
            do_init_prefix = 0;
        else if (strcmp(argv[i], "--no-open-config") == 0)
            open_settings = 0;
    }

    const char *no_ui_env = getenv("SPUMANTE_INSTALLER_NO_UI");
    if (no_ui_env && strcmp(no_ui_env, "1") == 0)
        unattended = 1;

    if (dry_run) {
        spm_json *doc = spm_json_object();
        spm_json_set_str(doc, "action", "install-spumante");
        spm_json_set_str(doc, "target_dir", target_dir);
        spm_json_set_str(doc, "bin_dir", bin_dir);
        spm_json_set_bool(doc, "install_cli", do_cli);
        spm_json_set_bool(doc, "install_login_agent", do_agent);
        spm_json_set_bool(doc, "init_prefix", do_init_prefix);
        spm_json_set_bool(doc, "unattended", unattended);
        spm_json_set_str(doc, "status", "dry_run_ready");
        emit_json(doc);
        return 0;
    }

#ifdef __APPLE__
    if (!unattended) {
        @autoreleasepool {
            NSApplication *app = [NSApplication sharedApplication];
            [app setActivationPolicy:NSApplicationActivationPolicyRegular];
            SpumanteInstallerController *ctrl = [[SpumanteInstallerController alloc] init];
            if (target_dir) ctrl.targetAppsDir = [NSString stringWithUTF8String:target_dir];
            if (bin_dir) ctrl.binDir = [NSString stringWithUTF8String:bin_dir];
            if (src_dir) ctrl.sourceDir = [NSString stringWithUTF8String:src_dir];
            [ctrl setupUI];
            [app activateIgnoringOtherApps:YES];
            [app run];
        }
        return 0;
    }

    /* Unattended CLI execution */
    @autoreleasepool {
        NSString *sDir = src_dir ? [NSString stringWithUTF8String:src_dir] : find_source_dir();
        NSString *tDir = [NSString stringWithUTF8String:target_dir];
        NSString *bDir = [NSString stringWithUTF8String:bin_dir];
        NSString *err = nil;

        printf("[Spumante Installer] Installing from '%s' to '%s'...\n", [sDir UTF8String], [tDir UTF8String]);
        if (!copy_app_bundle(sDir, tDir, @"Spumante.app", &err)) {
            fprintf(stderr, "[Spumante Installer] ERROR: %s\n", [err UTF8String]);
            return 1;
        }
        if (!copy_app_bundle(sDir, tDir, @"Spumante Config.app", &err)) {
            fprintf(stderr, "[Spumante Installer] ERROR: %s\n", [err UTF8String]);
            return 1;
        }
        if (do_cli) {
            install_cli_symlinks(tDir, bDir, nil);
        }
        NSString *installedSpumante = [tDir stringByAppendingPathComponent:@"Spumante.app/Contents/MacOS/Spumante"];
        if (do_agent && [[NSFileManager defaultManager] fileExistsAtPath:installedSpumante]) {
            NSTask *t = [NSTask launchedTaskWithLaunchPath:installedSpumante arguments:@[@"--install-login-agent"]];
            [t waitUntilExit];
        }
        if (do_init_prefix && [[NSFileManager defaultManager] fileExistsAtPath:installedSpumante]) {
            NSTask *t = [NSTask launchedTaskWithLaunchPath:installedSpumante arguments:@[@"--init-prefix"]];
            [t waitUntilExit];
        }
        if ([[NSFileManager defaultManager] fileExistsAtPath:installedSpumante]) {
            NSTask *t = [NSTask launchedTaskWithLaunchPath:installedSpumante arguments:@[@"--register"]];
            [t waitUntilExit];
        }
        if (open_settings) {
            NSString *cfg = [tDir stringByAppendingPathComponent:@"Spumante Config.app"];
            [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:cfg] configuration:[NSWorkspaceOpenConfiguration configuration] completionHandler:nil];
        }
        printf("[Spumante Installer] Installation completed successfully.\n");
        return 0;
    }
#else
    fprintf(stderr, "[Spumante Installer] Native installer is supported on macOS only.\n");
    return 1;
#endif
}

int handle_uninstaller_ui(int argc, char *argv[], int dry_run)
{
    const char *target_dir = "/Applications";
    const char *bin_dir = "/usr/local/bin";
    int unattended = 0;
    int purge_data = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--unattended") == 0 || strcmp(argv[i], "--cli") == 0 || strcmp(argv[i], "--headless") == 0)
            unattended = 1;
        else if (strcmp(argv[i], "--target-dir") == 0 && i + 1 < argc)
            target_dir = argv[++i];
        else if (strcmp(argv[i], "--bin-dir") == 0 && i + 1 < argc)
            bin_dir = argv[++i];
        else if (strcmp(argv[i], "--purge-data") == 0 || strcmp(argv[i], "--purge-prefix") == 0)
            purge_data = 1;
    }

    const char *no_ui_env = getenv("SPUMANTE_INSTALLER_NO_UI");
    if (no_ui_env && strcmp(no_ui_env, "1") == 0)
        unattended = 1;

    if (dry_run) {
        spm_json *doc = spm_json_object();
        spm_json_set_str(doc, "action", "uninstall-spumante");
        spm_json_set_str(doc, "target_dir", target_dir);
        spm_json_set_str(doc, "bin_dir", bin_dir);
        spm_json_set_bool(doc, "purge_data", purge_data);
        spm_json_set_bool(doc, "unattended", unattended);
        spm_json_set_str(doc, "status", "dry_run_ready");
        emit_json(doc);
        return 0;
    }

#ifdef __APPLE__
    if (!unattended) {
        @autoreleasepool {
            NSApplication *app = [NSApplication sharedApplication];
            [app setActivationPolicy:NSApplicationActivationPolicyRegular];
            SpumanteUninstallerController *ctrl = [[SpumanteUninstallerController alloc] init];
            if (target_dir) ctrl.targetAppsDir = [NSString stringWithUTF8String:target_dir];
            if (bin_dir) ctrl.binDir = [NSString stringWithUTF8String:bin_dir];
            [ctrl setupUI];
            [app activateIgnoringOtherApps:YES];
            [app run];
        }
        return 0;
    }

    /* Unattended CLI execution */
    @autoreleasepool {
        NSString *tDir = [NSString stringWithUTF8String:target_dir];
        NSString *bDir = [NSString stringWithUTF8String:bin_dir];

        printf("[Spumante Uninstaller] Removing Spumante from '%s'...\n", [tDir UTF8String]);
        NSString *installedSpumante = [tDir stringByAppendingPathComponent:@"Spumante.app/Contents/MacOS/Spumante"];
        if ([[NSFileManager defaultManager] fileExistsAtPath:installedSpumante]) {
            NSTask *t = [NSTask launchedTaskWithLaunchPath:installedSpumante arguments:@[@"--session-stop"]];
            [t waitUntilExit];
            NSTask *t2 = [NSTask launchedTaskWithLaunchPath:installedSpumante arguments:@[@"--unregister"]];
            [t2 waitUntilExit];
        }
        spm_session_stop(NULL, 0);
        spm_login_agent_uninstall();

        NSString *plistPath = [spm_effective_home_dir() stringByAppendingPathComponent:@"Library/LaunchAgents/org.spumante.tray.plist"];
        [[NSFileManager defaultManager] removeItemAtPath:plistPath error:nil];

        [[NSFileManager defaultManager] removeItemAtPath:[bDir stringByAppendingPathComponent:@"spumante"] error:nil];
        [[NSFileManager defaultManager] removeItemAtPath:[bDir stringByAppendingPathComponent:@"spumante-config"] error:nil];
        NSString *userBin = [spm_effective_home_dir() stringByAppendingPathComponent:@".local/bin"];
        [[NSFileManager defaultManager] removeItemAtPath:[userBin stringByAppendingPathComponent:@"spumante"] error:nil];
        [[NSFileManager defaultManager] removeItemAtPath:[userBin stringByAppendingPathComponent:@"spumante-config"] error:nil];

        [[NSFileManager defaultManager] removeItemAtPath:[tDir stringByAppendingPathComponent:@"Spumante.app"] error:nil];
        [[NSFileManager defaultManager] removeItemAtPath:[tDir stringByAppendingPathComponent:@"Spumante Config.app"] error:nil];

        if (purge_data) {
            printf("[Spumante Uninstaller] Purging ~/.spumante and ~/.config/spumante...\n");
            [[NSFileManager defaultManager] removeItemAtPath:[spm_effective_home_dir() stringByAppendingPathComponent:@".spumante"] error:nil];
            [[NSFileManager defaultManager] removeItemAtPath:[spm_effective_home_dir() stringByAppendingPathComponent:@".spumante-prefix"] error:nil];
            [[NSFileManager defaultManager] removeItemAtPath:[spm_effective_home_dir() stringByAppendingPathComponent:@".config/spumante"] error:nil];
            [[NSFileManager defaultManager] removeItemAtPath:[spm_effective_home_dir() stringByAppendingPathComponent:@"Library/Logs/Spumante"] error:nil];
        }
        printf("[Spumante Uninstaller] Uninstallation completed successfully.\n");
        return 0;
    }
#else
    fprintf(stderr, "[Spumante Uninstaller] Native uninstaller is supported on macOS only.\n");
    return 1;
#endif
}

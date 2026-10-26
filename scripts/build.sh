#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Canonical surgical build and upstream borrow entrypoint for wine-parity.
# Refuses full-tree builds unless --all is explicitly provided.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DLL=""
TEST_NAME=""
ARCH="x86_64"
BUILD_ALL=0
EXPORT_PATCH=0
BORROW_DLL=""
UPSTREAM_REF=""
DRY_RUN=0
BUILD_WINETEST=0
FULL_WINE_TARGET=""
SKIP_MONOLITHIC_COMPILE=0
BUILD_EXE_HANDLER=0
BUILD_DMG=0
NOTARIZE_DMG=0
CODESIGN_IDENTITY=""
NOTARY_PROFILE=""

usage() {
    cat <<EOF
Usage: ./scripts/build.sh --dll <dll_name> [--test <test_name>] [--winetest] [--arch x86_64|arm64] [--export-patch] [--all]
       ./scripts/build.sh --borrow <dll_name> [--upstream-ref <sha_or_tag>] [--dry-run]
       ./scripts/build.sh --full-x86|--x86 [--dry-run] [--skip-monolithic-compile]
       ./scripts/build.sh --full-arm64|--arm64 [--dry-run] [--skip-monolithic-compile]
       ./scripts/build.sh --exe-handler [--dry-run]
       ./scripts/build.sh --dmg|--package-dmg [-nz|--notarize] [--codesign-identity <id>] [--notary-profile <profile>] [--dry-run]

Surgical single-target build & upstream Wine borrow script (macOS First, Linux Second).
Examples:
  ./scripts/build.sh --dll bcrypt --test test_symmetric_key
  ./scripts/build.sh --dll bcrypt --winetest
  ./scripts/build.sh --dll bcrypt --arch arm64
  ./scripts/build.sh --borrow bcrypt --dry-run
  ./scripts/build.sh --dll bcrypt --export-patch
  ./scripts/build.sh --full-x86
  ./scripts/build.sh --full-arm64
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dll)
            DLL="${2:-}"
            shift 2
            ;;
        --test)
            TEST_NAME="${2:-}"
            shift 2
            ;;
        --winetest)
            BUILD_WINETEST=1
            shift
            ;;
        --arch)
            ARCH="${2:-x86_64}"
            shift 2
            ;;
        --borrow)
            BORROW_DLL="${2:-}"
            shift 2
            ;;
        --upstream-ref)
            UPSTREAM_REF="${2:-}"
            shift 2
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        --export-patch)
            EXPORT_PATCH=1
            shift
            ;;
        --all)
            BUILD_ALL=1
            shift
            ;;
        --full-x86|--x86|--full-x86_64)
            FULL_WINE_TARGET="x86"
            ARCH="x86_64"
            BUILD_ALL=1
            shift
            ;;
        --full-arm64|--arm64)
            FULL_WINE_TARGET="arm64"
            ARCH="arm64"
            BUILD_ALL=1
            shift
            ;;
        --skip-monolithic-compile)
            SKIP_MONOLITHIC_COMPILE=1
            shift
            ;;
        --exe-handler)
            BUILD_EXE_HANDLER=1
            shift
            ;;
        --dmg|--package-dmg)
            BUILD_DMG=1
            shift
            ;;
        -nz|--notarize)
            BUILD_DMG=1
            NOTARIZE_DMG=1
            shift
            ;;
        --codesign-identity)
            CODESIGN_IDENTITY="${2:-}"
            shift 2
            ;;
        --notary-profile)
            NOTARY_PROFILE="${2:-}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ "$BUILD_DMG" -eq 1 ]]; then
    DMG_ARGS=()
    if [[ "$DRY_RUN" -eq 1 ]]; then
        DMG_ARGS+=(--dry-run)
    fi
    if [[ "$NOTARIZE_DMG" -eq 1 ]]; then
        DMG_ARGS+=(-nz)
    fi
    if [[ -n "$CODESIGN_IDENTITY" ]]; then
        DMG_ARGS+=(--codesign-identity "$CODESIGN_IDENTITY")
    fi
    if [[ -n "$NOTARY_PROFILE" ]]; then
        DMG_ARGS+=(--notary-profile "$NOTARY_PROFILE")
    fi
    python3 "$REPO_ROOT/scripts/lib/dmg_packager.py" ${DMG_ARGS[@]+"${DMG_ARGS[@]}"}
    exit 0
fi

if [[ "$BUILD_EXE_HANDLER" -eq 1 ]]; then
    EXE_HANDLER_ARGS=(--action build)
    if [[ "$DRY_RUN" -eq 1 ]]; then
        EXE_HANDLER_ARGS+=(--dry-run)
    fi
    python3 "$REPO_ROOT/scripts/lib/exe_handler.py" "${EXE_HANDLER_ARGS[@]}"
    exit 0
fi

if [[ -n "$BORROW_DLL" ]]; then
    BORROW_ARGS=(--action borrow --dll "$BORROW_DLL")
    if [[ -n "$UPSTREAM_REF" ]]; then
        BORROW_ARGS+=(--upstream-ref "$UPSTREAM_REF")
    fi
    if [[ "$DRY_RUN" -eq 1 ]]; then
        BORROW_ARGS+=(--dry-run)
    fi
    python3 "$REPO_ROOT/scripts/lib/wine_borrow.py" "${BORROW_ARGS[@]}"
    exit 0
fi

if [[ -n "$FULL_WINE_TARGET" && "$DRY_RUN" -eq 1 ]]; then
    python3 "$REPO_ROOT/scripts/lib/wine_borrow.py" --action full-build --target "$FULL_WINE_TARGET" --dry-run
    exit 0
fi

if [[ -z "$DLL" && "$BUILD_ALL" -eq 0 ]]; then
    echo "[SURGICAL BUILD GUARD] Refusing unscoped build! Specify --dll <name> [--test <test_name>] or --borrow <name> to avoid rebuilding 1000+ targets, or pass --all explicitly." >&2
    usage >&2
    exit 2
fi

if [[ "$BUILD_WINETEST" -eq 1 ]]; then
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll "${DLL:-bcrypt}" --arch "$ARCH"
    exit 0
fi

if [[ "$BUILD_ALL" -eq 1 && -z "$DLL" ]]; then
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll bcrypt --test test_symmetric_key --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll bcrypt --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll ntdll --test test_apple_silicon_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll ntdll --test test_ntsync_macos_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll ntdll --test test_arm64x_loader_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll ntdll --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll kernelbase --test test_wait_on_address --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll kernelbase --test test_win32_utility_apis --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll kernelbase --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll winemac.drv --test test_winemac_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll winemac.drv --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll mfplat --test test_mfplat_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll mfplat --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll qcap --test test_qcap_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll qcap --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll d2d1 --test test_d2d1_msl_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll d2d1 --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll winegstreamer --test test_winegstreamer_macos --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll winegstreamer --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll wineusb.sys --test test_wineusb_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll wineusb.sys --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll winebth.sys --test test_winebth_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll winebth.sys --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll ole32 --test test_ole32_services_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll ole32 --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll setupapi --test test_setupapi_copy_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll setupapi --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll win32u --test test_win32u_msg_icon_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode build --dll win32u --arch "$ARCH"
    if [[ -n "$FULL_WINE_TARGET" ]]; then
        FULL_ARGS=(--action full-build --target "$FULL_WINE_TARGET")
        if [[ "$SKIP_MONOLITHIC_COMPILE" -eq 1 ]]; then
            FULL_ARGS+=(--skip-monolithic-compile)
        fi
        python3 "$REPO_ROOT/scripts/lib/wine_borrow.py" "${FULL_ARGS[@]}"
        python3 "$REPO_ROOT/scripts/lib/exe_handler.py" --action build >/dev/null
    fi
    exit 0
fi

EXTRA_ARGS=()
if [[ -n "$TEST_NAME" ]]; then
    EXTRA_ARGS+=(--test "$TEST_NAME")
fi
if [[ "$EXPORT_PATCH" -eq 1 ]]; then
    EXTRA_ARGS+=(--export-patch)
fi

python3 "$REPO_ROOT/scripts/lib/runner.py" --mode build --dll "$DLL" --arch "$ARCH" "${EXTRA_ARGS[@]}"


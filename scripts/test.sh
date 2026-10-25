#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Canonical surgical test, stub discovery, upstream alignment check, security scanner, and linter entrypoint.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DLL=""
TEST_NAME=""
ARCH="x86_64"
CASE_FILTER=""
LIVE_VM=0
SIMULATE_STUB=0
DISCOVER_STUBS=0
CHECK_UPSTREAM=0
RUN_SECURITY=0
RUN_WINETEST=0
FETCH_SPEC=""
RUN_LINT=0
RUN_CI=0
RUN_ALL=0
SCAN_EXE=""
REQUIRE_SIGNATURE=0
VT_API_KEY_ARG=""

usage() {
    cat <<EOF
Usage: ./scripts/test.sh [options]

Surgical single-target differential test & verification script (macOS First, Linux Second).
Options:
  --dll <name>             Target Wine DLL (e.g. bcrypt, kernelbase, mfplat)
  --test <name>            Target conformance test (e.g. test_symmetric_key)
  --winetest               Run upstream Wine winetest & A/B anti-regression suite for --dll
  --filter <pattern>       Filter specific test cases or winetest subtests (e.g. "AES128*")
  --arch <x86_64|arm64>    Target architecture (default: x86_64)
  --live-vm                Execute against live QEMU Windows VM via VirtIO serial qemu-ga
  --simulate-stub          Simulate unimplemented Wine stub to verify RED failure
  --discover-stubs         Run 3-layer stub discovery scanner across DLLs
  --check-upstream         Verify upstream/manifest.json alignment and local file integrity
  --security               Run 4-gate security audit (secrets/clean-room, SAST/sandbox, ASan+UBSan, CI schedule)
  --scan-exe <path>        Run 3-gate pre-execution PE security scan (Quarantine, Authenticode, Static PE + VirusTotal)
  --require-signature      Require valid PE Authenticode signature during --scan-exe
  --vt-api-key <key>       Optional VirusTotal v3 API key for --scan-exe (or set VT_API_KEY)
  --fetch-spec <dll:api>   Generate auditable Clean-Room Spec Packet in specs/<dll>/<api>.json
  --lint                   Run WineHQ C99/C11, Markdown \[...\], and script boundary linter
  --ci                     Run full CI verification suite (linter + differential + security + winetest follow-up)
  --all                    Explicitly run all registered differential and winetest suites
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
            RUN_WINETEST=1
            shift
            ;;
        --filter)
            CASE_FILTER="${2:-}"
            shift 2
            ;;
        --arch)
            ARCH="${2:-x86_64}"
            shift 2
            ;;
        --live-vm)
            LIVE_VM=1
            shift
            ;;
        --simulate-stub)
            SIMULATE_STUB=1
            shift
            ;;
        --discover-stubs)
            DISCOVER_STUBS=1
            shift
            ;;
        --check-upstream)
            CHECK_UPSTREAM=1
            shift
            ;;
        --security)
            RUN_SECURITY=1
            shift
            ;;
        --scan-exe)
            SCAN_EXE="${2:-}"
            shift 2
            ;;
        --require-signature)
            REQUIRE_SIGNATURE=1
            shift
            ;;
        --vt-api-key)
            VT_API_KEY_ARG="${2:-}"
            shift 2
            ;;
        --fetch-spec)
            FETCH_SPEC="${2:-}"
            shift 2
            ;;
        --lint)
            RUN_LINT=1
            shift
            ;;
        --ci)
            RUN_CI=1
            shift
            ;;
        --all)
            RUN_ALL=1
            shift
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

if [[ "$RUN_LINT" -eq 1 ]]; then
    python3 "$REPO_ROOT/scripts/lib/linter.py"
    exit 0
fi

if [[ -n "$SCAN_EXE" ]]; then
    SCAN_ARGS=(--action scan --exe "$SCAN_EXE")
    if [[ "$REQUIRE_SIGNATURE" -eq 1 ]]; then
        SCAN_ARGS+=(--require-signature)
    fi
    if [[ -n "$VT_API_KEY_ARG" ]]; then
        SCAN_ARGS+=(--vt-api-key "$VT_API_KEY_ARG")
    fi
    python3 "$REPO_ROOT/scripts/lib/exe_handler.py" "${SCAN_ARGS[@]}"
    exit $?
fi

if [[ "$RUN_SECURITY" -eq 1 ]]; then
    SEC_DLL="${DLL:-bcrypt}"
    SEC_TEST="${TEST_NAME:-test_symmetric_key}"
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll "$SEC_DLL" --test "$SEC_TEST"
    exit 0
fi

if [[ "$RUN_WINETEST" -eq 1 ]]; then
    WT_DLL="${DLL:-bcrypt}"
    WT_ARGS=(--mode test --dll "$WT_DLL" --arch "$ARCH")
    if [[ -n "$CASE_FILTER" ]]; then
        WT_ARGS+=(--filter "$CASE_FILTER")
    fi
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" "${WT_ARGS[@]}"
    exit 0
fi

if [[ -n "$FETCH_SPEC" ]]; then
    python3 "$REPO_ROOT/scripts/lib/spec_fetcher.py" "$FETCH_SPEC"
    exit 0
fi

if [[ "$CHECK_UPSTREAM" -eq 1 ]]; then
    TARGET_DLL="${DLL:-bcrypt}"
    python3 "$REPO_ROOT/scripts/lib/wine_borrow.py" --action check --dll "$TARGET_DLL"
    exit 0
fi

if [[ "$DISCOVER_STUBS" -eq 1 ]]; then
    if [[ -n "$DLL" ]]; then
        python3 "$REPO_ROOT/scripts/lib/stub_scanner.py" --dll "$DLL"
    else
        python3 "$REPO_ROOT/scripts/lib/stub_scanner.py"
    fi
    exit 0
fi

if [[ "$RUN_CI" -eq 1 || "$RUN_ALL" -eq 1 ]]; then
    python3 "$REPO_ROOT/scripts/lib/linter.py"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode test --dll bcrypt --test test_symmetric_key --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode test --dll bcrypt --test test_crypto_primitives --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode test --dll ntdll --test test_apple_silicon_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode test --dll ntdll --test test_ntsync_macos_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode test --dll ntdll --test test_arm64x_loader_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode test --dll kernelbase --test test_wait_on_address --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode test --dll kernelbase --test test_win32_utility_apis --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode test --dll winemac.drv --test test_winemac_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode test --dll mfplat --test test_mfplat_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode test --dll qcap --test test_qcap_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode test --dll d2d1 --test test_d2d1_msl_parity --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll bcrypt --test test_symmetric_key
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll bcrypt --test test_crypto_primitives
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll ntdll --test test_apple_silicon_parity
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll ntdll --test test_ntsync_macos_parity
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll ntdll --test test_arm64x_loader_parity
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll kernelbase --test test_wait_on_address
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll kernelbase --test test_win32_utility_apis
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll winemac.drv --test test_winemac_parity
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll mfplat --test test_mfplat_parity
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll qcap --test test_qcap_parity
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll d2d1 --test test_d2d1_msl_parity
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode test --dll bcrypt --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode test --dll ntdll --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode test --dll kernelbase --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode test --dll winemac.drv --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode test --dll mfplat --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode test --dll qcap --arch "$ARCH"
    python3 "$REPO_ROOT/scripts/lib/winetest_runner.py" --mode test --dll d2d1 --arch "$ARCH"
    exit 0
fi

if [[ -z "$DLL" || -z "$TEST_NAME" ]]; then
    echo "[SURGICAL TEST GUARD] Refusing unscoped test run! Specify --dll <name> --test <test_name> [--filter <case>] or --winetest --dll <name>, or pass --lint / --security / --discover-stubs / --check-upstream / --ci / --all." >&2
    usage >&2
    exit 2
fi

RUNNER_ARGS=(--mode test --dll "$DLL" --test "$TEST_NAME" --arch "$ARCH")
if [[ -n "$CASE_FILTER" ]]; then
    RUNNER_ARGS+=(--filter "$CASE_FILTER")
fi
if [[ "$LIVE_VM" -eq 1 ]]; then
    RUNNER_ARGS+=(--live-vm)
fi
if [[ "$SIMULATE_STUB" -eq 1 ]]; then
    RUNNER_ARGS+=(--simulate-stub)
fi

python3 "$REPO_ROOT/scripts/lib/runner.py" "${RUNNER_ARGS[@]}"

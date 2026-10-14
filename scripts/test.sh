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
FETCH_SPEC=""
RUN_LINT=0
RUN_CI=0
RUN_ALL=0

usage() {
    cat <<EOF
Usage: ./scripts/test.sh [options]

Surgical single-target differential test & verification script (macOS First, Linux Second).
Options:
  --dll <name>             Target Wine DLL (e.g. bcrypt, kernelbase, mfplat)
  --test <name>            Target conformance test (e.g. test_symmetric_key)
  --filter <pattern>       Filter specific test cases (e.g. "AES128*")
  --arch <x86_64|arm64>    Target architecture (default: x86_64)
  --live-vm                Execute against live QEMU Windows VM via VirtIO serial qemu-ga
  --simulate-stub          Simulate unimplemented Wine stub to verify RED failure
  --discover-stubs         Run 3-layer stub discovery scanner across DLLs
  --check-upstream         Verify upstream/manifest.json alignment and local file integrity
  --security               Run 4-gate security audit (secrets/clean-room, SAST/sandbox, ASan+UBSan, CI schedule)
  --fetch-spec <dll:api>   Generate auditable Clean-Room Spec Packet in specs/<dll>/<api>.json
  --lint                   Run WineHQ C99/C11, Markdown \[...\], and script boundary linter
  --ci                     Run full CI verification suite (linter + security + differential tests)
  --all                    Explicitly run all registered differential tests
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

if [[ "$RUN_SECURITY" -eq 1 ]]; then
    SEC_DLL="${DLL:-bcrypt}"
    SEC_TEST="${TEST_NAME:-test_symmetric_key}"
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll "$SEC_DLL" --test "$SEC_TEST"
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
    python3 "$REPO_ROOT/scripts/lib/security_scanner.py" --dll bcrypt --test test_symmetric_key
    python3 "$REPO_ROOT/scripts/lib/runner.py" --mode test --dll bcrypt --test test_symmetric_key --arch "$ARCH"
    exit 0
fi

if [[ -z "$DLL" || -z "$TEST_NAME" ]]; then
    echo "[SURGICAL TEST GUARD] Refusing unscoped test run! Specify --dll <name> --test <test_name> [--filter <case>] to avoid running 1000+ tests, or pass --lint / --security / --discover-stubs / --check-upstream / --ci / --all." >&2
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

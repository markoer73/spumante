#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Canonical VM and guest agent launch/snapshot entrypoint for Spumante.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="arm64"
SNAPSHOT="clean_ready"
ACTION="launch"
DRY_RUN=0
DISK_ARGS=()
NATIVE_ARGS=()

usage() {
    cat <<EOF
Usage: ./scripts/launch.sh [--arch arm64|x86_64] [--snapshot <name>] [--disk <qcow2_path>] [--status] [--dry-run]
       ./scripts/launch.sh --register-exe-handler [--dry-run]
       ./scripts/launch.sh --exe-handler-status
       ./scripts/launch.sh --sync-drive-c
       ./scripts/launch.sh --report-bug [--app "<Name & Version>"] [--dry-run]
       ./scripts/launch.sh --report-app [--app "<Name & Version>"] [--rating Platinum|Gold|Silver|Bronze|Garbage] [--dry-run]
       ./scripts/launch.sh --config-status
       ./scripts/launch.sh --set-config <key=value> [--set-config <key=value> ...]
       ./scripts/launch.sh --config-ui [--dry-run]

Starts or restores the headless QEMU Windows 10/11 Ground-Truth VM (macOS hvf/tcg first, Linux kvm second)
with VirtIO serial qemu-ga (/tmp/spumante_qga.sock) and QMP (/tmp/spumante_qmp.sock), registers
the macOS Spumante.app LaunchServices .exe/.msi handler, or manages user-level config.json & sandboxing.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            ARCH="${2:-arm64}"
            shift 2
            ;;
        --snapshot)
            SNAPSHOT="${2:-clean_ready}"
            ACTION="snapshot"
            shift 2
            ;;
        --disk)
            DISK_ARGS=(--disk "${2:-}")
            shift 2
            ;;
        --status|--verify)
            ACTION="status"
            shift
            ;;
        --register-exe-handler)
            NATIVE_ARGS+=(--register)
            shift
            ;;
        --exe-handler-status)
            NATIVE_ARGS+=(--status)
            shift
            ;;
        --sync-drive-c|--sync-drive-c-exec)
            NATIVE_ARGS+=(--sync-drive-c)
            shift
            ;;
        --report-bug)
            NATIVE_ARGS+=(--report-bug)
            shift
            ;;
        --report-app|--report-appdb)
            NATIVE_ARGS+=(--report-app)
            shift
            ;;
        --app)
            NATIVE_ARGS+=(--app "${2:-}")
            shift 2
            ;;
        --rating)
            NATIVE_ARGS+=(--rating "${2:-Platinum}")
            shift 2
            ;;
        --config-status)
            NATIVE_ARGS+=(--config-status)
            shift
            ;;
        --set-config)
            NATIVE_ARGS+=(--set-config "${2:-}")
            shift 2
            ;;
        --config-ui)
            NATIVE_ARGS+=(--config-ui)
            shift
            ;;
        --launch-builtin)
            NATIVE_ARGS+=(--launch-builtin "${2:-}")
            shift 2
            ;;
        --open-console)
            NATIVE_ARGS+=(--open-console)
            shift
            ;;
        --dry-run)
            DRY_RUN=1
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

if [[ "${#NATIVE_ARGS[@]}" -gt 0 ]]; then
    # Config, registry cascade, sandbox, drive_c sync, issue reporting and LaunchServices registration are all
    # handled by the native Spumante binary (src/launcher); build it on demand.
    python3 "$REPO_ROOT/scripts/lib/exe_handler.py" --action ensure-native >/dev/null
    if [[ "$DRY_RUN" -eq 1 ]]; then
        NATIVE_ARGS+=(--dry-run)
    fi
    exec "$REPO_ROOT/builds/Spumante.app/Contents/MacOS/Spumante" "${NATIVE_ARGS[@]}"
fi

EXTRA_FLAGS=()
if [[ "$DRY_RUN" -eq 1 ]]; then
    EXTRA_FLAGS+=(--dry-run)
fi
if [[ "${#DISK_ARGS[@]}" -gt 0 ]]; then
    EXTRA_FLAGS+=("${DISK_ARGS[@]}")
fi

python3 "$REPO_ROOT/scripts/lib/vm_manager.py" --action "$ACTION" --arch "$ARCH" --snapshot-name "$SNAPSHOT" "${EXTRA_FLAGS[@]}"

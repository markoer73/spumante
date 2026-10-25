#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Canonical VM and guest agent launch/snapshot entrypoint for wine-parity.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="arm64"
SNAPSHOT="clean_ready"
ACTION="launch"
DRY_RUN=0
DISK_ARGS=()
EXE_HANDLER_ACTION=""
CONFIG_ACTION=""
CONFIG_UPDATES=()

usage() {
    cat <<EOF
Usage: ./scripts/launch.sh [--arch arm64|x86_64] [--snapshot <name>] [--disk <qcow2_path>] [--status] [--dry-run]
       ./scripts/launch.sh --register-exe-handler [--dry-run]
       ./scripts/launch.sh --exe-handler-status
       ./scripts/launch.sh --config-status
       ./scripts/launch.sh --set-config <key=value> [--set-config <key=value> ...]
       ./scripts/launch.sh --config-ui [--dry-run]

Starts or restores the headless QEMU Windows 10/11 Ground-Truth VM (macOS hvf/tcg first, Linux kvm second)
with VirtIO serial qemu-ga (/tmp/wine_parity_qga.sock) and QMP (/tmp/wine_parity_qmp.sock), registers
the macOS WineParity.app LaunchServices .exe/.msi handler, or manages user-level config.json & sandboxing.
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
            EXE_HANDLER_ACTION="register"
            shift
            ;;
        --exe-handler-status)
            EXE_HANDLER_ACTION="status"
            shift
            ;;
        --config-status)
            CONFIG_ACTION="status"
            shift
            ;;
        --set-config)
            CONFIG_ACTION="set"
            CONFIG_UPDATES+=(--set "${2:-}")
            shift 2
            ;;
        --config-ui)
            EXE_HANDLER_ACTION="config-ui"
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

if [[ -n "$CONFIG_ACTION" ]]; then
    if [[ "${#CONFIG_UPDATES[@]}" -gt 0 ]]; then
        python3 "$REPO_ROOT/scripts/lib/config_manager.py" --action "$CONFIG_ACTION" "${CONFIG_UPDATES[@]}"
    else
        python3 "$REPO_ROOT/scripts/lib/config_manager.py" --action "$CONFIG_ACTION"
    fi
    exit 0
fi

if [[ -n "$EXE_HANDLER_ACTION" ]]; then
    EXE_ARGS=(--action "$EXE_HANDLER_ACTION")
    if [[ "$DRY_RUN" -eq 1 ]]; then
        EXE_ARGS+=(--dry-run)
    fi
    python3 "$REPO_ROOT/scripts/lib/exe_handler.py" "${EXE_ARGS[@]}"
    exit 0
fi

EXTRA_FLAGS=()
if [[ "$DRY_RUN" -eq 1 ]]; then
    EXTRA_FLAGS+=(--dry-run)
fi
if [[ "${#DISK_ARGS[@]}" -gt 0 ]]; then
    EXTRA_FLAGS+=("${DISK_ARGS[@]}")
fi

python3 "$REPO_ROOT/scripts/lib/vm_manager.py" --action "$ACTION" --arch "$ARCH" --snapshot-name "$SNAPSHOT" "${EXTRA_FLAGS[@]}"

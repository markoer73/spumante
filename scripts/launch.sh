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

usage() {
    cat <<EOF
Usage: ./scripts/launch.sh [--arch arm64|x86_64] [--snapshot <name>] [--disk <qcow2_path>] [--status] [--dry-run]

Starts or restores the headless QEMU Windows 10/11 Ground-Truth VM (macOS hvf/tcg first, Linux kvm second)
with VirtIO serial qemu-ga (/tmp/wine_parity_qga.sock) and QMP (/tmp/wine_parity_qmp.sock).
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

EXTRA_FLAGS=()
if [[ "$DRY_RUN" -eq 1 ]]; then
    EXTRA_FLAGS+=(--dry-run)
fi
if [[ "${#DISK_ARGS[@]}" -gt 0 ]]; then
    EXTRA_FLAGS+=("${DISK_ARGS[@]}")
fi

python3 "$REPO_ROOT/scripts/lib/vm_manager.py" --action "$ACTION" --arch "$ARCH" --snapshot-name "$SNAPSHOT" "${EXTRA_FLAGS[@]}"

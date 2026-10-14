#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Canonical stop and cleanup entrypoint for wine-parity.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ACTION="stop"
FLAGS=()

usage() {
    cat <<EOF
Usage: ./scripts/stop.sh [--vm] [--wine] [--all] [--status]

Cleanly stops the headless QEMU Windows VM daemon, removes guest agent sockets,
and cleans ephemeral /tmp/wine_test_* prefixes.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --vm)
            FLAGS+=(--vm)
            shift
            ;;
        --wine)
            FLAGS+=(--wine)
            shift
            ;;
        --all)
            FLAGS+=(--all)
            shift
            ;;
        --status)
            ACTION="status"
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

if [[ "${#FLAGS[@]}" -gt 0 ]]; then
    python3 "$REPO_ROOT/scripts/lib/vm_manager.py" --action "$ACTION" "${FLAGS[@]}"
else
    python3 "$REPO_ROOT/scripts/lib/vm_manager.py" --action "$ACTION"
fi

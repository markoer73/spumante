#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Canonical stop and cleanup entrypoint for Spumante.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ACTION="stop"
FLAGS=()
UNREGISTER_EXE_HANDLER=0
DRY_RUN=0

usage() {
    cat <<EOF
Usage: ./scripts/stop.sh [--vm] [--wine] [--all] [--status]
       ./scripts/stop.sh --unregister-exe-handler [--dry-run]

Cleanly stops the headless QEMU Windows VM daemon, removes guest agent sockets,
cleans ephemeral /tmp/wine_test_* prefixes, or unregisters the macOS .exe/.msi handler.
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
        --unregister-exe-handler)
            UNREGISTER_EXE_HANDLER=1
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

if [[ "$UNREGISTER_EXE_HANDLER" -eq 1 ]]; then
    EXE_ARGS=(--action unregister)
    if [[ "$DRY_RUN" -eq 1 ]]; then
        EXE_ARGS+=(--dry-run)
    fi
    python3 "$REPO_ROOT/scripts/lib/exe_handler.py" "${EXE_ARGS[@]}"
    exit 0
fi

if [[ "${#FLAGS[@]}" -gt 0 ]]; then
    python3 "$REPO_ROOT/scripts/lib/vm_manager.py" --action "$ACTION" "${FLAGS[@]}"
else
    python3 "$REPO_ROOT/scripts/lib/vm_manager.py" --action "$ACTION"
fi

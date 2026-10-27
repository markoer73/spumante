#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Headless QEMU Windows 10/11 VM orchestrator (macOS hvf/tcg first, Linux kvm second)."""

from __future__ import annotations

import argparse
import base64
import json
import os
import platform
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

DEFAULT_QGA_SOCK = Path("/tmp/spumante_qga.sock")
DEFAULT_QMP_SOCK = Path("/tmp/spumante_qmp.sock")
DEFAULT_PID_FILE = Path("/tmp/spumante_qemu.pid")
DEFAULT_OVERLAY_IMG = Path("/tmp/spumante_win11_overlay.qcow2")


def detect_host_accel(target_arch: str) -> tuple[str, str, list[str]]:
    """Select optimal QEMU binary, machine type, and hardware acceleration flags.

    Prioritizes macOS Apple Silicon / Intel (hvf -> tcg fallback) first, Linux (kvm -> tcg) second.
    """
    host_os = platform.system().lower()
    host_machine = platform.machine().lower()
    if host_machine in ("arm64", "aarch64"):
        norm_host_arch = "arm64"
    else:
        norm_host_arch = "x86_64"

    if target_arch == "arm64":
        qemu_bin = "qemu-system-aarch64"
        machine = "virt,highmem=on"
    else:
        qemu_bin = "qemu-system-x86_64"
        machine = "q35"

    if host_os == "darwin":
        if norm_host_arch == target_arch:
            return qemu_bin, machine, ["-accel", "hvf", "-cpu", "host"]
        return qemu_bin, machine, ["-accel", "tcg,thread=multi", "-cpu", "max"]

    if host_os == "linux" and norm_host_arch == target_arch and Path("/dev/kvm").exists():
        return qemu_bin, machine, ["-accel", "kvm", "-cpu", "host"]

    return qemu_bin, machine, ["-accel", "tcg,thread=multi", "-cpu", "max"]


def build_qemu_command(
    arch: str = "arm64",
    disk_image: Path | None = None,
    qga_socket: Path = DEFAULT_QGA_SOCK,
    qmp_socket: Path = DEFAULT_QMP_SOCK,
    pid_file: Path = DEFAULT_PID_FILE,
    snapshot_name: str | None = None,
    memory_mb: int = 4096,
    cpus: int = 4,
) -> list[str]:
    """Build the hermetic headless QEMU command line with strict network isolation."""
    qemu_bin, machine, accel_flags = detect_host_accel(arch)
    img_path = disk_image or DEFAULT_OVERLAY_IMG

    cmd = [
        qemu_bin,
        "-name",
        f"spumante-win11-{arch}",
        "-M",
        machine,
        *accel_flags,
        "-m",
        str(memory_mb),
        "-smp",
        str(cpus),
        "-display",
        "none",
        "-nic",
        "none",
        "-pidfile",
        str(pid_file),
        "-daemonize",
        "-drive",
        f"file={img_path},if=virtio,cache=writeback,format=qcow2",
        "-qmp",
        f"unix:{qmp_socket},server=on,wait=off",
        "-device",
        "virtio-serial-pci",
        "-chardev",
        f"socket,path={qga_socket},server=on,wait=off,id=qga0",
        "-device",
        "virtserialport,chardev=qga0,name=org.qemu.guest_agent.0",
    ]
    if snapshot_name:
        cmd.extend(["-loadvm", snapshot_name])
    return cmd


class QMPClient:
    """Minimal client for the QEMU Machine Protocol (QMP) Unix domain socket."""

    def __init__(self, socket_path: Path = DEFAULT_QMP_SOCK, timeout: float = 5.0) -> None:
        self.socket_path = socket_path
        self.timeout = timeout

    def execute(self, command: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        """Execute a QMP command over the Unix socket."""
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(self.timeout)
            sock.connect(str(self.socket_path))
            f_in = sock.makefile("r", encoding="utf-8")
            # Read QMP greeting
            _ = f_in.readline()
            sock.sendall(b'{"execute": "qmp_capabilities"}\n')
            _ = f_in.readline()

            payload: dict[str, Any] = {"execute": command}
            if arguments:
                payload["arguments"] = arguments
            sock.sendall((json.dumps(payload) + "\n").encode("utf-8"))
            resp_line = f_in.readline()
            return json.loads(resp_line) if resp_line else {}

    def load_snapshot(self, snapshot_name: str = "clean_ready") -> dict[str, Any]:
        """Revert the running VM to a warm snapshot in <1s via human-monitor-command."""
        return self.execute(
            "human-monitor-command",
            {"command-line": f"loadvm {snapshot_name}"},
        )


class QGAClient:
    """Client for the QEMU Guest Agent (qemu-ga) over VirtIO serial Unix socket."""

    def __init__(self, socket_path: Path = DEFAULT_QGA_SOCK, timeout: float = 15.0) -> None:
        self.socket_path = socket_path
        self.timeout = timeout

    def _rpc(self, execute: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        req: dict[str, Any] = {"execute": execute}
        if arguments:
            req["arguments"] = arguments
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(self.timeout)
            sock.connect(str(self.socket_path))
            sock.sendall((json.dumps(req) + "\n").encode("utf-8"))
            f_in = sock.makefile("r", encoding="utf-8")
            line = f_in.readline()
            return json.loads(line) if line else {}

    def ping(self) -> bool:
        """Verify guest agent responsiveness."""
        try:
            resp = self._rpc("guest-ping")
            return "return" in resp
        except OSError:
            return False

    def upload_file(self, host_path: Path, guest_path: str) -> None:
        """Transfer a compiled PE test executable into the guest via guest-file-write."""
        open_resp = self._rpc(
            "guest-file-open",
            {"path": guest_path, "mode": "wb"},
        )
        handle = open_resp["return"]
        try:
            data = host_path.read_bytes()
            b64_data = base64.b64encode(data).decode("ascii")
            self._rpc(
                "guest-file-write",
                {"handle": handle, "buf-b64": b64_data},
            )
        finally:
            self._rpc("guest-file-close", {"handle": handle})

    def exec_test(self, guest_exe_path: str, args: list[str] | None = None) -> dict[str, Any]:
        """Execute a test binary inside Windows VM and wait for completion."""
        exec_resp = self._rpc(
            "guest-exec",
            {
                "path": guest_exe_path,
                "arg": args or [],
                "capture-output": True,
            },
        )
        pid = exec_resp["return"]["pid"]
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            status_resp = self._rpc("guest-exec-status", {"pid": pid})
            ret = status_resp.get("return", {})
            if ret.get("exited"):
                out_b64 = ret.get("out-data", "")
                err_b64 = ret.get("err-data", "")
                stdout = base64.b64decode(out_b64).decode("utf-8", errors="replace") if out_b64 else ""
                stderr = base64.b64decode(err_b64).decode("utf-8", errors="replace") if err_b64 else ""
                return {
                    "exitcode": ret.get("exitcode", -1),
                    "stdout": stdout,
                    "stderr": stderr,
                }
            time.sleep(0.05)
        raise TimeoutError(f"Guest process PID {pid} timed out")


def vm_status() -> dict[str, Any]:
    """Inspect current VM daemon, sockets, and ephemeral Wine prefixes."""
    running = False
    pid = None
    if DEFAULT_PID_FILE.is_file():
        try:
            pid = int(DEFAULT_PID_FILE.read_text(encoding="utf-8").strip())
            os.kill(pid, 0)
            running = True
        except (ValueError, OSError):
            running = False

    ephemeral_prefixes = [str(p) for p in Path("/tmp").glob("wine_test_*")]
    return {
        "vm_running": running,
        "pid": pid,
        "qga_socket_exists": DEFAULT_QGA_SOCK.exists(),
        "qmp_socket_exists": DEFAULT_QMP_SOCK.exists(),
        "ephemeral_wine_prefixes": ephemeral_prefixes,
    }


def stop_environment(stop_vm: bool = True, stop_wine: bool = True) -> dict[str, Any]:
    """Cleanly stop the headless QEMU VM and clean up ephemeral Wine prefixes."""
    actions: list[str] = []
    if stop_vm:
        status = vm_status()
        if status["vm_running"] and status["pid"]:
            try:
                os.kill(status["pid"], 15)
                actions.append(f"Terminated QEMU PID {status['pid']}")
            except OSError:
                pass
        for sock_path in (DEFAULT_QGA_SOCK, DEFAULT_QMP_SOCK, DEFAULT_PID_FILE):
            if sock_path.exists():
                try:
                    sock_path.unlink()
                    actions.append(f"Removed {sock_path}")
                except OSError:
                    pass

    if stop_wine:
        for prefix in Path("/tmp").glob("wine_test_*"):
            shutil.rmtree(prefix, ignore_errors=True)
            actions.append(f"Cleaned ephemeral Wine prefix {prefix}")

    return {"stopped_vm": stop_vm, "stopped_wine": stop_wine, "actions": actions}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="QEMU Windows VM Manager")
    parser.add_argument("--action", choices=["launch", "stop", "status", "snapshot"], required=True)
    parser.add_argument("--arch", choices=["arm64", "x86_64"], default="arm64")
    parser.add_argument("--disk", type=Path, default=None)
    parser.add_argument("--snapshot-name", default="clean_ready")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--vm", action="store_true")
    parser.add_argument("--wine", action="store_true")
    parser.add_argument("--all", action="store_true")
    args = parser.parse_args(argv)

    if args.action == "status":
        print(json.dumps(vm_status(), indent=2))
        return 0

    if args.action == "stop":
        do_vm = args.vm or args.all or (not args.vm and not args.wine)
        do_wine = args.wine or args.all or (not args.vm and not args.wine)
        res = stop_environment(stop_vm=do_vm, stop_wine=do_wine)
        print(json.dumps(res, indent=2))
        return 0

    if args.action == "snapshot":
        if args.dry_run or not DEFAULT_QMP_SOCK.exists():
            print(
                json.dumps(
                    {
                        "action": "loadvm",
                        "snapshot": args.snapshot_name,
                        "status": "dry_run_or_offline_fallback",
                    },
                    indent=2,
                )
            )
            return 0
        qmp = QMPClient(DEFAULT_QMP_SOCK)
        resp = qmp.load_snapshot(args.snapshot_name)
        print(json.dumps(resp, indent=2))
        return 0

    if args.action == "launch":
        cmd = build_qemu_command(
            arch=args.arch,
            disk_image=args.disk,
            snapshot_name=args.snapshot_name if args.snapshot_name else None,
        )
        if args.dry_run:
            print(" ".join(cmd))
            return 0

        qemu_exe = shutil.which(cmd[0])
        img_target = args.disk or DEFAULT_OVERLAY_IMG
        if not qemu_exe or not img_target.exists():
            print(
                json.dumps(
                    {
                        "status": "fallback_cached_ground_truth",
                        "reason": (
                            f"QEMU binary ({cmd[0]}) or Windows VM image ({img_target}) not present; "
                            "differential runner will automatically use cached Ground-Truth fixtures."
                        ),
                        "planned_command": " ".join(cmd),
                    },
                    indent=2,
                )
            )
            return 0

        subprocess.run(cmd, check=True)
        print(json.dumps(vm_status(), indent=2))
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Persistent Wine session, launchd login agent, and dedicated ~/.spumante prefix tests.

Covers the launch-speed work: the menu bar tray keeps a persistent wineserver session alive, an optional
launchd LaunchAgent starts the tray at login, and every launch path uses the dedicated Spumante prefix
(~/.spumante) exported as WINEPREFIX. All tests are hermetic: HOME, SPUMANTE_CONFIG and the LaunchAgents
directory point into a temporary directory, and anything that would start Wine runs in --dry-run mode.
"""

import importlib.util
import json
import os
import plistlib
import socket
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT / "scripts" / "lib") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "scripts" / "lib"))
EXE_HANDLER = REPO_ROOT / "scripts" / "lib" / "exe_handler.py"


def _native_binary() -> Path:
    res = subprocess.run(
        ["python3", str(EXE_HANDLER), "--action", "ensure-native"],
        cwd=REPO_ROOT, capture_output=True, text=True, check=True,
    )
    return Path(json.loads(res.stdout)["app_binary"])


def _make_pe_bytes(machine: int) -> bytes:
    buf = bytearray(256)
    buf[0:2] = b"MZ"
    struct.pack_into("<I", buf, 0x3C, 0x40)
    buf[0x40:0x44] = b"PE\x00\x00"
    struct.pack_into("<H", buf, 0x44, machine)
    return bytes(buf)


@unittest.skipUnless(sys.platform == "darwin", "Spumante persistent session and launchd agent are macOS-only")
class TestPersistentSessionAndPrefix(unittest.TestCase):
    """Persistent session, login agent, and ~/.spumante prefix behavior of the native Spumante binary."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.app_bin = _native_binary()

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="spm_session_")
        self.tmp = Path(self._tmp.name).resolve()
        self.home = self.tmp / "home"
        self.home.mkdir()
        self.cfg = self.tmp / "config" / "config.json"
        self.agents_dir = self.tmp / "LaunchAgents"
        self.env = {k: v for k, v in os.environ.items() if not k.startswith("SPUMANTE_") and k != "WINEPREFIX"}
        self.env.update({
            "HOME": str(self.home),
            "SPUMANTE_CONFIG": str(self.cfg),
            "SPUMANTE_LAUNCH_AGENTS_DIR": str(self.agents_dir),
            "SPUMANTE_SKIP_LAUNCHCTL": "1",
            "SPUMANTE_NONINTERACTIVE_ROSETTA": "1",
        })
        self.expected_prefix = self.home / ".spumante"

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def run_app(self, *args: str, extra_env: dict | None = None) -> dict:
        env = dict(self.env)
        if extra_env:
            env.update(extra_env)
        res = subprocess.run([str(self.app_bin), *args], cwd=self.tmp, env=env, capture_output=True, text=True,
                             check=False, timeout=60)
        self.assertEqual(res.returncode, 0, f"{args} failed:\n{res.stdout}\n{res.stderr}")
        return json.loads(res.stdout)

    # ------------------------------------------------------------------ prefix

    def test_default_prefix_is_dot_spumante_and_legacy_default_is_migrated(self) -> None:
        doc = self.run_app("--config-status")
        self.assertEqual(doc["config"]["wine_environment"]["default_prefix"], "~/.spumante")
        self.assertEqual(Path(doc["cascade"]["prefix_dir"]), self.expected_prefix)

        # A config written by an older release still carries the legacy default: it must move to ~/.spumante
        legacy = json.loads(self.cfg.read_text(encoding="utf-8"))
        legacy["wine_environment"]["default_prefix"] = "~/.spumante-prefix"
        self.cfg.write_text(json.dumps(legacy), encoding="utf-8")
        doc = self.run_app("--config-status")
        self.assertEqual(doc["config"]["wine_environment"]["default_prefix"], "~/.spumante")
        saved = json.loads(self.cfg.read_text(encoding="utf-8"))
        self.assertEqual(saved["wine_environment"]["default_prefix"], "~/.spumante")

        # A user-chosen custom prefix is kept as-is
        legacy["wine_environment"]["default_prefix"] = "~/my-windows"
        self.cfg.write_text(json.dumps(legacy), encoding="utf-8")
        doc = self.run_app("--config-status")
        self.assertEqual(doc["config"]["wine_environment"]["default_prefix"], "~/my-windows")

    def test_every_launch_path_exports_the_spumante_prefix(self) -> None:
        # A pre-existing ~/.wine prefix must no longer be picked up as a fallback
        (self.home / ".wine").mkdir()
        (self.home / ".wine" / "system.reg").write_text("WINE REGISTRY Version 2\n", encoding="utf-8")

        exe = self.tmp / "apps" / "tool.exe"
        exe.parent.mkdir()
        exe.write_bytes(_make_pe_bytes(0x8664))
        exe.chmod(0o755)
        trace = self.run_app(str(exe), extra_env={"SPUMANTE_DRY_RUN_JSON": "1"})
        self.assertEqual(Path(trace["wineprefix"]), self.expected_prefix)

        builtin = self.run_app("--launch-builtin", "notepad.exe", "--dry-run")
        self.assertEqual(Path(builtin["wineprefix"]), self.expected_prefix)

        sync = self.run_app("--sync-drive-c")
        self.assertEqual(Path(sync["wineprefix"]), self.expected_prefix)

        # An explicit WINEPREFIX still wins everywhere
        custom = self.tmp / "custom-prefix"
        trace = self.run_app(str(exe), extra_env={"SPUMANTE_DRY_RUN_JSON": "1", "WINEPREFIX": str(custom)})
        self.assertEqual(Path(trace["wineprefix"]), custom)

    def test_runtime_launcher_defaults_wineprefix_to_dot_spumante(self) -> None:
        launcher = REPO_ROOT / "builds" / "wine-install-x86" / "bin" / "spumante"
        if not launcher.is_file():
            self.skipTest("x86 runtime launcher not built")
        text = launcher.read_text(encoding="utf-8")
        self.assertIn('export WINEPREFIX="${WINEPREFIX:-$HOME/.spumante}"', text)

    # ----------------------------------------------------------------- session

    def test_session_status_start_and_stop(self) -> None:
        status = self.run_app("--session-status")
        self.assertEqual(status["action"], "session-status")
        self.assertEqual(Path(status["wineprefix"]), self.expected_prefix)
        self.assertFalse(status["running"])

        start = self.run_app("--session-start", "--dry-run")
        self.assertEqual(start["action"], "session-start")
        self.assertTrue(start["dry_run"])
        self.assertEqual(Path(start["wineprefix"]), self.expected_prefix)
        cmds = start["commands"]
        self.assertEqual(len(cmds), 2)
        self.assertTrue(cmds[0][0].endswith("/bin/wineserver"), cmds[0])
        self.assertEqual(cmds[0][1:], ["-p"])
        self.assertTrue(cmds[1][0].endswith("/bin/spumante"), cmds[1])
        self.assertEqual(cmds[1][1:], ["wineboot"])

        stop = self.run_app("--session-stop", "--dry-run")
        self.assertEqual(stop["action"], "session-stop")
        self.assertTrue(stop["commands"][0][0].endswith("/bin/wineserver"))
        self.assertEqual(stop["commands"][0][1:], ["-k"])

    def test_session_status_detects_a_live_wineserver_socket(self) -> None:
        prefix = self.tmp / "live-prefix"
        prefix.mkdir()
        st = prefix.stat()
        server_dir = Path(f"/tmp/.wine-{os.getuid()}") / f"server-{st.st_dev:x}-{st.st_ino:x}"
        server_dir.mkdir(parents=True, exist_ok=True)
        os.chmod(server_dir.parent, 0o700)
        os.chmod(server_dir, 0o700)
        sock_path = server_dir / "socket"
        env = {"WINEPREFIX": str(prefix)}
        try:
            # Stale socket file without a listener: not running
            stale = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            stale.bind(str(sock_path))
            stale.close()
            status = self.run_app("--session-status", extra_env=env)
            self.assertEqual(status["server_socket"], str(sock_path))
            self.assertFalse(status["running"])
            sock_path.unlink()

            listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            listener.bind(str(sock_path))
            listener.listen(4)
            try:
                status = self.run_app("--session-status", extra_env=env)
                self.assertTrue(status["running"])
                start = self.run_app("--session-start", "--dry-run", extra_env=env)
                self.assertEqual(start["status"], "already_running")
            finally:
                listener.close()
        finally:
            if sock_path.exists():
                sock_path.unlink()
            server_dir.rmdir()

    def test_cascade_defers_registry_writes_while_the_session_owns_user_reg(self) -> None:
        prefix = self.tmp / "busy-prefix"
        (prefix / "dosdevices").mkdir(parents=True)
        os.symlink("/", prefix / "dosdevices" / "z:")
        user_reg = prefix / "user.reg"
        user_reg.write_text("WINE REGISTRY Version 2\n", encoding="utf-8")
        st = prefix.stat()
        server_dir = Path(f"/tmp/.wine-{os.getuid()}") / f"server-{st.st_dev:x}-{st.st_ino:x}"
        server_dir.mkdir(parents=True, exist_ok=True)
        os.chmod(server_dir, 0o700)
        sock_path = server_dir / "socket"
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(str(sock_path))
        listener.listen(4)
        try:
            doc = self.run_app("--config-status", extra_env={"WINEPREFIX": str(prefix)})
            self.assertTrue(doc["cascade"]["registry_deferred_session_running"])
            # wineserver owns user.reg while running: the file must not be rewritten underneath it
            self.assertEqual(user_reg.read_text(encoding="utf-8"), "WINE REGISTRY Version 2\n")
            # Drive mappings are live on disk and are still enforced
            self.assertTrue(doc["cascade"]["z_drive_stripped"])
            self.assertFalse(os.path.lexists(prefix / "dosdevices" / "z:"))
        finally:
            listener.close()
            sock_path.unlink()
            server_dir.rmdir()

    def test_prefix_init_plan(self) -> None:
        doc = self.run_app("--init-prefix", "--dry-run")
        self.assertEqual(doc["action"], "init-prefix")
        self.assertEqual(Path(doc["wineprefix"]), self.expected_prefix)
        cmds = doc["commands"]
        self.assertTrue(cmds[0][0].endswith("/bin/spumante"))
        self.assertEqual(cmds[0][1:], ["wineboot", "--init"])
        self.assertTrue(cmds[1][0].endswith("/bin/wineserver"))
        self.assertEqual(cmds[1][1:], ["-w"])

    def test_tray_mode_starts_the_session(self) -> None:
        doc = self.run_app("--tray", "--dry-run")
        self.assertEqual(doc["action"], "tray")
        self.assertTrue(doc["starts_session"])
        self.assertTrue(doc["stops_session_on_quit"])

    # ------------------------------------------------------------- login agent

    def test_login_agent_install_status_and_uninstall(self) -> None:
        plist_path = self.agents_dir / "org.spumante.tray.plist"
        status = self.run_app("--login-agent-status")
        self.assertFalse(status["installed"])
        self.assertEqual(Path(status["plist_path"]), plist_path)

        inst = self.run_app("--install-login-agent")
        self.assertTrue(inst["installed"])
        self.assertTrue(plist_path.is_file())
        with plist_path.open("rb") as fh:
            plist = plistlib.load(fh)
        self.assertEqual(plist["Label"], "org.spumante.tray")
        self.assertEqual(plist["ProgramArguments"], [str(self.app_bin), "--tray"])
        self.assertTrue(plist["RunAtLoad"])
        self.assertFalse(plist["KeepAlive"])
        self.assertEqual(plist["LimitLoadToSessionType"], "Aqua")
        self.assertEqual(plist["ProcessType"], "Interactive")
        self.assertTrue(self.run_app("--login-agent-status")["installed"])

        ui = self.run_app("--config-ui", "--dry-run")
        self.assertTrue(ui["login_agent"]["installed"])
        self.assertIn("running", ui["session"])

        self.run_app("--uninstall-login-agent")
        self.assertFalse(plist_path.exists())
        self.assertFalse(self.run_app("--login-agent-status")["installed"])
        self.assertFalse(self.run_app("--config-ui", "--dry-run")["login_agent"]["installed"])

    # --------------------------------------------------------------- installer

    def _installer(self) -> Path:
        spec = importlib.util.spec_from_file_location("dmg_packager", REPO_ROOT / "scripts" / "lib" / "dmg_packager.py")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        script_text = mod._generate_installer_command_script()
        self.assertNotIn("python", script_text.lower(), "Install Spumante.command must not depend on python")
        vol = self.tmp / "volume"
        (vol / "Spumante.app").mkdir(parents=True)
        (vol / "Spumante Config.app").mkdir(parents=True)
        installer = vol / "Install Spumante.command"
        installer.write_text(script_text, encoding="utf-8")
        installer.chmod(0o755)
        return installer

    def test_installer_offers_login_agent_and_initializes_the_prefix_once(self) -> None:
        installer = self._installer()
        res = subprocess.run([str(installer), "--dry-run"], cwd=self.tmp, env=self.env, capture_output=True,
                             text=True, check=False, stdin=subprocess.DEVNULL)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        plan = json.loads(res.stdout)
        self.assertEqual(Path(plan["wineprefix"]), self.expected_prefix)
        self.assertTrue(plan["prefix_init"])
        self.assertTrue(plan["login_agent_offered"])
        self.assertEqual(Path(plan["login_agent_plist"]), self.agents_dir / "org.spumante.tray.plist")

        res = subprocess.run([str(installer), "--dry-run", "--no-login-agent", "--skip-prefix-init"], cwd=self.tmp,
                             env=self.env, capture_output=True, text=True, check=False, stdin=subprocess.DEVNULL)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        plan = json.loads(res.stdout)
        self.assertFalse(plan["prefix_init"])
        self.assertFalse(plan["login_agent_offered"])

        text = installer.read_text(encoding="utf-8")
        self.assertIn("--install-login-agent", text)
        self.assertIn("--init-prefix", text)
        self.assertIn("SPUMANTE_LOGIN_AGENT_CONSENT", text)


if __name__ == "__main__":
    unittest.main()

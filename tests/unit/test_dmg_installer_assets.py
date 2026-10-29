#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Installer-app bundle and prosecco icon assets for the macOS disk image.

Covers the Gatekeeper-safe `Install Spumante.app` bundle (downloaded .command scripts are blocked by
macOS, a notarized .app is not) and the `CFBundleIconFile` / `.VolumeIcon.icns` wiring. Everything here
is hermetic: the bundle is generated into a temporary directory and nothing is installed or launched.
"""

import os
import plistlib
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT / "scripts" / "lib") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "scripts" / "lib"))

import dmg_packager  # noqa: E402
import exe_handler  # noqa: E402

ASSET_DIR = REPO_ROOT / "packaging" / "assets"
ICNS_PATH = ASSET_DIR / "spumante.icns"
PNG_PATH = ASSET_DIR / "spumante-icon-1024.png"


@unittest.skipUnless(sys.platform == "darwin", "Spumante ships macOS app bundles and disk images only")
class TestDmgInstallerAssets(unittest.TestCase):
    """Prosecco icon assets, bundle icon wiring, and the Install Spumante.app installer."""

    def test_icon_assets_are_committed(self) -> None:
        self.assertTrue(PNG_PATH.is_file(), "missing packaging/assets/spumante-icon-1024.png")
        self.assertTrue(ICNS_PATH.is_file(), "missing packaging/assets/spumante.icns")
        self.assertGreater(ICNS_PATH.stat().st_size, 10_000)
        self.assertEqual(ICNS_PATH.read_bytes()[:4], b"icns")

    def test_both_app_info_plists_declare_the_spumante_icon(self) -> None:
        for plist_text in (
            exe_handler._generate_info_plist(),
            exe_handler._generate_config_app_info_plist(),
        ):
            doc = plistlib.loads(plist_text.encode("utf-8"))
            self.assertEqual(doc["CFBundleIconFile"], "spumante")

    def test_installer_app_bundle_is_valid_and_executable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="spm_installer_app_") as tmp:
            bundle = Path(tmp) / "Install Spumante.app"
            dmg_packager._write_installer_app(bundle, ICNS_PATH)

            info = plistlib.loads((bundle / "Contents" / "Info.plist").read_bytes())
            self.assertEqual(info["CFBundleIdentifier"], "org.spumante.installer")
            self.assertEqual(info["CFBundleExecutable"], "InstallSpumante")
            self.assertEqual(info["CFBundleIconFile"], "spumante")
            self.assertEqual(info["CFBundlePackageType"], "APPL")

            executable = bundle / "Contents" / "MacOS" / "InstallSpumante"
            self.assertTrue(executable.is_file())
            self.assertTrue(executable.stat().st_mode & 0o111)

            script = executable.read_text(encoding="utf-8")
            self.assertTrue(script.startswith("#!/bin/bash"))
            # The bundle must resolve the disk image root two levels above Contents/MacOS: the app
            # lives at <volume>/Install Spumante.app and the payload sits beside it.
            self.assertIn('BUNDLE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"', script)
            self.assertIn('VOLUME_DIR="$(dirname "$BUNDLE_DIR")"', script)
            self.assertIn('"$VOLUME_DIR/Install Spumante.command"', script)
            self.assertIn("install-spumante.sh", script)
            self.assertIn("SPUMANTE_SOURCE_DIR", script)
            self.assertIn("SPUMANTE_LOGIN_AGENT_CONSENT", script)
            self.assertIn("osascript", script)

            # Embedded payload so the app still installs if the neighbouring script cannot be run.
            embedded = bundle / "Contents" / "Resources" / "install-spumante.sh"
            self.assertTrue(embedded.is_file())
            self.assertTrue(embedded.stat().st_mode & 0o111)
            self.assertIn("Spumante.app", embedded.read_text(encoding="utf-8"))

            self.assertTrue((bundle / "Contents" / "Resources" / "spumante.icns").is_file())

    def test_installer_app_installs_from_a_simulated_disk_image(self) -> None:
        """The app must find the payload beside it and delegate a real install (no dialogs)."""
        with tempfile.TemporaryDirectory(prefix="spm_dmg_volume_") as tmp:
            volume = Path(tmp) / "Spumante"
            volume.mkdir()
            for app_name, exe in (("Spumante.app", "Spumante"), ("Spumante Config.app", "SpumanteConfig")):
                macos = volume / app_name / "Contents" / "MacOS"
                macos.mkdir(parents=True)
                stub = macos / exe
                stub.write_text("#!/bin/bash\nexit 0\n", encoding="utf-8")
                stub.chmod(0o755)

            command = volume / "Install Spumante.command"
            command.write_text(dmg_packager._generate_installer_command_script(), encoding="utf-8")
            command.chmod(0o755)
            dmg_packager._write_installer_app(volume / "Install Spumante.app", ICNS_PATH)

            apps_dir = Path(tmp) / "Applications"
            bin_dir = Path(tmp) / "bin"
            env = {
                **os.environ,
                "HOME": str(Path(tmp) / "home"),
                "SPUMANTE_CONFIG": str(Path(tmp) / "config" / "config.json"),
                "SPUMANTE_LAUNCH_AGENTS_DIR": str(Path(tmp) / "LaunchAgents"),
                "SPUMANTE_SKIP_LAUNCHCTL": "1",
                "SPUMANTE_INSTALLER_NO_UI": "1",
            }
            Path(env["HOME"]).mkdir()

            res = subprocess.run(
                [
                    str(volume / "Install Spumante.app" / "Contents" / "MacOS" / "InstallSpumante"),
                    "--target-dir", str(apps_dir),
                    "--bin-dir", str(bin_dir),
                    "--skip-prefix-init",
                    "--no-login-agent",
                    "--no-open-config",
                ],
                env=env,
                capture_output=True,
                text=True,
                check=False,
                timeout=120,
            )
            self.assertEqual(res.returncode, 0, f"stdout:\n{res.stdout}\nstderr:\n{res.stderr}")

            self.assertTrue((apps_dir / "Spumante.app").is_dir())
            self.assertTrue((apps_dir / "Spumante Config.app").is_dir())
            self.assertTrue((bin_dir / "spumante").is_file())
            self.assertTrue((bin_dir / "spumante-config").is_file())

    def test_installer_command_stays_available_for_terminal_use(self) -> None:
        script = dmg_packager._generate_installer_command_script()
        self.assertIn("--no-login-agent", script)
        self.assertIn("--skip-prefix-init", script)
        self.assertIn("SPUMANTE_LOGIN_AGENT_CONSENT", script)

    def test_dmg_layout_ships_installer_app_and_volume_icon(self) -> None:
        source = (REPO_ROOT / "scripts" / "lib" / "dmg_packager.py").read_text(encoding="utf-8")
        self.assertIn('"Install Spumante.app"', source)
        self.assertIn('.VolumeIcon.icns', source)
        self.assertIn("_set_dmg_volume_icon", source)


if __name__ == "__main__":
    unittest.main()

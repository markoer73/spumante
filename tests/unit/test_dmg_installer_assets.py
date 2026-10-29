#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Installer-app bundle and prosecco icon assets for the macOS disk image.

Covers the Gatekeeper-safe `Install Spumante.app` bundle (downloaded .command scripts are blocked by
macOS, a notarized .app is not) and the `CFBundleIconFile` / `.VolumeIcon.icns` wiring. Everything here
is hermetic: the bundle is generated into a temporary directory and nothing is installed or launched.
"""

import plistlib
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
            # Runs the shared installer logic instead of duplicating it, and reports through dialogs.
            self.assertIn('INSTALLER="$VOLUME_DIR/Install Spumante.command"', script)
            self.assertIn("--no-open-config", script)
            self.assertIn("SPUMANTE_LOGIN_AGENT_CONSENT", script)
            self.assertIn("osascript", script)

            self.assertTrue((bundle / "Contents" / "Resources" / "spumante.icns").is_file())

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

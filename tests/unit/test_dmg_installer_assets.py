#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Installer and Uninstaller native AppKit bundles and icon assets for the macOS disk image.

Covers the native compiled `Install Spumante.app` and `Uninstall Spumante.app` bundles, the
accompanying terminal .command wrappers, and the `CFBundleIconFile` / `.VolumeIcon.icns` wiring.
Everything here is hermetic: the bundles are generated into a temporary directory and tested in
isolated sandboxes.
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
    """Prosecco icon assets, bundle icon wiring, and native AppKit Installer & Uninstaller."""

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
            # Must be a compiled native Mach-O executable (or fallback script)
            header = executable.read_bytes()[:4]
            self.assertTrue(header in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe") or header.startswith(b"#!"))

            self.assertTrue((bundle / "Contents" / "Resources" / "spumante.icns").is_file())

    def test_uninstaller_app_bundle_is_valid_and_executable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="spm_uninstaller_app_") as tmp:
            bundle = Path(tmp) / "Uninstall Spumante.app"
            dmg_packager._write_uninstaller_app(bundle, ICNS_PATH)

            info = plistlib.loads((bundle / "Contents" / "Info.plist").read_bytes())
            self.assertEqual(info["CFBundleIdentifier"], "org.spumante.uninstaller")
            self.assertEqual(info["CFBundleExecutable"], "UninstallSpumante")
            self.assertEqual(info["CFBundleIconFile"], "spumante")
            self.assertEqual(info["CFBundlePackageType"], "APPL")

            executable = bundle / "Contents" / "MacOS" / "UninstallSpumante"
            self.assertTrue(executable.is_file())
            self.assertTrue(executable.stat().st_mode & 0o111)
            header = executable.read_bytes()[:4]
            self.assertTrue(header in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe") or header.startswith(b"#!"))

            self.assertTrue((bundle / "Contents" / "Resources" / "spumante.icns").is_file())

    def test_installer_app_installs_from_a_simulated_disk_image(self) -> None:
        """The native installer must locate source apps and execute unattended installation."""
        with tempfile.TemporaryDirectory(prefix="spm_dmg_volume_") as tmp:
            volume = Path(tmp) / "Spumante"
            volume.mkdir()
            for app_name, exe in (("Spumante.app", "Spumante"), ("Spumante Config.app", "SpumanteConfig")):
                macos = volume / app_name / "Contents" / "MacOS"
                macos.mkdir(parents=True)
                stub = macos / exe
                stub.write_text("#!/bin/bash\nexit 0\n", encoding="utf-8")
                stub.chmod(0o755)

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
                    "--unattended",
                    "--source-dir", str(volume),
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
            self.assertTrue((bin_dir / "spumante").is_file() or (bin_dir / "spumante").is_symlink())
            self.assertTrue((bin_dir / "spumante-config").is_file() or (bin_dir / "spumante-config").is_symlink())

    def test_uninstaller_app_uninstalls_cleanly(self) -> None:
        """The native uninstaller must remove apps, symlinks, and user settings."""
        with tempfile.TemporaryDirectory(prefix="spm_uninst_test_") as tmp:
            apps_dir = Path(tmp) / "Applications"
            bin_dir = Path(tmp) / "bin"
            apps_dir.mkdir()
            bin_dir.mkdir()

            (apps_dir / "Spumante.app").mkdir()
            (apps_dir / "Spumante Config.app").mkdir()
            (bin_dir / "spumante").write_text("#!/bin/sh\nexit 0\n")
            (bin_dir / "spumante-config").write_text("#!/bin/sh\nexit 0\n")

            uninstaller_bundle = Path(tmp) / "Uninstall Spumante.app"
            dmg_packager._write_uninstaller_app(uninstaller_bundle, ICNS_PATH)

            env = {
                **os.environ,
                "HOME": str(Path(tmp) / "home"),
                "SPUMANTE_INSTALLER_NO_UI": "1",
            }
            fake_prefix = Path(env["HOME"]) / ".spumante"
            fake_config = Path(env["HOME"]) / ".config" / "spumante"
            fake_prefix.mkdir(parents=True)
            fake_config.mkdir(parents=True)

            res = subprocess.run(
                [
                    str(uninstaller_bundle / "Contents" / "MacOS" / "UninstallSpumante"),
                    "--unattended",
                    "--target-dir", str(apps_dir),
                    "--bin-dir", str(bin_dir),
                    "--purge-data",
                ],
                env=env,
                capture_output=True,
                text=True,
                check=False,
                timeout=120,
            )
            self.assertEqual(res.returncode, 0, f"stdout:\n{res.stdout}\nstderr:\n{res.stderr}")

            self.assertFalse((apps_dir / "Spumante.app").exists())
            self.assertFalse((apps_dir / "Spumante Config.app").exists())
            self.assertFalse((bin_dir / "spumante").exists())
            self.assertFalse((bin_dir / "spumante-config").exists())
            self.assertFalse(fake_prefix.exists())
            self.assertFalse(fake_config.exists())

    def test_terminal_command_wrappers_stay_available(self) -> None:
        inst_script = dmg_packager._generate_installer_command_script()
        self.assertIn("--no-login-agent", inst_script)
        self.assertIn("--skip-prefix-init", inst_script)

        uninst_script = dmg_packager._generate_uninstaller_command_script()
        self.assertIn("UninstallSpumante", uninst_script)
        self.assertIn("--uninstall", uninst_script)

    def test_dmg_layout_ships_installer_uninstaller_and_volume_icon(self) -> None:
        source = (REPO_ROOT / "scripts" / "lib" / "dmg_packager.py").read_text(encoding="utf-8")
        self.assertIn('"Install Spumante.app"', source)
        self.assertIn('"Uninstall Spumante.app"', source)
        self.assertIn('"Uninstall Spumante.command"', source)
        self.assertIn('.VolumeIcon.icns', source)
        self.assertIn("_set_dmg_volume_icon", source)


if __name__ == "__main__":
    unittest.main()

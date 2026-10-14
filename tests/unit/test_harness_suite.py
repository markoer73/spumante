#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Unit and integration tests for the wine-parity harness, scripts, and bcrypt conformance slice."""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


class TestWineParityArchitecture(unittest.TestCase):
    """Verifies all phases of the wine-parity architecture and canonical scripts."""

    def test_01_canonical_scripts_and_anti_proliferation(self) -> None:
        """Only the 4 canonical shell scripts may exist directly in scripts/."""
        scripts_dir = REPO_ROOT / "scripts"
        self.assertTrue(scripts_dir.is_dir(), "scripts/ directory must exist")
        sh_files = sorted(p.name for p in scripts_dir.glob("*.sh"))
        self.assertEqual(
            sh_files,
            ["build.sh", "launch.sh", "stop.sh", "test.sh"],
            "scripts/ must contain strictly build.sh, launch.sh, stop.sh, and test.sh",
        )
        py_root_files = list(scripts_dir.glob("*.py"))
        self.assertEqual(
            py_root_files,
            [],
            "Python helpers must reside in scripts/lib/, not directly in scripts/",
        )
        for name in sh_files:
            script_path = scripts_dir / name
            self.assertTrue(
                os.access(script_path, os.X_OK),
                f"scripts/{name} must be executable",
            )

    def test_02_surgical_build_and_test_guards(self) -> None:
        """build.sh and test.sh must refuse unscoped full-tree runs unless --all is passed."""
        build_sh = REPO_ROOT / "scripts" / "build.sh"
        res_build = subprocess.run(
            [str(build_sh)],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(res_build.returncode, 0)
        self.assertIn("SURGICAL BUILD GUARD", res_build.stdout + res_build.stderr)

        test_sh = REPO_ROOT / "scripts" / "test.sh"
        res_test = subprocess.run(
            [str(test_sh)],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(res_test.returncode, 0)
        self.assertIn("SURGICAL TEST GUARD", res_test.stdout + res_test.stderr)

    def test_03_json_result_schema_and_field_comparator(self) -> None:
        """tests/schema/result.json must exist and comparator must support all 4 modes."""
        schema_path = REPO_ROOT / "tests" / "schema" / "result.json"
        self.assertTrue(schema_path.is_file(), "tests/schema/result.json must exist")
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
        self.assertEqual(schema.get("title"), "WineParityDifferentialResult")
        self.assertIn("assertions", schema.get("properties", {}))

        import sys
        if str(REPO_ROOT) not in sys.path:
            sys.path.insert(0, str(REPO_ROOT))
        from scripts.lib.comparator import compare_differential_results

        win_doc = {
            "dll": "bcrypt",
            "test_name": "test_symmetric_key",
            "architecture": "x86_64",
            "assertions": [
                {
                    "id": "aes128_valid",
                    "case": "AES128_ValidKey",
                    "api": "BCryptGenerateSymmetricKey",
                    "comparison_mode": "exact",
                    "status": "0x00000000",
                    "last_error": 0,
                    "out_length": 16,
                    "buffer_hex": "00112233445566778899aabbccddeeff",
                    "handle_valid": True,
                },
                {
                    "id": "aes128_handle",
                    "case": "AES128_HandleState",
                    "api": "BCryptGenerateSymmetricKey",
                    "comparison_mode": "handle_state",
                    "status": "0x00000000",
                    "last_error": 0,
                    "handle_valid": True,
                    "handle_alignment": 8,
                },
            ],
        }
        wine_doc_matching = json.loads(json.dumps(win_doc))
        diff = compare_differential_results(win_doc, wine_doc_matching)
        self.assertTrue(diff["matched"], f"Expected match, got discrepancies: {diff}")
        self.assertEqual(diff["discrepancy_count"], 0)

        wine_doc_failing = json.loads(json.dumps(win_doc))
        wine_doc_failing["assertions"][0]["status"] = "0xc0000002"  # STATUS_NOT_IMPLEMENTED
        diff_fail = compare_differential_results(win_doc, wine_doc_failing)
        self.assertFalse(diff_fail["matched"])
        self.assertEqual(diff_fail["discrepancy_count"], 1)

    def test_04_vm_orchestration_macos_first_and_qga(self) -> None:
        """launch.sh and stop.sh must configure macOS hvf/tcg, virtio-serial qemu-ga, and QMP."""
        launch_sh = REPO_ROOT / "scripts" / "launch.sh"
        res = subprocess.run(
            [str(launch_sh), "--arch", "arm64", "--dry-run"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        output = res.stdout
        self.assertIn("qemu-system-aarch64", output)
        self.assertIn("wine_parity_qga.sock", output)
        self.assertIn("wine_parity_qmp.sock", output)
        self.assertIn("-nic none", output)

        stop_sh = REPO_ROOT / "scripts" / "stop.sh"
        res_stop = subprocess.run(
            [str(stop_sh), "--status"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_stop.returncode, 0, res_stop.stderr)

    def test_05_stub_discovery_scanner_3_layers(self) -> None:
        """test.sh --discover-stubs must scan .spec, C FIXMEs, and rank macOS readiness."""
        test_sh = REPO_ROOT / "scripts" / "test.sh"
        res = subprocess.run(
            [str(test_sh), "--discover-stubs", "--dll", "bcrypt"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        report = json.loads(res.stdout)
        self.assertIn("stubs", report)
        self.assertGreaterEqual(len(report["stubs"]), 1)
        first = report["stubs"][0]
        self.assertIn("api_name", first)
        self.assertIn("wineserver_independent", first)
        self.assertIn("macos_framework", first)

    def test_06_clean_room_spec_packet_generation(self) -> None:
        """test.sh --fetch-spec must create an auditable Clean-Room Spec Packet in specs/."""
        test_sh = REPO_ROOT / "scripts" / "test.sh"
        res = subprocess.run(
            [str(test_sh), "--fetch-spec", "bcrypt:BCryptGenerateSymmetricKey"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        spec_file = REPO_ROOT / "specs" / "bcrypt" / "BCryptGenerateSymmetricKey.json"
        self.assertTrue(spec_file.is_file(), "Clean-room spec packet must be persisted")
        spec_data = json.loads(spec_file.read_text(encoding="utf-8"))
        self.assertEqual(spec_data["api_name"], "BCryptGenerateSymmetricKey")
        self.assertTrue(spec_data["clean_room_attestation"]["no_proprietary_decompilation"])

    def test_07_bcrypt_symmetric_key_red_and_green_differential_loop(self) -> None:
        """Build and run BCryptGenerateSymmetricKey conformance test (RED stub vs GREEN macOS implementation)."""
        build_sh = REPO_ROOT / "scripts" / "build.sh"
        test_sh = REPO_ROOT / "scripts" / "test.sh"

        # 1. Surgically build only bcrypt + test_symmetric_key
        res_build = subprocess.run(
            [str(build_sh), "--dll", "bcrypt", "--test", "test_symmetric_key"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_build.returncode, 0, res_build.stdout + res_build.stderr)

        # 2. Verify RED when running against unimplemented stub mode (--simulate-stub)
        res_red = subprocess.run(
            [
                str(test_sh),
                "--dll",
                "bcrypt",
                "--test",
                "test_symmetric_key",
                "--simulate-stub",
            ],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(
            res_red.returncode,
            0,
            "Differential test MUST fail in RED mode when BCryptGenerateSymmetricKey is a stub",
        )

        # 3. Verify GREEN when running with the macOS CommonCrypto-backed implementation
        res_green = subprocess.run(
            [str(test_sh), "--dll", "bcrypt", "--test", "test_symmetric_key"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_green.returncode,
            0,
            f"Differential test must pass 100% in GREEN mode:\n{res_green.stdout}\n{res_green.stderr}",
        )
        self.assertIn("MATCHED (0 discrepancies)", res_green.stdout)

    def test_08_lint_and_ci_workflow(self) -> None:
        """test.sh --lint and .github/workflows/ci.yml must pass all checks."""
        ci_path = REPO_ROOT / ".github" / "workflows" / "ci.yml"
        self.assertTrue(ci_path.is_file(), ".github/workflows/ci.yml must exist")
        ci_text = ci_path.read_text(encoding="utf-8")
        self.assertIn("macos-latest", ci_text)
        self.assertIn("ubuntu-latest", ci_text)
        self.assertIn("./scripts/test.sh", ci_text)

        test_sh = REPO_ROOT / "scripts" / "test.sh"
        res_lint = subprocess.run(
            [str(test_sh), "--lint"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_lint.returncode,
            0,
            f"Linter failed:\n{res_lint.stdout}\n{res_lint.stderr}",
        )

    def test_09_wine_borrow_skill_and_sparse_overlay_manifest(self) -> None:
        """Verify wine-borrow skill, .gitignore tree dedup, upstream/manifest.json, and --borrow / --check-upstream."""
        skill_md = REPO_ROOT / ".agents" / "skills" / "wine-borrow" / "SKILL.md"
        doc_md = REPO_ROOT / "docs" / "skills" / "WINE_BORROW.md"
        rule_md = REPO_ROOT / ".agents" / "rules" / "wine-borrow.md"
        manifest_path = REPO_ROOT / "upstream" / "manifest.json"
        gitignore_path = REPO_ROOT / ".gitignore"

        self.assertTrue(skill_md.is_file(), ".agents/skills/wine-borrow/SKILL.md must exist")
        self.assertTrue(doc_md.is_file(), "docs/skills/WINE_BORROW.md must exist")
        self.assertTrue(rule_md.is_file(), ".agents/rules/wine-borrow.md must exist")
        self.assertTrue(manifest_path.is_file(), "upstream/manifest.json must exist")
        self.assertTrue(gitignore_path.is_file(), ".gitignore must exist to prevent committing .wine-upstream/")

        gitignore_text = gitignore_path.read_text(encoding="utf-8")
        self.assertIn(".wine-upstream/", gitignore_text)
        self.assertIn("builds/", gitignore_text)

        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.assertIn("upstream_remote", manifest)
        self.assertIn("borrowed_dlls", manifest)
        self.assertIn("bcrypt", manifest["borrowed_dlls"])

        # Test ./scripts/build.sh --borrow bcrypt --dry-run
        build_sh = REPO_ROOT / "scripts" / "build.sh"
        res_borrow = subprocess.run(
            [str(build_sh), "--borrow", "bcrypt", "--dry-run"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_borrow.returncode, 0, res_borrow.stdout + res_borrow.stderr)
        borrow_plan = json.loads(res_borrow.stdout)
        self.assertEqual(borrow_plan["dll"], "bcrypt")
        self.assertEqual(borrow_plan["upstream_access_mode"], "read-only")
        self.assertIn("DISABLE_PUSH_READ_ONLY_UPSTREAM", " ".join(borrow_plan["sparse_commands"]))
        self.assertIn("sparse-checkout set include dlls/bcrypt", " ".join(borrow_plan["sparse_commands"]))
        self.assertEqual(borrow_plan["strategy"], "3-way-backport-preserving-macos-blocks")

        # Test ./scripts/test.sh --check-upstream --dll bcrypt
        test_sh = REPO_ROOT / "scripts" / "test.sh"
        res_check = subprocess.run(
            [str(test_sh), "--check-upstream", "--dll", "bcrypt"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_check.returncode, 0, res_check.stdout + res_check.stderr)
        check_report = json.loads(res_check.stdout)
        self.assertEqual(check_report["dll"], "bcrypt")
        self.assertTrue(check_report["manifest_verified"])

    def test_10_security_audit_skill_and_four_gate_scanner(self) -> None:
        """Verify security-audit skill, ./scripts/test.sh --security 4-gate scan (ASan+UBSan, secrets, SAST), and cron CI."""
        skill_md = REPO_ROOT / ".agents" / "skills" / "security-audit" / "SKILL.md"
        doc_md = REPO_ROOT / "docs" / "skills" / "SECURITY_AUDIT.md"
        rule_md = REPO_ROOT / ".agents" / "rules" / "security-audit.md"
        ci_path = REPO_ROOT / ".github" / "workflows" / "ci.yml"

        self.assertTrue(skill_md.is_file(), ".agents/skills/security-audit/SKILL.md must exist")
        self.assertTrue(doc_md.is_file(), "docs/skills/SECURITY_AUDIT.md must exist")
        self.assertTrue(rule_md.is_file(), ".agents/rules/security-audit.md must exist")

        for local_doc in (skill_md, doc_md, rule_md):
            txt = local_doc.read_text(encoding="utf-8")
            self.assertNotIn("http://", txt, f"{local_doc} must not point to floating external URLs")
            self.assertNotIn("https://", txt, f"{local_doc} must not point to floating external URLs")

        ci_text = ci_path.read_text(encoding="utf-8")
        self.assertIn("schedule:", ci_text)
        self.assertIn("cron:", ci_text)
        self.assertIn("./scripts/test.sh --security", ci_text)

        test_sh = REPO_ROOT / "scripts" / "test.sh"
        res_sec = subprocess.run(
            [str(test_sh), "--security", "--dll", "bcrypt", "--test", "test_symmetric_key"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_sec.returncode,
            0,
            f"Security scan failed:\n{res_sec.stdout}\n{res_sec.stderr}",
        )
        sec_report = json.loads(res_sec.stdout)
        self.assertTrue(sec_report["passed"])
        self.assertTrue(sec_report["gates"]["secret_and_clean_room_scan"]["passed"])
        self.assertTrue(sec_report["gates"]["static_sast_and_sandbox_scan"]["passed"])
        self.assertTrue(sec_report["gates"]["asan_ubsan_memory_safety"]["passed"])


if __name__ == "__main__":
    unittest.main()



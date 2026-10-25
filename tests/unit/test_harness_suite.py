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

    def test_11_bcrypt_expanded_macos_backends_differential_loop(self) -> None:
        """Verify expanded dlls/bcrypt primitives (AES-CBC/PKCS#7, RNG, SHA256/HMAC, PBKDF2, RSA/ECC BCRYPT_PAD_NONE, provider APIs)."""
        build_sh = REPO_ROOT / "scripts" / "build.sh"
        test_sh = REPO_ROOT / "scripts" / "test.sh"

        # 1. Surgically build bcrypt + test_crypto_primitives
        res_build = subprocess.run(
            [str(build_sh), "--dll", "bcrypt", "--test", "test_crypto_primitives"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_build.returncode, 0, res_build.stdout + res_build.stderr)

        # 2. Verify RED failure when running with --simulate-stub
        res_red = subprocess.run(
            [str(test_sh), "--dll", "bcrypt", "--test", "test_crypto_primitives", "--simulate-stub"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(res_red.returncode, 0)

        # 3. Verify GREEN match against Windows Ground Truth
        res_green = subprocess.run(
            [str(test_sh), "--dll", "bcrypt", "--test", "test_crypto_primitives"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_green.returncode,
            0,
            f"test_crypto_primitives failed:\n{res_green.stdout}\n{res_green.stderr}",
        )
        self.assertIn("MATCHED (0 discrepancies) [12 assertions]", res_green.stdout)

        # 4. Verify 4-gate security audit (including ASan+UBSan) on test_crypto_primitives
        res_sec = subprocess.run(
            [str(test_sh), "--security", "--dll", "bcrypt", "--test", "test_crypto_primitives"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_sec.returncode,
            0,
            f"Security scan failed on test_crypto_primitives:\n{res_sec.stdout}\n{res_sec.stderr}",
        )

    def test_12_winetest_and_ab_upstream_regression_harness(self) -> None:
        """Verify upstream winetest & A/B regression harness (--winetest) runs as follow-up to unit and security tests."""
        build_sh = REPO_ROOT / "scripts" / "build.sh"
        test_sh = REPO_ROOT / "scripts" / "test.sh"
        ci_path = REPO_ROOT / ".github" / "workflows" / "ci.yml"
        wine_test_h = REPO_ROOT / "tests" / "include" / "wine" / "test.h"
        winetest_c = REPO_ROOT / "tests" / "winetest" / "bcrypt" / "winetest_bcrypt.c"

        self.assertTrue(wine_test_h.is_file(), "tests/include/wine/test.h compatibility header must exist")
        self.assertTrue(winetest_c.is_file(), "tests/winetest/bcrypt/winetest_bcrypt.c must exist")

        # 1. Surgically build winetest target for bcrypt
        res_build = subprocess.run(
            [str(build_sh), "--dll", "bcrypt", "--winetest"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_build.returncode, 0, res_build.stdout + res_build.stderr)

        # 2. Run full bcrypt winetest & A/B upstream regression harness
        res_wt = subprocess.run(
            [str(test_sh), "--winetest", "--dll", "bcrypt"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_wt.returncode,
            0,
            f"winetest --dll bcrypt failed:\n{res_wt.stdout}\n{res_wt.stderr}",
        )
        wt_report = json.loads(res_wt.stdout)
        self.assertTrue(wt_report["passed"])
        self.assertEqual(wt_report["dll"], "bcrypt")
        self.assertEqual(wt_report["failures"], 0)
        self.assertEqual(wt_report["regressions_vs_upstream"], 0)
        self.assertGreaterEqual(wt_report["total_assertions"], 80)
        self.assertIn("ab_comparison", wt_report)
        self.assertTrue(wt_report["ab_comparison"]["zero_regressions"])
        expected_subtests = {
            "test_BCryptGenRandom",
            "test_rng",
            "test_hashes",
            "test_BcryptHash",
            "test_BcryptDeriveKeyPBKDF2",
            "test_aes",
            "test_BCryptGenerateSymmetricKey",
            "test_BCryptEncrypt",
            "test_BCryptDecrypt",
            "test_key_import_export",
            "test_RSA",
            "test_ECDSA",
            "test_providers",
        }
        self.assertTrue(expected_subtests.issubset(set(wt_report["subtests_executed"])))

        # 3. Verify surgical subtest filter (--filter)
        res_filter = subprocess.run(
            [str(test_sh), "--winetest", "--dll", "bcrypt", "--filter", "test_BCryptGenRandom"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_filter.returncode, 0, res_filter.stdout + res_filter.stderr)
        filter_report = json.loads(res_filter.stdout)
        self.assertEqual(filter_report["subtests_executed"], ["test_BCryptGenRandom"])

        # 4. Verify CI workflow runs winetest as follow-up after unit and security tests
        ci_text = ci_path.read_text(encoding="utf-8")
        self.assertIn("./scripts/test.sh --winetest --dll bcrypt", ci_text)

    def test_13_ntdll_apple_silicon_intel_execution_parity(self) -> None:
        """Verify dlls/ntdll Apple Silicon (arm64) execution parity for Intel (x86_64/i386) apps (virtual.c, signal_arm64.c, signal_x86_64.c)."""
        build_sh = REPO_ROOT / "scripts" / "build.sh"
        test_sh = REPO_ROOT / "scripts" / "test.sh"

        # 1. Verify upstream/manifest.json alignment for ntdll
        res_check = subprocess.run(
            [str(test_sh), "--check-upstream", "--dll", "ntdll"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_check.returncode, 0, res_check.stdout + res_check.stderr)
        check_report = json.loads(res_check.stdout)
        self.assertEqual(check_report["dll"], "ntdll")
        self.assertTrue(check_report["manifest_verified"])
        self.assertTrue(check_report["macos_blocks_present"])

        # 2. Surgically build ntdll + test_apple_silicon_parity
        res_build = subprocess.run(
            [str(build_sh), "--dll", "ntdll", "--test", "test_apple_silicon_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_build.returncode, 0, res_build.stdout + res_build.stderr)

        # 3. Verify RED failure when running with --simulate-stub
        res_red = subprocess.run(
            [str(test_sh), "--dll", "ntdll", "--test", "test_apple_silicon_parity", "--simulate-stub"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(res_red.returncode, 0)

        # 4. Verify GREEN match against Windows Ground Truth
        res_green = subprocess.run(
            [str(test_sh), "--dll", "ntdll", "--test", "test_apple_silicon_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_green.returncode,
            0,
            f"test_apple_silicon_parity failed:\n{res_green.stdout}\n{res_green.stderr}",
        )
        self.assertIn("MATCHED (0 discrepancies) [8 assertions]", res_green.stdout)

        # 5. Verify 4-gate security audit (including ASan+UBSan) on ntdll
        res_sec = subprocess.run(
            [str(test_sh), "--security", "--dll", "ntdll", "--test", "test_apple_silicon_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_sec.returncode,
            0,
            f"Security scan failed on ntdll:\n{res_sec.stdout}\n{res_sec.stderr}",
        )

        # 6. Verify mandatory follow-up upstream winetest & A/B anti-regression suite for ntdll
        res_wt = subprocess.run(
            [str(test_sh), "--winetest", "--dll", "ntdll"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_wt.returncode,
            0,
            f"winetest --dll ntdll failed:\n{res_wt.stdout}\n{res_wt.stderr}",
        )
        wt_report = json.loads(res_wt.stdout)
        self.assertTrue(wt_report["passed"])
        self.assertEqual(wt_report["dll"], "ntdll")
        self.assertEqual(wt_report["failures"], 0)
        self.assertEqual(wt_report["regressions_vs_upstream"], 0)
        self.assertGreaterEqual(wt_report["total_assertions"], 900)
        self.assertTrue(wt_report["ab_comparison"]["zero_regressions"])
        self.assertTrue(wt_report["ab_comparison"]["asan_ubsan_passed"])

    def test_14_ntdll_and_kernelbase_macos_ntsync_parity(self) -> None:
        """Verify macOS fast NT synchronization (NTSync parity) in dlls/ntdll/sync.c and dlls/kernelbase/sync.c."""
        build_sh = REPO_ROOT / "scripts" / "build.sh"
        test_sh = REPO_ROOT / "scripts" / "test.sh"

        # 1. Verify upstream/manifest.json alignment for kernelbase and ntdll
        for dll_name in ("ntdll", "kernelbase"):
            res_check = subprocess.run(
                [str(test_sh), "--check-upstream", "--dll", dll_name],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_check.returncode, 0, res_check.stdout + res_check.stderr)
            check_report = json.loads(res_check.stdout)
            self.assertEqual(check_report["dll"], dll_name)
            self.assertTrue(check_report["manifest_verified"])

        # 2. Surgically build and test ntdll:test_ntsync_macos_parity (RED stub vs GREEN macOS ulock/Mach)
        res_build_nt = subprocess.run(
            [str(build_sh), "--dll", "ntdll", "--test", "test_ntsync_macos_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_build_nt.returncode, 0, res_build_nt.stdout + res_build_nt.stderr)

        res_red_nt = subprocess.run(
            [str(test_sh), "--dll", "ntdll", "--test", "test_ntsync_macos_parity", "--simulate-stub"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(res_red_nt.returncode, 0)

        res_green_nt = subprocess.run(
            [str(test_sh), "--dll", "ntdll", "--test", "test_ntsync_macos_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_green_nt.returncode,
            0,
            f"test_ntsync_macos_parity failed:\n{res_green_nt.stdout}\n{res_green_nt.stderr}",
        )
        self.assertIn("MATCHED (0 discrepancies) [16 assertions]", res_green_nt.stdout)

        # 3. Surgically build and test kernelbase:test_wait_on_address (RED stub vs GREEN)
        res_build_kb = subprocess.run(
            [str(build_sh), "--dll", "kernelbase", "--test", "test_wait_on_address"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_build_kb.returncode, 0, res_build_kb.stdout + res_build_kb.stderr)

        res_red_kb = subprocess.run(
            [str(test_sh), "--dll", "kernelbase", "--test", "test_wait_on_address", "--simulate-stub"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(res_red_kb.returncode, 0)

        res_green_kb = subprocess.run(
            [str(test_sh), "--dll", "kernelbase", "--test", "test_wait_on_address"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_green_kb.returncode,
            0,
            f"test_wait_on_address failed:\n{res_green_kb.stdout}\n{res_green_kb.stderr}",
        )
        self.assertIn("MATCHED (0 discrepancies) [12 assertions]", res_green_kb.stdout)

        # 4. Verify 4-gate security audit (including ASan+UBSan) on both targets
        for dll_name, test_name in (("ntdll", "test_ntsync_macos_parity"), ("kernelbase", "test_wait_on_address")):
            res_sec = subprocess.run(
                [str(test_sh), "--security", "--dll", dll_name, "--test", test_name],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                res_sec.returncode,
                0,
                f"Security scan failed on {dll_name}:{test_name}:\n{res_sec.stdout}\n{res_sec.stderr}",
            )

        # 5. Verify mandatory follow-up upstream winetest & A/B anti-regression suite for kernelbase
        res_wt_kb = subprocess.run(
            [str(test_sh), "--winetest", "--dll", "kernelbase"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_wt_kb.returncode,
            0,
            f"winetest --dll kernelbase failed:\n{res_wt_kb.stdout}\n{res_wt_kb.stderr}",
        )
        wt_kb_report = json.loads(res_wt_kb.stdout)
        self.assertTrue(wt_kb_report["passed"])
        self.assertEqual(wt_kb_report["dll"], "kernelbase")
        self.assertEqual(wt_kb_report["failures"], 0)
        self.assertEqual(wt_kb_report["regressions_vs_upstream"], 0)
        self.assertTrue(wt_kb_report["ab_comparison"]["zero_regressions"])
        self.assertTrue(wt_kb_report["ab_comparison"]["asan_ubsan_passed"])

    def test_15_full_wine_x86_and_arm64_build_modes(self) -> None:
        """Verify ./scripts/build.sh --full-x86/--x86 and --full-arm64/--arm64 build --all parity targets plus upstream Wine installation."""
        build_sh = REPO_ROOT / "scripts" / "build.sh"

        # 1. Verify --full-x86 and --x86 dry-run plans
        for flag in ("--full-x86", "--x86"):
            res_dry_x86 = subprocess.run(
                [str(build_sh), flag, "--dry-run"],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_dry_x86.returncode, 0, res_dry_x86.stdout + res_dry_x86.stderr)
            plan_x86 = json.loads(res_dry_x86.stdout)
            self.assertEqual(plan_x86["target_mode"], "full-x86")
            self.assertEqual(plan_x86["arch"], "x86_64")
            self.assertTrue(plan_x86["implies_all_parity_builds"])
            self.assertEqual(plan_x86["upstream_access_mode"], "read-only")
            self.assertIn("--enable-archs=i386,x86_64", " ".join(plan_x86["configure_command"]))
            self.assertIn("DISABLE_PUSH_READ_ONLY_UPSTREAM", " ".join(plan_x86["upstream_prepare_commands"]))

        # 2. Verify --full-arm64 and --arm64 dry-run plans
        for flag in ("--full-arm64", "--arm64"):
            res_dry_arm = subprocess.run(
                [str(build_sh), flag, "--dry-run"],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_dry_arm.returncode, 0, res_dry_arm.stdout + res_dry_arm.stderr)
            plan_arm = json.loads(res_dry_arm.stdout)
            self.assertEqual(plan_arm["target_mode"], "full-arm64")
            self.assertEqual(plan_arm["arch"], "arm64")
            self.assertTrue(plan_arm["implies_all_parity_builds"])
            self.assertEqual(plan_arm["upstream_access_mode"], "read-only")
            self.assertIn("--enable-archs=arm64ec,i386", " ".join(plan_arm["configure_command"]))

        # 3. Execute --full-x86 and verify all parity DLLs + wine-install-x86 launcher are built
        res_exec_x86 = subprocess.run(
            [str(build_sh), "--full-x86", "--skip-monolithic-compile"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_exec_x86.returncode, 0, res_exec_x86.stdout + res_exec_x86.stderr)
        launcher_x86 = REPO_ROOT / "builds" / "wine-install-x86" / "bin" / "wine-parity"
        self.assertTrue(launcher_x86.is_file(), "builds/wine-install-x86/bin/wine-parity must be created")
        self.assertTrue(os.access(launcher_x86, os.X_OK), "wine-parity launcher must be executable")
        launcher_txt = launcher_x86.read_text(encoding="utf-8")
        self.assertIn("WINEDLLOVERRIDES", launcher_txt)
        self.assertIn("WINEDLLPATH", launcher_txt)
        for dll_name in ("bcrypt", "ntdll", "kernelbase", "winemac.drv"):
            self.assertIn(dll_name, launcher_txt)

        # 4. Verify incremental build intelligence: when already configured & built with matching stamp,
        # running execute_full_wine_build WITHOUT --skip-monolithic-compile detects up-to-date state
        # and avoids re-running configure or full make.
        from scripts.lib.wine_borrow import (
            compute_parity_dll_hashes,
            execute_full_wine_build,
            load_manifest,
            write_monolithic_build_stamp,
        )
        import tempfile

        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            fake_build_dir = tmp_path / "wine-full-x86"
            fake_install_dir = tmp_path / "wine-install-x86"
            (fake_build_dir).mkdir(parents=True, exist_ok=True)
            (fake_install_dir / "bin").mkdir(parents=True, exist_ok=True)
            (fake_build_dir / "Makefile").write_text("# Fake Wine Makefile\n", encoding="utf-8")
            (fake_build_dir / "config.status").write_text("# --enable-archs=i386,x86_64\n", encoding="utf-8")
            fake_wine64 = fake_install_dir / "bin" / "wine64"
            fake_wine64.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            fake_wine64.chmod(0o755)

            all_borrowed_dlls = list(load_manifest().get("borrowed_dlls", {}).keys())
            write_monolithic_build_stamp(
                build_dir=fake_build_dir,
                target_mode="full-x86",
                enable_archs="--enable-archs=i386,x86_64",
                upstream_head="a312fc87a5",
                dll_hashes=compute_parity_dll_hashes(all_borrowed_dlls),
            )

            inc_result = execute_full_wine_build(
                target="x86",
                dry_run=False,
                skip_monolithic_compile=False,
                build_dir_override=fake_build_dir,
                install_dir_override=fake_install_dir,
                upstream_head_override="a312fc87a5",
            )
            self.assertEqual(inc_result["status"], "up_to_date_skipped_rebuild")
            self.assertFalse(inc_result["configure_executed"])
            self.assertFalse(inc_result["make_executed"])
            self.assertEqual(inc_result["changed_dlls"], [])

    def test_16_winemac_drv_iosurface_swapchain_multimon_and_xproc_parity(self) -> None:
        """Verify dlls/winemac.drv IOSurface child CAMetalLayer swapchains, multi-monitor ChangeDisplaySettingsEx, and cross-process DCs/layered windows."""
        build_sh = REPO_ROOT / "scripts" / "build.sh"
        test_sh = REPO_ROOT / "scripts" / "test.sh"

        # 1. Verify upstream/manifest.json alignment for winemac.drv
        res_check = subprocess.run(
            [str(test_sh), "--check-upstream", "--dll", "winemac.drv"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_check.returncode, 0, res_check.stdout + res_check.stderr)
        check_report = json.loads(res_check.stdout)
        self.assertEqual(check_report["dll"], "winemac.drv")
        self.assertTrue(check_report["manifest_verified"])
        self.assertTrue(check_report["macos_blocks_present"])

        # 2. Surgically build winemac.drv + test_winemac_parity
        res_build = subprocess.run(
            [str(build_sh), "--dll", "winemac.drv", "--test", "test_winemac_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_build.returncode, 0, res_build.stdout + res_build.stderr)

        # 3. Verify RED failure when running with --simulate-stub
        res_red = subprocess.run(
            [str(test_sh), "--dll", "winemac.drv", "--test", "test_winemac_parity", "--simulate-stub"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(res_red.returncode, 0)

        # 4. Verify GREEN match against Windows Ground Truth (13 assertions)
        res_green = subprocess.run(
            [str(test_sh), "--dll", "winemac.drv", "--test", "test_winemac_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_green.returncode,
            0,
            f"test_winemac_parity failed:\n{res_green.stdout}\n{res_green.stderr}",
        )
        self.assertIn("MATCHED (0 discrepancies) [13 assertions]", res_green.stdout)

        # 5. Verify 4-gate security audit (including ASan+UBSan) on winemac.drv
        res_sec = subprocess.run(
            [str(test_sh), "--security", "--dll", "winemac.drv", "--test", "test_winemac_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_sec.returncode,
            0,
            f"Security scan failed on winemac.drv:\n{res_sec.stdout}\n{res_sec.stderr}",
        )

        # 6. Verify mandatory follow-up upstream winetest & A/B anti-regression suite for winemac.drv
        res_wt = subprocess.run(
            [str(test_sh), "--winetest", "--dll", "winemac.drv"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_wt.returncode,
            0,
            f"winetest --dll winemac.drv failed:\n{res_wt.stdout}\n{res_wt.stderr}",
        )
        wt_report = json.loads(res_wt.stdout)
        self.assertTrue(wt_report["passed"])
        self.assertEqual(wt_report["dll"], "winemac.drv")
        self.assertEqual(wt_report["failures"], 0)
        self.assertEqual(wt_report["regressions_vs_upstream"], 0)
        self.assertGreaterEqual(wt_report["total_assertions"], 50)
        self.assertTrue(wt_report["ab_comparison"]["zero_regressions"])
        self.assertTrue(wt_report["ab_comparison"]["asan_ubsan_passed"])

    def test_17_kernelbase_win32_utility_apis_darwin_mach_parity(self) -> None:
        """Verify dlls/kernelbase Win32 utility APIs (GetSystemTimePreciseAsFileTime, Set/GetThreadDescription, CompareObjectHandles, VirtualAlloc2, MapViewOfFile3)."""
        build_sh = REPO_ROOT / "scripts" / "build.sh"
        test_sh = REPO_ROOT / "scripts" / "test.sh"

        # 1. Verify clean-room spec packets exist for all 6 APIs
        for api in (
            "GetSystemTimePreciseAsFileTime",
            "SetThreadDescription",
            "GetThreadDescription",
            "CompareObjectHandles",
            "VirtualAlloc2",
            "MapViewOfFile3",
        ):
            spec_file = REPO_ROOT / "specs" / "kernelbase" / f"{api}.json"
            self.assertTrue(spec_file.is_file(), f"Missing clean-room spec packet: {spec_file}")

        # 2. Surgically build kernelbase + test_win32_utility_apis
        res_build = subprocess.run(
            [str(build_sh), "--dll", "kernelbase", "--test", "test_win32_utility_apis"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_build.returncode, 0, res_build.stdout + res_build.stderr)

        # 3. Verify RED failure when running with --simulate-stub
        res_red = subprocess.run(
            [str(test_sh), "--dll", "kernelbase", "--test", "test_win32_utility_apis", "--simulate-stub"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(res_red.returncode, 0)

        # 4. Verify GREEN match against Windows Ground Truth (12 assertions)
        res_green = subprocess.run(
            [str(test_sh), "--dll", "kernelbase", "--test", "test_win32_utility_apis"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_green.returncode,
            0,
            f"test_win32_utility_apis failed:\n{res_green.stdout}\n{res_green.stderr}",
        )
        self.assertIn("MATCHED (0 discrepancies) [12 assertions]", res_green.stdout)

        # 5. Verify 4-gate security audit (including ASan+UBSan) on test_win32_utility_apis
        res_sec = subprocess.run(
            [str(test_sh), "--security", "--dll", "kernelbase", "--test", "test_win32_utility_apis"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_sec.returncode,
            0,
            f"Security scan failed on test_win32_utility_apis:\n{res_sec.stdout}\n{res_sec.stderr}",
        )

        # 6. Verify expanded winetest_kernelbase suite (8 subtests, >= 115 assertions)
        res_wt = subprocess.run(
            [str(test_sh), "--winetest", "--dll", "kernelbase"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_wt.returncode,
            0,
            f"winetest --dll kernelbase failed:\n{res_wt.stdout}\n{res_wt.stderr}",
        )
        wt_report = json.loads(res_wt.stdout)
        self.assertTrue(wt_report["passed"])
        self.assertEqual(wt_report["failures"], 0)
        self.assertGreaterEqual(wt_report["total_assertions"], 115)
        self.assertIn("test_GetSystemTimePreciseAsFileTime", wt_report["subtests_executed"])
        self.assertIn("test_ThreadDescription", wt_report["subtests_executed"])
        self.assertIn("test_CompareObjectHandles", wt_report["subtests_executed"])
        self.assertIn("test_VirtualAlloc2_and_MapViewOfFile3", wt_report["subtests_executed"])

    def test_18_mfplat_core_and_videotoolbox_differential_loop(self) -> None:
        """Verify dlls/mfplat core Media Foundation APIs and macOS VideoToolbox/CoreMedia H.264/HEVC decode bridge."""
        build_sh = REPO_ROOT / "scripts" / "build.sh"
        test_sh = REPO_ROOT / "scripts" / "test.sh"

        # 1. Verify clean-room spec packets exist for all 8 mfplat APIs
        for api in (
            "MFStartup",
            "MFShutdown",
            "MFCreateMemoryBuffer",
            "MFCreateAttributes",
            "MFCreateMediaType",
            "MFCreateGuid",
            "GetD3DFormatFromMFSubtype",
            "MFVideoToolboxCreateDecoder",
        ):
            spec_file = REPO_ROOT / "specs" / "mfplat" / f"{api}.json"
            self.assertTrue(spec_file.is_file(), f"Missing clean-room spec packet: {spec_file}")

        # 2. Surgically build mfplat + test_mfplat_parity
        res_build = subprocess.run(
            [str(build_sh), "--dll", "mfplat", "--test", "test_mfplat_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_build.returncode, 0, res_build.stdout + res_build.stderr)

        # 3. Verify RED failure when running with --simulate-stub
        res_red = subprocess.run(
            [str(test_sh), "--dll", "mfplat", "--test", "test_mfplat_parity", "--simulate-stub"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(res_red.returncode, 0)

        # 4. Verify GREEN match against Windows Ground Truth (12 assertions)
        res_green = subprocess.run(
            [str(test_sh), "--dll", "mfplat", "--test", "test_mfplat_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_green.returncode,
            0,
            f"test_mfplat_parity failed:\n{res_green.stdout}\n{res_green.stderr}",
        )
        self.assertIn("MATCHED (0 discrepancies) [12 assertions]", res_green.stdout)

        # 5. Verify 4-gate security audit (including ASan+UBSan) on test_mfplat_parity
        res_sec = subprocess.run(
            [str(test_sh), "--security", "--dll", "mfplat", "--test", "test_mfplat_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_sec.returncode,
            0,
            f"Security scan failed on test_mfplat_parity:\n{res_sec.stdout}\n{res_sec.stderr}",
        )

        # 6. Verify winetest_mfplat suite (6 subtests, >= 90 assertions)
        res_wt = subprocess.run(
            [str(test_sh), "--winetest", "--dll", "mfplat"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_wt.returncode,
            0,
            f"winetest --dll mfplat failed:\n{res_wt.stdout}\n{res_wt.stderr}",
        )
        wt_report = json.loads(res_wt.stdout)
        self.assertTrue(wt_report["passed"])
        self.assertEqual(wt_report["failures"], 0)
        self.assertGreaterEqual(wt_report["total_assertions"], 90)
        self.assertIn("test_startup", wt_report["subtests_executed"])
        self.assertIn("test_memory_buffer", wt_report["subtests_executed"])
        self.assertIn("test_attributes", wt_report["subtests_executed"])
        self.assertIn("test_mediatype", wt_report["subtests_executed"])
        self.assertIn("test_guid_and_d3dformat", wt_report["subtests_executed"])
        self.assertIn("test_videotoolbox_hardware_decode", wt_report["subtests_executed"])

    def test_19_qcap_avfoundation_and_audio_record_parity(self) -> None:
        """Verify dlls/qcap macOS AVFoundation video capture, DirectShow CLSID_AudioRecord, and mfreadwrite SourceReader bridge."""
        build_sh = REPO_ROOT / "scripts" / "build.sh"
        test_sh = REPO_ROOT / "scripts" / "test.sh"

        # 1. Verify clean-room spec packets exist for qcap
        for api in (
            "DllGetClassObject",
            "qcap_avf_create",
            "qcap_avf_read_frame",
            "audio_record_create",
            "QcapCreateAVFoundationReader",
        ):
            spec_file = REPO_ROOT / "specs" / "qcap" / f"{api}.json"
            self.assertTrue(spec_file.is_file(), f"Missing clean-room spec packet: {spec_file}")

        # 2. Surgically build qcap + test_qcap_parity
        res_build = subprocess.run(
            [str(build_sh), "--dll", "qcap", "--test", "test_qcap_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_build.returncode, 0, res_build.stdout + res_build.stderr)

        # 3. Verify RED failure when running with --simulate-stub
        res_red = subprocess.run(
            [str(test_sh), "--dll", "qcap", "--test", "test_qcap_parity", "--simulate-stub"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(res_red.returncode, 0)

        # 4. Verify GREEN match against Windows Ground Truth (12 assertions)
        res_green = subprocess.run(
            [str(test_sh), "--dll", "qcap", "--test", "test_qcap_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_green.returncode,
            0,
            f"test_qcap_parity failed:\n{res_green.stdout}\n{res_green.stderr}",
        )
        self.assertIn("MATCHED (0 discrepancies) [12 assertions]", res_green.stdout)

        # 5. Verify 4-gate security audit (including ASan+UBSan) on test_qcap_parity
        res_sec = subprocess.run(
            [str(test_sh), "--security", "--dll", "qcap", "--test", "test_qcap_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_sec.returncode,
            0,
            f"Security scan failed on test_qcap_parity:\n{res_sec.stdout}\n{res_sec.stderr}",
        )

        # 6. Verify winetest_qcap suite (7 subtests, >= 100 assertions)
        res_wt = subprocess.run(
            [str(test_sh), "--winetest", "--dll", "qcap"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_wt.returncode,
            0,
            f"winetest --dll qcap failed:\n{res_wt.stdout}\n{res_wt.stderr}",
        )
        wt_report = json.loads(res_wt.stdout)
        self.assertTrue(wt_report["passed"])
        self.assertEqual(wt_report["failures"], 0)
        self.assertGreaterEqual(wt_report["total_assertions"], 100)
        self.assertIn("test_audio_record_interfaces", wt_report["subtests_executed"])
        self.assertIn("test_audio_record_aggregation", wt_report["subtests_executed"])
        self.assertIn("test_audio_record_property_bag", wt_report["subtests_executed"])
        self.assertIn("test_audio_record_media_types_and_stream_config", wt_report["subtests_executed"])
        self.assertIn("test_audio_record_capture_stream", wt_report["subtests_executed"])
        self.assertIn("test_video_capture_interfaces_and_caps", wt_report["subtests_executed"])
        self.assertIn("test_avfoundation_video_capture_and_mfreadwrite", wt_report["subtests_executed"])

    def test_20_d2d1_primitives_and_vkd3d_shader_msl_parity(self) -> None:
        """Verify dlls/d2d1 Direct2D missing primitives (arcs, layers, AA, gradients, true cubic Beziers) and vkd3d-shader MSL generator."""
        build_sh = REPO_ROOT / "scripts" / "build.sh"
        test_sh = REPO_ROOT / "scripts" / "test.sh"

        # 1. Verify clean-room spec packets exist for d2d1
        for api in (
            "D2D1CreateFactory",
            "d2d_path_geometry_add_arc",
            "d2d_path_geometry_add_bezier",
            "d2d_render_target_push_pop_layer",
            "d2d_gradient_brush_sample",
            "vkd3d_shader_compile_msl",
        ):
            spec_file = REPO_ROOT / "specs" / "d2d1" / f"{api}.json"
            self.assertTrue(spec_file.is_file(), f"Missing clean-room spec packet: {spec_file}")

        # 2. Surgically build d2d1 + test_d2d1_msl_parity
        res_build = subprocess.run(
            [str(build_sh), "--dll", "d2d1", "--test", "test_d2d1_msl_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_build.returncode, 0, res_build.stdout + res_build.stderr)

        # 3. Verify RED failure when running with --simulate-stub
        res_red = subprocess.run(
            [str(test_sh), "--dll", "d2d1", "--test", "test_d2d1_msl_parity", "--simulate-stub"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(res_red.returncode, 0)

        # 4. Verify GREEN match against Windows Ground Truth (12 assertions)
        res_green = subprocess.run(
            [str(test_sh), "--dll", "d2d1", "--test", "test_d2d1_msl_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_green.returncode,
            0,
            f"test_d2d1_msl_parity failed:\n{res_green.stdout}\n{res_green.stderr}",
        )
        self.assertIn("MATCHED (0 discrepancies) [12 assertions]", res_green.stdout)

        # 5. Verify 4-gate security audit (including ASan+UBSan) on test_d2d1_msl_parity
        res_sec = subprocess.run(
            [str(test_sh), "--security", "--dll", "d2d1", "--test", "test_d2d1_msl_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_sec.returncode,
            0,
            f"Security scan failed on test_d2d1_msl_parity:\n{res_sec.stdout}\n{res_sec.stderr}",
        )

        # 6. Verify winetest_d2d1 suite (6 subtests, >= 35 assertions)
        res_wt = subprocess.run(
            [str(test_sh), "--winetest", "--dll", "d2d1"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_wt.returncode,
            0,
            f"winetest --dll d2d1 failed:\n{res_wt.stdout}\n{res_wt.stderr}",
        )
        wt_report = json.loads(res_wt.stdout)
        self.assertTrue(wt_report["passed"])
        self.assertEqual(wt_report["failures"], 0)
        self.assertGreaterEqual(wt_report["total_assertions"], 35)
        self.assertIn("test_factory_and_matrix_exports", wt_report["subtests_executed"])
        self.assertIn("test_true_cubic_bezier", wt_report["subtests_executed"])
        self.assertIn("test_elliptical_arc", wt_report["subtests_executed"])
        self.assertIn("test_layers_and_antialiasing", wt_report["subtests_executed"])
        self.assertIn("test_gradient_brushes", wt_report["subtests_executed"])
        self.assertIn("test_vkd3d_shader_msl", wt_report["subtests_executed"])

    def test_21_ntdll_arm64x_loader_tsc_and_jit_cache_parity(self) -> None:
        """Verify ARM64EC/ARM64X loader edge cases (DVRT relocations, NtReadFile system32 PE view, TSC rescaling, and on-disk JIT cache) in dlls/ntdll."""
        build_sh = REPO_ROOT / "scripts" / "build.sh"
        test_sh = REPO_ROOT / "scripts" / "test.sh"

        # 1. Verify clean-room spec packets exist for the new ARM64EC/ARM64X ntdll APIs
        for api in (
            "NtdllArm64xApplyDynamicRelocations",
            "NtReadFileArm64xPeView",
            "NtdllTscScaleConfigure",
            "NtdllJitCacheLookupBlock",
        ):
            spec_file = REPO_ROOT / "specs" / "ntdll" / f"{api}.json"
            self.assertTrue(spec_file.is_file(), f"Missing clean-room spec packet: {spec_file}")

        # 2. Surgically build ntdll + test_arm64x_loader_parity
        res_build = subprocess.run(
            [str(build_sh), "--dll", "ntdll", "--test", "test_arm64x_loader_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_build.returncode, 0, res_build.stdout + res_build.stderr)

        # 3. Verify RED failure when running with --simulate-stub
        res_red = subprocess.run(
            [str(test_sh), "--dll", "ntdll", "--test", "test_arm64x_loader_parity", "--simulate-stub"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(res_red.returncode, 0)

        # 4. Verify GREEN match against Windows Ground Truth (12 assertions)
        res_green = subprocess.run(
            [str(test_sh), "--dll", "ntdll", "--test", "test_arm64x_loader_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_green.returncode,
            0,
            f"test_arm64x_loader_parity failed:\n{res_green.stdout}\n{res_green.stderr}",
        )
        self.assertIn("MATCHED (0 discrepancies) [12 assertions]", res_green.stdout)

        # 5. Verify 4-gate security audit (including ASan+UBSan) on test_arm64x_loader_parity
        res_sec = subprocess.run(
            [str(test_sh), "--security", "--dll", "ntdll", "--test", "test_arm64x_loader_parity"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_sec.returncode,
            0,
            f"Security scan failed on test_arm64x_loader_parity:\n{res_sec.stdout}\n{res_sec.stderr}",
        )

        # 6. Verify expanded winetest_ntdll suite (18 subtests, >= 1000 assertions)
        res_wt = subprocess.run(
            [str(test_sh), "--winetest", "--dll", "ntdll"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            res_wt.returncode,
            0,
            f"winetest --dll ntdll failed:\n{res_wt.stdout}\n{res_wt.stderr}",
        )
        wt_report = json.loads(res_wt.stdout)
        self.assertTrue(wt_report["passed"])
        self.assertEqual(wt_report["failures"], 0)
        self.assertGreaterEqual(wt_report["total_assertions"], 1000)
        self.assertIn("test_arm64x_dvrt_and_ntreadfile_system32_pe_view", wt_report["subtests_executed"])
        self.assertIn("test_tsc_registry_rescaling_and_qpc", wt_report["subtests_executed"])
        self.assertIn("test_arm64ec_jit_disk_cache", wt_report["subtests_executed"])

    def test_22_macos_exe_handler_and_plus_x_permission_guard(self) -> None:
        """Verify WineParity.app macOS .exe/.msi handler, PE arch auto-dispatch, LaunchServices CLI, and mandatory +x permission check."""
        import struct
        import sys
        import tempfile

        build_sh = REPO_ROOT / "scripts" / "build.sh"
        launch_sh = REPO_ROOT / "scripts" / "launch.sh"
        stop_sh = REPO_ROOT / "scripts" / "stop.sh"

        # 1. Build the macOS .exe/.msi handler surgically via ./scripts/build.sh --exe-handler
        res_build = subprocess.run(
            [str(build_sh), "--exe-handler"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_build.returncode, 0, res_build.stdout + res_build.stderr)
        build_report = json.loads(res_build.stdout)
        self.assertTrue(build_report["require_executable_bit"])

        app_bundle = REPO_ROOT / "builds" / "WineParity.app"
        info_plist = app_bundle / "Contents" / "Info.plist"
        app_bin = app_bundle / "Contents" / "MacOS" / "WineParity"
        shell_helper = REPO_ROOT / "builds" / "wine-parity-shell.sh"
        exec_dylib = REPO_ROOT / "builds" / "libwine_parity_exec.dylib"
        launcher_x86 = REPO_ROOT / "builds" / "wine-install-x86" / "bin" / "wine-parity"
        launcher_arm64 = REPO_ROOT / "builds" / "wine-install-arm64" / "bin" / "wine-parity"

        self.assertTrue(info_plist.is_file(), "WineParity.app/Contents/Info.plist must exist")
        self.assertTrue(app_bin.is_file(), "WineParity.app/Contents/MacOS/WineParity binary must exist")
        self.assertTrue(os.access(app_bin, os.X_OK), "WineParity binary must be executable")
        self.assertTrue(shell_helper.is_file(), "builds/wine-parity-shell.sh must exist")
        self.assertTrue(launcher_x86.is_file(), "x86 wine-parity launcher must exist")
        self.assertTrue(launcher_arm64.is_file(), "arm64 wine-parity launcher must exist")
        if sys.platform == "darwin":
            self.assertTrue(exec_dylib.is_file(), "builds/libwine_parity_exec.dylib must exist on macOS")

        plist_txt = info_plist.read_text(encoding="utf-8")
        self.assertIn("org.wine-parity.launcher", plist_txt)
        self.assertIn("com.microsoft.windows-executable", plist_txt)
        self.assertIn("com.microsoft.msi-installer", plist_txt)

        # Helper to synthesize minimal valid PE headers with given COFF Machine type
        def make_pe_bytes(machine: int) -> bytes:
            buf = bytearray(256)
            buf[0:2] = b"MZ"
            struct.pack_into("<I", buf, 0x3C, 0x40)
            buf[0x40:0x44] = b"PE\x00\x00"
            struct.pack_into("<H", buf, 0x44, machine)
            return bytes(buf)

        with tempfile.TemporaryDirectory(prefix="wine_exe_handler_") as tmpdir:
            tmp_path = Path(tmpdir)
            sub_dir = tmp_path / "game_folder"
            sub_dir.mkdir(parents=True, exist_ok=True)

            exe_x64 = sub_dir / "app_x64.exe"
            exe_arm64 = sub_dir / "app_arm64.exe"
            msi_pkg = sub_dir / "setup.msi"

            exe_x64.write_bytes(make_pe_bytes(0x8664))  # IMAGE_FILE_MACHINE_AMD64
            exe_arm64.write_bytes(make_pe_bytes(0xAA64))  # IMAGE_FILE_MACHINE_ARM64
            msi_pkg.write_bytes(b"\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1" + b"\x00" * 64)

            # Explicitly set 0644 (NOT executable, no +x bit)
            exe_x64.chmod(0o644)
            exe_arm64.chmod(0o644)
            msi_pkg.chmod(0o644)

            # 2. Verify bin/wine-parity REFUSES to launch .exe and .msi without +x (exit code 126)
            for target_file in (exe_x64, msi_pkg):
                res_no_x_script = subprocess.run(
                    [str(launcher_x86), str(target_file)],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(
                    res_no_x_script.returncode,
                    126,
                    f"bin/wine-parity must refuse non-executable {target_file.name} with exit 126:\n"
                    f"{res_no_x_script.stdout}\n{res_no_x_script.stderr}",
                )
                self.assertIn("+x", res_no_x_script.stderr)

            # 3. Verify WineParity.app binary REFUSES to launch .exe and .msi without +x (exit code 126)
            for target_file in (exe_x64, exe_arm64, msi_pkg):
                res_no_x_app = subprocess.run(
                    [str(app_bin), str(target_file)],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(
                    res_no_x_app.returncode,
                    126,
                    f"WineParity.app must refuse non-executable {target_file.name} with exit 126:\n"
                    f"{res_no_x_app.stdout}\n{res_no_x_app.stderr}",
                )
                self.assertIn("+x", res_no_x_app.stderr)

            # 4. Grant +x (0755) and verify PE architecture auto-detection, cwd switch, and MSI routing
            exe_x64.chmod(0o755)
            exe_arm64.chmod(0o755)
            msi_pkg.chmod(0o755)

            env_trace = os.environ.copy()
            env_trace["WINE_PARITY_DRY_RUN_JSON"] = "1"
            env_trace["WINE_PARITY_CONFIG"] = str(tmp_path / "test22_config.json")
            env_trace["WINEPREFIX"] = str(tmp_path / "test22_prefix")

            res_ok_x64 = subprocess.run(
                [str(app_bin), str(exe_x64), "--windowed"],
                cwd=REPO_ROOT,
                env=env_trace,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_ok_x64.returncode, 0, res_ok_x64.stdout + res_ok_x64.stderr)
            trace_x64 = json.loads(res_ok_x64.stdout)
            self.assertEqual(trace_x64["pe_arch"], "x86_64")
            self.assertTrue(trace_x64["executable_bit_verified"])
            self.assertIn("wine-install-x86/bin/wine-parity", trace_x64["selected_launcher"])
            self.assertEqual(Path(trace_x64["working_directory"]).resolve(), sub_dir.resolve())
            self.assertEqual(trace_x64["args"], ["--windowed"])

            res_ok_arm64 = subprocess.run(
                [str(app_bin), str(exe_arm64), "--arm-arg"],
                cwd=REPO_ROOT,
                env=env_trace,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_ok_arm64.returncode, 0, res_ok_arm64.stdout + res_ok_arm64.stderr)
            trace_arm64 = json.loads(res_ok_arm64.stdout)
            self.assertEqual(trace_arm64["pe_arch"], "arm64")
            self.assertTrue(trace_arm64["executable_bit_verified"])
            self.assertIn("wine-install-arm64/bin/wine-parity", trace_arm64["selected_launcher"])
            self.assertEqual(Path(trace_arm64["working_directory"]).resolve(), sub_dir.resolve())
            self.assertEqual(trace_arm64["args"], ["--arm-arg"])

            res_ok_msi = subprocess.run(
                [str(app_bin), str(msi_pkg), "/qn"],
                cwd=REPO_ROOT,
                env=env_trace,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_ok_msi.returncode, 0, res_ok_msi.stdout + res_ok_msi.stderr)
            trace_msi = json.loads(res_ok_msi.stdout)
            self.assertTrue(trace_msi["is_msi"])
            self.assertTrue(trace_msi["executable_bit_verified"])
            self.assertEqual(trace_msi["msiexec_command"][:2], ["msiexec", "/i"])

            # 5. Verify ./scripts/launch.sh --register-exe-handler, --exe-handler-status, and ./scripts/stop.sh --unregister-exe-handler
            res_reg = subprocess.run(
                [str(launch_sh), "--register-exe-handler", "--dry-run"],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_reg.returncode, 0, res_reg.stdout + res_reg.stderr)
            reg_info = json.loads(res_reg.stdout)
            self.assertEqual(reg_info["bundle_id"], "org.wine-parity.launcher")
            self.assertTrue(reg_info["require_executable_bit"])

            res_stat = subprocess.run(
                [str(launch_sh), "--exe-handler-status"],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_stat.returncode, 0, res_stat.stdout + res_stat.stderr)
            stat_info = json.loads(res_stat.stdout)
            self.assertEqual(stat_info["bundle_id"], "org.wine-parity.launcher")
            self.assertTrue(stat_info["require_executable_bit"])

            res_unreg = subprocess.run(
                [str(stop_sh), "--unregister-exe-handler", "--dry-run"],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_unreg.returncode, 0, res_unreg.stdout + res_unreg.stderr)
            unreg_info = json.loads(res_unreg.stdout)
            self.assertEqual(unreg_info["action"], "unregister-exe-handler")

            # 6. Verify wine-parity-shell.sh wrapper enforces +x before dispatching
            exe_no_x = sub_dir / "blocked.exe"
            exe_no_x.write_bytes(make_pe_bytes(0x8664))
            exe_no_x.chmod(0o644)
            res_shell_no_x = subprocess.run(
                ["bash", "-c", f'source "{shell_helper}" && _wine_parity_run_checked "{exe_no_x}"'],
                cwd=REPO_ROOT,
                env=env_trace,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_shell_no_x.returncode, 126)
            self.assertIn("+x", res_shell_no_x.stderr)

            res_shell_ok = subprocess.run(
                ["bash", "-c", f'source "{shell_helper}" && _wine_parity_run_checked "{exe_x64}" --from-shell'],
                cwd=REPO_ROOT,
                env=env_trace,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_shell_ok.returncode, 0, res_shell_ok.stdout + res_shell_ok.stderr)
            shell_trace = json.loads(res_shell_ok.stdout)
            self.assertEqual(shell_trace["pe_arch"], "x86_64")
            self.assertEqual(shell_trace["args"], ["--from-shell"])

            # 7. On Darwin, verify libwine_parity_exec.dylib interposer enforces +x and dispatches +x PE binaries
            if sys.platform == "darwin":
                helper_c = tmp_path / "spawn_probe.c"
                helper_bin = tmp_path / "spawn_probe.bin"
                helper_c.write_text(
                    '#include <errno.h>\n'
                    '#include <spawn.h>\n'
                    '#include <stdio.h>\n'
                    '#include <sys/wait.h>\n'
                    '#include <unistd.h>\n'
                    'extern char **environ;\n'
                    'int main(int argc, char **argv) {\n'
                    '    if (argc < 2) return 2;\n'
                    '    pid_t pid = 0;\n'
                    '    int rc = posix_spawn(&pid, argv[1], NULL, NULL, &argv[1], environ);\n'
                    '    if (rc != 0) {\n'
                    '        fprintf(stderr, "posix_spawn_err=%d\\n", rc);\n'
                    '        return rc == EACCES ? 126 : 1;\n'
                    '    }\n'
                    '    int status = 0;\n'
                    '    waitpid(pid, &status, 0);\n'
                    '    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;\n'
                    '}\n',
                    encoding="utf-8",
                )
                subprocess.run(["clang", "-O1", str(helper_c), "-o", str(helper_bin)], check=True)

                env_dylib = env_trace.copy()
                env_dylib["DYLD_INSERT_LIBRARIES"] = str(exec_dylib)

                # Without +x -> EACCES (126)
                res_dylib_no_x = subprocess.run(
                    [str(helper_bin), str(exe_no_x)],
                    cwd=REPO_ROOT,
                    env=env_dylib,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(res_dylib_no_x.returncode, 126, res_dylib_no_x.stdout + res_dylib_no_x.stderr)

                # With +x -> transparently intercepted and routed through WineParity
                res_dylib_ok = subprocess.run(
                    [str(helper_bin), str(exe_arm64), "--via-dylib"],
                    cwd=REPO_ROOT,
                    env=env_dylib,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(res_dylib_ok.returncode, 0, res_dylib_ok.stdout + res_dylib_ok.stderr)
                dylib_trace = json.loads(res_dylib_ok.stdout)
                self.assertEqual(dylib_trace["pe_arch"], "arm64")
                self.assertEqual(dylib_trace["args"], ["--via-dylib"])

    def test_23_pre_execution_pe_security_scanner_three_gates_and_virustotal(self) -> None:
        """Verify 3-gate pre-execution PE security scanner (Gate 1: Quarantine, Gate 2: Authenticode, Gate 3: Static PE + VirusTotal)."""
        import hashlib
        import http.server
        import struct
        import sys
        import tempfile
        import threading

        build_sh = REPO_ROOT / "scripts" / "build.sh"
        test_sh = REPO_ROOT / "scripts" / "test.sh"
        app_bin = REPO_ROOT / "builds" / "WineParity.app" / "Contents" / "MacOS" / "WineParity"

        subprocess.run([str(build_sh), "--exe-handler"], cwd=REPO_ROOT, check=True, capture_output=True)

        def build_synthetic_pe(
            machine: int = 0x8664,
            dll_chars: int = 0x4160,
            sec_chars: int = 0x60000020,
            payload: bytes = b"\x90" * 128,
            sign_authenticode: bool = False,
            tamper_after_sign: bool = False,
        ) -> bytes:
            buf = bytearray(1024)
            buf[0:2] = b"MZ"
            e_lfanew = 0x40
            struct.pack_into("<I", buf, 0x3C, e_lfanew)
            buf[e_lfanew : e_lfanew + 4] = b"PE\x00\x00"
            struct.pack_into("<H", buf, e_lfanew + 4, machine)
            struct.pack_into("<H", buf, e_lfanew + 6, 1)  # NumberOfSections = 1
            opt_size = 240
            struct.pack_into("<H", buf, e_lfanew + 20, opt_size)
            struct.pack_into("<H", buf, e_lfanew + 22, 0x0022)  #EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE

            opt_off = e_lfanew + 24
            struct.pack_into("<H", buf, opt_off + 0, 0x020B)  # PE32+ (64-bit)
            struct.pack_into("<H", buf, opt_off + 70, dll_chars)  # DllCharacteristics
            struct.pack_into("<I", buf, opt_off + 108, 16)  # NumberOfRvaAndSizes = 16

            sec_off = opt_off + opt_size
            buf[sec_off : sec_off + 8] = b".text\x00\x00\x00"
            raw_ptr = 512
            raw_size = 512
            struct.pack_into("<I", buf, sec_off + 8, len(payload))
            struct.pack_into("<I", buf, sec_off + 12, 0x1000)
            struct.pack_into("<I", buf, sec_off + 16, raw_size)
            struct.pack_into("<I", buf, sec_off + 20, raw_ptr)
            struct.pack_into("<I", buf, sec_off + 36, sec_chars)

            buf[raw_ptr : raw_ptr + len(payload)] = payload

            if sign_authenticode:
                cert_offset = len(buf)
                # Temporarily set DataDirectory[4] (Certificate Table at opt_off + 144) to compute Authenticode digest
                # Wait: Authenticode hashes the file excluding CheckSum (opt_off + 64, 4 bytes) and DataDirectory[4] (opt_off + 144, 8 bytes)
                chk_off = opt_off + 64
                dd4_off = opt_off + 144
                h = hashlib.sha256()
                h.update(buf[:chk_off])
                h.update(buf[chk_off + 4 : dd4_off])
                h.update(buf[dd4_off + 8 : cert_offset])
                auth_digest = h.digest()

                # Construct DER PKCS#7 SignedData (OID 1.2.840.113549.1.7.2) containing SpcIndirectDataContent OID + auth_digest
                pkcs7_body = (
                    b"\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x07\x02"  # OID signedData
                    + b"\x06\x0a\x2b\x06\x01\x04\x01\x82\x37\x02\x01\x04"  # OID SpcIndirectDataContent
                    + b"\x04\x20"
                    + auth_digest
                )
                pkcs7_der = b"\x30" + bytes([len(pkcs7_body)]) + pkcs7_body
                win_cert_len = 8 + len(pkcs7_der)
                win_cert = struct.pack("<IHH", win_cert_len, 0x0200, 0x0002) + pkcs7_der
                # Pad to 8-byte boundary
                while len(win_cert) % 8 != 0:
                    win_cert += b"\x00"
                struct.pack_into("<II", buf, dd4_off, cert_offset, len(win_cert))
                buf.extend(win_cert)

                if tamper_after_sign:
                    buf[raw_ptr] ^= 0xFF

            return bytes(buf)

        with tempfile.TemporaryDirectory(prefix="wine_pe_scan_") as tmpdir:
            tmp_path = Path(tmpdir)
            env_trace = os.environ.copy()
            env_trace["WINE_PARITY_DRY_RUN_JSON"] = "1"
            env_trace["WINE_PARITY_CONFIG"] = str(tmp_path / "test23_config.json")
            env_trace["WINEPREFIX"] = str(tmp_path / "test23_prefix")

            # 1. Clean PE binary passes all 3 gates
            clean_exe = tmp_path / "clean_app.exe"
            clean_exe.write_bytes(build_synthetic_pe())
            clean_exe.chmod(0o755)

            res_clean = subprocess.run(
                [str(test_sh), "--scan-exe", str(clean_exe)],
                cwd=REPO_ROOT,
                env=env_trace,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_clean.returncode, 0, res_clean.stdout + res_clean.stderr)
            rep_clean = json.loads(res_clean.stdout)
            self.assertTrue(rep_clean["passed"])
            self.assertTrue(rep_clean["gates"]["gate1_quarantine_provenance"]["passed"])
            self.assertTrue(rep_clean["gates"]["gate2_authenticode_signature"]["passed"])
            self.assertTrue(rep_clean["gates"]["gate3_static_pe_and_virustotal"]["passed"])
            self.assertTrue(rep_clean["gates"]["gate3_static_pe_and_virustotal"]["dll_characteristics"]["dynamic_base_aslr"])
            self.assertTrue(rep_clean["gates"]["gate3_static_pe_and_virustotal"]["dll_characteristics"]["nx_compat_dep"])
            self.assertTrue(rep_clean["gates"]["gate3_static_pe_and_virustotal"]["dll_characteristics"]["guard_cf"])

            # 2. Gate 1: macOS com.apple.quarantine xattr blocks execution
            quarantined_exe = tmp_path / "quarantined.exe"
            quarantined_exe.write_bytes(build_synthetic_pe())
            quarantined_exe.chmod(0o755)
            if sys.platform == "darwin":
                subprocess.run(
                    [
                        "xattr",
                        "-w",
                        "com.apple.quarantine",
                        "0083;671b9c00;Safari;12345678-1234-1234-1234-1234567890AB",
                        str(quarantined_exe),
                    ],
                    check=True,
                )
            else:
                (tmp_path / "quarantined.exe:Zone.Identifier").write_text("[ZoneTransfer]\nZoneId=3\n", encoding="utf-8")

            res_quar = subprocess.run(
                [str(test_sh), "--scan-exe", str(quarantined_exe)],
                cwd=REPO_ROOT,
                env=env_trace,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertNotEqual(res_quar.returncode, 0)
            rep_quar = json.loads(res_quar.stdout)
            self.assertFalse(rep_quar["passed"])
            self.assertFalse(rep_quar["gates"]["gate1_quarantine_provenance"]["passed"])
            self.assertTrue(rep_quar["gates"]["gate1_quarantine_provenance"]["quarantined"])

            res_app_quar = subprocess.run(
                [str(app_bin), str(quarantined_exe)],
                cwd=REPO_ROOT,
                env=env_trace,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_app_quar.returncode, 126)
            self.assertIn("quarantine", res_app_quar.stderr.lower())

            # 3. Gate 2: Authenticode valid signature vs tampered signature vs --require-signature
            signed_ok = tmp_path / "signed_ok.exe"
            signed_ok.write_bytes(build_synthetic_pe(sign_authenticode=True, tamper_after_sign=False))
            signed_ok.chmod(0o755)

            res_sig_ok = subprocess.run(
                [str(test_sh), "--scan-exe", str(signed_ok), "--require-signature"],
                cwd=REPO_ROOT,
                env=env_trace,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_sig_ok.returncode, 0, res_sig_ok.stdout + res_sig_ok.stderr)
            rep_sig_ok = json.loads(res_sig_ok.stdout)
            self.assertTrue(rep_sig_ok["passed"])
            self.assertTrue(rep_sig_ok["gates"]["gate2_authenticode_signature"]["signed"])
            self.assertTrue(rep_sig_ok["gates"]["gate2_authenticode_signature"]["signature_valid"])
            self.assertFalse(rep_sig_ok["gates"]["gate2_authenticode_signature"]["tampered"])

            signed_tampered = tmp_path / "signed_tampered.exe"
            signed_tampered.write_bytes(build_synthetic_pe(sign_authenticode=True, tamper_after_sign=True))
            signed_tampered.chmod(0o755)

            res_tampered = subprocess.run(
                [str(test_sh), "--scan-exe", str(signed_tampered)],
                cwd=REPO_ROOT,
                env=env_trace,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertNotEqual(res_tampered.returncode, 0)
            rep_tampered = json.loads(res_tampered.stdout)
            self.assertFalse(rep_tampered["passed"])
            self.assertFalse(rep_tampered["gates"]["gate2_authenticode_signature"]["passed"])
            self.assertTrue(rep_tampered["gates"]["gate2_authenticode_signature"]["tampered"])

            res_app_tampered = subprocess.run(
                [str(app_bin), str(signed_tampered)],
                cwd=REPO_ROOT,
                env=env_trace,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_app_tampered.returncode, 126)

            # 4. Gate 3: W^X section violation and suspicious IAT process injection blocking
            wx_exe = tmp_path / "wx_bad.exe"
            wx_exe.write_bytes(build_synthetic_pe(sec_chars=0xE0000020))  # WRITE | EXECUTE | READ
            wx_exe.chmod(0o755)

            res_wx = subprocess.run(
                [str(test_sh), "--scan-exe", str(wx_exe)],
                cwd=REPO_ROOT,
                env=env_trace,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertNotEqual(res_wx.returncode, 0)
            rep_wx = json.loads(res_wx.stdout)
            self.assertFalse(rep_wx["passed"])
            self.assertFalse(rep_wx["gates"]["gate3_static_pe_and_virustotal"]["passed"])
            self.assertIn(".text", rep_wx["gates"]["gate3_static_pe_and_virustotal"]["wx_sections"])

            inj_payload = b"KERNEL32.dll\x00VirtualAllocEx\x00WriteProcessMemory\x00CreateRemoteThread\x00"
            inj_exe = tmp_path / "injector.exe"
            inj_exe.write_bytes(build_synthetic_pe(payload=inj_payload))
            inj_exe.chmod(0o755)

            res_inj = subprocess.run(
                [str(test_sh), "--scan-exe", str(inj_exe)],
                cwd=REPO_ROOT,
                env=env_trace,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertNotEqual(res_inj.returncode, 0)
            rep_inj = json.loads(res_inj.stdout)
            self.assertFalse(rep_inj["passed"])
            self.assertFalse(rep_inj["gates"]["gate3_static_pe_and_virustotal"]["passed"])
            self.assertTrue(len(rep_inj["gates"]["gate3_static_pe_and_virustotal"]["suspicious_imports"]) > 0)

            # 5. Gate 3: VirusTotal v3 API lookup when user provides VT_API_KEY
            bad_vt_exe = tmp_path / "vt_flagged.exe"
            bad_vt_exe.write_bytes(build_synthetic_pe(payload=b"VT_TEST_SAMPLE_PAYLOAD"))
            bad_vt_exe.chmod(0o755)
            bad_sha256 = hashlib.sha256(bad_vt_exe.read_bytes()).hexdigest()

            class MockVTov3Handler(http.server.BaseHTTPRequestHandler):
                def do_GET(self) -> None:
                    api_key = self.headers.get("x-apikey", "")
                    if api_key != "vt-unit-test-key":
                        self.send_response(401)
                        self.end_headers()
                        return
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.end_headers()
                    if bad_sha256 in self.path:
                        stats = {"malicious": 12, "suspicious": 2, "harmless": 54, "undetected": 4}
                    else:
                        stats = {"malicious": 0, "suspicious": 0, "harmless": 70, "undetected": 2}
                    body = json.dumps({"data": {"attributes": {"last_analysis_stats": stats}}})
                    self.wfile.write(body.encode("utf-8"))

                def log_message(self, format: str, *args: object) -> None:
                    return

            httpd = http.server.HTTPServer(("127.0.0.1", 0), MockVTov3Handler)
            port = httpd.server_port
            srv_thread = threading.Thread(target=httpd.serve_forever, daemon=True)
            srv_thread.start()
            try:
                vt_env = env_trace.copy()
                vt_env["VT_API_KEY"] = "vt-unit-test-key"
                vt_env["VT_API_BASE_URL"] = f"http://127.0.0.1:{port}"

                # Clean binary passes VT check
                res_vt_clean = subprocess.run(
                    [str(test_sh), "--scan-exe", str(clean_exe)],
                    cwd=REPO_ROOT,
                    env=vt_env,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(res_vt_clean.returncode, 0, res_vt_clean.stdout + res_vt_clean.stderr)
                rep_vt_clean = json.loads(res_vt_clean.stdout)
                self.assertTrue(rep_vt_clean["passed"])
                self.assertTrue(rep_vt_clean["gates"]["gate3_static_pe_and_virustotal"]["virustotal"]["scanned"])
                self.assertEqual(rep_vt_clean["gates"]["gate3_static_pe_and_virustotal"]["virustotal"]["malicious"], 0)

                # Malicious binary flagged by VT is blocked by both --scan-exe and WineParity.app
                res_vt_bad = subprocess.run(
                    [str(test_sh), "--scan-exe", str(bad_vt_exe)],
                    cwd=REPO_ROOT,
                    env=vt_env,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertNotEqual(res_vt_bad.returncode, 0)
                rep_vt_bad = json.loads(res_vt_bad.stdout)
                self.assertFalse(rep_vt_bad["passed"])
                self.assertEqual(rep_vt_bad["gates"]["gate3_static_pe_and_virustotal"]["virustotal"]["malicious"], 12)

                res_app_vt_bad = subprocess.run(
                    [str(app_bin), str(bad_vt_exe)],
                    cwd=REPO_ROOT,
                    env=vt_env,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(res_app_vt_bad.returncode, 126)
                self.assertIn("virustotal", res_app_vt_bad.stderr.lower())
            finally:
                httpd.shutdown()
                httpd.server_close()

    def test_24_user_config_cascade_native_macos_settings_ui_and_zero_trust_sandbox(self) -> None:
        """Verify user-config-containment skill, config.json -> user.reg cascade, native macOS config UI, and default HTTP/HTTPS sandbox."""
        import struct
        import tempfile

        build_sh = REPO_ROOT / "scripts" / "build.sh"
        launch_sh = REPO_ROOT / "scripts" / "launch.sh"
        app_bin = REPO_ROOT / "builds" / "WineParity.app" / "Contents" / "MacOS" / "WineParity"

        # 1. Verify skill, rule, and documentation files exist and have zero external floating URLs
        skill_md = REPO_ROOT / ".agents" / "skills" / "user-config-containment" / "SKILL.md"
        rule_md = REPO_ROOT / ".agents" / "rules" / "user-config-containment.md"
        doc_md = REPO_ROOT / "docs" / "skills" / "USER_CONFIG_AND_CONTAINMENT.md"
        self.assertTrue(skill_md.is_file(), "Missing .agents/skills/user-config-containment/SKILL.md")
        self.assertTrue(rule_md.is_file(), "Missing .agents/rules/user-config-containment.md")
        self.assertTrue(doc_md.is_file(), "Missing docs/skills/USER_CONFIG_AND_CONTAINMENT.md")

        for fpath in (skill_md, rule_md, doc_md):
            txt = fpath.read_text(encoding="utf-8")
            self.assertNotIn("http://", txt)
            self.assertNotIn("https://", txt)

        with tempfile.TemporaryDirectory(prefix="wine_cfg_sandbox_") as tmpdir:
            tmp_path = Path(tmpdir)
            cfg_file = tmp_path / "config.json"
            prefix_dir = tmp_path / "wineprefix"
            dosdevices_dir = prefix_dir / "dosdevices"
            dosdevices_dir.mkdir(parents=True, exist_ok=True)

            # Seed an existing user.reg with a custom user setting that must NOT be clobbered,
            # plus a dangerous z: -> / symlink that must be stripped by default sandbox settings
            user_reg = prefix_dir / "user.reg"
            user_reg.write_text(
                "WINE REGISTRY Version 2\n\n"
                "[Software\\\\CustomUserApp] 1700000000\n"
                '"UserSetting"="preserve_me"\n',
                encoding="utf-8",
            )
            z_link = dosdevices_dir / "z:"
            z_link.symlink_to("/")

            env = os.environ.copy()
            env["WINE_PARITY_CONFIG"] = str(cfg_file)
            env["WINEPREFIX"] = str(prefix_dir)

            # 2. Verify default zero-trust config initialization via ./scripts/launch.sh --config-status
            res_status = subprocess.run(
                [str(launch_sh), "--config-status"],
                cwd=REPO_ROOT,
                env=env,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_status.returncode, 0, res_status.stdout + res_status.stderr)
            status_data = json.loads(res_status.stdout)
            cfg = status_data["config"]

            self.assertTrue(cfg_file.is_file(), "config.json must be created automatically")
            self.assertTrue(cfg["sandbox"]["enabled"], "Applications must run sandboxed by default")
            self.assertTrue(cfg["sandbox"]["strip_z_drive"], "Drive Z: must be stripped by default")
            self.assertTrue(cfg["sandbox"]["protect_sensitive_host_paths"])
            self.assertEqual(cfg["network"]["incoming"], "deny", "Incoming networking must be disabled by default")
            self.assertEqual(
                cfg["network"]["outgoing"],
                "http_https_only",
                "Outgoing networking must default to HTTP/HTTPS only",
            )
            self.assertEqual(cfg["network"]["allowed_outgoing_ports"], [80, 443])

            # Verify default Seatbelt profile enforces incoming deny + outgoing ports 80/443 only
            sb_default = status_data["seatbelt_profile_preview"]
            self.assertIn("(deny network-inbound)", sb_default)
            self.assertIn("(deny network-outbound)", sb_default)
            self.assertIn("*:80", sb_default)
            self.assertIn("*:443", sb_default)
            self.assertIn(".ssh", sb_default)

            # 3. Verify --set-config updates config.json AND cascades to $WINEPREFIX/user.reg & dosdevices/
            res_set = subprocess.run(
                [
                    str(launch_sh),
                    "--set-config",
                    "wine_environment.windows_version=win10",
                    "--set-config",
                    "wine_environment.retina_mode=true",
                    "--set-config",
                    "app_profiles. studio.exe .windows_version=win11",
                ],
                cwd=REPO_ROOT,
                env=env,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_set.returncode, 0, res_set.stdout + res_set.stderr)
            set_data = json.loads(res_set.stdout)
            self.assertEqual(set_data["config"]["wine_environment"]["windows_version"], "win10")
            self.assertFalse(z_link.exists(), "dosdevices/z: symlink must be unlinked during prefix cascade")

            reg_txt = user_reg.read_text(encoding="utf-8")
            self.assertIn('"UserSetting"="preserve_me"', reg_txt, "Existing user.reg keys must be preserved")
            self.assertIn("[Software\\\\Wine]", reg_txt)
            self.assertIn('"Version"="win10"', reg_txt)
            self.assertIn("[Software\\\\Wine\\\\Mac Driver]", reg_txt)
            self.assertIn('"RetinaMode"="y"', reg_txt)
            self.assertIn("[Software\\\\Wine\\\\AppDefaults\\\\studio.exe]", reg_txt)
            self.assertIn('"Version"="win11"', reg_txt)

            # 4. Verify dynamic incoming/outgoing network policy transitions in Seatbelt profile
            res_net_deny = subprocess.run(
                [
                    str(launch_sh),
                    "--set-config",
                    "network.incoming=allow",
                    "--set-config",
                    "network.outgoing=deny_all",
                ],
                cwd=REPO_ROOT,
                env=env,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_net_deny.returncode, 0, res_net_deny.stdout + res_net_deny.stderr)
            sb_custom = json.loads(res_net_deny.stdout)["seatbelt_profile_preview"]
            self.assertIn("(allow network-inbound)", sb_custom)
            self.assertIn("(deny network-outbound)", sb_custom)
            self.assertNotIn("*:80", sb_custom)
            self.assertNotIn("*:443", sb_custom)

            # Restore default incoming=deny and outgoing=http_https_only
            subprocess.run(
                [
                    str(launch_sh),
                    "--set-config",
                    "network.incoming=deny",
                    "--set-config",
                    "network.outgoing=http_https_only",
                ],
                cwd=REPO_ROOT,
                env=env,
                check=True,
                capture_output=True,
            )

            # 5. Configure VirusTotal v3 in config.json and verify native macOS AppKit --config-ui exposes VT controls & key input
            subprocess.run(
                [
                    str(launch_sh),
                    "--set-config",
                    "security.virustotal_enabled=true",
                    "--set-config",
                    "security.virustotal_api_key=vt_gui_test_key_0123456789abcdef",
                ],
                cwd=REPO_ROOT,
                env=env,
                check=True,
                capture_output=True,
            )

            subprocess.run([str(build_sh), "--exe-handler"], cwd=REPO_ROOT, check=True, capture_output=True)

            app_m_file = REPO_ROOT / "builds" / "exe_handler_src" / "wine_parity_app.m"
            if app_m_file.is_file():
                objc_src = app_m_file.read_text(encoding="utf-8")
                self.assertIn("virustotalEnableCheck", objc_src)
                self.assertIn("virustotalKeyField", objc_src)
                self.assertIn("NSSecureTextField", objc_src)
                self.assertIn("toggleVtKeyVisibility:", objc_src)
                self.assertIn("security.virustotal_enabled=", objc_src)
                self.assertIn("security.virustotal_api_key=", objc_src)

            res_ui = subprocess.run(
                [str(launch_sh), "--config-ui", "--dry-run"],
                cwd=REPO_ROOT,
                env=env,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_ui.returncode, 0, res_ui.stdout + res_ui.stderr)
            ui_info = json.loads(res_ui.stdout)
            self.assertEqual(ui_info["action"], "config-ui")
            self.assertEqual(ui_info["ui_style"], "macos-native-appkit-settings")
            self.assertIn("Sandbox & Network", ui_info["sections"])
            self.assertTrue(ui_info["cascades_to_wine_registry"])
            vt_ui = ui_info["virustotal_controls"]
            self.assertTrue(vt_ui["enabled_checkbox"])
            self.assertTrue(vt_ui["api_key_input_field"])
            self.assertTrue(vt_ui["show_hide_key_toggle"])
            self.assertTrue(vt_ui["virustotal_enabled"])
            self.assertTrue(vt_ui["virustotal_api_key_configured"])

            # Clear the dummy VT key before running the offline sandboxed .exe dispatch check
            subprocess.run(
                [
                    str(launch_sh),
                    "--set-config",
                    "security.virustotal_api_key=",
                ],
                cwd=REPO_ROOT,
                env=env,
                check=True,
                capture_output=True,
            )

            # Create a clean +x PE binary and verify WineParity dispatches it under the zero-trust sandbox
            pe_buf = bytearray(512)
            pe_buf[0:2] = b"MZ"
            struct.pack_into("<I", pe_buf, 0x3C, 0x40)
            pe_buf[0x40:0x44] = b"PE\x00\x00"
            struct.pack_into("<H", pe_buf, 0x44, 0x8664)
            sample_exe = tmp_path / "sandboxed_app.exe"
            sample_exe.write_bytes(bytes(pe_buf))
            sample_exe.chmod(0o755)

            env_run = env.copy()
            env_run["WINE_PARITY_DRY_RUN_JSON"] = "1"
            res_run = subprocess.run(
                [str(app_bin), str(sample_exe), "--test-sandbox"],
                cwd=REPO_ROOT,
                env=env_run,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(res_run.returncode, 0, res_run.stdout + res_run.stderr)
            run_trace = json.loads(res_run.stdout)
            self.assertTrue(run_trace["sandboxed"])
            self.assertTrue(run_trace["z_drive_stripped"])
            self.assertEqual(run_trace["network_incoming"], "deny")
            self.assertEqual(run_trace["network_outgoing"], "http_https_only")
            self.assertEqual(run_trace["allowed_outgoing_ports"], [80, 443])
            self.assertTrue(Path(run_trace["seatbelt_profile"]).is_file())


if __name__ == "__main__":
    unittest.main()




#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Field-level differential comparator for Windows VM Ground Truth vs. Wine outputs."""

from __future__ import annotations

import fnmatch
import json
from pathlib import Path
from typing import Any


def validate_result_document(doc: dict[str, Any]) -> list[str]:
    """Validate a differential result document against the structural rules of result.json."""
    errors: list[str] = []
    for req in ("dll", "test_name", "architecture", "assertions"):
        if req not in doc:
            errors.append(f"Missing top-level required field: '{req}'")
    if doc.get("architecture") not in ("x86_64", "arm64"):
        errors.append(f"Invalid architecture: {doc.get('architecture')}")
    assertions = doc.get("assertions")
    if not isinstance(assertions, list):
        errors.append("'assertions' must be a list")
        return errors

    valid_modes = {"exact", "handle_state", "masked", "roundtrip"}
    for idx, item in enumerate(assertions):
        if not isinstance(item, dict):
            errors.append(f"Assertion[{idx}] is not an object")
            continue
        for field in ("id", "case", "api", "comparison_mode", "status", "last_error"):
            if field not in item:
                errors.append(f"Assertion[{idx}] missing required field '{field}'")
        mode = item.get("comparison_mode")
        if mode not in valid_modes:
            errors.append(f"Assertion[{idx}] has invalid comparison_mode '{mode}'")
    return errors


def _compare_single_assertion(
    expected: dict[str, Any],
    actual: dict[str, Any],
) -> list[dict[str, Any]]:
    """Compare a single assertion record according to its comparison_mode."""
    mismatches: list[dict[str, Any]] = []
    mode = expected.get("comparison_mode", "exact")

    # Status and last_error are always compared across all modes
    for core_field in ("status", "last_error"):
        exp_val = expected.get(core_field)
        act_val = actual.get(core_field)
        if isinstance(exp_val, str) and isinstance(act_val, str):
            exp_norm = exp_val.lower()
            act_norm = act_val.lower()
        else:
            exp_norm = exp_val
            act_norm = act_val
        if exp_norm != act_norm:
            mismatches.append(
                {
                    "field": core_field,
                    "expected": exp_val,
                    "actual": act_val,
                }
            )

    if mode == "exact":
        for exact_field in ("out_length", "buffer_hex", "handle_valid", "roundtrip_ok"):
            if exact_field in expected:
                exp_v = expected[exact_field]
                act_v = actual.get(exact_field)
                if isinstance(exp_v, str) and isinstance(act_v, str):
                    matched = exp_v.lower() == act_v.lower()
                else:
                    matched = exp_v == act_v
                if not matched:
                    mismatches.append(
                        {
                            "field": exact_field,
                            "expected": exp_v,
                            "actual": act_v,
                        }
                    )
    elif mode == "handle_state":
        for h_field in ("handle_valid", "handle_alignment"):
            if h_field in expected and expected[h_field] != actual.get(h_field):
                mismatches.append(
                    {
                        "field": h_field,
                        "expected": expected[h_field],
                        "actual": actual.get(h_field),
                    }
                )
    elif mode == "masked":
        mask = int(expected.get("bitmask_mask", 0xFFFFFFFF))
        exp_masked = int(expected.get("bitmask_value", 0)) & mask
        act_masked = int(actual.get("bitmask_value", 0)) & mask
        if exp_masked != act_masked:
            mismatches.append(
                {
                    "field": "bitmask_value",
                    "expected": hex(exp_masked),
                    "actual": hex(act_masked),
                }
            )
    elif mode == "roundtrip":
        for rt_field in ("roundtrip_ok", "out_length", "buffer_hex"):
            if rt_field in expected and expected[rt_field] != actual.get(rt_field):
                mismatches.append(
                    {
                        "field": rt_field,
                        "expected": expected[rt_field],
                        "actual": actual.get(rt_field),
                    }
                )

    return mismatches


def compare_differential_results(
    windows_doc: dict[str, Any],
    wine_doc: dict[str, Any],
    case_filter: str | None = None,
) -> dict[str, Any]:
    """Compare Windows Ground Truth JSON against Wine execution JSON."""
    win_errors = validate_result_document(windows_doc)
    wine_errors = validate_result_document(wine_doc)
    if win_errors or wine_errors:
        return {
            "matched": False,
            "schema_errors": {"windows": win_errors, "wine": wine_errors},
            "total_assertions": 0,
            "discrepancy_count": len(win_errors) + len(wine_errors),
            "discrepancies": [],
        }

    win_assertions = windows_doc.get("assertions", [])
    wine_by_id = {item["id"]: item for item in wine_doc.get("assertions", [])}

    discrepancies: list[dict[str, Any]] = []
    compared_count = 0

    for exp in win_assertions:
        if case_filter and not (
            fnmatch.fnmatch(exp["case"], case_filter)
            or fnmatch.fnmatch(exp["id"], case_filter)
        ):
            continue
        compared_count += 1
        step_id = exp["id"]
        act = wine_by_id.get(step_id)
        if act is None:
            discrepancies.append(
                {
                    "id": step_id,
                    "case": exp["case"],
                    "api": exp["api"],
                    "reason": "Missing assertion in Wine output",
                    "mismatches": [],
                }
            )
            continue

        field_mismatches = _compare_single_assertion(exp, act)
        if field_mismatches:
            discrepancies.append(
                {
                    "id": step_id,
                    "case": exp["case"],
                    "api": exp["api"],
                    "comparison_mode": exp["comparison_mode"],
                    "mismatches": field_mismatches,
                }
            )

    return {
        "dll": windows_doc.get("dll"),
        "test_name": windows_doc.get("test_name"),
        "architecture": windows_doc.get("architecture"),
        "matched": len(discrepancies) == 0 and compared_count > 0,
        "total_assertions": compared_count,
        "discrepancy_count": len(discrepancies),
        "discrepancies": discrepancies,
    }


def load_and_compare(
    windows_json_path: Path,
    wine_json_path: Path,
    case_filter: str | None = None,
) -> dict[str, Any]:
    """Load two result JSON files from disk and compare them."""
    win_doc = json.loads(windows_json_path.read_text(encoding="utf-8"))
    wine_doc = json.loads(wine_json_path.read_text(encoding="utf-8"))
    return compare_differential_results(win_doc, wine_doc, case_filter=case_filter)

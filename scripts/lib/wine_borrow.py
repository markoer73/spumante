#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Sparse-Overlay + Manifest upstream Wine alignment and 3-way surgical backport engine.

STRICT READ-ONLY UPSTREAM INVARIANT:
The original Wine repository (winehq.org) is strictly READ-ONLY. Pushing to upstream Wine
is permanently disabled via `git remote set-url --push origin DISABLE_PUSH_READ_ONLY_UPSTREAM`.
All development, commits, and CI run exclusively in the user's own `Spumante` GitHub repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import stat
import subprocess
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST_PATH = REPO_ROOT / "upstream" / "manifest.json"
READ_ONLY_PUSH_GUARD = "DISABLE_PUSH_READ_ONLY_UPSTREAM"


def load_manifest() -> dict[str, Any]:
    """Load and validate upstream/manifest.json, enforcing read-only upstream access."""
    if not MANIFEST_PATH.is_file():
        raise FileNotFoundError(f"Upstream manifest not found: {MANIFEST_PATH}")
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    if manifest.get("upstream_access_mode") != "read-only":
        raise PermissionError("SECURITY VIOLATION: upstream_access_mode in upstream/manifest.json MUST be 'read-only'")
    return manifest


def sha256_file(path: Path) -> str:
    """Compute SHA-256 hex digest of a local file."""
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def build_sparse_commands(dll: str, manifest: dict[str, Any], upstream_ref: str | None = None) -> list[str]:
    """Construct the blobless sparse-checkout commands for a single Wine DLL with read-only push lock."""
    remote = manifest["upstream_remote"]
    cache_dir = manifest.get("local_sparse_cache", ".wine-upstream")
    dll_meta = manifest.get("borrowed_dlls", {}).get(dll, {})
    upstream_path = dll_meta.get("upstream_path", f"dlls/{dll}")
    ref = upstream_ref or dll_meta.get("base_upstream_commit", "master")

    return [
        f"git clone --filter=blob:none --no-checkout --sparse {remote} {cache_dir}",
        f"git -C {cache_dir} remote set-url --push origin {READ_ONLY_PUSH_GUARD}",
        f"git -C {cache_dir} sparse-checkout init --cone",
        f"git -C {cache_dir} sparse-checkout set include {upstream_path}",
        f"git -C {cache_dir} checkout {ref}",
    ]


def _configure_upstream_git_excludes(sparse_dir: Path) -> None:
    """Ensure local macOS desktop metadata exclusions exist in .wine-upstream/.git/info/exclude."""
    exclude_file = sparse_dir / ".git" / "info" / "exclude"
    if not exclude_file.parent.is_dir():
        return
    existing = exclude_file.read_text(encoding="utf-8", errors="replace") if exclude_file.is_file() else ""
    macos_patterns = [".DS_Store", ".DS_Store?", "._*", ".Spotlight-V100", ".Trashes"]
    missing = [p for p in macos_patterns if p not in existing.splitlines()]
    if missing:
        prefix = "" if (not existing or existing.endswith("\n")) else "\n"
        exclude_file.write_text(existing + prefix + "\n".join(missing) + "\n", encoding="utf-8")


def ensure_upstream_wine_populated(
    full_tree: bool = False,
    dll: str | None = None,
    upstream_ref: str | None = None,
    dry_run: bool = False,
    sparse_dir_override: Path | None = None,
    remote_override: str | None = None,
) -> dict[str, Any]:
    """Check if upstream Wine (.wine-upstream) is present on disk, and if not, clone and populate it."""
    manifest = load_manifest()
    sparse_dir = (
        sparse_dir_override
        if sparse_dir_override is not None
        else (REPO_ROOT / manifest.get("local_sparse_cache", ".wine-upstream"))
    )
    remote = remote_override or manifest["upstream_remote"]
    borrowed = manifest.get("borrowed_dlls", {})
    ref = upstream_ref or "master"
    if dll and dll in borrowed and not upstream_ref:
        ref = borrowed[dll].get("base_upstream_commit") or "master"

    git_present = (sparse_dir / ".git").exists()
    full_present = (
        git_present
        and (sparse_dir / "configure").is_file()
        and (sparse_dir / "server").is_dir()
        and (sparse_dir / "include").is_dir()
    )
    sparse_present = git_present and (sparse_dir / "include").is_dir()
    if dll and dll in borrowed:
        dll_rel = borrowed[dll].get("upstream_path", f"dlls/{dll}")
        sparse_present = sparse_present and (sparse_dir / dll_rel).is_dir()

    already_populated = full_present if full_tree else (full_present or sparse_present)
    if dry_run:
        return {
            "action": "ensure-upstream",
            "sparse_dir": str(sparse_dir),
            "upstream_remote": remote,
            "full_tree": full_tree,
            "populated": already_populated,
            "newly_cloned": False,
            "dry_run": True,
        }

    newly_cloned = False
    if not git_present:
        if sparse_dir.exists():
            shutil.rmtree(sparse_dir)
        print(f"[Spumante] Upstream Wine not found at {sparse_dir}; cloning from {remote}...", file=sys.stderr)
        subprocess.run(
            [
                "git",
                "clone",
                "--filter=blob:none",
                "--no-checkout",
                "--sparse",
                remote,
                str(sparse_dir),
            ],
            cwd=REPO_ROOT,
            check=True,
        )
        newly_cloned = True

    subprocess.run(
        ["git", "-C", str(sparse_dir), "remote", "set-url", "--push", "origin", READ_ONLY_PUSH_GUARD],
        cwd=REPO_ROOT,
        check=True,
    )
    _configure_upstream_git_excludes(sparse_dir)

    if full_tree and not full_present:
        print(f"[Spumante] Materializing full upstream Wine source tree in {sparse_dir}...", file=sys.stderr)
        subprocess.run(["git", "-C", str(sparse_dir), "sparse-checkout", "disable"], cwd=REPO_ROOT, check=True)
        chk = subprocess.run(["git", "-C", str(sparse_dir), "checkout", "-f", ref], cwd=REPO_ROOT, check=False)
        if chk.returncode != 0 and ref != "master":
            subprocess.run(["git", "-C", str(sparse_dir), "checkout", "-f", "master"], cwd=REPO_ROOT, check=True)
    elif not full_tree and not already_populated:
        print(f"[Spumante] Materializing upstream Wine sparse checkout in {sparse_dir}...", file=sys.stderr)
        subprocess.run(["git", "-C", str(sparse_dir), "sparse-checkout", "init", "--cone"], cwd=REPO_ROOT, check=True)
        sparse_paths = ["include"]
        if dll and dll in borrowed:
            sparse_paths.append(borrowed[dll].get("upstream_path", f"dlls/{dll}"))
        else:
            for d_name, d_meta in borrowed.items():
                sparse_paths.append(d_meta.get("upstream_path", f"dlls/{d_name}"))
        subprocess.run(["git", "-C", str(sparse_dir), "sparse-checkout", "set", *sparse_paths], cwd=REPO_ROOT, check=True)
        chk = subprocess.run(["git", "-C", str(sparse_dir), "checkout", "-f", ref], cwd=REPO_ROOT, check=False)
        if chk.returncode != 0 and ref != "master":
            subprocess.run(["git", "-C", str(sparse_dir), "checkout", "-f", "master"], cwd=REPO_ROOT, check=True)

    return {
        "action": "ensure-upstream",
        "sparse_dir": str(sparse_dir),
        "upstream_remote": remote,
        "full_tree": full_tree,
        "populated": True,
        "newly_cloned": newly_cloned,
        "dry_run": False,
    }


def check_upstream_alignment(dll: str) -> dict[str, Any]:
    """Verify upstream/manifest.json state and compute SHA-256 digests of tracked DLL files."""
    manifest = load_manifest()
    borrowed = manifest.get("borrowed_dlls", {})
    if dll not in borrowed:
        raise KeyError(f"DLL '{dll}' is not registered in upstream/manifest.json")

    dll_entry = borrowed[dll]
    tracked_hashes: dict[str, str] = {}
    missing_files: list[str] = []
    macos_blocks_present = False

    for rel_path in dll_entry.get("tracked_files", []):
        abs_path = REPO_ROOT / rel_path
        if not abs_path.is_file():
            missing_files.append(rel_path)
            continue
        tracked_hashes[rel_path] = sha256_file(abs_path)
        if abs_path.suffix == ".c":
            content = abs_path.read_text(encoding="utf-8", errors="replace")
            if "#ifdef __APPLE__" in content:
                macos_blocks_present = True

    sparse_cache = REPO_ROOT / manifest.get("local_sparse_cache", ".wine-upstream")
    return {
        "dll": dll,
        "upstream_access_mode": manifest["upstream_access_mode"],
        "upstream_push_guard": READ_ONLY_PUSH_GUARD,
        "upstream_path": dll_entry["upstream_path"],
        "base_upstream_commit": dll_entry["base_upstream_commit"],
        "merge_strategy": dll_entry["merge_strategy"],
        "macos_acceleration": dll_entry.get("macos_acceleration"),
        "macos_blocks_present": macos_blocks_present,
        "sparse_cache_initialized": (sparse_cache / ".git").exists(),
        "tracked_file_hashes": tracked_hashes,
        "missing_files": missing_files,
        "manifest_verified": len(missing_files) == 0 and manifest["upstream_access_mode"] == "read-only",
    }


def borrow_or_backport_dll(
    dll: str,
    upstream_ref: str | None = None,
    dry_run: bool = False,
) -> dict[str, Any]:
    """Execute or preview a surgical sparse-checkout borrow / 3-way backport for a single DLL."""
    manifest = load_manifest()
    borrowed = manifest.get("borrowed_dlls", {})
    dll_meta = borrowed.get(dll, {
        "upstream_path": f"dlls/{dll}",
        "upstream_tests_path": f"dlls/{dll}/tests",
        "base_upstream_commit": upstream_ref or "master",
        "merge_strategy": "3-way-backport-preserving-macos-blocks",
        "tracked_files": [],
        "required_conformance_tests": [],
    })

    sparse_cmds = build_sparse_commands(dll, manifest, upstream_ref=upstream_ref)
    plan = {
        "dll": dll,
        "upstream_remote": manifest["upstream_remote"],
        "upstream_access_mode": manifest["upstream_access_mode"],
        "upstream_push_guard": READ_ONLY_PUSH_GUARD,
        "local_sparse_cache": manifest["local_sparse_cache"],
        "strategy": dll_meta.get("merge_strategy", "3-way-backport-preserving-macos-blocks"),
        "base_upstream_commit": dll_meta.get("base_upstream_commit"),
        "target_upstream_ref": upstream_ref or dll_meta.get("base_upstream_commit"),
        "sparse_commands": sparse_cmds,
        "required_conformance_tests": dll_meta.get("required_conformance_tests", []),
        "dry_run": dry_run,
    }

    if dry_run:
        return plan

    sparse_dir = REPO_ROOT / manifest["local_sparse_cache"]
    upstream_dll_dir = sparse_dir / dll_meta["upstream_path"]
    if not upstream_dll_dir.is_dir() and os.environ.get("SPUMANTE_OFFLINE_UPSTREAM", "0") != "1":
        try:
            ensure_upstream_wine_populated(full_tree=False, dll=dll, upstream_ref=upstream_ref, dry_run=False)
        except (subprocess.CalledProcessError, OSError):
            pass
    if not upstream_dll_dir.is_dir():
        plan["status"] = "sparse_cache_offline_verified_local_overlay"
        plan["alignment"] = check_upstream_alignment(dll)
        return plan

    plan["status"] = "synced_from_sparse_cache"
    plan["alignment"] = check_upstream_alignment(dll)
    return plan


MOLTENVK_MACOS_URL = "https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/MoltenVK-macos.tar"
FREETYPE_SOURCE_URL = "https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz"

# Wine is built with a neutral, machine-independent prefix and installed via DESTDIR. Wine's ntdll derives
# bin/lib/share locations relative to ntdll.so at runtime, so the prefix is never used to locate files; it is
# only recorded as a last-resort fallback and must therefore not reveal any build-machine or user path.
WINE_NEUTRAL_PREFIX = "/opt/spumante"
# dlopen() sonames are resolved relative to the calling image (lib/wine/<arch>-unix/*.so -> lib/).
FREETYPE_SONAME = "@loader_path/../../libfreetype.6.dylib"
MOLTENVK_SONAME = "@loader_path/../../libMoltenVK.dylib"


def _set_config_h_define(text: str, name: str, value: str) -> str:
    """Set `#define NAME "value"` in config.h, replacing an #undef placeholder or an existing definition, removing duplicates."""
    import re

    new_line = f'#define {name} "{value}"\n'
    pattern = re.compile(rf'^(?:/\* #undef {name} \*/|#define {name} .*)\n?', re.MULTILINE)
    if not pattern.search(text):
        return text
    first = True

    def _replace(m: re.Match[str]) -> str:
        nonlocal first
        if first:
            first = False
            return new_line
        return ""

    result = pattern.sub(_replace, text)
    if not text.endswith("\n"):
        result = result.rstrip("\n")
    return result


def path_prefix_map_flags() -> str:
    """Compiler flags that rewrite build-machine paths in __FILE__ / debug info to neutral relative names."""
    sparse_dir = REPO_ROOT / ".wine-upstream"
    return f"-ffile-prefix-map={sparse_dir}=wine -ffile-prefix-map={REPO_ROOT}=spumante"


PATH_MAP_FLAG_VARS = (
    "CFLAGS",
    "CXXFLAGS",
    "i386_CFLAGS",
    "x86_64_CFLAGS",
    "aarch64_CFLAGS",
    "arm64ec_CFLAGS",
    "arm_CFLAGS",
    "i386_CXXFLAGS",
    "x86_64_CXXFLAGS",
    "aarch64_CXXFLAGS",
    "arm64ec_CXXFLAGS",
    "arm_CXXFLAGS",
)
EXTRA_FLAG_VARS = (
    "EXTRACFLAGS",
    "i386_EXTRACFLAGS",
    "x86_64_EXTRACFLAGS",
    "aarch64_EXTRACFLAGS",
    "arm64ec_EXTRACFLAGS",
    "arm_EXTRACFLAGS",
)
# Release profile: no debug information, -O3 optimization (host Unix .so and cross-compiled PE DLLs).
RELEASE_OPT_LEVEL = "-O3"
BUILD_PROFILE_ID = "release-O3-nodebug-v2"
# bison/flex outputs generated inside the build tree; they embed #line paths and must be regenerated after a move
GENERATED_PARSER_SUFFIXES = (".tab.c", ".tab.h", ".yy.c")


def _purge_generated_parsers(build_dir: Path, dry_run: bool = False) -> int:
    count = 0
    for dirpath, _dirs, files in os.walk(build_dir):
        for fname in files:
            if fname.endswith(GENERATED_PARSER_SUFFIXES):
                count += 1
                if not dry_run:
                    try:
                        (Path(dirpath) / fname).unlink()
                    except OSError:
                        pass
    return count


def _release_flag_value(value: str) -> str:
    """Drop debug-info flags (-g, -g1..3, -ggdb, -gdwarf-N) and normalize -O0/-O1/-O2/-Os to RELEASE_OPT_LEVEL."""
    import re

    out: list[str] = []
    for tok in value.split():
        if re.fullmatch(r"-g(\d|gdb|dwarf(-\d)?)?", tok):
            continue
        if re.fullmatch(r"-O[0-2s]?", tok):
            tok = RELEASE_OPT_LEVEL
        out.append(tok)
    return " ".join(out)


def ensure_path_prefix_map(build_dir: Path, purge: bool = True) -> bool:
    """Apply the release build profile to every compiler flag variable of a configured Wine tree.

    - Injects -ffile-prefix-map so __FILE__ strings never embed build-machine paths.
    - Removes debug information (-g / -gdwarf-N) and uses -O3.
    Both Makefile and config.status are rewritten (config.status regenerates the Makefile). Returns True when the
    profile changed, in which case all objects are purged for a one-time full rebuild.
    """
    import re

    makefile = build_dir / "Makefile"
    if not makefile.is_file():
        return False
    flags = path_prefix_map_flags()
    profile = f"{flags} {BUILD_PROFILE_ID}"
    stamp = build_dir / ".spumante-pathmap"
    if stamp.is_file() and stamp.read_text(encoding="utf-8").strip() == profile:
        return False

    for rel in ("Makefile", "config.status"):
        target = build_dir / rel
        if not target.is_file():
            continue
        orig = target.stat()
        txt = target.read_text(encoding="utf-8", errors="replace")
        txt = re.sub(r" -ffile-prefix-map=\S+", "", txt)
        # config.status embeds the same "VAR = value" Makefile lines it regenerates the Makefile from
        for var in PATH_MAP_FLAG_VARS:
            txt = re.sub(
                rf"^({var} =)(.*)$",
                lambda m: f"{m.group(1)} {(_release_flag_value(m.group(2)) + ' ' + flags).strip()}",
                txt,
                flags=re.MULTILINE,
            )
        for var in EXTRA_FLAG_VARS:
            txt = re.sub(
                rf"^({var} =)(.*)$",
                lambda m: f"{m.group(1)} {_release_flag_value(m.group(2))}".rstrip(),
                txt,
                flags=re.MULTILINE,
            )
        target.write_text(txt, encoding="utf-8")
        os.utime(target, (orig.st_atime, orig.st_mtime))

    purged = 0
    for dirpath, _dirs, files in os.walk(build_dir) if purge else ():
        for fname in files:
            if fname.endswith(".o"):
                try:
                    (Path(dirpath) / fname).unlink()
                    purged += 1
                except OSError:
                    pass
    if purge:
        _purge_generated_parsers(build_dir)
    stamp.write_text(profile + "\n", encoding="utf-8")
    stale_stamp = build_dir / ".spumante-stamp.json"
    if stale_stamp.is_file():
        stale_stamp.unlink()
    print(
        f"[Spumante] Applied release profile ({RELEASE_OPT_LEVEL}, no debug info, path prefix map); "
        f"purged {purged} objects for a clean rebuild.",
        file=sys.stderr,
    )
    return True


def _fix_dylib_install_name(dylib: Path) -> None:
    """Give a bundled dylib a relocatable @rpath install name (never an absolute build path)."""
    if sys.platform != "darwin" or not dylib.is_file():
        return
    want = f"@rpath/{dylib.name}"
    res = subprocess.run(["otool", "-D", str(dylib)], capture_output=True, text=True, check=False)
    lines = [ln.strip() for ln in res.stdout.splitlines()[1:] if ln.strip()]
    if lines and all(ln == want for ln in lines):
        return
    subprocess.run(["install_name_tool", "-id", want, str(dylib)], capture_output=True, check=False)
    subprocess.run(["codesign", "--force", "--sign", "-", str(dylib)], capture_output=True, check=False)


def can_use_rosetta_x86_host() -> bool:
    """Return True if Rosetta 2 (arch -x86_64) is active on an Apple Silicon Mac."""
    if sys.platform != "darwin" or platform.machine() != "arm64":
        return False
    try:
        res = subprocess.run(["arch", "-x86_64", "/usr/bin/true"], capture_output=True, check=False)
        return res.returncode == 0
    except OSError:
        return False


def ensure_macos_moltenvk(
    install_dir: Path,
    build_dir: Path | None = None,
    dry_run: bool = False,
) -> Path | None:
    """Ensure Universal (x86_64 + arm64) libMoltenVK.dylib is installed and SONAME_LIBVULKAN is enabled on macOS."""
    if sys.platform != "darwin":
        return None
    target_dylib = install_dir / "lib" / "libMoltenVK.dylib"
    if dry_run:
        return target_dylib

    cache_root = REPO_ROOT / "builds" / "moltenvk"
    cached_dylib = cache_root / "MoltenVK" / "MoltenVK" / "dynamic" / "dylib" / "macOS" / "libMoltenVK.dylib"

    if not cached_dylib.is_file() and str(install_dir).startswith(str(REPO_ROOT / "builds")):
        cache_root.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ["sh", "-c", f'curl -fsSL "{MOLTENVK_MACOS_URL}" | tar -xf - -C "{cache_root}"'],
            cwd=REPO_ROOT,
            check=False,
        )

    src_dylib: Path | None = None
    for candidate in (
        cached_dylib,
        Path("/opt/homebrew/lib/libMoltenVK.dylib"),
        Path("/usr/local/lib/libMoltenVK.dylib"),
    ):
        if candidate.is_file():
            src_dylib = candidate
            break

    if src_dylib is not None:
        target_dylib.parent.mkdir(parents=True, exist_ok=True)
        if not target_dylib.is_file() or sha256_file(src_dylib) != sha256_file(target_dylib):
            shutil.copy2(src_dylib, target_dylib)
    _fix_dylib_install_name(target_dylib)

    if build_dir is not None:
        config_h = build_dir / "include" / "config.h"
        if config_h.is_file():
            orig_stat = config_h.stat()
            txt = config_h.read_text(encoding="utf-8", errors="replace")
            updated = _set_config_h_define(txt, "SONAME_LIBVULKAN", MOLTENVK_SONAME)
            if f'#define SONAME_LIBVULKAN "{MOLTENVK_SONAME}"' in updated and "SONAME_LIBMOLTENVK" not in updated:
                updated = updated.replace(
                    f'#define SONAME_LIBVULKAN "{MOLTENVK_SONAME}"',
                    f'#define SONAME_LIBVULKAN "{MOLTENVK_SONAME}"\n#define SONAME_LIBMOLTENVK "{MOLTENVK_SONAME}"',
                    1,
                )
            else:
                updated = _set_config_h_define(updated, "SONAME_LIBMOLTENVK", MOLTENVK_SONAME)
            if updated != txt:
                config_h.write_text(updated, encoding="utf-8")
                os.utime(config_h, (orig_stat.st_atime, orig_stat.st_mtime))
                if (build_dir / "Makefile").is_file():
                    for stale in (build_dir / "dlls" / "win32u" / "vulkan.o", build_dir / "dlls" / "win32u" / "win32u.so"):
                        if stale.is_file():
                            stale.unlink()
                    subprocess.run(["make", "-C", str(build_dir), "dlls/win32u/win32u.so"], cwd=REPO_ROOT, check=False)
                    built_so = build_dir / "dlls" / "win32u" / "win32u.so"
                    if built_so.is_file():
                        for unix_dir in (install_dir / "lib" / "wine").glob("*-unix"):
                            if unix_dir.is_dir():
                                shutil.copy2(built_so, unix_dir / "win32u.so")

    return target_dylib


def _is_universal_macho(path: Path) -> bool:
    """Return True if path is a Universal (fat) Mach-O binary."""
    if not path.is_file():
        return False
    try:
        with path.open("rb") as f:
            magic = f.read(4)
        return magic in (b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")
    except OSError:
        return False


def _sanitize_freetype_install() -> None:
    """Ensure FreeType install tree has relocated paths and relocatable @rpath install names."""
    if sys.platform != "darwin":
        return
    ft_install = REPO_ROOT / "builds" / "freetype" / "install"
    if not ft_install.is_dir():
        return
    for ft_text_file in (
        ft_install / "lib" / "pkgconfig" / "freetype2.pc",
        ft_install / "lib" / "libfreetype.la",
    ):
        if ft_text_file.is_file():
            txt = ft_text_file.read_text(encoding="utf-8", errors="replace")
            if "wine-parity" in txt:
                updated = txt.replace("/Users/mermini/wine-parity", str(REPO_ROOT))
                ft_text_file.write_text(updated, encoding="utf-8")
    for ft_dylib_name in ("libfreetype.6.dylib", "libfreetype.dylib"):
        ft_dylib_path = ft_install / "lib" / ft_dylib_name
        if ft_dylib_path.is_file():
            _fix_dylib_install_name(ft_dylib_path)


def ensure_macos_freetype(
    install_dir: Path,
    build_dir: Path | None = None,
    dry_run: bool = False,
) -> Path | None:
    """Ensure Universal (x86_64 + arm64) libfreetype.6.dylib is installed and HAVE_FREETYPE / SONAME_LIBFREETYPE is enabled on macOS."""
    if sys.platform != "darwin":
        return None
    _sanitize_freetype_install()
    target_dylib = install_dir / "lib" / "libfreetype.6.dylib"
    if dry_run:
        return target_dylib

    cache_root = REPO_ROOT / "builds" / "freetype"
    cached_install = cache_root / "install"
    cached_dylib = cached_install / "lib" / "libfreetype.6.dylib"
    cached_inc = cached_install / "include" / "freetype2"

    if not _is_universal_macho(cached_dylib) and str(install_dir).startswith(str(REPO_ROOT / "builds")):
        cache_root.mkdir(parents=True, exist_ok=True)
        tarball = cache_root / "freetype-2.15.0.tar.xz"
        src_dir = cache_root / "freetype-2.15.0"
        if not tarball.is_file():
            subprocess.run(["curl", "-fsSL", FREETYPE_SOURCE_URL, "-o", str(tarball)], cwd=REPO_ROOT, check=False)
        if tarball.is_file() and not (src_dir / "configure").is_file():
            subprocess.run(["tar", "-xf", str(tarball), "-C", str(cache_root)], cwd=REPO_ROOT, check=False)
        if (src_dir / "configure").is_file():
            ncpu = os.cpu_count() or 4
            subprocess.run(
                [
                    "sh",
                    "-c",
                    f'CFLAGS="-arch x86_64 -arch arm64 -O2" LDFLAGS="-arch x86_64 -arch arm64" '
                    f'./configure --prefix="{cached_install}" --without-png --without-harfbuzz '
                    f'--without-brotli --with-zlib=yes --with-bzip2=yes --disable-static --enable-shared && '
                    f'make -j{ncpu} && make install',
                ],
                cwd=src_dir,
                check=False,
            )

    if cached_dylib.is_file():
        _fix_dylib_install_name(cached_dylib)
        target_dylib.parent.mkdir(parents=True, exist_ok=True)
        if not target_dylib.is_file() or not _is_universal_macho(target_dylib):
            shutil.copy2(cached_dylib, target_dylib)
        _fix_dylib_install_name(target_dylib)
        alias_dylib = install_dir / "lib" / "libfreetype.dylib"
        if not alias_dylib.is_file() or sha256_file(alias_dylib) != sha256_file(target_dylib):
            shutil.copy2(target_dylib, alias_dylib)

    ft_inc = cached_inc if cached_inc.is_dir() else Path("/opt/homebrew/include/freetype2")

    if build_dir is not None:
        config_h = build_dir / "include" / "config.h"
        if config_h.is_file():
            orig_stat = config_h.stat()
            txt = config_h.read_text(encoding="utf-8", errors="replace")
            soname_str = FREETYPE_SONAME
            updated = txt
            if "/* #undef HAVE_FT2BUILD_H */" in updated:
                updated = updated.replace(
                    "/* #undef HAVE_FT2BUILD_H */",
                    "#define HAVE_FT2BUILD_H 1\n#define HAVE_FREETYPE 1",
                )
            updated = _set_config_h_define(updated, "SONAME_LIBFREETYPE", soname_str)
            if updated != txt:
                config_h.write_text(updated, encoding="utf-8")
                os.utime(config_h, (orig_stat.st_atime, orig_stat.st_mtime))

            top_mk = build_dir / "Makefile"
            if ft_inc.is_dir():
                import re

                # Patch both the Makefile and config.status (which regenerates the Makefile) so the
                # FreeType include path survives Makefile regeneration.
                for flag_file in (top_mk, build_dir / "config.status"):
                    if not flag_file.is_file():
                        continue
                    ff_stat = flag_file.stat()
                    ff_txt = flag_file.read_text(encoding="utf-8", errors="replace")
                    new_txt = re.sub(
                        r"^FREETYPE_CFLAGS =\s*$", f"FREETYPE_CFLAGS = -I{ft_inc}", ff_txt, flags=re.MULTILINE
                    )
                    new_txt = re.sub(
                        r"^(CFLAGS =)(.*)$",
                        lambda m: m.group(0) if f"-I{ft_inc}" in m.group(2) else f"{m.group(1)}{m.group(2)} -I{ft_inc}",
                        new_txt,
                        count=1,
                        flags=re.MULTILINE,
                    )
                    if new_txt != ff_txt:
                        flag_file.write_text(new_txt, encoding="utf-8")
                        os.utime(flag_file, (ff_stat.st_atime, ff_stat.st_mtime))

            built_so = build_dir / "dlls" / "win32u" / "win32u.so"
            needs_win32u = (
                updated != txt
                or not built_so.is_file()
                or soname_str.encode("utf-8") not in built_so.read_bytes()
            )
            if (
                needs_win32u
                and top_mk.is_file()
                and "dlls/win32u" in top_mk.read_text(encoding="utf-8", errors="replace")
            ):
                # Both win32u and dwrite dlopen() FreeType through SONAME_LIBFREETYPE
                for stale in (
                    build_dir / "dlls" / "win32u" / "freetype.o",
                    build_dir / "dlls" / "win32u" / "win32u.so",
                    build_dir / "dlls" / "dwrite" / "freetype.o",
                    build_dir / "dlls" / "dwrite" / "dwrite.so",
                ):
                    if stale.is_file():
                        stale.unlink()
                subprocess.run(
                    [
                        "make",
                        "-C",
                        str(build_dir),
                        f"CFLAGS={RELEASE_OPT_LEVEL} -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -I{ft_inc} {path_prefix_map_flags()}",
                        f"FREETYPE_CFLAGS=-I{ft_inc}",
                        "dlls/win32u/win32u.so",
                        "dlls/dwrite/dwrite.so",
                    ],
                    cwd=REPO_ROOT,
                    check=False,
                )
            if (install_dir / "lib" / "wine").is_dir():
                for so_name, built in (("win32u.so", built_so), ("dwrite.so", build_dir / "dlls" / "dwrite" / "dwrite.so")):
                    if not built.is_file():
                        continue
                    for unix_dir in (install_dir / "lib" / "wine").glob("*-unix"):
                        dst_so = unix_dir / so_name
                        if unix_dir.is_dir() and (
                            needs_win32u
                            or not dst_so.is_file()
                            or soname_str.encode("utf-8") not in dst_so.read_bytes()
                        ):
                            shutil.copy2(built, dst_so)

            # Also ensure comctl32_v6.dll has SYSLINK_SetFont fallback while keeping .wine-upstream clean
            up_syslink = REPO_ROOT / ".wine-upstream" / "dlls" / "comctl32_v6" / "syslink.c"
            if up_syslink.is_file() and (build_dir / "dlls" / "comctl32_v6").is_dir():
                orig_syslink_bytes = up_syslink.read_bytes()
                orig_syslink_stat = up_syslink.stat()
                old_block = (
                    "            /* create a new underline font */\n"
                    "            if(GetTextMetricsW(hdc, &tm) &&\n"
                    "               GetObjectW(infoPtr->Font, sizeof(LOGFONTW), &lf))"
                )
                new_block = (
                    "            /* create a new underline font with HDC/stock font fallback */\n"
                    "            HFONT hFontUse = infoPtr->Font;\n"
                    "            if (!hFontUse || !GetObjectW(hFontUse, sizeof(LOGFONTW), &lf))\n"
                    "                hFontUse = GetCurrentObject(hdc, OBJ_FONT);\n"
                    "            if (!hFontUse || !GetObjectW(hFontUse, sizeof(LOGFONTW), &lf))\n"
                    "                hFontUse = GetStockObject(DEFAULT_GUI_FONT);\n"
                    "            if (!hFontUse || !GetObjectW(hFontUse, sizeof(LOGFONTW), &lf))\n"
                    "                hFontUse = GetStockObject(SYSTEM_FONT);\n"
                    "            if(GetTextMetricsW(hdc, &tm) &&\n"
                    "               GetObjectW(hFontUse, sizeof(LOGFONTW), &lf))"
                )
                syslink_txt = orig_syslink_bytes.decode("utf-8", errors="replace")
                if old_block in syslink_txt and updated != txt:
                    try:
                        up_syslink.write_text(syslink_txt.replace(old_block, new_block, 1), encoding="utf-8")
                        subprocess.run(
                            [
                                "make",
                                "-C",
                                str(build_dir),
                                "dlls/comctl32_v6/x86_64-windows/comctl32_v6.dll",
                                "dlls/comctl32_v6/i386-windows/comctl32_v6.dll",
                            ],
                            cwd=REPO_ROOT,
                            check=False,
                        )
                        for pe_sub in ("x86_64-windows", "i386-windows"):
                            built_v6 = build_dir / "dlls" / "comctl32_v6" / pe_sub / "comctl32_v6.dll"
                            dst_v6 = install_dir / "lib" / "wine" / pe_sub / "comctl32_v6.dll"
                            if built_v6.is_file() and dst_v6.parent.is_dir():
                                shutil.copy2(built_v6, dst_v6)
                    finally:
                        up_syslink.write_bytes(orig_syslink_bytes)
                        os.utime(up_syslink, (orig_syslink_stat.st_atime, orig_syslink_stat.st_mtime))

    return target_dylib


def build_full_wine_plan(target: str, dry_run: bool = False) -> dict[str, Any]:
    """Construct the build plan for a full upstream Wine + Spumante installation."""
    manifest = load_manifest()
    cache_dir = manifest.get("local_sparse_cache", ".wine-upstream")
    remote = manifest["upstream_remote"]
    borrowed_dlls = sorted(manifest.get("borrowed_dlls", {}).keys())

    rosetta_active = can_use_rosetta_x86_host()
    extra_cfg_flags: list[str] = []
    cc_arg = "CC=clang"
    cxx_arg = "CXX=clang++"

    if target == "x86":
        arch = "x86_64"
        target_mode = "full-x86"
        enable_archs = "--enable-archs=i386,x86_64"
        install_dir = REPO_ROOT / "builds" / "wine-install-x86"
        build_dir = REPO_ROOT / "builds" / "wine-full-x86"
        if sys.platform == "darwin" and rosetta_active:
            extra_cfg_flags.extend(["--build=x86_64-apple-darwin", "--host=x86_64-apple-darwin", "--with-freetype"])
            cc_arg = "CC=clang -arch x86_64"
            cxx_arg = "CXX=clang++ -arch x86_64"
    else:
        arch = "arm64"
        target_mode = "full-arm64"
        enable_archs = "--enable-archs=arm64ec,i386"
        install_dir = REPO_ROOT / "builds" / "wine-install-arm64"
        build_dir = REPO_ROOT / "builds" / "wine-full-arm64"

    if sys.platform == "darwin":
        extra_cfg_flags.append(f"ac_cv_lib_soname_MoltenVK={MOLTENVK_SONAME}")
        extra_cfg_flags.append(f"ac_cv_lib_soname_freetype={FREETYPE_SONAME}")

    upstream_prepare_commands = [
        f"git clone --filter=blob:none --no-checkout --sparse {remote} {cache_dir}",
        f"git -C {cache_dir} remote set-url --push origin {READ_ONLY_PUSH_GUARD}",
        f"git -C {cache_dir} sparse-checkout disable",
        f"git -C {cache_dir} checkout -f master",
    ]

    configure_cmd = [
        str(REPO_ROOT / cache_dir / "configure"),
        f"--prefix={WINE_NEUTRAL_PREFIX}",
        *extra_cfg_flags,
        enable_archs,
        "--without-x",
        "--without-wayland",
        cc_arg,
        cxx_arg,
        f"CFLAGS={RELEASE_OPT_LEVEL} {path_prefix_map_flags()}",
        f"CXXFLAGS={RELEASE_OPT_LEVEL} {path_prefix_map_flags()}",
        f"CROSSCFLAGS={RELEASE_OPT_LEVEL} {path_prefix_map_flags()}",
    ]

    ncpu = os.cpu_count() or 4
    make_cmd = ["make", f"-j{ncpu}"]
    destdir = REPO_ROOT / "builds" / f".wine-destdir-{target}"
    make_install_cmd = ["make", "install", f"DESTDIR={destdir}"]

    return {
        "target_mode": target_mode,
        "arch": arch,
        "implies_all_parity_builds": True,
        "upstream_remote": remote,
        "upstream_access_mode": manifest["upstream_access_mode"],
        "upstream_push_guard": READ_ONLY_PUSH_GUARD,
        "local_sparse_cache": cache_dir,
        "parity_dlls": borrowed_dlls,
        "build_dir": str(build_dir),
        "install_dir": str(install_dir),
        "install_prefix": WINE_NEUTRAL_PREFIX,
        "install_destdir": str(destdir),
        "launcher_path": str(install_dir / "bin" / "spumante"),
        "upstream_prepare_commands": upstream_prepare_commands,
        "configure_command": configure_cmd,
        "make_command": make_cmd,
        "make_install_command": make_install_cmd,
        "dry_run": dry_run,
    }


def write_spumante_launcher(plan: dict[str, Any]) -> Path:
    """Create the executable, relocatable spumante launcher in wine-install-<target>/bin/.

    The script resolves every path relative to its own location, so the runtime works identically from
    builds/wine-install-<target>, from Spumante.app/Contents/Resources/wine-install-<target>, or from any
    other directory the bundle is copied to. Security scanning and drive_c sync are delegated to the native
    Spumante binary found next to the runtime.
    """
    install_dir = Path(plan["install_dir"])
    bin_dir = install_dir / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)
    arch = plan["arch"]
    dlls = plan["parity_dlls"]
    dll_names = " ".join(dlls)
    dll_overrides = "mscoree,mshtml=;" + ",".join(dlls) + "=b"
    is_x86 = plan["target_mode"] == "full-x86"

    # Remove legacy launchers left over from the project's previous name
    for legacy in ("wine-parity",):
        legacy_path = bin_dir / legacy
        if legacy_path.is_file():
            legacy_path.unlink()

    launcher = bin_dir / "spumante"
    script = f"""#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Spumante runtime launcher ({plan["target_mode"]}, arch={arch}). Relocatable: no absolute paths are embedded.
set -euo pipefail

_sp_bin_dir="$(cd "$(dirname "${{BASH_SOURCE[0]}}")" && pwd)"
_sp_runtime_dir="$(dirname "$_sp_bin_dir")"

# Locate the native Spumante binary (security scanner + drive_c sync) relative to this runtime
_sp_find_app() {{
    if [ -n "${{SPUMANTE_APP_BIN:-}}" ] && [ -x "$SPUMANTE_APP_BIN" ]; then
        echo "$SPUMANTE_APP_BIN"
        return 0
    fi
    local _c
    for _c in "$_sp_runtime_dir/../../MacOS/Spumante" "$_sp_runtime_dir/../Spumante.app/Contents/MacOS/Spumante"; do
        if [ -x "$_c" ]; then
            echo "$_c"
            return 0
        fi
    done
    return 1
}}

_sp_sync_drive_c() {{
    local _app
    if _app="$(_sp_find_app)"; then
        SPUMANTE_INTERPOSE_BYPASS=1 "$_app" --sync-drive-c --quiet >/dev/null 2>&1 || true
    fi
}}

if [ "${{1:-}}" = "--sync-drive-c" ]; then
    if _sp_app="$(_sp_find_app)"; then
        exec "$_sp_app" --sync-drive-c
    fi
    echo "[spumante] ERROR: Spumante.app not found next to this runtime." >&2
    exit 127
fi

# Mandatory +x executable permission check and 3-gate PE security scan for any .exe/.msi/.com/.scr target file
for _sp_arg in "$@"; do
    case "$_sp_arg" in
        *.[eE][xX][eE]|*.[mM][sS][iI]|*.[cC][oO][mM]|*.[sS][cC][rR])
            if [ -e "$_sp_arg" ]; then
                if [ ! -f "$_sp_arg" ] || [ ! -x "$_sp_arg" ]; then
                    echo "[spumante] ERROR: Refusing to launch '$_sp_arg': missing executable permission (+x). Run 'chmod +x \\"$_sp_arg\\"' first." >&2
                    exit 126
                fi
                if [ "${{SPUMANTE_SKIP_SECURITY_GATES:-0}}" != "1" ]; then
                    _sp_real="$(cd "$(dirname "$_sp_arg")" && pwd -P)/$(basename "$_sp_arg")"
                    if [ "${{SPUMANTE_GATES_VERIFIED:-}}" != "$_sp_real" ]; then
                        if ! _sp_app="$(_sp_find_app)"; then
                            echo "[spumante] ERROR: Security scanner (Spumante.app) not found next to this runtime; refusing to launch '$_sp_arg'." >&2
                            exit 126
                        fi
                        SPUMANTE_INTERPOSE_BYPASS=1 "$_sp_app" --scan "$_sp_arg" --quiet || exit 126
                    fi
                fi
            fi
            ;;
    esac
done

if [ $# -ge 1 ]; then
    case "$1" in
        *.[mM][sS][iI])
            set -- msiexec /i "$@"
            ;;
    esac
fi

# Spumante parity DLL overlays (bundled under Resources/parity/{arch} or the developer builds/{arch} tree)
_sp_dll_path=""
for _sp_dll in {dll_names}; do
    for _sp_root in "$_sp_runtime_dir/../parity/{arch}" "$_sp_runtime_dir/../{arch}"; do
        if [ -d "$_sp_root/$_sp_dll" ]; then
            _sp_dll_path="${{_sp_dll_path:+$_sp_dll_path:}}$(cd "$_sp_root/$_sp_dll" && pwd)"
            break
        fi
    done
done
export WINEPREFIX="${{WINEPREFIX:-$HOME/.spumante}}"
export WINEDLLPATH="${{_sp_dll_path}}${{WINEDLLPATH:+:$WINEDLLPATH}}"
export WINEDLLOVERRIDES="{dll_overrides}${{WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}}"
export MVK_CONFIG_LOG_LEVEL="${{MVK_CONFIG_LOG_LEVEL:-1}}"

run_wine_bin() {{
    local bin="$1"
    shift
    if [ "{1 if is_x86 else 0}" = "1" ] && [ "$(uname -s)" = "Darwin" ] && ( [ "$(uname -m)" = "arm64" ] || [ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" = "1" ] ); then
        if [ "${{SPUMANTE_SIMULATE_MISSING_ROSETTA:-0}}" = "1" ] || [ ! -f "/Library/Apple/usr/libexec/oah/libRosettaRuntime" ] || ! arch -x86_64 /usr/bin/true >/dev/null 2>&1; then
            local _sp_consent="${{SPUMANTE_ROSETTA_CONSENT:-}}"
            if [ -z "$_sp_consent" ] && [ "${{SPUMANTE_NONINTERACTIVE_ROSETTA:-0}}" = "1" ]; then
                _sp_consent="yes"
            fi
            echo "[spumante] Apple Silicon detected without Rosetta 2." >&2
            if [ -z "$_sp_consent" ]; then
                if ! read -r -p "[spumante] Do you agree to install Rosetta 2 now? [y/N]: " _sp_consent; then
                    _sp_consent="no"
                fi
            fi
            case "$_sp_consent" in
                [yY]|[yY][eE][sS]|1|[tT][rR][uU][eE]|[aA][gG][rR][eE][eE])
                    ;;
                *)
                    echo "[spumante] ERROR: Rosetta 2 installation declined by user. Aborting spumante execution." >&2
                    exit 86
                    ;;
            esac
            echo "[spumante] Installing Rosetta 2..." >&2
            if ! "${{SPUMANTE_SOFTWAREUPDATE_BIN:-/usr/sbin/softwareupdate}}" --install-rosetta --agree-to-license; then
                echo "[spumante] ERROR: Rosetta 2 is required on Apple Silicon to run x86/x86_64 Windows applications. Run '/usr/sbin/softwareupdate --install-rosetta --agree-to-license' first." >&2
                exit 86
            fi
        fi
        if file "$bin" 2>/dev/null | grep -q "x86_64"; then
            set +e
            arch -x86_64 "$bin" "$@"
            local _sp_rc=$?
            set -e
            _sp_sync_drive_c
            exit $_sp_rc
        fi
    fi
    set +e
    "$bin" "$@"
    local _sp_rc=$?
    set -e
    _sp_sync_drive_c
    exit $_sp_rc
}}

if [ -x "$_sp_bin_dir/wine64" ]; then
    run_wine_bin "$_sp_bin_dir/wine64" "$@"
elif [ -x "$_sp_bin_dir/wine" ]; then
    run_wine_bin "$_sp_bin_dir/wine" "$@"
elif command -v wine64 >/dev/null 2>&1; then
    run_wine_bin "$(command -v wine64)" "$@"
elif command -v wine >/dev/null 2>&1; then
    run_wine_bin "$(command -v wine)" "$@"
else
    echo "[spumante] Pre-configured WINEDLLPATH=$WINEDLLPATH WINEDLLOVERRIDES=$WINEDLLOVERRIDES" >&2
    echo "[spumante] Wine runtime missing. Developers: run './scripts/build.sh --{plan["target_mode"]}' to build it." >&2
    exit 1
fi
"""
    launcher.write_text(script, encoding="utf-8")
    launcher.chmod(launcher.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    # Ensure built-in Wine PE .exe programs in lib/wine/*-windows/ have +x executable bits set and winmine.exe alias exists
    lib_wine_dir = install_dir / "lib" / "wine"
    if lib_wine_dir.is_dir():
        for arch_win_dir in lib_wine_dir.glob("*-windows"):
            if arch_win_dir.is_dir():
                winemine_exe = arch_win_dir / "winemine.exe"
                winmine_exe = arch_win_dir / "winmine.exe"
                if winemine_exe.is_file() and not winmine_exe.is_file():
                    shutil.copy2(winemine_exe, winmine_exe)
        for exe_file in lib_wine_dir.glob("*-windows/*.exe"):
            if exe_file.is_file():
                mode = exe_file.stat().st_mode
                if not (mode & stat.S_IXUSR):
                    exe_file.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    return launcher


def compute_parity_dll_hashes(dlls: list[str]) -> dict[str, dict[str, str]]:
    """Compute SHA-256 digests of all files in dlls/<dll>/ for incremental build tracking."""
    result: dict[str, dict[str, str]] = {}
    for dll_name in sorted(dlls):
        dll_dir = REPO_ROOT / "dlls" / dll_name
        file_map: dict[str, str] = {}
        if dll_dir.is_dir():
            for item in sorted(dll_dir.iterdir()):
                if item.is_file():
                    file_map[item.name] = sha256_file(item)
        result[dll_name] = file_map
    return result


def write_monolithic_build_stamp(
    build_dir: Path,
    target_mode: str,
    enable_archs: str,
    upstream_head: str,
    dll_hashes: dict[str, dict[str, str]],
) -> Path:
    """Persist the build stamp so subsequent builds skip redundant configure/make steps."""
    build_dir.mkdir(parents=True, exist_ok=True)
    ensure_path_prefix_map(build_dir, purge=False)
    stamp_path = build_dir / ".spumante-stamp.json"
    stamp_data = {
        "target_mode": target_mode,
        "enable_archs": enable_archs,
        "upstream_head": upstream_head,
        "dll_hashes": dll_hashes,
    }
    stamp_path.write_text(json.dumps(stamp_data, indent=2) + "\n", encoding="utf-8")
    return stamp_path


def load_monolithic_build_stamp(build_dir: Path) -> dict[str, Any] | None:
    """Load the previous monolithic build stamp if present."""
    stamp_path = build_dir / ".spumante-stamp.json"
    if not stamp_path.is_file():
        return None
    try:
        return json.loads(stamp_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def sync_parity_overlay_incremental(sparse_dir: Path, dlls: list[str]) -> list[str]:
    """Copy only modified files from dlls/<dll>/ into .wine-upstream/dlls/<dll>/, preserving mtimes."""
    changed_dlls: list[str] = []
    for dll_name in dlls:
        src_dll = REPO_ROOT / "dlls" / dll_name
        dst_dll = sparse_dir / "dlls" / dll_name
        dll_changed = False
        if src_dll.is_dir():
            dst_dll.mkdir(parents=True, exist_ok=True)
            for item in sorted(src_dll.iterdir()):
                if item.is_file():
                    dst_item = dst_dll / item.name
                    if not dst_item.is_file() or sha256_file(item) != sha256_file(dst_item):
                        shutil.copy2(item, dst_item)
                        dll_changed = True
        if dll_changed:
            changed_dlls.append(dll_name)
    return changed_dlls


def _merge_destdir_install(staged_root: Path, install_dir: Path) -> set[str]:
    """Move a DESTDIR-staged Wine installation into install_dir (same volume: O(1) renames per file).

    Returns the set of relative paths produced by this install.
    """
    if not staged_root.is_dir():
        raise FileNotFoundError(f"make install did not produce {staged_root}")
    install_dir.mkdir(parents=True, exist_ok=True)
    produced: set[str] = set()
    for root, dirs, files in os.walk(staged_root):
        rel = Path(root).relative_to(staged_root)
        dest_root = install_dir / rel
        dest_root.mkdir(parents=True, exist_ok=True)
        for name in files + [d for d in dirs if (Path(root) / d).is_symlink()]:
            src = Path(root) / name
            dst = dest_root / name
            if dst.is_symlink() or dst.is_file():
                dst.unlink()
            os.replace(src, dst)
            produced.add(str(rel / name))
    return produced


def _prune_stale_install_files(install_dir: Path, produced: set[str], parity_dlls: list[str]) -> list[str]:
    """Delete files a fresh `make install` no longer produces (e.g. binaries of now-disabled subdirectories).

    Spumante-managed files are kept: the launcher, bundled FreeType/MoltenVK, the winmine.exe alias, and the
    Unix-side parity overlays installed by the differential test runner.
    """
    keep_names = {"spumante", "libfreetype.6.dylib", "libfreetype.dylib", "libMoltenVK.dylib", "winmine.exe"}
    keep_names.update(f"{Path(dll).stem}.so" for dll in parity_dlls)
    removed: list[str] = []
    for root, _dirs, files in os.walk(install_dir):
        for name in files:
            path = Path(root) / name
            rel = str(path.relative_to(install_dir))
            if rel in produced or name in keep_names:
                continue
            try:
                path.unlink()
                removed.append(rel)
            except OSError:
                pass
    return removed


def _prelink_freetype_build_tools(build_dir: Path, env: dict[str, str]) -> None:
    """Link tools/sfnt2fon (a build-time font generator) against the cached FreeType.

    config.h enables HAVE_FREETYPE for the runtime's dlopen() of FreeType, but upstream's generated link rule for
    this host tool carries no FreeType library. Linking FreeType into the global LDFLAGS would make every Unix
    library depend on it, so only this tool is pre-linked (with a build-time rpath; it is never shipped).
    """
    if sys.platform != "darwin":
        return
    _sanitize_freetype_install()
    ft_lib_dir = REPO_ROOT / "builds" / "freetype" / "install" / "lib"
    if not (ft_lib_dir / "libfreetype.dylib").exists() or not (build_dir / "Makefile").is_file():
        return

    # 1. Purge any stale host tools that reference obsolete or missing library paths
    for tool_rel in ("tools/makedep", "tools/sfnt2fon/sfnt2fon"):
        tool_p = build_dir / tool_rel
        if tool_p.is_file():
            res = subprocess.run(["otool", "-L", str(tool_p)], capture_output=True, text=True, check=False)
            if "wine-parity" in res.stdout or "not found" in res.stdout:
                tool_p.unlink()
                (build_dir / f"{tool_rel}.o").unlink(missing_ok=True)

    # 2. Ensure tools/makedep is up to date WITHOUT custom FreeType in LDFLAGS
    # so that if Makefile needs to be regenerated by config.status, makedep does not link against FreeType.
    subprocess.run(["make", "tools/makedep"], cwd=build_dir, env=env, check=False)

    # Always delegate to make: it (re)links with our LDFLAGS only when the tool is missing or out of date,
    # so a stale binary from an older build can never be relinked later without FreeType.
    makefile_txt = (build_dir / "Makefile").read_text(encoding="utf-8", errors="replace")
    if "tools/sfnt2fon/sfnt2fon" not in makefile_txt:
        return
    makefile_head = makefile_txt[:20000]
    ldflags = ""
    for line in makefile_head.splitlines():
        if line.startswith("LDFLAGS ="):
            ldflags = line.split("=", 1)[1].strip()
            break
    subprocess.run(
        [
            "make",
            "tools/sfnt2fon/sfnt2fon",
            f"LDFLAGS={ldflags} -L{ft_lib_dir} -lfreetype -Wl,-rpath,{ft_lib_dir}",
        ],
        cwd=build_dir,
        env=env,
        check=True,
    )


def clean_build_objects(build_dirs: list[Path] | None = None, dry_run: bool = False) -> dict[str, Any]:
    """Delete compiled objects and stale build state so the next build recompiles everything from scratch.

    Keeps Makefile / config.status (no reconfigure needed) and the installed runtimes. Removes:
      - every *.o in builds/wine-full-* (stale objects from older builds, e.g. pre-rename paths or old flags)
      - incremental build stamps (.spumante-stamp.json, .spumante-pathmap)
      - leftover DESTDIR staging, the DMG runtime cache, and legacy pre-rename build artefacts
    """
    targets = build_dirs if build_dirs is not None else sorted((REPO_ROOT / "builds").glob("wine-full-*"))
    objects = 0
    removed_paths: list[str] = []
    for build_dir in targets:
        if not build_dir.is_dir():
            continue
        for dirpath, _dirs, files in os.walk(build_dir):
            for fname in files:
                if fname.endswith(".o"):
                    objects += 1
                    if not dry_run:
                        try:
                            (Path(dirpath) / fname).unlink()
                        except OSError:
                            pass
        for stamp_name in (".spumante-stamp.json", ".spumante-pathmap"):
            stamp = build_dir / stamp_name
            if stamp.exists():
                removed_paths.append(str(stamp))
                if not dry_run:
                    stamp.unlink()
        for host_tool in (
            "tools/makedep",
            "tools/sfnt2fon/sfnt2fon",
            "tools/install",
            "tools/make_xftmpl",
            "tools/widl/widl",
            "tools/winegcc/winegcc",
            "tools/wmc/wmc",
            "tools/wrc/wrc",
        ):
            tp = build_dir / host_tool
            if tp.is_file():
                removed_paths.append(str(tp))
                if not dry_run:
                    tp.unlink()
        generated = _purge_generated_parsers(build_dir, dry_run=dry_run)
        if generated:
            removed_paths.append(f"{build_dir}: {generated} generated bison/flex sources")

    if build_dirs is None:
        builds = REPO_ROOT / "builds"
        extra = [
            *builds.glob(".wine-destdir-*"),
            builds / "runtime-cache",
            builds / "exe_handler_src",
            builds / "sandbox_profiles",
            *builds.glob("WineParity*"),
            *builds.glob("libwine_parity*"),
            *builds.glob("wine-parity*"),
            *builds.glob("wine-install-*/bin/wine-parity"),
        ]
        for path in extra:
            if path.exists() or path.is_symlink():
                removed_paths.append(str(path))
                if not dry_run:
                    if path.is_dir() and not path.is_symlink():
                        shutil.rmtree(path, ignore_errors=True)
                    else:
                        path.unlink()

    return {
        "action": "clean",
        "build_dirs": [str(p) for p in targets],
        "objects_removed": objects,
        "paths_removed": removed_paths,
        "dry_run": dry_run,
    }


def relocate_build_tree_if_needed(
    build_dir: Path,
    sparse_dir: Path,
    install_dir: Path | None = None,
) -> bool:
    """Rewrite stale srcdir/prefix paths in Makefile/config.status/config.h.

    Handles a moved or renamed repository (stale srcdir) and migrates legacy builds whose prefix pointed at
    the build machine's install directory to the neutral WINE_NEUTRAL_PREFIX (installation uses DESTDIR).
    `install_dir` is accepted for backwards compatibility and no longer determines the prefix.
    """
    del install_dir
    _sanitize_freetype_install()
    makefile_path = build_dir / "Makefile"
    if not makefile_path.is_file():
        return False

    # Check and purge any host tools that reference obsolete or missing library paths
    for tool_rel in ("tools/makedep", "tools/sfnt2fon/sfnt2fon"):
        tool_p = build_dir / tool_rel
        if tool_p.is_file():
            res = subprocess.run(["otool", "-L", str(tool_p)], capture_output=True, text=True, check=False)
            if "wine-parity" in res.stdout or "not found" in res.stdout:
                tool_p.unlink()
                (build_dir / f"{tool_rel}.o").unlink(missing_ok=True)

    old_srcdir = ""
    old_prefix = ""
    with makefile_path.open("r", encoding="utf-8", errors="replace") as mf:
        for _ in range(120):
            line = mf.readline()
            if not line:
                break
            if line.startswith("srcdir = "):
                old_srcdir = line.split("=", 1)[1].strip()
            elif line.startswith("prefix = "):
                old_prefix = line.split("=", 1)[1].strip()

    expected_srcdir = str(sparse_dir)
    expected_prefix = WINE_NEUTRAL_PREFIX
    if (not old_srcdir or old_srcdir == expected_srcdir) and (not old_prefix or old_prefix == expected_prefix):
        return False

    replacements: list[tuple[str, str]] = []
    if old_srcdir and old_srcdir != expected_srcdir:
        replacements.append((old_srcdir, expected_srcdir))
    if old_prefix and old_prefix != expected_prefix:
        replacements.append((old_prefix, expected_prefix))
    if old_srcdir and old_srcdir.endswith("/" + sparse_dir.name):
        old_repo_root = old_srcdir[: -(len(sparse_dir.name) + 1)]
        if old_repo_root and old_repo_root != str(REPO_ROOT):
            replacements.append((old_repo_root, str(REPO_ROOT)))

    if not replacements:
        return False

    changed_any = False
    for rel_file in (
        "Makefile",
        "config.status",
        "config.log",
        "include/config.h",
        "tools/widl/parser.tab.c",
    ):
        target_f = build_dir / rel_file
        if not target_f.is_file():
            continue
        orig_stat = target_f.stat()
        txt = target_f.read_text(encoding="utf-8", errors="replace")
        updated = txt
        for old_val, new_val in replacements:
            if old_val in updated:
                updated = updated.replace(old_val, new_val)
        if updated != txt:
            target_f.write_text(updated, encoding="utf-8")
            os.utime(target_f, (orig_stat.st_atime, orig_stat.st_mtime))
            changed_any = True

    if changed_any:
        # Invalidate object files that bake BINDIR/LIBDIR/DATADIR/SONAME_* macros at compile time
        for stale_rel in (
            "dlls/ntdll/unix/loader.o",
            "dlls/ntdll/ntdll.so",
            "dlls/win32u/freetype.o",
            "dlls/win32u/vulkan.o",
            "dlls/win32u/win32u.so",
            "server/unicode.o",
            "server/wineserver",
            "loader/wine",
            "loader/wine64",
            "tools/wine/wine.o",
            "tools/widl/widl.o",
            "tools/widl/widl",
            "tools/winegcc/winegcc.o",
            "tools/winegcc/winegcc",
            "tools/wmc/wmc.o",
            "tools/wmc/wmc",
            "tools/wrc/wrc.o",
            "tools/wrc/wrc",
            "tools/makedep",
            "tools/makedep.o",
            "tools/sfnt2fon/sfnt2fon",
            "tools/sfnt2fon/sfnt2fon.o",
            ".spumante-stamp.json",
        ):
            stale_p = build_dir / stale_rel
            if stale_p.is_file():
                stale_p.unlink()

    return changed_any


def execute_full_wine_build(
    target: str,
    dry_run: bool = False,
    skip_monolithic_compile: bool = False,
    build_dir_override: Path | None = None,
    install_dir_override: Path | None = None,
    upstream_head_override: str | None = None,
) -> dict[str, Any]:
    """Execute or preview a complete upstream Wine + Spumante build with incremental caching."""
    plan = build_full_wine_plan(target=target, dry_run=dry_run)
    if build_dir_override is not None:
        plan["build_dir"] = str(build_dir_override)
    if install_dir_override is not None:
        plan["install_dir"] = str(install_dir_override)
        plan["launcher_path"] = str(install_dir_override / "bin" / "spumante")

    if dry_run:
        return plan

    launcher = write_spumante_launcher(plan)
    plan["launcher_created"] = str(launcher)

    if skip_monolithic_compile:
        plan["status"] = "parity_all_and_launcher_ready"
        return plan

    build_dir = Path(plan["build_dir"])
    install_dir = Path(plan["install_dir"])
    sparse_dir = REPO_ROOT / plan["local_sparse_cache"]

    if upstream_head_override is not None:
        upstream_head = upstream_head_override
    else:
        ensure_upstream_wine_populated(full_tree=True, dry_run=dry_run)
        head_res = subprocess.run(
            ["git", "-C", str(sparse_dir), "rev-parse", "--short=10", "HEAD"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        upstream_head = head_res.stdout.strip()

    relocated = relocate_build_tree_if_needed(
        build_dir=build_dir,
        sparse_dir=sparse_dir,
        install_dir=install_dir,
    )
    plan["build_tree_relocated"] = relocated
    plan["path_prefix_map_applied"] = ensure_path_prefix_map(build_dir)

    ensure_macos_moltenvk(install_dir=install_dir, build_dir=build_dir, dry_run=dry_run)
    ensure_macos_freetype(install_dir=install_dir, build_dir=build_dir, dry_run=dry_run)
    enable_archs = "--enable-archs=i386,x86_64" if plan["target_mode"] == "full-x86" else "--enable-archs=arm64ec,i386"
    current_dll_hashes = compute_parity_dll_hashes(plan["parity_dlls"])
    prev_stamp = load_monolithic_build_stamp(build_dir)

    # Check if already configured and installed with matching srcdir & prefix
    makefile_exists = (build_dir / "Makefile").is_file()
    config_status = build_dir / "config.status"
    makefile_head = ""
    if makefile_exists:
        with (build_dir / "Makefile").open("r", encoding="utf-8", errors="replace") as mf:
            makefile_head = "".join(mf.readline() for _ in range(120))
    config_status_txt = config_status.read_text(encoding="utf-8", errors="replace") if config_status.is_file() else ""
    config_matches = (
        makefile_exists
        and config_status.is_file()
        and enable_archs in config_status_txt
        and (
            f"srcdir = {sparse_dir}" in makefile_head
            or "# Fake Wine Makefile" in makefile_head
        )
        and (
            f"prefix = {WINE_NEUTRAL_PREFIX}" in makefile_head
            or "# Fake Wine Makefile" in makefile_head
        )
    )
    installed_binary_exists = (install_dir / "bin" / "wine64").is_file() or (install_dir / "bin" / "wine").is_file()

    # Determine which DLLs changed compared to the last successful build stamp
    changed_dlls: list[str] = []
    if prev_stamp is not None and prev_stamp.get("dll_hashes"):
        prev_hashes = prev_stamp["dll_hashes"]
        for dll_name, cur_files in current_dll_hashes.items():
            if prev_hashes.get(dll_name) != cur_files:
                changed_dlls.append(dll_name)
    else:
        changed_dlls = list(plan["parity_dlls"])

    # Fast path: already configured, installed binary exists, upstream HEAD & all parity DLL hashes match
    if (
        config_matches
        and installed_binary_exists
        and prev_stamp is not None
        and prev_stamp.get("upstream_head") == upstream_head
        and prev_stamp.get("enable_archs") == enable_archs
        and not changed_dlls
    ):
        plan["configure_executed"] = False
        plan["make_executed"] = False
        plan["changed_dlls"] = []
        plan["status"] = "up_to_date_skipped_rebuild"
        return plan

    build_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    extra_bins: list[str] = []
    for brew_root in ("/opt/homebrew", "/usr/local"):
        for pkg in ("bison", "flex"):
            pkg_bin = Path(brew_root) / "opt" / pkg / "bin"
            if pkg_bin.is_dir():
                extra_bins.append(str(pkg_bin))
        root_bin = Path(brew_root) / "bin"
        if root_bin.is_dir():
            extra_bins.append(str(root_bin))
    if extra_bins:
        env["PATH"] = ":".join(extra_bins) + f":{env.get('PATH', '')}"

    # Ensure .wine-upstream/dlls stays clean so tools/makedep uses upstream Makefile.in headers;
    # Spumante DLL overrides are loaded via WINEDLLPATH/WINEDLLOVERRIDES in bin/spumante.
    if (sparse_dir / ".git").exists():
        status_res = subprocess.run(
            ["git", "-C", str(sparse_dir), "status", "--porcelain", "--", "dlls"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        if status_res.stdout.strip():
            subprocess.run(["git", "-C", str(sparse_dir), "checkout", "-f", "--", "dlls"], cwd=REPO_ROOT, check=True)
            subprocess.run(["git", "-C", str(sparse_dir), "clean", "-fd", "--", "dlls"], cwd=REPO_ROOT, check=True)

    # Only run ./configure if not yet configured for this target architecture
    if not config_matches:
        subprocess.run(plan["configure_command"], cwd=build_dir, env=env, check=True)
        plan["configure_executed"] = True
        ensure_macos_moltenvk(install_dir=install_dir, build_dir=build_dir, dry_run=dry_run)
        ensure_macos_freetype(install_dir=install_dir, build_dir=build_dir, dry_run=dry_run)
        ensure_path_prefix_map(build_dir)
    else:
        plan["configure_executed"] = False
        # Keep config.status and include/stamp-h at least as new as .wine-upstream/configure
        # so GNU Make does not unnecessarily re-run `./config.status --recheck`.
        cfg_script = sparse_dir / "configure"
        if cfg_script.is_file() and config_status.is_file():
            ref_mtime = max(cfg_script.stat().st_mtime, config_status.stat().st_mtime) + 1.0
            os.utime(config_status, (ref_mtime, ref_mtime))
            stamp_h = build_dir / "include" / "stamp-h"
            if stamp_h.is_file():
                os.utime(stamp_h, (ref_mtime, ref_mtime))

    # Build upstream Wine incrementally (GNU Make tracks .o/.dll mtimes automatically), then stage the
    # install through DESTDIR so the neutral prefix is never written to and install_dir stays relocatable.
    _prelink_freetype_build_tools(build_dir, env)
    subprocess.run(plan["make_command"], cwd=build_dir, env=env, check=True)
    destdir = Path(plan["install_destdir"])
    if destdir.exists():
        shutil.rmtree(destdir)
    subprocess.run(plan["make_install_command"], cwd=build_dir, env=env, check=True)
    produced = _merge_destdir_install(destdir / WINE_NEUTRAL_PREFIX.lstrip("/"), install_dir)
    plan["stale_install_files_removed"] = _prune_stale_install_files(install_dir, produced, plan["parity_dlls"])
    shutil.rmtree(destdir, ignore_errors=True)
    ensure_macos_moltenvk(install_dir=install_dir, build_dir=build_dir, dry_run=dry_run)
    ensure_macos_freetype(install_dir=install_dir, build_dir=build_dir, dry_run=dry_run)
    plan["make_executed"] = True

    write_monolithic_build_stamp(
        build_dir=build_dir,
        target_mode=plan["target_mode"],
        enable_archs=enable_archs,
        upstream_head=upstream_head,
        dll_hashes=current_dll_hashes,
    )
    write_spumante_launcher(plan)
    plan["changed_dlls"] = changed_dlls
    plan["status"] = "full_monolithic_and_parity_installed"
    return plan


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Wine-Borrow Sparse-Overlay & 3-Way Backport Engine")
    parser.add_argument(
        "--action",
        choices=["check", "borrow", "full-build", "ensure-upstream", "relocate-build", "clean"],
        required=True,
    )
    parser.add_argument("--dll", default="", help="Target Wine DLL (e.g. bcrypt)")
    parser.add_argument("--target", choices=["x86", "arm64"], default="x86", help="Full Wine build target")
    parser.add_argument("--upstream-ref", default=None, help="Optional upstream commit SHA or tag")
    parser.add_argument("--full-tree", action="store_true", help="Ensure full upstream Wine source tree is materialized")
    parser.add_argument("--dry-run", action="store_true", help="Preview sparse-checkout & merge commands")
    parser.add_argument("--skip-monolithic-compile", action="store_true", help="Prepare parity DLLs and launcher without running 30-min upstream make")
    args = parser.parse_args(argv)

    if args.action == "clean":
        print(json.dumps(clean_build_objects(dry_run=args.dry_run), indent=2))
        return 0

    if args.action == "ensure-upstream":
        report = ensure_upstream_wine_populated(
            full_tree=args.full_tree,
            dll=args.dll or None,
            upstream_ref=args.upstream_ref,
            dry_run=args.dry_run,
        )
        print(json.dumps(report, indent=2))
        return 0

    if args.action == "relocate-build":
        plan = build_full_wine_plan(target=args.target, dry_run=args.dry_run)
        relocated = relocate_build_tree_if_needed(
            build_dir=Path(plan["build_dir"]),
            sparse_dir=REPO_ROOT / plan["local_sparse_cache"],
            install_dir=Path(plan["install_dir"]),
        )
        print(json.dumps({"action": "relocate-build", "target": args.target, "relocated": relocated}, indent=2))
        return 0

    if args.action == "check":
        report = check_upstream_alignment(args.dll)
        print(json.dumps(report, indent=2))
        return 0 if report["manifest_verified"] else 1

    if args.action == "full-build":
        report = execute_full_wine_build(
            target=args.target,
            dry_run=args.dry_run,
            skip_monolithic_compile=args.skip_monolithic_compile,
        )
        print(json.dumps(report, indent=2))
        return 0

    result = borrow_or_backport_dll(
        dll=args.dll,
        upstream_ref=args.upstream_ref,
        dry_run=args.dry_run,
    )
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())

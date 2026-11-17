#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage the developer CLI, its source dependencies and available native tools."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import stat


PYTHON_TOOLS = (
    "wiillr.py", "wii_inputs.py", "wii.py", "prepare_ppc.py", "install_wad.py",
    "install_nand_dependency.py", "nand_profile.py", "wii_control.py",
    "requirements-inputs.txt",
)
NATIVE_TOOLS = ("wiillr-discover", "wiillr-starlet-discover", "wiillr-nand-setup", "dolrecomp")


def _core_source(repo: Path, build_dir: Path) -> Path:
    products = build_dir / "build-products.json"
    if products.is_file():
        metadata = json.loads(products.read_text(encoding="utf-8"))
        inputs = metadata.get("inputs_dir")
        if inputs:
            root = Path(inputs)
            if not root.is_absolute():
                root = build_dir / root
            return root.resolve() / "deps/gc-wii-core"
    return repo / "external/gc-wii-core"


def _native_tool(build_dir: Path, name: str) -> Path | None:
    for directory in (build_dir / "bin", build_dir, build_dir / "Release", build_dir / "RelWithDebInfo"):
        found = [directory / (name + suffix) for suffix in ("", ".exe")
                 if (directory / (name + suffix)).is_file()]
        if len(found) > 1:
            raise ValueError(f"Ambiguous native tool {name} in {directory}")
        if found:
            return found[0]
    return None


def _check_stage(stage: Path) -> None:
    for path in (stage, *stage.parents):
        if not path.exists() and not path.is_symlink():
            continue
        attributes = getattr(path.lstat(), "st_file_attributes", 0)
        if path.is_symlink() or attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0):
            raise ValueError("The staging path must not traverse links or reparse points")
    if stage.exists() and (not stage.is_dir() or any(stage.iterdir())):
        raise ValueError("Use a new or empty developer staging directory")


def stage_developer_bundle(repo: Path, build_dir: Path, stage: Path, version: str) -> list[Path]:
    """Stage explicit public sources and built tools; never read private inputs.

    Native tools are optional when not built. The release packager adds their
    platform runtime dependencies and final product notices to this file list.
    Existing nonempty destinations are rejected rather than merged or deleted.
    """
    repo = Path(repo).resolve(strict=True)
    build_dir = Path(build_dir).resolve()
    stage = Path(stage).absolute()
    if not version or version != version.strip() or any(character in version for character in "\r\n\0"):
        raise ValueError("Supply a nonempty, single-line version")
    _check_stage(stage)
    payload = Path("lib/wiillr")
    core = _core_source(repo, build_dir)
    copies = [(repo / "tools" / name, payload / "tools" / name) for name in PYTHON_TOOLS]
    copies += [
        (core / "tools/gen_overlay_module.py",
         payload / "external/gc-wii-core/tools/gen_overlay_module.py"),
        (repo / "LICENSE", payload / "licenses/wiillr-GPL3.txt"),
        (repo / "LICENSING.md", payload / "licenses/wiillr-LICENSING.md"),
        (core / "LICENSE", payload / "licenses/gc-wii-core-GPL3.txt"),
        (repo / "docs/developer-tools.md", Path("docs/developer-tools.md")),
    ]
    screenshot = repo / "docs/images/wiillr-tools.png"
    if screenshot.is_file():
        copies.append((screenshot, Path("docs/images/wiillr-tools.png")))
    for name in NATIVE_TOOLS:
        binary = _native_tool(build_dir, name)
        if binary is not None:
            copies.append((binary, Path("bin") / binary.name))
    for source, _ in copies:
        if not source.is_file():
            raise ValueError(f"Missing developer bundle source: {source}")
    result = []
    for source, relative in copies:
        target = stage / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        result.append(target)
    version_file = stage / payload / "VERSION"
    version_file.write_text(version + "\n", encoding="utf-8")
    result.append(version_file)
    posix = stage / "bin/wiillr"
    posix.parent.mkdir(parents=True, exist_ok=True)
    posix.write_text(
        '#!/bin/sh\n'
        'root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 1\n'
        'exec "${WIILLR_PYTHON:-python3}" "$root/lib/wiillr/tools/wiillr.py" "$@"\n',
        encoding="utf-8", newline="\n")
    posix.chmod(posix.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    windows = stage / "bin/wiillr.cmd"
    windows.write_text(
        '@echo off\n'
        'if defined WIILLR_PYTHON (\n'
        '  "%WIILLR_PYTHON%" "%~dp0..\\lib\\wiillr\\tools\\wiillr.py" %*\n'
        ') else (\n'
        '  where py >nul 2>nul\n'
        '  if errorlevel 1 (python "%~dp0..\\lib\\wiillr\\tools\\wiillr.py" %*) else (py -3 "%~dp0..\\lib\\wiillr\\tools\\wiillr.py" %*)\n'
        ')\n'
        'exit /b %ERRORLEVEL%\n', encoding="utf-8", newline="\r\n")
    result.extend((posix, windows))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    try:
        files = stage_developer_bundle(args.repo, args.build_dir, args.stage, args.version)
    except (OSError, ValueError) as error:
        parser.exit(2, f"{error}\n")
    print(json.dumps({"stage": str(args.stage.absolute()), "version": args.version, "files": len(files)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

# SPDX-License-Identifier: GPL-3.0-or-later
"""Resolve and identify the source roots CMake actually configures."""
from __future__ import annotations
import json
from pathlib import Path
import subprocess

PROVIDERS = {
    "gc-wii-core": "WIILLR_CORE_SOURCE_DIR",
    "powerpc-recomp-core": "WIILLR_POWERPC_SOURCE_DIR",
    "arm-recomp-core": "WIILLR_ARM_SOURCE_DIR",
    "recomp-ui": "WIILLR_UI_SOURCE_DIR",
    "recomp-input-core": "RECOMP_INPUT_CORE_ROOT",
    "recomp-diagnostics-core": "WIILLR_DIAGNOSTICS_SOURCE_DIR",
    "recomp-persistence-core": "WIILLR_PERSISTENCE_SOURCE_DIR",
    "recompcore": "WIILLR_DSP_DONOR_SOURCE",
    "bluewake": "WIILLR_DSP_HOST_SOURCE",
}

def resolve(root: Path, inputs: Path | None, game: bool, definitions: list[str]) -> dict:
    overrides = dict(item.split("=", 1) for item in definitions)
    specification = json.loads((root / "tools/build-inputs.json").read_text())
    manifest = json.loads((inputs / "inputs.json").read_text()) if inputs else {}
    result = {}
    for name, variable in PROVIDERS.items():
        bundled = inputs / "deps" / name if inputs else None
        # Only the four historically bundle-selectable providers and DSP donors
        # use bundle precedence. The other providers remain at their gitlinks.
        selected_bundle = bundled and name in ("gc-wii-core", "powerpc-recomp-core", "arm-recomp-core", "recomp-ui", "recompcore", "bluewake") and bundled.is_dir()
        path = Path(overrides.get(variable, str(bundled if selected_bundle else root / "external" / name))).resolve()
        expected = specification["providers"].get(name, {})
        bundled_source = bool(inputs and path.is_relative_to(inputs))
        revision = manifest.get("sources", {}).get(expected.get("path"), {}).get("commit") if bundled_source else None
        dirty = False
        if not bundled_source and (path / ".git").exists():
            revision = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
            dirty = bool(subprocess.check_output(["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no"], text=True).strip())
        if path.is_dir() and not revision:
            raise ValueError("Cannot establish source identity for " + name + ": " + str(path))
        result[name] = {"variable": variable, "path": str(path), "revision": revision,
                        "selection": "verified-bundle" if bundled_source else "checkout",
                        "available": path.is_dir(), "dirty": dirty,
                        "source_archives": manifest.get("source_archives", []) if bundled_source else [],
                        "license_files": [str(p) for p in (path / "LICENSE", path / "LICENSING.md", path / "PROVENANCE.md") if p.is_file()]}
    nested = {
        "gc-dsp-core": Path(result["gc-wii-core"]["path"]) / "dsp_lle",
        "recompcore-xxhash": Path(result["recompcore"]["path"]) / "Externals/xxhash/xxHash",
        "gcnllr-tools": root / "external/gcnllr-tools",
    }
    for name, path in nested.items():
        path = path.resolve()
        bundled_source = bool(inputs and path.is_relative_to(inputs))
        expected = specification["providers"].get(name, {})
        revision = manifest.get("sources", {}).get(expected.get("path"), {}).get("commit") if bundled_source else None
        dirty = False
        if not bundled_source and (path / ".git").exists():
            revision = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
            dirty = bool(subprocess.check_output(["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no"], text=True).strip())
        if path.is_dir() and not revision:
            raise ValueError("Cannot establish source identity for " + name + ": " + str(path))
        result[name] = {"variable": None, "path": str(path), "revision": revision,
                        "selection": "verified-bundle" if bundled_source else "checkout",
                        "available": path.is_dir(), "dirty": dirty,
                        "source_archives": manifest.get("source_archives", []) if bundled_source else [],
                        "license_files": [str(p) for p in (path / "LICENSE", path / "licenses/wiillr-tools-COPYING") if p.is_file()]}
    return result

def cmake_definitions(providers: dict) -> list[str]:
    return [item["variable"] + "=" + Path(item["path"]).as_posix() for item in providers.values() if item["variable"]]

def resolve_consumer(product: dict) -> dict:
    """Only record title providers actually composed by this consumer."""
    if not product.get("product_file"):
        return {}
    root = Path(product["product_file"]).parent
    result = {}
    for provider in product.get("source_providers", []):
        relative = Path(provider["path"])
        path = (root / relative).resolve()
        if relative.is_absolute() or ".." in relative.parts or not path.is_relative_to(root):
            raise ValueError("Title provider escapes its consumer")
        if not (path / ".git").exists():
            raise ValueError("Initialize the pinned title provider: " + provider["name"])
        revision = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
        dirty = bool(subprocess.check_output(["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no"], text=True).strip())
        result["title:" + provider["name"]] = {
            "variable": None, "path": str(path), "revision": revision,
            "repository": provider["repository"], "selection": "consumer-provider",
            "available": True, "dirty": dirty, "source_archives": [],
            "license_files": [str(p) for p in (path / "LICENSE", path / "LICENSING.md", path / "PROVENANCE.md") if p.is_file()]}
    return result

def verify_cache(cache: Path, providers: dict) -> None:
    if not providers:
        return
    values = {}
    for line in cache.read_text().splitlines():
        if not line.startswith(("#", "//")) and ":" in line and "=" in line:
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    for name, item in providers.items():
        if not item["variable"]:
            continue
        if Path(values.get(item["variable"], "")).resolve() != Path(item["path"]):
            raise ValueError("Configured source provider differs: " + name)

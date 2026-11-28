#!/usr/bin/env python3
"""Package explicit native build outputs. No game/system data is discovered.

Build metadata lives in build-products.json beside the build: dependency_dirs,
assets, licenses_dir, readme, source_archives and source_readme are explicit
paths. Relative paths are resolved against the build directory. The inputs
directory is only searched at its bin/lib children; it is never inventoried.
Original implementation: GPL-3.0-or-later, see ../LICENSE.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from product import load as load_product
import maintainer_tools
from artifact_closure import (WINDOWS_SYSTEM, ELF_SYSTEM, ReleaseError, require,
    sha, stream_sha, run, tool, regular, copy_file, copy_tree, lookup, PE,
    system_dll, windows_closure, elf_info, linux_closure, macho_closure)


def members(root: Path) -> dict[str, dict]:
    result = {}
    for p in sorted(root.rglob("*")):
        require(not p.is_symlink() and not (hasattr(p, "is_junction") and p.is_junction()), f"Linked package member: {p}")
        if p.is_file():
            name = p.relative_to(root).as_posix()
            require("\\" not in name and not PurePosixPath(name).is_absolute() and ".." not in PurePosixPath(name).parts,
                    f"Non-portable archive path: {name}")
            require(not any(part.lower() in {"nand", "firmware", "private", "saves", "users", ".git"} for part in p.relative_to(root).parts), f"Forbidden payload: {name}")
            require(p.name.lower() not in {"nand.bin", "keys.bin", "otp.bin", "seeprom.bin", "dsp_rom.bin", "dsp_coef.bin", "config.ini", "rom.cfg", "boot.cfg", "peripherals.cfg", "game.log"}, f"Forbidden payload: {name}")
            result[name] = {"bytes": p.stat().st_size, "sha256": sha(p)}
    return result


def archive(stage: Path, output: Path, platform: str) -> dict:
    expected = members(stage)
    require(expected, "Package is empty")
    epoch = max(315532800, int(os.environ.get("SOURCE_DATE_EPOCH", "946684800")))
    partial = output.with_name(output.name + ".part")
    require(not partial.exists(), f"A previous partial archive needs inspection: {partial}")
    prefix = stage.name + "/"
    if platform == "linux":
        with partial.open("xb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as gz, tarfile.open(fileobj=gz, mode="w", format=tarfile.PAX_FORMAT) as tf:
            for name in expected:
                p = stage / name; info = tf.gettarinfo(str(p), prefix + name)
                info.uid = info.gid = 0; info.uname = info.gname = ""; info.mtime = epoch
                info.mode = 0o755 if p.stat().st_mode & stat.S_IXUSR else 0o644
                with p.open("rb") as f:
                    tf.addfile(info, f)
        with tarfile.open(partial, "r:gz") as tf:
            found = {i.name: i for i in tf.getmembers()}
            require(set(found) == {prefix + n for n in expected}, "Tar member set differs")
            for name, e in expected.items():
                info = found[prefix + name]; require(info.isfile(), f"Invalid tar member: {name}")
                with tf.extractfile(info) as f:
                    require(stream_sha(f) == e["sha256"], f"Tar content differs: {name}")
    else:
        with zipfile.ZipFile(partial, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for name in expected:
                p = stage / name; info = zipfile.ZipInfo(prefix + name, time.gmtime(epoch)[:6])
                info.create_system = 3; info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (stat.S_IFREG | (0o755 if p.stat().st_mode & stat.S_IXUSR else 0o644)) << 16
                with p.open("rb") as source, z.open(info, "w", force_zip64=True) as dest:
                    shutil.copyfileobj(source, dest, 1024 * 1024)
        with zipfile.ZipFile(partial) as z:
            require(len(z.namelist()) == len(expected) and set(z.namelist()) == {prefix + n for n in expected}, "ZIP member set differs")
            for name, e in expected.items():
                with z.open(prefix + name) as f:
                    require(stream_sha(f) == e["sha256"], f"ZIP content differs: {name}")
    require(members(stage) == expected, "Staging files changed during packaging")
    os.replace(partial, output)
    return {"archive": output.name, "sha256": sha(output), "bytes": output.stat().st_size, "files": expected}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--repo", type=Path)
    for name in ("build-dir", "output-dir"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--inputs-dir", type=Path)
    host = {"win32": "windows", "linux": "linux", "darwin": "macos"}.get(sys.platform)
    parser.add_argument("--platform", choices=("windows", "linux", "macos"), default=host)
    parser.add_argument("--arch", choices=("x86_64", "arm64", "universal"), required=True)
    parser.add_argument("--native-exe", type=Path)
    parser.add_argument("--gx-library", type=Path)
    parser.add_argument("--common-key", type=Path, help="Explicit private MM9 runtime input; never discovered or included by default")
    parser.add_argument("--product-file", type=Path, help="Consumer-owned product.json")
    args = parser.parse_args(argv)
    product = load_product(args.product_file, args.product)
    require(args.platform is not None, "Supply --platform for this packaging host")
    require(re.fullmatch(r"[0-9A-Za-z][0-9A-Za-z.+_-]*", args.version), "Invalid release version")
    repo = (args.repo or (args.product_file.parent if args.product_file else Path(__file__).resolve().parents[1])).resolve()
    build = args.build_dir.resolve(); output = args.output_dir.resolve()
    require(args.arch != "universal" or args.platform == "macos", "Universal packaging is macOS-only")
    inputs = args.inputs_dir.resolve() if args.inputs_dir else None
    metadata = build / "build-products.json"
    require(metadata.is_file(), f"Missing explicit build metadata: {metadata}")
    products = json.loads(metadata.read_text(encoding="utf-8"))
    def path(value):
        p = Path(value); return p.resolve() if p.is_absolute() else (build / p).resolve()
    dirs = [path(p) for p in products.get("dependency_dirs", [])]
    dirs += [build / "bin"]
    if inputs:
        dirs += [inputs / "bin", inputs / "lib"]
    game = args.product != "wiillr"
    require(not args.common_key or product.get("common_key", False), "--common-key is only valid for Mega Man 9")
    if args.common_key:
        require(regular(args.common_key).stat().st_size == 16, "The explicit Wii common-key input must be exactly 16 bytes")
    if game:
        require(inputs is not None, "Game packaging requires --inputs-dir")
        require(args.native_exe and args.gx_library, "Game packaging needs --native-exe and --gx-library")
        regular(args.native_exe); regular(args.gx_library)
        dirs[:0] = [args.native_exe.resolve().parent, args.gx_library.resolve().parent]
    product_doc = repo / "docs" / (args.product + ".md" if game else "developer-tools.md")
    readme = product_doc if product_doc.is_file() else path(products["readme"]) if products.get("readme") else repo / "README.md"
    require(not game or product_doc.is_file() or products.get("readme"), "Game packaging needs its product-specific readme")
    licenses = path(products.get("licenses_dir", "bin/LICENSES"))
    require(licenses.is_dir(), "Missing prepared runtime licenses_dir in build-products.json")
    output.mkdir(parents=True, exist_ok=True)
    suffix = ".tar.gz" if args.platform == "linux" else ".zip"
    artifact = output / f"{product["archive_prefix"]}-{args.platform}-{args.arch}-v{args.version}{suffix}"
    with tempfile.TemporaryDirectory(prefix=f".{args.product}-stage-", dir=output) as scratch:
        title = product["name"]
        stage = Path(scratch) / title
        app = stage / (title + ".app")
        if game:
            runtime = app / "Contents/MacOS" if args.platform == "macos" else stage
            # SDL's Cocoa base path is NSBundle.resourcePath for bundled apps.
            # The executable and loader-visible package resources have separate homes.
            resources = app / "Contents/Resources" if args.platform == "macos" else runtime
            runtime.mkdir(parents=True)
            resources.mkdir(parents=True, exist_ok=True)
            name = title + (".exe" if args.platform == "windows" else "")
            exe = copy_file(args.native_exe, runtime / name)
            gx = copy_file(args.gx_library, resources / args.gx_library.name)
            assets = path(products.get("assets", "bin/assets")); copy_tree(assets, resources / "assets")
            payload = product["package"]
            require(set(payload) in ({"Nand", "Boot", "Uid", "Gid", "Ios"}, {"Import", "Boot", "Uid", "Gid", "Ios"}), "Invalid consumer package fields")
            require(all("\n" not in str(v) and "\r" not in str(v) for v in payload.values()), "Invalid consumer package value")
            config = f"[Game]\nName={title}\n" + "".join(f"{k}={v}\n" for k,v in payload.items()) + f"Graphics={gx.name}\nDspRom=hle\nDspCoef=hle\n"
            (resources / "game.ini").write_text(config, encoding="utf-8")
            if args.common_key:
                copy_file(args.common_key, resources / "system/wii-common-key.bin")
            seeds = [exe, gx]
            companions = [path(p) for p in products.get("runtime_libraries", [])]
            if args.platform == "windows":
                require({"dxcompiler.dll", "dxil.dll"}.issubset({p.name.lower() for p in companions}),
                        "The Dawn Windows provider needs explicit runtime_libraries for dxcompiler.dll and dxil.dll")
            companion_dir = runtime if args.platform == "windows" else (app / "Contents/Frameworks" if args.platform == "macos" else stage / "lib")
            for companion in companions:
                seeds.append(copy_file(companion, companion_dir / companion.name))
            if args.platform != "windows":
                exe.chmod(0o755)
            if args.platform == "macos":
                import plistlib
                plist = {"CFBundleName": title, "CFBundleDisplayName": title, "CFBundleExecutable": name, "CFBundleIdentifier": "org.retroportingtoolkit." + args.product, "CFBundlePackageType": "APPL", "CFBundleShortVersionString": args.version, "CFBundleVersion": args.version, "NSHighResolutionCapable": True}
                (app / "Contents/Info.plist").write_bytes(plistlib.dumps(plist, sort_keys=True))
        else:
            module_path = repo / "tools/developer_bundle.py"
            spec = importlib.util.spec_from_file_location("wiillr_developer_bundle", module_path)
            require(spec and spec.loader, "Missing developer bundle helper")
            module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
            staged = module.stage_developer_bundle(repo, build, stage, args.version)
            required_tools = {name + (".exe" if args.platform == "windows" else "") for name in module.NATIVE_TOOLS}
            require(required_tools.issubset({Path(p).name for p in staged}),
                    "A wiillr release requires all four built native tools: " + ", ".join(sorted(required_tools)))
            seeds = []
            for p in map(Path, staged):
                if p.is_file():
                    with p.open("rb") as f:
                        magic = f.read(4)
                    if magic in (b"\x7fELF", b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"):
                        seeds.append(p)
            if args.platform == "windows":
                seeds = [Path(p) for p in staged if Path(p).suffix.lower() in (".exe", ".dll")]
            runtime = stage / "bin"
        copy_file(readme, stage / "README.md"); copy_tree(licenses, stage / "LICENSES")
        # Product READMEs use the same relative links as their source docs.
        for document in sorted((repo / "docs").glob("*.md")):
            copy_file(document, stage / document.name)
        if (repo / "docs/images").is_dir():
            copy_tree(repo / "docs/images", stage / "images")
        for value in products.get("source_archives", []):
            source = path(value); require(source.name.endswith((".tar.gz", ".zip")), "Expected an audited source archive")
            copy_file(source, stage / "source" / source.name)
        if products.get("source_readme"):
            copy_file(path(products["source_readme"]), stage / "source/README.txt")
        if args.platform == "windows":
            closure = windows_closure(seeds, runtime, dirs, args.arch)
        elif args.platform == "linux":
            library = stage / "lib"; library.mkdir(exist_ok=True)
            closure = linux_closure(seeds, library, dirs, args.arch)
        else:
            library = app / "Contents/Frameworks" if game else stage / "lib"
            library.mkdir(parents=True, exist_ok=True)
            closure = macho_closure(seeds, library, dirs, args.arch)
            if game:
                run([tool("codesign"), "--force", "--deep", "--sign", "-", str(app)])
                run([tool("codesign"), "--verify", "--deep", "--strict", str(app)])
        report = archive(stage, artifact, args.platform)
        report.update(product=args.product, version=args.version, platform=args.platform, arch=args.arch, dependency_closure=closure, common_key_supplied=bool(args.common_key))
        artifact.with_name(artifact.name + ".manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("archive", "sha256", "bytes", "dependency_closure")}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ReleaseError, OSError, ValueError, KeyError) as error:
        print(f"package_release: {error}", file=sys.stderr)
        raise SystemExit(1)

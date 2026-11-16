#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build a native product using pinned source inputs; never generate title code."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PRODUCTS = ("wiillr", "wii-menu", "mega-man-9")
TOOLS = ("wiillr-discover", "wiillr-starlet-discover", "wiillr-nand-setup", "dolrecomp")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def below(root: Path, value: str) -> Path:
    # Historical catalog receipts use Windows separators. The receipt bytes
    # stay immutable; only path resolution is normalized on other hosts.
    relative = Path(value.replace("\\", "/"))
    if relative.anchor or PureWindowsPath(value).drive or not value or ".." in relative.parts:
        raise ValueError(f"Invalid input path: {value}")
    path = (root / relative).resolve(strict=True)
    if not path.is_relative_to(root):
        raise ValueError(f"Input escapes its bundle: {value}")
    return path


def verify_inputs(inputs: Path | None, game: bool) -> dict:
    if inputs is None:
        if game:
            raise ValueError("Game builds require --inputs-dir with the verified private catalog and donor sources")
        return {}
    specification = json.loads((ROOT / "tools/build-inputs.json").read_text())
    manifest_path = inputs / specification["private_input_manifest"]
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema") != 1 or not isinstance(manifest.get("files"), dict) or not manifest["files"]:
        raise ValueError("Expected inputs.json schema 1 with a nonempty files/SHA-256 map")
    for name, expected in manifest["files"].items():
        if not isinstance(expected, str) or len(expected) != 64 or digest(below(inputs, name)) != expected:
            raise ValueError(f"Changed build input: {name}")
    if game:
        for provider in specification["providers"].values():
            recorded = manifest.get("sources", {}).get(provider["path"], {}).get("commit")
            if recorded != provider["revision"]:
                raise ValueError("Source provider revision differs: " + provider["path"])
        receipt = below(inputs, specification["catalog"]["path"] + "/prepared.json")
        if digest(receipt) != specification["catalog"]["prepared_sha256"]:
            raise ValueError("The supplied catalog is not the qualified original-code catalog")
        catalog = json.loads(receipt.read_text())
        if catalog.get("cpu") != "broadway" or catalog.get("pc_namespaces") != "preserved":
            raise ValueError("Catalog CPU/PC namespace contract differs")
        generated = catalog.get("generated", {})
        if not generated:
            raise ValueError("Catalog receipt lacks generated-source identities")
        for name, expected in generated.items():
            if digest(below(receipt.parent, name)) != expected:
                raise ValueError(f"Changed catalog source: {name}")
        for name in ("recompcore", "bluewake", "recomp-ui"):
            if not (inputs / specification["providers"][name]["path"]).is_dir():
                raise ValueError(f"Missing pinned source provider: {name}")
    return {"manifest_sha256": digest(manifest_path), "files": len(manifest["files"]),
            "source_archives": [str(below(inputs, value)) for value in manifest.get("source_archives", [])],
            "source_readme": str(below(inputs, manifest["source_readme"])) if manifest.get("source_readme") else None}


def run(argv: list[str]) -> None:
    if argv[0] == "cmake":
        program = shutil.which(os.environ.get("CMAKE", "cmake"))
        if program is None:
            raise ValueError("CMake was not found; install CMake 3.25 or newer")
        argv = [str(Path(program).resolve()), *argv[1:]]
    print("+ " + subprocess.list2cmdline(argv), flush=True)
    subprocess.run(argv, check=True)


def architecture(value: str | None) -> str:
    value = (value or platform.machine()).lower()
    aliases = {"amd64": "x86_64", "x64": "x86_64", "aarch64": "arm64"}
    value = aliases.get(value, value)
    if value not in ("x86_64", "arm64", "universal"):
        raise ValueError("--arch must be x86_64, arm64 or universal")
    if value == "universal" and sys.platform != "darwin":
        raise ValueError("Universal binaries require macOS")
    if sys.platform != "darwin" and value != aliases.get(platform.machine().lower(), platform.machine().lower()):
        raise ValueError("Use a native host matching --arch; cross-compilation is not configured")
    return value


def copy_product(source: Path, destination: Path) -> None:
    if source.resolve() != destination.resolve():
        shutil.copy2(source, destination)


def find_tool(build: Path, name: str, extension: str) -> Path:
    # Explicit target locations, never a recursive build-tree search.
    candidates = (build.parent / "bin" / (name + extension), build / "bin" / (name + extension), build / "powerpc" / (name + extension),
                  build / (name + extension), build / "Release" / (name + extension))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise ValueError(f"CMake did not produce target {name}")


def compiler_runtime_directories(build: Path) -> list[str]:
    export = build / "release-runtime-directories.txt"
    if not export.is_file():
        raise ValueError("CMake compiler runtime directory export is missing: " + str(export))
    return list(dict.fromkeys(str(path.resolve()) for line in export.read_text().splitlines()
                             if line.strip() and (path := Path(line.strip())).is_dir()))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", choices=PRODUCTS, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--inputs-dir", type=Path)
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--arch")
    parser.add_argument("--no-package", action="store_true")
    parser.add_argument("--cmake-definition", action="append", default=[], metavar="NAME=VALUE",
                        help="Additional CPU build definition supplied by a game consumer")
    parser.add_argument("--common-key", type=Path, help="External MM9 package input; never a compilation input")
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    for definition in args.cmake_definition:
        if "=" not in definition or not definition.split("=", 1)[0].replace("_", "").isalnum():
            parser.error("--cmake-definition requires NAME=VALUE")
    arch = architecture(args.arch)
    build = args.build_dir.resolve()
    inputs = args.inputs_dir.resolve(strict=True) if args.inputs_dir else None
    game = args.product != "wiillr"
    identity = verify_inputs(inputs, game)
    binary = build / "bin"
    binary.mkdir(parents=True, exist_ok=True)
    config = {"product": args.product, "arch": arch, "inputs": str(inputs), "version": args.version}
    if args.cmake_definition:
        config["consumer_definitions"] = args.cmake_definition
    marker = build / "build-configuration.json"
    if marker.exists() and json.loads(marker.read_text()) != config:
        raise ValueError("This build directory belongs to a different product/version/input configuration")
    marker.write_text(json.dumps(config, indent=2) + "\n")
    common = ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
              "-DPython3_EXECUTABLE=" + sys.executable,
              "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=" + binary.as_posix(),
              "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=" + binary.as_posix(),
              "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=" + binary.as_posix(),
              "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE=" + binary.as_posix()]
    prefix = os.environ.get("CMAKE_PREFIX_PATH")
    if prefix and os.name != "nt":
        prefix = prefix.replace(os.pathsep, ";")
    if inputs:
        sdk = inputs / "sdk" / (platform.system().lower() + "-" + arch)
        if sdk.is_dir():
            prefix = str(sdk) + (";" + prefix if prefix else "")
    if prefix:
        common.append("-DCMAKE_PREFIX_PATH=" + prefix)
    if sys.platform == "darwin":
        common.append("-DCMAKE_OSX_ARCHITECTURES=" + ("x86_64;arm64" if arch == "universal" else arch))
    cpu = build / "cpu"
    initial = ["-DWIILLR_RELEASE_PRODUCT=" + args.product,
               "-DWIILLR_RELEASE_INPUTS=" + (inputs.as_posix() if inputs else ""),
               "-C", str(ROOT / "cmake/ReleaseBuild.cmake")]
    run(["cmake", "-S", str(ROOT), "-B", str(cpu), *initial, *common,
         *("-D" + definition for definition in args.cmake_definition)])
    targets = ["wiillr-native"] if game else list(TOOLS)
    run(["cmake", "--build", str(cpu), "--config", "Release", "--parallel", str(args.jobs), "--target", *targets])
    extension = ".exe" if os.name == "nt" else ""
    products = {"product": args.product, "version": args.version, "arch": arch,
                "assets": str(binary / "assets"), "dependency_dirs": [str(binary)],
                "licenses_dir": str(build / "licenses"), "input_identity": identity,
                "readme": str(ROOT / "docs" / ("developer-tools.md" if not game else args.product + ".md")),
                "source_archives": identity.get("source_archives", []),
                "source_readme": identity.get("source_readme"),
                "native_exe": None, "gx_library": None, "tool_executables": [], "runtime_libraries": []}
    if game:
        assert inputs is not None
        native = find_tool(cpu, "wiillr-native", extension)
        copy_product(native, binary / ("wiillr-native" + extension))
        graphics = build / "gpu"
        core = inputs / "deps/gc-wii-core"
        if not core.is_dir():
            core = ROOT / "external/gc-wii-core"
        run(["cmake", "-S", str(ROOT / "cmake/gpu"), "-B", str(graphics), *common,
             "-DWIILLR_CORE_SOURCE_DIR=" + core.as_posix(),
             "-DGXRUNTIME_DIR=" + (inputs / "deps/recompcore/GXRuntime").as_posix(),
             "-DWIILLR_RELEASE_BIN_DIR=" + binary.as_posix()])
        run(["cmake", "--build", str(graphics), "--config", "Release", "--parallel", str(args.jobs),
             "--target", "gcnllr-native-gx"])
        library = "gcnllr-native-gx.dll" if os.name == "nt" else "libgcnllr-native-gx." + ("dylib" if sys.platform == "darwin" else "so")
        if not (binary / library).is_file():
            raise ValueError("Native graphics target did not produce " + library)
        products.update(native_exe=str(binary / ("wiillr-native" + extension)), gx_library=str(binary / library))
        cache = (graphics / "CMakeCache.txt").read_text()
        for line in cache.splitlines():
            if line.startswith("Dawn_DIR:"):
                dawn = Path(line.split("=", 1)[1])
                if len(dawn.parents) >= 3:
                    products["dependency_dirs"].append(str(dawn.parents[2] / "bin"))
        products["dependency_dirs"].append(str(graphics / "_deps/dawn_prebuilt-src/bin"))
    else:
        for name in TOOLS:
            source = find_tool(cpu, name, extension)
            destination = binary / (name + extension)
            copy_product(source, destination)
            products["tool_executables"].append(str(destination))
    if prefix:
        for root in prefix.split(";"):
            for name in ("bin", "lib"):
                candidate = Path(root) / name
                if candidate.is_dir():
                    products["dependency_dirs"].append(str(candidate.resolve()))
    # Include compiler-selected multiarch and runtime locations on Debian,
    # Arch and other native toolchains, after explicit dependency prefixes.
    products["dependency_dirs"].extend(compiler_runtime_directories(cpu))
    if game:
        products["dependency_dirs"].extend(compiler_runtime_directories(graphics))
    if os.name == "nt":
        redist = os.environ.get("WIILLR_REDIST_DIR")
        if redist and Path(redist).is_dir():
            products["dependency_dirs"].append(str(Path(redist).resolve()))
        for value in os.environ.get("PATH", "").split(os.pathsep):
            if value and Path(value).is_dir():
                products["dependency_dirs"].append(str(Path(value).resolve()))
    products["dependency_dirs"] = list(dict.fromkeys(products["dependency_dirs"]))
    if game and os.name == "nt":
        # Dawn loads these shader-compilation libraries dynamically; they are
        # absent from its PE import table and must be explicit packaging seeds.
        for name in ("dxcompiler.dll", "dxil.dll"):
            candidates = [Path(value) / name for value in products["dependency_dirs"]]
            companion = next((value for value in candidates if value.is_file()), None)
            if companion is None:
                raise ValueError("Dawn runtime companion was not found: " + name)
            products["runtime_libraries"].append(str(companion.resolve()))
    licenses = build / "licenses"
    licenses.mkdir(exist_ok=True)
    shutil.copy2(ROOT / "LICENSE", licenses / "wiillr-LICENSE")
    if inputs and (inputs / "LICENSES").is_dir():
        shutil.copytree(inputs / "LICENSES", licenses, dirs_exist_ok=True)
    # Record source roots; packaging uses the explicit notices and archives.
    products["source_roots"] = [str(ROOT)] + ([str(inputs / "deps")] if inputs else [])
    products["inputs_dir"] = str(inputs) if inputs else None
    (build / "build-products.json").write_text(json.dumps(products, indent=2) + "\n")
    if not args.no_package:
        command = [sys.executable, str(ROOT / "tools/package_release.py"), "--product", args.product,
                   "--version", args.version, "--build-dir", str(build), "--output-dir", str(args.output_dir.resolve()),
                   "--arch", arch]
        if game:
            command += ["--native-exe", products["native_exe"], "--gx-library", products["gx_library"]]
        if inputs:
            command += ["--inputs-dir", str(inputs)]
        if args.common_key:
            command += ["--common-key", str(args.common_key.resolve(strict=True))]
        run(command)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print("Build failed: " + str(error), file=sys.stderr)
        raise SystemExit(1)

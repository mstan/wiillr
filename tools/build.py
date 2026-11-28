#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a native product using pinned source inputs; never generate title code."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PureWindowsPath
import platform
import re
import tempfile
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from product import load as load_product
from provider_provenance import resolve as resolve_providers, cmake_definitions, verify_cache
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


def verify_inputs(inputs: Path | None, game: bool, catalog_spec: dict | None = None) -> dict:
    if inputs is None:
        if game:
            raise ValueError("Game builds require --inputs-dir with the verified private catalog and donor sources")
        return {}
    specification = json.loads((ROOT / "tools/build-inputs.json").read_text())
    manifest_path = inputs / specification["private_input_manifest"]
    if not manifest_path.is_file():
        raise ValueError("Expected a build-input bundle containing inputs.json. Import NAND/system files in the Wii Menu launcher; see docs/building.md for developer inputs.")
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema") != 1 or not isinstance(manifest.get("files"), dict) or not manifest["files"]:
        raise ValueError("Expected inputs.json schema 1 with a nonempty files/SHA-256 map")
    total = len(manifest["files"])
    print(f"Checking {total:,} build-input files...", flush=True)
    for index, (name, expected) in enumerate(manifest["files"].items(), 1):
        if not isinstance(expected, str) or len(expected) != 64 or digest(below(inputs, name)) != expected:
            raise ValueError(f"Changed build input: {name}")
        if index % 1000 == 0:
            print(f"  Checked {index:,}/{total:,} files", flush=True)
    if game:
        for provider in specification["providers"].values():
            recorded = manifest.get("sources", {}).get(provider["path"], {}).get("commit")
            if recorded != provider["revision"]:
                raise ValueError("Source provider revision differs: " + provider["path"])
        catalog_spec = catalog_spec or specification.get("catalog")
        if not catalog_spec:
            raise ValueError("The consumer must supply its qualified catalog identity")
        receipt = below(inputs, catalog_spec["path"] + "/prepared.json")
        if digest(receipt) != catalog_spec["prepared_sha256"]:
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


def windows_gpu_environment(arch: str) -> tuple[dict[str, str], list[str]]:
    """Use the Windows SDK's C++ ABI for Dawn, independently of the CPU compiler."""
    installer = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not installer.is_file():
        raise ValueError("Windows graphics builds require Visual Studio C++ tools, LLVM and the Windows SDK")
    installations = json.loads(subprocess.check_output(
        [str(installer), "-all", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-format", "json"], text=True))
    candidates = [Path(item["installationPath"]) for item in installations
                  if (Path(item["installationPath"]) / "VC/Tools/Llvm/x64/bin/clang++.exe").is_file()]
    if not candidates:
        raise ValueError("Install Visual Studio's C++ tools, LLVM and Windows SDK for the native graphics build")
    install = candidates[-1]
    compiler = install / "VC/Tools/Llvm/x64/bin"
    setup = install / "VC/Auxiliary/Build" / ("vcvarsamd64_arm64.bat" if arch == "arm64" else "vcvars64.bat")
    environment = dict(os.environ)
    # CPU dependency prefixes may contain MinGW CRT headers and libraries.
    # The native GPU dependency recipe selects its own matching packages.
    environment.pop("CMAKE_PREFIX_PATH", None)
    environment["PATH"] = os.pathsep.join(part for part in environment["PATH"].split(os.pathsep)
                                          if "msys64" not in part.lower() and not any(c in part for c in '\"()'))
    command = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/cmd.exe"
    output = subprocess.check_output(f'"{command}" /d /c ""{setup}" >nul 2>&1 && set"',
                                     text=True, env=environment)
    for line in output.splitlines():
        name, separator, value = line.partition("=")
        if separator and name:
            environment[name.upper()] = value
    cmake_directory = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "CMake/bin"
    if not (cmake_directory / "cmake.exe").is_file():
        cmake_directory = install / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin"
    if not (cmake_directory / "cmake.exe").is_file():
        raise ValueError("Install native Windows CMake for the graphics build")
    ninja = install / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"
    environment["PATH"] = os.pathsep.join(map(str, (compiler, cmake_directory, ninja))) + os.pathsep + environment["PATH"]
    environment["CMAKE"] = str(cmake_directory / "cmake.exe")
    options = ["-DCMAKE_C_COMPILER=" + str(compiler / "clang.exe"),
               "-DCMAKE_CXX_COMPILER=" + str(compiler / "clang++.exe")]
    if arch == "arm64":
        options += ["-DCMAKE_C_COMPILER_TARGET=aarch64-pc-windows-msvc",
                    "-DCMAKE_CXX_COMPILER_TARGET=aarch64-pc-windows-msvc"]
    return environment, options


def run(argv: list[str], environment: dict[str, str] | None = None) -> None:
    if argv[0] == "cmake":
        selection = environment if environment is not None else os.environ
        program = shutil.which(selection.get("CMAKE", "cmake"), path=selection.get("PATH"))
        if program is None:
            raise ValueError("CMake was not found; install CMake 3.25 or newer")
        argv = [str(Path(program).resolve()), *argv[1:]]
    print("+ " + subprocess.list2cmdline(argv), flush=True)
    subprocess.run(argv, check=True, env=environment)


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



def release_version(value: str) -> str:
    if not re.fullmatch(r"[0-9A-Za-z][0-9A-Za-z.+_-]*", value):
        raise argparse.ArgumentTypeError("Use a package version such as 0.1.0 or 0.1.1-local.1 (letters, digits, . + _ -; no spaces or slashes)")
    return value


def project_version() -> str:
    match = re.search(r"project\s*\(\s*wiillr\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", (ROOT / "CMakeLists.txt").read_text(), re.I)
    if not match:
        raise ValueError("The engine CMake project does not define a package version")
    return match.group(1)


def host_name() -> str:
    try:
        return {"win32": "windows", "linux": "linux", "darwin": "macos"}[sys.platform]
    except KeyError:
        raise ValueError("Build on Windows, Linux or macOS") from None


def input_bundle(value: Path | None, build: Path, expected: str | None) -> Path | None:
    """Accept an extracted bundle or import a checksum-pinned ZIP atomically."""
    if value is None:
        if expected:
            raise ValueError("--inputs-sha256 requires --inputs")
        return None
    source = value.resolve(strict=True)
    if source.is_dir():
        if expected:
            raise ValueError("--inputs-sha256 applies to ZIP archives; directory contents are checked by inputs.json")
        return source
    if source.suffix.lower() != ".zip":
        raise ValueError("--inputs expects an extracted build bundle or ZIP, not NAND/game files")
    specification = json.loads((ROOT / "tools/build-inputs.json").read_text())
    expected = (expected or specification.get("archive", {}).get("sha256", "")).lower()
    if not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError("Supply the build archive's --inputs-sha256, or use the archive pinned by this checkout")
    if digest(source) != expected:
        raise ValueError("Build-input ZIP checksum differs from this checkout; use its matching bundle or an explicitly verified --inputs-sha256")
    cache = build / "input-cache"
    for ancestor in (cache, *cache.parents):
        if ancestor.exists() and (ancestor.is_symlink() or
                getattr(ancestor.lstat(), "st_file_attributes", 0) & 0x400):
            raise ValueError("Build-input cache must not traverse links or reparse points")
    destination = cache / expected
    marker = destination / ".wiillr-input-archive"
    if destination.exists():
        if marker.is_file() and marker.read_text().strip() == expected:
            return destination
        raise ValueError("An incomplete input cache exists; choose another build directory")
    cache.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location("wiillr_release_inputs", Path(__file__).with_name("release_inputs.py"))
    release_inputs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(release_inputs)
    print("Importing build-input ZIP into " + str(destination), flush=True)
    with tempfile.TemporaryDirectory(prefix=".import-", dir=cache) as temporary:
        staging = Path(temporary)
        release_inputs.unpack(source, staging, expected)
        (staging / ".wiillr-input-archive").write_text(expected + "\n")
        staging.rename(destination)
    return destination


def preflight(inputs: Path | None, game: bool) -> list[str]:
    missing = []
    for command, label in ((os.environ.get("CMAKE", "cmake"), "CMake 3.25+"), ("ninja", "Ninja")):
        if not shutil.which(command):
            missing.append("Install " + label + " or put its executable on PATH")
    if not any(shutil.which(name) for name in (os.environ.get("CXX", ""), "g++", "clang++", "cl") if name):
        missing.append("Install a C++23 compiler and make it available through CXX/PATH")
    names = ["gc-wii-core", "powerpc-recomp-core", "arm-recomp-core"]
    if game:
        names.append("recomp-ui")
    for name in names:
        bundled = inputs / "deps" / name if inputs else None
        source = bundled if bundled and (bundled / "CMakeLists.txt").is_file() else ROOT / "external" / name
        if not (source / "CMakeLists.txt").is_file():
            missing.append("Missing source component " + name + ": initialize the pinned submodule or supply its source in a build-input bundle")
    return missing


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", default="wiillr", help="Build preset (default: developer tools)")
    parser.add_argument("--version", type=release_version, default=project_version(), help="Package label (default: engine project version)")
    parser.add_argument("--build-dir", type=Path, help="Working files (default: build/PRODUCT/HOST-ARCH)")
    parser.add_argument("--output-dir", type=Path, help="Archives (default: dist)")
    parser.add_argument("--inputs", "--inputs-dir", dest="inputs_dir", type=Path, help="Verified build-input directory or ZIP; not runtime NAND/game data")
    parser.add_argument("--inputs-sha256", help="Expected SHA-256 for a different trusted build-input ZIP")
    parser.add_argument("--check", action="store_true", help="Check inputs and basic build prerequisites without compiling or packaging")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--arch")
    parser.add_argument("--no-package", action="store_true")
    parser.add_argument("--cmake-definition", action="append", default=[], metavar="NAME=VALUE",
                        help="Additional CPU build definition supplied by a game consumer")
    parser.add_argument("--common-key", type=Path, help="External consumer package input; never a compilation input")
    parser.add_argument("--product-file", type=Path, help="Consumer-owned product.json")
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    for definition in args.cmake_definition:
        if "=" not in definition or not definition.split("=", 1)[0].replace("_", "").isalnum():
            parser.error("--cmake-definition requires NAME=VALUE")
    product = load_product(args.product_file, args.product)
    arch = architecture(args.arch)
    build = (args.build_dir or ROOT / "build" / args.product / (host_name() + "-" + arch)).resolve()
    args.output_dir = (args.output_dir or ROOT / "dist").resolve()
    print(f"Product: {args.product}\nPackage version: {args.version}\nBuild directory: {build}\nOutput directory: {args.output_dir}", flush=True)
    game = args.product != "wiillr"
    inputs = input_bundle(args.inputs_dir, build, args.inputs_sha256)
    if args.common_key and not product.get("common_key"):
        parser.error("--common-key is only valid for the internal game product")
    identity = verify_inputs(inputs, game, product.get("catalog"))
    providers = resolve_providers(ROOT, inputs, game, args.cmake_definition)
    missing = preflight(inputs, game)
    missing += ["Missing selected source provider " + name + ": " + item["path"] for name, item in providers.items() if not item["available"]]
    if missing:
        raise ValueError("Build setup is incomplete:\n- " + "\n- ".join(missing))
    gpu_environment, gpu_compiler = (windows_gpu_environment(arch) if game and os.name == "nt" else (None, []))
    if args.check:
        print(json.dumps({"product": product, "profile": "hle", "providers": providers}, indent=2), flush=True)
        print("Build inputs and basic prerequisites are ready. CMake will check platform SDK/library packages during configure.", flush=True)
        return 0
    binary = build / "bin"
    binary.mkdir(parents=True, exist_ok=True)
    config = {"product": args.product, "arch": arch, "inputs": str(inputs), "product_file": product.get("product_file"), "providers": providers}
    if args.cmake_definition:
        config["consumer_definitions"] = args.cmake_definition
    marker = build / "build-configuration.json"
    if marker.exists():
        previous = json.loads(marker.read_text())
        previous.pop("version", None)  # Package labels do not affect compiled objects.
        if previous != config:
            raise ValueError("This build directory belongs to a different product/architecture/input configuration")
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
    if game:
        assert inputs is not None
        graphics = build / "gpu"
        core = Path(providers["gc-wii-core"]["path"])

        def configure_graphics() -> None:
            windows_sdk = (["-DAURORA_SDL3_PROVIDER=package", "-DCMAKE_DISABLE_FIND_PACKAGE_fmt=TRUE",
                            "-DCMAKE_PREFIX_PATH="]
                           if os.name == "nt" else [])
            graphics_common = [value for value in common
                               if os.name != "nt" or not value.startswith("-DCMAKE_PREFIX_PATH=")]
            run(["cmake", "-S", str(ROOT / "cmake/gpu"), "-B", str(graphics), *graphics_common, *gpu_compiler,
                 *windows_sdk, "-DWIILLR_CORE_SOURCE_DIR=" + core.as_posix(),
                 "-DGXRUNTIME_DIR=" + (inputs / "deps/recompcore/GXRuntime").as_posix(),
                 "-DWIILLR_RELEASE_BIN_DIR=" + binary.as_posix()], environment=gpu_environment)

        if os.name == "nt":
            # Native SDL headers work with both compilers; a MinGW include root
            # exposes incompatible CRT headers to the MSVC-ABI GPU compiler.
            configure_graphics()
            sdk_cache = (graphics / "CMakeCache.txt").read_text()
            sdl = next((line.split("=", 1)[1] for line in sdk_cache.splitlines()
                        if line.startswith("SDL3_DIR:")), None)
            if not sdl or not Path(sdl).is_dir():
                raise ValueError("The Windows GPU configure did not provide a shared SDL3 SDK")
            common.append("-DSDL3_DIR=" + sdl)
    cpu = build / "cpu"
    initial = ["-DWIILLR_RELEASE_PRODUCT=" + args.product,
               "-DWIILLR_RELEASE_INPUTS=" + (inputs.as_posix() if inputs else ""),
               "-C", str(ROOT / "cmake/ReleaseBuild.cmake")]
    product_definition = ["-DWIILLR_PRODUCT_CMAKE=" + product["cmake"]] if game else []
    run(["cmake", "-S", str(ROOT), "-B", str(cpu), *initial, *common,
         *("-D" + definition for definition in cmake_definitions(providers)),
         *("-D" + definition for definition in args.cmake_definition), *product_definition])
    verify_cache(cpu / "CMakeCache.txt", providers)
    targets = ["wiillr-native"] if game else list(TOOLS)
    run(["cmake", "--build", str(cpu), "--config", "Release", "--parallel", str(args.jobs), "--target", *targets])
    extension = ".exe" if os.name == "nt" else ""
    products = {"product": args.product, "version": args.version, "arch": arch,
                "assets": str(binary / "assets"), "dependency_dirs": [str(binary)],
                "licenses_dir": str(build / "licenses"), "input_identity": identity,
                "readme": product.get("readme", str(ROOT / "docs/developer-tools.md")),
                "consumer": product, "providers": providers, "build_profile": "hle",
                "source_archives": identity.get("source_archives", []),
                "source_readme": identity.get("source_readme"),
                "native_exe": None, "gx_library": None, "tool_executables": [], "runtime_libraries": []}
    if game:
        assert inputs is not None
        native = find_tool(cpu, "wiillr-native", extension)
        copy_product(native, binary / ("wiillr-native" + extension))
        if os.name != "nt":
            configure_graphics()
        run(["cmake", "--build", str(graphics), "--config", "Release", "--parallel", str(args.jobs),
             "--target", "gcnllr-native-gx"], environment=gpu_environment)
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
    if inputs and (inputs / "LICENSES").is_dir():
        shutil.copytree(inputs / "LICENSES", licenses, dirs_exist_ok=True)
    # Current project terms follow imported notices so inputs cannot replace them.
    shutil.copy2(ROOT / "LICENSE", licenses / "wiillr-GPL3.txt")
    shutil.copy2(ROOT / "LICENSING.md", licenses / "wiillr-LICENSING.md")
    shutil.copy2(ROOT / "LICENSES/PolyForm-Noncommercial-1.0.0.txt",
                 licenses / "PolyForm-Noncommercial-1.0.0.txt")
    if game:
        consumer = Path(product["product_file"]).parent
        for name in ("LICENSE", "LICENSING.md"):
            shutil.copy2(consumer / name, licenses / (product["archive_prefix"] + "-" + name))
    # Record source roots; packaging uses the explicit notices and archives.
    products["source_roots"] = [str(ROOT)] + ([str(Path(product["product_file"]).parent)] if game else []) + ([str(inputs / "deps")] if inputs else [])
    products["inputs_dir"] = str(inputs) if inputs else None
    (build / "build-products.json").write_text(json.dumps(products, indent=2) + "\n")
    if not args.no_package:
        command = [sys.executable, str(ROOT / "tools/package_release.py"), "--product", args.product,
                   "--version", args.version, "--build-dir", str(build), "--output-dir", str(args.output_dir.resolve()),
                   "--arch", arch]
        if game:
            command += ["--product-file", product["product_file"]]
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

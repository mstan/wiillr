#!/usr/bin/env python3
"""Package explicit native build outputs. No game/system data is discovered.

Build metadata lives in build-products.json beside the build: dependency_dirs,
assets, licenses_dir, readme, source_archives and source_readme are explicit
paths. Relative paths are resolved against the build directory. The inputs
directory is only searched at its bin/lib children; it is never inventoried.
Original implementation: MIT, see ../LICENSE.
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

PRODUCTS = {"wiillr": "wiillr", "wii-menu": "Wii Menu", "mega-man-9": "Mega Man 9"}
PREFIXES = {"wiillr": "wiillr", "wii-menu": "WiiMenuRecomp", "mega-man-9": "MegaMan9Recomp"}
WINDOWS_SYSTEM = set((
    "advapi32 avrt bcrypt bcryptprimitives cabinet cfgmgr32 comctl32 comdlg32 "
    "crypt32 cryptbase cryptsp d2d1 d3d11 d3d12 d3dcompiler_47 dbghelp dhcpcsvc "
    "dnsapi dwrite dwmapi dxgi dxguid gdi32 hid imm32 iphlpapi kernel32 kernelbase "
    "mf mfplat mfreadwrite mfuuid mmdevapi mpr msimg32 msvcrt mswsock ncrypt "
    "netapi32 normaliz ntdll ole32 oleacc oleaut32 opengl32 powrprof propsys "
    "psapi rpcrt4 secur32 setupapi shcore shell32 shlwapi ucrtbase urlmon "
    "user32 userenv usp10 uxtheme version winhttp wininet winmm winspool "
    "wintrust ws2_32 wtsapi32"
).split())
ELF_SYSTEM = re.compile(
    r"^(?:ld-linux[^/]*|ld-musl[^/]*|lib(?:c|m|pthread|dl|rt|resolv|util)\.so(?:\..*)?"
    r"|lib(?:GL|GLX|GLdispatch|EGL|OpenGL|vulkan|drm|gbm)\.so(?:\..*)?)$")


class ReleaseError(RuntimeError):
    pass


def require(value, message):
    if not value:
        raise ReleaseError(message)


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def stream_sha(stream) -> str:
    h = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        h.update(block)
    return h.hexdigest()


def run(argv: list[str]) -> str:
    try:
        return subprocess.check_output(argv, text=True, stderr=subprocess.STDOUT)
    except (OSError, subprocess.CalledProcessError) as e:
        raise ReleaseError(f"Command failed: {argv[0]}: {getattr(e, 'output', str(e))}") from e


def tool(*names: str) -> str:
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    raise ReleaseError("Required packaging tool missing: " + " or ".join(names))


def regular(path: Path) -> Path:
    require(path.is_file() and not path.is_symlink(), f"Expected a regular file: {path}")
    return path


def copy_file(source: Path, dest: Path) -> Path:
    regular(source)
    require(not source.is_junction() if hasattr(source, "is_junction") else True,
            f"Linked input is unsupported: {source}")
    if dest.exists():
        require(sha(source) == sha(dest), f"Conflicting package filename: {dest.name}")
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, dest)
    require(sha(source) == sha(dest), f"Copy verification failed: {source}")
    return dest


def copy_tree(source: Path, dest: Path):
    require(source.is_dir() and not source.is_symlink(), f"Missing payload directory: {source}")
    for p in sorted(source.rglob("*")):
        require(not p.is_symlink() and not (hasattr(p, "is_junction") and p.is_junction()),
                f"Linked payload is unsupported: {p}")
        if p.is_file():
            copy_file(p, dest / p.relative_to(source))


def lookup(name: str, directories: list[Path]) -> Path:
    require(Path(name).name == name, f"Dependency is not a filename: {name}")
    for directory in directories:
        if os.name == "nt" and os.environ.get("SystemRoot"):
            if directory.resolve().is_relative_to(Path(os.environ["SystemRoot"]).resolve()):
                continue  # App-local redistributables come from declared SDKs, never System32.
        if not directory.is_dir():
            continue
        direct = directory / name
        if direct.is_file():
            return direct.resolve()
        # PE module names are case insensitive even on a Unix packaging host.
        for p in sorted(directory.iterdir()):
            if p.is_file() and p.name.lower() == name.lower():
                return p.resolve()
    raise ReleaseError(f"Dependency {name} is missing from the declared dependency directories")


class PE:
    """Small bounded PE64 import/export reader; no pefile/objdump dependency."""
    def __init__(self, path: Path, arch: str):
        self.data = regular(path).read_bytes()
        self.path = path
        self.imports: dict[str, list[str | int]] = {}
        self.exports: dict[str | int, str | None] = {}
        self.sections = []
        d = self.data
        require(len(d) >= 64 and d[:2] == b"MZ", f"Not a PE executable: {path}")
        off = self.u32(60)
        require(self.take(off, 4) == b"PE\0\0", f"Invalid PE signature: {path}")
        machine, count = self.unpack("<HH", off + 4)
        require(machine == {"x86_64": 0x8664, "arm64": 0xAA64}[arch], f"Wrong PE architecture: {path}")
        optional_size = self.unpack("<H", off + 20)[0]
        optional = off + 24
        require(self.unpack("<H", optional)[0] == 0x20B and optional_size >= 128,
                f"Expected PE32+ headers: {path}")
        for i in range(count):
            s = optional + optional_size + 40 * i
            virtual_size, rva, raw_size, raw = self.unpack("<IIII", s + 8)
            self.sections.append((rva, max(virtual_size, raw_size), raw, raw_size))
        export_rva, export_size = self.unpack("<II", optional + 112)
        import_rva, _ = self.unpack("<II", optional + 120)
        if import_rva:
            n = 0
            while True:
                require(n < 4096, f"Excessive PE imports: {path}")
                ilt, stamp, chain, module, iat = self.unpack("<IIIII", self.offset(import_rva + n * 20))
                if not any((ilt, stamp, chain, module, iat)):
                    break
                dll = self.string(module).lower()
                names = []
                for i in range(65536):
                    value = self.unpack("<Q", self.offset((ilt or iat) + i * 8))[0]
                    if not value:
                        break
                    names.append(value & 0xFFFF if value & (1 << 63) else self.string(value + 2))
                else:
                    raise ReleaseError(f"Unterminated PE imports: {path}")
                self.imports.setdefault(dll, []).extend(names)
                n += 1
        if export_rva:
            e = self.offset(export_rva)
            base, functions, names, address_table, name_table, ordinal_table = self.unpack("<IIIIII", e + 16)
            require(functions < 1000000 and names <= functions, f"Invalid PE exports: {path}")
            for i in range(functions):
                value = self.u32(self.offset(address_table + i * 4))
                if value:
                    self.exports[base + i] = self.string(value) if export_rva <= value < export_rva + export_size else None
            for i in range(names):
                name = self.string(self.u32(self.offset(name_table + i * 4)))
                ordinal = base + self.unpack("<H", self.offset(ordinal_table + i * 2))[0]
                require(ordinal in self.exports, f"Invalid PE named export: {path}")
                self.exports[name] = self.exports[ordinal]

    def take(self, offset, size):
        require(0 <= offset <= len(self.data) - size, f"Truncated PE file: {self.path}")
        return self.data[offset:offset + size]

    def unpack(self, fmt, offset):
        return struct.unpack(fmt, self.take(offset, struct.calcsize(fmt)))

    def u32(self, offset):
        return self.unpack("<I", offset)[0]

    def offset(self, rva):
        for start, size, raw, raw_size in self.sections:
            if start <= rva < start + size:
                require(rva - start < raw_size, f"PE RVA has no file backing: {self.path}")
                return raw + rva - start
        raise ReleaseError(f"Unmapped PE RVA in {self.path}")

    def string(self, rva):
        off = self.offset(rva)
        end = self.data.find(b"\0", off, min(off + 4096, len(self.data)))
        require(end >= off, f"Unterminated PE name: {self.path}")
        return self.data[off:end].decode("ascii")


def system_dll(name: str) -> bool:
    return name.startswith(("api-ms-win-", "ext-ms-win-")) or name.removesuffix(".dll") in WINDOWS_SYSTEM


def windows_closure(seeds: list[Path], dest: Path, directories: list[Path], arch: str) -> dict:
    modules: dict[str, PE] = {}
    pending = list(seeds)
    while pending:
        path = pending.pop(0)
        name = path.name.lower()
        if name in modules:
            continue
        pe = PE(path, arch)
        modules[name] = pe
        dependencies = set(pe.imports)
        dependencies.update(f.rsplit(".", 1)[0].lower().removesuffix(".dll") + ".dll" for f in pe.exports.values() if f)
        for dll in sorted(dependencies):
            if system_dll(dll) or dll in modules:
                continue
            source = lookup(dll, [dest, *directories])
            target = copy_file(source, dest / source.name)
            pending.append(target)
    checked = 0
    def exported(dll, symbol, seen):
        require((dll, symbol) not in seen, f"Cyclic forwarded export: {dll}!{symbol}")
        if system_dll(dll):
            return
        require(dll in modules and symbol in modules[dll].exports, f"Missing import {dll}!{symbol}")
        forward = modules[dll].exports[symbol]
        if forward:
            module, target = forward.rsplit(".", 1)
            exported(module.lower().removesuffix(".dll") + ".dll", int(target[1:]) if target.startswith("#") else target, seen | {(dll, symbol)})
    for pe in modules.values():
        for dll, names in pe.imports.items():
            for name in names:
                exported(dll, name, set())
                checked += not system_dll(dll)
    return {"modules": len(modules), "bundled_symbols_checked": checked,
            "system_dependencies": sorted({d for pe in modules.values() for d in pe.imports if system_dll(d)})}


def elf_info(path: Path, arch: str) -> tuple[list[str], list[str]]:
    with path.open("rb") as f:
        head = f.read(20)
    require(head[:6] == b"\x7fELF\x02\x01" and len(head) == 20, f"Expected little-endian ELF64: {path}")
    require(struct.unpack_from("<H", head, 18)[0] == {"x86_64": 62, "arm64": 183}[arch], f"Wrong ELF architecture: {path}")
    text = run([tool("readelf", "llvm-readelf"), "-d", str(path)])
    needed = re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", text)
    paths = re.findall(r"\((?:RUNPATH|RPATH)\).*?\[([^\]]*)\]", text)
    return needed, [p for item in paths for p in item.split(":") if p]


def linux_closure(seeds: list[Path], dest: Path, directories: list[Path], arch: str) -> dict:
    binaries = []; pending = list(seeds); seen = set(); system = set()
    while pending:
        p = pending.pop(0)
        if p in seen:
            continue
        seen.add(p); binaries.append(p)
        needed, _ = elf_info(p, arch)
        for name in needed:
            if ELF_SYSTEM.fullmatch(name):
                system.add(name); continue
            source = lookup(name, [dest, *directories])
            target = copy_file(source, dest / name)
            if target not in seen:
                pending.append(target)
    # Every consumer can find the sibling lib directory from its own location.
    if any(dest.iterdir()):
        patcher = tool("patchelf")
        for p in binaries:
            relative = os.path.relpath(dest, p.parent).replace(os.sep, "/")
            origin = "$ORIGIN" + ("/" + relative if relative != "." else "")
            run([patcher, "--set-rpath", origin, str(p)])
            needed, paths = elf_info(p, arch)
            require(paths == [origin], f"ELF relocation did not stick: {p}")
            require(all(ELF_SYSTEM.fullmatch(n) or (dest / n).is_file() for n in needed), f"Unresolved ELF closure: {p}")
    return {"modules": len(binaries), "system_dependencies": sorted(system)}


def macho_closure(seeds: list[Path], dest: Path, directories: list[Path], arch: str) -> dict:
    require(sys.platform == "darwin", "macOS packaging requires native otool/install_name_tool/codesign")
    binaries = []; pending = list(seeds); seen = set(); dependencies = {}; origins = {}
    def inspect(p):
        arches = run([tool("lipo"), "-archs", str(p)]).split()
        required = {"x86_64", "arm64"} if arch == "universal" else {arch}
        require(required.issubset(arches), f"Wrong Mach-O architecture: {p}")
        return [line.strip().split(" (compatibility", 1)[0] for line in run([tool("otool"), "-L", str(p)]).splitlines()[1:] if line.strip()]
    while pending:
        p = pending.pop(0)
        if p in seen:
            continue
        seen.add(p); binaries.append(p)
        refs = inspect(p); dependencies[p] = []
        for ref in refs:
            if ref.startswith(("/System/Library/", "/usr/lib/")):
                continue
            name = PurePosixPath(ref).name
            # A dylib's own install name is not a dependency.
            if name == p.name and p.suffix == ".dylib":
                continue
            absolute = Path(ref)
            if absolute.is_absolute() and absolute.is_file() and any(absolute.resolve().is_relative_to(d.resolve()) for d in directories):
                source = absolute.resolve()
            else:
                source = lookup(name, [dest, *directories])
            target = copy_file(source, dest / name)
            origins[target] = source
            dependencies[p].append((ref, target))
            pending.append(target)
    changer = tool("install_name_tool")
    for p in binaries:
        if p.suffix == ".dylib":
            run([changer, "-id", "@rpath/" + p.name, str(p)])
        for old, target in dependencies[p]:
            relative = os.path.relpath(target, p.parent).replace(os.sep, "/")
            run([changer, "-change", old, "@loader_path/" + relative, str(p)])
        # Remove build-machine paths; all rewritten dependencies are loader-relative.
        load = run([tool("otool"), "-l", str(p)])
        for path in re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", load):
            run([changer, "-delete_rpath", path, str(p)])
        for ref in inspect(p):
            require(ref.startswith(("/System/Library/", "/usr/lib/", "@loader_path/", "@rpath/")), f"Absolute Mach-O dependency remains: {ref}")
            if ref.startswith("@loader_path/"):
                require((p.parent / ref[len("@loader_path/"):]).is_file(), f"Unresolved Mach-O dependency: {ref}")
    # Dependency rewrites invalidate existing signatures. Sign deepest items first.
    for p in reversed(binaries):
        run([tool("codesign"), "--force", "--sign", "-", str(p)])
    return {"modules": len(binaries), "signature": "ad-hoc; not notarized"}


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
    parser.add_argument("--product", choices=PRODUCTS, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    for name in ("build-dir", "output-dir"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--inputs-dir", type=Path)
    host = {"win32": "windows", "linux": "linux", "darwin": "macos"}.get(sys.platform)
    parser.add_argument("--platform", choices=("windows", "linux", "macos"), default=host)
    parser.add_argument("--arch", choices=("x86_64", "arm64", "universal"), required=True)
    parser.add_argument("--native-exe", type=Path)
    parser.add_argument("--gx-library", type=Path)
    parser.add_argument("--common-key", type=Path, help="Explicit private MM9 runtime input; never discovered or included by default")
    args = parser.parse_args(argv)
    require(args.platform is not None, "Supply --platform for this packaging host")
    require(re.fullmatch(r"[0-9A-Za-z][0-9A-Za-z.+_-]*", args.version), "Invalid release version")
    repo = args.repo.resolve(); build = args.build_dir.resolve(); output = args.output_dir.resolve()
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
    require(not args.common_key or args.product == "mega-man-9", "--common-key is only valid for Mega Man 9")
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
    artifact = output / f"{PREFIXES[args.product]}-{args.platform}-{args.arch}-v{args.version}{suffix}"
    with tempfile.TemporaryDirectory(prefix=f".{args.product}-stage-", dir=output) as scratch:
        title = PRODUCTS[args.product]
        stage = Path(scratch) / title
        app = stage / (title + ".app")
        if game:
            runtime = app / "Contents/MacOS" if args.platform == "macos" else stage
            runtime.mkdir(parents=True)
            name = title + (".exe" if args.platform == "windows" else "")
            exe = copy_file(args.native_exe, runtime / name)
            gx = copy_file(args.gx_library, runtime / args.gx_library.name)
            assets = path(products.get("assets", "bin/assets")); copy_tree(assets, runtime / "assets")
            menu = args.product == "wii-menu"
            config = (f"[Game]\nName={title}\n" + ("Nand=seed/nand\nBoot=title/00000001/00000002/content/00000098.app\n" if menu else "Import=wad\nBoot=title/00010001/57523945/content/0000000a.app\n") + f"Graphics={gx.name}\nDspRom=hle\nDspCoef=hle\nUid=4096\nGid={1 if menu else 12344}\nIos={80 if menu else 37}\n")
            (runtime / "game.ini").write_text(config, encoding="utf-8")
            if args.common_key:
                copy_file(args.common_key, runtime / "system/wii-common-key.bin")
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
                plist = {"CFBundleName": title, "CFBundleDisplayName": title, "CFBundleExecutable": name, "CFBundleIdentifier": "org.retroportingtoolkit." + args.product, "CFBundlePackageType": "APPL", "CFBundleShortVersionString": args.version, "CFBundleVersion": args.version, "LSMinimumSystemVersion": "11.0", "NSHighResolutionCapable": True}
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

#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Developer commands for inspecting inputs and preparing Wii recompilations."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
COMMANDS = {
    "inputs": ("wii_inputs.py", "Inspect an original WAD or extracted NAND."),
    "discover": ("wii.py", "Run bounded discovery with explicit execution accounting."),
    "prepare": ("prepare_ppc.py", "Compile verified original code pages."),
    "install-wad": ("install_wad.py", "Verify and install a WAD into an owned NAND copy."),
    "install-ios": ("install_nand_dependency.py", "Copy a verified IOS title into an owned NAND copy."),
}


def version() -> str:
    path = ROOT / "VERSION"
    return path.read_text(encoding="utf-8").strip() if path.is_file() else "development"


def binary_directory() -> Path:
    if ROOT.name == "wiillr" and ROOT.parent.name == "lib" and (ROOT / "VERSION").is_file():
        return ROOT.parent.parent / "bin"
    return ROOT / "bin"


def option(arguments: list[str], name: str, default: str | None = None) -> str | None:
    for index, argument in enumerate(arguments):
        if argument.startswith(name + "="):
            return argument[len(name) + 1:]
        if argument == name:
            return arguments[index + 1] if index + 1 < len(arguments) else default
    return default


def has_option(arguments: list[str], name: str) -> bool:
    return any(value == name or value.startswith(name + "=") for value in arguments)


def bundled_binary(name: str) -> Path:
    directory = binary_directory()
    names = (name + ".exe", name) if sys.platform == "win32" else (name, name + ".exe")
    for filename in names:
        candidate = directory / filename
        if candidate.is_file():
            return candidate
    raise ValueError(f"{name} is not bundled. Build it or supply its explicit path.")


def command_arguments(command: str, arguments: list[str]) -> list[str]:
    script = ROOT / "tools" / COMMANDS[command][0]
    if not script.is_file():
        raise ValueError(f"The developer bundle is missing {script.name}.")
    forwarded = list(arguments)
    # Help belongs to the original command and needs no native binary.
    if not any(value in ("-h", "--help") for value in forwarded):
        if command == "prepare" and not has_option(forwarded, "--compiler"):
            forwarded += ["--compiler", str(bundled_binary("dolrecomp"))]
        elif command == "discover" and not has_option(forwarded, "--executable"):
            processor = option(forwarded, "--processor", "ppc")
            mode = option(forwarded, "--ppc-mode", "discovery")
            name = "wiillr-starlet-discover" if processor == "starlet" else {
                "discovery": "wiillr-discover",
                "static": "wiillr-native",
                "mixed-discovery": "wiillr-native-discover",
            }.get(mode, "wiillr-discover")
            forwarded += ["--executable", str(bundled_binary(name))]
    return [sys.executable, "-B", str(script), *forwarded]


def main(arguments: list[str] | None = None) -> int:
    if sys.version_info < (3, 10):
        print("wiillr developer tools require Python 3.10 or newer.", file=sys.stderr)
        return 2
    descriptions = "\n".join(f"  {name:13} {description}" for name, (_, description) in COMMANDS.items())
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=descriptions + "\n\nUse wiillr COMMAND --help for the existing command's options.")
    parser.add_argument("--version", action="version", version="wiillr " + version())
    parser.add_argument("command", choices=COMMANDS, nargs="?")
    parser.add_argument("arguments", nargs=argparse.REMAINDER, help=argparse.SUPPRESS)
    args = parser.parse_args(arguments)
    if args.command is None:
        parser.print_help()
        return 0
    try:
        return subprocess.call(command_arguments(args.command, args.arguments))
    except (OSError, ValueError) as error:
        print(f"wiillr: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())

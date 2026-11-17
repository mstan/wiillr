# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 Matthew Stanley
"""Produce a private, version-verified Wii Menu listing with upstream annotations."""
from __future__ import annotations

import argparse
from bisect import bisect_right
import hashlib
import json
from pathlib import Path
import re
import struct
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "annotations/wii-menu-43u.json"


def verified_inputs(directory: Path, identity: dict) -> dict[str, str]:
    files = {}
    for relative, expected in identity["upstream"]["files"].items():
        data = (directory / relative).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError("Annotation source differs: " + relative)
        files[relative] = data.decode("utf-8")
    expected_dol = files["config/43U/build.sha1"].split()[0]
    if expected_dol != identity["original_dol_sha1"]:
        raise ValueError("Upstream executable identity differs")
    return files


def parse_symbols(text: str) -> list[dict]:
    symbols = []
    pattern = re.compile(r"^(.+?) = (\S+):0x([\da-fA-F]+);\s*//\s*(.*)$")
    for number, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("//"):
            continue
        match = pattern.fullmatch(line)
        if not match:
            raise ValueError(f"Unrecognized symbol record at line {number}")
        name, section, address, attributes = match.groups()
        fields = dict(re.findall(r"(\w+):([^\s]+)", attributes))
        kind = fields.get("type")
        if kind not in ("function", "object", "label"):
            raise ValueError(f"Unknown symbol type at line {number}: {kind}")
        symbols.append(dict(name=name, section=section, address=int(address, 16),
                            size=int(fields.get("size", "0"), 0), kind=kind,
                            source_line=number))
    return sorted(symbols, key=lambda item: (item["address"], item["name"]))


def parse_splits(text: str) -> list[dict]:
    source = None
    ranges = []
    for line in text.splitlines():
        if line and not line[0].isspace() and line.endswith(":"):
            source = line[:-1] if line != "Sections:" else None
        match = re.fullmatch(r"\s*(\S+)\s+start:0x([\da-fA-F]+)\s+end:0x([\da-fA-F]+).*", line)
        if match:
            section, begin, end = match.groups()
            begin, end = int(begin, 16), int(end, 16)
            if not source or begin >= end:
                raise ValueError("Invalid source split: " + line)
            ranges.append(dict(section=section, start=begin, end=end, source=source))
    return sorted(ranges, key=lambda item: item["start"])


def describe(name: str) -> str:
    """Display-only CodeWarrior namespace decoding; retain the exact raw label."""
    if "__Q" not in name:
        return name
    method, qualified = name.rsplit("__Q", 1)
    if not qualified or not qualified[0].isdigit():
        return name
    count, rest, parts = int(qualified[0]), qualified[1:], []
    for _ in range(count):
        length = re.match(r"(\d+)([A-Za-z_])", rest)
        if not length:
            return name
        size = int(length[1]); rest = rest[len(length[1]):]
        if size > len(rest):
            return name
        parts.append(rest[:size]); rest = rest[size:]
    if not parts:
        return name
    method = {"__ct": parts[-1], "__dt": "~" + parts[-1]}.get(method, method)
    return "::".join(parts + [method])


def dol_sections(data: bytes, expected_sha1: str) -> list[dict]:
    if hashlib.sha1(data).hexdigest() != expected_sha1:
        raise ValueError("Wrong executable: expected the pinned original Wii Menu 4.3U DOL")
    if len(data) < 0x100:
        raise ValueError("Truncated DOL")
    word = lambda at: struct.unpack_from(">I", data, at)[0]
    sections = []
    for index in range(18):
        offset, address, size = word(index * 4), word(0x48 + index * 4), word(0x90 + index * 4)
        if not size:
            continue
        if offset < 0x100 or offset + size > len(data) or address + size > 0x100000000:
            raise ValueError("DOL section outside original file/address space")
        if index < 7 and (address | size) & 3:
            raise ValueError("Unaligned DOL text")
        if any(address < old["address"] + old["size"] and old["address"] < address + size
               for old in sections):
            raise ValueError("Overlapping DOL sections")
        sections.append(dict(index=index, offset=offset, address=address, size=size, code=index < 7))
    return sorted(sections, key=lambda item: item["address"])


def build_index(symbols: list[dict], splits: list[dict], identity: dict) -> list[dict]:
    notes = {item["symbol"]: item["comment"] for item in identity["notes"]}
    missing = set(notes) - {item["name"] for item in symbols}
    if missing:
        raise ValueError("Notes reference missing symbols: " + ", ".join(sorted(missing)))
    starts = [item["start"] for item in splits]
    result = []
    for symbol in symbols:
        item = dict(symbol, display_name=describe(symbol["name"]))
        index = bisect_right(starts, item["address"]) - 1
        if index >= 0:
            split = splits[index]
            if item["address"] < split["end"] and item["section"] == split["section"]:
                item["source"] = split["source"]
        if item["name"] in notes:
            item["comment"] = notes[item["name"]]
        result.append(item)
    return result


def module_sections(sections: list[dict], splits: list[dict], module: str | None) -> list[dict]:
    if module is None:
        return sections
    ranges = [item for item in splits if item["source"] == module]
    if not ranges:
        raise ValueError("Unknown source module in pinned splits: " + module)
    result = []
    for section in sections:
        for span in ranges:
            begin = max(section["address"], span["start"])
            end = min(section["address"] + section["size"], span["end"])
            if begin < end:
                result.append(dict(section, address=begin, size=end - begin,
                                   offset=section["offset"] + begin - section["address"]))
    return sorted(result, key=lambda item: item["address"])


def verify_structure_fields(data: bytes, sections: list[dict], identity: dict) -> None:
    for field in identity.get("structures", []):
        proof = field.get("verification")
        if not proof:
            continue
        address = int(proof["pc"], 0)
        section = next((item for item in sections if item["address"] <= address and
                        address + 4 <= item["address"] + item["size"]), None)
        if section is None:
            raise ValueError("Structure accessor outside original executable")
        offset = section["offset"] + address - section["address"]
        word = int.from_bytes(data[offset:offset + 4], "big")
        if word != int(proof["instruction"], 0) or word & 0xFFFF != int(field["offset"], 0):
            raise ValueError("Structure field/accessor proof differs: " + field["field"])


def annotate(data: bytes, sections: list[dict], symbols: list[dict], output: Path) -> dict:
    try:
        import capstone
    except ImportError as error:
        raise ValueError("Install the optional listing dependency: pip install capstone==5.0.7") from error
    decoder = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    by_address = {}
    for item in symbols:
        by_address.setdefault(item["address"], []).append(item)
    functions = [item for item in symbols if item["kind"] == "function" and item["size"]]
    starts = sorted({item["address"] for item in functions})
    ends = {start: max(item["address"] + item["size"] for item in by_address[start]
                      if item["kind"] == "function") for start in starts}
    # A data label can lie inside a function's extent (for example a literal
    # pool). Honor its entire span, rather than decoding only past its first word.
    objects = sorted((item["address"], item["address"] + item["size"])
                     for item in symbols if item["kind"] == "object" and item["size"])
    object_ranges = []
    for begin, end in objects:
        if object_ranges and begin <= object_ranges[-1][1]:
            object_ranges[-1] = (object_ranges[-1][0], max(end, object_ranges[-1][1]))
        else:
            object_ranges.append((begin, end))
    object_starts = [item[0] for item in object_ranges]
    counts = dict(decoded_instructions=0, undecoded_function_words=0, unclassified_text_words=0)
    with (output / "wii-menu-43u.asm").open("x", encoding="utf-8", newline="\n") as listing:
        listing.write("; Original Wii Menu 4.3U. Read-only annotated listing; not an assembler input.\n"
                      "; Names/ranges are upstream annotations. Unclassified or unsupported words stay explicit.\n")
        for section in sections:
            # This original DOL puts most executable bytes in a data-labelled
            # initialized section. Verified function ranges identify those bytes;
            # the header's text/data flag alone is not a code classification.
            has_functions = any(item["address"] < section["address"] + section["size"] and
                                section["address"] < item["address"] + item["size"] for item in functions)
            if not section["code"] and not has_functions:
                continue
            header_kind = "text" if section["code"] else "data"
            listing.write(f"\n; DOL initialized section {section['index']} at 0x{section['address']:08X} (header: {header_kind})\n")
            for offset in range(0, section["size"], 4):
                address = section["address"] + offset
                labels = by_address.get(address, [])
                for item in labels:
                    listing.write(f"\n; {item['display_name']} [{item['kind']}, size 0x{item['size']:X}]\n")
                    if "source" in item:
                        listing.write("; source: " + item["source"] + "\n")
                    if "comment" in item:
                        listing.write("; " + item["comment"] + "\n")
                    listing.write(item["name"] + ":\n")
                raw = data[section["offset"] + offset:section["offset"] + offset + 4]
                i = bisect_right(starts, address) - 1
                in_function = i >= 0 and address < ends[starts[i]]
                object_index = bisect_right(object_starts, address + 3) - 1
                object_here = object_index >= 0 and address < object_ranges[object_index][1]
                instructions = list(decoder.disasm_lite(raw, address)) if in_function and not object_here else []
                if instructions:
                    _, _, mnemonic, operands = instructions[0]
                    text = (mnemonic + " " + operands).rstrip()
                    if mnemonic.startswith("b"):
                        targets = re.findall(r"0x([\da-fA-F]+)", operands)
                        if targets and int(targets[-1], 16) in by_address:
                            text += " ; " + by_address[int(targets[-1], 16)][0]["display_name"]
                    counts["decoded_instructions"] += 1
                else:
                    reason = "undecoded function word" if in_function and not object_here else "unclassified/data text word"
                    text = f".long 0x{int.from_bytes(raw, 'big'):08X} ; {reason}"
                    counts["undecoded_function_words" if in_function and not object_here else "unclassified_text_words"] += 1
                listing.write(f"  {address:08X}  {raw.hex().upper()}  {text}\n")
    return dict(counts, decoder="capstone " + capstone.__version__)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    fetch = commands.add_parser("fetch", help="Fetch only pinned public symbol/split metadata")
    fetch.add_argument("--output", type=Path, required=True)
    run = commands.add_parser("annotate", help="Read an original DOL and create a new private listing directory")
    run.add_argument("--dol", type=Path, required=True)
    run.add_argument("--annotations", type=Path, required=True)
    run.add_argument("--output", type=Path, required=True)
    run.add_argument("--module", help="Limit the instruction listing to one exact source path from splits.txt")
    args = parser.parse_args(argv)
    identity = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if args.output.exists():
        parser.error("Output already exists; choose a new directory to preserve annotations")
    if args.command == "annotate":
        destination = args.output.resolve()
        if destination.is_relative_to(ROOT) and not destination.is_relative_to(ROOT / "private"):
            parser.error("Keep original-instruction listings under private/ or outside the source checkout")
    if args.command == "fetch":
        payloads = {}
        for relative, expected in identity["upstream"]["files"].items():
            url = ("https://raw.githubusercontent.com/koopthekoopa/wii-ipl/" +
                   identity["upstream"]["revision"] + "/" + relative)
            with urlopen(url, timeout=30) as response:
                data = response.read(4 * 1024 * 1024 + 1)
            if len(data) > 4 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != expected:
                raise ValueError("Downloaded annotation identity differs: " + relative)
            payloads[relative] = data
        args.output.mkdir(parents=True)
        for relative, data in payloads.items():
            path = args.output / relative; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
        print("Fetched verified public annotations: " + str(args.output))
        return
    data = args.dol.read_bytes()
    sections = dol_sections(data, identity["original_dol_sha1"])
    verify_structure_fields(data, sections, identity)
    files = verified_inputs(args.annotations, identity)
    splits = parse_splits(files["config/43U/splits.txt"])
    symbols = build_index(parse_symbols(files["config/43U/symbols.txt"]), splits, identity)
    sections = module_sections(sections, splits, args.module)
    args.output.mkdir(parents=True)
    (args.output / "symbols.json").write_text(json.dumps(dict(identity=identity, symbols=symbols), indent=2) + "\n", encoding="utf-8")
    with (args.output / "channel-grid.md").open("x", encoding="utf-8") as index:
        index.write("# Wii Menu 4.3U channel-grid annotations\n\nVerified original executable SHA-1: `" + identity["original_dol_sha1"] + "`.\n\n")
        for item in symbols:
            if "comment" in item:
                index.write(f"- `0x{item['address']:08X}` **{item['display_name']}**: {item['comment']}\n")
        index.write("\n## Structure notes\n\n")
        for field in identity.get("structures", []):
            index.write(f"- **{field['type']}::{field['field']}** at `{field['offset']}`: {field['comment']}\n")
    result = annotate(data, sections, symbols, args.output)
    result.update(symbols=len(symbols), functions=sum(item["kind"] == "function" for item in symbols),
                  source_module=args.module,
                  listing_functions=sum(item["kind"] == "function" and any(
                      section["address"] <= item["address"] < section["address"] + section["size"]
                      for section in sections) for item in symbols),
                  original_dol_sha1=identity["original_dol_sha1"], source_unchanged=hashlib.sha1(args.dol.read_bytes()).hexdigest() == identity["original_dol_sha1"])
    (args.output / "listing.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()

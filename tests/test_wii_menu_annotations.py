# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 Matthew Stanley
import hashlib
import importlib.util
from contextlib import redirect_stderr
import io
from pathlib import Path
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location("menu_annotations", Path(__file__).parents[1] / "tools/annotate_wii_menu.py")
annotations = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(annotations)


class MenuAnnotations(unittest.TestCase):
    def test_aliases_and_data_are_not_promoted(self):
        symbols = annotations.parse_symbols(
            "entry = .text:0x81330000; // type:function size:0x10\n"
            "alias = .text:0x81330000; // type:function size:0x10\n"
            "pool = .text:0x81330004; // type:object size:0x8\n")
        self.assertEqual(len(symbols), 3)
        self.assertEqual([x["kind"] for x in symbols], ["function", "function", "object"])
        self.assertEqual(annotations.describe("calcCommon__Q33ipl5scene13ChannelSelectFv"),
                         "ipl::scene::ChannelSelect::calcCommon")

    def test_unknown_classification_rejects(self):
        with self.assertRaises(ValueError):
            annotations.parse_symbols("mystery = .text:0x81330000; // type:guess size:4\n")

    def test_source_split_and_our_notes_match_exact_symbol(self):
        splits = annotations.parse_splits("Sections:\n .text type:code align:4\nsrc/scene.cpp:\n .text start:0x81330000 end:0x81330010\n")
        symbols = annotations.parse_symbols("entry = .text:0x81330000; // type:function size:0x10\n")
        index = annotations.build_index(symbols, splits, {"notes": [{"symbol": "entry", "comment": "verified"}]})
        self.assertEqual(index[0]["source"], "src/scene.cpp")
        self.assertEqual(index[0]["comment"], "verified")
        with self.assertRaises(ValueError):
            annotations.build_index(symbols, splits, {"notes": [{"symbol": "missing", "comment": "bad"}]})

    def test_wrong_executable_is_rejected_before_decode(self):
        with self.assertRaisesRegex(ValueError, "Wrong executable"):
            annotations.dol_sections(b"incorrect executable", "0" * 40)

    def test_existing_annotation_directory_is_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp); note = output / "notes.txt"; note.write_text("Keep my annotations")
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                annotations.main(["fetch", "--output", str(output)])
            self.assertEqual(error.exception.code, 2)
            self.assertEqual(note.read_text(), "Keep my annotations")

    def test_focused_module_preserves_original_file_address_mapping(self):
        sections = [{"index": 7, "address": 0x1000, "offset": 0x100, "size": 0x100, "code": False}]
        splits = [{"source": "scene.cpp", "start": 0x1020, "end": 0x1040}]
        selected = annotations.module_sections(sections, splits, "scene.cpp")
        self.assertEqual((selected[0]["address"], selected[0]["offset"], selected[0]["size"]), (0x1020, 0x120, 0x20))
        with self.assertRaises(ValueError):
            annotations.module_sections(sections, splits, "missing.cpp")

    def test_structure_comment_cannot_override_original_accessor(self):
        sections = [{"address": 0x813478FC, "offset": 0, "size": 4}]
        identity = {"structures": [{"field": "mpPane", "offset": "0x94",
            "verification": {"pc": "0x813478FC", "instruction": "0x80630094"}}]}
        annotations.verify_structure_fields(bytes.fromhex("80630094"), sections, identity)
        identity["structures"][0]["offset"] = "0x88"
        with self.assertRaises(ValueError):
            annotations.verify_structure_fields(bytes.fromhex("80630094"), sections, identity)

    def test_changed_annotation_input_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "symbols.txt"; path.write_bytes(b"changed")
            identity = {"upstream": {"files": {"symbols.txt": hashlib.sha256(b"original").hexdigest()}}}
            with self.assertRaisesRegex(ValueError, "Annotation source differs"):
                annotations.verified_inputs(Path(tmp), identity)

    def test_entire_embedded_data_span_stays_data(self):
        if importlib.util.find_spec("capstone") is None:
            self.skipTest("optional Capstone decoder unavailable")
        symbols = annotations.parse_symbols(
            "entry = .text:0x81330000; // type:function size:0x10\n"
            "pool = .text:0x81330004; // type:object size:0x8\n")
        symbols = annotations.build_index(symbols, [], {"notes": []})
        # Match the real Menu's unusual header: original functions also reside
        # in a data-labelled DOL section. The verified overlay wins in both cases.
        for header_code in (True, False):
            with self.subTest(header_code=header_code), tempfile.TemporaryDirectory() as tmp:
                output = Path(tmp)
                result = annotations.annotate(bytes.fromhex("60000000") * 4,
                    [{"index": 0, "offset": 0, "address": 0x81330000, "size": 16, "code": header_code}], symbols, output)
                text = (output / "wii-menu-43u.asm").read_text()
            self.assertEqual(result["decoded_instructions"], 2)
            self.assertEqual(result["unclassified_text_words"], 2)
            self.assertIn("81330008  60000000  .long", text)


if __name__ == "__main__":
    unittest.main()

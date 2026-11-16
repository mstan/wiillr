# SPDX-License-Identifier: MIT
"""Build-input confinement and catalog-free developer graph contracts."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release_build", ROOT / "tools/build.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class BuildDriver(unittest.TestCase):
    def test_private_input_integrity_and_confinement(self):
        with tempfile.TemporaryDirectory() as directory:
            inputs = Path(directory).resolve()
            sample = inputs / "source.c"
            sample.write_bytes(b"original source\n")
            manifest = inputs / "inputs.json"
            manifest.write_text(json.dumps({"schema": 1, "files": {"source.c": build.digest(sample)}}))
            self.assertEqual(build.verify_inputs(inputs, False)["files"], 1)
            sample.write_bytes(b"changed source\n")
            with self.assertRaisesRegex(ValueError, "Changed build input"):
                build.verify_inputs(inputs, False)
            manifest.write_text(json.dumps({"schema": 1, "files": {"../outside.c": "0" * 64}}))
            with self.assertRaisesRegex(ValueError, "Invalid input path"):
                build.verify_inputs(inputs, False)
            sub = inputs / "generated" / "chunks"
            sub.mkdir(parents=True)
            (sub / "page.c").write_bytes(b"source")
            self.assertEqual(build.below(inputs, "generated\\chunks\\page.c"), sub / "page.c")
            for invalid in ("C:\\outside.c", "..\\outside.c", "/outside.c"):
                with self.assertRaises(ValueError):
                    build.below(inputs, invalid)

    def test_games_require_qualified_inputs_before_configure(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(build, "run") as run:
            with self.assertRaisesRegex(ValueError, "Game builds require"):
                build.main(["--product", "wii-menu", "--version", "test", "--build-dir", directory,
                            "--output-dir", directory, "--no-package"])
            run.assert_not_called()

    def test_developer_targets_have_no_catalog_or_gpu_dependency(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            calls = []

            def command(argv):
                calls.append(argv)
                if "-S" in argv:
                    cpu = root / "build/cpu"
                    cpu.mkdir(parents=True)
                    (cpu / "release-runtime-directories.txt").write_text(str(root) + "\n")
                if "--build" in argv:
                    binary = root / "build/bin"
                    binary.mkdir(parents=True, exist_ok=True)
                    suffix = ".exe" if build.os.name == "nt" else ""
                    for name in build.TOOLS:
                        (binary / (name + suffix)).write_bytes(b"fixture")

            with patch.object(build, "run", side_effect=command):
                self.assertEqual(build.main(["--product", "wiillr", "--version", "test",
                    "--build-dir", str(root / "build"), "--output-dir", str(root / "out"),
                    "--no-package"]), 0)
            self.assertEqual(len(calls), 2)
            self.assertEqual(calls[1][calls[1].index("--target") + 1:], list(build.TOOLS))
            products = json.loads((root / "build/build-products.json").read_text())
            self.assertIsNone(products["gx_library"])
            self.assertEqual(len(products["tool_executables"]), 4)
            self.assertFalse(any("catalog" in arg or "cmake/gpu" in arg for call in calls for arg in call))
            self.assertIn(str(root.resolve()), products["dependency_dirs"])

    def test_runtime_export_filters_missing_directories_and_duplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            with self.assertRaisesRegex(ValueError, "export is missing"):
                build.compiler_runtime_directories(root)
            (root / "release-runtime-directories.txt").write_text(
                str(root) + "\n" + str(root / "absent") + "\n" + str(root) + "\n\n")
            self.assertEqual(build.compiler_runtime_directories(root), [str(root)])


if __name__ == "__main__":
    unittest.main()

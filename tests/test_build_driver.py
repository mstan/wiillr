# SPDX-License-Identifier: GPL-3.0-or-later
"""Build-input confinement and catalog-free developer graph contracts."""
import importlib.util
import json
import hashlib
import io
import zipfile
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release_build", ROOT / "tools/build.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class BuildDriver(unittest.TestCase):
    def setUp(self):
        resolver = patch.object(build, "resolve_providers", return_value={})
        resolver.start()
        self.addCleanup(resolver.stop)

    def product(self, root):
        (root / "product.cmake").write_text("# synthetic consumer\n")
        (root / "README.md").write_text("Synthetic consumer\n")
        (root / "LICENSE").write_text("fixture GPL terms\n")
        (root / "LICENSING.md").write_text("fixture notices\n")
        product = root / "product.json"
        product.write_text(json.dumps({"engine_product":"synthetic-game", "name":"Synthetic Game",
            "archive_prefix":"SyntheticGame", "cmake":"product.cmake", "readme":"README.md"}))
        return product

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
            with self.assertRaisesRegex(ValueError, "consumer --product-file"):
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
                    cpu.mkdir(parents=True, exist_ok=True)
                    (cpu / "release-runtime-directories.txt").write_text(str(root) + "\n")
                if "--build" in argv:
                    binary = root / "build/bin"
                    binary.mkdir(parents=True, exist_ok=True)
                    suffix = ".exe" if build.os.name == "nt" else ""
                    for name in build.TOOLS:
                        (binary / (name + suffix)).write_bytes(b"fixture")

            with patch.object(build, "run", side_effect=command), patch.object(build, "preflight", return_value=[]):
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
            previous = json.loads((root / "build/build-configuration.json").read_text())
            previous["version"] = "old"
            (root / "build/build-configuration.json").write_text(json.dumps(previous))
            with patch.object(build, "run", side_effect=command), patch.object(build, "preflight", return_value=[]):
                self.assertEqual(build.main(["--version", "new", "--build-dir", str(root / "build"),
                    "--output-dir", str(root / "out"), "--no-package"]), 0)
            self.assertEqual(json.loads((root / "build/build-products.json").read_text())["version"], "new")
            self.assertNotIn("version", json.loads((root / "build/build-configuration.json").read_text()))


    def test_default_check_uses_project_metadata_without_creating_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            (root / "CMakeLists.txt").write_text("project(wiillr VERSION 2.3.4 LANGUAGES C CXX)\n")
            with patch.object(build, "ROOT", root), patch.object(build, "preflight", return_value=[]), redirect_stdout(io.StringIO()) as output:
                self.assertEqual(build.main(["--check"]), 0)
            self.assertIn("Package version: 2.3.4", output.getvalue())
            self.assertIn(str(root / "build/wiillr"), output.getvalue())
            self.assertFalse((root / "build").exists())
            self.assertFalse((root / "dist").exists())

    def test_bad_versions_fail_before_input_import_or_configure(self):
        for version in ("../build", "nightly build", ""):
            with self.subTest(version=version), patch.object(build, "input_bundle") as inputs, patch.object(build, "run") as run, redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    build.main(["--version", version, "--check"])
                self.assertEqual(error.exception.code, 2)
                inputs.assert_not_called()
                run.assert_not_called()

    def test_missing_prerequisites_are_reported_together(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(build, "ROOT", Path(directory)), patch.object(build.shutil, "which", return_value=None):
            errors = build.preflight(None, False)
            self.assertTrue(any("CMake" in error for error in errors))
            self.assertTrue(any("Ninja" in error for error in errors))
            self.assertTrue(any("gc-wii-core" in error for error in errors))
            self.assertTrue(any("powerpc-recomp-core" in error for error in errors))

    def test_zip_input_import_is_pinned_reusable_and_detects_changed_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            (root / "tools").mkdir()
            content = b"independent source fixture\n"
            manifest = {"schema": 1, "files": {"source.c": hashlib.sha256(content).hexdigest()}}
            archive = root / "inputs.zip"
            with zipfile.ZipFile(archive, "w") as output:
                output.writestr("inputs.json", json.dumps(manifest))
                output.writestr("source.c", content)
            expected = build.digest(archive)
            (root / "tools/build-inputs.json").write_text(json.dumps({
                "private_input_manifest": "inputs.json", "archive": {"sha256": expected}}))
            work = root / "work"
            with patch.object(build, "ROOT", root), redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(ValueError, "checksum"):
                    build.input_bundle(archive, work, "0" * 64)
                self.assertFalse(work.exists())
                imported = build.input_bundle(archive, work, None)
                self.assertEqual((imported / "source.c").read_bytes(), content)
                self.assertEqual(build.input_bundle(archive, work, None), imported)
                self.assertEqual(build.verify_inputs(imported, False)["files"], 1)
                (imported / "source.c").write_bytes(b"changed")
                with self.assertRaisesRegex(ValueError, "Changed build input"):
                    build.verify_inputs(imported, False)
                self.assertEqual(list((work / "input-cache").glob(".import-*")), [])


    @unittest.skipUnless(build.os.name == "nt", "Windows compiler/provider separation")
    def test_windows_gpu_uses_native_sdk_without_cpu_prefix_leaks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            inputs = root / "inputs"; inputs.mkdir()
            sdk = root / "native-sdl"; sdk.mkdir()
            calls = []
            def command(argv, environment=None):
                calls.append((argv, environment))
                if "-S" in argv:
                    target = Path(argv[argv.index("-B") + 1]); target.mkdir(parents=True, exist_ok=True)
                    (target / "release-runtime-directories.txt").write_text(str(root) + "\n")
                    if target.name == "gpu":
                        (target / "CMakeCache.txt").write_text("SDL3_DIR:PATH=" + str(sdk) + "\n")
                if "--build" in argv:
                    binary = root / "build/bin"; binary.mkdir(parents=True, exist_ok=True)
                    for name in ("wiillr-native.exe", "gcnllr-native-gx.dll", "dxcompiler.dll", "dxil.dll"):
                        (binary / name).write_bytes(b"fixture")
            gpu_environment = {"PATH": "native-sdk-only"}
            with patch.object(build, "resolve_providers", return_value={"gc-wii-core":{"path":str(inputs / "deps/gc-wii-core"),"variable":"WIILLR_CORE_SOURCE_DIR","available":True}}), patch.object(build, "verify_cache"), patch.object(build, "verify_inputs", return_value={}), patch.object(build, "preflight", return_value=[]), patch.object(build, "windows_gpu_environment", return_value=(gpu_environment, ["-DCMAKE_CXX_COMPILER=native-clang"])), patch.object(build, "run", side_effect=command), patch.dict(build.os.environ, {"CMAKE_PREFIX_PATH": str(root / "mingw-prefix")}):
                self.assertEqual(build.main(["--product", "synthetic-game", "--product-file", str(self.product(root)), "--inputs", str(inputs),
                    "--build-dir", str(root / "build"), "--no-package"]), 0)
            configs = [(args, env) for args, env in calls if "-S" in args]
            self.assertEqual(len(configs), 2)
            graphics, cpu = configs
            self.assertEqual(graphics[1], gpu_environment)
            self.assertIn("-DCMAKE_PREFIX_PATH=", graphics[0])
            self.assertFalse(any("mingw-prefix" in arg for arg in graphics[0]))
            self.assertIn("-DSDL3_DIR=" + str(sdk), cpu[0])
            self.assertTrue(any("mingw-prefix" in arg for arg in cpu[0]))
            self.assertIsNone(cpu[1])

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

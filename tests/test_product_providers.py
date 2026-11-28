# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual source-route identity, consumer confinement and tooling adapters."""
from pathlib import Path
import hashlib
import importlib.util
import json
import socket
import subprocess
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import provider_provenance as provenance
import product
import package_release as package
from wii_control import Control, validate_reply


class ProductProviders(unittest.TestCase):
    def test_actual_checkout_override_and_bundle_routes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "tools").mkdir()
            (root / "tools/build-inputs.json").write_text(json.dumps({"providers":{
                name:{"path":"deps/"+name,"revision":"a"*40} for name in provenance.PROVIDERS}}))
            bundle = root / "bundle"
            sources = {}
            for name in ("gc-wii-core", "powerpc-recomp-core", "arm-recomp-core", "recomp-ui", "recompcore", "bluewake"):
                path = bundle / "deps" / name
                path.mkdir(parents=True)
                sources["deps/"+name] = {"commit":"a"*40}
            (bundle / "inputs.json").write_text(json.dumps({"sources":sources,"source_archives":["sources.zip"]}))
            actual = provenance.resolve(root, bundle, True, [])
            self.assertEqual(actual["recomp-ui"]["revision"], "a"*40)
            self.assertEqual(actual["recomp-ui"]["selection"], "verified-bundle")
            checkout = root / "checkout"
            subprocess.run(["git","init","-q",str(checkout)],check=True)
            (checkout / "provider.c").write_text("original\n")
            subprocess.run(["git","-C",str(checkout),"add","provider.c"],check=True)
            subprocess.run(["git","-C",str(checkout),"-c","user.name=Fixture","-c","user.email=fixture@example.invalid","commit","-qm","fixture"],check=True)
            revision = subprocess.check_output(["git","-C",str(checkout),"rev-parse","HEAD"],text=True).strip()
            override = provenance.resolve(root, bundle, True, ["WIILLR_UI_SOURCE_DIR="+str(checkout)])
            self.assertEqual(override["recomp-ui"]["revision"],revision)
            self.assertEqual(override["recomp-ui"]["selection"],"checkout")
            self.assertFalse(override["recomp-ui"]["dirty"])
            (checkout / "provider.c").write_text("changed\n")
            self.assertTrue(provenance.resolve(root,bundle,True,["WIILLR_UI_SOURCE_DIR="+str(checkout)])["recomp-ui"]["dirty"])
            cache = root / "CMakeCache.txt"
            cache.write_text("\n".join(item["variable"]+":PATH="+item["path"] for item in override.values() if item["variable"]))
            provenance.verify_cache(cache,override)
            cache.write_text(cache.read_text().replace(str(checkout),str(root / "different")))
            with self.assertRaisesRegex(ValueError,"Configured source provider differs: recomp-ui"):
                provenance.verify_cache(cache,override)

    def test_consumer_product_requires_confined_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "README.md").write_text("consumer\n")
            (root / "product.cmake").write_text("# consumer\n")
            description = {"engine_product":"example","name":"Example","archive_prefix":"Example",
                           "cmake":"product.cmake","readme":"README.md"}
            path = root / "product.json"
            path.write_text(json.dumps(description))
            self.assertEqual(product.load(path,"example")["name"],"Example")
            with self.assertRaisesRegex(ValueError,"another consumer"):
                product.load(path,"other")
            description["readme"] = "../README.md"
            path.write_text(json.dumps(description))
            with self.assertRaises((ValueError,FileNotFoundError)):
                product.load(path,"example")

    def test_wii_adapter_keeps_ascii_requests_and_reply_validation(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1",0))
        listener.listen(1)
        requests = []
        def serve():
            with listener:
                peer,_ = listener.accept()
                with peer, peer.makefile("rb") as stream:
                    requests.append(stream.readline())
                    peer.sendall(b'{"ok":true,"paused":true}\n')
        worker = threading.Thread(target=serve)
        worker.start()
        with Control(listener.getsockname()[1]) as client:
            self.assertTrue(client.command("pause")["paused"])
        worker.join()
        self.assertEqual(requests,[b"pause\n"])
        for value in ([],{}, {"ok":False,"error":"unsupported"}):
            with self.assertRaises(RuntimeError):validate_reply(value,"pause")

    def test_archive_reopens_hashes_and_rejects_user_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage = root / "stage"
            stage.mkdir()
            (stage / "README.md").write_bytes(b"public product\n")
            report = package.archive(stage,root / "package.zip","windows")
            self.assertEqual(report["files"]["README.md"]["sha256"],hashlib.sha256(b"public product\n").hexdigest())
            (stage / "config.ini").write_text("user settings")
            with self.assertRaisesRegex(package.ReleaseError,"Forbidden payload"):
                package.archive(stage,root / "unsafe.zip","windows")


if __name__ == "__main__":unittest.main()

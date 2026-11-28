#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build and package one explicit consumer or the engine developer tools."""
from pathlib import Path
import argparse
import json
import platform
import subprocess
import sys
import tempfile
import tarfile
import zipfile
from product import load as load_product

ROOT = Path(__file__).resolve().parents[1]

def smoke_game(archive, product, host, output):
    with tempfile.TemporaryDirectory(prefix="wiillr-package-smoke-", dir=output) as temporary:
        destination = Path(temporary)
        if archive.suffix == ".zip":
            with zipfile.ZipFile(archive) as source: source.extractall(destination)
        else:
            with tarfile.open(archive) as source: source.extractall(destination, filter="data")
        title = product["name"]
        base = destination / title
        binary = (base / (title+".app") / "Contents/MacOS" / title if host == "macos"
                  else base / (title+(".exe" if host == "windows" else "")))
        if host != "windows": binary.chmod(binary.stat().st_mode | 0o111)
        import os
        environment = dict(os.environ, WIILLR_USER_DIR=str(destination/"empty-user-profile"),
                           GCN_AUDIO_MUTE="1", GCNLLR_RENDER_HIDDEN="1", LNG_TEST_HIDDEN="1")
        resources = base / (title+".app") / "Contents/Resources" if host == "macos" else base
        graphics = resources / ("gcnllr-native-gx.dll" if host == "windows" else
                                "libgcnllr-native-gx." + ("dylib" if host == "macos" else "so"))
        probe = ('import ctypes as c,json,os,sys; '
                 'scope=os.add_dll_directory(os.path.dirname(sys.argv[1])) if os.name=="nt" else None; '
                 'library=c.CDLL(sys.argv[1],mode=getattr(os,"RTLD_NOW",0)); '
                 'api=library.gcnllr_native_gx_get_api; api.argtypes=[c.c_uint32]; api.restype=c.c_void_p; '
                 'assert api(12) and not api(0); print(json.dumps({"graphics_loaded":True,"abi":12,"gpu_initialized":False}))')
        loaded = subprocess.run([sys.executable,"-c",probe,str(graphics)],cwd=destination,
                                env=environment,capture_output=True,text=True)
        if loaded.returncode:
            raise RuntimeError("Packaged graphics service did not load: "+loaded.stderr)
        result = subprocess.run([str(binary),"--launcher-check"],cwd=destination,
                                env=environment,capture_output=True,text=True)
        expected = product["empty_profile_check"]
        if result.returncode != expected["returncode"] or expected["stderr_contains"] not in result.stderr:
            raise RuntimeError("Consumer package startup check failed: "+result.stderr)
        for name in expected.get("required_dirs", []):
            if not (destination / "empty-user-profile" / name).is_dir():
                raise RuntimeError("Consumer package profile was not initialized: "+name)
        if expected.get("empty_stderr") and result.stderr.strip():
            raise RuntimeError("Consumer package startup reported an error: "+result.stderr)
        (output/(product["engine_product"]+"-"+host+"-package-smoke.json")).write_text(json.dumps({
            "product":product["engine_product"],"returncode":result.returncode,
            "stdout":result.stdout,"stderr":result.stderr,"window_opened":False,"guest_executed":False,
            "graphics":json.loads(loaded.stdout)},indent=2)+"\n")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product-file",type=Path)
    parser.add_argument("--inputs-dir",type=Path)
    parser.add_argument("--build-dir",type=Path,required=True)
    parser.add_argument("--output-dir",type=Path,required=True)
    parser.add_argument("--version",required=True)
    parser.add_argument("--jobs",type=int,default=2)
    parser.add_argument("--common-key",type=Path)
    args=parser.parse_args()
    name=json.loads(args.product_file.read_text())["engine_product"] if args.product_file else "wiillr"
    product=load_product(args.product_file,name)
    command=[sys.executable,str(ROOT/"tools/build.py"),"--product",name,
             "--build-dir",str(args.build_dir),"--output-dir",str(args.output_dir),
             "--version",args.version,"--jobs",str(args.jobs)]
    if args.product_file: command += ["--product-file",str(args.product_file)]
    if args.inputs_dir: command += ["--inputs-dir",str(args.inputs_dir)]
    if args.common_key: command += ["--common-key",str(args.common_key)]
    subprocess.run(command,check=True)
    host={"win32":"windows","linux":"linux","darwin":"macos"}[sys.platform]
    arch={"amd64":"x86_64","aarch64":"arm64"}.get(platform.machine().lower(),platform.machine().lower())
    suffix=".tar.gz" if host=="linux" else ".zip"
    archive=args.output_dir/(product["archive_prefix"]+"-"+host+"-"+arch+"-v"+args.version+suffix)
    if args.product_file:
        smoke_game(archive,product,host,args.output_dir)
    else:
        with tempfile.TemporaryDirectory(prefix="wiillr-tools-smoke-",dir=args.output_dir) as temporary:
            destination=Path(temporary)
            if archive.suffix==".zip":
                with zipfile.ZipFile(archive) as source:source.extractall(destination)
            else:
                with tarfile.open(archive) as source:source.extractall(destination,filter="data")
            cli=destination/"wiillr/lib/wiillr/tools/wiillr.py"
            for arguments in (("--version",),("--help",),*((name,"--help") for name in ("inputs","discover","prepare","install-wad","install-ios"))):
                subprocess.run([sys.executable,str(cli),*arguments],check=True)
    import hashlib
    digest=hashlib.sha256()
    with archive.open("rb") as stream:
        for block in iter(lambda:stream.read(1024*1024),b""):digest.update(block)
    (args.output_dir/("SHA256SUMS-"+host+"-"+arch+".txt")).write_text(digest.hexdigest()+"  "+archive.name+"\n")

if __name__=="__main__":main()

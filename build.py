#!/bin/python

import os
import shutil
from pathlib import Path
import subprocess
import argparse
import filecmp
import json

parser = argparse.ArgumentParser()
parser.add_argument("-U", "--update", action="store_true", help="Update")
parser.add_argument("-C", "--configure", action="store_true", help="Force configure")
parser.add_argument("-B", "--build", action="store_true", help="Build")
parser.add_argument("-R", "--release", action="store_true", help="Release")
parser.add_argument("-I", "--install", action="store_true", help="Install")
parser.add_argument("--asan", action="store_true", help="Enable Address Sanitizer")
parser.add_argument("--system-linker", action="store_true", help="Use system linker (even if mold is available)")
parser.add_argument("--use-gcc", action="store_true", help="Use GCC instead of Clang")
args = parser.parse_args()

# -----------------------------------------------------------------------------

def ensure_dir(path: Path | str) -> Path:
    os.makedirs(path, exist_ok=True)
    return Path(path)

cwd = Path(".").absolute()
build_dir = ensure_dir(cwd / ".build")
vendor_dir = ensure_dir(build_dir / "3rdparty")

# -----------------------------------------------------------------------------

def write_file_lazy(path: Path, data: str | bytes):
    match data:
        case bytes(b):
            if not path.exists() or b != path.read_bytes():
                path.write_bytes(b)
        case str(t):
            if not path.exists() or t != path.read_text():
                path.write_text(t)

# -----------------------------------------------------------------------------

local_bin_dir  = ensure_dir(os.path.expanduser("~/.local/bin"))
xdg_portal_dir = ensure_dir(os.path.expanduser("~/.config/xdg-desktop-portal"))

def install_file(file: Path, target: Path):
    if target.exists():
        if filecmp.cmp(file, target):
            return
        os.remove(target)
    print(f"Installing [{file}] to [{target}]")
    shutil.copy2(file, target)

# -----------------------------------------------------------------------------

cxx_compilers = {
    "clang": "clang++",
    "gcc":   "g++",
}

def build(build_type, compiler, linker_type, install = None):
    build_name = f"{build_type.lower()}-{compiler}-{linker_type.lower()}"
    if args.asan:
        build_name += "-asan"
    cmake_dir = build_dir / build_name

    configure_ok = True

    if ((args.build or args.install) and not cmake_dir.exists()) or args.configure:
        cmd  = ["cmake", "--fresh", "-B", cmake_dir, "-G", "Ninja", f"-DVENDOR_DIR={vendor_dir}", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
        cmd += [f"-DCMAKE_C_COMPILER={compiler}", f"-DCMAKE_CXX_COMPILER={cxx_compilers[compiler]}", f"-DCMAKE_LINKER_TYPE={linker_type}"]
        cmd += [f"-DCMAKE_BUILD_TYPE={build_type}"]
        if args.asan:
            cmd += ["-DUSE_ASAN=1"]

        print(cmd)
        res = subprocess.run(cmd)
        if res.returncode != 0:
            os._exit(res.returncode)

    if (cmake_dir / "compile_commands.json").exists():
        shutil.copy2(cmake_dir / "compile_commands.json", build_dir / "compile_commands.json")

    if configure_ok and (args.build or args.install):
        res = subprocess.run(["cmake", "--build", cmake_dir])
        if res.returncode != 0:
            os._exit(res.returncode)

    if install:
        install_file(cmake_dir / install, local_bin_dir / install)

# -----------------------------------------------------------------------------

use_mold = not args.system_linker and shutil.which("mold")

build(build_type  = "Release" if args.release else "Debug",
      compiler    = "gcc"     if args.use_gcc else "clang",
      linker_type = "MOLD"    if use_mold     else "SYSTEM",
      install     = "harmony" if args.install else None)

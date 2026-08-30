#!/usr/bin/env python3
"""Build cpp-mcp as a shared library (libmcp.so) without CMake.

Usage:
    ./build.py                    # build libmcp.so into build/
    ./build.py --compiler clang++ # pick a compiler
    ./build.py --ssl              # enable OpenSSL/HTTPS support
    ./build.py --examples         # also build the example programs
    ./build.py --clean            # remove the build directory first
    ./build.py -j 8               # parallel compile jobs
"""

import argparse
import os
import shutil
import subprocess
import sys
import sysconfig
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC_DIR = ROOT / "src"
INCLUDE_DIRS = [ROOT / "include", ROOT / "common"]

SOURCES = [
    "mcp_message.cpp",
    "mcp_resource.cpp",
    "mcp_server.cpp",
    "mcp_tool.cpp",
    "mcp_stdio_client.cpp",
    "mcp_sse_client.cpp",
]

EXAMPLES = [
    "sse_client_example.cpp",
    "stdio_client_example.cpp",
    "server_example.cpp",
    "agent_example.cpp",
    "stdio_server_example.cpp",
]

LIB_NAME = "libmcp.so"


def detect_compiler(preferred=None):
    candidates = [preferred] if preferred else [os.environ.get("CXX"), "g++", "clang++"]
    for c in candidates:
        if c and shutil.which(c):
            return c
    sys.exit("error: no C++ compiler found (tried: %s)" % ", ".join(x for x in candidates if x))


def run(cmd, verbose):
    if verbose:
        print(" ".join(cmd))
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    if proc.returncode != 0:
        raise RuntimeError("command failed: " + " ".join(cmd))


def compile_one(compiler, src, obj, flags, verbose):
    obj.parent.mkdir(parents=True, exist_ok=True)
    print("CXX %s" % src.relative_to(ROOT))
    run([compiler, "-c", str(src), "-o", str(obj)] + flags, verbose)
    return obj


def main():
    p = argparse.ArgumentParser(description="Build cpp-mcp as a shared library.")
    p.add_argument("--compiler", help="C++ compiler to use (default: $CXX, g++, clang++)")
    p.add_argument("--build-dir", default="build", help="output directory (default: build)")
    p.add_argument("--std", default="c++17", help="C++ standard (default: c++17)")
    p.add_argument("--debug", action="store_true", help="debug build (-O0 -g) instead of -O2")
    p.add_argument("--ssl", action="store_true", help="enable OpenSSL support")
    p.add_argument("--examples", action="store_true", help="also build the examples")
    p.add_argument("--max-sessions", type=int, default=10, help="MCP_MAX_SESSIONS (default: 10)")
    p.add_argument("--session-timeout", type=int, default=30, help="MCP_SESSION_TIMEOUT seconds (default: 30)")
    p.add_argument("--clean", action="store_true", help="wipe the build directory first")
    p.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 1, help="parallel compile jobs")
    p.add_argument("-v", "--verbose", action="store_true", help="print each command")
    p.add_argument("--extra-cxxflags", default="", help="additional compiler flags")
    p.add_argument("--extra-ldflags", default="", help="additional linker flags")
    args = p.parse_args()

    compiler = detect_compiler(args.compiler)
    build_dir = (ROOT / args.build_dir).resolve()
    obj_dir = build_dir / "obj"

    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)

    cxxflags = ["-std=" + args.std, "-fPIC", "-pthread", "-Wall"]
    cxxflags += ["-O0", "-g"] if args.debug else ["-O2", "-DNDEBUG"]
    for d in INCLUDE_DIRS:
        cxxflags += ["-I", str(d)]
    cxxflags += [
        "-DMCP_MAX_SESSIONS=%d" % args.max_sessions,
        "-DMCP_SESSION_TIMEOUT=%d" % args.session_timeout,
    ]

    ldflags = ["-pthread"]
    if args.ssl:
        cxxflags += ["-DMCP_SSL", "-DCPPHTTPLIB_OPENSSL_SUPPORT"]
        ldflags += ["-lssl", "-lcrypto"]
    if sys.platform == "darwin":
        soname = []
    else:
        soname = ["-Wl,-soname," + LIB_NAME]
    cxxflags += args.extra_cxxflags.split()
    ldflags += args.extra_ldflags.split()

    print("compiler: %s" % compiler)
    print("output:   %s" % (build_dir / LIB_NAME))

    jobs = [(SRC_DIR / s, obj_dir / (s + ".o")) for s in SOURCES]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(compile_one, compiler, src, obj, cxxflags, args.verbose)
                   for src, obj in jobs]
        objects = [f.result() for f in futures]

    lib_path = build_dir / LIB_NAME
    print("LD  %s" % lib_path.name)
    run([compiler, "-shared", "-o", str(lib_path)] + [str(o) for o in objects] + soname + ldflags,
        args.verbose)

    if args.examples:
        ex_dir = build_dir / "examples"
        ex_dir.mkdir(exist_ok=True)
        for ex in EXAMPLES:
            out = ex_dir / Path(ex).stem
            print("EXE %s" % out.name)
            run([compiler, str(ROOT / "examples" / ex), "-o", str(out)] + cxxflags +
                ["-I", str(ROOT / "examples"), "-L", str(build_dir), "-lmcp",
                 "-Wl,-rpath," + str(build_dir)] + ldflags, args.verbose)

    print("done: %s (%.1f KiB)" % (lib_path, lib_path.stat().st_size / 1024))


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as e:
        sys.exit("build failed: %s" % e)

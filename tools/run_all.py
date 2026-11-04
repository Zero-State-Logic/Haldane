"""Build and run everything: C unit tests, the core DLL, Python tests, eval.

    python tools/run_all.py            build + C tests + Python tests + eval
    python tools/run_all.py --no-eval  skip the (slower) evaluation

Windows uses MSVC through tools/msvc.bat; Linux/macOS use $CC (default cc).
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BUILD = REPO / "build"
CORE = sorted((REPO / "firmware" / "core").glob("*.c"))
CTEST = sorted((REPO / "host" / "ctest").glob("*.c"))
CAPI = [REPO / "host" / "capi" / "haldane_api.c"]
WINDOWS = sys.platform.startswith("win")


def rel(paths: list[Path]) -> list[str]:
    return [str(p.relative_to(REPO)) for p in paths]


def run(cmd: list[str], cwd: Path = REPO) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True)


def build_msvc() -> tuple[Path, Path]:
    (BUILD / "obj_tests").mkdir(parents=True, exist_ok=True)
    (BUILD / "obj_dll").mkdir(parents=True, exist_ok=True)
    common = ["cmd", "/c", r"tools\msvc.bat", "/O2", "/WX", r"/Ifirmware\core"]
    exe = BUILD / "haldane_tests.exe"
    dll = BUILD / "haldane_core.dll"
    run(common + [r"/Fobuild\obj_tests\\", r"/Febuild\haldane_tests.exe"] + rel(CTEST + CORE))
    run(common + ["/LD", r"/Fobuild\obj_dll\\", r"/Febuild\haldane_core.dll"] + rel(CAPI + CORE))
    return exe, dll


def build_cc() -> tuple[Path, Path]:
    BUILD.mkdir(exist_ok=True)
    cc = os.environ.get("CC", "cc")
    flags = ["-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-Ifirmware/core"]
    exe = BUILD / "haldane_tests"
    lib = BUILD / ("libhaldane_core.dylib" if sys.platform == "darwin" else "libhaldane_core.so")
    run([cc] + flags + ["-o", str(exe)] + rel(CTEST + CORE) + ["-lm"])
    run([cc] + flags + ["-shared", "-fPIC", "-fvisibility=hidden", "-o", str(lib)]
        + rel(CAPI + CORE))
    return exe, lib


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-eval", action="store_true", help="skip results/ regeneration")
    args = ap.parse_args()
    exe, _ = build_msvc() if WINDOWS else build_cc()
    run([str(exe)])
    run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-v"], cwd=REPO / "host")
    if not args.no_eval:
        run([sys.executable, "-m", "haldane.eval_co"], cwd=REPO / "host")
    print("\nall green")
    return 0


if __name__ == "__main__":
    sys.exit(main())

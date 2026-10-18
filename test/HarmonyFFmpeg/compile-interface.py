"""Compile the production FFmpeg receiver and original tests without linking foreign libraries."""

import argparse
import ctypes
import os
import subprocess
import sys
from pathlib import Path

if os.name == "nt":
    ctypes.windll.kernel32.SetErrorMode(0x8003)
parser = argparse.ArgumentParser()
for name in ("source", "project-root", "qt-root", "ffmpeg-root", "cmake", "compiler"):
    parser.add_argument("--" + name, type=Path, required=True)
args = parser.parse_args()
build = args.project_root / "tmp/build-harmony-ffmpeg-interface"
env = os.environ.copy()
env["PATH"] = os.pathsep.join((str(args.qt_root / "bin"), str(args.compiler.parent), env["PATH"]))
env["QT_PLUGIN_PATH"] = str(args.qt_root / "plugins")
env["QML_IMPORT_PATH"] = str(args.qt_root / "qml")
env["QML2_IMPORT_PATH"] = str(args.qt_root / "qml")
env["PYTHONDONTWRITEBYTECODE"] = "1"
for library in ("libwinpthread-1.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll"):
    runtime = args.compiler.parent / library
    if not runtime.is_file():
        raise SystemExit(f"Missing compiler runtime: {runtime}")
frontend = subprocess.run(
    (str(args.compiler), "-print-prog-name=cc1plus"),
    cwd=args.project_root,
    env=env,
    check=True,
    capture_output=True,
    text=True,
    timeout=10,
).stdout.strip()
subprocess.run(
    (frontend, "--version"),
    cwd=args.project_root,
    env=env,
    check=True,
    stdout=subprocess.DEVNULL,
    timeout=10,
)
cmake = args.cmake
commands = (
    (
        cmake,
        "--fresh",
        "-S",
        args.source,
        "-B",
        build,
        "-G",
        "Ninja",
        f"-DCMAKE_PREFIX_PATH={args.qt_root.as_posix()}",
        f"-DQt6_DIR={(args.qt_root / 'lib/cmake/Qt6').as_posix()}",
        f"-DCMAKE_CXX_COMPILER={args.compiler.as_posix()}",
        f"-DCMAKE_MAKE_PROGRAM={cmake.with_name('ninja.exe').as_posix()}",
        f"-DQGC_FFMPEG_ROOT={args.ffmpeg_root.as_posix()}",
    ),
    (cmake, "--build", build, "--parallel", "1"),
)
for command in commands:
    result = subprocess.run(
        [str(item) for item in command], env=env, cwd=args.project_root, timeout=180
    )
    if result.returncode:
        sys.exit(result.returncode)
print(
    "Compiled production FFmpeg receiver, moc and original receiver tests; no runtime codec test was launched."
)

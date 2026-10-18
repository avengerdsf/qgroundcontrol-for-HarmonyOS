"""Compile the native QtTest application; never install, launch or select a device."""

import argparse
import ctypes
import os
import subprocess
import sys
from pathlib import Path

if os.name == "nt":
    ctypes.windll.kernel32.SetErrorMode(0x8003)
parser = argparse.ArgumentParser()
for name in ("source", "project-root", "qt-target", "qt-host", "ffmpeg-root", "cmake", "sdk-root"):
    parser.add_argument("--" + name, type=Path, required=True)
parser.add_argument("--arch", choices=("arm64-v8a", "x86_64"), required=True)
args = parser.parse_args()
build = args.project_root / ("tmp/build-harmony-ffmpeg-runtime-" + args.arch)
for required in (
    args.qt_target / "lib/cmake/Qt6/qt.toolchain.cmake",
    args.ffmpeg_root / "lib/libavformat.a",
):
    if not required.is_file():
        raise SystemExit(f"Missing dependency: {required}")
env = os.environ.copy()
compiler_runtime = args.qt_host.parents[1] / "Tools/mingw1310_64/bin"
for library in ("libwinpthread-1.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll"):
    if not (compiler_runtime / library).is_file():
        raise SystemExit(f"Missing Qt host runtime: {compiler_runtime / library}")
env["PATH"] = os.pathsep.join((str(args.qt_host / "bin"), str(compiler_runtime), env["PATH"]))
env["PYTHONDONTWRITEBYTECODE"] = "1"
env["QT_PLUGIN_PATH"] = str(args.qt_host / "plugins")
env["QML_IMPORT_PATH"] = str(args.qt_host / "qml")
env["QML2_IMPORT_PATH"] = str(args.qt_host / "qml")
commands = (
    (
        args.cmake,
        "--fresh",
        "-S",
        args.source,
        "-B",
        build,
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        f"-DCMAKE_TOOLCHAIN_FILE={(args.qt_target / 'lib/cmake/Qt6/qt.toolchain.cmake').as_posix()}",
        f"-DQT_HOST_PATH={args.qt_host.as_posix()}",
        f"-DOHOS_SDK_ROOT={args.sdk_root.as_posix()}",
        f"-DOHOS_ARCH={args.arch}",
        f"-DQGC_FFMPEG_ROOT={args.ffmpeg_root.as_posix()}",
        f"-DCMAKE_MAKE_PROGRAM={args.cmake.with_name('ninja.exe').as_posix()}",
    ),
    (args.cmake, "--build", build, "--parallel", "1"),
)
for command in commands:
    result = subprocess.run(
        [str(item) for item in command], cwd=args.project_root, env=env, timeout=180
    )
    if result.returncode:
        sys.exit(result.returncode)
print(f"Native FFmpeg QtTest application built in {build}; no device operation performed.")

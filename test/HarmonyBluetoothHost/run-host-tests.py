"""Build and run a standalone Qt test with one kit and no Windows error dialogs."""

import argparse
import ctypes
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path


def main():
    if os.name == "nt":
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    parser = argparse.ArgumentParser()
    for name in (
        "project-root",
        "source",
        "build",
        "qt-root",
        "compiler-dir",
        "cmake-dir",
        "executable",
    ):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--cmake-arg", action="append", default=[])
    args = parser.parse_args()
    project = Path(args.project_root).resolve()
    build = Path(args.build).resolve()
    if not build.is_relative_to(project / "tmp"):
        raise RuntimeError("Test build directory must be inside project tmp")
    qt = Path(args.qt_root).resolve()
    compiler = Path(args.compiler_dir).resolve()
    cmake = Path(args.cmake_dir).resolve() / "cmake.exe"
    ctest = cmake.with_name("ctest.exe")
    objdump = compiler / "objdump.exe"
    for required in (
        qt / "bin" / "Qt6Core.dll",
        cmake,
        ctest,
        objdump,
        compiler / "g++.exe",
        compiler / "libwinpthread-1.dll",
        compiler / "libgcc_s_seh-1.dll",
        compiler / "libstdc++-6.dll",
    ):
        if not required.exists():
            raise RuntimeError(f"Missing dependency: {required}")
    env = os.environ.copy()
    cache = project / "tmp/cache/host-tests"
    for name, directory in (
        ("TEMP", cache / "temp"),
        ("TMP", cache / "temp"),
        ("TMPDIR", cache / "temp"),
        ("APPDATA", cache / "appdata"),
        ("LOCALAPPDATA", cache / "localappdata"),
        ("HOME", cache / "home"),
        ("USERPROFILE", cache / "home"),
        ("XDG_CACHE_HOME", cache / "xdg"),
    ):
        directory.mkdir(parents=True, exist_ok=True)
        env[name] = str(directory)
    env["PATH"] = os.pathsep.join((str(qt / "bin"), str(compiler), env["PATH"]))
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env["QT_PLUGIN_PATH"] = str(qt / "plugins")
    env["QML_IMPORT_PATH"] = str(qt / "qml")
    env["QML2_IMPORT_PATH"] = str(qt / "qml")
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QT_QUICK_BACKEND"] = "software"
    env["QT_QUICK_CONTROLS_STYLE"] = "Basic"
    env["QML_DISABLE_DISK_CACHE"] = "1"

    def run(command, **kwargs):
        return subprocess.run(
            [str(item) for item in command], cwd=project, env=env, check=True, **kwargs
        )

    frontend = run(
        (compiler / "g++.exe", "-print-prog-name=cc1plus"),
        capture_output=True,
        text=True,
        timeout=10,
    ).stdout.strip()
    run((frontend, "--version"), stdout=subprocess.DEVNULL, timeout=10)
    print(f"Verified compiler runtime: {frontend}", flush=True)

    if not args.check_only:
        run(
            (
                cmake,
                "--fresh",
                "-S",
                args.source,
                "-B",
                build,
                "-G",
                "Ninja",
                "-DCMAKE_BUILD_TYPE=Debug",
                f"-DCMAKE_PREFIX_PATH={qt.as_posix()}",
                f"-DQt6_DIR={(qt / 'lib/cmake/Qt6').as_posix()}",
                f"-DCMAKE_CXX_COMPILER={(compiler / 'g++.exe').as_posix()}",
                f"-DCMAKE_MAKE_PROGRAM={cmake.with_name('ninja.exe').as_posix()}",
                *args.cmake_arg,
            )
        )
        run((cmake, "--build", build, "--parallel", "1"))

    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    for name, value in re.findall(r"^(Qt6\w*_DIR):[^=]+=(.+)$", cache, re.MULTILINE):
        if not value or value.endswith("-NOTFOUND"):
            continue
        if not Path(value).resolve().is_relative_to(qt):
            raise RuntimeError(f"Mixed Qt kit: {name}={value}; selected {qt}")

    executable = build / args.executable
    imports = run((objdump, "-p", executable), capture_output=True, text=True).stdout
    # Validate every Qt import against the DLL that PATH will supply before starting the test.
    for block in re.split(r"DLL Name: ", imports)[1:]:
        dll = block.splitlines()[0].strip()
        if not dll.lower().startswith("qt6"):
            continue
        selected = qt / "bin" / dll
        if not selected.is_file():
            raise RuntimeError(f"Selected kit does not provide {dll}")
        adjacent = build / dll
        if (
            adjacent.exists()
            and hashlib.sha256(adjacent.read_bytes()).digest()
            != hashlib.sha256(selected.read_bytes()).digest()
        ):
            raise RuntimeError(f"Executable directory contains a different {dll}")
        exported = run((objdump, "-p", selected), capture_output=True, text=True).stdout
        symbols = re.findall(r"^\s*[0-9a-f]+\s+\d+\s+([A-Za-z_?@]\S*)", block, re.MULTILINE)
        for symbol in symbols:
            if not re.search(r"\]\s+" + re.escape(symbol) + r"\s*$", exported, re.MULTILINE):
                raise RuntimeError(f"{dll} from {qt} does not export {symbol}")
    print(f"Verified CMake Qt modules and executable imports against {qt}", flush=True)
    if args.check_only:
        return 0
    result = subprocess.run(
        (
            str(ctest),
            "--test-dir",
            str(build),
            "--parallel",
            "1",
            "--output-on-failure",
            "--no-tests=error",
            "--timeout",
            "60",
            "--output-junit",
            str(build / "test-results.xml"),
        ),
        cwd=project,
        env=env,
        timeout=120,
    )
    native_result = build / "result.txt"
    if native_result.exists():
        try:
            report = native_result.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            report = native_result.read_text(
                encoding="mbcs" if os.name == "nt" else "utf-8", errors="replace"
            )
        print(report)
    return result.returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr)
        sys.exit(1)

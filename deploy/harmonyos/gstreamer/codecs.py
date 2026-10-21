#!/usr/bin/env python3
"""Build pinned static PIC dependencies with upstream build systems for OHOS.

Use the same prefix as the GStreamer recipes. Host executables stay in
work-root/host-tools; only target headers, archives and pkg-config files enter
the prefix. --prepare-only downloads and verifies inputs without compiling.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import shlex
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
LOCK = json.loads((HERE / "codecs-lock.json").read_text(encoding="utf-8"))


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def absolute_path(value: str) -> Path:
    path = Path(value)
    if not path.is_absolute():
        raise argparse.ArgumentTypeError("An absolute path is required")
    if path.drive.lower() == "c:":
        raise argparse.ArgumentTypeError("C: is read-only for this project")
    if any(character.isspace() for character in str(path)):
        raise argparse.ArgumentTypeError("Upstream configure builds require paths without whitespace")
    return path


def find_tool(variable: str, candidates: list[Path]) -> Path:
    override = os.environ.get(variable)
    if override:
        candidates = [Path(override)]
    for path in candidates:
        if path.is_file():
            return path.absolute()
    raise RuntimeError(f"Missing host tool {variable}; checked: " + ", ".join(map(str, candidates)))


def msys_path(path: Path) -> str:
    value = path.absolute().as_posix()
    if len(value) > 2 and value[1] == ":":
        return "/" + value[0].lower() + value[2:]
    return value


def normalize_pkgconfig_prefix(content: str, prefix: Path) -> str:
    content = content.replace(msys_path(prefix), prefix.as_posix())
    return "\n".join(f"prefix={prefix.as_posix()}" if line.startswith("prefix=") else line for line in content.splitlines()) + "\n"


class Recipe:
    def __init__(self, arguments: argparse.Namespace):
        self.args = arguments
        self.name = arguments.codec
        self.info = LOCK["sources"][self.name]
        self.root = arguments.work_root
        self.prefix = arguments.prefix
        self.dependencies = arguments.dependency_root
        self.architecture = arguments.architecture
        self.target = "aarch64-linux-ohos" if self.architecture == "arm64" else "x86_64-linux-ohos"
        self.cpu = "aarch64" if self.architecture == "arm64" else "x86_64"
        self.sdk = self.dependencies / "ohos-sdk/native"
        self.llvm = self.sdk / "llvm/bin"
        self.host = self.dependencies / "toolchains/Qt/Tools/mingw1310_64/bin"
        self.cache = self.root / "downloads/sources"
        self.source = self.root / "sources" / self.info["directory"]
        self.build = self.root / self.architecture / "codecs" / self.name
        self.temporary = self.build / "tmp"
        for directory in (self.cache, self.build, self.temporary, self.prefix):
            directory.mkdir(parents=True, exist_ok=True)
        msys = self.dependencies / "vcpkg-downloads/tools/msys2/19f68f647e350a23/usr/bin"
        self.bash = find_tool("QGC_GST_BASH", [msys / "bash.exe", Path("D:/Progrem/Git/bin/bash.exe")])
        self.shell_bin = self.bash.parent
        if self.shell_bin.name == "bin" and (self.shell_bin.parent / "usr/bin/sh.exe").exists():
            self.shell_bin = self.shell_bin.parent / "usr/bin"
        self.make = find_tool("QGC_GST_MAKE", [msys / "make.exe", self.host / "mingw32-make.exe"])
        self.patch = find_tool("QGC_GST_PATCH", [msys / "patch.exe", self.shell_bin / "patch.exe", Path("D:/Progrem/Git/usr/bin/patch.exe")])
        self.cc = self.require(self.llvm / "clang.exe")
        self.cxx = self.require(self.llvm / "clang++.exe")
        self.ar = self.require(self.llvm / "llvm-ar.exe")
        self.ranlib = self.require(self.llvm / "llvm-ranlib.exe")
        self.flags = [f"--target={self.target}", f"--sysroot={self.sdk.as_posix()}/sysroot", "-fPIC", "-D__OHOS__", "-D__MUSL__"]
        self.link_flags = self.flags[:2]
        self.environment = os.environ.copy()
        for key in ("CC", "CXX", "CPP", "AR", "AS", "LD", "NM", "RANLIB", "STRIP", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "ASFLAGS", "LDFLAGS", "HOSTCC", "HOSTCXX", "CROSS", "CROSS_COMPILE", "CROSS_PREFIX", "LIB", "LIBPATH", "INCLUDE", "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH", "CMAKE_PREFIX_PATH"):
            self.environment.pop(key, None)
        self.environment.update({
            "TEMP": str(self.temporary), "TMP": str(self.temporary), "TMPDIR": msys_path(self.temporary),
            "PYTHONDONTWRITEBYTECODE": "1", "PYTHONPYCACHEPREFIX": str(self.temporary / "pycache"),
            "PKG_CONFIG_PATH": "", "PKG_CONFIG_LIBDIR": str(self.prefix / "lib/pkgconfig"),
            "PKG_CONFIG_SYSROOT_DIR": "", "CONFIG_SITE": "/dev/null", "LC_ALL": "C",
            "CC": self.cc.as_posix(), "CXX": self.cxx.as_posix(), "AR": self.ar.as_posix(),
            "RANLIB": self.ranlib.as_posix(), "NM": (self.llvm / "llvm-nm.exe").as_posix(),
            "HOSTCC": (self.host / "gcc.exe").as_posix(), "HOSTCXX": (self.host / "g++.exe").as_posix(),
            "STRIP": (self.llvm / "llvm-strip.exe").as_posix(),
            "CFLAGS": " ".join(self.flags), "CXXFLAGS": " ".join([*self.flags, "-stdlib=libc++"]),
            "LDFLAGS": " ".join(self.link_flags),
        })
        self.prepend_path(self.shell_bin, self.host, self.llvm)
        self.commands: list[dict] = []
        self.tools: dict[str, str] = {"bash": str(self.bash), "make": str(self.make), "cc": str(self.cc), "cxx": str(self.cxx), "ar": str(self.ar), "python": sys.executable}
        self.nasm: Path | None = None

    @staticmethod
    def require(path: Path) -> Path:
        if not path.is_file():
            raise RuntimeError(f"Required tool not found: {path}")
        return path

    def prepend_path(self, *paths: Path) -> None:
        self.environment["PATH"] = os.pathsep.join(map(str, paths)) + os.pathsep + self.environment.get("PATH", "")

    def run(self, command: list[str | Path], cwd: Path | None = None, stdin: str | None = None) -> None:
        cwd = cwd or self.build
        cwd.mkdir(parents=True, exist_ok=True)
        rendered = [str(item) for item in command]
        self.commands.append({"cwd": str(cwd), "arguments": rendered, "stdin": stdin})
        print("+ " + subprocess.list2cmdline(rendered), flush=True)
        subprocess.run(rendered, cwd=cwd, env=self.environment, input=stdin, text=True, check=True)

    def shell(self, command: list[str | Path], cwd: Path | None = None) -> None:
        command_line = " ".join(shlex.quote(str(item).replace("\\", "/")) for item in command)
        self.run([self.bash, "--noprofile", "--norc", "-c", command_line], cwd)

    def make_target(self, *targets: str, cwd: Path | None = None) -> None:
        self.shell([self.make.as_posix(), f"-j{self.args.jobs}", f"SHELL={self.shell_bin.as_posix()}/sh.exe", *targets], cwd)

    def download(self, info: dict) -> Path:
        archive = self.cache / info["archive"]
        if not archive.exists() and self.name == "ffmpeg":
            shared = self.dependencies / "ffmpeg" / info["version"] / "source" / info["archive"]
            if shared.is_file() and sha256(shared) == info["sha256"]:
                shutil.copyfile(shared, archive)
        if not archive.exists():
            partial = archive.with_name(archive.name + ".part")
            print(f"Downloading {info['url']} -> {archive}", flush=True)
            try:
                request = urllib.request.Request(info["url"], headers={"User-Agent": "QGC-OHOS-dependencies/1"})
                with urllib.request.urlopen(request, timeout=60) as response, partial.open("wb") as output:
                    shutil.copyfileobj(response, output)
                if sha256(partial) != info["sha256"]:
                    raise RuntimeError(f"SHA256 mismatch: {partial}")
                partial.replace(archive)
            except Exception as error:
                partial.unlink(missing_ok=True)
                raise RuntimeError(f"Cannot fetch {info['url']}; place the verified archive at {archive}: {error}") from error
        if sha256(archive) != info["sha256"]:
            raise RuntimeError(f"SHA256 mismatch: {archive}; expected {info['sha256']}")
        return archive

    def apply_patches(self) -> None:
        for name in self.info.get("patches", []):
            patch = self.require(HERE / name)
            base = [str(self.patch), "--batch", "--silent", "-p1", "-i", str(patch)]
            probe = subprocess.run([*base, "--forward", "--dry-run"], cwd=self.source, env=self.environment, capture_output=True, text=True)
            if probe.returncode == 0:
                self.run([*base, "--forward", "--no-backup-if-mismatch"], self.source)
            else:
                reverse = subprocess.run([*base, "--reverse", "--dry-run"], cwd=self.source, env=self.environment, capture_output=True, text=True)
                if reverse.returncode:
                    raise RuntimeError(f"Patch does not apply cleanly: {patch}\n{probe.stdout}{probe.stderr}")

    def prepare(self) -> None:
        archive = self.download(self.info)
        if not self.source.is_dir():
            self.source.parent.mkdir(parents=True, exist_ok=True)
            with tarfile.open(archive) as source_archive:
                source_archive.extractall(self.source.parent, filter="data")
        if not self.source.is_dir():
            raise RuntimeError(f"Archive did not contain {self.source.name}: {archive}")
        self.apply_patches()
        if self.architecture == "x86_64" and self.name in {"ffmpeg", "openh264", "vpx", "x264"}:
            nasm_info = LOCK["hostTools"]["nasm"]
            nasm_root = self.root / "host-tools" / nasm_info["directory"]
            nasm_override = os.environ.get("QGC_GST_NASM")
            if nasm_override:
                self.nasm = self.require(Path(nasm_override))
            else:
                nasm_archive = self.download(nasm_info)
                if not (nasm_root / "nasm.exe").is_file():
                    host_tools = nasm_root.parent
                    host_tools.mkdir(parents=True, exist_ok=True)
                    with zipfile.ZipFile(nasm_archive) as source_archive:
                        for member in source_archive.infolist():
                            destination = (host_tools / member.filename).resolve()
                            if not destination.is_relative_to(host_tools.resolve()):
                                raise RuntimeError(f"Unsafe archive path: {member.filename}")
                        source_archive.extractall(host_tools)
                self.nasm = self.require(nasm_root / "nasm.exe")
            self.prepend_path(self.nasm.parent)
            self.tools["nasm"] = str(self.nasm)

    def pkgconfig(self, name: str, description: str, libraries: str, private: str = "", requires: str = "") -> None:
        directory = self.prefix / "lib/pkgconfig"
        directory.mkdir(parents=True, exist_ok=True)
        content = (
            f"prefix={self.prefix.as_posix()}\nexec_prefix=${{prefix}}\nlibdir=${{prefix}}/lib\nincludedir=${{prefix}}/include\n\n"
            f"Name: {name}\nDescription: {description}\nVersion: {self.info['version']}\n"
            f"Libs: -L${{libdir}} {libraries}\nLibs.private: {private}\nRequires.private: {requires}\nCflags: -I${{includedir}}\n"
        )
        (directory / f"{name}.pc").write_text(content, encoding="utf-8", newline="\n")

    def ffmpeg(self) -> None:
        arguments = [
            f"--prefix={msys_path(self.prefix)}", "--enable-cross-compile", "--target-os=linux", f"--arch={self.cpu}",
            f"--cc={self.cc.as_posix()}", f"--cxx={self.cxx.as_posix()}", f"--as={self.cc.as_posix()}", f"--ar={self.ar.as_posix()}",
            f"--nm={self.llvm.as_posix()}/llvm-nm.exe", f"--ranlib={self.ranlib.as_posix()}",
            f"--strip={self.llvm.as_posix()}/llvm-strip.exe", f"--host-cc={self.host.as_posix()}/gcc.exe",
            f"--extra-cflags={' '.join(self.flags)}", f"--extra-cxxflags={' '.join([*self.flags, '-stdlib=libc++'])}",
            f"--extra-ldflags={' '.join(self.link_flags)}",
            f"--tempprefix={msys_path(self.temporary)}/ffconf", "--pkg-config=false", "--disable-autodetect",
            "--disable-programs", "--disable-doc", "--disable-shared", "--enable-static", "--enable-pic",
            "--enable-gpl", "--enable-version3", "--enable-avcodec", "--enable-avdevice", "--enable-avfilter",
            "--enable-avformat", "--enable-avutil", "--enable-swresample", "--enable-swscale",
        ]
        if self.nasm:
            arguments.append(f"--x86asmexe={self.nasm.as_posix()}")
        self.shell([msys_path(self.source / "configure"), *arguments])
        self.make_target()
        self.make_target("install")

    def openh264(self) -> None:
        tools = self.dependencies / "vcpkg-downloads/tools"
        meson = find_tool("QGC_GST_MESON", sorted(tools.glob("meson-*/meson.py"), reverse=True))
        ninja = find_tool("QGC_GST_NINJA", sorted(tools.glob("ninja-*-windows/ninja.exe"), reverse=True))
        self.tools.update({"meson": str(meson), "ninja": str(ninja)})
        self.prepend_path(ninja.parent)
        cross = self.build / "ohos-cross.ini"
        cross.write_text(
            "[binaries]\n"
            + f"c = {[self.cc.as_posix()]}\ncpp = {[self.cxx.as_posix()]}\nar = {[self.ar.as_posix()]}\nstrip = {[(self.llvm / 'llvm-strip.exe').as_posix()]}\n"
            + (f"nasm = {[self.nasm.as_posix()]}\n" if self.nasm else "")
            + "[host_machine]\nsystem = 'linux'\n"
            + f"cpu_family = '{self.cpu}'\ncpu = '{self.cpu}'\nendian = 'little'\n"
            + "[properties]\nneeds_exe_wrapper = true\n"
            + f"sys_root = '{self.sdk.as_posix()}/sysroot'\npkg_config_libdir = ['{self.prefix.as_posix()}/lib/pkgconfig']\n"
            + "[built-in options]\n"
            + f"c_args = {self.flags}\ncpp_args = {[*self.flags, '-stdlib=libc++']}\nc_link_args = {self.link_flags}\ncpp_link_args = {[*self.link_flags, '-stdlib=libc++']}\n",
            encoding="utf-8", newline="\n",
        )
        build = self.build / "build"
        setup = [sys.executable, "-B", meson, "setup", build, self.source, "--cross-file", cross,
                 f"--prefix={self.prefix.as_posix()}", "--libdir=lib", "--buildtype=release", "--default-library=static", "--wrap-mode=nofallback", "-Db_staticpic=true", "-Dtests=disabled"]
        if (build / "meson-private/coredata.dat").exists():
            setup.append("--reconfigure")
        self.run(setup)
        self.run([sys.executable, "-B", meson, "compile", "-C", build, "-j", str(self.args.jobs)])
        self.run([sys.executable, "-B", meson, "install", "-C", build, "--no-rebuild"])
        self.pkgconfig("openh264", "OpenH264 encoder and decoder", "-lopenh264", "-lc++ -lm -pthread")

    def vpx(self) -> None:
        self.environment["LD"] = self.cc.as_posix()
        if self.nasm:
            self.environment["AS"] = self.nasm.as_posix()
        else:
            self.environment["AS"] = self.cc.as_posix()
            self.environment["ASFLAGS"] = " ".join(self.flags)
        target = "arm64-linux-gcc" if self.architecture == "arm64" else "x86_64-linux-gcc"
        self.shell([msys_path(self.source / "configure"), f"--prefix={msys_path(self.prefix)}", f"--target={target}",
                    "--enable-static", "--disable-shared", "--enable-pic", "--enable-vp8", "--enable-vp9",
                    "--enable-vp9-highbitdepth", "--enable-multithread", "--enable-runtime-cpu-detect",
                    "--disable-examples", "--disable-tools", "--disable-docs", "--disable-unit-tests"])
        self.make_target()
        self.make_target("install")
        self.pkgconfig("vpx", "WebM VP8 and VP9 encoder and decoder", "-lvpx", "-lm -pthread")

    def x264(self) -> None:
        self.environment["AS"] = self.nasm.as_posix() if self.nasm else " ".join([self.cc.as_posix(), *self.flags])
        host = "aarch64-linux-gnu" if self.architecture == "arm64" else "x86_64-linux-gnu"
        self.shell([msys_path(self.source / "configure"), f"--prefix={msys_path(self.prefix)}", f"--host={host}",
                    "--enable-static", "--enable-pic", "--disable-cli", "--bit-depth=all", "--chroma-format=all",
                    "--disable-bashcompletion", "--disable-avs", "--disable-lavf", "--disable-ffms", "--disable-gpac", "--disable-lsmash", "--disable-swscale"])
        self.make_target()
        self.make_target("install")
        self.pkgconfig("x264", "H.264/AVC encoder, 8 and 10 bit, all chroma formats", "-lx264", "-lm -ldl -pthread")

    def x265(self) -> None:
        tools = self.dependencies / "vcpkg-downloads/tools"
        cmake = find_tool("QGC_GST_CMAKE", sorted(tools.glob("cmake-*-windows/*/bin/cmake.exe"), reverse=True))
        ninja = find_tool("QGC_GST_NINJA", sorted(tools.glob("ninja-*-windows/ninja.exe"), reverse=True))
        self.tools.update({"cmake": str(cmake), "ninja": str(ninja)})
        self.prepend_path(ninja.parent)
        common = [
            cmake, "-G", "Ninja", "-S", self.source / "source",
            f"-DCMAKE_TOOLCHAIN_FILE={self.sdk.as_posix()}/build/cmake/ohos.toolchain.cmake",
            f"-DOHOS_ARCH={'arm64-v8a' if self.architecture == 'arm64' else 'x86_64'}", "-DOHOS_STL=c++_shared",
            f"-DCMAKE_MAKE_PROGRAM={ninja.as_posix()}", f"-DCMAKE_INSTALL_PREFIX={self.prefix.as_posix()}",
            f"-DCMAKE_FIND_ROOT_PATH={self.prefix.as_posix()};{self.sdk.as_posix()}/sysroot",
            "-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER", "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY",
            "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY", "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY",
            "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DENABLE_PIC=ON",
            "-DENABLE_SHARED=OFF", "-DENABLE_CLI=OFF", "-DENABLE_TESTS=OFF", "-DENABLE_LIBNUMA=OFF",
            "-DNATIVE_BUILD=OFF", "-DSTATIC_LINK_CRT=OFF", "-DLIB_INSTALL_DIR=lib", "-DGIT_ARCHETYPE=1",
            # x86 NASM code contains absolute relocations in this upstream release.
            f"-DENABLE_ASSEMBLY={'ON' if self.architecture == 'arm64' else 'OFF'}",
        ]
        if self.architecture == "arm64":
            common.append("-DCROSS_COMPILE_ARM64=ON")
        for depth in (12, 10, 8):
            build = self.build / f"{depth}bit"
            arguments = [*common, "-B", build, f"-DHIGH_BIT_DEPTH={'OFF' if depth == 8 else 'ON'}", f"-DEXPORT_C_API={'ON' if depth == 8 else 'OFF'}", f"-DMAIN12={'ON' if depth == 12 else 'OFF'}"]
            if depth == 8:
                arguments += [f"-DEXTRA_LIB={(self.build / '10bit/libx265.a').as_posix()};{(self.build / '12bit/libx265.a').as_posix()}", "-DLINKED_10BIT=ON", "-DLINKED_12BIT=ON"]
            self.run(arguments)
            self.run([cmake, "--build", build, "--parallel", str(self.args.jobs)])
        build8 = self.build / "8bit"
        self.run([cmake, "--install", build8])
        self.run([self.ar, "-M"], build8, "CREATE libx265-combined.a\nADDLIB libx265.a\nADDLIB ../10bit/libx265.a\nADDLIB ../12bit/libx265.a\nSAVE\nEND\n")
        shutil.copyfile(build8 / "libx265-combined.a", self.prefix / "lib/libx265.a")
        self.pkgconfig("x265", "H.265/HEVC encoder, 8, 10 and 12 bit", "-lx265", "-lc++ -lm -ldl -pthread")

    def openssl(self) -> None:
        perl = find_tool("QGC_GST_PERL", [self.shell_bin / "perl.exe", Path("D:/Progrem/Git/usr/bin/perl.exe")])
        self.tools["perl"] = str(perl)
        target = "linux-aarch64" if self.architecture == "arm64" else "linux-x86_64"
        self.shell([perl.as_posix(), msys_path(self.source / "Configure"), target, "no-shared", "no-module", "no-tests", "no-apps",
                    f"--prefix={msys_path(self.prefix)}", "--libdir=lib", "--openssldir=/etc/ssl"])
        self.make_target("build_libs")
        self.make_target("install_dev")
        self.pkgconfig("libcrypto", "OpenSSL cryptography library", "-lcrypto", "-ldl -pthread")
        self.pkgconfig("libssl", "OpenSSL TLS and DTLS library", "-lssl", requires="libcrypto")
        self.pkgconfig("openssl", "OpenSSL libraries", "", requires="libssl libcrypto")

    def verify_archive(self, archive: Path) -> None:
        expected_machine = 183 if self.architecture == "arm64" else 62
        objects = 0
        with archive.open("rb") as stream:
            if stream.read(8) != b"!<arch>\n":
                raise RuntimeError(f"Expected a regular static ar archive: {archive}")
            while header := stream.read(60):
                if len(header) != 60 or header[58:60] != b"`\n":
                    raise RuntimeError(f"Invalid ar header: {archive}")
                size = int(header[48:58])
                data = stream.read(size)
                if size % 2:
                    stream.read(1)
                if header[:3] == b"#1/":
                    data = data[int(header[3:16].strip()):]
                if data.startswith(b"\x7fELF"):
                    endian = "<" if data[5] == 1 else ">"
                    if struct.unpack_from(endian + "H", data, 18)[0] != expected_machine:
                        raise RuntimeError(f"Host or wrong-architecture object in {archive}")
                    objects += 1
                elif header[:16].strip() not in {b"/", b"//", b"/SYM64/", b"__.SYMDEF", b"__.SYMDEF SORTED"}:
                    raise RuntimeError(f"Non-ELF archive member in {archive}: {header[:16]!r}")
        if not objects:
            raise RuntimeError(f"No target ELF objects found in {archive}")

    def finish(self) -> None:
        # Configure/Make projects emit MSYS prefixes; pkgconf on Windows needs drive paths.
        for package in self.info["packages"]:
            pc = self.prefix / "lib/pkgconfig" / f"{package}.pc"
            if not pc.is_file():
                raise RuntimeError(f"Upstream install omitted required pkg-config metadata: {pc}")
            content = normalize_pkgconfig_prefix(pc.read_text(encoding="utf-8"), self.prefix)
            pc.write_text(content, encoding="utf-8", newline="\n")
            if package == "openssl":
                continue
            library = package if package.startswith("lib") else "lib" + package
            archive = self.prefix / "lib" / f"{library}.a"
            self.require(archive)
            self.verify_archive(archive)
        licenses = self.prefix / "share/licenses" / self.name
        licenses.mkdir(parents=True, exist_ok=True)
        source_licenses = []
        for name in self.info["licenses"]:
            source = self.require(self.source / name)
            shutil.copyfile(source, licenses / source.name)
            source_licenses.append({"source": str(source), "installed": str(licenses / source.name), "sha256": sha256(source)})
        manifest = {
            "name": self.name, "version": self.info["version"], "architecture": self.architecture,
            "target": self.target, "linkage": "static", "pic": True, "cxxRuntime": "libc++_shared",
            "source": {**self.info, "archivePath": str(self.cache / self.info["archive"]), "directoryPath": str(self.source)},
            "patches": [{"path": str(HERE / name), "sha256": sha256(HERE / name)} for name in self.info.get("patches", [])],
            "licenseFiles": source_licenses, "tools": self.tools, "commands": self.commands,
            "environment": {key: self.environment[key] for key in ("CC", "CXX", "AR", "AS", "LD", "NM", "RANLIB", "STRIP", "HOSTCC", "HOSTCXX", "CFLAGS", "CXXFLAGS", "ASFLAGS", "LDFLAGS", "PKG_CONFIG_PATH", "PKG_CONFIG_LIBDIR", "PKG_CONFIG_SYSROOT_DIR") if key in self.environment},
            "assembly": "portable C/C++ for x86_64 static PIC" if self.name == "x265" and self.architecture == "x86_64" else "upstream architecture optimizations",
        }
        destination = self.prefix / "share/qgc-gstreamer/manifests"
        destination.mkdir(parents=True, exist_ok=True)
        (destination / f"{self.name}.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
        print(f"Installed {self.name} {self.info['version']} ({self.architecture}) -> {self.prefix}", flush=True)


def main() -> int:
    if os.name == "nt":
        # Inherited by child tools: missing Windows DLLs must fail without a modal dialog.
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--architecture", required=True, choices=("x86_64", "arm64"))
    parser.add_argument("--codec", required=True, choices=tuple(LOCK["sources"]))
    parser.add_argument("--prefix", required=True, type=absolute_path)
    parser.add_argument("--work-root", required=True, type=absolute_path)
    parser.add_argument("--dependency-root", required=True, type=absolute_path)
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--prepare-only", action="store_true", help="Verify and prepare sources/tools; do not configure or compile")
    arguments = parser.parse_args()
    if arguments.jobs < 1:
        parser.error("--jobs must be positive")
    recipe = Recipe(arguments)
    recipe.prepare()
    if arguments.prepare_only:
        print(f"Prepared {recipe.name} {recipe.info['version']}: {recipe.source}", flush=True)
        return 0
    getattr(recipe, arguments.codec)()
    recipe.finish()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"codecs: {error}", file=sys.stderr)
        raise SystemExit(1) from error

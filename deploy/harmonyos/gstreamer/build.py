"""Build the target GStreamer SDK used by the HarmonyOS application."""

import argparse
import ctypes
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path


def run(command, log, *, cwd=None, env=None):
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open('w', encoding='utf-8') as stream:
        result = subprocess.run(command, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(encoding='utf-8', errors='replace')[-5000:])
        raise RuntimeError(f'{log.name} failed ({result.returncode})')


def download(url, destination, expected):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists():
        temporary = destination.with_suffix(destination.suffix + '.part')
        with urllib.request.urlopen(url, timeout=60) as response, temporary.open('wb') as output:
            shutil.copyfileobj(response, output)
        temporary.replace(destination)
    if hashlib.sha256(destination.read_bytes()).hexdigest() != expected:
        raise RuntimeError(f'Source checksum mismatch: {destination.name}')
    return destination


def main():
    if os.name == 'nt':
        ctypes.windll.kernel32.SetErrorMode(0x8003)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--architecture', choices=['x86_64', 'arm64'], required=True)
    parser.add_argument('--dependency-root', type=Path, required=True)
    parser.add_argument('--component', choices=['libffi', 'glib', 'gstreamer', 'gst-plugins-base', 'gst-plugins-good', 'gst-plugins-bad', 'gst-plugins-ugly', 'gst-libav', 'gst-rtsp-server', 'glib-networking', 'manifest'], required=True)
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[3]
    recipe = Path(__file__).resolve().parent
    root = repository / 'tmp/gstreamer/1.28.4'
    dependency = args.dependency_root.resolve()
    prefix = root / args.architecture / 'install'
    build = root / args.architecture / 'build' / args.component
    logs = root / args.architecture / 'logs'
    prefix.mkdir(parents=True, exist_ok=True)
    (prefix / 'lib/pkgconfig').mkdir(parents=True, exist_ok=True)
    sdk = dependency / 'ohos-sdk'
    if not (sdk / 'native/llvm/bin/clang.exe').exists():
        sdk = Path(os.environ['OHOS_SDK_ROOT'])
    sdk = sdk.absolute()
    llvm = sdk / 'native/llvm/bin'
    ninja = sdk / 'native/build-tools/cmake/bin/ninja.exe'
    host = dependency / 'toolchains/Qt/Tools/mingw1310_64/bin'
    pkgconfig = Path(os.environ.get('QGC_GST_PKG_CONFIG', str(dependency / 'toolchains/gstreamer-1.22.12-mingw-x86_64/bin/pkg-config.exe')))
    meson = dependency / 'vcpkg-downloads/tools/meson-1.9.0-12a69e/meson.py'
    if not meson.exists():
        raise RuntimeError('Set up the pinned host Meson under the dependency directory first')
    env = os.environ.copy()
    env.update({'PYTHONDONTWRITEBYTECODE': '1', 'GIT_TERMINAL_PROMPT': '0', 'VCPKG_DISABLE_METRICS': '1', 'VCPKG_MAX_CONCURRENCY': '1'})
    env['PATH'] = os.pathsep.join([str(root / 'host-tools/winflexbison'), str(ninja.parent), str(host), str(dependency / 'toolchains/Qt/6.12.0/mingw_64/bin'), str(Path(sys.executable).parent), env['PATH']])
    env['OHOS_SDK_ROOT'] = str(sdk)
    target = 'aarch64-linux-ohos' if args.architecture == 'arm64' else 'x86_64-linux-ohos'
    flags = [f'--target={target}', f'--sysroot={(sdk / "native/sysroot").as_posix()}', '-D__MUSL__']
    if args.component == 'libffi':
        vcpkg = dependency / 'vcpkg/vcpkg.exe'
        ffi_port = json.loads((dependency / 'vcpkg/ports/libffi/vcpkg.json').read_text(encoding='utf-8'))
        if ffi_port.get('version') != '3.5.2':
            raise RuntimeError('This SDK recipe requires the libffi 3.5.2 vcpkg port')
        triplets = root / 'triplets'
        triplets.mkdir(exist_ok=True)
        triplet = ('arm64' if args.architecture == 'arm64' else 'x64') + '-qgc-ohos'
        base = ('arm64' if args.architecture == 'arm64' else 'x64') + '-ohos.cmake'
        host_triple = 'aarch64-unknown-linux-musl' if args.architecture == 'arm64' else 'x86_64-unknown-linux-musl'
        (triplets / (triplet + '.cmake')).write_text(f'include("{(dependency / "vcpkg/triplets/community" / base).as_posix()}")\nset(VCPKG_MAKE_BUILD_TRIPLET "--host={host_triple}")\nset(VCPKG_BUILD_TYPE release)\n', encoding='utf-8')
        env['VCPKG_DOWNLOADS'] = str(dependency / 'vcpkg-downloads')
        binary_cache = root / 'binary-cache'
        binary_cache.mkdir(exist_ok=True)
        env['VCPKG_DEFAULT_BINARY_CACHE'] = str(binary_cache)
        installed = root / 'vcpkg-installed'
        run([str(vcpkg), 'install', 'libffi:' + triplet, '--classic', '--disable-metrics', '--host-triplet=x64-mingw-dynamic', '--overlay-triplets=' + str(triplets), '--x-buildtrees-root=' + str(root / 'vcpkg-buildtrees'), '--x-install-root=' + str(installed), '--x-packages-root=' + str(root / 'vcpkg-packages')], logs / 'libffi-build.log', env=env)
        lib = installed / triplet
        shutil.copytree(lib / 'include', prefix / 'include', dirs_exist_ok=True)
        shutil.copy2(lib / 'lib/libffi.a', prefix / 'lib/libffi.a')
        text = (lib / 'lib/pkgconfig/libffi.pc').read_text(encoding='utf-8')
        text = '\n'.join('prefix=' + prefix.as_posix() if line.startswith('prefix=') else line for line in text.splitlines()) + '\n'
        (prefix / 'lib/pkgconfig/libffi.pc').write_text(text, encoding='utf-8')
        shutil.copytree(lib / 'share/libffi', prefix / 'share/licenses/libffi', dirs_exist_ok=True)
        print('libffi ' + args.architecture + ' complete')
        return
    sources = {item['name']: item for item in json.loads((recipe / 'sources-lock.json').read_text(encoding='utf-8'))}

    def source(name):
        item = sources[name]
        path = root / 'sources' / (name + '-' + item['version'])
        archive_name = item['url'].split('/')[-1]
        if not archive_name.endswith(('.xz', '.gz', '.bz2')):
            archive_name = name + '-' + item['version'] + '.tar.gz'
        archive = download(item['url'], root / 'downloads' / archive_name, item['sha256'])
        if not path.exists():
            with tarfile.open(archive) as entries:
                entries.extractall(root / 'sources', filter='data')
        if not path.exists():
            raise RuntimeError('Source archive layout mismatch: ' + name)
        return path

    if args.component == 'manifest':
        plugins = sorted(file.stem.removeprefix('libgst') for file in (prefix / 'lib/gstreamer-1.0').glob('libgst*.a'))
        required = {'app', 'coreelements', 'isomp4', 'libav', 'matroska', 'mpegtsdemux', 'multifile', 'opengl', 'openh264', 'playback', 'rtp', 'rtpmanager', 'rtsp', 'sdpelem', 'tcp', 'typefindfunctions', 'udp', 'videoparsersbad', 'vpx', 'videoconvertscale', 'videotestsrc', 'x264', 'x265', 'mpegtsmux'}
        missing = required - set(plugins)
        if missing:
            raise RuntimeError('Missing required static plugins: ' + ', '.join(sorted(missing)))
        ca = 'etc/ssl/certs/ca-certificates.crt'
        if not (prefix / 'lib/gio/modules/libgioopenssl.a').is_file() or not (prefix / ca).is_file():
            raise RuntimeError('The OpenSSL GIO backend and CA bundle are required')
        result = {'version': '1.28.4', 'architecture': args.architecture, 'abi': 'arm64-v8a' if args.architecture == 'arm64' else 'x86_64', 'defaultLibrary': 'static', 'glEnabled': True, 'plugins': plugins, 'gioModules': ['openssl'], 'caBundle': ca, 'sourceManifests': ['sources-lock.json', 'codecs-lock.json']}
        metadata = prefix / 'share/qgc-gstreamer'
        metadata.mkdir(parents=True, exist_ok=True)
        for name in result['sourceManifests']:
            shutil.copy2(recipe / name, metadata / name)
        for name, item in sources.items():
            directory = root / 'sources' / (name + '-' + item['version'])
            if not directory.is_dir():
                continue
            notices = prefix / 'share/licenses' / name
            notices.mkdir(parents=True, exist_ok=True)
            for license_file in directory.iterdir():
                if license_file.is_file() and license_file.name.startswith(('COPYING', 'LICENSE', 'PATENTS')):
                    shutil.copy2(license_file, notices / license_file.name)
        pcre = root / 'sources/glib-2.84.4/subprojects/pcre2-10.44'
        pcre_notices = prefix / 'share/licenses/pcre2'
        pcre_notices.mkdir(parents=True, exist_ok=True)
        for name in ['LICENCE', 'COPYING', 'AUTHORS']:
            if (pcre / name).is_file():
                shutil.copy2(pcre / name, pcre_notices / name)
        (prefix / 'build-manifest.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result))
        return
    project = source(args.component)
    if args.component == 'glib':
        proxy = source('proxy-libintl')
        shutil.copytree(proxy, project / 'subprojects/proxy-libintl', dirs_exist_ok=True)
    patch_names = {
        'glib': ['glib-ohos-no-journald.patch', 'glib-netlink-header-check.patch', 'glib-windows-cross-gdb-install.patch'],
        'gstreamer': ['gstreamer-windows-cross-gdb-install.patch', 'gstreamer-ohos-bus-clock-underflow.patch'],
        'gst-plugins-base': ['gst-plugins-base-egl-prefer-eglgetprocaddress.patch', 'gst-plugins-base-gles-norm16-capability.patch'],
        'gst-libav': ['gst-libav-ffmpeg9-video-codec-ids.patch'],
        'glib-networking': ['glib-networking-static-install.patch'],
    }.get(args.component, [])
    patch_program = Path(shutil.which('git.exe')).parent.parent / 'usr/bin/patch.exe'
    for name in patch_names:
        patch = recipe / 'patches' / name
        check = subprocess.run([str(patch_program), '-p1', '--dry-run', '--forward', '--batch', '-i', str(patch)], cwd=project, env=env, capture_output=True)
        if check.returncode == 0:
            run([str(patch_program), '-p1', '--forward', '--batch', '-i', str(patch)], logs / (name + '.log'), cwd=project, env=env)
        else:
            reverse = subprocess.run([str(patch_program), '-p1', '--dry-run', '--reverse', '--force', '--batch', '-i', str(patch)], cwd=project, env=env, capture_output=True)
            if reverse.returncode:
                raise RuntimeError('Patch does not match the pinned source: ' + name)
    # These .pc files describe public OHOS SDK libraries, never host OpenGL libraries.
    for name, library, version in [('egl', 'EGL', '1.5'), ('glesv2', 'GLESv2', '3.2')]:
        (prefix / 'lib/pkgconfig' / (name + '.pc')).write_text(f'Name: {name}\nDescription: HarmonyOS SDK {name}\nVersion: {version}\nLibs: -l{library}\n', encoding='utf-8')
    flex_archive = download('https://github.com/lexxmark/winflexbison/releases/download/v2.5.24/win_flex_bison-2.5.24.zip', root / 'downloads/win_flex_bison-2.5.24.zip', '39c6086ce211d5415500acc5ed2d8939861ca1696aee48909c7f6daf5122b505')
    flex = root / 'host-tools/winflexbison'
    if not (flex / 'win_flex.exe').exists():
        import zipfile
        with zipfile.ZipFile(flex_archive) as entries:
            entries.extractall(flex)
    cross = root / args.architecture / 'cross.ini'
    binaries = {'c': [(llvm / 'clang.exe').as_posix()], 'cpp': [(llvm / 'clang++.exe').as_posix()], 'ar': [(llvm / 'llvm-ar.exe').as_posix()], 'strip': [(llvm / 'llvm-strip.exe').as_posix()], 'pkg-config': [pkgconfig.as_posix()]}
    for tool in ['glib-mkenums', 'glib-genmarshal']:
        binaries[tool] = [Path(sys.executable).as_posix(), (prefix / 'bin' / tool).as_posix()]
    text = '[binaries]\n' + ''.join(key + ' = ' + repr(value) + '\n' for key, value in binaries.items())
    text += f"[host_machine]\nsystem = 'ohos'\ncpu_family = '{'aarch64' if args.architecture == 'arm64' else 'x86_64'}'\ncpu = '{'aarch64' if args.architecture == 'arm64' else 'x86_64'}'\nendian = 'little'\n"
    text += "[properties]\nneeds_exe_wrapper = true\npkg_config_libdir = " + repr([(prefix / 'lib/pkgconfig').as_posix(), (prefix / 'lib/gstreamer-1.0/pkgconfig').as_posix()]) + '\n'
    text += '[built-in options]\n' + ''.join(key + ' = ' + repr(flags) + '\n' for key in ['c_args', 'cpp_args', 'c_link_args', 'cpp_link_args'])
    cross.write_text(text, encoding='utf-8')
    native = root / args.architecture / 'native.ini'
    native.write_text('[binaries]\n' + ''.join(key + ' = ' + repr(value) + '\n' for key, value in binaries.items() if key.startswith('glib-')), encoding='utf-8')
    common = ['-Dauto_features=disabled', '-Dtests=disabled', '-Ddoc=disabled']
    common_full = [*common, '-Dexamples=disabled', '-Dintrospection=disabled', '-Dnls=disabled']
    options = {
        'glib': ['-Dtests=false', '-Dinstalled_tests=false', '-Ddocumentation=false', '-Dman-pages=disabled', '-Dnls=disabled', '-Dintrospection=disabled', '-Dlibmount=disabled', '-Dselinux=disabled', '-Dlibelf=disabled', '-Dsysprof=disabled', '-Ddtrace=disabled', '-Dsystemtap=disabled'],
        'gstreamer': [*common_full, '-Dbenchmarks=disabled', '-Dptp-helper=disabled', '-Dtools=disabled', '-Dlibunwind=disabled', '-Dlibdw=disabled', '-Dcheck=enabled', '-Dcoretracers=enabled'],
        'gst-plugins-base': [*common_full, '-Dtools=disabled', '-Dgl=enabled', '-Dgl_api=gles2', '-Dgl_platform=egl', '-Dgl_winsys=egl,surfaceless', '-Degl_module_name=libEGL.so', '-Dgles2_module_name=libGLESv2.so', '-Dapp=enabled', '-Dplayback=enabled', '-Dtypefind=enabled', '-Dvideoconvertscale=enabled', '-Dvideotestsrc=enabled', '-Dtcp=enabled', '-Daudioconvert=enabled', '-Daudioresample=enabled', '-Dgio=enabled', '-Dencoding=enabled'],
        'gst-plugins-good': [*common, '-Dexamples=disabled', '-Dnls=disabled', '-Drtp=enabled', '-Drtpmanager=enabled', '-Drtsp=enabled', '-Dudp=enabled', '-Disomp4=enabled', '-Dmatroska=enabled', '-Dmultifile=enabled', '-Dvpx=enabled', '-Ddebugutils=enabled'],
        'gst-plugins-bad': [*common_full, '-Dvideoparsers=enabled', '-Dmpegtsdemux=enabled', '-Dmpegtsmux=enabled', '-Dsdp=enabled', '-Dopenh264=enabled', '-Dx265=enabled', '-Ddebugutils=enabled', '-Dgpl=enabled'],
        'gst-plugins-ugly': [*common, '-Dnls=disabled', '-Dx264=enabled', '-Dgpl=enabled'],
        'gst-libav': common,
        'gst-rtsp-server': [*common, '-Dexamples=disabled', '-Dintrospection=disabled', '-Drtspclientsink=enabled'],
        'glib-networking': ['-Dauto_features=disabled', '-Dgnutls=disabled', '-Dopenssl=enabled', '-Dlibproxy=disabled', '-Dgnome_proxy=disabled', '-Dinstalled_tests=false'],
    }[args.component]
    # Only GLib's locked PCRE2/proxy subprojects may be used as fallbacks. Every GStreamer
    # component must resolve dependencies from the already-built SDK instead of fetching wraps.
    wrap_mode = [] if args.component == 'glib' else ['--wrap-mode=nofallback']
    configure = [sys.executable, '-B', str(meson), 'setup', str(build), str(project), '--cross-file', str(cross), '--native-file', str(native), '--prefix', str(prefix), '--libdir', 'lib', '--buildtype', 'release', '-Ddefault_library=static', '-Db_staticpic=true', *wrap_mode, *options]
    if (build / 'meson-private/coredata.dat').exists():
        configure.append('--reconfigure')
    print('Configuring ' + args.component + ' for ' + args.architecture, flush=True)
    run(configure, logs / (args.component + '-configure.log'), cwd=root, env=env)
    print('Building ' + args.component + ' (one compiler job)', flush=True)
    run([str(ninja), '-C', str(build), '-j1', 'install'], logs / (args.component + '-build.log'), env=env)
    if args.component == 'glib-networking':
        module = prefix / 'lib/gio/modules/libgioopenssl.a'
        members = subprocess.run(
            [str(llvm / 'llvm-ar.exe'), 't', str(module)],
            text=True, capture_output=True, check=True, env=env,
        ).stdout.split()
        # Upstream tls/openssl/meson.build builds the static module with
        # extract_all_objects(recursive: true), so libtlsbase objects are already inside the
        # installed module. Merging libtlsbase.a in again only duplicates those members, so
        # reject such an archive instead of shipping 38 duplicate symbol definitions.
        duplicates = [name for name in members if 'libtlsbase.a.p/' in name]
        if duplicates:
            raise SystemExit('libgioopenssl.a contains duplicate tlsbase objects: ' + ', '.join(duplicates))
        ca = sources['ca-certificates']
        ca_file = download(ca['url'], root / 'downloads/ca-certificates.crt', ca['sha256'])
        destination = prefix / 'etc/ssl/certs/ca-certificates.crt'
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ca_file, destination)
    notices = prefix / 'share/licenses' / args.component
    notices.mkdir(parents=True, exist_ok=True)
    for name in ['COPYING', 'COPYING.LIB', 'LICENSE', 'LICENSE_EXCEPTION']:
        if (project / name).is_file():
            shutil.copy2(project / name, notices / name)
    print(args.component + ' ' + args.architecture + ' complete', flush=True)


if __name__ == '__main__':
    main()

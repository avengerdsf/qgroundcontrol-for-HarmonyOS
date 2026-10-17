"""Verify a source-free HAP retains every runtime payload byte from its baseline."""

import argparse
import hashlib
import json
from pathlib import Path
from zipfile import BadZipFile, ZipFile


LOOSE_SOURCE_PREFIX = 'resources/rawfile/qml/'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(archive, name):
    with archive.open(name) as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify(baseline, candidate):
    with ZipFile(baseline) as old, ZipFile(candidate) as new:
        old_names, new_names = set(old.namelist()), set(new.namelist())
        require(len(old_names) == len(old.infolist()) and len(new_names) == len(new.infolist()),
                'Duplicate ZIP entry names')
        removed = {name for name in old_names if name.startswith(LOOSE_SOURCE_PREFIX)}
        require(not any(name.startswith(LOOSE_SOURCE_PREFIX) for name in new_names), 'Loose application sources remain')
        require(new_names == old_names - removed, 'A non-source runtime entry was removed or added')

        old_module, new_module = json.loads(old.read('module.json')), json.loads(new.read('module.json'))
        version = new_module['app']['versionCode']
        require(version >= old_module['app']['versionCode'], 'Installation version decreased')
        old_module['app']['versionCode'] = version
        require(old_module == new_module, 'Module behavior, permissions or device declarations changed')
        require(new_module['app']['versionName'] == '5.0.8', 'Unexpected application version')
        require(sorted(new_module['module']['deviceTypes']) == ['2in1', 'phone', 'tablet'], 'Unexpected device declarations')
        old_pack, new_pack = json.loads(old.read('pack.info')), json.loads(new.read('pack.info'))
        old_pack['summary']['app']['version']['code'] = version
        require(old_pack == new_pack, 'Package metadata changed beyond the installation version')

        hashes = {}
        for name in sorted(new_names - {'module.json', 'pack.info'}):
            before, after = digest(old, name), digest(new, name)
            require(before == after, f'Runtime payload changed: {name}')
            hashes[name] = after
        libraries = {name: value for name, value in hashes.items() if name.endswith('.so')}
        require(libraries and 'libs/arm64-v8a/libQGroundControl.so' in libraries, 'Missing application library')
        return {
            'status': 'passed', 'baselineBytes': baseline.stat().st_size,
            'candidateBytes': candidate.stat().st_size,
            'removedLooseFiles': len(removed),
            'removedLooseBytes': sum(old.getinfo(name).file_size for name in removed),
            'unchangedPayloadEntries': len(hashes), 'unchangedNativeLibraries': len(libraries),
            'nativeSha256': libraries, 'versionCode': version,
            'metadataOnlyChanges': ['module.json:app.versionCode', 'pack.info:summary.app.version.code'],
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    project_tmp = Path(__file__).resolve().parents[2] / 'tmp'
    output = args.output.resolve()
    if not output.is_relative_to(project_tmp.resolve()):
        parser.error('Report must stay inside project tmp')
    try:
        report = verify(args.baseline, args.candidate)
    except (BadZipFile, OSError, ValueError, KeyError) as error:
        report = {'status': 'failed', 'error': str(error)}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key != 'nativeSha256'}, ensure_ascii=False))
    return 0 if report['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())

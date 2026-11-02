#!/usr/bin/env python3
"""Verify and archive a native Linux development candidate without touching player slots."""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tarfile
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if not line.startswith(('#', '//')) and ':' in line and '=' in line:
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    if Path(cache.get('CMAKE_HOME_DIRECTORY', '/nonexistent')).resolve() != source:
        parser.error('The build directory must belong to this source tree.')
    if cache.get('MEGA_VULKAN_TESTS') != 'ON':
        parser.error('Configure this build with MEGA_VULKAN_TESTS=ON to verify rendered workflows.')
    if platform.system() != 'Linux':
        parser.error('This packaging workflow currently supports Linux.')
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    label = f'mega-dormitory-development-{platform.machine()}-{stamp}'
    archive = output / f'{label}.tar.gz'
    log_path = output / f'{label}-checks.log'
    env = dict(os.environ)
    env.pop('MEGA_DATA_DIR', None)
    with tempfile.TemporaryDirectory(prefix='mega-release-check-') as temp, log_path.open('w') as log:
        temp = Path(temp)
        env.update(TMPDIR=str(temp), XDG_DATA_HOME=str(temp / 'user-data'),
                   VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation')

        def run(command, cwd=source):
            log.write('\n' + json.dumps([str(v) for v in command]) + '\n')
            log.flush()
            subprocess.run(command, cwd=cwd, env=env, stdout=log,
                           stderr=subprocess.STDOUT, check=True)

        run(['cmake', '--build', str(build), '--parallel', '4'])
        run(['ctest', '--test-dir', str(build), '--output-on-failure'])
        run([os.sys.executable, str(source / 'tools/check_locales.py')])
        run([os.sys.executable, str(source / 'tools/verify_assets.py')])
        package = temp / 'Mega Dormitory'
        run(['cmake', '--install', str(build), '--prefix', str(package)])
        # Relocation and an unrelated working directory must not require the source tree.
        moved = temp / 'relocated package' / 'Mega Dormitory'
        moved.parent.mkdir()
        package.rename(moved)
        run([str(moved / 'mega_dormitory'), '--verify-runtime'], cwd=temp)
        files = {}
        for file in sorted(moved.rglob('*')):
            if file.is_symlink():
                raise RuntimeError(f'Unexpected package symlink: {file}')
            if file.is_file():
                files[file.relative_to(moved).as_posix()] = hashlib.sha256(file.read_bytes()).hexdigest()
        report = {'channel': 'development', 'created_utc': stamp, 'files_sha256': files,
                  'checks': ['native_ctest_including_vulkan', 'localization', 'downloaded_asset_hashes',
                             'relocated_runtime_resources'],
                  'production_ready': False,
                  'remaining_release_work': 'docs/status.md and docs/releasing.md'}
        (moved / 'candidate-report.json').write_text(json.dumps(report, indent=2) + '\n')
        partial = output / f'{label}.partial'
        try:
            with tarfile.open(partial, 'w:gz') as tar:
                tar.add(moved, arcname='Mega Dormitory')
            partial.replace(archive)
        finally:
            partial.unlink(missing_ok=True)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(archive.suffix + '.sha256').write_text(f'{digest}  {archive.name}\n')
    print(f'Verified development candidate: {archive}')
    print(f'Check log: {log_path}')


if __name__ == '__main__':
    main()

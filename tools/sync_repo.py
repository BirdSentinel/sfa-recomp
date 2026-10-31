"""Export reviewable source only; leave the working game and builds in place."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / 'git'
SDK = ROOT / 'tools/rexglue-sdk-terrain'
MARKER = '.repo-export.json'


def sdk_git(*args):
    return subprocess.check_output(
        ['git', '-c', f'safe.directory={SDK.as_posix()}', '-C', str(SDK), *args])


def collect():
    files = {}

    def add(source, destination=None):
        path = ROOT / source
        files[destination or source] = path.read_bytes()

    for folder, extensions in {
        'src': {'.cpp', '.h'}, 'tests': {'.cpp', '.h', '.py', '.ps1'},
        'tools': {'.py', '.ps1'}, 'cmake': {'.cmake'},
    }.items():
        for path in sorted((ROOT / 'snupi' / folder).rglob('*')):
            if path.is_file() and path.suffix in extensions and '__pycache__' not in path.parts:
                add(path.relative_to(ROOT).as_posix())
    for name in ['CMakeLists.txt', 'CMakePresets.json', 'snupi_manifest.toml',
                 '.gitattributes', 'Controls.md', 'Display-settings.md',
                 'High-FPS-physics.md', 'Internal-resolution.md',
                 'Launch-options.md', 'Settings-menu.md',
                 'start.bat', 'start-2x.bat', 'start-4x.bat',
                 'generated/rexglue.cmake', 'resources/snupi.rc']:
        add('snupi/' + name)
    for name in ['CMakeLists.txt', 'depth_bias_gpu_test.cpp',
                 'geometry_depth_test.cpp', 'viewport_depth_test.cpp', 'rtv_geometry_depth.h',
                 'snupi_presenter.cpp', 'snupi_presenter.h', 'presentation_policy.h']:
        add('snupi/renderer/' + name)
    add('tools/sync_repo.py')
    add('tools/prepare_sdk.ps1')
    add('tools/repository/README.txt', 'README.txt')
    add('tools/repository/gitignore', '.gitignore')
    add('tools/repository/AGENTS.md', 'AGENTS.md')
    add('tools/rexglue-sdk-terrain/LICENSE', 'patches/rexglue-LICENSE')
    base = sdk_git('rev-parse', 'HEAD').decode().strip()
    files['patches/rexglue-base.json'] = (json.dumps({
        'repository': 'https://github.com/rexglue/rexglue-sdk.git',
        'commit': base, 'sdk_version': '0.10.0',
    }, indent=2) + '\n').encode()
    # A complete diff, not a collection of historical candidate patches.
    untracked = sdk_git('ls-files', '--others', '--exclude-standard').decode().strip()
    if untracked:
        raise RuntimeError('Review untracked SDK files before exporting: ' + untracked)
    patch = sdk_git('diff', '--no-ext-diff', '--no-textconv', '--binary', 'HEAD', '--',
                    'include', 'src')
    changed = sdk_git('diff', '--name-only', 'HEAD').decode().splitlines()
    if any(not name.startswith(('include/', 'src/')) for name in changed):
        raise RuntimeError('SDK changes outside include/src need export review')
    files['patches/rexglue-rtv.patch'] = patch
    # The user may maintain a repository landing page directly in the mirror.
    # Preserve it verbatim; the generated build guide remains README.txt.
    readme = DEST / 'README.md'
    if readme.is_file():
        files['README.md'] = readme.read_bytes()
    return files


def safe_target(relative):
    target = DEST / relative
    if target.is_symlink() or not target.resolve().is_relative_to(DEST.resolve()):
        raise RuntimeError('Unsafe export path: ' + relative)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Verify without changing files')
    args = parser.parse_args()
    if (ROOT / MARKER).exists():
        raise SystemExit('This is already the export. Run sync from the original workspace.')
    if DEST.is_symlink():
        raise SystemExit('Export directory must not be a symlink')
    files = collect()
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())}
    previous = {}
    marker = DEST / MARKER
    if marker.exists():
        previous = json.loads(marker.read_text())['files']
    expected_marker = (json.dumps({'format': 1, 'files': hashes}, indent=2) + '\n').encode()
    failures = []
    for name, data in files.items():
        target = safe_target(name)
        if target.exists() and target.read_bytes() == data:
            continue
        if args.check:
            failures.append(name)
            continue
        # Refuse to overwrite edits made directly in the upload folder.
        if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() != previous.get(name):
            raise RuntimeError('Export has independent edits; reconcile first: ' + name)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    for name in previous.keys() - files.keys():
        target = safe_target(name)
        if not target.exists():
            continue
        if args.check:
            failures.append(name + ' (stale)')
        elif hashlib.sha256(target.read_bytes()).hexdigest() == previous[name]:
            target.unlink()  # Only remove a previously exported, unchanged file.
        else:
            raise RuntimeError('Stale export has independent edits: ' + name)
    # Unknown files can include game files or outputs. Never silently approve
    # them for upload, and never delete user-added content automatically.
    extras = [p.relative_to(DEST).as_posix() for p in DEST.rglob('*')
              if p.is_file() and '.git' not in p.relative_to(DEST).parts
              and p.relative_to(DEST).as_posix() not in {*files, MARKER}]
    if extras:
        raise RuntimeError('Unmanaged files in export need review: ' + ', '.join(extras))
    if args.check:
        if not marker.exists() or marker.read_bytes() != expected_marker:
            failures.append(MARKER)
        if failures:
            raise SystemExit('Export is out of date: ' + ', '.join(failures))
    else:
        marker.write_bytes(expected_marker)
    print(f'{"Verified" if args.check else "Synced"} {len(files)} source files, '
          f'{sum(map(len, files.values())):,} bytes: {DEST}')


if __name__ == '__main__':
    main()

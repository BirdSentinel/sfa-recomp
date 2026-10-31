"""Prepare locally supplied game assets; none of the resulting files belong in Git."""
import argparse
from pathlib import Path
import re
from PIL import Image
from add_display_settings import patch


def update_config(data):
    text = data.decode('utf-8-sig')
    values = {'LockFrameRateToVSync': 'FALSE',
              'DesiredFrameRateLocked': '1000000',
              'DesiredFrameRateUnlocked': '1000000'}
    section = re.search(r'(?mi)^\[ENGINE\.DISPLAY\][^\r\n]*\r?\n', text)
    if not section:
        raise ValueError('Missing ENGINE.DISPLAY section')
    next_section = re.search(r'(?m)^\[', text[section.end():])
    end = section.end() + next_section.start() if next_section else len(text)
    body = text[section.end():end]
    for name, value in values.items():
        body, count = re.subn(r'(?mi)^' + re.escape(name) + r'\s*=[^\r\n]*',
                              name + '=' + value, body)
        if not count:
            body += f'{name}={value}\n'
    return (text[:section.end()] + body + text[end:]).encode('utf-8')


def main():
    project = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, default=project.parent / 'decomp/extracted')
    args = parser.parse_args()
    root = args.game_root.resolve()
    package = root / 'Packages/UI/FrontEnd.pkg'
    configs = [root / 'Config' / name for name in
               ('FlyingAce_Xbox360.ini', 'FlyingAceCustom_Xbox360.ini')]
    image = Image.open(root / 'SFA.png').convert('RGBA')
    # Prefer the established pre-display baseline when preparing an existing tree.
    backup = package.with_suffix('.pkg.before-display-settings')
    baseline = backup.read_bytes() if backup.exists() else package.read_bytes()
    updated = patch(baseline)
    config_updates = [(path, update_config(path.read_bytes())) for path in configs]
    # Compute and validate all updates before writing anything.
    if not backup.exists():
        backup.write_bytes(baseline)
    package.write_bytes(updated)
    for path, data in config_updates:
        config_backup = path.with_suffix(path.suffix + '.before-fps-selector')
        if not config_backup.exists():
            config_backup.write_bytes(path.read_bytes())
        path.write_bytes(data)
    icon = project / 'resources/SFA.ico'
    icon.parent.mkdir(parents=True, exist_ok=True)
    image.save(icon, sizes=[(16,16), (24,24), (32,32), (48,48), (64,64)])
    print(f'Prepared native settings, FPS configuration and executable icon from {root}')


if __name__ == '__main__':
    main()

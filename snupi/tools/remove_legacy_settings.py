"""Remove obsolete Xbox downloads/save-device UI from FrontEnd.pkg (package v25).

Keeps export IDs and offsets stable so existing script references remain valid.
Only the settings interface's layout/render lists and navigation are changed.
Run with the package path; a byte-for-byte backup is made before the first edit.
"""
import argparse
import struct
from pathlib import Path


class Package:
    def __init__(self, data):
        self.data = bytearray(data)
        magic, version = struct.unpack_from('>II', data)
        assert (magic, version) == (0x9E2A83C1, 25), 'Unsupported package'
        count, pos = struct.unpack_from('>II', data, 16)
        self.names = []
        for _ in range(count):
            length = struct.unpack_from('>H', data, pos)[0]
            pos += 2
            self.names.append(data[pos:pos + length - 1].decode('ascii'))
            pos += length
        pos = struct.unpack_from('>I', data, 32)[0]
        count = struct.unpack_from('>I', data, pos)[0]
        pos += 4
        self.exports = [None]
        for _ in range(count):
            fields = struct.unpack_from('>HHHHiiIIIIIII', data, pos)
            self.exports.append(dict(name=self.names[fields[0]],
                cls=self.names[fields[2]], parent=fields[4],
                offset=fields[8], size=fields[7], table=pos))
            pos += 49 + struct.unpack_from('>I', data, pos + 44)[0] * 8
        assert pos == struct.unpack_from('>I', data, 12)[0]

    def find(self, name, cls, parent=None):
        matches = [i for i, e in enumerate(self.exports) if e and
                   e['name'] == name and e['cls'] == cls and
                   (parent is None or e['parent'] == parent)]
        assert len(matches) == 1, (name, matches)
        return matches[0]

    def properties(self, index):
        export = self.exports[index]
        pos = export['offset'] + 12
        end = export['offset'] + export['size']
        result = []
        while pos < end:
            name = struct.unpack_from('>H', self.data, pos)[0]
            if name == 0:
                assert pos + 4 == end, 'Unexpected object trailer'
                return result
            size = struct.unpack_from('>I', self.data, pos + 8)[0]
            assert pos + 14 + size <= end
            result.append((self.names[name], bytes(self.data[pos:pos + 14]),
                           bytes(self.data[pos + 14:pos + 14 + size])))
            pos += 14 + size
        raise AssertionError('Missing property terminator')

    def edit(self, index, changes):
        export = self.exports[index]
        start, size = export['offset'], export['size']
        output = self.data[start:start + 12]
        found = set()
        for name, header, value in self.properties(index):
            if name in changes:
                value = changes[name](value)
                header = bytearray(header)
                struct.pack_into('>I', header, 8, len(value))
                found.add(name)
            output += header + value
        assert found == set(changes)
        output += bytes(4)
        assert len(output) <= size, 'Edits must fit the original export'
        self.data[start:start + size] = output + bytes(size - len(output))
        struct.pack_into('>I', self.data, export['table'] + 20, len(output))
        struct.pack_into('>I', self.data, export['table'] + 28, len(output))
        export['size'] = len(output)


def patch(data):
    pkg = Package(data)
    root = pkg.find('m_settings', 'Interface')
    button = lambda name: pkg.find(name, 'Element_2DInterfaceButton', root)
    gamer = button('3_Download_GamerPics')
    avatar = button('4_Download_AvatarAssets')
    framerate = button('6_FramerateLock')
    save = button('2_SelectSaveDevice')
    autosave = button('5_AutosaveState')
    manager = pkg.find('Manager', 'Element_2DElementManager', root)
    removed = {gamer, avatar, save}

    def remove_refs(value):
        words = list(struct.unpack('>' + 'I' * (len(value) // 4), value))
        assert words[0] == len(words) - 1
        refs = [ref for ref in words[1:] if ref not in removed]
        assert len(words) - 1 - len(refs) in (0, 1, 3)
        return struct.pack('>' + 'I' * (len(refs) + 1), len(refs), *refs)

    def vertical(value):
        assert struct.unpack('>I', value)[0] in (7, 5, 4)
        return struct.pack('>I', 4)

    pkg.edit(root, {'m_aRenderList': remove_refs})
    pkg.edit(manager, {'m_aChildren': remove_refs, 'm_vertical': vertical})
    pkg.edit(framerate, {'m_downElement': lambda _: struct.pack('>I', autosave)})
    pkg.edit(autosave, {'m_upElement': lambda _: struct.pack('>I', framerate)})
    # Normalize the now-unused button too, so original and previously patched
    # packages converge to identical output without discarding the first backup.
    pkg.edit(save, {'m_upElement': lambda _: struct.pack('>I', framerate)})

    # Reparse the actual output and validate lists and both navigation directions.
    checked = Package(pkg.data)
    prop = lambda obj, key: next(v for n, h, v in checked.properties(obj) if n == key)
    for obj, key in [(root, 'm_aRenderList'), (manager, 'm_aChildren')]:
        value = prop(obj, key)
        refs = struct.unpack('>' + 'I' * (len(value) // 4), value)[1:]
        assert not removed.intersection(refs)
    assert struct.unpack('>I', prop(framerate, 'm_downElement'))[0] == autosave
    assert struct.unpack('>I', prop(autosave, 'm_upElement'))[0] == framerate
    first = button('0_musicVolume')
    for direction in ('m_downElement', 'm_upElement'):
        current, visited = first, set()
        while current not in visited:
            assert current not in removed
            visited.add(current)
            current = struct.unpack('>I', prop(current, direction))[0]
        # Four visible settings plus the pre-existing hidden UI-scale button.
        assert current == first and len(visited) == 5
    assert len(pkg.data) == len(data)
    for i, export in enumerate(checked.exports[1:], 1):
        assert export['offset'] == pkg.exports[i]['offset']
        assert export['offset'] + export['size'] <= len(data)
    return bytes(pkg.data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    args = parser.parse_args()
    original = args.package.read_bytes()
    result = patch(original)
    assert patch(result) == result, 'Patch must be idempotent'
    if result == original:
        print('Legacy settings already removed; validation passed.')
    else:
        backup = args.package.with_suffix(args.package.suffix + '.before-legacy-settings')
        if backup.exists():
            assert patch(backup.read_bytes()) == result, 'Existing backup differs'
        else:
            backup.write_bytes(original)
        args.package.write_bytes(result)
        print('Removed download and save-device rows; navigation validation passed.')
        print(f'Backup: {backup}')

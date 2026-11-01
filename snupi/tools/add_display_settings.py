"""Add native Resolution and Display Mode selectors to FrontEnd.pkg v25.

Reuses the game's button/text states, CurrentIndex variables and input actions.
Builds deterministically from the pre-display backup; preserves all export IDs.
The executable supplies localization IDs 0x7000..0x7202 and supported-mode count.
"""
import argparse
import struct
from pathlib import Path
from remove_legacy_settings import Package, patch as remove_legacy


def word(n):
    return struct.pack('>I', n)


def array(refs):
    return word(len(refs)) + b''.join(word(x) for x in refs)


class Editor:
    def __init__(self, data):
        self.pkg = Package(data)
        self.objects = [None]
        for i, e in enumerate(self.pkg.exports[1:], 1):
            t = e['table']
            length = 49 + struct.unpack_from('>I', data, t + 44)[0] * 8
            self.objects.append([bytearray(data[t:t + length]),
                bytearray(data[e['offset']:e['offset'] + 12]), self.pkg.properties(i)])

    def get(self, i, key):
        return next(v for n, h, v in self.objects[i][2] if n == key)

    def set(self, i, key, value):
        props = self.objects[i][2]
        for j, (n, h, v) in enumerate(props):
            if n == key:
                h = bytearray(h)
                struct.pack_into('>I', h, 8, len(value))
                props[j] = n, bytes(h), value
                return
        raise AssertionError((i, key))

    def append_refs(self, i, key, refs):
        old = self.get(i, key)
        self.set(i, key, word(struct.unpack_from('>I', old)[0] + len(refs)) +
                 old[4:] + b''.join(word(x) for x in refs))

    def clone(self, source_ids, parent_map=None):
        mapping = {i: len(self.objects) + j for j, i in enumerate(source_ids)}
        mapping.update(parent_map or {})
        for i in source_ids:
            table, prefix, props = self.objects[i]
            table, prefix = bytearray(table), bytearray(prefix)
            for data, offset in [(table, 8), (prefix, 8)]:
                parent = struct.unpack_from('>I', data, offset)[0]
                struct.pack_into('>I', data, offset, mapping.get(parent, parent))
            for offset in range(48, len(table) - 1, 8):
                ref = struct.unpack_from('>I', table, offset)[0]
                struct.pack_into('>I', table, offset, mapping.get(ref, ref))
            new_props = []
            for n, h, v in props:
                kind = struct.unpack_from('>H', h, 4)[0]
                if kind == 10:  # object property
                    ref = struct.unpack('>I', v)[0]
                    v = word(mapping.get(ref, ref))
                elif kind == 0x55:  # these template arrays contain object refs
                    refs = struct.unpack('>' + 'I' * (len(v) // 4), v)[1:]
                    v = array([mapping.get(ref, ref) for ref in refs])
                new_props.append((n, h, v))
            self.objects.append([table, prefix, new_props])
        return mapping

    def write(self):
        tables, bodies = [], []
        for table, prefix, props in self.objects[1:]:
            table = bytearray(table)
            body = prefix + b''.join(h + v for n, h, v in props) + bytes(4)
            # Include new dependencies so the package loader preloads new children.
            deps = {struct.unpack_from('>I', table, x)[0]
                    for x in range(48, len(table) - 1, 8)}
            added = []
            for n, h, v in props:
                kind = struct.unpack_from('>H', h, 4)[0]
                refs = ([struct.unpack('>I', v)[0]] if kind == 10 else
                        list(struct.unpack('>' + 'I' * (len(v) // 4), v)[1:])
                        if kind == 0x55 else [])
                for ref in refs:
                    if ref and ref not in deps:
                        added.append(word(ref) + bytes.fromhex('00004000'))
                        deps.add(ref)
            count = struct.unpack_from('>I', table, 44)[0]
            table[-1:-1] = b''.join(added)
            struct.pack_into('>I', table, 44, count + len(added))
            struct.pack_into('>I', table, 20, len(body))
            struct.pack_into('>I', table, 28, len(body))
            tables.append(table)
            bodies.append(body)
        start = struct.unpack_from('>I', self.pkg.data, 32)[0]
        header = self.pkg.data[:start] + word(len(tables))
        pos = len(header) + sum(map(len, tables))
        struct.pack_into('>I', header, 12, pos)
        for table, body in zip(tables, bodies):
            struct.pack_into('>I', table, 24, pos)
            pos += len(body)
        return bytes(header + b''.join(tables) + b''.join(bodies))


def patch(data):
    e = Editor(remove_legacy(data))
    root, manager, framerate, autosave = 49633, 49694, 49802, 49706
    buttons = [49792, 49782, 49772]  # Resolution, Display Mode, VSync
    all_vars, texts = [], []
    for slot, button in enumerate(buttons):
        # Keep native interface ownership. Distinct FName instances also avoid
        # collisions in the interface's variable and element lookup tables.
        variables = e.clone([49640, 49641])
        proxy, values = variables[49640], variables[49641]
        for obj in [proxy, values]:
            for blob in e.objects[obj][:2]:
                struct.pack_into('>H', blob, 2, 40 + slot)
        all_vars.extend([proxy, values])
        locbase, count = [(0x7100, 32), (0x7200, 3), (0x7400, 2)][slot]
        value = bytearray(e.get(values, 'm_aVariable')[:14])
        struct.pack_into('>I', value, 8, 4 + count * 4)
        e.set(values, 'm_aVariable', value + array(list(range(locbase, locbase + count))) + bytes(4))
        subtree = e.clone(list(range(49812, 49824)),
                          {framerate: button, 49641: values})
        text = subtree[49812]
        for blob in e.objects[text][:2]:
            struct.pack_into('>H', blob, 2, 40 + slot)
        texts.append(text)
        # Retain the old row's own name and matching default state tree.
        default_state = e.get(button, 'm_pDefaultState')
        e.objects[button][2] = list(e.objects[framerate][2])
        e.set(button, 'm_pDefaultState', default_state)
        label = bytearray(e.get(button, 'm_buttonText'))
        struct.pack_into('>I', label, 14, 0x7000 + slot)
        e.set(button, 'm_buttonText', label)
        e.set(button, 'm_id', word(8 + slot))
        e.set(button, 'm_aChildren', array([text]))
        for switch, case_src, action_src in [(50815, 50816, 50818), (50832, 50833, 50835)]:
            clone = e.clone([case_src, action_src], {49638: proxy})
            case, action = clone[case_src], clone[action_src]
            # Same switch scope: allocate a distinct FName instance.
            for blob in e.objects[case][:2]:
                struct.pack_into('>H', blob, 2, 40 + slot)
            e.set(case, 'm_Value', struct.pack('>f', 8 + slot))
            e.set(case, 'm_aActions', array([action]))
            e.append_refs(switch, 'm_aCases', [case])
        # Space / controller A follows the same save-and-close path as the
        # existing volume, framerate and autosave selectors.
        condition = e.clone([50897])[50897]
        for blob in e.objects[condition][:2]:
            struct.pack_into('>H', blob, 2, 40 + slot)
        e.set(condition, 'm_ComparisonValue', struct.pack('>f', 8 + slot))
        e.append_refs(50895, 'm_aConditions', [condition])
    # Repurpose Autosave's existing selector and text, with a separate saved
    # renderer scale. Disconnect both the toggle popup and its init assignment.
    label = bytearray(e.get(autosave, 'm_buttonText'))
    struct.pack_into('>I', label, 14, 0x7003)
    e.set(autosave, 'm_buttonText', label)
    value = bytearray(e.get(49643, 'm_aVariable')[:14])
    struct.pack_into('>I', value, 8, 12)
    e.set(49643, 'm_aVariable', value + array([0x7500, 0x7501]) + bytes(4))
    for case, source in [(50821, 50818), (50838, 50835)]:
        action = e.clone([source], {e.pkg.exports[source]['parent']: case, 49638: 49642})[source]
        e.set(case, 'm_aActions', array([action]))
    e.set(50410, 'm_aActions', array([]))
    e.set(50416, 'm_aActions', array([]))
    e.set(50868, 'm_aActions', array([]))
    # Replace the legacy boolean toggle and its confirmation dialog with a
    # CurrentIndex selector. Leave its existing label and button design intact.
    value = bytearray(e.get(49641, 'm_aVariable')[:14])
    struct.pack_into('>I', value, 8, 4 + 9 * 4)
    e.set(49641, 'm_aVariable', value + array(list(range(0x7300, 0x7309))) + bytes(4))
    for case, source in [(50819, 50818), (50836, 50835)]:
        action = e.clone([source], {e.pkg.exports[source]['parent']: case, 49638: 49640})[source]
        e.set(case, 'm_aActions', array([action]))
    # Do not let the old saved On/Off value overwrite the persisted FPS index.
    actions = struct.unpack('>' + 'I' * (len(e.get(50779, 'm_aActions')) // 4), e.get(50779, 'm_aActions'))[1:]
    e.set(50779, 'm_aActions', array([i for i in actions if i != 50790]))
    e.set(50603, 'm_aActions', array([]))
    e.set(50608, 'm_aActions', array([]))
    chain = [49728, 49750, 49684, framerate, *buttons, autosave]
    for j, button in enumerate(chain):
        e.set(button, 'm_upElement', word(chain[j - 1]))
        e.set(button, 'm_downElement', word(chain[(j + 1) % len(chain)]))
    e.set(manager, 'm_aChildren', array(chain))
    e.set(manager, 'm_vertical', word(7))
    e.append_refs(root, 'm_aRenderList', buttons + texts)
    e.append_refs(root, 'm_aVariables', all_vars)
    # New selectors use left/right. Do not show the obsolete Download footer.
    # Native selection handler already shows its accept prompt only for ids <2.
    result = e.write()
    p = Package(result)
    prop = lambda i, key: next(v for n, h, v in p.properties(i) if n == key)
    assert prop(manager, 'm_aChildren') == array(chain)
    assert 49772 in struct.unpack('>' + 'I' * (len(prop(root, 'm_aRenderList')) // 4), prop(root, 'm_aRenderList'))[1:]
    for j, button in enumerate(chain):
        assert prop(button, 'm_upElement') == word(chain[j - 1])
        assert prop(button, 'm_downElement') == word(chain[(j + 1) % len(chain)])
    for i in range(1, len(p.exports)):
        p.properties(i)  # validate every serialized property boundary
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    args = parser.parse_args()
    backup = args.package.with_suffix('.pkg.before-display-settings')
    if not backup.exists():
        backup.write_bytes(args.package.read_bytes())
    result = patch(backup.read_bytes())
    args.package.write_bytes(result)
    print('Installed Resolution, Display Mode, VSync and Internal resolution rows; package and navigation validation passed.')

"""Independently validate the runtime C++ menu patch with the Python parser."""
import sys
import struct
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from remove_legacy_settings import Package

original = Package(Path(sys.argv[1]).read_bytes())
patched = Package(Path(sys.argv[2]).read_bytes())
def prop(p, i, key): return next(v for n, h, v in p.properties(i) if n == key)
def integer(i, key): return struct.unpack('>I', prop(patched, i, key))[0]
def refs(i, key):
    v = prop(patched, i, key)
    count, = struct.unpack_from('>I', v)
    assert len(v) == 4 + count * 4
    return struct.unpack_from('>' + 'I'*count, v, 4)
assert len(original.data) == len(patched.data)
assert len(original.exports) == len(patched.exports)
hidden = {2699, 2731, 2763, 2689, 2859}
assert not hidden.intersection(refs(2654, 'm_aRenderList'))
assert refs(2709, 'm_aChildren') == (2753, 2773, 2783, 2793, 2721)
assert refs(2741, 'm_aChildren') == ()
assert integer(2753, 'm_pParent') == 2709
assert refs(2803, 'm_aChildren') == (2919, 2709, 2875)
assert integer(2709, 'm_vertical') == 5
chain = [2753, 2773, 2783, 2793, 2721]
for j, button in enumerate(chain):
    assert integer(button, 'm_upElement') == chain[j-1]
    assert integer(button, 'm_downElement') == chain[(j+1) % len(chain)]
assert integer(5008, 'm_element') == 2753
def y(p, i):
    value = prop(p, i, 'm_Value')
    pos = 0
    while struct.unpack_from('>H', value, pos)[0]:
        name = p.names[struct.unpack_from('>H', value, pos)[0]]
        size = struct.unpack_from('>I', value, pos + 8)[0]
        if name == 'y': return struct.unpack_from('>f', value, pos + 14)[0]
        pos += 14 + size
    raise AssertionError('Missing y')

geometry = set()
for area, position, scale, background_position in [(2715, 2714, 2922, 2924), (2720, 2719, 2929, 2931)]:
    geometry.update((area, position, scale, background_position))
    old_height = y(original, scale) * 0.7250000238418579
    new_height = y(patched, scale) * 0.7250000238418579
    # Panel shrinks while its top edge, original row pitch, and text size stay fixed.
    assert new_height < old_height
    assert abs(y(patched, background_position) + new_height - y(original, background_position) - old_height) < 1e-6
    assert abs(y(patched, area) / 4 - y(original, area) / 5) < 1e-6
    assert abs(new_height - y(patched, area) - 0.08) < 1e-6
    assert abs(y(patched, position) - y(patched, background_position)) < 1e-6
anchors = {2819, 2824, 2813, 2835, 2846, 2857}
for anchor, scale, position in [(2819, 2922, 2924),
                                *[(i, 2929, 2931) for i in (2824, 2813, 2835, 2846, 2857)]]:
    # safe_BL sits at -0.9. Every slide state must place the paper bottom at -1.
    bottom = -0.9 + y(patched, anchor) + y(patched, position) - y(patched, scale) * 0.7250000238418579
    assert abs(bottom + 1.0) < 1e-6
changed = {2654, 2709, 2741, 2803, 5008, 46351, *chain, *geometry, *anchors}
for i, e in enumerate(original.exports[1:], 1):
    other = patched.exports[i]
    assert e['offset'] == other['offset']
    patched.properties(i)
    if i not in changed:
        assert e == other
        assert original.data[e['offset']:e['offset']+e['size']] == patched.data[e['offset']:e['offset']+e['size']]
print('Hide Live graph passed: compact four-button layout, background bounds, navigation, initial selection and unrelated exports validated.')

# Normal and hidden menus both use the native loading sequence on Enlist Back.
normal = Package(Path(sys.argv[2] + '.normal').read_bytes())
for package in (normal, patched):
    props = dict((n, v) for n, h, v in package.properties(46351))
    assert props == {'m_nextInterface': struct.pack('>I', 2654)}
for i, e in enumerate(original.exports[1:], 1):
    if i != 46351:
        assert e == normal.exports[i]
        assert original.data[e['offset']:e['offset']+e['size']] == normal.data[e['offset']:e['offset']+e['size']]
print('Enlist Back uses default loading in both variants; normal menu and native animation states are preserved.')

"""Check hidden pause row, both navigation directions, and unchanged other UI."""
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
    return struct.unpack_from('>' + 'I' * count, v, 4)
assert len(original.data) == len(patched.data)
assert 15167 not in refs(15080, 'm_aRenderList')
chain = (15137, 15105, 15177, 15157, 15147, 15127)
assert refs(15115, 'm_aChildren') == chain
assert integer(15115, 'm_vertical') == 5
assert integer(15080, 'm_pDefaultElement') == chain[0]
for j, button in enumerate(chain):
    assert integer(button, 'm_upElement') == chain[j-1]
    assert integer(button, 'm_downElement') == chain[(j+1) % len(chain)]
changed = set()
for i in range(1, len(original.exports)):
    a, b = original.exports[i], patched.exports[i]
    assert a['offset'] == b['offset']
    if original.data[a['offset']:a['offset']+a['size']] != patched.data[b['offset']:b['offset']+b['size']]:
        changed.add(i)
assert changed == {15080, 15115, 15157, 15177}, changed
print('Pause menu removal, navigation, default selection, and unrelated UI checks passed')

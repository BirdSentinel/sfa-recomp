"""Validate the native settings graph and both controller selector directions."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from add_display_settings import patch
from remove_legacy_settings import Package

path = Path(sys.argv[1])
data = path.read_bytes()
assert patch(path.with_suffix('.pkg.before-display-settings').read_bytes()) == data
p = Package(data)


def prop(obj, key):
    return next(v for n, h, v in p.properties(obj) if n == key)


def integer(obj, key):
    return struct.unpack('>I', prop(obj, key))[0]


def refs(obj, key):
    values = struct.unpack('>' + 'I' * (len(prop(obj, key)) // 4), prop(obj, key))
    assert values[0] == len(values) - 1
    return values[1:]


for slot, button in enumerate([49792, 49782, 49772]):
    assert struct.unpack_from('>I', prop(button, 'm_buttonText'), 14)[0] == 0x7000 + slot
    assert integer(button, 'm_id') == 8 + slot
    assert integer(button, 'm_staticInterface') == integer(49802, 'm_staticInterface')
    assert button in refs(49633, 'm_aRenderList')
    text, = refs(button, 'm_aChildren')
    assert text in refs(49633, 'm_aRenderList')
    assert integer(text, 'm_pParent') == button
    values = integer(text, 'm_textVariable')
    assert values in refs(49633, 'm_aVariables')
    proxy, = [i for i in refs(49633, 'm_aVariables')
               if p.exports[i]['cls'] == 'GlobalVariable_CurrentIndex' and
               integer(i, 'm_staticRefVar') == values]
    payload = prop(values, 'm_aVariable')
    count, = struct.unpack_from('>I', payload, 14)
    assert count == [32, 3, 2][slot]
    assert struct.unpack_from('>I', payload, 18)[0] == [0x7100, 0x7200, 0x7400][slot]
    if slot == 2:
        assert struct.unpack_from('>2I', payload, 18) == (0x7400, 0x7401)
    assert any(integer(i, 'm_variable') == 49658 and
               struct.unpack('>f', prop(i, 'm_ComparisonValue'))[0] == 8 + slot
               for i in refs(50895, 'm_aConditions') if any(
                   n == 'm_ComparisonValue' for n, h, v in p.properties(i)))
    for switch, operator in [(50815, 0x2d3), (50832, 0x2d4)]:
        cases = [i for i in refs(switch, 'm_aCases')
                 if any(n == 'm_Value' and struct.unpack('>f', v)[0] == 8 + slot
                        for n, h, v in p.properties(i))]
        case, = cases
        action, = refs(case, 'm_aActions')
        assert integer(action, 'm_variable') == proxy
        assert integer(action, 'm_operator') >> 16 == operator
        assert struct.unpack('>f', prop(action, 'm_NewValue'))[0] == 1

render = refs(49633, 'm_aRenderList')
assert 49772 in render  # Repurposed VSync row, not Select Save Device.
chain = refs(49694, 'm_aChildren')
assert len(chain) == 8 and integer(49694, 'm_vertical') == 7  # existing hidden UI-scale row
for j, button in enumerate(chain):
    assert integer(button, 'm_upElement') == chain[j - 1]
    assert integer(button, 'm_downElement') == chain[(j + 1) % len(chain)]
assert integer(50901, 'm_ButtonEnum') == 0x03900000  # Space / controller A
assert refs(50888, 'm_aActions') == (50889,)
assert integer(50889, 'm_Trigger') == 50890  # native SaveAndClose
assert struct.unpack_from('>10I', prop(49641, 'm_aVariable'), 14) == (9, *range(0x7300, 0x7309))
for case, operator in [(50819, 0x2d3), (50836, 0x2d4)]:
    action, = refs(case, 'm_aActions')
    assert integer(action, 'm_variable') == 49640
    assert integer(action, 'm_operator') >> 16 == operator
assert 50790 not in refs(50779, 'm_aActions')
assert not refs(50603, 'm_aActions') and not refs(50608, 'm_aActions')
print('Display package tests passed: layout, values, +/- actions, Space/A confirm, navigation, deterministic rebuild.')

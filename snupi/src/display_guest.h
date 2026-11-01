#pragma once
#include <cstdint>

namespace snupi::display {
// Native FLocString is 44 bytes: the localization ID is at +36, after
// the reference-counted string storage. Its first word is not the ID.
template <typename ReadWord>
uint32_t VariableTag(ReadWord read, uint32_t object) {
  if (!object) return 0;
  const auto vtable = read(object);
  if (vtable < 0x82000000 || vtable >= 0x82be0000 ||
      read(vtable + 192) != 0x82302A98 || read(object + 344)) return 0;
  const auto count = read(object + 372), data = read(object + 380);
  if (!data || !count) return 0;
  const auto id = read(data + 36);
  return (id == 0x7100 && count == 32) || (id == 0x7200 && count == 3) ||
                 (id == 0x7300 && count == 9) || (id == 0x7400 && count == 2) || (id == 0x7500 && count == 2) ? id : 0;
}
}

#include "display_guest.h"
#include <cstdlib>
#include <iostream>
#include <unordered_map>

int main() {
  // Layout verified against the running game's settings objects. The string
  // header begins with a refcount of 1; localization IDs live 36 bytes later.
  constexpr uint32_t object = 0x4340d600, table = 0x82188db0, data = 0x41514e00;
  std::unordered_map<uint32_t, uint32_t> words = {
      {object, table}, {table + 192, 0x82302A98}, {object + 344, 0},
      {object + 372, 32}, {object + 380, data}, {data, 1}, {data + 36, 0x7100}};
  auto read = [&](uint32_t address) { return words.at(address); };
  auto check = [&](uint32_t expected) {
    if (snupi::display::VariableTag(read, object) != expected) std::abort();
  };
  check(0x7100);
  words[object + 372] = 3;
  words[data + 36] = 0x7200;
  check(0x7200);
  words[object + 372] = 9;
  words[data + 36] = 0x7300;
  check(0x7300);
  words[object + 372] = 2;
  words[data + 36] = 0x7400;
  check(0x7400);
  words[object + 372] = 3;
  check(0);  // VSync must have exactly Off and On.
  words[data + 36] = 0x34;  // original on/off array
  check(0);
  words[data + 36] = 0x7100;  // wrong count must not identify another array
  check(0);
  words[object + 344] = 123;
  check(0);
  words[object] = 0;
  check(0);
  if (snupi::display::VariableTag(read, 0) != 0) std::abort();
  std::cout << "Native display-variable identification tests passed\n";
}

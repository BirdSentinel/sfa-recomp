#include "intro_package.h"
#include "startup_options.h"
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
  auto require = [](bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
  };
  require(argc == 3, "Expected original package and patched output paths");
  char exe[] = "-skipintro", flag[] = "-skipintro", other[] = "--resolution_scale=2";
  char double_dash[] = "--skipintro", similar[] = "-skipintro-other";
  char* args[] = {exe, flag, other, double_dash, similar};
  const auto normalized = snupi::startup::NormalizeArguments(5, args);
  require(normalized == std::vector<std::string>{"-skipintro", "--skipintro",
      "--resolution_scale=2", "--skipintro", "-skipintro-other"}, "Argument alias");
  require(snupi::startup::NormalizeArguments(1, args)[0] == "-skipintro", "Normal launch");
  char hide[] = "-hidelive", hide_long[] = "--hidelive", hide_similar[] = "-hidelive-other";
  char* combined[] = {exe, flag, hide, hide_long, hide_similar};
  require(snupi::startup::NormalizeArguments(5, combined) ==
      std::vector<std::string>{"-skipintro", "--skipintro", "--hidelive", "--hidelive", "-hidelive-other"},
      "Hide Live alias must compose with skipintro and match exactly");
  std::ifstream input(argv[1], std::ios::binary);
  require(bool(input), "Read package");
  std::vector<uint8_t> original((std::istreambuf_iterator<char>(input)), {});
  auto patched = original;
  snupi::startup::PatchIntroPackage(patched);
  require(patched.size() == original.size(), "Package length preserved");
  auto word = [&](size_t offset) {
    return (uint32_t(patched[offset]) << 24) | (uint32_t(patched[offset + 1]) << 16) |
           (uint32_t(patched[offset + 2]) << 8) | patched[offset + 3];
  };
  require(word(0x40b1) == 1 && word(0x40ab) == 21, "Keep exactly one startup movie");
  require(word(0x409f) == 0, "Music cue must follow FrontEnd_Intro to index zero");
  require(std::equal(original.begin() + 0x40c6, original.begin() + 0x40d7,
                     patched.begin() + 0x40b5), "FrontEnd_Intro entry must remain intact");
  require(word(0x42cb) > 0 && word(0x42cb) < 0x3c23d70a,
          "Loading-screen wait must expire in under 0.01 seconds");
  require(word(0x42cb) == snupi::startup::kIntroGateTimerBits,
          "Runtime must be able to identify the movie-completion timer");
  require(word(0x4e77) == 0, "Save disclaimer must render nothing");
  require(word(0x3eef) > 0 && word(0x3eef) < 0x3c23d70a,
          "Save disclaimer must transition in under 0.01 seconds");
  for (size_t i = 0; i < original.size(); ++i) {
    const bool allowed = (i >= 5640 + 20 && i < 5640 + 24) ||
        (i >= 5640 + 28 && i < 5640 + 32) ||
        (i >= 0x409f && i < 0x40a3) ||
        (i >= 0x40ab && i < 0x40af) || (i >= 0x40b1 && i < 16481 + 306) ||
        (i >= 0x42cb && i < 0x42cf) || (i >= 0x3eef && i < 0x3ef3) ||
        (i >= 0x4e71 && i < 20043 + 149) ||
        (i >= 9327 + 20 && i < 9327 + 24) ||
        (i >= 9327 + 28 && i < 9327 + 32);
    require(allowed || original[i] == patched[i], "Unrelated package data changed");
  }
  auto corrupt = original;
  corrupt[0x40b7] ^= 1;
  bool rejected = false;
  try { snupi::startup::PatchIntroPackage(corrupt); }
  catch (const std::runtime_error&) { rejected = true; }
  require(rejected, "Unsupported movie list must be rejected");
  rejected = false;
  std::vector<uint8_t> truncated;
  try { snupi::startup::PatchIntroPackage(truncated); }
  catch (const std::runtime_error&) { rejected = true; }
  require(rejected, "Truncated package must be rejected");
  std::ofstream output(argv[2], std::ios::binary);
  output.write(reinterpret_cast<const char*>(patched.data()), patched.size());
  require(bool(output), "Write test output");
  std::cout << "Startup argument and package tests passed\n";
}

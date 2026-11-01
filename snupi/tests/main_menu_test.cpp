#include "main_menu_package.h"
#include <fstream>
#include <iterator>
#include <iostream>
int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) throw std::runtime_error("Expected input and output package paths");
  std::ifstream input(argv[1], std::ios::binary);
  if (!input) throw std::runtime_error("Missing input package");
  std::vector<uint8_t> original((std::istreambuf_iterator<char>(input)), {});
  auto patched = original;
  if (argc == 4) {
    snupi::startup::PatchPauseMenuPackage(patched);
    auto again = patched;
    snupi::startup::PatchPauseMenuPackage(again);
    if (again != patched || patched.size() != original.size())
      throw std::runtime_error("Pause patch must be idempotent and preserve length");
    std::ofstream output(argv[2], std::ios::binary);
    output.write(reinterpret_cast<const char*>(patched.data()), patched.size());
    if (!output) throw std::runtime_error("Pause output write failed");
    return 0;
  }
  snupi::startup::PatchMainMenuReturnAnimation(patched);
  auto normal = patched;
  snupi::startup::PatchMainMenuReturnAnimation(patched);
  if (patched != normal) throw std::runtime_error("Return animation patch not idempotent");
  std::ofstream normal_output(std::string(argv[2]) + ".normal", std::ios::binary);
  normal_output.write(reinterpret_cast<const char*>(normal.data()), normal.size());
  if (!normal_output) throw std::runtime_error("Normal package write failed");
  snupi::startup::PatchMainMenuPackage(patched);
  auto again = patched;
  snupi::startup::PatchMainMenuReturnAnimation(again);
  snupi::startup::PatchMainMenuPackage(again);
  if (again != patched || patched.size() != original.size())
    throw std::runtime_error("Patch must be idempotent and preserve length");
  for (auto corrupt : {std::vector<uint8_t>{}, std::vector<uint8_t>(original.begin(), original.begin()+100)}) {
    bool rejected = false;
    try { snupi::startup::PatchMainMenuPackage(corrupt); }
    catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) throw std::runtime_error("Truncated package accepted");
  }
  auto bad = original; bad[4] ^= 1;
  bool rejected = false;
  try { snupi::startup::PatchMainMenuPackage(bad); }
  catch (const std::runtime_error&) { rejected = true; }
  if (!rejected) throw std::runtime_error("Wrong version accepted");
  std::ofstream output(argv[2], std::ios::binary);
  output.write(reinterpret_cast<const char*>(patched.data()), patched.size());
  if (!output) throw std::runtime_error("Output write failed");
  std::cout << "Hide Live package bounds, idempotence and output tests passed\n";
}

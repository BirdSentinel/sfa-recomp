#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace snupi::startup {
inline std::vector<std::string> NormalizeArguments(int argc, char** argv) {
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    args.emplace_back(i > 0 && (arg == "-skipintro" || arg == "-showlive")
                          ? "-" + std::string(arg) : std::string(arg));
  }
  return args;
}
}  // namespace snupi::startup

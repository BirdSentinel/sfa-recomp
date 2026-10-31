#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace snupi::startup {
inline std::vector<std::string> NormalizeArguments(int argc, char** argv) {
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) {
    args.emplace_back(i > 0 && std::string_view(argv[i]) == "-skipintro"
                          ? "--skipintro" : argv[i]);
  }
  return args;
}
}  // namespace snupi::startup

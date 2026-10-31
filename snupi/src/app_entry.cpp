// Reuse the SDK lifecycle, normalizing the game's single-dash option before
// cvar parsing. The included SDK source keeps its original license notice.
#define wWinMain SnupiSdkWinMain
#define main SnupiSdkMain
#include "windowed_app_main_sdl.cpp"
#undef main
#undef wWinMain

#include "startup_options.h"

namespace {
int RunSnupi(int argc, char** argv) {
  auto args = snupi::startup::NormalizeArguments(argc, argv);
  std::vector<char*> pointers;
  for (auto& arg : args) pointers.push_back(arg.data());
  return RunWindowedApp(static_cast<int>(pointers.size()), pointers.data());
}
}

#if REX_PLATFORM_WIN32
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
  int argc = 0;
  auto wide = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!wide) return EXIT_FAILURE;
  auto args = WideArgsToUtf8(argc, wide);
  LocalFree(wide);
  std::vector<char*> pointers;
  for (auto& arg : args) pointers.push_back(arg.data());
  return RunSnupi(static_cast<int>(pointers.size()), pointers.data());
}
#else
int main(int argc, char** argv) { return RunSnupi(argc, argv); }
#endif

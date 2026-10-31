Snoopy Flying Ace recompilation - source repository
==================================================

This folder contains the source and configuration needed to reproduce the
current Windows DirectX 12 RTV build of snoopy.exe. It intentionally contains
no original game files, generated guest code, game icon, saves, SDK binaries,
executables, captures, or temporary output. Nothing has been uploaded for you.

Layout
------
snupi/             Application hooks, build files, tests, launchers and tools.
snupi/renderer/    RTV-only plugin build and depth regression checks.
patches/           Complete SDK modifications and exact upstream revision.
tools/             SDK preparation and development-workspace export scripts.

Local prerequisites
-------------------
Windows x64; Git; Visual Studio 2022 C++ Build Tools with Windows SDK;
Clang 20 (clang++ on PATH); CMake 3.25+; Ninja; Python 3.10+ and Pillow.
Use a Visual Studio x64 developer shell so linker and Windows SDK tools work.
Obtain ReXGlue SDK 0.10.0 for Windows x64 separately and place its installed
bin/include/lib/share directories under win-amd64/ at the repository root.
The SDK download is not part of this repository. The source dependency is
pinned in patches/rexglue-base.json, with its original license alongside it.

Preparing a fresh checkout (PowerShell)
--------------------------------------
1. Place your extracted game under decomp/extracted/, including default.xex,
   SFA.png, Config/, Packages/, and the remaining original game content.
   These files remain ignored by Git.

2. Fetch the pinned SDK source and apply this project's complete patch:

     powershell -ExecutionPolicy Bypass -File tools/prepare_sdk.ps1

   This is separate from the installed SDK package: the game links the SDK
   runtime but always compiles its own RTV-only graphics plugin and presenter
   from source. The presenter implements the native menu's live VSync toggle.

3. Install the local icon conversion dependency and prepare the game:

     python -m pip install Pillow
     python snupi/tools/prepare_game.py

   This regenerates SFA.ico locally, installs the native settings menu changes,
   and updates the original frame-budget settings for the FPS selector. It
   preserves pre-change backups and can be run again from those backups.

4. Generate guest code from your local executable BEFORE configuring CMake:

     Set-Location snupi
     ../win-amd64/bin/rexglue.exe codegen snupi_manifest.toml

   Generated guest C++ stays in ignored generated/default/. The small tracked
   generated/rexglue.cmake file is build glue, not translated game code.

5. Configure and build (still inside snupi/):

     cmake -S . -B out/build/win-amd64-relwithdebinfo -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_PREFIX_PATH="$PWD/../win-amd64" -DSNUPI_BUILD_STARTUP_TESTS=ON
     cmake --build out/build/win-amd64-relwithdebinfo --parallel 6
     ctest --test-dir out/build/win-amd64-relwithdebinfo --output-on-failure
     python tests/display_package_test.py ../decomp/extracted/Packages/UI/FrontEnd.pkg

6. Run start.bat (or start-2x.bat / start-4x.bat). The built executable is
   out/build/win-amd64-relwithdebinfo/snoopy.exe. Keep its staged runtime DLLs
   alongside it. The launchers locate the local game data and saves.

Only DirectX 12 RTV is supported. No Vulkan build or ROV selection is needed.
The existing CMake presets reflect the development machine's compiler paths;
the explicit configure command above avoids those machine-specific paths.

Keeping the upload folder current
--------------------------------
In the original D:/snupi-recomp workspace, git/ is a source-only mirror, not
the active build tree. Edit the working source and run:

  python tools/sync_repo.py
  python tools/sync_repo.py --check

The normal CMake build also refreshes the mirror automatically when it exists.
AGENTS.md requires the same sync after source/build/documentation changes.
The exporter includes a SHA-256 manifest, refreshes the SDK patch, removes
unchanged stale exports, and refuses to overwrite independent edits made in
the mirror. It preserves any .git history. It does not commit, push, or create
a remote. A standalone clone is edited directly and needs no nested mirror.

Upload the contents of this folder. Do not add the ignored local prerequisites
or generated game files. The .repo-export.json manifest is safe to commit.

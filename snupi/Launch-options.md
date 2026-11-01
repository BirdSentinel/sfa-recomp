# Launch options

`snoopy.exe` always sets `license_mask` to `1` during startup, overriding saved
config and command-line values. No license launch option is needed.

Returning from Enlist Now uses the main menu's native Load-to-Default slide
animation on every launch. The shipped Back action's invalid `Show` mode
override is removed in the cached package on every launch.

**Xbox LIVE**, **Leaderboards**, and **Download Content** are hidden from the
main menu by default. **Leaderboards** is also hidden in the in-game pause menu.
Keyboard/controller navigation skips those rows, and the main menu starts on
Enlist Now. The download notification badge is hidden as well.

Append `-showlive` to restore those buttons in both menus for the current launch:

```bat
snoopy.exe -showlive
snoopy.exe -skipintro -showlive
launch_1x.bat -showlive
```

`--showlive` is also accepted. The previous `-hidelive` flag has been replaced;
no flag is needed to hide the online entries. Omit `-showlive` to use the default
compact menu. Its four remaining buttons and paper panel stay anchored to the
viewport bottom, including during slide transitions. Both aspect-ratio layouts
keep the existing button and text sizes.

Separate cached packages keep default-hidden and `-showlive` launches isolated,
including when combined with `-skipintro`. Original game packages are preserved.
Achievements and settings are unchanged. Hiding menu entries does not disable
network services elsewhere in the game.

Append `-skipintro` to skip startup logos and the save disclaimer:

```bat
snoopy.exe -skipintro
launch_1x.bat -skipintro
launch_2x.bat -skipintro
```

For a Windows shortcut, append ` -skipintro` after the quoted executable path in
Target. `--skipintro` is also accepted. Omit the option to keep the original intro.
Other launch options can be combined with it as usual.

The game supports only DirectX 12 RTV. Direct launches use RTV automatically;
`--render_target_path_d3d12=rtv` remains accepted for existing launchers. ROV is
not selectable. The experimental native Vulkan renderer and its launchers have
been removed. Every build compiles and stages the game's RTV-only Xenos plugin.

The option bypasses the initial Xbox/ESRB splash sequence, `FrontEnd_Logos.wmv`,
and the save disclaimer. `FrontEnd_Intro.wmv` remains enabled, with its native
music cue moved to movie index zero so music starts with the intro. Press Start
waits for the native movie controller to finish the intro and for required game
loading to finish. There is no additional fixed 12-second wait.
It does not press Start, choose a profile,
or enter gameplay.

The original game packages and movies are unchanged. Each process redirects
the UI package directory to the cache for its selected startup options. The
startup package is patched only with `-skipintro`; menu packages reflect the
Live-button choice. Other packages are linked unchanged (copied if the cache
is on another volume). The actual game file-open path is checked against
the patched bytes at startup. Idle menu animations, later movies, saves, keyboard controls,
and controller controls retain their normal behavior.

Implementation: `src/app_entry.cpp` normalizes the single-dash option before the
SDK parses arguments. `src/skip_intro.cpp` installs the per-process VFS redirect
and marks the native splash sequence complete after renderer initialization.
The dedicated splash PNG draw is suppressed as well, including the first draw
inside renderer initialization, so the Xbox Live Arcade image cannot flash.
It holds the loading-screen timer until the FMV controller advances out of its
intro phase, then lets the original timer event release the title screen. A
decoder-open failure releases the wait too, preventing an unavailable movie
from trapping the game on its loading screen.
`src/intro_package.h` validates the shipped v25 package before changing only the
intro movie array and timer; unsupported package layouts fail explicitly.

Configure with `-DSNUPI_BUILD_STARTUP_TESTS=ON`, build `snupi_startup_test`, and run
`ctest --test-dir out/build/win-amd64-relwithdebinfo --output-on-failure` to check
argument matching, package bounds, preserved data, and invalid-input rejection.

The Hide Live test also writes `test-hidelive.pkg` in the build directory. Its
menu graph can be checked independently with:

```text
python tests/main_menu_package_test.py ../decomp/extracted/Packages/UI/FrontEnd.pkg out/build/win-amd64-relwithdebinfo/test-hidelive.pkg
```

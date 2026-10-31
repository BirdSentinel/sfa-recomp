# Display settings

The game's Help & Options → Settings menu includes **Resolution** and
**Display Mode**, using its existing selector rows, arrows, value boxes and sounds.
Use the controller's normal left/right menu controls, or their keyboard mappings
(A/D for the left stick; Shift+Left/Right for the D-pad). Changes apply immediately
and are saved in `saves/display-settings.txt` beside the game.
Space / controller A confirms and closes Settings, just like the original rows.

- Windowed: the selected resolution is the window's client size.
- Borderless: fills the desktop at its native resolution. The resolution row
  stays at the desktop dimensions while this mode is selected.
- Fullscreen: switches the monitor to the selected supported resolution. The
  desktop mode is restored on focus loss or exit.

Resolution controls the output size; the existing 1x/2x renderer scale remains
independent. Choices come from the monitor attached to the game window at launch.
Unsupported saved resolutions fall back to the desktop size. A failed display
change restores the previous choice. Explicit `--fullscreen` (borderless) or
`--no-fullscreen` launch arguments override the saved mode; the standard launch
scripts no longer force windowed mode.

The download and save-device entries remain absent. `tools/add_display_settings.py`
rebuilds the native package from `FrontEnd.pkg.before-display-settings`, preserving
the first backup and existing export IDs. The executable supplies display labels
through a per-process localization cache, leaving original localization files intact.

**Lock Framerate** offers Off, 30fps, 60fps, 90fps, 120fps, 144fps, 165fps,
180fps and 240fps. Left/right changes the cap immediately; Space/controller A
confirms and closes Settings. The choice persists in `saves/framerate.txt`.
The limiter paces the shared game presentation path, covering gameplay and menus.
Off removes the new limiter. A cap is an upper limit; rendering, video playback,
and hardware performance can result in a lower actual frame rate.

The old console boolean toggle, confirmation popup and saved On/Off-to-index
assignment are disconnected. Legacy locked/unlocked INI targets are set above
practical rendering rates (a one-microsecond budget), and guest vblank throttling
is disabled so it cannot override the chosen cap. Frame timing and simulation
delta calculations remain native. The limiter sleeps using a high-resolution
Windows timer and finishes with a short precision wait; it does not accumulate
catch-up frames after loading stalls.

`--frame_limit_audit=true` logs measured presentation rates for diagnostics.

Validation:

```text
python tests/display_package_test.py ../decomp/extracted/Packages/UI/FrontEnd.pkg
snupi.exe --display_settings_selftest=true
```

The opt-in runtime self-test cycles all three modes and two resolutions, verifies
actual window/display sizes and persistence, then restores the original settings.
Windows mode changes require a normal desktop process; a restricted sandbox may
reject them. The self-test uses a separate temporary settings file.

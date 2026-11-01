# Controls

Keyboard and the first physical controller share player 1. You can switch between
them during play without changing modes. Controller buttons, analog sticks,
triggers, and vibration keep their existing behavior.

| Xbox controller input | Keyboard |
| --- | --- |
| Left stick | W / A / S / D |
| Right stick | Arrow keys |
| A / confirm | Space or semicolon |
| B / back | Backspace or quote |
| X | L |
| Y | P |
| Left / right trigger | Q / E (or I / O) |
| Left / right bumper | 1 / 3 |
| Left / right stick click | F / K |
| D-pad | Shift + arrow keys |
| Start / pause | Enter or X |
| Back | Tab or Z |

Focus the game window to use the keyboard. Bindings use exact modifiers: release
Shift, Ctrl, and Alt for ordinary bindings; hold Shift for the D-pad.

The installed game reads `decomp/extracted/snupi.toml` beside `snupi.exe`, including
when started with `launch_1x.bat` or `launch_2x.bat`. Restart the game after changing
this file. Mouse look is disabled so the right stick uses arrow keys.

Future builds enable keyboard emulation by default in `SnupiApp::OnPreSetup`.
To disable it, set `mnk_mode = false` in the executable's `snupi.toml`, or launch
with `--no-mnk_mode`. The physical controller backend is unchanged.

SDK debug, settings, console, and achievement overlay shortcuts are disabled,
including saved rebindings. F1–F12 cannot open these overlays. Use the game's
own menus for settings and achievements.

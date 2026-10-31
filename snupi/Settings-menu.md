# Settings menu cleanup

The installed `decomp/extracted/Packages/UI/FrontEnd.pkg` omits Download Gamer
Pictures, Download Avatar Awards, and Select Save Device. Restart the game to
reload this package. Autosave and the actual save system remain unchanged.

`tools/remove_legacy_settings.py` applies the change reproducibly to package
version 25. It removes the buttons from the settings render list and dynamic
manager children, changes the manager from seven visible rows to four, and links
Lock Framerate and Autosave directly in both navigation directions.
Existing button IDs and all export offsets are preserved. Download resources
remain in the package so unrelated references remain valid.

The script creates `FrontEnd.pkg.before-legacy-settings` before the first change.
Restore that file over `FrontEnd.pkg` with the game closed to undo the change.

Validation: reparsed all 82,949 exports; only the five intended objects changed;
verified navigation cycles in both directions exclude all three removed buttons;
verified applying the patch again produces identical bytes. The existing hidden
UI-scale button remains unchanged. The prior download-only patch passed a game
startup check. Visual verification remains unavailable because computer-use
access to Snupi was denied; the save-device removal was checked structurally.

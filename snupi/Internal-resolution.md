# Internal resolution

Help & Options -> Settings has an Internal resolution (restart) selector in
place of Autosave. Left/right selects exactly 1x or 2x; Space/controller A uses
the existing confirm-and-close action. The saved choice applies on the next
launch. Default is 1x.

- 1x: native 1152 x 640 full-scene rendering.
- 2x: 2304 x 1280 full-scene rendering (twice each dimension).

This uses the renderer's original integer draw scaling and leaves the native
scene allocation, shared textures and window-resolution logic unchanged.
The display Resolution option still controls the window/output size separately.
2x increases GPU load and memory use.

The menu choice is saved as 1 or 2 in saves/internal-resolution.txt, and takes
precedence over legacy resolution_scale / draw_resolution_scale arguments and
scale shortcuts. Invalid or missing saved values use 1x. Renderer startup logs
the actual selected scale. No automatic window-to-scene sizing is installed.

Autosave is permanently On. The native player getter reinstates savingEnabled
for profiles previously set to Off, and the native PlayerSavingEnabled action
cannot disable it. The old settings toggle and confirmation actions are
removed. The normal checks for a valid player/save destination still apply.

--internal_resolution_selftest=true checks both saved choices, rejects invalid
indices, verifies that an Off local-player flag becomes On through the native
getter, and leaves the user's chosen scale intact. Use a separate test user-data
directory. Both 1x and 2x renderer startup paths are checked independently.

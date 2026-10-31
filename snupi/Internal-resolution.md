# Internal resolution

Close the game before selecting a different scale. All launchers use the
optimized RelWithDebInfo build, normal RTV renderer, and existing save data.

- `start.bat`: native resolution (renderer default).
- `start-2x.bat`: 2x width and 2x height, or four times the pixels.
- `start-4x.bat`: 4x width and 4x height, or sixteen times the pixels.

The same shortcuts are provided in both existing build directories.
You can also run `start.bat --resolution_scale=2` or
`start.bat --resolution_scale=4` from a command prompt.

These options scale internal rendering, independently of the window size.
For a 1280x720 render target, 2x is 2560x1440 and 4x is 5120x2880.
Higher scaling increases GPU load and memory use and may reduce frame rate.

The installed ReXGlue 0.10.0 Xenos plugin declares `resolution_scale` as the
combined setting for `draw_resolution_scale_x` and `draw_resolution_scale_y`.
Its help strings confirm that these are draw-resolution scales. The renderer
may reduce unsupported scales and reports that in the game log. Visual
correctness and performance at 2x and 4x still require a gameplay check on
the selected GPU.

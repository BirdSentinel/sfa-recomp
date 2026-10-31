# High-frame-rate physics correction

The game's `DesiredFrameRateUnlocked` target is 120 in
`decomp/extracted/Config/FlyingAceCustom_Xbox360.ini`. Both launchers retain
60 Hz guest video timing. Rendering FPS and simulated elapsed time are separate.

## Cause and correction

The main loop calculates an elapsed-time delta and passes it through
`sub_82904960` / `sub_82903980` to scene wrapper `sub_8281D6C8`.
The wrapper discards that argument and passes the float constant at
0x8213B810 (1/30 second) to PhysX simulate (`sub_826412B0`, return address
0x8281D744) once per rendered frame. This doubles physics speed near 60 FPS.

`src/guest_fixes.cpp` preserves the incoming delta in a scoped thread-local
pointer and substitutes it only at that specific simulate call. Both original
functions still execute. The original maximum substep of 1/60 second, maximum
of two substeps, scene synchronization, and other PhysX callers are unchanged.
The delta comes from the game's existing timing and time-scale path, not from
an assumed frame rate or a global clock multiplier.

## Validation (2026-10-01)

- Debug preset rebuilt and linked successfully after refreshing the moved
  project's CMake paths.
- User confirmed normal flight speed in the same level with Lock Framerate off.
- A 20.1328-second read-only gameplay sample recorded 1,134 guest presents:
  56.33 FPS average. PhysX accumulated 20.0768 seconds, or 0.9972 simulated
  seconds per wall-clock second.
- PhysX step values followed the game's supplied delta (approximately
  0.013-0.021 seconds in this sample), rather than a constant 0.033333 seconds.
- Measurement used guest device +16772 (present count) and PhysX scene +1284
  (accumulated simulation time). Host window repaints are excluded.

The configured target is 120 FPS; this Debug gameplay sample did not sustain
120 FPS. The correction fixes speed independently of the achieved frame rate.

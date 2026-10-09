# SA-MP background acceptance — 2026-10-09

Tested with GTA SA 1.0 US, the original SA-MP client and the reconstructed client
build 299, in two dedicated installations. This is test tooling; players do not
need this ASI to use the replacement samp.dll.

The first background trial **confined the user's mouse**. Disabling GTA's usual
cursor routine was insufficient: original SA-MP calls ClipCursor itself at RVA
0x7D703. With no_activate enabled, the plugin now intercepts cursor confinement,
recentering, capture and focus imports in the game EXE and samp.dll. These are
module-local hooks, not global user32 hooks. They do not repeatedly call
ClipCursor(NULL), which could interfere with another application's capture.

The final Release ASI SHA-256 is
`0aae06b885876ffe241a499a1588850a8dd9c3a80d7659bf4350c0b19ec06a69`.

Observed in the final paired trial:

- Both clients rendered off-screen. Direct GPU screenshots include SA-MP chat.
- Each client accepted 60 local-pad updates independently. Displacements were
  2.504 m and 2.926 m on different axes; this is not a trajectory parity test.
- The other client stayed still during each command; both stayed still at idle.
- Explicit release, deadline expiry and rejection of 301-frame input worked.
- A 70-second watchdog took 1,378 samples: the cursor confinement rectangle
  stayed at the full desktop, with no detected change. The plugin recorded
  2,421 and 1,848 intercepted mouse requests by the movement checkpoint.
- Both games were closed after testing. Eight Node transport tests passed;
  the Win32 Release build and seven conversion compile-time assertions passed.

Capture also required handling 10-bit back buffers and refreshing the real
Direct3D device Present hook after SA-MP replaces its proxy. Early black captures,
missing-overlay captures and timeouts are retained as failed/provisional trials.

Limits: this validates ground movement and capture in these installations.
Firing, vehicles, natural parachute use, all device resets/mod combinations,
raw mouse camera paths and SA-MP shortcuts polling OS keys are not certified.
The watchdog is bounded evidence, not a guarantee of permanent desktop isolation.
Pad injection does not synthesize all histories/timers inside native UpdatePads.

Evidence is preserved in the SA-MP workspace at
`re/tooling/gta-sa-mcp/` and
`.local/development-captures/20261009-gta-sa-mcp/`.
See the README for configuration and local build commands.

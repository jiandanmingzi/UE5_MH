# MHRise trajectory recorder

`MHGZ_AerialTrajectoryRecorder.lua` is a read-only REFramework utility for
recording Monster Hunter Rise hunter movement. Press `F9` to begin and press it
again to stop. A red `[REC]` label stays in the top-left while recording; start
and stop each show a confirmation bar. Each capture writes samples and events
CSV files under `reframework/data/MHGZ_AerialTrajectoryRecorder`.

From 1.10.0, keyboard polling uses Monster Hunter Rise's `via.hid.Keyboard`
device on REFramework's `UpdateHID` callback first, then falls back to REFramework
Windows virtual-key polling if the game HID device is unavailable. The floating
`MHGZ Recorder Control` window exposes `BIND CAPTURE KEY`; while binding, release
all keys and press the desired key. The window displays the selected input
backend, whether the capture key is down, and the last key detected during
binding. `F8` remains reserved for the motion probe; the default capture key is
`F9`.

From 1.11.0, each sample also records read-only controller input from
`via.hid.GamePad:get_LastInputDevice()`: raw `AxisL`/`AxisR` X/Y/Z values and
every button exposed by `via.hid.GamePadButton`. The CSV columns are named
`input_gamepad_axis_*` and `input_gamepad_button_<enum-name>_{down,pressed,released}`.
`pressed` and `released` mark transitions between HID polls; `down` is the held
state. Edge flags are retained until the next trajectory row is written, so
multiple HID polls between rows do not erase a press/release transition.
`input_gamepad_available` distinguishes no active gamepad device (`0`,
axis/button cells blank) from a connected device. The event CSV records the
button enum schema and changes in reader availability. The floating control
window shows live stick values and currently held button names, so verify these
before relying on a capture. Button names are RE Engine enum names, not a
promise of a particular controller's printed glyphs; trigger values are
recorded as the engine's digital button states, not analog pressure.

The controller source follows REFramework's FreeCam API: `via.hid.GamePad`
`get_LastInputDevice`, `via.hid.GamePadDevice` `AxisL`/`AxisR`, and
`isDown(via.hid.GamePadButton)`. Controller input is only sampled and written;
the recorder never consumes or changes it. Axis reads are best-effort across
TDB variants, so a live UI status of `Connected: controller axes unavailable`
means that build needs a runtime accessor adjustment before using the stick data.

`F8` is an optional diagnostic motion probe, not part of the normal capture
workflow and not required for M6.2. If you deliberately use it, press it at idle
and again in a phase to write read-only reflected motion/animation/frame/time/
rate/blend/rotation candidates. The current M6.2 implementation uses the F9
world-transform, velocity, camera, facing, and controller samples; do not repeat
F8 captures just to continue this task.

## Facing

From 1.6.0 each sample also carries the hunter's facing: `rot_qx/qy/qz/qw` (the
raw quaternion), `forward_x/y/z` (local `+Z` rotated by it) and `facing_yaw_deg`
(its angle in the XZ plane). Position and facing come from the same
`get_Transform` read, so they cannot desync.

Record the raw quaternion rather than trusting the derived columns: the axis
convention can then be corrected offline instead of by re-recording. A lateral
displacement and a heading change are indistinguishable from position alone,
and they belong in different channels — trajectory offset versus root rotation.

`get_Rotation` is a native accessor, so its name cannot be enumerated from the
managed method table (`get_methods()` does not list native methods). The script
therefore probes `get_Rotation` → `get_WorldRotation` → `get_LocalRotation` once
per capture and logs a `rotation_accessor_resolved` event naming the one that
worked. A failure leaves the facing columns empty and does not interrupt the
capture; it logs a `facing_unavailable` event instead.

## Camera

From 1.7.0 each sample also carries the **primary camera's** world Transform, in
twelve `cam_*` columns: position, the raw quaternion, `forward_*`, `yaw_deg` and
`pitch_deg`.

It goes through `sdk.get_primary_camera()` → `get_GameObject()` →
`get_Transform()` — the same two hops the player path uses. That entry point is
REFramework's own resolver for the current camera (internally `via.SceneManager`
→ `get_MainView` → `get_PrimaryCamera`), so there is no reason to re-walk that
chain from Lua.

**Why it is recorded:** the hunter's facing cannot answer *whose* back "back" is.
A move whose displacement sits 180° from the hunter's facing could be the stick
being pulled back relative to the hunter or relative to the camera, and the two
are indistinguishable from facing alone. Only the camera yaw at trigger time
separates them.

**Failure handling** differs from facing in one respect: the camera object is
re-fetched every sample rather than cached, because a capture spans scene loads
and holding a managed camera reference across one risks a dangling pointer. The
lookup is three TDB calls — negligible beside the per-sample motion-layer reads.
A failure leaves the `cam_*` columns empty and writes a `camera_unavailable`
event once per distinct reason; `camera_accessor_resolved` names the camera's
type, which is how you tell a player camera from a demo/photo-mode one.

**The camera looks down its own local `-Z`** — unlike the hunter, whose forward
*is* local `+Z`. This was measured, not assumed: rotating local `-Z` against the
geometric truth (the camera→hunter vector, the camera sitting ~7 m behind and
above the player) gives a median error of **0.76°** with **99.8%** of samples
inside 5°, while local `+Z` is **179°** off. 1.7.1 negates accordingly.

**⚠ A 1.7.0 capture has these three columns reversed.** `via.Camera` resolved on
the very first attempt in 1.7.0, but the derived `cam_forward_*` / `cam_yaw_deg`
/ `cam_pitch_deg` came out 180° out because 1.7.0 reused the hunter's `+Z`
convention. Only the derived columns are affected — `cam_qx..cam_qw` is the raw
quaternion and is correct either way, so a 1.7.0 capture is fixed offline
(negate the quaternion's local `+Z`) rather than by re-recording.

**Detect it geometrically, not by remembering which capture it was:**
`dot(cam_forward, camera→hunter)` should be ≈ **+1** — the camera sits behind and
above the hunter looking at them. All but one capture measures +0.96 to +0.98;
`20260919_005435_01` measures **−0.968** and is the only inverted one. Any
analysis script can therefore self-check per capture
(`build_kinsect_slash_curves.py: camera_sign()`), which keeps working across
recorder versions instead of encoding a filename. Full conventions live in
`docs/reference/README.md`.

The camera columns exist from 1.7.0 (facing only from 1.6.0); earlier captures
have neither.

The source script belongs in this repository; the installed copy belongs in
the game's `reframework/autorun` directory. It records the master hunter's
actual world Transform every rendered game frame. Timing uses
`via.Application.get_UpTimeSecond`; it neither changes game state nor
reads/writes the save file. Monster Hunter Rise's world-up axis is `Y`, so
`vertical_phase` is derived from `velocity_y`.

The four offline `build_*.py` scripts locate the recorder data through Steam's
installed game directory and `libraryfolders.vdf`. If the game is elsewhere,
set `MHGZ_RISE_RECORDER_DIR` to the absolute `reframework/data/MHGZ_AerialTrajectoryRecorder`
directory before running them. On a newly installed device the data directory
does not exist until the first F9 capture; the scripts do not copy captures
from another machine or regenerate the historical reference documents without
their original input data.

## M6 action-ID audit

Run `python Scripts/MHRise/audit_action_motion_captures.py` from the repository
root to scan the installed recorder's `*_samples.csv` files for contiguous
`player_motion_old_id` episodes of 193, 195, 196, 199 and 206. The report
includes phase duration through the next ID, displacement, mean velocity/speed,
camera/facing alignment, stick input, and a per-axis linear velocity fit. Fits
are emitted both with and without the first transition sample: REFramework can
report zero velocity on that boundary sample even though movement begins on the
next sample. The report also shows whether `motion_l*_frame` fields were
populated. To audit a copied capture directory, pass `--directory <path>`; to
save the machine-readable report, pass `--json-out <path>`. Without that option
the tool prints to stdout and writes nothing. This is an offline read-only
audit: action IDs and elapsed time do not reveal source-animation frame numbers;
missing frame fields remain explicitly missing, never inferred.

For the 2026-10-06 M6 capture, F9 provided enough data to audit the action
phases and world motion; F8 is not a required capture step. Do not treat the
movement-state ID duration as an animation clip's source-frame range.

## M6.3 Powder Vortex source-action discovery

The recorder already writes `player_motion_old_id` and every readable
controller button's `down`/`pressed`/`released` state on each F9 sample. No Lua
change or F8 motion probe is needed to find the Powder Vortex action ID. To
isolate it, record a short capture with one deliberate Powder Vortex execution
and no other attacks, then run:

```text
python Scripts/MHRise/discover_action_ids_by_input.py --directory "F:\games\Steam\steamapps\common\MonsterHunterRise\reframework\data\MHGZ_AerialTrajectoryRecorder"
```

The command above uses this machine's known game install path and inspects its
newest `*_samples.csv`. On another device, pass `--directory <recorder-data-path>`
or set `MHGZ_RISE_RECORDER_DIR` first. For a specific capture, pass
`--capture <absolute-csv-path>`;
to save a machine-readable copy, add `--json-out Saved/M6Audit/powder_vortex_input_id.json`.
The report aligns button edges, held-button rows, contiguous `player_motion_old_id`
episodes, transition IDs, and observed durations. Button labels are the RE Engine
enum names shown by the recorder, so use the captured press/hold/release sequence
to identify the controller chord rather than guessing enum names.

Use the isolated action's ID and its neighboring IDs to search the local Rise
motion/action index and extracted animation assets. The report identifies game
motion-state IDs only; it does not infer UE source frames or prove that an ID is
itself a single animation clip. If the recorder says controller input is
unavailable, or no pressed/released edges appear, do not interpret the IDs as a
button-to-action mapping; fix/verify the input capture first.

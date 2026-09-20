# MHRise trajectory recorder

`MHGZ_AerialTrajectoryRecorder.lua` is a read-only REFramework utility for
recording Monster Hunter Rise hunter movement. Press `F9` to begin and press it
again to stop. A red `[REC]` label stays in the top-left while recording; start
and stop each show a confirmation bar. Each capture writes samples and events
CSV files under `reframework/data/MHGZ_AerialTrajectoryRecorder`.

Press `F8` once at idle and again in each animation phase to write read-only
motion-probe snapshots in the same directory. Each report lists the reflected
motion/animation/frame/time/rate/blend/rotation candidates reachable from the
master hunter and its GameObject components. It does not call those candidate
methods or write to game state.

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

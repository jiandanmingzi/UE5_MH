# MHRise trajectory recorder

`MHGZ_AerialTrajectoryRecorder.lua` is a read-only REFramework utility for
recording Monster Hunter Rise hunter movement. Press `F9` to begin and press it
again to stop. A red `[REC]` label stays in the top-left while recording; start
and stop each show a confirmation bar. Each capture writes samples and events
CSV files under `reframework/data/MHGZ_AerialTrajectoryRecorder`.

## Legacy host compatibility repair (1.21.0, 2026-10-19)

Deployed after normal exit, with1.20 backups/hashes under
`Saved/Maintenance/RecorderDeploy/20261019-Combat121/`. PID30288 loaded1.21 with
engine_context_stack and the new native queue; load snapshot has no drops/errors.
Capture `mhrise_20261019_145235_02` passed all three targeted repairs:28 complete
damage/pre/post groups,66 selected flow fields, no errors/coverage warnings or
queue loss, valid F9 save. Evidence:
`Saved/Audio/RiseIG/NativeBridge/Combat121/Capture145235-Audit/findings.md`.
Do not repeat startup/short recording.27/28 hit sounds have a unique complete
join; one reused emitter stays unknown. Final Media is still not captured.

- Prefer official hook storage; otherwise use sdk.get_thread_context with exact
  pre-argument token validation and independent bounded stacks (64 contexts,
  depth32). Recursion/interleaving, uncaptured nested calls, drained pre rows and
  F9 epochs are protected. POST_ERROR exposes absent/broken capabilities. Engine
  context tokens never masquerade as Windows thread IDs.
- Selected scalar capacity96 includes prioritized StaminaDamage/StunDamage.
  Container-only part metadata stays explicitly `container_only_unexpanded`, not
  a read failure or a claim of complete part traversal. Other bounds unchanged.
- Only the native object queue changed to a bounded MPSC sequence ring, removing
  consumer-lock contention. Full/retry losses remain distinct and explicit; gaps
  still invalidate complete joins. Native Post/condition queues and CSV columns
  are unchanged.
- 89 audio Lua checks,17 animation/stamina,10 combat-audit checks and3 native
  CTests pass. Original recorder blocks outside combat/version are unchanged.
  Existing test dependencies: set PYTHONPATH to
  `Saved/Maintenance/RiseAnimationReader20261015/python_deps` for lupa2.8.

Rollback only while the game is closed: restore the backed-up1.20 Lua/DLL.

## Combat completeness repair (1.20.0, historical evidence)

Latest short capture `mhrise_20261019_134722_03` partially works, NOT accepted:
42 raw native hit edges match42 damage callbacks, but post snapshots0 and21
recording-time object gap markers. Current host lacks the assumed hook-storage
capability; missing-API handling and the lossy object queue require repair before
any new recording. Evidence: `Saved/Audio/RiseIG/NativeBridge/Combat120/Capture134722-Audit/findings.md`.
The load-only checkpoint below is historical, not a capture pass.

Built/tested and deployed after normal game exit; runtime1.20 loaded in PID33060.
Only the original recorder Lua and bridge DLL were replaced, with1.19 backups
and hashes in `Saved/Maintenance/RecorderDeploy/20261019-Combat120/manifest.json`.
81 audio Lua checks,17 animation/stamina checks,3 native CTests,10 combat-audit
checks and19 existing mapping checks pass. Original F9/sampling/animation/inputs/
stamina and native Post/condition formats remain unchanged.

- Field profiles prioritize required descriptors instead of declaration order;
  `_EffectMaterialType` is no longer lost at the end of the sound type. DamageRS
  and bounded snow.hit/snow.enemy nested data are sampled. Remaining bounds or
  absent required data are explicit in `field_coverage` and live coverage warnings.
  Profiles allow64 selected scalars, up to8 metadata parent types,4 nested
  references/two levels; not an unlimited world-object walk. Descriptor-only
  `combat_field_schema.txt` records the actual profiles without F8.
- `*_hits.csv` retains its original29 columns and appends DamageRS type/fields,
  coverage, post sound values and thread ID (34 total). The additional
  `hit_sound_context_post` row reads after hitTrigger updates material/strength.
  Official per-call hook storage handles recursion, drained pre rows and F9 epoch
  changes; post rows do not count as independent damage callbacks.
- `*_audio_objects.csv` now has15 columns, keeping the first10 names. The
  `container_trigger_context` kind carries actual owner/emitter/trigger arguments
  and, only for a signature-guarded exact nested caller chain, HitInfo/thread
  context pointers. Trigger ID is NOT EventID. Raw pooled HitInfo pointers are
  not unique transaction IDs; reused/ambiguous identities stay unresolved.
- `rise_audio_capture_map.py --audit-combat` separates same-emitter requests from
  declared bank defaults and unknowns; it never borrows another object's Switch.
  The prior capture has a verified root-container default for group3805839008,
  value1804647490, but that is NOT a recorded setter or final-Media selection.

Rollback only after exiting Rise: restore the saved1.19 Lua/DLL. Never unload a
pinned bridge live. New fields, callchain contexts and save health need a small
runtime check; no full-moveset re-recording yet. The load snapshot in
`Saved/Audio/RiseIG/NativeBridge/Combat120/Runtime33060/` shows all observers ready,
1250 trigger calls and no entry errors, but no actual hits yet. Three object-queue
drops occurred before F9 (Post/condition drops0); this is not a zero-loss claim.
Capture start establishes a new drop baseline and an initial unknown row. Any
new recording-time loss emits a gap and makes the offline join fail closed.

## Independent hit/object-key capture (1.19.0, historical evidence)

Short-capture update: `mhrise_20261019_123713_02` verified44 independent damage
callbacks (41 hunter/3 kinsect) plus44 separately-labelled sound contexts,1130
Post/key exact-ID matches,17675 condition rows and2153 object rows. Seven CSVs
have no ragged rows and stop normally; zero read exceptions/drops. Evidence and
hashes: `Saved/Audio/RiseIG/NativeBridge/Combat119/Capture123713/audit.json`.
Basic capture is accepted, NOT complete attribution: generic32-field/four-parent
caps truncate required raw data, part-info has no captured scalars, temporary hit
emitters lack a proved hit parent, and one hit Switch is only observed on the
target's different audio object. No full-moveset recording or repeated short
recording is needed before these gaps are addressed. Startup notes below are historical.

1.19 is built/tested and installed with verified hashes. Process1512 verified
loading:661 Post requests,1987 numeric conditions,1410 object-key/lifecycle calls,
zero drops/gaps/errors. Both managed listeners are installed, but actual hit
callbacks/fields and a short empty-swing/hit recording remain unverified; do not
record the full moveset yet. Evidence: `Saved/Audio/RiseIG/NativeBridge/Combat119/Runtime1512/`.
76 audio Lua checks,17 animation/stamina checks and3 native CTests pass.
Backup/deployment: `Saved/Maintenance/RecorderDeploy/20261019-Combat119/manifest.json`.

- F9 additionally writes `*_hits.csv`: the exact damage-side callback and the
  separate Wwise `hitTrigger` sound-context callback. These are different kinds;
  the second is NOT another independent hit. Attack/target GameObject addresses
  and names, hit position, raw scalar fields of attack/damage shape, part-info,
  damage-flow and sound material context are copied while parameters are alive.
  Unknown fields stay unknown; raw names are not guessed to mean segment/part or
  final resolved damage. Times are last-completed Lua sample estimates, not QPC.
- The Lua collector has512 preallocated row slots and32 cached type schemas,
  each limited to32 scalar fields/four parent types. No managed object survives
  in queued rows. No file IO occurs in these SDK callbacks; frame/stop drains.
  `combat_capture_live.txt` reports installed listeners vs actual callbacks/errors
  without F9. Exact method descriptors are required; unavailable/ambiguous/shared
  native entries fail closed. It never hooks generic property getters.
- Native `*_audio_objects.csv` observes the verified engine
  `(GameObject*, UInt32 sub-ID) -> UInt64 key` return, plus audio-object
  register/unregister requests. Pointer and numeric-ID namespaces are separate
  full-width decimal columns. These raw keys can be joined to Post by exact ID;
  owner labels require independently matching live object identities. No nearest
  time/distance guess, no null-stub resolver, no claim of final Media selection.
  The initial row explicitly has no pre-F9 baseline; drops write gap markers.
- Native entry copies only scalars, no object dereference/Lua/file IO. Its8192
  queue and optional profile-guarded hook are isolated from existing Post/condition
  files. Existing F9/trajectory/input/stamina/animation CSV structures are unchanged.

Rollback: exit the game, restore the saved1.18.1 Lua and1.18 DLL to their original
autorun/plugins paths. Never unload/replace the pinned DLL while Rise is running.

## Numeric audio conditions (1.18.0, historical evidence 2026-10-18)

Lua and NativeBridge1.18 are built, tested, deployed and observed in process2960:
5965 numeric requests,53 cached keys (13 State/19 Switch/21 RTPC),zero drops/gaps,
without F9. Evidence: `Saved/Audio/RiseIG/NativeBridge/Conditions118/Runtime2960/`.
F9 behavior and all original sample/animation CSV columns
remain unchanged. Native condition requests now get a separate
`*_audio_context.csv`; before/after F9, `audio_native_context_live.txt` reports
actual observed IDs/targets, current process PID, counts, losses and gaps, with
at most64 preview rows. No recording is needed to verify basic hook activity.

The new file distinguishes global State, object Switch and object/global RTPC
requests. RTPC target float bits, duration, curve and bypass are copied without
changing any arguments or original return values. The initial row explicitly
marks pre-hook conditions unknown; baseline rows are previously observed
requests, not assumed zero/default values or confirmed final sound selections.
Object registration/unregistration, parameter reset, queue loss/QPC barriers and
bounded-history overflow invalidate stale observations. Callback work remains
allocation-free scalar copying; files/caches are handled outside hooks.

3 native CTests,63 audio Lua tests and17 animation/stamina tests pass; full script
compiles in Lua5.4/LuaJIT. Backup/deployment manifest:
`Saved/Maintenance/RecorderDeploy/20261018-AudioContext118/manifest.json`.
See [NativeBridge](../Audio/NativeBridge/README.md) for exact ABI/profile evidence
and rollback. At that milestone emitter ownership, independent damage transactions, interpolated
RTPC current values and final Media are **not** implemented. The supported build's
reflected GameObjectID resolver is a null stub; never use it as ownership proof.
The automatic descriptor preflight now also includes the two parameter types
actually found in the damage-side callback (15 explicit types total).
Lua1.18.1 additionally fixes descriptor offsets to the official
`REField.get_offset_from_base` API and reports offset errors. That metadata-only
fix was verified loaded in process36304 on2026-10-19 (correct offsets including
HitInfo AttackObject40/DamageObject48/Position64). This did not itself implement hits.
The1.18 F9 condition CSV has not yet been captured in the actual game.

## Audio context preflight (1.17.1, 2026-10-18)

Historical step: Lua **1.17.1** with the verified1.17 bridge.
This is the first prerequisite for the authorized condition/emitter/hit upgrade,
**not completed runtime condition or damage capture**. Original F9, trajectory,
input, stamina, animation and native-audio client blocks are text-identical to
1.17.0 except the version and the two new preflight/UI calls.

The first frame automatically exports `mhrise_audio_context_<timestamp>_boot.txt`;
no F8, F9 or attack is required. Thirteen explicit Wwise/condition/damage types
are inspected, with method addresses, declaring owners, exact parameter types,
and field offsets **from descriptors only**. No game method is invoked, no live
object field is read and no new hook is installed. Missing descriptors and
truncation are explicit; the report does not infer initial values or Media IDs.
Limits: 2048 methods, 256 fields and 16 parameters per type/method, no inherited
type walk. Large player/enemy types are filtered to hit/damage names; WwiseManager
uses an exact method-name allowlist. Export runs once even on failure.

60 audio-focused checks pass, including actual LuaJIT userdata metadata vectors
and whole-script Lua5.4/LuaJIT compilation. Backup/hash manifest:
`Saved/Maintenance/RecorderDeploy/20261018-AudioContext1171/manifest.json`.
Runtime report and numeric native entry verification remain pending: start Rise
and remain in an operable scene, without recording. Do not ask for another full
move-set recording until the new collector's fields have actual-value evidence.

## Audio interface discovery (1.15.0, 2026-10-17)

**Current 1.17.0:** the authorized native bridge has been built/tested/deployed;
game loading/IPC/native calls and the first F9 CSV are verified: 1508 rows,
73 EventIDs, no drops/errors; 26 IDs/352 calls match the limited M1 inventory.
Action/variant/hit-sound assignments remain incomplete. It preserves F9,
adds `*_audio_native.csv` and `[AUDIO NATIVE]` live acknowledgement, and leaves
the original trajectory/input/animation/stamina columns unchanged. See
[NativeBridge](../Audio/NativeBridge/README.md) for the pinned game profile,
tests, output semantics, installation and rollback. No repeated capture is
currently needed; next resolve action/variant and real-hit sources from this capture.
The earlier Lua observation files below are diagnostic only, not EventID truth.

**1.15.1 correction:** the first real boot report found all 12 types but exposed
a probe bug: REFramework returns `std::vector` descriptor lists as userdata,
and the table-only checks rejected methods/fields/parameter lists. The fix
copies only bounded container lengths/indices for both tables and userdata;
unsupported containers stay explicit errors, not empty success. This matches
the [actual REFramework bindings](https://github.com/praydog/REFramework/blob/master/src/mods/bindings/Sdk.cpp).
The 2026-10-18 `000440_boot` report verified the fix on the actual game:
12 types and 184 keyword candidates, with readable signatures. Enumeration
limits still apply; this is not an exhaustive method dump or playback evidence.
Evidence/backup: `Saved/Maintenance/RecorderDeploy/20261017-AudioProbe1151/`.

This section describes the **metadata-only prerequisite**, separate from the
1.16.0 observation listeners below.
The first frame after script load automatically writes
`mhrise_audio_probe_<timestamp>_boot.txt` into the same recorder data directory.
No F8, F9, attacks, active hunter or full recording is needed for this report.
Starting F9 capture also writes a same-basename `*_audio_probe.txt` companion
and an `audio_probe_metadata` event. Existing 194-column samples and animation
CSV schemas are unchanged.

The probe inspects descriptors for 13 known Wwise/related types (including
`RequestInfo` from 1.16.1), at most two
inheritance levels, 192 methods and 64 fields per type, and 16 parameters per
method. It records names, static/instance flags, return/parameter types and
names, field **types only**, plus explicit missing/truncated/incomplete markers.
Metadata is cached once per script session, not re-enumerated per sample.
`signature_complete` means descriptor information was available, **not** that
the method is safely hookable. No audio methods, field values, setters,
native arguments or audio hooks are accessed. Export errors are isolated from
trajectory capture, with a visible `[AUDIO PROBE ERROR]`; automatic failure is
not retried every frame, while F9 can retry writing the cached report.

In 1.16.0 the metadata report uses `probe_installs_audio_hooks=0` and points to
the separate listener report. Earlier reports' `audio_hooks=0` described the
then-current 1.15 probe. Do not interpret descriptors as playback evidence,
hook unknown overloads by keyword, or sweep methods every frame.

References: [RETypeDefinition metadata](https://cursey.github.io/reframework-book/api/types/RETypeDefinition.html)
and [REMethodDefinition metadata](https://cursey.github.io/reframework-book/api/types/REMethodDefinition.html).
No new external Lua module or global Python dependency is required.

## Audio listener repair (1.16.1, 2026-10-18)

Current deployment is 1.16.1. It removes the unfiltered `get_EventId()` hook;
old 1.16 getter rows are not reliable audio identities and must not be reused
as a move-to-sound mapping. This follows the official [getter-hook warning](https://cursey.github.io/reframework-book/api/general/best-practices.html#hooking-gotchas),
not a claim that the old values' exact origin has been proven.

Only the three previously inspected `WwiseManager.postEvent` overloads remain.
Method selection uses the actual descriptors from one bounded `get_methods()`
enumeration, **never a hand-built `get_method(prototype)` lookup**. The scan
accepts table or userdata vectors, up to512 methods and32 post candidates.
Exactly one descriptor must match method/owner/static/return/parameters;
incomplete or ambiguous discovery fails closed. Failure reports distinguish
missing type/list, enumeration limit, unreadable descriptor, owner/static/
return/parameter mismatch, ambiguity, unavailable address and hook failure.
Function addresses are de-duplicated and reported; no audio arguments or
objects are read, no audio methods are actively called, and the original call
and exact return value are always preserved.

After one reload/restart, **do not start F9 just to verify installation**:

- `mhrise_audio_hooks_<timestamp>_boot.txt` reports signature matches,
  selected descriptor indices, native addresses and install results.
- `*_live.txt` is written automatically on first callback or after2 seconds
  with no callbacks. If that initial snapshot was zero, first later activity
  updates it once. These are at most two live writes per script load; failures
  are not retried every frame. Per-listener callback/error counts are included.
- The metadata probe additionally describes `via.wwise.RequestInfo` fields/
  methods without reading live values, so later event-identity work has evidence
  without another metadata-only recording.

`schema=mhgz_audio_listeners_v2` explicitly declares
`unfiltered_event_getter=disabled; event_id_capture=not_implemented`.
F9 CSV columns remain compatible but only `kind=post_event_return` is emitted;
`event_id` stays empty, and `post_return_id` remains semantically unknown.
Installed hooks, observed calls, known EventIDs and final WEM identities are
four different milestones. This repair does not yet establish the latter two.

Validation:29 listener checks,14 metadata checks and17 existing animation/
stamina checks pass (60 total); full Lua5.4/LuaJIT compilation passes. Actual
sample-building and animation-reader code is byte-identical to1.16.0. No UE
changes or full-project regression runs. Disk deployment/hash verification and
the1.16.0 backup are recorded in
`Saved/Maintenance/RecorderDeploy/20261018-AudioObserve1161/`.
Runtime installation/activity of the repaired listeners is still unverified;
read the new automatic reports before requesting a short capture.

## Audio scalar observations (1.16.0, 2026-10-18)

Historical implementation below; superseded by the1.16.1 repair above.

Four exact signatures selected from the successful real report are attempted
once on startup: `WwiseManager.postEvent(GameObject, RequestInfo)` and its
one/two-extra-UInt32 overloads, plus `WwiseEventParam.get_EventId()`.
Each listener validates declaring type, static flag, return/parameter types and
native function availability; identical function addresses are not hooked twice.
Setup errors are recorded, not retried each frame. Native wrappers may install
without seeing actual engine calls: **installed is not observed**.

Hooks always call the original function and preserve its exact return value,
including recorder failures. No arguments, objects or raw memory are read;
no audio function/getter is actively invoked. Callbacks only copy scalar returns
into 2048 preallocated records; main-frame code writes/flushes the CSV. Queue
overflow and conversion/I/O failures are explicit. F9 still controls all capture;
audio failure does not block trajectories or animation/stamina recording.

- Startup: `mhrise_audio_hooks_<timestamp>_boot.txt` reports installation only.
- Each F9 session: `*_audio_hooks.txt` reports setup and cumulative observed
  callback count at capture start; `*_audio.csv` contains actual observations.
- `kind=post_event_return`: UInt32 is kept as **post_return_id with unknown
  semantics**, never relabelled EventID, PlayingID or successful playback.
- `kind=event_parameter_read`: `event_id` is the engine's naturally read value;
  it is not proof of a corresponding successful post or an audible sound.
- All-world, unattributed scope: ambient/UI/other entities may appear. No claim
  that every row belongs to the master hunter, and no guessed cross-row pairing.
- `capture_time_s`/`last_sample_index` identify the last completed trajectory
  sample; `frame_index` is the recorder frame counter. `clock_elapsed_s` is a
  separate Lua `os.clock` delta, not a sample-accurate audio playback timestamp.
- `audio_capture_health` in `*_events.csv` records rows, dropped entries,
  conversion errors and file error at stop. Zero rows is diagnostic failure/
  insufficient coverage, not "the game played no sound".

Thirty audio checks and 17 existing animation/stamina checks pass, including
whole-script Lua5.4/LuaJIT compilation. Source and installed 1.16.0 hashes match;
backup/evidence: `Saved/Maintenance/RecorderDeploy/20261018-AudioObserve116/`.
**Reload scripts or restart once** to use 1.16.0, then record only a short empty
swing and real hit with F9. Runtime hook activity, exact event-to-post linkage,
Switch/State/RTPC conditions and WEM variant selection remain unverified. This
does not enter UE playback implementation or require a full move-set recording.

### First 1.16 runtime recording: not accepted

`mhrise_20261018_003753_01` saved normally (157.0185s / 18825 valid samples),
but only 1/4 listeners installed: all three `postEvent` lookups/validations
failed, while `get_EventId()` produced 19434 readings with values only 20–30.
There are no post-return rows and no match against the 152 M1 event identities.
Zero queue/conversion/I/O errors does not make these values valid audio IDs.
The combined failure marker does not establish which validation failed.
Unfiltered getter hooks also have a documented [shared-implementation
risk](https://cursey.github.io/reframework-book/api/general/best-practices.html#hooking-gotchas);
this recording lacks object evidence to prove or rule it out. Resolve method
identity and observation validity before requesting another recording.

## Animation-frame repair (1.13.0, 2026-10-15)

### Hunter stamina capture (1.14.0, 2026-10-17)

F9 now appends five columns to the trajectory CSV: `stamina_available`,
`stamina_raw`, `stamina_max_raw`, `stamina_source`, `stamina_error`. The first189
columns retain their positions; new files have194. The same-frame master hunter's
`get_PlayerData()` supplies read-only `_stamina` and `_staminaMax`; raw values are
not divided by30 or normalized to a guessed display cap. Missing/nonfinite/out-of-range
values stay unavailable, never fabricated as zero. A `REC STAMINA OK` or warning
toast and an event mark reader transitions. No player/game state is changed.

The installed autorun script matches source1.14.0; the previous1.13.0 is backed up
in `Saved/Maintenance/Stamina20261017/`. Seventeen focused checks passed, including
both Lua compilers and the actual CSV builder. Runtime stamina capture is verified:
`mhrise_20261017_160355_01` has23,624 valid stamina rows and no reader errors.
Original costs/recovery must not be guessed from prior position-only
captures; see [stamina reference](../../docs/reference/虫棍耐力参考与回填.md).

The old `motion_l*` columns read `_IG_InsectBehavior`, **not necessarily the
rendered attack clip**. In the 2026-10-14 ground-attack capture, all 5,968 rows
of state160 had only layer0 bank0/id1/end1; the other layers and node summaries
were empty. Previous source-entry frames fitted from root displacement are
estimates, not directly captured animation-frame truth.

F9 now writes a third companion file, `*_animation.csv`, with one row per
observed node/layer/sample. It resolves the hunter's `via.motion.Motion`
component (bounded to 32 hunter/descendant objects, 16 layers and 16 nodes per
layer), reads `get_Layer`, `get_HighestWeightMotionNode`, node bank/ID/name,
available node frame/end/speed/weight, and a separately labelled layer clock.
Motion-list resources are read through `findMotionBank` when available.
The read-only API references are [EMV Engine](https://github.com/alphazolam/EMV-Engine/blob/main/EMV%20Engine/reframework/autorun/EMV%20Engine/init.lua)
and [Enhanced Model Viewer](https://github.com/alphazolam/EMV-Engine/blob/main/Enhanced%20Model%20Viewer/reframework/autorun/Enhanced%20Model%20Viewer.lua);
EMV itself is **not** installed or required.

- `frame_source=node` means the node's getter returned the clock.
- `single_node_layer` is allowed only when a real count/array enumerates one
  index0 node; a layer clock is never assigned to two blend participants.
- `unavailable` stays blank. `node_enumeration=slot0_and_highest_only` explicitly
  means incomplete enumeration; unknown native indices are not probed blindly.
- `frame_units=engine_native_unscaled`: no multiplying elapsed seconds by60 or
  blindly applying the UE asset's2x RateScale. Resource-name/length calibration
  against imported clips is still required before calling a number a UE source frame.
- Compatible `changeMotion(bank,id,startFrame,...)` overloads are observed via
  read-only hooks on discovered layer types. `animation_change_requested` records
  the requested start frame and preceding sample index; it does not prove the
  request was accepted. `animation_first_observed` is only the first sampled
  frame, not the requested start. Missing overloads remain unverified.
- Animation availability is shown separately as `[ANIM READ]` or
  `[ANIM WARNING]`; normal trajectory `[REC OK]` does not prove attack-frame
  capture. Getter capabilities are emitted automatically in the events file
  once per type/capture; **F8 is not required**. A short F9 capture is sufficient
  for the first in-game API check; do not request the full attack matrix yet.

The original samples header and35-column legacy motion tail remain compatible.
Unknown controller bools now write an empty cell instead ofLua `nil`, fixing
row shrinkage; false captured layer flags are preserved asfalse. New rows check
their width before writing. Old recordings are not rewritten and missing input
or attack-node frames cannot be restored from them.

Local validation executes the actual Lua reader against read-only native-object
doubles, tests the actual trajectory row builder, and compiles the whole script
with Lua5.4 and LuaJIT2.1. These tests do **not** validate the installed game's
native getters or guarantee complete blend-node coverage. Run the focused tests
with `python -m unittest discover -s Scripts/MHRise/tests -p test_recorder_animation_reader.py -v`
in an environment containing `lupa`; absence explicitly skips rather than claiming
success. This maintenance task uses an isolated `Saved/Maintenance/RiseAnimationReader20261015/python_deps`
installation, not a system Python package change.

Deployment and game-runtime acceptance are separate. See
`docs/using/current-status.md`; the first new in-game file must establish readable
real clips/frames before re-recording the entry comparison.

1.13.0 was deployed to the local Rise autorun path on2026-10-15. The old1.12.0
copy, before/afterSHA-256 and test summary are saved under
`Saved/Maintenance/RecorderDeploy/20261015-AnimationReader113/`. Source/installed
hashes match;13 new local checks and4 existing action-audit checks passed.
Runtime verification now covers captures `mhrise_20261015_213914_01` and
`mhrise_20261015_214015_02`: actual `player00` clips have native node frames,
names and weights with count-based enumeration; the short capture includes
three simultaneous movement nodes with separate weights. All trajectory rows
have189 columns and all animation rows30; neither capture reports a reader error.
This verifies the observed nodes, not universal blend coverage for all moves.

The36-second capture contains11 executions of `plw_InsectGlaive_100_160`.
Their first sampled native frames range0.566–0.663 and their clocks advance at
about60 native frames/s. Against the imported160 root track, all11 best fits
use `UE source frame = 2 * native frame`, with zero additional frame offset
among the tested offsets. This calibrates this clip's frame units; it is not a
global conversion for every clip, nor proof of identical weapon/bone poses.
Both compatible hooks were installed but **no change-request events occurred**;
exact requested frame0 remains unobserved. First-sample evidence supports entry
near the beginning, not an exact captured request of0.000. Do not trim the UE
entry merely to hide its weapon pose mismatch and call that the original entry.
Audit and import-source evidence: `Saved/Maintenance/RiseAnimationReader20261015/`.

From 1.12.0, capture health checks the existing hunter/motion samples without
additional reflection scans. Three valid rows produce `[REC OK]`; no valid
row for three wall-clock seconds produces a persistent `[REC WARNING]` and a
`capture_health` event. Recovery is reported once. The warning does not stop
or restart capture; zero valid rows are explicitly warned about when saving.
Camera/controller support still uses the existing separate status columns.
Update the installed autorun Lua from this source when deploying a recorder
change; editing this repository does not update the game installation.

On 2026-10-13, version 1.12.0 was deployed to the local MonsterHunterRise
autorun directory and its SHA-256 was checked against this source. The previous
1.11.0 file and the deployment manifest are retained under
`Saved/Maintenance/RecorderDeploy/20261013-195135`. In-game capture health testing
was deferred at the user's request; deployment is not a gameplay sign-off.

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

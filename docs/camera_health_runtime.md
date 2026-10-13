# Experimental camera health alerts

Software implementation for #65–#69, October 13, 2026. The
[dated investigation](camera_health_alerts.md) remains the design and primary-source
ledger; [HERO12 polling](gopro_health_polling.md) documents exact wire premises and
response ambiguity. Health alerts remain **Experimental / Not tested** on hardware.
No camera was operated or physical compatibility promoted for this implementation.

## Commissioning and owner wiring

Set `QualifiedLocalTelemetry::runtime.health_enabled = true` in the private bench
provider to opt in. Existing board, storage session namespace, GPS and camera
qualification remain required. This flag does not manufacture peer identity or
state evidence. The bench provider binds the bounded serial sink automatically
when `runtime.health_sink` is null; a custom sink must outlive the session and
return promptly without reentry or camera IO.

For each commissioned HERO12 additionally set `Hero12Qualification.health_enabled`.
Remaining video seconds require `ordinary_video_confirmed`; leave this false if
external mode changes can violate the connection's ordinary-video premise. There
is no mode decoder in this version. Both the source qualification and a live
owner attachment are required before health radio work starts. Unsupported or
unavailable fields remain explicit, and a failed optional query does not become
successful recording evidence. All flags default off.

`CameraEventSession` owns the evaluator and its single eight-transition ring, attaches
its independent manager health observer, binds optional adapter samples, and
runs evaluation after the sole adapter pass. The same owner snapshot supplies
`LocalTelemetryStatus::camera_health`, handlebar LED selection and serial STATUS.
The health logger drains at most two transitions/pass independently of existing
IMU/camera admission quotas. GPS remains admitted through the same telemetry owner.
Health logging selects version **7**, retaining existing bookmark, camera, GPS
and motion row contracts; old versions 1–6 keep their existing interpretation.

## Recording and resource behavior

Only an admitted Recording intent arms an episode. ACK, write completion and
keep-alive never establish prior recording or renew its age. A qualified fresh
Recording followed by Stopped raises UnexpectedStop. Before a first recording
observation, finite command failure raises RecordingUnconfirmed instead. Missing
or expired evidence raises RecordingEvidenceLost with current state Unknown.
Stable status resync preserves the episode. Admitted Stop, cancellation, seal or
reset retires it; refused requests leave the existing episode alone.

X5 observations use retained qualified CE80 display receipt/sequence and expire
strictly after 5000 ms. DJI observations use documented status with receipt time
and a logical connection sequence, including 16-bit wire-sequence rollover;
they expire at 1500 ms. No new DJI resource alerts are enabled. GO 3S and ONE RS
lack authoritative recording observations and report MonitoringUnavailable.
Neither a remaining-runtime display nor a command ACK becomes a Stop observation.

Qualified HERO12 resources use the design thresholds: battery warning <=20%,
critical <=5%; ordinary-video remaining time warning <=300 s, critical <=60 s;
source overheating true is critical. Hysteresis requires fresh distinct samples
at least 5 s apart. Absent battery/unqualified mode does not become numeric zero.
Each field expires independently. Previously critical resource history becomes
current Warning/Partial when stale, with historical severity retained; fresh
qualified samples are needed for recovery. Reconnect requires a new qualified
poll floor and new observations. Shutdown retires all reasons before logger finish,
without turning retirement into a healthy recovery or a lost-contact alert.

LED priority is application/supervisor or camera Critical -> Error, existing
Recovery, then camera Warning -> Partial, then ordinary group display. STATUS
lists current recording validity and every peer's current/historical reasons and
resource fields, including unsupported/unavailable cases. There is no new button
mapping, acknowledgement gesture, haptic output, retry, wake or reconnect policy.

Serial transitions are emitted once when drained, with opaque peer, episode,
sequence, reason and severity. The sink checks available TX space before writing;
insufficient space drops the line and increments `serial_dropped`, visible on
STATUS. It does not wait for serial space. The compact line fits the default
ESP32 UART FIFO even at maximum valid field widths (126 bytes including newline).
Keys are `p` peer slot, `id` opaque peer identity, `m` model, `e` episode, `s`
transition sequence, `k` transition kind, `reason/severity`, `v` validity, `a`
receipt age in milliseconds, `d` transition drops and `c` incomplete wire-history
coalescing. Model/kind/severity/validity use the documented log enum values.
The active snapshot remains visible
when either serial or SD history drops.

`wire_coalesced` reports actual incomplete receive-edge history; benign repeated
unchanged-alert coalescing is counted separately. Wire optional failures are
visible as `wire_failures`. The versioned health log carries actual receive-edge
coalescing in `transition_coalesced` and declares `history_incomplete` when this,
transition loss or storage drop/rejection/loss is known. A final Retired record
also preserves an armed healthy episode's identity and known history loss.
Filesystem failure can prevent its own final evidence from being written; this
software does not promise lossless notification history or power-cut durability.

The [fixed-memory audit](camera_health_resources.md) measures 3504 bytes of
persistent health state plus a conservative 448-byte owner scratch reservation:
3952 bytes against the 4096-byte ESP32 cap. The eight-entry history ring avoids a
second inbox and does not limit current peer alert state. CI checks this budget;
physical task stack margin remains part of existing activation qualification.

## Verification and physical handoff

Focused native tests exercise the evaluator, direct/group intent hooks, source
receipts, codec, optional-query scheduling/correlation, serial/LED outputs and
actual session/logger final-publication ordering. Independent Python fixtures
exercise real version-7 serialization/parsing, malformed fields, combined existing
rows and unchanged old-version bytes; pressure/lifetime checks use sanitizers.
These are **Synthetic** results, not footage, notice-latency or thermal evidence.

The isolated October 13 delivery snapshot passed 185 Python tests and 679 native
cases, including the common V6/V7 producer and session contracts, Linux C++
portability, Python lint and C++ formatting. Pinned `hero12_adapter_compile` and
`bench_application` builds passed; the resource probe measured 3952 bytes and
passed the target 4096-byte guard. Focused evaluator/output checks also passed
ASan/UBSan. The delivered foundation was independently checked in an isolated
source snapshot (55 native cases and the pinned HERO12 build).

The delivery retains the common V6/V7 data and owner APIs so independent callers
can preserve bookmarks alongside health records. Physical bookmark input, the
serial MARK frontend, native HERO12 HiLight wire dispatch and offline bookmark
export remain separate integrations.

Existing #46/#22 rows retain camera/display/manual-Stop, resource comparison,
receipt/alert latency, playback, missing-peer/radio coexistence and per-model
qualification. #31 owns enabled alert/log load, stale/recovery and reset/no-replay
observations in its already declared campaign. Unavailable equipment and unsafe
thermal cases remain Blocked/Not run. No additional soak or physical-test ticket
is created.

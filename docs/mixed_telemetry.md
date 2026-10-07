# Bounded mixed telemetry, version 2 (#34)

This software contract is explicitly selected with the final `StorageConfig`
argument `StorageFormat::MixedV2`. The default remains `GpsV1`, preserving its
API, path and bytes. Mixed files exclusively create
`/telemetry-<16 lowercase hex session ID>.csv`, start with
`#ridesync_telemetry,2`, retain session/firmware/provenance and storage policy
metadata, the GPS field header, and `#imu_layout,2,see_docs/mixed_telemetry.md`.
The GPS field header describes the suffix of `gps` rows, not the other kinds.
No native memory images are persisted. Unknown versions/kinds must fail closed;
no guessing another layout. Only complete LF-terminated rows are valid. A
partial trailing row is discarded/reported as partial; it cannot be repaired by
replaying/appending bytes. The independent test parser conservatively rejects
an entire supplied stream with a partial trailing row.

`TelemetryRecord` is a fixed tagged copied value: `kind`, `RecordTimestamp`,
`ModemSnapshot`, `ImuEvidence`. Kind values are GPS=0, IMU sample=1,
configuration=2, health=3, control=4; Count=5 is invalid. Unused payloads are not
serialized. Raw paired IMU XYZ counts are signed int16 values from **one complete
sensor-frame pair**, unchanged by this pipeline. No remap, cross-axis host
compensation, gravity removal, lean, calibration application or sensor I/O is
implemented. A future acquisition adapter must preserve pre-transform bytes;
converted driver floats are not raw evidence. #12 owns acquisition and #27 owns
physical selection/mount qualification.

## Exact row schema

ASCII CSV: commas, decimal integer values, LF; no quoting or user strings in
payloads. Firmware/provenance remain the bounded sanitized GPS v1 tokens.
`gps` rows have **36 columns**: literal `gps` plus all 35 GPS v1 columns in
[log_format.md](log_format.md). Their counters remain aggregate Storage health.
The other kinds (`imu`, `config`, `health`, `control`) have **88 columns** in
this exact order:

```text
kind,session_id,monotonic_ms,monotonic_quality,anchor_quality,anchor_sequence,anchor_receipt_ms,anchor_utc_ms,uncertainty_known,uncertainty_ms,anchor_age_ms,has_utc_estimate,utc_estimate_ms,batch_sequence,frame_sequence,byte_position,sensor_epoch,receipt_known,receipt_millis32,drain_known,drain_start_millis32,drain_end_millis32,timing_flags,acquisition_known,acquisition_ms,generation,sensor_id,mount_id,calibration_id,sensor_state,mount_state,calibration_state,accel_range_mg,gyro_range_mdps,accel_scale_numerator,accel_scale_denominator,gyro_scale_numerator,gyro_scale_denominator,accel_odr_millihz,gyro_odr_millihz,accel_filter,gyro_filter,accel_offset_compensation,gyro_offset_compensation,calibration_method,calibration_time_known,calibration_utc_ms,calibration_temperature_known,calibration_temperature_millic,calibration_offsets_known,accel_offset_x,accel_offset_y,accel_offset_z,gyro_offset_x,gyro_offset_y,gyro_offset_z,calibration_gains_known,accel_gain_x_numerator,accel_gain_x_denominator,accel_gain_y_numerator,accel_gain_y_denominator,accel_gain_z_numerator,accel_gain_z_denominator,gyro_gain_x_numerator,gyro_gain_x_denominator,gyro_gain_y_numerator,gyro_gain_y_denominator,gyro_gain_z_numerator,gyro_gain_z_denominator,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,event_code,event_length,event_bytes,sensor_time_present,sensor_time_ticks24,event_count,event_count_lower_bound,accepted,dropped,rejected,lost,written,flushed
```

The first 12 timestamp fields after kind have exactly GPS v1 semantics and
validation. They are **admission** timestamps, retaining their copied UTC anchor
through later corrections/resets. They do not date acquisition. Every IMU row
carries its full configuration: losing a configuration-event row cannot orphan
sample interpretation. The logger preserves order among admitted records;
inbox ordering preserves transport publication order, including events within a
batch. GPS/control owner admissions may interleave between two tick calls.
Different metadata with the same identity/generation is a caller contract error;
the pipeline retains it visibly and does not invent a configuration history.

| Fields | Contract |
| --- | --- |
| batch/frame sequence, byte position, sensor epoch | uint32 source-local opaque association. Zero is unspecified for sequences/epoch; byte position zero is valid. Caller must retire/reidentify before reuse/exhaustion; this layer does not infer rollover/reset continuity. |
| receipt/drain presence and millis32 | Explicit observed host **raw Clock millis32** domain, modulo 2^32, shared domain with the clock's raw source but not session elapsed milliseconds. Absent values blank; present zero valid. Never compare directly to admission/session time or UTC. Continuity/age cannot be inferred from a wrap alone. |
| timing_flags | bits 0 discontinuity, 1 stale receipt, 2 wrap/reset ambiguity, 3 software endpoint indication; transport-supplied observations, zero does not assert precise timing or absence of clipping. No later register flags are assigned to historical frames. |
| acquisition_known / acquisition_ms | Always `0` / blank. No observed per-frame acquisition timestamp exists in this contract; no nominal-period backdating. |
| generation and sensor/mount/calibration IDs | uint32, zero unknown. Opaque caller-assigned identities; no uniqueness, SKU, mounting rotation or calibration invented. No supplied profile alone proves hardware support. |
| sensor/mount/calibration state | 0 unknown, 1 unqualified, 2 qualified. Qualified requires a nonzero respective ID; physical evidence remains caller responsibility. Defaults unknown. |
| accel_range_mg / gyro_range_mdps | Effective sensor ranges in milli-g / milli-degrees per second; zero unknown. |
| scale numerator/denominator | Exact positive uint32 rational **g/count** and **dps/count**. Both zero means unknown; only one zero is invalid. No unit conversion performed. The synthetic documentary profile uses 1/2048 g/count and 125/2048 dps/count, without qualifying a device. |
| ODR millihz / filter | Effective accel/gyro ODR in milli-Hz (zero unknown), opaque effective filter-setting IDs (zero unknown). Acquisition must document its readback interpretation. |
| offset compensation | Each channel group: 0 unknown, 1 disabled, 2 enabled device compensation. Sensor-frame counts still include any device-side filtering/compensation. Host remapping/compensation is excluded. |
| calibration method/time/temperature | Opaque method ID (zero unknown); signed POSIX ms and milli-Celsius with explicit presence flags. Absent values blank; no elapsed-time or physical plausibility inference. |
| offsets / gains | Optional signed int16 calibration offsets in sensor counts and per-axis positive rational dimensionless gains, retained **without application**. Each group's presence flag controls all its fields; unknown fields blank. Known gains require all nonzero denominators/numerators. Known zero offsets remain valid. |
| accel/gyro XYZ | Signed raw sensor-frame counts on `imu` rows; blank on config/health/control rows. Complete pairs only, not ordinal merging of separate extractor arrays. |
| event code | 0 unknown, 1 unsupported format, 2 transport error, 3 partial frame, 4 skipped, 5 FIFO read-boundary sensor time, 6 input configuration, 7 full indication, 8 reset, 9 flush. No camera protocol. |
| event length/bytes | 0..4 bytes, lowercase hex in source order (two chars/byte), blank for length zero. Transport carries event/byte position and discontinuity rather than guessing recovery. |
| sensor time present/ticks24 | Explicit boolean and raw 24-bit ticks; present zero valid. Present requires control kind, code 5, three bytes matching little-endian value. These are **FIFO read boundary** events, never sample acquisition timestamps. Tick unit/semantics are source-profile evidence, not a timestamp conversion. |
| event count/lower bound | uint32 source evidence with boolean lower-bound flag; skipped count 255 can mean at least 255, FIFO full alone supplies no exact loss count. Zero does not prove no physical loss. |
| last six counters | Storage health for this row's kind, sampled before writing the row. Same meanings as GPS v1; snapshots, not a transactional footer or persistence acknowledgment. |

No sensor-specific transport header parser is claimed here. #12 must publish
supported raw pairs/control evidence and report unsupported/partial/unpaired
frames without manufacturing data. Trailing BMI270 ticks remain a read-boundary
event; unknown timing survives serialization.

## Ownership, admission and stop

Exactly one transport execution context owns `ImuInbox::publish/finish`; exactly
one admission context consumes it, mutates `SessionClock` (including GPS tick,
anchor/reset), calls `TelemetryAdmission` and is the **sole Storage producer**.
The storage worker alone performs sink I/O. A transport/ISR must never access the
SessionClock or call Storage enqueue. Copied fixed batches have 1..4 evidence
records; inbox capacity four batches = at most 16 records. Publication is atomic
at batch granularity; overflow drops the whole batch and saturating per-kind
inbox dropped counters count its records. `rejected()` counts malformed batches
(invalid count/tag), whose record-kind association may be unknowable. Storage's
per-kind rejected counters account malformed/session-mismatched evidence after
consumption. Inbox drops and Storage drops are separate stages: do not add
published/accepted counts as though they described disjoint samples.

`tick()` consumes at most two records, including rejected/dropped attempts.
Each receives an owner snapshot; no transport UTC/clock mutation. Inbox slots
stay immutable until the batch's last record is consumed. IMU tick admissions
reserve two of eight Storage slots (at most six queued IMU attempts accepted).
Direct raw sample admission also reserves two; owner config/health/control events
and GPS can use all eight. This permits GPS/control progress under IMU backlog;
it cannot prevent GPS/control's own overload or a permanently blocked card.
Caller chooses cadence and scheduling, including GPS/control before IMU service.
No hard sampling/service-period/200 Hz throughput guarantee follows. Tick and
admission contain no allocation, bus/filesystem calls, formatting, mutex, wait
or unbounded retry; uint32/boolean atomics are lock-free on supported builds.

Future qualified composition: construct `ArduinoSdStorage` with its existing
explicit qualified dedicated bus/card config and a MixedV2 `StorageConfig`;
construct `SessionClock`, `ImuInbox`, `TelemetryAdmission(clock, sd.storage(),
inbox)` with one unique caller-supplied session. The transport owner publishes
immutable evidence and handles stop requests. In the control owner use the same
`snapshot` for `modem.snapshot(timestamp)` and `admission.gps(timestamp, sample)`;
use `event` for owner configuration/health/control and `tick` for inbox service.
The convenience `gps(sample)` is only suitable when the sample's age/receipt
association matches its new owner snapshot (e.g. no age evidence). Do not call
Storage from another GPS/IMU context. Existing firmware keeps physical drivers
unqualified/disabled and does not instantiate this composition.

Shutdown order: admission owner `requestStop()` refuses/counts further direct
admissions and release-publishes inbox stop request. Transport observes it,
finishes its bounded acquisition outside admission, may publish one final batch,
then calls irreversible `finish()` **after** its last publication. Further
publish is refused/counts drops. Admission continues bounded ticks, acquires
finish, rechecks the queue, drains all published evidence (or accounts Storage
refusals), and only then calls `Storage::requestStop()`. `admission.stopped()`
means admission drained/quiescent, not filesystem closed. Keep every object,
clock, bus and sink alive until transport joined/quiescent, admission stopped,
Storage `health().stopped` and adapter `workerFinished()`. A transport that never
finishes leaves stop pending; supervise that failure rather than silently discard
unknown final evidence. Terminal media failure still needs worker close completion;
health supervision must not retire on `terminal` alone.

Storage per-kind accepted/dropped/rejected/written/flushed/lost counters saturate
at UINT32_MAX with bounded single-writer operations. Health is observational;
exact accounting ends at saturation. Terminal loss is accepted minus flushed,
including racing publications, cached rows and partial bytes, conservatively
uncertain. Aggregate counters preserve GPS v1 semantics; invalid tags have no
valid per-kind bucket but increment aggregate rejection. Drops/rejections before
Storage acceptance, including whole inbox drops, are additional losses. Final
runtime health remains authoritative because failed media cannot log its footer.

## Bounds, evidence and limits

Pinned ESP32 GCC 8.4.0 ABI size probe (not stack/scheduling measurement):
ImuEvidence **240 B**, TelemetryRecord **472 B**, ImuBatch **968 B**, inbox
**3912 B**, Storage **6160 B**, admission **16 B**, combined objects **10088 B**.
Native arm64 probe: same payload/inbox sizes, Storage **6168 B**, admission
**32 B**. Compile-time assertions guard payload/inbox growth. Eight copied
Storage records consume 3776 B; serialization buffer remains 2048 B with at
most 2047 emitted bytes plus NUL; chunks remain 256 B. An IMU row has 88 fields,
each at most 20 characters, so a conservative line bound is **1848 B** including
commas/LF. A mixed GPS row has 36 fields and conservative bound **756 B**.
Header metadata fits the same buffer; formatter bounds failures are terminal.
Caller transport needs a fixed 968 B publication batch; worker has a temporary
472 B record, and producer enqueue has a temporary record of that size. These
are object/temporary costs, not measured whole-task stack peaks. The SD adapter's
existing 8192 B task stack/SDK allocations remain separately qualified.

The 16 inbox frames represent only 80 ms at nominal 200 Hz; six shared queue
slots represent another 30 ms, before event/GPS overhead. Neither is a stall
absorption guarantee. Worker chunking bounds calls/bytes, not sink latency;
mount/write/fsync/close can block indefinitely. Existing exclusive creation,
no formatting/deletion/truncation/replay, terminal partial writes and flush
uncertainty remain unchanged. Successful fsync is software reported completion,
not measured physical power-fail durability. Flush record bounds remain queue
capacity + flush threshold (12 default / 16 maximum); no time bound in blocked
I/O. No target jitter, sample throughput, axis/calibration quality, scheduler
fairness, SD physical durability or road acceptance is established.

Tests in `test/test_telemetry`, `test/test_telemetry_disk.py`,
`test/telemetry_parser.py`, and `test/test_telemetry_stop.py` use author-created
synthetic public fixtures under repository MIT. The C++ fixture is independently
parsed from the documented schema, not shared formatting code. It includes
signed endpoints, absent calibration, known zero sensor ticks, host millis32
wrap ambiguity, copied old UTC and configuration/event ordering. No vendor code,
private captures, credentials, device identifiers or precise ride locations are
copied. The immutable pinned-driver audit stays documentary in #12/#27; these
software tests cannot close #11/#12/#31's original physical gates.

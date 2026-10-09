# Source-backed pure Insta360 GPS encoder (#29)

Date: 2026-10-09. Status: implemented; #29 software acceptance complete.
The user approved the proposed pure 71-byte encoder approach. This document
makes its implementation contract reviewable. Work stays directly on main.

## Intent and completion boundary

Implement the existing #29 software outcome: convert an evidenced, fresh GNSS
snapshot into one bounded packet independently of BLE transport. Provide a real
source-backed video encoding path, independent expected bytes and explicit errors.
Do not manufacture missing telemetry or silently extend a camera protocol.

This completes software encoding only after its tests/build/format/CI gates pass.
No camera capability becomes enabled or Confirmed. #14 owns forwarding; #22 owns
camera footage/metadata interpretation; #30/#31 own installation and physical
acceptance. No new issue, driver, camera connection, worker or runtime activation
is introduced. The immediate X5 recording milestone remains independent.

## Evidence and selected approach

Use `GarminBe80VideoV1` as an explicit experimental wire profile. Its reference
is arsfabula/Insta360-Remote-CIQ commit
`39c51b3aa7c453227831d811355899371bbb8b94`, `BLE Barrel/BLEBarrel.mc`,
`sendPosition`, `toDouble` and `sendCMD`. The pinned README reports ONE R 360-mod
testing; it does not qualify a fitted ONE RS module, X5 or GO 3S.

Primary source links:

- [Pinned packet-building source](https://github.com/arsfabula/Insta360-Remote-CIQ/blob/39c51b3aa7c453227831d811355899371bbb8b94/BLE%20Barrel/BLEBarrel.mc).
- [Pinned README](https://github.com/arsfabula/Insta360-Remote-CIQ/blob/39c51b3aa7c453227831d811355899371bbb8b94/README.md).
- [Pinned MPL-2.0 license](https://github.com/arsfabula/Insta360-Remote-CIQ/blob/39c51b3aa7c453227831d811355899371bbb8b94/LICENSE).
- [Garmin Moment.value](https://developer.garmin.com/connect-iq/api-docs/Toybox/Time/Moment.html#value-instance_function): UTC seconds since Unix epoch.
- [Garmin ByteArray.encodeNumber](https://developer.garmin.com/connect-iq/api-docs/Toybox/Lang/ByteArray.html#encodeNumber-instance_function): default little-endian representation.
- [Garmin Position.Info](https://developer.garmin.com/connect-iq/api-docs/Toybox/Position/Info.html): nullable input fields and units.

The chosen approach preserves the evidenced layout and ordinary binary32-to-
binary64 numerical path, with canonical IEEE conversion for zero/subnormals.
This corrects the reference helper's numerical defects as an explicit encoder
policy; it is not evidence that a camera accepts those edge cases. Missing fields
and negative altitude are rejected because an honest wire representation has not
been established. NaN/Inf are rejected. Unknown constants remain opaque.

Waiting for annotated camera captures is the alternative. Those captures could
justify broader optional/signed-altitude support later, but they are not required
for the finite source-backed software boundary of #29. CE82 RMC is a separate,
unqualified protocol and is not guessed or substituted here.

## API and ownership

New portable files: `include/insta360_gps_encoder.h` and
`src/insta360_gps_encoder.cpp`. Namespace `ridesync::insta360`.

Public types:

- `GpsWireProfile`: Disabled and GarminBe80VideoV1. Unsupported enum values fail.
- `GpsEncoderConfig`: profile defaults Disabled; `max_age_ms` defaults zero.
  An explicit enabled call requires age limit 1..60000 ms, a software policy.
- `GpsEncodingError`: None, Disabled, InvalidConfig, InvalidSequence,
  InvalidClock, InvalidFix, MissingField, InvalidCoordinate, InvalidUtc,
  InvalidMetric and UnsupportedAltitude.
- `GpsEncodingResult`: error plus `size`, and a fixed `std::array<uint8_t,71>`
  initialized to zero. Success has error None and size 71; every failure has
  size zero and all-zero bytes. No partially usable output escapes.

The stateless entry point is
`encodeGps(const GpsEncoderConfig&, const RecordTimestamp& now,
const ModemSnapshot&, uint8_t sequence)` returning `GpsEncodingResult` by value.
These existing inputs are copied observations, not drivers. The function does
not invoke ModemGnss, mutate SessionClock, read a clock, access BLE/SD, allocate,
block, retain pointers or maintain a sequence counter. No caller buffer can be
shorter than the fixed result. Compile-time packet size and independent tests
establish the 71-byte payload limit.

Caller supplies a sequence in 1..254. No automatic increment, reset or retry is
performed. A later transport owns the complete command stream and its delivery
ambiguity. #14 may split a successful packet into writes; this encoder emits no
20-byte queue/chunk and claims no ACK or delivery.

## Input validation and freshness

Validation is deterministic in this order, returning the first applicable error:

1. Disabled profile returns Disabled. Unknown profile or invalid age limit returns
   InvalidConfig. Sequence 0/255 returns InvalidSequence.
2. `now` must have a nonzero session, Valid monotonic quality and elapsed time no
   greater than SessionClock's allowed duration. Otherwise InvalidClock.
3. Snapshot session must match `now`; validity must be Valid, `fix.valid` true and
   age available. Receipt must not exceed `now.monotonic_ms`. Compute age from the
   copied receipt using checked subtraction, require equality with snapshot age,
   and require age <= configured limit. Failure returns InvalidFix. This rejects
   Missing, NoFix, Invalid, Stale, foreign sessions, future receipts and stale
   copies, including a supposedly Valid snapshot retained beyond expiry.
4. Coordinates must be finite and within latitude [-90,90], longitude [-180,180].
   Failure returns InvalidCoordinate.
5. UTC date/time, speed, course and altitude must all be available. Missing any
   required field returns MissingField. Satellites/fix quality are ignored;
   there is no evidenced packet field for them.
6. UTC uses the existing GNSS calendar policy: Gregorian years 2000..2099, real
   dates, hours 0..23, minutes/seconds 0..59 and centiseconds 0..99. Epoch seconds
   must fit uint32. Failure returns InvalidUtc. UTC comes from the fix's source
   fields, never receipt time, a SessionClock anchor or wall-clock synthesis.
   Centiseconds are deliberately omitted because only whole seconds are evidenced;
   filler bytes are not interpreted as fractional time.
7. Speed must be finite and nonnegative; course finite in [0,360); altitude finite.
   Invalid values return InvalidMetric. Finite altitude <0 returns
   UnsupportedAltitude; it is never encoded using its absolute value.
8. The finite scalars must remain representable after explicit binary32 narrowing.
   Overflow, nonzero underflow to zero, or narrowed course >=360 returns
   InvalidMetric. No saturation, clamping, angle wrapping or missing-value fallback.

An age exactly at the limit succeeds; limit+1 fails. The function uses the existing
64-bit session time, not raw millis or an invented fix acquisition epoch. Source
GNSS UTC, monotonic receipt and camera metadata interpretation remain distinct.

## Exact wire layout

The packet always has 71 bytes. Indexes are zero-based and inclusive.

| Bytes | Contents |
|---|---|
| 0..17 | `47 00 00 00 04 00 00 35 00 02 SS 00 00 80 00 00 0a 35`, where SS is the supplied sequence |
| 18..21 | uint32 UTC Unix seconds, least significant byte first |
| 22..28 | `00 00 00 00 00 00 41`, kept opaque |
| 29..36 | Absolute latitude degrees, binary64 little-endian |
| 37 | ASCII N when original latitude >=0, otherwise S |
| 38..45 | Absolute longitude degrees, binary64 little-endian |
| 46 | ASCII E when original longitude >=0, otherwise W |
| 47..54 | Speed m/s, binary64 little-endian |
| 55..62 | Course degrees supplied by GNSS, binary64 little-endian |
| 63..70 | Nonnegative altitude metres, binary64 little-endian |

For each scalar, narrow the validated value to IEEE binary32 and promote to
binary64, matching the reference's ordinary numerical precision. Coordinates
are magnitudes plus hemisphere bytes; both signed zeros canonicalize to positive
zero and N/E. Negative zero speed/altitude/course also serialize as positive zero.
Nonzero representable binary32 subnormals promote correctly; do not copy the
reference exponent rebias trick. Use verified IEC559 float/double widths and
`memcpy` to integer bit storage followed by explicit byte extraction, avoiding
aliasing, unaligned loads, struct padding and host-endian dependence. No native
struct serialization is permitted.

The source calls its field heading; RideSync supplies the GNSS course-over-ground
value. This is the selected GPS consumer mapping, not a claim that course is body
yaw or stationary compass heading. Documentation and future per-model
qualification must preserve that distinction. Satellites, accuracy, fractional
UTC, signed altitude, photo packets and arbitrary optional variants are unsupported.

## Source/license handling

Retain source/version/function/offset provenance with every field and fixture.
Do not import Garmin controller code, radio queues or unrelated assets. The new
source-derived encoder files and any derived packet fixtures carry MPL-2.0
file notices and attribution to the pinned reference. Add the matching license
text under `licenses/` and document file coverage in `docs/sources.md`; existing
repository files keep their existing license. Arithmetic tests are independently
written, with synthetic coordinates and no private locations or identifiers.
This is provenance preservation, not an assertion of camera certification.

## Verification and finite acceptance

`test/test_insta360_gps_encoder/` will cover:

- Complete independent literal packets for ordinary exact values in both
  hemispheres, with annotated field offsets and independent UTC arithmetic.
  Expected bytes must not be generated by the production encoder.
- North/south/east/west, both signed zeros, ±90/±180 and just-outside limits.
  Decode expected floats independently to check source binary32 precision.
- Measured zero versus absent fields; every missing UTC/metric combination;
  Missing/NoFix/Invalid/Stale, invalid clock/session/future/inconsistent receipt
  and age exactly at/one beyond configured bounds.
- UTC epoch examples, leap days, invalid dates/times, year boundaries and
  centisecond omission. No accidental dependence on owner receipt/anchor UTC.
- Zero/subnormal/normal extremes, float32 overflow/underflow, NaN/Inf, negative
  speed/altitude and course boundaries including float32 rounding to 360.
- Caller sequences 1/254 and rejection of 0/255; no mutation of config, fix or
  sequence input; identical inputs produce identical packets.
- Fixed 71-byte results, all-zero failure payloads, immutable returned copies,
  independently specified prefix/filler and full error precedence.

An independent Python fixture uses standard `struct`/calendar arithmetic to read
actual portable C++ encoder output, check every field and distinguish canonical
zero conversion from the upstream bug. It also rejects truncated/oversized
payloads and labels every fixture synthetic or source-derived, not captured.

Run focused native/Python encoder tests and existing GNSS/session tests only when
shared code changes. Compile pinned default/retained ESP32 builds, enforce C++
formatting and run CI on pushed main. Retain the encoder in an existing opt-in
compile image or an explicit data-only link anchor so the target check exercises
its emitted implementation without activation. Verify retained symbols, fixed
result/packet sizes and individual compiler frames; do not claim physical stack,
latency or camera acceptance from compilation.

Update `docs/gps_protocol.md`, `docs/sources.md`, testing/README and the issue
review with implemented capability limits and actual verification results. Apply
`docs/acceptance_policy.md`: #29 can finish after a real encoder path and these
software gates pass; #14 and physical acceptance remain separate and open.

## Handoff

The user approved this written contract on 2026-10-09. The
[implementation plan](../plans/2026-10-09-insta360-gps-encoder.md) was approved
and executed inline on main. Independent review and final CI passed at c52d5bc;
#29 is closed for software. Camera forwarding and physical acceptance remain
separate as specified above.

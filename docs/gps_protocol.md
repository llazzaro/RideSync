# GNSS and camera telemetry

Selected hardware: **A7670E with built-in GPS**, accessed through modem AT
commands. No external GPS module is planned. A portable GNSS response codec is
implemented alongside an opt-in portable AT acquisition driver; SD logger and
the pure camera GPS encoder remain separate. Physical driver qualification is pending.

LILYGO's [modem examples](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series)
are the starting point for identifying the modem and its GNSS variant. The
factory probe recorded `A7670E-FASE`, ATI revision `A7670M7_V1.11.1` and CGMR
`A110B01A7670M7_F` on the preinstalled 2022-11-21 AT firmware. The closest
date-matched manual located is the SIMCom-authored [A76XX Series AT Command
Manual V1.09](https://files.waveshare.com/wiki/A7670E-Cat-1-GNSS-HAT/A76XX_Series_AT_Command_Manual_V1.09.pdf),
hosted by Waveshare; its revision history ends in 2023. V1.09 is a working
documentary reference, not confirmation that it is the exact manual supplied
for this firmware. SIMCom now lists V2.03 in its [technical-document
index](https://cn.simcom.com/technical_files-p2.html?filetype=0&pro_cat=0&pro_li=83&time=0).
Retain the manual version with every command test and recheck against the module
variant and firmware before hardware qualification. Do not infer command support or
speed/altitude units from another A76xx model.

In V1.09, `AT+CGNSSPWR=1` returns `OK` on acceptance and may later emit
`+CGNSSPWR: READY!`; the manual says the position queries are valid after READY.
This probe observed acceptance and READY, then empty `AT+CGNSSINFO` and
`AT+CGPSINFO` fields, so it confirms enabled/ready with **no fix observed**.
It ran without a SIM, which establishes only that this startup path did not
require an inserted SIM. A valid fix, actual antenna hookup, and outdoor sky-view
test remain unverified. The manual specifies CGPSINFO latitude/longitude as
degrees-and-minutes, altitude in metres, speed in knots and course in degrees;
these are manual definitions, not fields validated against a real fix on this
unit. Documentary conversions are implemented and tested against V1.09; a live
valid fix and the manual-to-firmware match still require hardware validation.

The normalized codec fix carries coordinate validity, latitude/longitude in
degrees, optional MSL altitude in metres, speed in metres/second, and course in
degrees. Course over ground is not a stationary heading. UTC date and UTC time
are separately optional typed fields; satellite count and fix quality are
unavailable in CGPSINFO, not zero. Monotonic receipt time is distinct from UTC.
Freshness and session clock conversion belong to consumers, not this codec.

The [community GPS spec](https://github.com/TheAngryRaven/insta360-ble-gps-spec)
reports CE82 GPS/sensor notifications on an X4. That is not proof of support on
X5, GO 3S or ONE RS. For any CE82 encoder, verify complete framing, length,
endianness, signed coordinates, scales, timestamp interpretation and rate using
captures and golden fixtures. Store citations with every field definition.

Local CSV logging must continue without BLE: UTC, coordinates, optional altitude,
speed, heading, validity and satellites. Bounded buffers must prevent a missing
or full microSD card from stalling control. Telemetry injection is opt-in per
camera. The [experimental BE80 forwarding component](gps_forwarding.md) is
implemented; metadata interpretation remains untested and defaults stay off.

## Licensed binary telemetry reference (#29)

October 9 audit: [Garmin controller source](https://github.com/arsfabula/Insta360-Remote-CIQ/blob/39c51b3aa7c453227831d811355899371bbb8b94/BLE%20Barrel/BLEBarrel.mc)
provides a different candidate from CE82 RMC. `sendPosition` builds a 71-byte
binary video packet; `sendCMD` updates byte 10 with a 1–254 sequence and writes
20-byte chunks through BE81 under BE80. Its comments identify an epoch field,
little-endian doubles, hemisphere bytes, speed in m/s, heading in degrees and
altitude in metres. Unknown header constants remain opaque.

The pinned [README](https://github.com/arsfabula/Insta360-Remote-CIQ/blob/39c51b3aa7c453227831d811355899371bbb8b94/README.md)
reports ONE R 360-mod testing; ONE RS compatibility is a claim, not local evidence.
Both license files specify MPL-2.0. The source-derived encoder and synthetic
fixtures now retain that license; see [file coverage](sources.md#pure-gps-encoder-file-licensing-29).

Do not adopt its edge behavior: altitude loses its sign, missing speed becomes
zero, and the float32-to-float64 exponent rebias lacks a zero special case.
Independent arithmetic gives `2^-127`, rather than zero, for float32 zero.
No raw camera capture or camera golden was obtained in this audit. The pure
encoder below supplies independent software expectations for numeric/validation
behavior. Actual target support remains unqualified; #14 owns delivery. This is
a binary BE80 path, not a CE82 encoder or metadata qualification.

### Field-level evidence reconciliation — 2026-10-09

Read the same pinned source again for #29. Garmin's primary API documentation
resolves the time input: [`Time.Moment.value()`](https://developer.garmin.com/connect-iq/api-docs/Toybox/Time/Moment.html#value-instance_function)
returns UTC seconds since 1970-01-01. In `sendPosition`, `info.when` supplies that
Moment and `encodeNumber(...UINT32...)` writes four bytes at offsets 18..21.
This establishes the source's timestamp convention; it is not a camera capture
or proof of a camera's interpretation. The zero bytes at 22..25 must remain
opaque; they cannot be claimed as a supported fractional-time field.

| Packet offsets (inclusive) | Source-backed representation | Evidence limit |
|---|---|---|
| 0..17 | Fixed 18-byte video prefix, with byte 10 replaced by sendCMD sequence | Other header constants have no established semantics |
| 18..21 | Little-endian uint32 UTC epoch seconds | Source/API convention; actual target interpretation unverified |
| 22..28 | Fixed seven-byte filler | Includes unexplained constants; no optional-field flags inferred |
| 29..36, 37 | Absolute latitude in degrees, little-endian binary64; ASCII N/S | Source chooses N for zero; no signed-coordinate field |
| 38..45, 46 | Absolute longitude in degrees, little-endian binary64; ASCII E/W | Source chooses E for zero |
| 47..54 | Nonnegative speed in m/s, little-endian binary64 | Source invents zero when speed is absent; not an availability encoding |
| 55..62 | Heading in degrees, little-endian binary64 | Input is Garmin heading in radians; body yaw/stationary heading and GNSS course are not interchangeable claims |
| 63..70 | Absolute altitude in metres, little-endian binary64 | Negative-altitude sign is discarded; signed altitude is not evidenced |

Garmin's [`Position.Info`](https://developer.garmin.com/connect-iq/api-docs/Toybox/Position/Info.html)
documents nullable fields. Missing speed, heading, altitude or UTC therefore
cannot be represented honestly by silently inserting zero. No presence bits or
shorter video variant were established. The implemented strict encoder refuses
missing required fields and unsupported negative altitude rather than invent a
wire convention. These are explicit consumer policies, not observed protocol
requirements. Sequence belongs to the caller's complete command stream: the
source increments 1..254 at byte 10 before splitting into 20-byte writes. A pure
encoder must not infer delivery success or consume a transport sequence itself.

The helper first narrows each scalar to binary32 before constructing its binary64
representation. Normal finite values have a source-derived numeric path; its
zero/subnormal/NaN/Inf cases are not evidence for sensible wire values. Independent
expected fixtures must distinguish canonical IEEE encoding policies from exact
reproduction of the helper's mistakes. None of this enables a camera profile. The software encoding path and independent
fixtures are implemented below; delivery and camera metadata remain separate.

## Implemented pure encoder (#29)

`ridesync::insta360::encodeGps(config, now, snapshot, sequence)` consumes copied
`RecordTimestamp`/`ModemSnapshot` observations. `GpsWireProfile` defaults Disabled;
`GarminBe80VideoV1` must be selected explicitly with age limit 1..60000 ms.
Sequence is caller-owned, 1..254; the encoder neither increments it nor chunks,
queues, transmits or retries. Success is `None`, size71 and fixed bytes; every
failure is size0 with all-zero bytes. No driver, clock read or allocation is used.

Validation order: profile/age/sequence; valid bounded session clock; matching
session/Valid fix/receipt and recomputed consistent fresh age; finite coordinate
ranges; all five UTC/metric availability flags; real Gregorian UTC 2000..2099;
finite nonnegative speed, course [0,360), finite altitude; nonnegative altitude;
then binary32 representability and narrowed course <360. Errors are Disabled,
InvalidConfig, InvalidSequence, InvalidClock, InvalidFix, InvalidCoordinate,
MissingField, InvalidUtc, InvalidMetric and UnsupportedAltitude. Age at the limit
passes; a retained copy beyond it fails. Source UTC supplies whole epoch seconds;
centiseconds are validated then omitted, and session anchors are ignored.

The prefix is `47 00 00 00 04 00 00 35 00 02 SS 00 00 80 00 00 0a 35`;
SS is byte10. Filler22..28 is `00 00 00 00 00 00 41`. Offsets/units follow the
source table above. Each scalar narrows to binary32 and promotes to binary64,
serialized explicitly little-endian. All signed zeros canonicalize to positive
zero (coordinates N/E); representable subnormals promote correctly. Overflow and
nonzero underflow-to-zero fail, as does course rounding to360. Coordinates use
magnitude plus hemisphere; negative altitude is refused. RideSync maps GNSS
course-over-ground to the source heading field, without claiming stationary
heading or body yaw. No photo packet, accuracy, satellite, fractional UTC,
signed altitude or arbitrary optional variant is supported.

Independent synthetic full packets cover both hemispheres/sequences. Thirteen
Unity tests exercise parameterized validation, calendar, numeric, freshness and
precedence cases. Six Python oracle tests compile the actual C++ encoder and
check every output byte using independent standard-library calendar/struct
arithmetic, including zero/subnormals/precision and 70/72-byte rejection.

The opt-in HERO12 compile image retains the real encoder through a data-only
anchor, without calling it or changing camera capabilities. The pinned target
ABI is config8/result80/packet71 bytes, packet member offset8. Individual static
compiler frames are encodeGps256, narrowScalar48, writeDouble32 and leap32 bytes.
The audit resolves static Xtensa literal-loaded calls, rejects unresolved calls
and unexpected ordinary dependencies, and verifies actual anchor contents.
Pinned absolute ROM arithmetic helpers are trusted where ELF bodies are absent;
compiler `__stack_chk_fail` remains an explicitly excluded integrity-abort path.
These are compile/link facts, not physical worst-case stack/latency, camera
acceptance, ACK, stored metadata or enabled profile evidence. Tests and CI
commands are in [testing](testing.md#pure-insta360-gps-encoder-software-checks-29).

## Local ride logger scope

The [#14 forwarding contract](gps_forwarding.md) implements real encoder-to-ATT
delivery, opt-in ONE RS source-profile selection and optional composed GNSS
publication. Bounded latest-fix/rate/MTU admission and a shared command-stream
lease prevent uncontrolled telemetry queues and interleaved control bytes.
Camera metadata stays unobserved in #46/#22; other model routes remain disabled.

Local GPS logging is a core project goal. An external IMU will add raw motion
and separately validated derived acceleration/lean/pitch records. These use a
shared session/timebase with camera events. Camera telemetry injection remains
optional and profile-specific. HERO12 Black footage can be aligned with external
logs; embedding the logs into camera metadata is not promised.

## First hardware observation

The [factory-firmware probe](hardware-results/2026-10-07-bringup.md) confirmed
A7670E-FASE and accepted `AT+CGNSSPWR=1`, then emitted READY. `CGNSSINFO` and
`CGPSINFO` returned empty fields: no position fix was observed. Documentary codec
work (#25) and transport work (#10) remain separate from physical fix validation.

## Codec contract and evidence

`parseGnssLine(line, length, receipt_monotonic_ms)` in `gnss_parser.h` consumes
one complete response line using fixed storage. The caller supplies the explicit
byte length and a raw monotonic receipt timestamp; no Arduino, UART, BLE or SD
state is consulted. A final CR, LF or CRLF is permitted, and spaces immediately
after the colon are permitted. Embedded control characters/NUL, additional AT
exchange lines, fields over 24 bytes, lines over 256 bytes, and more than 17
fields are rejected. It never waits for READY or acquires a fix.

The selected **documentary CGPSINFO profile** is V1.09 §24.2.11, printed
pp.490–491: exactly nine comma-separated fields, latitude `ddmm.mmmmmm`, N/S,
longitude `dddmm.mmmmmm`, E/W, date `ddmmyy`, UTC `hhmmss.ss`, MSL altitude
(metres), speed (knots), course (degrees). Coordinates convert as degrees plus
minutes/60, with south/west negative; knots convert by 1852/3600. Coordinates
and hemisphere indicators are mandatory for a fix. Date, time and each metric
may be empty independently; explicit availability flags distinguish missing
from a measured zero. A valid codec fix means structurally valid coordinates,
not hardware-qualified position accuracy.

The two-digit year policy is **2000–2099** (`2000 + yy`), explicitly a codec
policy rather than an inference from receipt time. Gregorian dates, including
leap days, are checked. Time retains centiseconds and rejects second 60; leap
seconds require a future explicit policy. No epoch or session-time conversion
is performed here (#26). Numbers use plain decimal grammar: no exponent, plus
sign, NaN, infinity, trailing junk or incomplete fraction. Negative altitude
is allowed; speed is nonnegative, course is in [0,360), and coordinate minutes
are in [0,60) within ±90/±180 degrees.

`Fix`, `NoFix` and `ParseError` are distinct statuses; errors identify the
failed input category. Errors return no partially valid fix or optional metrics.
The nine-empty-field CGPSINFO and CGNSSINFO responses are the observed no-fix
forms. Other CGNSSINFO payloads return `UnsupportedSchema` within input bounds:
V1.09 §24.2.12 pp.491–493 documents multiple layouts and coordinate wording
that conflicts with its example. Numeric magnitude or field count never
selects a coordinate interpretation. A later profile needs explicit evidence.

[Test fixtures](../test/fixtures/gnss/README.md) separate the public manual
example from the locally observed empty responses. Boundary inputs are
synthetic. Tests do not establish a fix acquired on A7670E-FASE, ATI revision
A7670M7_V1.11.1 / CGMR A110B01A7670M7_F.

## Bounded software AT acquisition (#10)

`ModemGnss` is a portable single-owner transport with fixed line storage and one
outstanding command; `GpsManager` samples the same `SessionClock` used by camera
records. Pass its `RecordTimestamp` to each transport tick/snapshot. Receipt time
is the shared **session elapsed monotonic milliseconds** at the tick completing
the data line, and is retained until the command's successful terminal. It is not
UTC, modem sampling time or terminal time. Sample the raw clock less than 2^32 ms
apart as required by `SessionClock`; raw millis rollover is handled there. A new
session, backwards/invalid time or exceeded clock duration invalidates acquisition.
A manager that sees invalid time latches InvalidClock until explicit recovery.
After resetting SessionClock to a valid unique session, use
`GpsManager::restartAfterVerifiedBarrier()` (rather than restarting its modem
independently). This applies the modem's same receive-empty/profile/lifetime-cap
guards and binds the manager to the new session only on success. Caller asserts
that qualified physical supply/reset startup is complete and all old command
executions and buffered/in-flight bytes are retired. Recovery deasserts an
interrupted PWRKEY, resumes AT startup and marks the power stage Complete without
replaying supply enable or a key pulse. Rejected recovery does not clear a latched InvalidClock; an interrupted key is
released for safety. Old fixes and provisional data are
cleared by the accepted modem restart. Invalid time or a session change during
initial physical startup cannot issue an AT command before this recovery.

The sequence is `AT` → successful terminal, `AT+CGNSSPWR=1` → successful terminal,
then `+CGNSSPWR: READY!`, then serialized `AT+CGPSINFO` polling. V1.09 §24.2.1
printed pp.478–479 documents power acceptance, READY and a 9000 ms response
maximum; §24.2.11 pp.490–491 documents CGPSINFO. The repository's sanitized
factory probe independently supports `AT` → `OK`, power acceptance and READY,
but not a qualified startup duration. Software defaults (10 s command, 15 s
READY, 1 s poll, 3 s stale) are configurable scheduling policies, **not measured
hardware timings**. No SIM setup, cellular connection, cold-start command,
identity logging, SD or BLE operation is performed.

Power acceptance, receiver READY and structurally valid fresh fix are separate.
Query data is provisional until its own `OK`; ERROR, missing/duplicate/malformed
payload or wrong CGNSSINFO schema yields explicit invalidity. Empty CGPSINFO
returns NoFix. `snapshot` returns validity Missing/Valid/NoFix/Invalid/Stale,
optional age, normalized fix, session ID and UART health. A stale snapshot clears
`fix.valid`; consumers must check validity and availability rather than interpret
zeros as measurements. Age is available for successful fix/no-fix responses,
including stale ones; invalid/error/missing data has no age. UTC stays optional
and does not automatically anchor the session clock.

AT UART responses carry **no transaction generation identifier**. Timeout is
checked before parsing late bytes, discards provisional data and enters
Desynchronized. Silence or a quiet window never permits a retry. Opting into
`terminal_retires_transaction` explicitly asserts an ordered exclusive UART,
one response terminal per command, no duplicated terminal or asynchronous GNSS
query responses, and that a fully transmitted command's OK/ERROR/CME/CMS terminal
retires all its response data. Under these assumptions, draining that old
terminal permits a bounded retry; all old data is discarded. This documentary
contract still needs verification against this exact firmware. A partially
transmitted command cannot be retired by a received terminal. If any assumption
is unavailable, leave the driver disabled. A caller may instead invoke
`restartAfterVerifiedBarrier` only after a qualified physical reset/receive
barrier retires all old executions and buffered/in-flight bytes; the method does
not perform or infer such a reset. Unsolicited modem `RDY` invalidates state and
requires verified physical/receive barrier recovery even if a terminal later arrives. READY alone never retires a transaction.

Each tick reads at most the configured 1–256 bytes and writes at most 1–32 bytes
(64/16 defaults), with no dynamic queue or allocation. Frames exceed neither
256 bytes nor the codec bounds. Overflow discards the frame through its delimiter
and enters Desynchronized. Input flooding/backpressure cannot extend a startup
or pending poll indefinitely. Successful queries reset the consecutive attempt
budget; terminal errors and retired timeouts permit at most 1–8 total attempts
(default 3). READY silence fails; transaction silence stays Desynchronized with
health Timeout. Barrier restarts are limited to 0–8 (default 2) for the object's
lifetime. Durations must be positive and below 2^31 ms. No retries consume an
unretired exchange. The caller continues ticking cameras/scheduler independently.

`modem_arduino.h` provides opt-in UART and supply/PWRKEY adapters. They have **no
pin, polarity or baud defaults**, drive no RESET pin and are never instantiated
by the serial-only firmware. `begin` requires explicit pin qualification and
profile opt-in. Qualification includes physical routing, boot straps, electrical
levels and conflicts. `GpsManager` additionally requires a qualified power timing
(or explicitly qualified externally powered operation). Supply enable/key pulse/
settling proceed on ticks, with caller-provided timings bounded to 60 s each as a
software guard; no timing is invented for this board. UART uses availableForWrite
prechecks, one writer and bounded writes without flush or busy waits. Callbacks
must return promptly; tick cadence must satisfy the qualified physical pulse
upper bounds. These adapters compile but have not been operated on hardware.

Physical qualification belongs to #30/#31 and remains **Not tested**: verify board routing and
power/boot timing, exact firmware/manual/terminal semantics, antenna handling,
RideSync startup/READY, outdoor cold-start fix/no-fix duration, field units,
stale-data behavior and power/recovery with annotated sanitized bench captures.
The factory probe and fake-UART tests do not qualify these physical behaviors.

The opt-in production-driver diagnostic in `tools/bench/gnss_driver/` is
prepared and compiled, with host fault-injection coverage. It has not been run
on the fitted board and does not supply physical qualification evidence.

# GNSS and camera telemetry

Selected hardware: **A7670E with built-in GPS**, accessed through modem AT
commands. No external GPS module is planned. A portable GNSS response codec is
implemented; AT driver, SD logger and GPS BLE encoder work remain separate.

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
X5, GO 3S or ONE RS. Before writing an encoder, verify complete framing, length,
endianness, signed coordinates, scales, timestamp interpretation and rate using
captures and golden fixtures. Store citations with every field definition.

Local CSV logging must continue without BLE: UTC, coordinates, optional altitude,
speed, heading, validity and satellites. Bounded buffers must prevent a missing
or full microSD card from stalling control. Telemetry injection is opt-in per
camera and disabled until its profile is validated.

## Local ride logger scope

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

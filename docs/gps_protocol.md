# GNSS and camera telemetry

Selected hardware: **A7670E with built-in GPS**, accessed through modem AT
commands. No external GPS module is planned. No GNSS parser, AT driver, SD
logger or GPS BLE encoder is implemented.

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
variant and firmware before implementation. Do not infer command support or
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
unit. Keep unit conversion out of the parser until a valid sample is captured.

Proposed normalized fix: validity, latitude/longitude in degrees, altitude in
metres when available, speed in metres/second, heading in degrees, UTC timestamp,
satellite count and fix quality when supplied, plus monotonic receipt time.
Represent missing values explicitly and reject stale/invalid fixes.

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
`CGPSINFO` returned empty fields: no position fix was observed. Production parser
and transport work remain separate reviewed issues #25 and #10.

# GNSS and camera telemetry

Selected hardware: **A7670E with built-in GPS**, accessed through modem AT
commands. No external GPS module is planned. No GNSS parser, AT driver, SD
logger or GPS BLE encoder is implemented.

LILYGO's [modem examples](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series)
are the starting point for identifying the modem and its GNSS variant. Capture
modem identification/firmware, consult its exact SIMCom AT manual, then verify
GNSS power/fix commands and response fields. Do not infer command support or
speed/altitude units from another A76xx model.

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

# Factory firmware reconnect investigation — 2026-10-08

## Observations

After the user reconnected the board, macOS enumerated its USB serial bridge
as VID:PID **1A86:55D4**. The descriptor was `USB Single Serial`; this does not
independently identify the bridge silicon or physical PCB revision.

One 115200-baud serial session, with DTR and RTS configured false before open,
captured a fresh ESP32 `POWERON_RESET` / `SPI_FAST_FLASH_BOOT` banner. The
diagnostic again reported ESP32-D0WDQ5 revision 3, 240 MHz, 4 MB flash and 4 MB
usable PSRAM. The application banner was dated **2022-11-21 23:37:38** and
identified itself as the factory AT-command interaction firmware.

The session ended after 55 seconds at `Modem starting...`, without a
`Modem started` marker. No host AT commands were sent in this session. The
serial port was closed afterward. This observation does not establish a safe
startup deadline, prove that the modem is electrically off, or establish the
cause of the stalled startup.

Earlier sessions on the same day were inconsistent: one reached
`Modem started` at approximately 17.13 seconds but did not answer the subsequent
four-second AT probe; another did not reach that marker during its bounded
capture. Reopening serial repeatedly produced fresh factory boot banners.
Repeated port opens are therefore not a substitute for a controlled modem
power-cycle test.

No firmware upload, erase or state-changing host AT command was performed in
these reconnect checks. Firmware-controlled startup GPIO actions were not
instrumented. Physical LED behavior, supply voltage/current and modem UART
signals were not measured. The successful GNSS-ready/no-fix observation from
[October 7](2026-10-07-bringup.md) has not been reproduced in these checks.

## Source comparison

The vendor's [ATdebug example at the pinned reference revision](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00/examples/ATdebug/ATdebug.ino)
controls power, reset, DTR and PWRKEY, checks AT responses and scans baud rates
before entering its serial forwarding loop. Its messages differ from the
installed factory program. The [historical November 2022 example](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/b68a5a826593d8e16891c0825442809da1461ed0/examples/new_version/Arduino/ATdebug/ATdebug.ino)
also differs. Neither source has been established as the exact installed binary
source; its startup behavior must not be attributed to this unit as fact.

## Next bench gate

The user confirmed that a battery or external supply is connected in addition
to USB; which of the two is fitted remains unspecified. Disconnecting USB alone
may therefore leave the modem powered. The user subsequently confirmed completing
the requested removal of all power and reconnection with USB only. A new
60-second serial capture again showed the same factory ESP32 boot and ended at
`Modem starting...`, without `Modem started`. No host AT queries were sent;
the port was closed. Opening serial may itself restart the ESP32, so this is
not an uninterrupted capture from initial physical power application. The
power-cycle report does not establish measured rail discharge or supply quality.
The stall persisted after the user-reported power cycle; its cause remains
unproven. Record the
actual power-cycle procedure and fitted PCB revision, then capture one continuous
startup session. Require a timely `AT` → `OK` before identity/GNSS queries and
measure elapsed startup time. If communication still fails, measure the supply
and qualified reset/PWRKEY/UART signals against the matching schematic before
changing firmware or pin assignments.

Issue #1 remains open. These results establish USB communication and ESP32 boot,
not modem recovery, a GNSS position fix, or RideSync firmware qualification.

## Photographic identification and diagnostic preparation

The user's subsequent photographs show a V1.4 PCB marking, an ESP32-WROVER-E
module, an A7670E modem label, an empty 18650 holder and a 16 GB microSD card.
A blue LED is illuminated; still photographs cannot establish blinking behavior
or prove modem readiness. The GNSS antenna connector appears unconnected.
Device identifiers visible in the photographs are not reproduced here, and the
original photographs are not included in the public repository.

The user then reported completing removal of the microSD and reconnection via
USB-A to USB-C. The vendor's [board instructions](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00/docs/en/esp32/a7670-esp32/README.MD)
recommend that cable arrangement, require adequate peak USB power, and warn that
an inserted card can interfere with uploading. Supply voltage/current remains
unmeasured.

The ESP32 bootloader subsequently detected revision v3.1 and 4 MB flash. An
initial read-only full-flash backup attempt at 460800 baud failed with an invalid
packet header; this is evidence of a failed serial transfer, not a diagnosis of
its cause. No flash was written or erased by that attempt.

An isolated build of the vendor's pinned ATdebug source, with unchanged source
files and `LILYGO_T_A7670` selected, succeeded using RideSync's pinned
Arduino/ESP32 toolchain. Its selected modem UART TX/RX 26/27, reset 5, PWRKEY 4
and DTR 25 agree with the vendor V1.4 schematic reference. This documentary
comparison does not replace electrical measurements. The diagnostic source
drives power enable 12 high, sequences reset/PWRKEY, holds DTR low and probes
baud rates. It is a vendor diagnostic, not RideSync application firmware.

## Verified factory backup and vendor diagnostic run

The retry at 115200 baud read the complete 4,194,304-byte ESP32 flash in
379.2 seconds. A separate device-side digest comparison of the entire address
range passed before any firmware write. The backup SHA-256 is
`1c0be69a89e205be67c1df57f1a21ffc75ce95d18526f58ec3b3bc7100b6f18f`.
Two private local copies were retained, including one outside the repository.
The binary, NVS contents and device identifiers are not published.

After verification, the pinned vendor ATdebug diagnostic was uploaded at
115200 baud. The bootloader, partition table, boot selection data and diagnostic
application regions were written; all four write checksums passed. There was
no whole-chip erase and no SIMCom modem-firmware update. **The board now runs
the vendor diagnostic instead of its original factory ESP32 application.**
The full-flash backup preserves the original image for restoration.

One subsequent continuous capture reported the selected ESP32 T-A7670 profile
and these software stages: UART RX27/TX26 and power enable GPIO12 high at
approximately 5.66 seconds, reset GPIO5, DTR GPIO25 low and PWRKEY GPIO4 at
approximately 8.36 seconds. These prints are not voltage measurements.

The diagnostic probed 13 UART rates: 115200, 9600, 57600, 38400, 19200, 74400,
74880, 230400, 460800, 2400, 4800, 14400 and 28800. It found no `OK` response
and printed its terminal failure at approximately 153.70 seconds. The host sent
no additional identity/GNSS queries because readiness was never established.
The capture ended and the serial port was closed.

This failure persists with the vendor startup sequence and baud probes; it does
not establish defective hardware, a firmware cause, actual GPIO voltage levels,
or adequate supply current. The next evidence needed is modem LED behavior and
qualified power/reset/UART electrical measurements. Issue #1 remains open.

## Recovery after a further USB connection change

The user reported that both modem red LEDs remained off, then changed the USB
connection again. Its exact cable, port and supply characteristics were not
measured or independently identified. No firmware was written during the
following two sessions: the same vendor diagnostic remained installed.

In the first session, the modem responded at 115200 baud approximately
19.78 seconds after serial open. A host `AT` received `OK`. `ATI` identified
**A7670E-FASE**, revision **A7670M7_V1.11.1**; `AT+CGMR` returned
**A110B01A7670M7_F**. GNSS power was initially 0 and the location query returned
ERROR while GNSS was off. The module also reported `SIM REMOVED`.

In the second session, the modem again responded at 115200 baud, approximately
19.73 seconds after open. Following an independently successful host `AT`,
`AT+CGNSSPWR=1` received `OK` at approximately 20.12 seconds. The unsolicited
`+CGNSSPWR: READY!` arrived at approximately 28.44 seconds. Seven subsequent
power/location query pairs, ending at approximately 91.90 seconds, returned
power 1 and empty `+CGNSSINFO: ,,,,,,,,` fields with `OK`.

The serial ports were closed after both bounded sessions. GNSS was requested
on and was not deliberately powered off afterward. No position fix was observed.
The antenna appeared unconnected in the earlier photographs; its current
connection and sky view remain unverified.

This reproduces modem command readiness twice and GNSS-ready/no-fix behavior
with the vendor diagnostic after the connection change. It does **not** isolate
the electrical cause of the earlier failures: cable capability, port power,
voltage sag, reset state and other changed conditions were not measured. Two
startup observations do not qualify a worst-case startup deadline. RideSync
drivers, actual GPS position, camera control and SD/IMU operation remain untested
on the device. Issue #1 remains open.

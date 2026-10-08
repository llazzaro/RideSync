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
may therefore leave the modem powered. A complete power cycle has been requested
but has not yet been observed or confirmed. Record the
actual power-cycle procedure and fitted PCB revision, then capture one continuous
startup session. Require a timely `AT` → `OK` before identity/GNSS queries and
measure elapsed startup time. If communication still fails, measure the supply
and qualified reset/PWRKEY/UART signals against the matching schematic before
changing firmware or pin assignments.

Issue #1 remains open. These results establish USB communication and ESP32 boot,
not modem recovery, a GNSS position fix, or RideSync firmware qualification.

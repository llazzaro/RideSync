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

## Antenna-connected trials and serial-open reset investigation

The owner subsequently confirmed the GNSS antenna was connected, with a red
modem LED on and the antenna indoors/by a window. A continuous session reached
GNSS READY and returned twelve `AT+CGPSINFO` responses parsed by the actual
RideSync host-built parser as `NoFix`, ending at approximately 143.31 seconds.
No coordinates were observed. This was still vendor firmware on the ESP32,
not on-device RideSync driver qualification.

Startup remained intermittent. A later run with USB-A to USB-C completed all
13 baud probes without modem response, printing failure at approximately
153.7 seconds. The owner reported modem LEDs off. After unplug/replug the LEDs
were reported on, but during another ordinary serial-open trial they went off.
That trial showed a fresh ESP32 startup and the diagnostic's modem reset/PWRKEY
sequence; it was deliberately stopped after approximately 89.8 seconds with
no host AT success. It did not complete the baud sweep. These observations do
not distinguish a driver control-line transient, modem reset/PWRKEY timing or
supply behavior; no electrical waveform or supply measurement was taken.

After another owner unplug/replug with red LEDs on, a connection was opened
without pySerial's explicit DTR/RTS state updates and with HUPCL cleared through
termios. This trial received a host `AT` → `OK` without an observed ESP32 reset
banner. `AT+CGNSSPWR?` returned 0; enabling GNSS received `OK`, followed by
`+CGNSSPWR: READY!`. This supports investigating serial-open/startup behavior;
a single trial does not establish a reliable fix or prove which control signal
caused the earlier failure. No firmware was written during these trials.

The reproducible connection setup and its platform/version limitations are in
[Connecting this device](../device_connection.md). Raw logs remain private and
ignored by Git; no IMEI, serial number, coordinates or flash contents are added
to this report.

The passive-open session ended normally after approximately 199.9 seconds:
18 `AT+CGPSINFO` responses were parsed as `NoFix`, with no observed ESP32 reset
banner. The owner confirmed the antenna remained indoors next to a window and
could not perform an outdoor test today. Clear-sky position acquisition is
therefore deferred; the no-fix result does not establish a receiver/antenna
fault. The host serial descriptor was closed after the bounded test.


## Warm reconnect repeatability

After the passive-open GNSS session, three consecutive passive close/reopen
trials each sent only `AT` and received `OK` in a three-second capture window
(3.07, 3.08 and 3.09 seconds total capture duration). HUPCL was confirmed clear
on each open descriptor. No ESP32 reset/startup banner was observed. Each port
was closed after its capture, with one second between trials. No firmware or
GNSS power command was sent. These are successful warm serial reconnects with
the already-running vendor bridge, not measured AT response latencies or
cold-start deadline/power-cycle qualification. The earlier ordinary-open
failure remains in the evidence; no further potentially disruptive comparison
was made. Issue #1 remains open for its exact manual and electrical/startup
qualification criteria.

## microSD host preparation

The owner identified the host-mounted test card as `H1N_SD`. macOS reported an
external writable FAT32 volume of 15,923,183,616 bytes, with approximately
14.16 GB free. The volume already contained user files; their names and contents
are not published. Neither RideSync ledger slot existed. A host open-file check
reported no open descriptors on the volume at preparation time; this is not a
continuous exclusive-access guarantee.

A fresh nonzero 32-bit namespace was reserved before card writes in an external
Mac-side registry under `Documents/RideSync-backups/sd-namespace-reservations`.
The reservation is permanent even on failure and is scoped to this controlled
RideSync deployment; random selection alone is not a global uniqueness proof.
Using the existing offline commissioner, both root ledger slots were created
exclusively, synced and read back. Each was 40 bytes and its namespace and CRC
verified. Existing user files were not intentionally modified; the card was not
formatted. The two commissioning tests also passed, including consumption by
the real C++ session allocator. These results establish host preparation only,
not ESP32 SD reads/writes, filesystem durability or power-loss behavior.

The separate [SD read-only bench diagnostic](../../tools/bench/sd_readonly/README.md)
builds with the pinned ESP32 toolchain. Its file operations are limited to
reading the two fresh baseline slots at 1 MHz, with automatic formatting
disabled. It awaits an explicit serial `S` command and does not operate modem
reset/PWRKEY. Production storage/logger acceptance (#11) remains open.


The read-only diagnostic was subsequently uploaded at 115200 baud; all four
written-region hashes verified. The private factory-backup SHA-256 was checked
again before the write. The ESP32 now runs `sd_readonly`, replacing ATdebug;
SIMCom firmware was not changed and no whole-chip erase was requested. With the
prepared card still host-mounted, serial `S` produced the expected `mount FAILED`
result for the empty board socket. This is a negative-path check, not a
successful physical card test. The prepared host card was then safely ejected
for insertion with all board power removed. Actual card/ledger reading on the
ESP32 is the next pending bench step.

# T-A7670E microSD bench probe

Temporary diagnostic for the photographed V1.4 ESP32-WROVER-E/A7670E board.
This is separate from the production application and does not qualify the
production storage worker, power-loss durability or logging latency.

Uses documentary pin candidates power enable 12, SCK 14, MISO 2, MOSI 15,
CS 13 from the [pinned LILYGO profile](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00/examples/ATdebug/utilities.h)
and the [V1.4 schematic](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00/schematic/esp32/T-A7670X-V1.4.pdf).
Physical routing/electrical qualification remains incomplete. Do not use these
pins on another board variant by assumption.

The probe enables the peripheral supply, initializes SPI and waits for serial
`S`. Each request mounts at 1 MHz with formatting explicitly disabled, reads only
`/.session-id-a` and `/.session-id-b`, validates both fresh 40-byte baseline
records (including CRC, complements and equal namespace), then unmounts. It does
not list user filenames, print namespace/device identifiers, create files or
control modem reset/PWRKEY. The filesystem is not hardware write-protected;
no power-loss safety guarantee follows from file reads alone. Mount errors
return to the command loop. Blocking SDK I/O is acceptable only in this isolated
bench diagnostic, not the production scheduler.

## Procedure

1. Preserve a verified ESP32 backup before replacing installed firmware. Upload
   only while the card is removed from the board (GPIO2 is a boot strap).
2. From the repository root, build with
   `.venv/bin/pio run -d tools/bench/sd_readonly`.
3. Upload using
   `.venv/bin/pio run -d tools/bench/sd_readonly -t upload --upload-port <enumerated-port>`.
   This replaces the current ESP32 application. It does not update SIMCom firmware.
4. Prepare a FAT32 card with the existing
   [offline session-ledger procedure](../../../docs/log_format.md). Existing
   files must be preserved. Never rerun commissioning to reset an existing ledger.
5. Safely eject the host-mounted card. Remove all board power, insert the card,
   then reconnect power without holding BOT or IO0.
6. Open serial at 115200 baud, following the
   [macOS connection procedure](../../../docs/device_connection.md). Send `S`
   only after the boot message or after confirming the sketch is running.
7. Record card type/capacity, both slot statuses and `baseline_pair=VALID`.
   INVALID is expected for a previously allocated ledger with nonzero counters;
   do not erase or reset it to make this fresh-baseline test pass.
8. After the test, remove power before moving the card. Verify both ledger files
   and preexisting data remain intact on the host. Restore vendor ATdebug or
   proceed to explicitly commissioned production firmware as appropriate.

No red modem LEDs are promised by this sketch: modem startup is deliberately
absent. A successful probe demonstrates this pin/card/filesystem read path only;
it does not complete issue #11's write/reset/power-loss acceptance.

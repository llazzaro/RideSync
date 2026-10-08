# Connecting this T-A7670E on macOS

These instructions describe the photographed **V1.4 ESP32-WROVER-E board with
A7670E-FASE built-in GNSS**, tested on October 8, 2026. The modem connection trials used
LILYGO ATdebug from revision `e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00`,
not the RideSync application. Later on the same day, the board was switched to
the [SD read-only diagnostic](../tools/bench/sd_readonly/README.md) for card
qualification. It does not expose the AT bridge or start the modem; restore
ATdebug before repeating this guide's modem commands. The original full flash has been backed up and
verified privately; the SIMCom modem firmware has not been changed.

## Power and indicators

Use the board's **USB-C connector** for ESP32 serial communication. The separate
Micro-USB connector is used for modem firmware upgrades. LILYGO recommends
USB-A to USB-C and specifies a 5 V USB/VBUS source capable of 2 A peaks.
Our unit is using USB-A to USB-C; its voltage/current have not been measured.
Do not infer adequate modem power simply because the ESP32 enumerates.

Red LEDs near the modem indicate modem status/network activity, not a GPS fix.
Blue LEDs relate to charging. If modem LEDs are off and AT commands receive no
response, diagnose startup before spending time on antenna reception.
For a complete power cycle, disconnect USB and every battery/external source;
the switch does not disconnect USB or a supply wired to VBAT. Reconnect without
holding BOT, which selects modem firmware update mode.

## Avoid restarting the modem when opening serial

Ordinary pySerial opens repeatedly coincided with an ESP32 boot banner. ATdebug
then drives GPIO12 high, resets the modem on GPIO5 and pulses PWRKEY on GPIO4.
In one owner-observed trial, red LEDs went off during that connection attempt
and no AT response followed. This establishes an observed sequence, not an
isolated electrical cause.

A subsequent macOS trial skipped pySerial's explicit DTR/RTS updates and cleared
termios HUPCL on the open descriptor. It received `AT` → `OK` without an observed
ESP32 reset banner. Three subsequent close/reopen trials each received `OK`
within a three-second capture window, with HUPCL confirmed clear and no observed
reset banner. These were warm reconnects, not cold-start deadline measurements.
Use one continuous connection for startup and GNSS polling;
avoid repeatedly opening a monitor while diagnosing this behavior.

The tested connection setup is below. It uses **pySerial 3.5 private POSIX hooks**,
so recheck behavior after changing Python, pySerial, USB driver or operating
system. This cannot guarantee that the OS/driver itself will never toggle a
control line during open. It is a macOS bench workaround, not a portable firmware
fix. It disables explicit host DTR/RTS updates; modem DTR GPIO25 is a separate
signal controlled by ATdebug.

```python
import serial
import termios

class PassiveSerial(serial.Serial):
    def _update_dtr_state(self):
        pass

    def _update_rts_state(self):
        pass

    def _reconfigure_port(self, *args, **kwargs):
        super()._reconfigure_port(*args, **kwargs)
        attrs = termios.tcgetattr(self.fd)
        attrs[2] &= ~termios.HUPCL
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)

# Enumerate ports first; do not assume the device path is stable.
port = PassiveSerial(port=None, baudrate=115200, timeout=0.2,
                     rtscts=False, dsrdtr=False)
port.port = "/dev/cu.usbserial-REPLACE_WITH_ENUMERATED_PORT"
port.open()
# Keep this descriptor open for the entire command/capture session.
# Send commands with CRLF and read complete responses before sending another.
# Close with port.close() in a finally block when the session ends.
```

Enumerate using `.venv/bin/python -m serial.tools.list_ports`. Only one process
should own the serial port. This workaround is for the existing ATdebug bridge;
it does not upload, erase or restore firmware. Capture any ESP32 reset banner
and compare red LED behavior instead of assuming a no-reset connection worked.

## GNSS commands and interpretation

1. Send `AT\r\n` and require `OK` before other commands.
2. Query `AT+CGNSSPWR?`. If off, send `AT+CGNSSPWR=1` and require `OK`.
3. Capture the asynchronous `+CGNSSPWR: READY!` message when powering on.
4. Poll `AT+CGPSINFO` about every 10 seconds over the same connection.
5. Empty location fields mean no fix. Use the production parser described in
   [GPS protocol](gps_protocol.md) for coordinate validation; `OK` or GNSS READY
   alone does not mean a position was acquired.

Connect the GNSS antenna to the GNSS-labelled connector and give it clear sky
view. The earlier owner-reported location was indoors/by a window. No outdoor
position fix or accuracy qualification has been established. Do not publish raw
coordinates, device identifiers, NVS contents or full flash backups in this
public repository. Closing the host session does not intentionally power GNSS
off; power state may persist until another reset or power cycle.

See the [dated bench report](hardware-results/2026-10-08-reconnect.md) for results.

## References

- [LILYGO board guide: power, connectors, LEDs and GNSS antenna](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00/docs/en/esp32/a7670-esp32/README.MD)
- [Installed ATdebug source: startup reset and PWRKEY sequence](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00/examples/ATdebug/ATdebug.ino)
- [Espressif: automatic reset and serial control lines](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html)
- [pySerial: opening ports and control-line behavior](https://pyserial.readthedocs.io/en/latest/pyserial_api.html)

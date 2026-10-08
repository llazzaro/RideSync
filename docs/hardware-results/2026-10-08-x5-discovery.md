# X5 BLE discovery — October 8, 2026

The owner reported an X5 available in Bluetooth remote pairing mode. The Mac
used Bleak 3.0.2 with CoreBluetooth for a bounded discovery and connection.
One Insta360/X5-named advertisement was observed with RSSI -45 dBm and the
BE80 service. Name and owner association identify a bench candidate; firmware,
identity and protocol semantics have not yet been independently confirmed.
Exact names, host-local peripheral UUIDs, manufacturer data and raw captures
remain private. A macOS peripheral UUID is not a BLE MAC address.

## Observed service discovery

A connection and service discovery succeeded. CoreBluetooth reported ATT MTU
515; this is a host/backend observation, not proof of ESP32 negotiated MTU
or a validated application payload limit. The exposed service shape was:

| UUID (Bluetooth base `0000xxxx-0000-1000-8000-00805f9b34fb`) | Properties | Descriptors |
|---|---|---|
| BE80 | Service | — |
| BE81 | Read, Write | None reported |
| BE82 | Notify | One reported |
| BE83 | Read | None reported |

The discovery connection performed no characteristic reads, writes or
notification subscriptions and disconnected afterward. This is actual
central-to-camera BE80 discovery evidence, separate from the CE80 remote
peripheral path. It does not prove pairing/security requirements, explicit
record/stop encoding, readiness/state interpretation or an ESP32 adapter.

A subsequent notification-only connection subscribed to BE82, without vendor
command writes. It captured 77 notifications over approximately 77.561 seconds;
every payload was seven bytes and all payloads were identical. The connection
unsubscribed and disconnected normally. The owner reported that the camera did
not show a paired remote; no manual recording/stopped-state annotation was
obtained. The messages are therefore unclassified, not recording/readiness
observations or a proven handshake. Raw payloads remain private pending field
classification and anonymization. No shutter command has been sent by RideSync.
Firmware remains unrecorded. Issues #3/#17/#19 stay open.

## Reproduction and limits

Close competing camera apps, keep the owner-selected camera available, perform
a bounded Bleak scan, then connect to the observed candidate and enumerate
services/characteristics/properties/descriptors. Preserve private identifiers
only in excluded local captures. Do not select a camera solely by RSSI,
interpret an accepted write as recording, or reuse results for another model.
No packet implementation from unlicensed research sources was used.

Bleak documents CoreBluetooth's host-local UUIDs, authorization behavior and
notification limitations in its [macOS backend documentation](https://bleak.readthedocs.io/en/latest/backends/macos.html).
The host diagnostic does not change the ESP32 firmware, SD card or modem.

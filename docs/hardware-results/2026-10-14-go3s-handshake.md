# GO 3S ESP32 discovery and handshake — October 14, 2026

## Scope and result

The owner requested testing the powered GO 3S without an Action Pod, using the
mobile app for possible control approval. The owner reported updating the camera
that day; the installed firmware number was not read. The owner explicitly
requested proceeding without further version checks.

**Observed on the ESP32:** a unique GO 3S advertising-name candidate, successful
BLE connection, BE80 service, BE81 with properties 14 (Read/Write/Write Without
Response), and BE82 with property 16 (Notify). Negotiated MTU was 128. One
subscription session received a complete camera-to-controller FFFrame SYNC with
valid CRC16/MODBUS. These are discovery/handshake observations, not qualified
recording support.

**Not established:** authorization, REC/STOP, playable footage, production
security/bond commissioning, recording-state decoding or compatibility acceptance.
No REC or STOP serial command was sent. Physical recording checks in #46/#22
remain pending.

## Finite observations

1. The initial discovery probe connected and discovered the endpoints. The link
   remained connected for roughly one minute before explicit disconnect.
2. Subsequent connection attempts ended with NimBLE reason 531 before the
   expected connection callback; the probe timed out and required reset. The
   cause was not isolated. No control command was replayed.
3. After the owner power-cycled the camera, connection and discovery passed.
   Subscription received a 17-byte FFFrame SYNC. The probe sent the source-backed
   seven-zero sync response and one CheckAuth request with sequence 1 and an
   explicit diagnostic controller ID.
4. A 24-byte camera-to-controller message had correct outer length and CRC, but
   its header did not satisfy the existing qualified response decoder. Its code
   at the response/status offset was 0x205f and its low sequence byte was 255,
   not the requested sequence 1. It is retained privately as an unidentified
   message, not interpreted as an authorization ACK or a rejection. The first
   control diagnostic stopped on that packet and disconnected.
5. A revised diagnostic retained unidentified packets without treating them as
   ACKs, allowing the bounded wait to continue. A new connection/discovery passed,
   but no BE82 SYNC arrived within two seconds or within one second after the
   single zero-byte prompt. No CheckAuth or recording command was sent in this
   attempt. The phone/app state differed and was not isolated as the cause.

The owner explained that the app was open to approve control. The exact app
screen and approval route remain unresolved. App remote-control pairing should
not be assumed equivalent to the direct camera BE80 central route tested here.
No RequestAuthorization (0x56) trial was performed.

## Probe and preservation evidence

Testing used the fitted ESP32-WROVER-E board over its USB-C serial port and one
continuous PassiveSerial owner per capture. The temporary probe was prepared
privately from the existing audited HERO12 central startup and NVS refusal
wrappers, using the repository's GO 3S codec. It is an isolated protocol diagnostic,
not an activation of the production GO 3S runtime or a commissioned production
configuration. Production code and its security requirements were not changed.
The observed direct GATT connection was unencrypted and unbonded; the probe did not request SMP bonding
or bond deletion. Camera-side authorization persistence was not verified. Unknown recording state was
retained throughout.

Repository baseline: `f3e89a7e4cd7b0edbb7d4b926019948956bebc6d`.
Final temporary capture-probe firmware SHA256: `71e5baaf24740721fbea6c9a0619b2c10fff94230ff3e04539e25ce1e1fe1e0b`.

Pinned PlatformIO builds and ELF audits passed for the temporary images: strong
BLE HAL anchor, RAM-only store, empty persistence object, and NVS/PHY refusal
wrappers. Before each upload a retained/reconstructed complete 4 MiB image
verified against the device. Partition-table comparison passed. Each upload used
the existing four standard regions; independent region verification passed.
NVS at 0x9000, length 0x5000, matched the original baseline after uploads and
again after the final session. No full erase was issued. Full flash/NVS,
transcripts and temporary probe source remain private outside Git. The final
GO 3S probe is installed and was reset to idle after the preservation readback;
it performs no automatic connect or recording command.

## References and next bounded test

- [Existing GO 3S profile](../go3s_profile.md) and
  [finite hardware procedure](../testing.md#go-3s-source-backed-profile-verification-21).
- [Pinned community GO 3S protocol](https://github.com/OpenGraphLabs/syncfield-python/blob/88a74d84e09602d73c0e8a77f0fe583beb33cb61/src/syncfield/adapters/insta360_go3s/ble/protocol.py).
- [Pinned community direct-camera example](https://github.com/nicecx/insta360-ai-content-studio/blob/abf472566198aeefb493b742f3fe69d9132ff460/lib/camera_remote.py).
- [Upstream protocol documentation](https://github.com/xaionaro-go/insta360ctl/blob/main/doc/ble_protocol.md)
  was consulted for authorization context; that mutable document is not a new
  qualified implementation profile or proof of the unidentified packet's meaning.
- [Retained flashing and backup procedure](../device_connection.md#reusable-backed-up-probe-flashing-procedure).

Resolve which app control-approval screen is in use before selecting another
protocol route. For a further BE80 trial, capture a fresh complete handshake
without mistaking unsolicited messages for ACKs. Recording remains gated on a
valid correlated authorization response; then perform independently requested
REC/STOP with owner-observed behavior and playback. This report passes none of
those pending checks.

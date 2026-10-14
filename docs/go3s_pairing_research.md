# GO 3S pairing research — October 14, 2026

The owner requested other GitHub implementations after the
[local ESP32 trials](hardware-results/2026-10-14-go3s-handshake.md). The camera
can remain off during this source review. No new hardware claim or production
profile change follows from these findings.

## Strongest new leads

### OpenGraphLabs/syncfield-kotlin

Pinned revision: `61006a1819af844df423748b057f268d0020b983`.

The [README](https://github.com/OpenGraphLabs/syncfield-kotlin/blob/61006a1819af844df423748b057f268d0020b983/README.md)
reports Android GO 3S dual-camera recording and collection validation. This is
author-reported evidence, not independent local reproduction. Its full control
path uses Insta360 OneSDK; it is not ESP32 firmware.

The [packet constants](https://github.com/OpenGraphLabs/syncfield-kotlin/blob/61006a1819af844df423748b057f268d0020b983/syncfield-insta360/src/main/kotlin/io/opengraph/syncfield/insta360/Insta360ProtocolPackets.kt)
and [raw BLE session](https://github.com/OpenGraphLabs/syncfield-kotlin/blob/61006a1819af844df423748b057f268d0020b983/syncfield-insta360/src/main/kotlin/io/opengraph/syncfield/insta360/Insta360BleProtocolSession.kt)
provide a specific alternative: negotiate MTU 256, subscribe on BE80, then write
ten alternating AB/BA bytes immediately and await camera FFFrame SYNC for up to
five seconds. Our tested diagnostic instead waited for SYNC and used a bounded
zero-byte prompt. This difference is a candidate for an isolated next test,
not a proven cause of the intermittent SYNC or disconnect failures.

The raw session explicitly leaves first-pair wake authorization to the SDK's
native implementation. It documents the `ff0d03` reply prefix but does not
supply the complete outbound authorization encoder. Do not invent that packet
or treat its prefix alone as successful pairing.

### OpenGraphLabs/syncfield-swift

Pinned revision: `40deed1806f5e2b4cf4a4013c0ec6eed679197f3`.

Its [phone-authorization investigation](https://github.com/OpenGraphLabs/syncfield-swift/blob/40deed1806f5e2b4cf4a4013c0ec6eed679197f3/docs/insta360-phone-authorization.md)
reports cameras working on one phone but failing for other users until device
approval was established. It distinguishes connection/wake authorization from
phone authorization and describes one active phone at a time.

The [controller](https://github.com/OpenGraphLabs/syncfield-swift/blob/40deed1806f5e2b4cf4a4013c0ec6eed679197f3/Sources/SyncFieldInsta360/Insta360BLEController.swift)
uses the official SDK authorization API with initiator type 1 and a controller
ID. An initial Unauthorized result can mean the camera is awaiting the user's
decision; the implementation waits for a separate final result notification.
This makes asynchronous approval a relevant investigation target. It does not
provide a complete raw ESP32 equivalent or prove approval through the app's
GPS Remote search UI.

### dstrat28/action-multicam-remote

Pinned revision: `b89cd0022e5a50b357a7fdbab154b9a5ff037e21`.

The [README](https://github.com/dstrat28/action-multicam-remote/blob/b89cd0022e5a50b357a7fdbab154b9a5ff037e21/README.md)
implements CE80 GPS Remote emulation, but explicitly marks GO 3S hardware as
untested. Its discovery menu instructions and generic compatibility list are
not evidence that it solves the local GO 3S app-search failure.

## Decision and limits

No turnkey, hardware-verified ESP32 GO 3S first-pair solution was established by
this review. The strongest next direction is direct BE80 connection with the
newly evidenced proactive SYNC sequence, followed by separately investigating
controller authorization and the final user-approval exchange. Keep the two
roles distinct: a camera-hosted BE80 controller will not appear as a CE80 GPS
Remote in the app's accessory search. Preserve unknown recording state and
existing bonds; do not silently loosen production security checks or borrow
other camera models' status fields. Prepare an audited idle diagnostic before
asking the owner to power on the camera again.

## Later source and local evidence reconciliation

Pinned [authorization.proto](https://github.com/xaionaro-go/insta360ctl/blob/f94193ce03c5af0921a9992bfd1af6bd946150d0/pkg/protocol/protobuf/authorization.proto)
at `f94193ce03c5af0921a9992bfd1af6bd946150d0` provides a stronger concrete schema
than the earlier prose description: initiator_type is field 3; field 2 is an
optional Find My string. Its response enum maps 1 to Authorized, 2 to Not
Authorized, 3 to Pending, 4 to Denied and 5 to Expired. Do not use the conflicting
enum numbering in the earlier mutable prose document.

The [later local run](hardware-results/2026-10-14-go3s-handshake.md#proactive-direct-sync-and-authorization-check)
observed a matching status-200 CheckAuth reply with field 1 value 1 after
proactive SYNC. This supports authorization for that isolated run, while
recording and the production security/commissioning path remain unverified.
The first proactive diagnostic still used the older field-2 tag; its subsequent
idle receive-queue overflow was an instrumentation defect. A new private probe
corrects the tag and continuous receive handling before further camera testing.
These wire facts were implemented independently; no upstream generated code or
proprietary SDK binary was copied into RideSync.


## Follow-up: powered-off wake protocol

After both the targeted three-second remote beacon and CONNECT/SYNC-only
experiments failed to power on GO 3S, the owner confirmed successful wake from
the official app. A fresh GitHub source review retained the following revisions
privately; unchanged upstream revisions were reused rather than treated as new
independent evidence.

- **insta360ctl** at `f94193ce03c5af0921a9992bfd1af6bd946150d0` has a
  [special wake authorization path](https://github.com/xaionaro-go/insta360ctl/blob/f94193ce03c5af0921a9992bfd1af6bd946150d0/doc/ble_protocol.md#54-wake-up-authorization-special-path).
  It describes a simplified frame: FF 0C 02, uint16 little-endian authorization
  data length, authorization-ID string bytes, then CRC16/MODBUS in little-endian
  order. It says this bypasses normal SYNC/protobuf authorization in sleep.
  Its [encoder](https://github.com/xaionaro-go/insta360ctl/blob/f94193ce03c5af0921a9992bfd1af6bd946150d0/pkg/protocol/go2blepacket.go)
  explicitly associates EncodeGo2BlePacket2(0x0C, 0x02, auth_id_bytes) with
  sendWakeUpAuthorization. These are reverse-engineered source facts, not a
  successful local GO 3S wake capture. The earlier enum discrepancy in that
  repository's prose remains a reason to verify claims against code and hardware.
- **syncfield-kotlin** at `61006a1819af844df423748b057f268d0020b983`
  [packet constants](https://github.com/OpenGraphLabs/syncfield-kotlin/blob/61006a1819af844df423748b057f268d0020b983/syncfield-insta360/src/main/kotlin/io/opengraph/syncfield/insta360/Insta360ProtocolPackets.kt)
  identify a FF 0D 03 wake-auth reply with result at byte 5 (1 success, 0 failure).
  Its raw BLE session delegates outbound wake authorization to native SDK code,
  and comments place that exchange after SYNC; this sequencing difference from
  insta360ctl's sleep-path description is unresolved.
- **syncfield-swift** at `40deed1806f5e2b4cf4a4013c0ec6eed679197f3`
  calls writeWakeupAuthDataToCamera separately from phone authorization. Its
  wakeUpSpecificCamera path remains an opaque SDK operation; six-character
  serial encoding does not expose the advertisement payload.
- **nicecx/insta360-ai-content-studio** at
  `abf472566198aeefb493b742f3fe69d9132ff460` has an author-reported GO 3S
  [recording module](https://github.com/nicecx/insta360-ai-content-studio/blob/abf472566198aeefb493b742f3fe69d9132ff460/lib/camera_remote.py).
  Its Go3SCamera class explicitly targets an already powered-on camera and
  implements no wake path. It does not solve the observed power-off failure.

The concrete next candidate is a separately gated ESP32 wake-auth write using
the retained controller authorization ID, with CRC/length checks and a finite
wait for the documented response, then disconnect and owner power-on observation.
It must not send recording commands or infer power-on from a GATT connection or
write acknowledgement. A private isolated encoder/probe was subsequently prepared for the owner-requested
experiment; see the dated hardware report for transmission and observations.
Neither power-off wake nor production GO 3S wake is qualified by source review.


## Follow-up after the repeated wake-auth rejection

The owner requested further GitHub investigation after the CRC-valid result-0
wake reply and observed failure. Code searches for the exact SDK wake methods
found the previously inspected SyncField SDK wrappers and insta360ctl format;
no additional raw GO 3S provisioning implementation was established. GitHub's
code-search API subsequently returned its rate-limit error, so further exact
code search was unavailable in this review. Directly inspected repositories
and schema files supplied the following additional evidence.

**RigacciOrg/insta360-wifi-api**, pinned at
`89320e06a2395140e9e506a735c59a7ff8d88446`, contains generated
[Options descriptors](https://github.com/RigacciOrg/insta360-wifi-api/blob/89320e06a2395140e9e506a735c59a7ff8d88446/pb2/options_pb2.py):
AUTHORIZATION_ID is option type 38, Options.authorization_id is string field 38,
and BT_WAKEUP_SW / Options.bt_wakeup_sw use 97. The wake-switch enum is off 0,
on 1, unknown 2. This provides a specific readback candidate rather than another
blind wake retry. It does not establish that GO 3S exposes these options, that
field 38 is the SDK's sleep-wake credential, or that writing it registers wake
permission. The repository targets ONE RS WiFi and lists AUTHORIZATION_ID among
options not returned by that camera; source/schema existence is not readback
qualification. No generated upstream implementation was copied into RideSync.

The pinned **syncfield-swift** method configureWakeOnBluetoothIfPossible calls
writeWakeupAuthDataToCamera separately from phone authorization. Its comment
requires explicit foreground wake provisioning and warns against racing ordinary
setOptions/authorization with GO 3S connection preflight. The actual SDK-generated
wake-data bytes are not implemented by the wrapper. These facts support a
missing-provisioning hypothesis, but do not prove the root cause of result 0.

Additional repositories were scoped directly: **arsfabula/Insta360-Remote-CIQ**
at `39c51b3aa7c453227831d811355899371bbb8b94` implements direct BE80 control in
BLEBarrel.mc without a sleep-wake credential registration path;
**Daiki-Iijima/insta360-go-ultra-sdk** at
`f5fa853b153b4483c4494ca087c7cd5b551f3aca` implements powered-on WiFi control,
not GO 3S BLE wake provisioning. Official Android SDK documentation exposes
bleWakeUp independently from checkAuthorization but does not document raw
provisioning bytes.

Next evidence needed: while powered on and explicitly authorized, read only
AUTHORIZATION_ID and BT_WAKEUP_SW through the isolated ESP32 path, retaining
responses privately and comparing any returned identifier without publishing
it. Missing/denied fields remain inconclusive. Do not overwrite the app's working
identity or claim that SetOptions field 38 is a fix without verifying its role.
If that readback does not establish the registration route, capture or inspect
the SDK's writeWakeupAuthDataToCamera transaction before implementing a new wake
attempt. No confirmed ESP32 GO 3S wake fix was found in this expanded review;
the camera and ESP32 remained idle throughout research.

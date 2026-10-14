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

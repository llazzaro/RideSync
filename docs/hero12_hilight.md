# Optional HERO12 HiLight dispatch (#64)

RideSync's local bookmark remains the source of truth. Native HiLight is an
explicitly enabled, experimental addition for a qualified HERO12 Black peer.
The session owner forwards only a `BookmarkReceipt::Admitted` key
`(session_id, bookmark_id)`; failed local admission cannot send a camera command.
`Hero12Qualification::hilight_enabled` defaults false. Mixed-camera dispatch
returns Unsupported for every other model, without borrowing that model's
Start/Stop command path. Camera loss, Busy, refusal and timeout leave the local
bookmark intact.

Commissioning must opt in at both gates: the private provider supplies
`cameras.hero12[slot].hilight_enabled = true` for the qualified peer, and the
session uses `telemetry.runtime.bookmark_enabled = true` together with
`telemetry.runtime.native_bookmark_enabled = true`. The global session flag does
not silently enable a camera profile. These flags should be set only alongside
the existing HERO12 identity, firmware/API and BLE qualification evidence.

## Pinned wire contract and evidence

The [official Open GoPro Hilight Moment documentation](https://gopro.github.io/OpenGoPro/docs/ble/hilights/)
says the command is valid only while encoding. At official OpenGoPro commit
`0f963572611c4410a15678531e9681a6ff874edb`,
[`HilightMoment.kt`](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/kotlin/kmp_sdk/wsdk/src/commonMain/kotlin/com/gopro/open_gopro/operation/commands/HilightMoment.kt)
uses `executeTlvCommand` with dedicated `HILGIHT_MOMENT`, and the pinned
[`CommandId`](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/kotlin/kmp_sdk/wsdk/src/commonMain/kotlin/com/gopro/open_gopro/entity/communicator/BleCommunicator.kt)
assigns it `0x18`. The [SDK transmitter](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/kotlin/kmp_sdk/wsdk/src/commonMain/kotlin/com/gopro/open_gopro/domain/communicator/BleCommunicator.kt)
uses the Command characteristic, no arguments, and Command response with the
same ID and a zero success result. RideSync's extended-13 request is
`20 01 18`; compact alternate `01 18`. A successful framed response is
`02 18 00`; nonzero result is camera refusal. These fixture bytes are
**synthetic/schema-derived**, not device captures or official raw goldens.

The adapter sends a fresh Encoding query (`0x13`, status `0x0A`) for each
request. A successful, current query with Encoding true is needed before
`0x18`. It also requires a ready qualified link, idle command lane, an attached
camera audit sink and the explicit HERO12 opt-in. One marker may be pending per
peer. The local key, connection generation and a separate marker generation
identify the audit transaction. The key is never transmitted to the camera.

Freshness refers to owner receipt and expected response fields. Classic status
queries have no transaction nonce, so an identically shaped late status reply
cannot establish stronger causal or camera-acquisition timing proof.

The marker command produces an **Attempt** row only after BLE write admission.
A matching successful `0x18` Command response during the live transaction and
after ATT completion produces a **WireAck** row. Refused, Failed and Cancelled
are separate rows. Timeout has no automatic retry or reconnect replay. After an
unanswered transmitted marker, the adapter refuses another marker on that
connection because the camera may already have applied the first command.
STOP cancels pending marker work and proceeds through the control owner; reset,
shutdown and link retirement cancel it. Keepalive remains ahead of marker
traffic when due. If that keepalive delays an already checked marker, the owner
rechecks Encoding and observed Busy before resuming it; a stopped or Busy update
refuses the marker without retiring the control link. A bounded marker deadline
prevents waiting on an ambiguous
camera response indefinitely.

**WireAck is not marker confirmation.** GoPro Active Hilights and Time Since
Last Hilight statuses contain no RideSync bookmark key, so a status change is
an uncorrelated camera observation. Only saved-clip inspection can confirm the
tag in media. Hilight Moment marks the camera's current encoding moment, so any
dispatch delay cannot tag the earlier local input time. The protocol has no
transaction nonce; a duplicate success arriving during a later live attempt on
the same connection cannot be proven to belong to one of those attempts from
the BLE response alone. The bounded audit records command evidence, not a
stronger causal claim.

The [bookmark investigation](highlight_bookmarks.md) and
[`docs/acceptance_policy.md`](acceptance_policy.md) retain the physical boundary.
Software fixtures and adapter tests are synthetic. Hardware confirmation needs
the new #46 HERO12 row: three requests in one recording with annotated wire
responses and saved-clip tags, one stopped refusal and one disconnect with no
replay. Other models remain Unsupported.

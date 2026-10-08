# Bounded central BLE transport (#35)

`BleCentral` is the serialized application owner; `Esp32BleHost::instance()` is
one boot-lifetime raw NimBLE host and bond-store owner. The production bring-up
main leaves BLE disabled. The separately retained `ble_central_compile` target
compiles and links all backend operations without calling them or activating BLE.
No GoPro/Insta360 profile, camera Ready/ACK, recording observation, keepalive,
wake, group policy, UI or pin selection is implemented here.

## Qualified composition

The caller provides a source-qualified board/SDK configuration, independently
verified identity addresses and a durable restoration qualification record. Start
the host before any configuration NVS owner exists; do not run SDK init/deinit,
whole-partition erase or BLE/store lifecycle concurrently with that owner. Boot
NVS guard admission and safe-mode policy must already pass. The actual calls are:

```cpp
// Keep host, sink, health publisher and this fixed owner alive for their lifetimes.
// Sink consumes bounded copied results in the owner context.
auto &host = ridesync::Esp32BleHost::instance();
// restored_proof comes from an independent retained commissioning record.
if (!host.configureRestore(restored_proof)) {
  // Surface refusal; do not create/overwrite restoration evidence here.
  return;
}
static ridesync::BleCentral central(host, profile_sink, &ble_health);
if (!central.begin(ble_enabled, source_qualified, monotonic_now)) {
  // host.state()/fault()/error() retain the startup refusal/failure.
  return;
}
// Once host.state() is Ready, supply a verified BondIdentity and bounded spec:
central.connect(peer_slot, captured_connection_generation, verified_identity, spec);
// Exactly one application context calls service, admissions and lifecycle APIs.
central.service(monotonic_now); // fault/retirement results first
// Profile drains its own results, then calls CameraManager/RecordingManager tick.
```

`BleProfileSpec` supplies up to three required service UUIDs and eight required
characteristics, each with service index, required properties and optional notify
or indicate subscription. This admits multiple services including future camera
management endpoints. The transport finds actual service/characteristic handles,
uses the next declaration or service end for descriptor ranges, requires 0x2902,
writes 01 00 or 02 00, and reads the exact two-byte CCCD value back. All required
subscriptions repeat on every new connection. Later security changes are checked
in every connected phase; encryption/bond/identity or required authentication loss
retires the link. A valid rekey does not restart discovery or an active GATT
phase. `TransportReady` means those ATT prerequisites completed; only a future profile can declare camera Ready.

Read/write requests use resolved endpoint indices, one outstanding GATT procedure
per peer and the actual `ble_att_mtu(handle)`. A write needs the acknowledged Write
property and a nonempty payload no larger than both 64 bytes and MTU-3; no silent
truncation or long-write fallback. `WriteComplete` is an ATT response, not camera
acknowledgement or evidence of recording. Notifications and reads are copied from
stack-owned mbufs; no pointer or mbuf ownership is retained.

Future peripheral services use raw GAP/GATT services on this same host. They must
share its capacity, store refusal and security admission policy. Never call
`NimBLEDevice::init`, create wrapper clients for raw links, initialize another
host/store, or use wrapper synchronous discovery/read/write/subscription APIs.
The raw host intentionally leaves wrapper initialization flags unset.

## Admission, callback ownership and cleanup

Four fixed peer slots, eight fixed peer callback contexts and one scan context are
owned by the fixed noncopyable/nonmovable central object. There is one global
initiating scan/connect procedure and one GATT procedure per peer. One due connect
is admitted per service pass with a rotating start peer; unrelated peers continue
servicing. No missed-pass catch-up burst, automatic reconnect, command retry or
recording intent replay exists. External generations are nonzero and strictly
increase on slot reuse; generation/procedure wrap is refused.

Each callback copies peer, generation, immutable initiating phase/procedure,
connection/attribute handles and at most 64 bytes. A fixed 32-record queue uses a
single bounded try-lock copy; contention and overflow seal the affected context
and latch a fault independently of queue space. The owner consumes all fault
latches before ordinary results and rechecks while draining. Public command
admission also checks those latches, so a delayed queue drain cannot authorize a
successor after overflow or malformed data. Callback code never invokes a camera
manager, waits for owner work, discovers attributes or accesses NVS.

Every pending phase has a rollover-safe absolute 5000 ms deadline; startup host
sync has an absolute 1000 ms owner deadline. Service must run more frequently than
2^31 ms. Expiry takes precedence over a queued completion at the deadline. Local
ambiguity, cancellation and stop seal the generation immediately and retire the
whole link. A global connect cancellation that loses to link establishment
terminates the actual captured link once. There is no general per-GATT cancel API
in this pinned host, and successful terminate submission never means cleanup
finished. Failed termination is reported via `error(peer)` and remains quarantined.

Pinned `ble_gap_conn_broken` calls `ble_gattc_connection_broken` before GAP
DISCONNECT. GATT terminal callbacks and the disconnect enqueue fixed host-queue
barriers. The pinned FreeRTOS NPL helper waits with `portMAX_DELAY`; the adapter
instead uses `xQueueSendToBack(..., 0)` on the exposed `ble_npl_eventq::q`, with the
same event-pointer and queued-flag representation. An independent atomic flag
prevents duplicate enqueue and synchronizes reuse after NPL clears `queued`
before executing the barrier. Full-queue refusal seals/latches a host fault and
leaves `quiet` false; it never manufactures cleanup. The alternate opaque
controller NPL representation fails compilation. This exact-pin ABI coupling
must be reviewed again when either dependency or framework changes. Every raw callback counts access from its entry through every early return,
including parsing and mbuf copying before result delivery. Cross-slot disconnect
cleanup and final router detachment share the bounded routing try-lock, and
detachment rechecks quiescence after acquiring it. A context remains allocated
until terminal cleanup, its barrier and zero
in-flight callback access are observed. The backend then explicitly detaches the
context from its boot-lifetime store router before the slot closes. Old copied
queue records contain values only and are rejected by generation/procedure.

`stop()` seals admission and starts asynchronous retirement. `stopped()` does not
mean callbacks are finished. Keep the owner and sink alive until `canDestroy()`;
destruction before that boundary aborts instead of freeing live callback storage.
A missing terminal event/barrier, host reset, failed controller cleanup or failed
startup permanently consumes bounded fixed storage for that lifetime. There is no
replacement context, worker, host initialization retry or forced task deletion.
Scan cleanup attempts cancellation at most once per scan generation. A nonzero
SDK result is retained by `scanCancelError()` and emitted once as
`ScanCancelFailed` with the exact `event.status`; it does not claim a normal
`ScanComplete` or permit context reuse. Terminal/barrier absence leaves that scan
quarantined without repeated SDK calls. A later real terminal can still complete
cleanup. The absolute scan deadline wins over a queued terminal completion at or
after the deadline; already accepted earlier completion is not retroactively
invalidated while its barrier drains.

Only a completed service pass publishes HealthProgress. A stopped owner publishes
`finished()` only after all its cleanup and routing detachment returns. The shared
host itself lives until CPU reset; its task is not destroyed or restarted.

## Bond-store and restoration evidence

The backend checks #33 `NvsBootStatus::persistenceAllowed()` before startup and at
store read/write/delete boundaries. It preserves the pinned stack config store;
it does not erase, reset or migrate bonds. The actual `onStoreStatus` override is
installed through `ble_hs_cfg.store_status_cb` before any pairing, always returns
nonzero for FULL/OVERFLOW and never delegates to oldest-peer eviction. Known peer
store refusal seals that peer; unrouteable store errors seal the host. Repeated
pairing is refused without deleting an existing bond. Passkey/numeric-comparison
requests are refused; the current explicit policy is Secure Connections bonding
with Just Works. Encryption is separate from authenticated MITM evidence. A
profile requiring authenticated security rejects an unauthenticated connection.

`BleStoreProof` has no qualifying defaults. Its nonzero `qualification_record`
references independent durable commissioning evidence. `inspectStore(false)` is
an explicitly invoked diagnostic operation before the host task exists; it emits
only SHA256/counts, no addresses or raw keys. Inspection does **not** set a record,
install a proof or authorize restoration. After commissioning and an independently
observed orderly power cycle, retain that snapshot outside this boot/store along
with board/SDK/build/pairing/physical qualification evidence. A later boot supplies
that previously retained proof through `configureRestore`. Never take the current
boot's snapshot and automatically accept it as its own restoration proof. A
missing namespace qualifies as empty only against an independently retained empty
proof; NVS errors, unknown schemas, oversized blobs or absent expected records
refuse startup. A lost proof requires operator requalification, not migration.

The digest binds a version (1,2,3,6), sorted NVS names, one-byte name length,
two-byte little-endian blob length and exact pinned-ABI blob bytes. Full readback
covers OUR_SEC, PEER_SEC, CCCD, CSFC, local IRK, RPA records and host privacy peer
records. Restored security/CCCD and other public store records must match their
persisted blobs and counts. The pinned host-privacy record getters are used only
for readback before the host task starts; the private record arrays are never
modified. Live `inspectStore` is refused to prevent concurrent NVS/store mutation.
The default initializer logs/ignores some restore failures; this independent
persisted-vs-stack comparison therefore gates startup instead of trusting init or
`isBonded` enumeration. The exact-layout digest requires requalification after
SDK/ABI changes; no automatic conversion exists.

Before security, actual store counts, matched valid LTK record identity when
present, restored readback, installed refusal and NVS admission feed #18
`admitBond`. Capacity is the actual compiled five-entry OUR/PEER tables; pressure
is the maximum of their observed counts. Existing verified identities may reuse
stored security at capacity; new identities are refused when either table is
full. FULL/OVERFLOW remains the authoritative refusal under concurrent pairing.
Connected encrypted peer identity must exactly match the supplied public/static
identity; unresolved/private configured addresses are rejected. Newly paired
records still need independent commissioning evidence before a later boot.

## Fixed resources and execution limits

- Application: four peers, three services/eight endpoints and at most 32 discovered
  characteristic declarations per peer; excess or duplicates retire the link.
  Queue is 32 records, each 132 bytes (4224 bytes); one command copy is 120 bytes.
  Native ARM64 `sizeof(BleCentral)` is 9024 bytes; the pinned Xtensa target is
  8920 bytes and each target callback context is 32 bytes. No application
  transport heap allocation or per-command FIFO is used; one caller request per ready peer is admitted.
- Host: configured five total connections and five bonds, with four central peers
  admitted and one connection reserved for a future raw peripheral role; 32 CCCD
  records. These macros do not prove live controller/bond capacity. The host owns
  fixed pools and SDK allocations; target measured heap/stack stress remains open.
- One checked SDK host task uses a 4096-byte stack, priority 5, CPU0. The application
  owner runs in the caller's serialized context, without another helper worker.
  The linked target backend singleton is 484 bytes, plus an eight-byte C++
  initialization guard. Nine backend slots have fixed NPL terminal barriers,
  enqueue admission flags and callback counters.
  Store inspection uses a fixed 80-name table, a 512-byte aligned scratch record
  and SHA256 context; caller startup stack margin must be measured.

Raw port initialization avoids `NimBLEDevice::init`'s indefinite host-sync loop:
controller init/enable, HCI init, `nimble_port_init`, stack store init and task
creation return their actual status; sync/reset callbacks feed asynchronous state.
Failed or expired startup retains a single irreversible host lease, seals
admission and cannot be revived by a late sync callback. There is no application
wait for sync or for teardown.

**SDK initialization/controller/HCI/NVS/store/host-lock latency is unmeasured.**
Asynchronous procedure APIs can acquire locks, allocate host resources and send
HCI commands; they are not a hard real-time execution guarantee. If any SDK call
blocks, no timer manufactures owner progress. Source qualification must fail if
those calls violate the existing health/TWDT envelope. The framework five-second
panic TWDT and CPU0 idle subscription are preserved without changes. The dedicated
compile image retains Arduino's `btInUse` linkage so boot does not release BLE
controller memory; this reservation does not activate BLE.

Physical gates #17/#18/#4/#22 remain OPEN: real four/mixed-link capacity, restored
identity/security/CCCD behavior and power-cut preservation, actual refusal at
capacity, callback/teardown and initialization/NVS latency, stack/heap margin,
coexistence and camera screen/recording evidence. No hardware was used here.

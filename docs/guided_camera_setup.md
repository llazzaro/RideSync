# Guided camera setup investigation (#56)

Decision dated October 13, 2026. This completes the investigation requested in
[#56](https://github.com/llazzaro/RideSync/issues/56); the interfaces below are
proposed, not implemented. Firmware and hardware support are unchanged.

## Decision and scope

Start with a **host-assisted USB serial wizard**, using the existing exclusive
serial owner and configuration storage task. First deliver preference editing,
validation, durable-save status and a prerequisite report for already commissioned
cameras. Then add bounded discovery and model-specific commissioning assistance.
Keep private qualification providers until a separately verified evidence owner
can replace their function. Automatic first-time commissioning for every model
is unresolved; a scan result cannot remove that boundary.

A parked-only Wi-Fi web interface is a later optional transport for the same
commands, after measuring resource costs and scheduling camera radio operations
with Wi-Fi stopped. Defer BLE setup: the pinned provisioning implementation uses
a different host API and cannot be attached to RideSync's exclusive NimBLE host
unchanged. No new issues or firmware implementation are required by this ticket.

The intended user is the person preparing an installed RideSync on the bench or
while parked. The smallest useful outcome is choosing a known camera, naming it,
saving valid preferences and seeing exactly why control is admitted or refused.
It must never convert user declarations into observed identity, bond restoration,
firmware compatibility, board pin qualification or commissioned SD resources.

## Current configuration and ownership audit

The following are inspected source contracts, not new hardware observations.

| Source | Current contract | Consequence for a wizard |
|---|---|---|
| [config.h](../include/config.h), [config.cpp](../src/config.cpp) | Registry up to eight; names 1–64 bytes; model/family agreement; MAC syntax, typed public/random address and case-insensitive duplicate-address rejection; DJI wake/GPS rejected. | Reuse validation; never infer model or stable identity from name, RSSI or address syntax. Add runtime-group validation before saving. |
| [config_storage.cpp](../src/config_storage.cpp), [config_storage_nvs.cpp](../src/config_storage_nvs.cpp) | Two guarded NVS records, generation, CRC, explicit commit and exact read-back; whole-candidate validation; no credentials or erase. | Reuse transaction and errors; schema need not change for existing preferences. CRC is corruption detection, not authentication. |
| [config_bootstrap.h](../include/config_bootstrap.h), [config_bootstrap.cpp](../src/config_bootstrap.cpp) | Fixed single-producer mailbox; saves require current epoch/generation; adoption only after verified persistence. Identity/count/model/wake changes permanently invalidate the session mapping. | One setup producer; expose request completion, not only mailbox acceptance. No live rebinding of an opaque peer ID. |
| [main.cpp](../src/main.cpp) | `settings_requests` has no production save producer; bounded startup barrier fixes boot policy. | New commands and response publication are needed; current console cannot edit settings. A late save cannot reopen startup admission. |
| [application_startup.cpp](../src/application_startup.cpp) | Weak `commissionedApplication()` returns null. | Settings do not construct qualified workers. |
| [bench_application.cpp](../src/bench_application.cpp), [bench guide](bench_test_version.md) | Private provider supplies reviewed bus routing, camera profiles and local resources; saved settings override private defaults only when valid. Missing/incomplete provider refuses. | Show provider and settings status independently. Do not expose provider flags as writable form fields. |
| [mixed_camera.cpp](../src/profiles/mixed_camera.cpp), [ble_remote.h](../include/ble_remote.h) | Four shared peer slots; X5 must occupy slot zero; typed commissioned identities must match each camera profile. | Setup-managed groups have count/capacity at most four, including disabled entries occupying slots. No fifth camera or second X5 can be silently admitted. Eight-entry legacy data stays preserved, with explicit unsupported-group status. |
| [ble_esp32.h](../include/ble_esp32.h), [pairing_reset.h](../include/pairing_reset.h), [pairing_reset.cpp](../src/pairing_reset.cpp) | One host/store owner, guarded restoration, overflow refusal; reset accepts verified stable public/random-static target, quiesces and verifies removal. | Setup cannot initialize a second host, evict old bonds, clear all bonds or target deletion using a scanned MAC. |
| [pairing_proof_esp32.cpp](../src/pairing_proof_esp32.cpp) | Separate observed store proof and retained revocation floor. | Changed store requires renewed independently reviewed proof; never mint an arbitrary digest/qualification record. |

**Preference edits allowed by the proposed service:** camera label, selected
supported model/family, enabled flag, typed identifier and existing wake/GPS
preferences within validators and the commissioned profile. Identity/model/wake
edits are drafts requiring renewed matching qualification before control. A GPS
checkbox only requests an already evidenced capability. Unsupported combinations
must fail explicitly. Button timing/actions can later reuse existing validators;
GPIO assignment/enable stays outside this minimal camera workflow.

**Still requires qualified commissioning:** stable peer identity and restore/store
proof, installed firmware/API/profile, X5 captured-display premises, DJI controller
identity/challenge and protocol security review, wake identifier provenance,
board routes/pulls/polarities, modem start state, IMU calibration/reference and
SD namespace/ledger/exclusive volume. These are separate resource/evidence
contracts. In particular, existing persisted `board_qualified` data cannot supply
the independent driver acknowledgement. No hardware admission flag is writable
through this interface.

## Transport research and alternatives

Primary sources inspected October 13, 2026:

- Espressif [provisioning overview](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/provisioning/index.html)
  concerns network provisioning, not camera qualification. The moving stable URL
  currently describes v6.1; it is not RideSync's dependency baseline.
- Pinned-generation [protocomm v4.4.7](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/provisioning/protocomm.html)
  offers console, HTTP and BLE transports with application endpoints. Security 1
  uses Curve25519/AES-CTR and optional proof of possession; Security 0 is unsecured.
  This supplies a candidate session layer, not camera authentication.
- [Wi-Fi provisioning v4.4.7](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/provisioning/wifi_provisioning.html)
  supports custom endpoints and BLE-memory release handlers. Its Wi-Fi credential
  lifecycle and automatic stop behavior do not match this setup workflow; do not
  copy erase/reprovision or Bluetooth-memory-release behavior into the camera host.
- The pinned [BLE transport source](https://github.com/espressif/esp-idf/blob/v4.4.7/components/protocomm/src/transports/protocomm_ble.c)
  uses `esp_gatts`/`esp_gap_ble` and `simple_ble`, unlike RideSync's raw NimBLE
  integration. Reuse would require a reviewed transport port or stack change.
- Espressif's pinned [local control example](https://github.com/espressif/esp-idf/blob/v4.4.7/examples/protocols/esp_local_ctrl/README.md)
  demonstrates HTTPS properties, mDNS and a Python client with certificate trust.
  It is a protocol/security reference, not a browser wizard or permission to reuse
  example private keys/certificates.
- Espressif [ESP32 RF coexistence v5.0](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32/api-guides/coexist.html)
  documents one shared RF path and time division; connected SoftAP/BLE operation
  is listed with unstable performance. This is documentary RF guidance from a
  later IDF, not measured performance or configuration proof for our v4.4.7 build.
- Espressif [NVS v4.4.7](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/storage/nvs_flash.html)
  documents commit and power-interruption recovery. SDK behavior does not certify
  RideSync's installed power-cut boundaries; preserve the existing guarded owner.

[platformio.ini](../platformio.ini) pins Arduino 2.0.17 packaging and NimBLE commit
`dfb4ac561a06797081be9e752902a6582e7f029e`; the installed pinned SDK header reports
IDF 4.4.7. It sets five SDK connections/bonds, while the application central has
four peer slots. These numbers neither reserve a usable phone link nor prove
four-camera physical coexistence.

| Alternative | Dependencies and memory | Radio/host consequence | Recommendation |
|---|---|---|---|
| Host serial | Python serial client (pin dependency when implemented), bounded device parser/session and response mailbox. No new Wi-Fi/TLS stack. Existing 12 KiB configuration task and 6992-byte persistence owner are reused, not free extra RAM. | Setup discovery still leases the shared BLE host; serial itself adds no RF traffic. One USB reader/writer owns console. | First stage; lowest integration cost, inferred from dependency scope. |
| Parked Wi-Fi web | SoftAP, HTTP server, bounded local assets, authentication, browser origin/CSRF protections; HTTPS adds TLS allocation and certificate trust. Wi-Fi buffers, tasks, sockets and TLS heap must be measured. | Shares RF with cameras. Pause Wi-Fi during discovery/pairing; browser reconnects afterward and reads retained status. No background AP while riding. | Optional second transport after serial semantics pass. |
| BLE setup | Phone client, custom GATT service, authenticated session, ATT fragmentation, extra buffers and connection; pinned protocomm BLE is not a drop-in NimBLE service. | Must extend the one immutable peripheral registration before host start; X5 CE80 already uses that role. Host reinit/deinit or freeing Bluetooth memory is unacceptable. | Defer until one-host service composition and resource feasibility are demonstrated. |

No numeric incremental heap/flash cost or coexistence latency is claimed: no
setup firmware was built. A future transport trial records linked flash/static
RAM delta, minimum internal free heap, largest free block, per-task stack
high-water marks, allocation failures and teardown leaks at its declared maximum
load. PSRAM presence does not establish sufficient internal radio/TLS memory.
Keep optional transports disabled if that trial fails; serial remains useful.

## Discovery, identity and per-model approval

Discovery returns bounded **candidates**, never trusted cameras. Use the existing
central scan owner for central-role candidates: propose a 10-second scan, at most
16 copied entries and an explicit truncation flag. Show address type, advertised
name/services and RSSI privately. Select one candidate at a time; repeated
advertisements merge only by typed observed address. Names and services suggest
profiles but never auto-select/enable one. Cancellation waits for actual callback
release before another operation, not merely the deadline.

Canonicalize textual MAC case for validation, then compare stable verified
identities across slots as well as observed addresses. The existing validator
rejects duplicate address strings even across address types; preserve that
conservative rule. For private/random advertisements, obtain authenticated stack
identity resolution before saving an enabled peer. `AddressType::Random` syntax
does not distinguish random-static from resolvable/non-resolvable private.
Reject unresolved rotating addresses for control/reset; do not hash them into a
persistent ID or save an IRK in settings. An unresolved candidate can remain an
unsaved draft. Explicitly refuse capacity overflow before connect or new bond;
restored bond-store capacity and camera-slot capacity are separate checks.

| Model | Discovery/approval path | Completion and limits |
|---|---|---|
| X5 | RideSync is the CE80 peripheral; camera initiates pairing from its remote UI. A BE80 advertisement does not identify the production CE80 route. Assist existing [private identity/store commissioning](x5_serial_milestone.md#private-commissioning), then explicitly CONNECT/advertise for the expected identity. | Retain independently reviewed stable identity, guarded store receipt/revocation record, installed firmware/profile and actual CE82 subscription. Probe observations on firmware 1.11.10 are not automatic composed qualification. First-time bond creation remains unresolved by the present production path; no automatic security handshake/eviction is admitted. |
| GO 3S | Camera-hosted BE80/BE81/BE82 FFFrame; camera rather than Pod is the selected target. Follow [GO 3S profile](go3s_profile.md), with approved camera pairing and commissioned encrypted/bonded identity; record camera/Pod firmware separately. | Subscription, MTU, SYNC and successful CheckAuth are separate steps. Community firmware 8.0.4.11 is a source reference, not an installed-firmware observation. State remains Unknown; no guessed UI/pairing bytes or Pod proxy. |
| ONE RS | Camera-hosted BE80 explicit command candidate; camera-side remote selection is documented by [Insta360](https://onlinemanual.insta360.com/oners/en-us/camera/connect/gpsremote). Follow [ONE RS profile](one_rs_profile.md), retaining verified identity/bond. | No evidenced universal third-party authorization handshake/minimum firmware. Show Experimental / Not tested; missing approval/security evidence refuses control. ATT delivery never supplies recording state. |
| HERO12 Black | Central discovers FEA6 and GP-0090 management endpoints; use [HERO12 setup contract](gopro_plan.md) for camera-approved pairing/bonding, management pairing-finish, external-control claim and Hardware Info/API queries. | Compare returned model, exact commissioned firmware/API and verified identity. Documentary minimum v01.10.00 alone cannot qualify. Bond alone does not clear the pairing screen or make control ready. |
| DJI Action 4/5 Pro/6, Osmo 360 | Central FFF0/FFF4/FFF5, explicit controller identity and declared challenge; use official pinned [connect logic](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/92fe23e5a749f189593f980a26a105c3bb66aa1c/logic/connect_logic.c) and [local profile](dji_camera_profile.md). New pair requires camera-screen grant; valid prior pairing is a separate declared state. | Correlated ACK/grant must succeed; denial/timeout leaves unpaired. Route uses application authentication without SMP/encryption guarantee; do not display Bonded or general encrypted-channel claims. Installed firmware must be recorded; no universal minimum is established. |

For each model, unknown firmware or missing profile prerequisites reports
`Needs commissioning`; explicitly incompatible returned model/API/firmware reports
`Unsupported profile`. User model declaration never overrides returned identity
checks. No recording command is used to identify a camera. Pairing failure leaves
settings unchanged and never triggers factory reset, bond eviction or retry loops.
If pairing changes the stack store, require fresh reviewed store evidence before
normal startup; persisting a preference is not atomic with camera-side pairing.

## Smallest workflow and entry policy

All following commands/states are design names, not current console commands.

1. **Enter setup:** physically attach USB and boot into an explicit setup-only
   mode while declaring parked operation. Until a qualified dedicated input exists,
   use a deliberate serial entry challenge during a 30-second boot window; the
   device acknowledges setup isolation before accepting mutations. USB attachment
   alone is insufficient. One session/owner only; ride control/wake/recovery ingress
   stays closed. Never silently disconnect a recording camera or send STOP.
2. **Inspect:** read saved preferences, storage result and independent provider/
   qualification status. Invalid/future/ambiguous/read-failed data remains intact;
   stop edits and report explicit recovery required. Missing records may use RAM
   defaults, with no automatic save.
3. **Discover/select:** bounded model-aware scan or X5 camera-initiated connection
   preparation. Select a candidate or existing commissioned slot; reject a fifth
   entry and arrange X5 at slot zero before saving, showing any reorder explicitly.
4. **Name/declare model:** edit a local draft (one complete candidate), escape
   output and reject NUL/control characters. Preserve the existing 64-byte name
   bound, count UTF-8 bytes rather than characters; reject partial/unknown fields.
5. **Validate:** run existing settings validators, runtime-group bounds and
   qualification matching. Preview the whole change and show Saved / Unsaved,
   identity verification, pairing/restore, profile/firmware, enabled/requested
   capabilities and each missing prerequisite separately. Qualification failures
   may be saved as valid disabled preferences, with explicit refusal of control.
6. **Save:** explicit user action submits one current-epoch/generation candidate.
   Show Pending until durable verification; stale/full/busy/error is terminal for
   this request, with no UI auto-resubmission. Show restart required even for
   label-only edits in the first release, simplifying runtime policy.
7. **Exit:** discard unsent drafts, revoke setup session, retire scan/pairing and
   wait for owner release. Never restore old recording intent. Offer a deliberate
   restart only after durable outcome and all owners/storage are quiescent; boot
   performs qualification again and sends no REC.

Proposed fixed limits: 120 seconds inactivity, 10 minutes total per session,
three failed authentication attempts then closed until new physical entry. Deadlines
use monotonic time; trickle input cannot extend the absolute limit. Timeout closes
new request admission and drops unsent drafts; an already admitted storage commit
may finish and must be reported on next inspection. It cannot be claimed cancelled
by a disconnected client. Pending pairing/store changes retain explicit uncertain
status and require inspection, never automatic deletion.

Serial trusts physical possession of the USB port plus deliberate entry; it does
not protect against a malicious process on that host. No unattended serial setup
or remote bridge is authorized by this design. GNSS NoFix is not proof of parked
operation; no qualified motion/ignition interlock exists. First-stage parked-only
is an explicit operator procedure plus setup isolation, not an automatic vehicle
safety claim. If reliable motion evidence later indicates movement, revoke setup;
missing evidence never enables an always-on network interface.

Later Wi-Fi requires unique per-session randomly generated AP credentials and
an authenticated application session available only through physical entry;
no universal/default password, LAN auto-start or cloud requirement. Use exact
origin/Host validation, CSRF tokens, one client, rate limits and no mutating GETs.
Choose and verify HTTPS certificate trust or reviewed authenticated protocomm
before accepting credentials over HTTP; AP association alone is not sufficient
application authorization. BLE likewise needs application proof of possession,
not name/RSSI or unauthenticated GATT writes. These are future security gates.

Never persist setup passwords/tokens, IRKs/LTKs, camera keys or DJI challenge in
`SourceConfig`. Tokens expire on exit/reset; secret entry has no serial echo,
command-line argument or transcript logging. Host exports are opt-in private
files with restrictive permissions; default logs contain counts, outcome and
session/request IDs only. Addresses, advertised names, firmware receipts and
store digests are confined to authenticated private inspection, redacted from
public reports. Escape hostile names in terminal/browser output. Targeted bond
maintenance remains its own explicitly authorized operation with verified target,
quiescence, revocation and deletion verification; configuration edit/reset never
invokes it or clears the stack namespace.

## Transaction, restart and verification plan

Reuse [configuration persistence](configuration.md): complete validation before
handoff, inactive-slot generation+1, set/commit/read-back, old valid slot retained.
Preserve 1000 ms coalescing and bounded spaced attempts, indeterminate outcomes,
future-schema refusal and NVS boot gating. Do not call storage from serial, BLE or
web callbacks. Extend publication to correlate save request/result without
replacing the single configuration owner or adding multiple mailbox producers.
Keep a separate setup draft from the durable snapshot. Camera identity/model/count/
wake changes invalidate session mappings permanently; even reverting values does
not requalify that boot. A supervised restart creates new mappings only from the
independent commissioning provider; missing/mismatched proof still refuses.

| Step | Behavior and affected modules | Finite verification | Completion boundary |
|---|---|---|---|
| 1. Setup service contract | New bounded setup parser/owner and host client; integrate `bench_console`, `main`, `config_bootstrap` through one producer and correlated result publication. Physical entry, deadlines, draft and request IDs. | Fake-clock unauthorized/pre-entry, wrong session, third auth failure, inactivity/absolute expiry, reset/replay, oversized/partial/unknown input and full mailbox cases; host transcript fixtures. | Preferences/status only; no new camera pairing or network transport. |
| 2. Validated save/restart | Reuse `config`, `config_storage`, `config_bootstrap`; add four-slot/X5-position validator, immutable qualification display and quiescent exit/restart barrier. | Invalid model/name/address, NUL, duplicate case/identity, fifth slot, disabled-slot count, stale epoch/generation, partial candidate, storage refusal/error and restart without REC. Reuse unchanged codec/NVS suites. | Explicit verified saves for existing commissioned profiles; corrupt/future records preserved; unqualified resources stay disabled. |
| 3. Discovery assistance | Lease `BleCentral`/`Esp32BleHost`, bounded copied candidate inventory, private display and per-model wizard routes using current adapters. | Scan deadline/cancel-release, truncation, repeated names, rotating/private address refusal, resolved identity duplicate, two concurrent clients, capacity/bond overflow, camera refusal/timeout, late grant and host reset. | Candidate selection and evidenced existing pairing paths only. New bond/proof-owner capability remains blocked until implemented/tested separately; no invented model auth. |
| 4. Private commissioning handoff | Assist provider/receipt review using `pairing_proof_esp32`, profile qualification and bench admission. Preserve private headers initially; any later evidence store needs a distinct provenance/security schema. | Fixture mismatched/revoked store proof, wrong firmware/API/controller identity, changed peer mappings, denied bond maintenance and attempted writable qualification flags. | Honest prerequisite reports and reviewed handoff; universal unattended commissioning remains unresolved. |
| 5. Optional Wi-Fi feasibility | Compile/link trial for parked SoftAP, bounded assets, authenticated session and serial-equivalent service. Stop AP before BLE work; reconnect UI reads durable results. | Maximum-client/parser load, auth/CSRF/origin rejection, allocation failure, resource measurements, teardown and serial recovery; pinned SDK build. | Ship disabled unless declared memory budgets and lifecycle tests pass. Radio measurements/physical coexistence belong to #46/#22, not assumed from compilation. |

Software implementation steps require focused behavior/error tests, affected
pinned builds, formatting and CI per [acceptance policy](acceptance_policy.md).
Existing independent synthetic evidence is in
[test_config_storage](../test/test_config_storage/test_main.cpp),
[test_config_bootstrap](../test/test_config_bootstrap/test_main.cpp),
[test_config_nvs.py](../test/test_config_nvs.py),
[test_pairing_reset_esp32.py](../test/test_pairing_reset_esp32.py) and
[test_bench_application.py](../test/test_bench_application.py). Reuse passing
unchanged evidence; new entry/parser/identity-result behavior needs new tests.

For power interruption, synthetic store faults cover before set, during set,
after set/before commit, commit error and before/during read-back; boot must
publish one complete valid record or explicit refusal, never a partial camera
list. Physical cuts at those declared boundaries remain with #31: record exact
build/configuration, observed old/new/refused settings and preserved unrelated
bond/SD bytes. No durability promise is inferred from mocks. Actual per-model
pairing grant/refusal, firmware, three observed REC/STOP cycles/playback and
reconnect/no replay remain in [#46](https://github.com/llazzaro/RideSync/issues/46)
and compatibility [#22](https://github.com/llazzaro/RideSync/issues/22).
Installed physical entry/wiring qualification belongs to #30; integrated
reset/storage/control checks belong to #31. This plan creates no new campaigns.

## Investigation disposition

All six #56 investigation items have a concrete result above: audit, transport
comparison/serial-first decision, bounded model-aware identity/approval handling,
minimal workflow, entry/security policy and finite persistence/implementation
checks. The unresolved capabilities are automatic first-time stable identity/
bond proof for all models, universal Insta360 authorization/firmware detection,
BLE setup on the existing peripheral registration, actual added transport memory
cost/coexistence and reliable automatic parked detection. These findings do not
block closing a planning ticket and do not complete their future implementation.

Evidence labels: repository contracts and official APIs are **documented**;
existing fixtures/mocks are **synthetic**; linked isolated X5 sessions are
**observed** for their recorded profile only. This investigation adds no hardware
observations or **hardware-confirmed** support. Documentation validation is
sufficient for this change; no firmware rebuild or device IO is required.

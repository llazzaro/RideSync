# Single-X5 Recording Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Native execution in this session is recommended. The owner approved this plan and native execution in the prior session on October 10, 2026.

**Goal:** Complete #3's production single-X5 serial REC/STOP path and its finite acceptance, reusing existing camera evidence.

**Architecture:** Add a portable one-peer X5 adapter and a concrete CE80 peripheral backend within the existing singleton BLE host. Compose a dedicated one-attempt camera manager and bounded serial milestone image, independently of HERO12 and local telemetry. Keep SDK delivery, desired state and fresh observed state separate.

**Tech Stack:** C++11, Unity 2.6.1, PlatformIO Core 6.1.18, espressif32 6.12.0, Arduino ESP32 3.20017.241212+sha.dcc1105b, NimBLE-Arduino dfb4ac561a06797081be9e752902a6582e7f029e.

**Spec:** [Approved design](../specs/2026-10-10-x5-recording-adapter-design.md), written-spec approval received October 10, 2026.

## Global constraints

- Work on main; commit/push verified explicit paths to origin/main, without a PR, worktree, force push or unrelated changes.
- One X5, one peripheral connection, one outbound request; fixed 32-event receive queue and owned 256-byte payload storage.
- Maximum observation age 5000 ms; command/query deadline 5000 ms; Connect and manager outer timeout 15000 ms; max_attempts = 1.
- Use the existing CE80 shutter encoder and captured display decoder. Never invent an ACK, wire query, mode command, handshake or BE80 substitution.
- One boot-lifetime BLE host and guarded store. No auto-format, bond deletion/eviction, SDK reinitialization or task deletion on timeout.
- One explicit CONNECT per attempt; no automatic reconnect, shutter retry, reset replay or delayed command queue.
- Real private identity and independent store proof are required. Missing commissioning is an explicit refusal, never fabricated qualification.
- Preserve the observed advertisement and both services; do not claim every documentary attribute is required by X5.
- All new camera operations stay in #46. Reuse the checked three-cycle/reconnect and receive evidence; only its ready final composition row remains for #3.
- Tests/build/CI and a fresh read-only final review precede hardware operation. Documentation-only commits reuse passing source checks.

## Review focus

Each condition is pinned by the owning task's tests below:

1. A matching display was queued before a shutter send: it must not complete the new command (Task 1).
2. Queue overflow loses the only Photo transition: a sticky loss latch must revoke video/control qualification even if the queue is full (Tasks 1/2).
3. A SDK notify call returns after its deadline or after disconnect: retain its storage and report ambiguity without a second send (Task 2).
4. A camera reuses the same connection handle: old subscription/display/cleanup events must not authorize the new generation (Tasks 1/2).
5. An oversized or partial serial line contains REC/CONNECT: discard it as one malformed line, with continued deadline progress (Task 3).

## File map

| Files | Responsibility |
|---|---|
| `include/x5_peripheral.h` | Portable copied event/request and peripheral port contract |
| `include/profiles/insta360_x5.h`, `src/profiles/insta360_x5.cpp` | X5 qualification, observation policy, finite operation and CameraTransport adapter |
| `include/x5_peripheral_esp32.h`, `src/x5_peripheral_esp32.cpp` | Retained raw GATT definitions, callback queue, SDK submissions and barriers |
| `include/ble_esp32.h`, `src/ble_esp32.cpp` | Pre-start registration hook and shared peripheral radio/store reservation |
| `include/profiles/insta360_x5_esp32.h`, `src/profiles/insta360_x5_esp32.cpp` | Boot-lifetime adapter/manager and commissioned runtime |
| `include/x5_serial_control.h`, `src/x5_serial_control.cpp` | Portable bounded line parser and serial command dispatch |
| `src/main.cpp`, `platformio.ini` | Mutually exclusive runnable X5 milestone selection and retained compile target |
| `test/test_x5_adapter/test_main.cpp`, `test/test_x5_serial/test_main.cpp` | Independent policy/manager/parser behavior tests |
| `test/test_x5_peripheral_policy/test_main.cpp` | SDK-independent retained mailbox/loss/generation/radio policy tests |
| `.github/workflows/ci.yml` | Retain and build the new concrete targets in CI |
| `docs/insta360_protocol.md`, `docs/testing.md`, `docs/issue_review.md` | Delivered limitations, reproducible commissioning, verification and issue evidence |

## Task 1: Portable X5 adapter and real manager contract

**Files:** Create `include/x5_peripheral.h`, `include/profiles/insta360_x5.h`, `src/profiles/insta360_x5.cpp`, `test/test_x5_adapter/test_main.cpp`. Reuse `test/fixtures/insta360/ce80_display.h` and existing codec; do not rewrite it.

**Interfaces:** Define the following portable contract in namespace `ridesync`. `BondIdentity` and `BleStoreProof` are the existing types from `ble_pairing_reset.h`; `Clock`, `Token`, `Event` and `CameraTransport` are existing manager types.

```cpp
enum class X5InputKind : uint8_t {
  Connected, Subscribed, Unsubscribed, Display, SendReturned,
  Disconnected, Terminal, Fault
};
enum class X5Failure : uint8_t {
  None, Disabled, Qualification, Busy, Host, Subscription, UnknownState,
  WrongMode, Stale, Timeout, Transport, Cancelled, LostInput, Exhausted
};
struct X5Qualification {
  bool enabled = false;
  BondIdentity identity;
  BleStoreProof store;
  insta360::Ce80DisplayConfig display;
  std::array<char, 16> firmware{};
  uint8_t firmware_size = 0;
};
struct X5Input {
  X5InputKind kind = X5InputKind::Fault;
  uint32_t connection = 0, operation = 0, sequence = 0, received_ms = 0;
  uint16_t handle = kBleNoHandle, size = 0;
  int status = 0;
  BondIdentity identity;
  std::array<uint8_t, 256> bytes{};
};
struct X5ShutterRequest {
  Token token;
  uint16_t handle = kBleNoHandle;
  uint32_t deadline_ms = 0, observation_sequence = 0;
};
class X5PeripheralPort {
public:
  virtual ~X5PeripheralPort() = default;
  virtual bool configure(const X5Qualification &) = 0;
  virtual bool connect(Token, uint32_t deadline_ms) = 0;
  virtual bool notify(const X5ShutterRequest &) = 0;
  virtual void cancel(Token) = 0;
  virtual void close(uint32_t connection) = 0;
  virtual bool poll(X5Input &) = 0;
  virtual bool takeLoss(uint32_t connection) = 0;
  virtual bool released(uint32_t connection) const = 0;
  virtual void service(uint32_t now_ms) = 0;
};
```

`X5Adapter` implements begin/cancel/close, adds `configure(const X5Qualification&)`,
`attach(CameraManager&)`, `service()`, `ready(Operation) const`,
`failure() const`, `observed() const` and static `managerPolicy()`.
`ready` is a local admission check, not an SDK call. `observed()` returns the
currently fresh `RecordingState`; failure reports the concrete refusal reason.
Attach exactly once; no inline manager callbacks from begin/cancel/close.

- [ ] **Write independent failing tests.** Implement a fixed fake port whose
  `notify` increments a counter and records the request; inject literal video,
  photo, elapsed and remaining fixtures through copied `X5Input`s. Start with
  the central invariant:

```cpp
void ambiguous_send_never_retries() {
  FakeClock clock;
  FakePeripheral port;
  X5Adapter adapter(port, clock);
  CameraManager manager(clock, adapter, X5Adapter::managerPolicy());
  adapter.attach(manager);
  configureQualifiedX5(adapter, manager);
  connectSubscribedVideo(adapter, manager, port, clock);
  TEST_ASSERT_TRUE(adapter.ready(Operation::Start));
  manager.request(0, Operation::Start);
  adapter.service();
  TEST_ASSERT_EQUAL_UINT32(1, port.notifications);
  clock.now_ms += 5001;
  adapter.service();
  manager.tick();
  clock.now_ms += 15001;
  adapter.service();
  manager.tick();
  TEST_ASSERT_EQUAL_UINT32(1, port.notifications);
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Unknown), int(adapter.observed()));
}
```

Define `FakeClock` as a `Clock` returning public `uint32_t now_ms`.
`configureQualifiedX5` uses one synthetic public identity, nonzero synthetic
store qualification and `X5CapturedDisplayV1` solely in the fake port test.
`connectSubscribedVideo` requests Connect, queues matching Connected/Subscribed
events, then a literal video fixture with a strictly increasing receipt sequence;
it services adapter then manager. No fake data is used by the SDK backend.

- [ ] **Run the test red.** `.venv/bin/pio test -e native -f test_x5_adapter`.
  Establish compiled signatures first; verify the substantive missing behavior
  fails before implementing it, rather than treating a missing header as proof.
- [ ] **Implement qualification and finite operations.** Reject wrong model,
  identity/profile, firmware-size mismatch and unsupported enum values. Return
  manager policy `{15000, 200, 1}`. Connect completes only on matched connection
  and subscription; emit supported Start/Stop/passive Query capabilities but
  no invented wake/GPS support. Events are processed on service after begin.
- [ ] **Implement observations and video epochs.** Validate generation/sequence/
  complete length, decode only admitted CE81 Display inputs, retain fresh state
  for at most 5000 ms. Video settings establishes qualification; Photo, bad
  relevant display, loss or disconnected generation invalidates it. Valid
  timers renew recording in the established video epoch, never discover mode.
  Unknown opaque/remaining frames do not renew observation age. Emit fresh
  RecordingObserved updates and Unknown on invalidation/expiry.
- [ ] **Implement one-send commands and passive query.** REFUSE unknown/photo/
  stale/busy state before manager request; do not issue delayed toggles. On valid
  Start/Stop, retire pre-submission evidence and enqueue exactly one request.
  Await eligible post-submission desired observation or report timeout/failure.
  Emit RecordingObserved separately, then Completed; never use wireAck. SDK
  acceptance does not complete the command. Query waits passively or reports
  fresh cached evidence. A no-op is allowed only with fresh qualified desired state.
- [ ] **Add finite adversarial tests and run green.** Pin exact age/deadline
  boundaries, time rollover, photo/unknown modes, elapsed without Video,
  missing service/subscription and failure, oversize/malformed data, dropped
  input, pre-send queued target state, reused handle/old generation, operation
  exhaustion, cancel/reset/reconnect, unsupported model and no-op commands.
  Check both manager outcome and notification count, not just adapter flags.
  Run native focused tests and isolated strict ASan/UBSan portable tests.
- [ ] **Format, commit and push the verified portable unit.** Use explicit paths;
  record focused results. This commit does not close #3 or enable hardware.

## Task 2: Shared-host production CE80 backend

**Files:** Create `include/x5_peripheral_esp32.h`, `src/x5_peripheral_esp32.cpp` and
portable mailbox/radio-policy source beside that backend; add
`test/test_x5_peripheral_policy/test_main.cpp`. Modify `ble_esp32` and
`platformio.ini` narrowly. Preserve existing central/wake/store behavior.

**Interfaces:** `Esp32X5Peripheral` implements every `X5PeripheralPort` method.
Its definitions and queues have boot lifetime. Extend host with one pre-start
peripheral registration callback and a generation-scoped radio reservation.
The registration callback runs after raw NimBLE init and before the host task;
registration is immutable after `leased_`. Reservation acquisition/release must
be serialized with existing wake/reset and central GAP admission. The backend
uses existing `configureRestore`, `start`, `admittedProof`, `state` and `fault`;
no separate startup/store implementation is introduced.

- [ ] **Write mailbox/radio policy tests red.** Use a fixed queue and a fake SDK
  submission gate. Assert a full queue still preserves the separate loss latch:

```cpp
void lost_photo_transition_revokes_control() {
  PeripheralMailbox mailbox;
  mailbox.begin(7);
  X5Input input;
  input.connection = 7;
  input.kind = X5InputKind::Display;
  for (unsigned i = 0; i < 32; ++i)
    TEST_ASSERT_TRUE(mailbox.push(input));
  TEST_ASSERT_FALSE(mailbox.push(input));
  TEST_ASSERT_TRUE(mailbox.takeLoss(7));
  TEST_ASSERT_FALSE(mailbox.takeLoss(8));
}
```

Define `PeripheralMailbox` with `begin(uint32_t)`, `push(const X5Input&)`,
`poll(X5Input&)` and `takeLoss(uint32_t)`. It has 32 fixed entries and a sticky
per-generation loss bit. Its caller supplies callback-safe synchronization.
Add explicit tests for old-generation loss/events, terminal retention and
late notify returns. Run `.venv/bin/pio test -e native -f test_x5_peripheral_policy`.
- [ ] **Implement the raw GATT profile.** Independently define the exact CE80/
  D0FF service facts documented by the probe, fixed little-endian read bytes,
  automatic CE82 CCCD and the observed advertisement/name. Use
  `ble_gatts_count_cfg`/`ble_gatts_add_svcs` in the host's pre-start hook;
  any error prevents advertising. Copy CE81 mbufs into fixed storage after
  connection/attribute/size checks. Wrong characteristic or truncated data
  cannot become a valid Display. Never retain SDK mbuf pointers.
- [ ] **Implement retained SDK ownership.** One worker/host-event mailbox owns
  startup/advertising/notify/cleanup submissions. The control loop only copies
  requests and polls bounded results. Context and service storage remain alive
  on timeout. Require terminal disconnect and queued host barrier with zero
  active callbacks before `released` becomes true. Never delete blocked work.
- [ ] **Implement actual peer/store/radio admission.** Match the SDK stable or
  verified resolved identity to configured qualification before subscription or
  notification authorizes control. Deny unqualified security/store mutation.
  Honor exact restore proof and existing guarded NVS callbacks. Reservation
  blocks overlapping peripheral/wake/reset/central GAP work and makes Busy
  finite. Include tests for revoked proof, pending host, radio Busy and reused
  handles; inspect the existing shared-host paths for bypasses.
- [ ] **Implement notification admission once.** Make the final check after
  SDK mbuf preparation, against generation, current subscription, deadline,
  cancellation and loss latch. Mark the request consumed before the SDK call:

```cpp
if (!current_generation || !subscribed || cancelled || input_lost ||
    now_ms - request.deadline_ms < 0x80000000UL) {
  releasePreparedMbuf();
  publishFailure();
  return;
}
consumed = true;
int status = ble_gatts_notify_custom(connection_handle, ce82_handle, prepared);
publishSendReturned(status); // never re-enqueue this request
```

Here the three named helper actions are backend methods: free the locally owned
unsubmitted mbuf, enqueue a generation-bound Fault, and enqueue SendReturned.
After SDK entry, preserve the pinned API's ownership contract even on error.
Use the existing encoder's literal nine-byte result, never a second encoder.
  At actual SDK entry, snapshot the callback receipt-sequence counter, not
  merely the last display processed by the adapter. Publish that cutoff in
  the matching SendReturned event. Before this cutoff is known, observations
  may update general status but cannot complete the command; after it is known,
  only displays with greater sequence qualify. Test a display already queued
  at submission and a display received during a delayed SDK call. Preserve
  the no-ACK/no-causal-proof limitation even for greater-sequence observations.
- [ ] **Complete failure/race tests and build.** Exercise subscribe loss between
  prepare and submit, callback overflow before submit, return after expiry,
  return after disconnect, allocation failure, cancel before and after entry,
  duplicate callback and shutdown before barrier. Prove no second SDK call and
  continued serial-owner deadline progress. Run policy and adapter tests.
  Add `x5_adapter_compile` extending the pinned base with linker retention for
  the concrete backend/runtime, then `.venv/bin/pio run -e x5_adapter_compile`.
  Inspect retained symbols and stack/memory use; label it compile evidence.
- [ ] **Format, commit and push verified backend paths.** Preserve unrelated
  changes and previous source verification; no hardware upload in this task.

## Task 3: Runnable serial milestone and ticket delivery

**Files:** Create the portable serial control files, ESP32 runtime files and
`test/test_x5_serial/test_main.cpp`; modify main selection, PlatformIO, CI and
the three delivery documents from the file map.

**Interfaces:** `X5Runtime` exposes references to adapter and manager plus
`configure(const X5Qualification&, const SourceConfig&)`, `service()` and
`request(Operation)`; it refuses requests unless adapter admission allows them.
`X5SerialControl` consumes a runtime command port with
`request(Operation)`, `disconnect()` and `status()` methods. Its
`consume(char)` accepts complete LF/CRLF lines with a fixed 32-byte buffer,
and `service()` drains at most one parsed command. STATUS reports a copied
snapshot; no serial function performs SDK or NVS work.

- [ ] **Write parser/runtime tests red.** Test exact command lines, duplicate
  REC while Busy, STATUS without side effects and malformed line atomicity:

```cpp
void oversized_line_cannot_embed_recording() {
  FakeSerialRuntime runtime;
  X5SerialControl serial(runtime);
  for (unsigned i = 0; i < 40; ++i) serial.consume('x');
  for (char c : std::string("REC\n")) serial.consume(c);
  serial.service();
  TEST_ASSERT_EQUAL_UINT32(0, runtime.requests);
  for (char c : std::string("STATUS\r\n")) serial.consume(c);
  serial.service();
  TEST_ASSERT_EQUAL_UINT32(1, runtime.status_requests);
}
```

`FakeSerialRuntime` implements the stated command port and increments counters;
no SDK calls are needed. Add partial-line, embedded CONNECT/STOP, CR without LF,
invalid bytes and noisy-UART deadline tests. Run
`.venv/bin/pio test -e native -f test_x5_serial` before implementation.
- [ ] **Implement the boot-lifetime runtime.** Compose real peripheral, adapter,
  one-attempt manager and clock once. Service port/adapter before manager tick;
  no parallel manager owner, queued toggle producer or replay state. Provide
  explicit private commissioning through a local untracked header selected by
  the milestone build: verified identity, exact firmware, real store proof and
  qualification flags. Missing header/qualification is explicit refusal, not a
  silently successful build-and-activation claim. Keep header ignored and do
  not print its values. Document how the existing commissioning owner observes
  store counts/digest and supplies the independent qualification record.
- [ ] **Implement actual main/serial selection.** Under
  `RIDESYNC_X5_SERIAL_MILESTONE`, select the dedicated runtime before ordinary
  application worker selection. Read at most 32 serial bytes and service at
  most 32 receive events per loop; no hardware GPIO/bus assumptions. The
  configuration owner retains exclusive proof-maintenance initialization;
  the BLE SDK owner retains startup/store inspection. Boot never auto-connects
  or records. CONNECT/REC/STOP/QUERY/STATUS/DISCONNECT dispatch to real methods.
  Emit operation IDs, fresh state/age, link/subscription and failures, without
  identifiers or raw payloads. Ordinary main/HERO12 builds preserve behavior.
- [ ] **Add compile and runnable CI coverage.** `x5_serial_milestone` extends
  `x5_adapter_compile` with `-DRIDESYNC_X5_SERIAL_MILESTONE`. CI compiles it
  with a nonactivating qualification provider, while the physical declared
  image uses the real private provider. This exercises selection and SDK
  linkage but never claims physical activation from synthetic build data.
  Run default, retained-X5 and serial milestone builds, focused tests, then
  `scripts/check_pipeline.sh`. Run one isolated strict sanitizer pass over
  new portable suites; retain command/output evidence.
- [ ] **Request one fresh read-only review.** Review the complete implementation
  against this spec, no hardware operations or shared build-directory races.
  Resolve actionable findings and rerun affected checks only. Record any
  outside-scope findings against their existing owner, not as automatic new
  campaigns. Push verified implementation and require passing source CI.
- [ ] **Reconcile source/receive documentation.** State the actual service and
  subscription prerequisites, experimental Video epoch, passive Query,
  uncorrelated observation, exact deadlines and supported settings. Update
  #46's state-correlation row from the existing #19 replay/capture evidence;
  preserve the missing authoritative query semantics. Retire #3's outdated
  unclassified-receive text, without checking its implementation criteria
  until the real code/test evidence exists.
- [ ] **Execute only the ready final composition row in #46.** Declare revision,
  firmware 1.11.10 if unchanged, board, private commissioning, mode and card.
  Preserve installed firmware backup/NVS/card data before upload. Once the
  owner and software are ready, perform one serial REC then one STOP with
  observed status and camera-display confirmation. This qualifies the new
  application wiring; it does not repeat the three-cycle component smoke or
  receive campaign. If unavailable, keep that row explicitly Blocked and
  report the exact remaining prerequisite; do not close #3 prematurely.
- [ ] **Close #3 with a requirement audit.** Link actual code, focused tests,
  builds, passing CI, review resolution, existing three-cycle/reconnect log
  and final composition result. Check each remaining criterion only if its
  evidence proves it. Update roadmap #16 and delivery docs. Commit/push any
  final prose-only results without redundant firmware rebuilds. Leave #22,
  #31 and the broader #46 checklist open for their own remaining scope.

## Plan self-review

The three tasks cover portable state/control semantics, actual shared-host
service/SDK lifecycle and runnable delivery respectively. Every spec boundary
has an owner: privacy/proof/radio policy in Task 2; no retry/freshness/video epoch
in Task 1; parser/commissioning/selection/CI and finite hardware closure in Task 3.
The five review-focus risks have explicit tests. No task substitutes the probe,
a link-only image or passing codec for the production adapter. No new ticket or
additional camera campaign is created by this plan.

## Execution handoff

Recommended: native execution in this session, followed by the single fresh
final reviewer. The three units share tightly coupled lifecycle interfaces;
keeping implementation with one owner avoids repeated context/setup and shared
SDK build races. Review this plan and approve that method before code begins.

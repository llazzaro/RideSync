# Handlebar control composition (#41)

The source-gated, opt-in control composition shares #40's serialized application
owner. Firmware `setup()` does not activate it; #42 must admit and supervise the
qualified workers. No physical button, LED, camera or timing result is claimed.

`HandlebarControl` takes caller-owned input and LED ports and the existing HERO12
adapter/CameraManager/RecordingManager trio. Attach it with
`LocalTelemetryRuntime::attachControl` before `start()`. All control methods,
button polling, GPS/clock/admission and copied status access belong to this same
owner. The input, sink, control and telemetry resources outlive the actual final
SD/IMU access and `canRelease()`. Detach only after that barrier. Other tasks must
receive copies, never read these mutable owners directly.

`Esp32HandlebarControl` composes the real `ArduinoButtonInput`, `Esp32LedGpio`,
`GpioLedSink` and `HandlebarControl` around the existing `Esp32LocalTelemetry`.
`ridesync_handlebar_runtime(telemetry, qualified)` retains one boot-lifetime
instance; the first call fixes its resource/config references. Call
`ridesync_handlebar_begin` before telemetry startup. Configuration requires
explicit opt-in, board/electrical qualification, qualified button pins/polarity,
and complete LED reservations including the button. No default pin is supplied.
Disabled LED mode is explicit; input qualification is still required. Backend
refusal/configuration/write state and error remain copied in `HandlebarGpioStatus`.
Call `ridesync_handlebar_service` OR the existing telemetry/global service entry
once per owner pass: the facade delegates to that same exclusive route. It adds
no second session, service binding, camera manager, session clock, logger or SD owner.

## Intent and cancellation policy

- Only Running telemetry with CameraAdmission::Admitted and enabled cameras
  admits actions. Local-only, safe-mode/refused camera, allocation and stopped
  sessions refuse control while eligible independent local logging continues.
- The first admitted RecordingIntent is explicit REC, even if startup observation
  already says Recording. Each enabled peer independently prepares through the
  qualified HERO12 reconnect/fresh-observation route. Recovery is invoked with
  `ensure_recording=false`: **RecordingManager is the only REC authority**. The
  exact completed recovery connection/operation, current lifecycle and observation
  must still match before the group dispatches conditional explicit Start.
  Already-recording peers need no shutter command. ACK alone never proves state.
- The next admitted RecordingIntent is explicit STOP, including during recovery.
  Each replacement group request first cancels every old preparation and retires
  active operations before admitting replacement work. STOP uses the existing
  explicit group reconnect/Stop route; confirmed Stopped peers require no shutter.
  A delayed scan, old query or connection notification cannot resurrect REC.
  If canceling an unanswered query/command seals the link while the manager
  temporarily still reports Ready, the replacement request waits for the actual
  disconnect and per-peer host final-access barrier, then reconnects with current
  generations. This wait is bounded to 140 seconds; timeout retains the host
  resources and reports failure, never fabricating STOP or forcing lease release.
  Other peers continue independently. REC preparation shares this same 140-second
  budget when it follows retirement; it does not restart the preparation deadline.
- Four copied actions form an owner-only FIFO. Admission returns Admitted,
  Inactive, Refused or Overflow and counts each outcome. Overflow creates no
  request and does not flip the REC/STOP latch. The finite batch drains before
  advancement, so a STOP in that batch cancels earlier REC preparation before
  recovery can issue work. Reset/shutdown discard the queue and expose the count.
  Reset clears intent/latch to Unknown; a manager reset generation change also
  invalidates previously admitted actions. Nothing is replayed on reconnect/reset.
- Configured short, long and double actions remain authoritative. Defaults are
  short RecordingIntent, long WakeReconnect, optional double Resync. WakeReconnect
  and Resync perform fresh preparation/query with no automatic REC and do not
  flip the explicit RecordingIntent latch. Unsupported physical wake is copied as
  Unsupported per peer; supported BLE recovery remains independently eligible.
  Power-on from removed power and unqualified Insta360 protocols are not invented.
- Recovery retains its existing two bounded scan attempts and 140-second overall
  deadline, also enforced by the group preparation stage. Group-owned REC waits
  for current profile/ATT admission after the qualified observation; a due
  keepalive completes first. STOP on an existing link uses a bounded 2-second
  admission wait when keepalive/profile work owns that link. Profile faults are
  delivered first and remain explicit rather than dispatching overlapping work.
  Available peers can proceed while others scan/fail. Per-peer group
  pending/error and recovery outcomes remain visible; unavailable peers do not
  abort available work. Terminal errors take LED Error precedence; partial,
  unknown and recovery outcomes remain explicit in the same copied snapshot.

## Scheduling and status

The adapter brackets a composed pass with `beginServicePass`. It services central
transport, profile deadlines and queued connection faults/responses, then invokes
the control action seam, ticks CameraManager once and advances RecordingManager
once, then advances non-REC recovery. Event/request ingestion inside that pass
updates evidence/admission without extra group advancement. Standalone group
APIs retain immediate advancement. Diagnostic tick/advancement counters
measure actual implementations, including begin/event paths. Adapter and local
telemetry service reject recursive entry. External ports still must return
promptly and obey their owner contracts.

After telemetry service/observation, one copied ControlStatus contains group,
lifecycle, recovery, unsupported wake, local GPS/IMU/storage observations, LED
health/selection and latched backend error. Safe mode and terminal startup faults
are copied even when no worker was admitted. When telemetry is unbound and
releasable (Inactive/Refused/Finished), its concrete service publishes only its own
cached status through the same owner. It never calls an unrelated global route
or advances camera managers on that path. The existing actual LED selector and
present-frame renderer consume this snapshot; they do not replay missed edges.
No supervisor execution/liveness claim is added here; #42 supplies that policy.

Native tests drive the real ButtonManager, CameraEventSession, HERO12 adapter,
central transport, codecs and RecordingManager with synthetic UART/radio/filesystem
boundaries. They cover REC/STOP, unavailable peer, already-recording startup,
recovery presses, delayed evidence/reset, action pressure, custom mapping, actual
service order/count and copied faults. The retained ESP32 image links callable
GPIO/control composition. Fixed ABI and individual compiler frames are software
measurements, not physical stack/heap/latency or camera qualification.

Original #22 retains all mixed-brand capacity/timing/logging-continuity work;
#31 retains the full fault/8-hour-soak/controlled-ride matrix. Insta360 capture,
estimator/reference and hardware qualification gates remain unchanged.

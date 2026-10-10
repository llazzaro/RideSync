# Known limitations

- Source-gated, opt-in software implements camera intent/state contracts, bounded
  group REC/STOP, HERO12 codecs/shared BLE transport/recovery, configuration
  handoff and coherent handlebar/LED status. Default firmware keeps optional
  control and AT/BLE/SD/IMU workers inactive without explicit resource
  qualification. Bounded configuration/startup, completed-work supervision and
  ordered worker teardown are composed in software. Native/retained builds are not hardware proof.
- A factory-firmware probe confirmed modem identity and GNSS enable/READY, but no
  position fix. Isolated RideSync diagnostics observed X5 CE80 pairing/subscription
  and one owner-confirmed wake. A card-ready retry on owner-reported firmware
  1.11.10 established a remote recording start and playable clip after manual
  stop; a subsequent connection established reconnect. The production
  runtime and per-camera compatibility remain unqualified. See the
  [pairing/shutter result](hardware-results/2026-10-09-x5-pairing.md) and
  [wake result](hardware-results/2026-10-09-x5-wake.md).
- Configuration has validated bounded runtime/persistence and immutable startup
  handoff contracts. Sample configuration supplies no proof of qualified pins,
  camera firmware, bond identity or commissioned durable session namespace.
- Explicit recording intent, command response and fresh observed state remain
  distinct. No blind shutter-toggle retry/replay is supported. Reset starts
  Unknown and cancels pending intent; command ACK does not establish recording.
- Shared BLE central/pairing software exists, but actual simultaneous connection
  capacity, mixed-role timing, per-peer delivery and mixed-brand compatibility
  require the complete #22 bench matrix. Four configured peers are a software
  target, not measured radio capacity.
- HERO12 software is pinned to source-qualified Open GoPro setup/control and
  independently supplied firmware/API/bond evidence. Physical firmware/control,
  wake/reconnect behavior and mixed BLE roles remain open. Unsupported physical
  wake is explicit; BLE reconnect does not prove power-on. External GPS/IMU
  injection into GoPro is not assumed.
- Insta360 X5/ONE RS protocols and wake remain gated by model-specific captures
  and qualification. The isolated X5 probe completed three camera-observed remote
  Start/Stop cycles and reconnect on owner-reported firmware 1.11.10. The real
  X5 adapter remains unimplemented, incoming state packets remain unclassified,
  and the isolated wake result is not production orchestration. Remaining camera
  checks are consolidated in [#46](https://github.com/llazzaro/RideSync/issues/46).
  Official GO 3S accessory documentation identifies the camera as the remote
  pairing target and lists GPS Action Remote compatibility. The experimental
  [GO 3S BE80 FFFrame adapter](go3s_profile.md) implements source-backed sync,
  authorization and explicit Start/Stop on the shared host. ACKs leave observed
  recording Unknown; Query, Wake and GPS are Unsupported. Actual security,
  camera/Pod firmware and physical recording remain Not tested in #46.
- A7670E GNSS parsing/acquisition, session clocks, isolated microSD logging,
  durable session allocation and independent local telemetry composition have
  implemented software contracts. Real modem AT behavior/fix, commissioned
  namespace uniqueness, card power-loss durability and storage/bus qualification
  remain open. UTC or randomness alone does not prove unique session identity.
- External BMI270 raw IMU acquisition has selected, pinned source and qualified
  opt-in transport contracts. Physical sensor/electrical qualification,
  calibration and dynamic lean/reference validation remain open. GPS alone does
  not measure lean; no validated inclination estimator is claimed.
- Button/LED GPIO adapters require explicit qualified wiring and default off.
  Software watchdog/recovery and actual main supervision/admission contracts exist;
  physical watchdog/stack/heap measurements, installation/enclosure (#30), and the complete
  #31 finite fault/bench/controlled-ride acceptance remain open.

Source implementation, Experimental qualification and Confirmed hardware support
must remain distinct. Promote hardware support only with reproducible target
results; preserve failures and firmware versions in reports.

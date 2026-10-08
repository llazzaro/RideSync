# Known limitations

- Source-gated, opt-in software implements camera intent/state contracts, bounded
  group REC/STOP, HERO12 codecs/shared BLE transport/recovery, configuration
  handoff and coherent handlebar/LED status. Default firmware keeps optional
  control and AT/BLE/SD/IMU workers inactive without explicit resource
  qualification. Bounded configuration/startup, completed-work supervision and
  ordered worker teardown are composed in software. Native/retained builds are not hardware proof.
- A factory-firmware probe confirmed modem identity and GNSS enable/READY, but no
  position fix. RideSync firmware and per-camera compatibility remain unverified
  on physical hardware.
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
  and qualification. GO 3S/Action Pod link ownership, third-party pairing,
  services, recording commands and authoritative state are unresolved. Official
  materials document Pod Bluetooth remote control without a third-party GATT
  protocol; absent captures are not an incompatibility finding. See the
  [GO 3S evidence report](insta360_protocol.md#go-3s-and-action-pod-feasibility-6).
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

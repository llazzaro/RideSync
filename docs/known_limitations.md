# Known limitations

- Only repository bring-up firmware exists. No camera operations are implemented.
- A factory-firmware probe confirmed modem identity and GNSS enable/READY, but
  no position fix. RideSync firmware and camera compatibility are unverified.
- The sample camera configuration is documentation, not a runtime input.
- A shutter toggle cannot safely implement explicit REC/STOP without state.
- BLE role, pairing, simultaneous connection capacity and per-peer delivery are
  pending validation. Three-camera architecture is a requirement, not a result.
- Camera-specific wake identifiers and multi-camera advertising are untested.
- GoPro HERO12 Black is planned only; installed firmware needs recording. Open GoPro API
  compatibility, wake behavior and mixed BLE role capacity need validation.
  External GPS/IMU injection into GoPro is not assumed.
- GO 3S/Action Pod control-link ownership, third-party pairing, services,
  recording commands and authoritative state are unresolved. Official materials
  document Pod remote control over Bluetooth but not a third-party GATT protocol;
  there are no local hardware captures, and this is not an incompatibility
  finding. See the [GO 3S evidence report](insta360_protocol.md#go-3s-and-action-pod-feasibility-6).
- The selected A7670E has built-in GPS. Its AT command behavior still needs
  verification; parsing, camera telemetry and independent SD logging are pending.
- An external IMU is required for motion sensing; sensor selection, calibration
  and dynamic lean estimation are pending. GPS alone does not measure lean.
- External button, LED, watchdog/recovery coverage and enclosure are pending.

Promote support to Experimental only when implemented, and to Confirmed only
with reproducible target-hardware evidence. Preserve failures and firmware
versions in test reports.

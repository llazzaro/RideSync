# Known limitations

- Only repository bring-up firmware exists. No camera operations are implemented.
- No hardware testing or per-model compatibility confirmation has occurred.
- The sample camera configuration is documentation, not a runtime input.
- A shutter toggle cannot safely implement explicit REC/STOP without state.
- BLE role, pairing, simultaneous connection capacity and per-peer delivery are
  pending validation. Three-camera architecture is a requirement, not a result.
- Camera-specific wake identifiers and multi-camera advertising are untested.
- GO 3S may require a distinct protocol; no unsupported claim is made yet.
- The selected A7670E has built-in GPS. Its AT command behavior still needs
  verification; parsing, camera telemetry and independent SD logging are pending.
- External button, LED, watchdog/recovery coverage and enclosure are pending.

Promote support to Experimental only when implemented, and to Confirmed only
with reproducible target-hardware evidence. Preserve failures and firmware
versions in test reports.

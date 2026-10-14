# ONE RS legacy shutter trial — October 14, 2026

The owner approved a bounded compatibility trial for ONE RS Core + 4K Boost Lens,
camera v2.0.11.4, after the [physical pairing investigation](../hardware-results/2026-10-14-one-rs-4k-pairing.md).
The installed camera sent CE81 traffic and remained connected without a CE82
subscription event, so the previous isolated probe refused every manual shutter
request. This software change enables an explicitly selected addressed shutter
trial; it does not qualify production control or provide recording-state decoding.

## Change and source decision

Idle-only L before A selects ONE RS legacy mode for that boot. The existing
SDK owner can submit each fresh manual S to the actual connected peer and
registered CE82 handle without a subscription event. The selected option and
actual subscription are reported separately. No CCCD or SDK subscription state
is changed or fabricated, and recording remains Unknown. Default mode retains
subscription admission. Wake-only mode still refuses shutter operations.

Disconnect clears pending work and the per-link legacy allowance. Deadline,
stop, cancellation, SDK-owner admission and reported unsubscribe still prevent
submission or replay. A reported client unsubscribe revokes the allowance.
NimBLE suppresses unchanged CCCD events, so an explicit zero write when CCCD
is already zero cannot revoke this operator-selected direct-notification trial.

The pinned MIT [ESP32 example](https://github.com/pchwalek/insta360_ble_esp32/blob/83d4748b68d6ee5fd4414994a9e26b7d2f21364b/Insta_BLE.ino)
pre-enables BLE2902 notifications. The pinned MIT [M5 example](https://github.com/marcelpallares/insta360-m5stick-remote/blob/c76e140396de8b2404cdd36d17cf0d1a251a9dcc/ble_handlers.h)
also supplies addressed notifications using the ESP API. The local pinned
NimBLE `ble_gatts_notify_custom` sends to the supplied connection/handle without
checking CCCD; the implementation uses that existing audited API, not copied
upstream application logic or modified SDK/store behavior. The xaionaro-go
protocol document's alternate wake description was not substituted for the
existing independently tested M5/ESP32 wake layout.

Wake uses the existing finite three-second W/A advertisement with a separately
checked private camera identifier and Bluetooth Wakeup enabled before power-off.
No camera identifier or packet capture is published. An actual power-on result
is required before claiming ONE RS wake support.

## Verification

- The new compatibility test first failed on the absent API, then failed at
  actual shutter request admission after adding the interface without the
  admission branch. It passed after the separate legacy allowance was wired in.
- Focused probe tests passed, covering default refusal, current-peer/handle
  routing, truthful subscription state, fresh explicit admission, disconnect
  reset/no replay, stop, deadline, unsubscribe and cancellation.
- `scripts/check_pipeline.sh` passed: portability, Ruff, 217 Python tests,
  finite bench tests, 700 native cases and source formatting. Existing fixture
  compiler warnings remain in the private log; no check failed.
- Pinned `pio run -d tools/bench/x5_peripheral` passed: RAM 45,420 bytes,
  flash 574,629 bytes. Firmware SHA256:
  `34b78d58b865eb65318ece10a80ca455e4b6e15f9e38342d8d8bc8c6c14e8da5`.
- Final-ELF audit passed for addressed notify ownership, raw wake preparation,
  RAM-only store, NVS/core/PHY refusal boundaries and retained Bluetooth HAL.
- Independent review found no actionable code issue and identified the
  unchanged-CCCD limitation above. `graphify update .` completed.

The current 4 MiB installed flash was backed up privately and verified against
the device before any upload; the current NVS region was retained separately.
At software delivery, firmware upload was awaiting ESP32 SD-slot preparation.
The subsequent upload and first advertising attempt are recorded in the
[hardware report](../hardware-results/2026-10-14-one-rs-4k-pairing.md#legacy-image-upload-and-first-pairing-window).
Three observed recording cycles, playable footage, reconnect/no
replay and ONE RS wake remain unperformed. Existing X5 observations do not pass
those ONE RS checks. The source-qualified ordinary-360 BE80 production adapter
and its model qualification remain unchanged.

## Session lifetime correction

The owner subsequently asked how the GitHub implementations keep the remote
connected after repeated manual pairing windows expired. The pinned ESP32
example's `CAPTURE_DELAY` controls its automatic demonstration commands; it
does not terminate the BLE server. The M5 implementation keeps its server
running and restarts normal advertising on disconnect. The Go CLI's remote
start action waits for context cancellation. No periodic CE80 keep-alive packet
was found in these inspected control loops. Their continuous server lifecycle
does not establish ONE RS reliability or explain the separately observed HCI
connection timeout.

The explicitly selected L-mode normal session now has a ten-minute bound and
manual X shutdown. `Capture::tick` and its SDK advertising-duration admission
use the same selected window. Default capture remains two minutes; W/A retains
its existing three-second wake slice and original capture bound even if L was
selected. Startup still expires at 15 seconds. A real disconnect ends the
attempt, and no pending recording command is retained or replayed.

The new session test failed at the original two-minute boundary after the new
interface was introduced, then passed with selected-window expiry/remaining.
It verifies ten-minute expiry, startup refusal, manual stop, timestamp wrap and
single-use admission. The full pipeline passed again (217 Python tests,
700 native cases, finite bench tests, portability, Ruff and formatting).
Pinned probe build and final-ELF audit passed, using RAM 45,420 bytes and flash
574,669 bytes. Independent review found no actionable issue, and graphify was
updated. The new firmware SHA256 is
`dac864e592ccf2af946dde6f35c4f497bac138cfc64955d2d8d95458b0a60da8`.

This corrects intentional early shutdown during the interactive test; it is
not a perpetual-connection implementation, an invented handshake, or a pass
for Stop, reconnect/no-replay or wake. The owner-confirmed Start and camera
error/timeout during the first Stop are recorded in the hardware report.

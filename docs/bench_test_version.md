# Composed bench test version

Use the `bench_application` environment for the supervised application: configured
camera control, local GNSS/SD logging and independently commissioned optional
button/LED/IMU. This is an experimental bench image. Physical observations remain
in [#46](https://github.com/llazzaro/RideSync/issues/46) and the existing integrated
acceptance owners; building it does not qualify installed hardware.

The image loads `include/bench_commissioning.local.h` privately. Without that
provider it reports a missing-provider refusal and starts no board peripherals.
An enabled incomplete provider reports a configuration or pin/startup failure.
Memory allocation failure refuses before peripheral IO. No automatic ledger
creation, formatting, camera identity discovery or fabricated store proof is performed.

## Private configuration

Retain the verified full-flash backup before upload. For X5, first complete the
[private store/identity commissioning](x5_serial_milestone.md#private-commissioning),
including the real `ridesyncPrivateX5Qualification` provider in the ignored
`include/x5_commissioning.local.h`. Reuse that function below; do not include its
header a second time or replace its observed proof with fixture values.

The fitted board's documentary UART route is TX26/RX27 at 115200. Its SD route
has an observed 1 MHz mount/write on SCK14/MISO2/MOSI15/CS13 with GPIO12 high.
Review these routes against the actual installed board before opting in.
The provider initializes dedicated SPI and optional I2C exactly once. The example enables a finite PWRKEY startup on the GPS owner: GPIO4 LOW for
100 ms, HIGH for 100 ms, LOW with 3000 ms settling, then bounded AT startup.
GPIO12 remains asserted throughout SD ownership. The sequence follows the pinned
[LILYGO ATdebug source](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00/examples/ATdebug/ATdebug.ino)
and its [A7670 timing definitions](https://github.com/Xinyuan-LilyGo/LilyGo-Modem-Series/blob/e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00/examples/ATdebug/utilities.h).
It performs no blocking setup wait or modem RESET pulse. Declare the actual modem
power/start state for the session; the documentary sequence and elapsed settling
do not establish physical readiness. AT/GNSS status records the resulting outcome.
Already-powered operation remains available by setting `modem_already_powered`
true and disabling `modem_power_sequence`, with the explicit completed startup/
receive-barrier premise and no key pin reservation.

Find the current SD namespace in the private provisioning record and keep the
existing ledger. Never run commissioning on this previously used card just to
start a new session. For a genuinely new, exclusively held card, assign a
never-used namespace in the external registry and follow
[offline first-use provisioning](log_format.md#commissioned-session-identities-and-pre-session-sd-ownership).
Boot advances the existing ledger through the production storage owner.

Create `include/bench_commissioning.local.h` from this example, replacing the
required namespace with the actual private assigned value. Copying and enabling
this provider declares a reviewed experimental routing/start-state session. It does
not establish electrical, power-loss or physical camera support.

```cpp
#pragma once
#include "bench_application.h"

// Define the actual retained namespace, never a random or example value.
#ifndef RIDESYNC_PRIVATE_SD_NAMESPACE
#error "Supply the actual privately commissioned SD namespace"
#endif

bool ridesyncPrivateX5Qualification(ridesync::X5Qualification &,
                                   ridesync::SourceConfig &);

inline bool ridesyncPrivateBenchConfig(ridesync::BenchApplicationConfig &c) {
  using namespace ridesync;
  if (!ridesyncPrivateX5Qualification(c.cameras.x5, c.source))
    return false;
  if (c.source.count != 1 || c.source.cameras[0].model != CameraModel::X5)
    return false;
  c.enabled = c.board_routing_reviewed = true;
  c.power_enable_active_high = true;
  c.pins.power_enable = 12;
  c.pins.spi_sck = 14;
  c.pins.spi_miso = 2;
  c.pins.spi_mosi = 15;
  c.pins.spi_cs = 13;
  c.pins.modem_tx = 26;
  c.pins.modem_rx = 27;
  c.pins.modem_key = 4;
  auto &q = c.telemetry;
  q.runtime.opt_in = q.runtime.gps_qualified = true;
  q.runtime.cameras_qualified = true;
  q.runtime.firmware = "private declared revision/build";
  q.runtime.provenance = "reviewed V1.4 bench session; observed SD; documentary AT route";
  q.runtime.modem.documentary_profile_opt_in = true;
  q.runtime.modem.terminal_retires_transaction = true;
  q.runtime.power_timing.qualified = true;
  q.modem_power_sequence = true;
  q.runtime.power_timing.pre_key_ms = 100;
  q.runtime.power_timing.key_active_ms = 100;
  q.runtime.power_timing.settle_ms = 3000;
  q.modem.supply = 12;
  q.modem.key = 4;
  q.modem.supply_active_high = q.modem.key_active_high = true;
  q.modem.pins_qualified = q.modem.documentary_profile_opt_in = true;
  q.modem.tx = 26;
  q.modem.rx = 27;
  q.modem.baud = 115200;
  q.sd.opt_in = q.sd.wiring_card_qualified = q.sd.exclusive_volume = true;
  q.sd.namespace_commissioned = true;
  q.sd.commissioned_namespace = RIDESYNC_PRIVATE_SD_NAMESPACE;
  q.sd.chip_select = 13;
  q.sd.frequency_hz = 1000000;
  q.runtime.peers.count = 1;
  q.runtime.peers.entries[0].id = 1; // Assigned opaque event identity for this slot.
  q.runtime.peers.entries[0].slot = 0;
  q.runtime.peers.entries[0].model = CameraModel::X5;
  return true;
}
```

The example enables the actual X5 and GNSS/SD composed workflow. IMU, external
button/LED and motion are omitted because no installed module/wiring/reference
facts are recorded. To enable them, supply the actual independently reviewed
configuration; requesting an incomplete enabled component causes refusal.
For IMU set matching `pins.imu_enabled`/`runtime.imu_enabled`, qualified BMI270
address 0x68/0x69 and sensor ID, dedicated/electrical flags, actual SDA/SCL,
positive I2C frequency and timeout. The provider reserves a 128-byte Wire buffer.
For a button supply matching source and handlebar GPIO/pull/polarity, both opt-in
and acknowledgement, and reserve its pin. LED pins/polarities and complete
reservations must match the reviewed wiring. All active bus/button/LED pins are
checked for conflicts before IO. Dynamic measurements require their separately
supplied reference/qualification and retain their documented validity limits.

Private source settings are copied into RAM only when persisted configuration is
absent. Existing valid saved settings win; corrupt/future/read-failed settings
are refused without replacement. This provider never saves or rewrites NVS.
A saved configuration that differs from the commissioning profile may refuse
camera/button admission; inspect status and resolve it explicitly.

## Build and session

From the repository root, using the pinned Python 3.11 environment:

```sh
.venv/bin/pio run -e bench_application
shasum -a 256 .pio/build/bench_application/firmware.bin
# After backup, private commissioning and declared hardware preparation:
.venv/bin/pio device list
.venv/bin/pio run -e bench_application -t upload --upload-port <private-port>
.venv/bin/pio device monitor --port <private-port> --baud 115200
```

Use one serial owner, record resets and retain startup/provider/configuration,
worker and runtime status. SPI initialization reports that its void initialization
call ran; actual mount/identity/IO success comes from storage status. I2C startup
and buffer allocation failures are explicit. GNSS NoFix/Unknown and disabled
components remain honest outcomes. Retain the exact revision, private binary
hash and component declarations alongside the bench report; publish only a
redacted summary.

## One composed workflow before installation

Send one uppercase command per LF/CRLF line and wait for its terminal report.
Pasted command bursts are refused; commands are not queued for replay. Boot sends
no recording intent. `STATUS` prints provider/configuration, local worker results,
internal heap and loop-stack measurements, group intent, each peer's delivery and
observed recording separately. Enum values follow the linked public headers.

| Command | Action |
|---|---|
| `STATUS` | Read the current provider, logger and camera outcomes. |
| `CONNECT` | Explicitly connect each configured camera; no shutter command. |
| `REC` | Request Start for the group; unavailable peers retain errors while available peers progress. |
| `STOP` | Request Stop for the group, retiring the preceding intent. |
| `QUERY` | Resynchronize supported state observations; unsupported queries remain explicit. |
| `WAKE` | Use the commissioned X5 wake path or per-model reconnect path; no automatic REC. |
| `SHUTDOWN` | Retire the application and drain/close logging; wait for `releasable=1`. It does not issue camera STOP. |
| `CLEAR` | Explicitly clear the retained safe-mode marker; retired camera admission stays closed for this boot. |
| `STATIONARY` | Declare that the supported fixture is stationary for the next qualified IMU sample; initialize one finite experimental motion window. |

First verify GNSS and logger startup. Confirm the actual camera video mode and
recording state, connect, then separately request REC and STOP with camera-display
annotations and saved-media playback. Check the resulting SD session's GPS/raw
IMU and correlated camera rows. Button and serial actions use the same group
intent; second short press requests Stop even after an explicit serial REC.
Repeat affected model/group checks from #46, including one missing peer while
local logging continues. Complete these on the bench before installing anything.

Command admission, ATT delivery, camera ACK, observed recording and saved
footage are distinct outcomes. ONE RS and GO 3S can receive explicit Start/Stop
while observed recording remains Unknown; group confirmation then expires with
an explicit timeout. That is a missing observation capability, not a fabricated
successful recording result. Their physical wake and query remain unsupported.

X5 must occupy slot zero; the supported shared group has at most four slots.
Populate `cameras.one_rs`, `cameras.go3s` and `cameras.hero12` at each corresponding
source slot with the actual profile qualifications from
[ONE RS](one_rs_profile.md), [GO 3S](go3s_profile.md) and
[HERO12](gopro_plan.md). Supply a distinct opaque event ID for every source slot.
No per-model singleton is independently ticked within the shared application.

Optional X5 wake uses `cameras.x5_wake` and `cameras.x5_wake_policy`, matching the
source wake identifier and the [existing evidenced profile](x5_serial_milestone.md#optional-bounded-wakerecovery-9).
Optional ONE RS GPS uses `cameras.gps[slot]` and source `gps_telemetry=true` with
the [source-backed forwarding qualification](gps_forwarding.md). The mixed owner
feeds GNSS snapshots into the same ONE RS command transport/sequence and gives
control priority. Unsupported X5/GO 3S forwarding is refused explicitly.

For live dynamic diagnostics, set `telemetry.runtime.dynamic_motion.enabled`,
`dynamic_cadence_us` alongside actual IMU calibration, mount and sample qualification.
Supply matching `telemetry.metadata` mount/calibration qualification states and
IDs, known residual offsets and positive gain ratios, with acceleration/gyro
compensation flags set to the delivered disabled-compensation profile (1).
Incomplete or mismatched enabled motion configuration refuses provider startup.
The bench image supplies a one-shot reference source when `dynamic_reference` is
omitted. While the fixture is stationary, send `STATIONARY` and hold it stationary
until a qualified sample arrives within 1000 ms. Earlier buffered samples are
rejected; a later receipt expires the declaration. Each later window needs a new
explicit declaration. A custom
retained `dynamic_reference` may be supplied instead. Never declare stationary
while riding. MotionV5 records
Unreliable numerical outputs and invalid/horizon/gap results; this is an
experimental testable implementation, not validated continuous motorcycle lean.
See [live diagnostics](dynamic_motion_estimator.md).

The private-branch compile environment `bench_application_private_compile`
always declines commissioning. It checks linkage and memory of the complete
activation branch without fake identities, proofs or card namespaces. Use
`bench_application` with the actual ignored provider for the physical session.

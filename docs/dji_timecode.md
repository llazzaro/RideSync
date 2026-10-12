# DJI Osmo timecode investigation

Issue [#52](https://github.com/llazzaro/RideSync/issues/52), investigated October
12, 2026. **Decision: unresolved for BLE native timecode synchronization on each
of Action 4, Action 5 Pro and Action 6. Osmo 360 native capability is separately
unestablished.** No timecode command or clock-setting feature is enabled by this
investigation. This is a completed source-backed investigation finding, not
delivery of the conditional software feature or physical compatibility.

Evidence here is **Source/documentary**, including third-party source authors'
reports. RideSync has no **Observed** or **Confirmed** timecode result for these
models, no fitted firmware version, and no achieved alignment or drift bound.
The existing [DJI recording profiles](dji_camera_profile.md) remain Experimental
/ Not tested. [Acceptance policy](acceptance_policy.md) permits a substantive
unresolved investigation without pretending dependent implementation is complete.

## Native capability, assessed independently

| Exact profile | Primary model evidence | Firmware evidence / remaining unknown |
|---|---|---|
| Osmo Action 4 (`DJI_ACTION4`) | [DJI manual v1.0, p.17](https://dl.djicdn.com/downloads/DJI_Osmo_Action_4/UM/20230803/DJI_Osmo_Action_4_User_Manual_v1.0_en.pdf) documents system-time and USB-C generator synchronization. | Manual identifies the model, not a minimum compatible firmware. Installed version unknown. |
| Osmo Action 5 Pro (`DJI_ACTION5_PRO`) | [DJI manual v1.0, §4.5, p.22](https://dl.djicdn.com/downloads/DJI_Osmo_Action_5_Pro/UM/20240919/DJI_Osmo_Action_5_Pro_User_Manual_v1.0_en.pdf) documents reset, system-time synchronization, USB-C generator and live-view display. | No BLE timecode firmware contract established. Installed version unknown. |
| Osmo Action 6 (`DJI_ACTION6`) | [DJI manual v1.0, p.18](https://dl.djicdn.com/downloads/DJI_Osmo_Action_6/UM/1104/DJI_Osmo_Action_6_User_Manual_v1.0_en.pdf) independently documents those workflows. | No BLE timecode firmware contract established. Installed version unknown. |
| Osmo 360 (`DJI_OSMO360`) | [DJI support](https://www.dji.com/support/product/360) and [release notes](https://dl.djicdn.com/downloads/Osmo_360/20260224/DJI_Osmo_360_Release_Notes_en.pdf) establish a separate product/firmware history; neither supplies a native timecode contract in inspected material. | Notes identify V01.03.08.70 on February 24, 2026; this is documentary version evidence, not the installed version or proof of timecode support. |

DJI's [timecode help article](https://repair.dji.com/help/content?customId=01700007306&lang=en&paperDocType=ARTICLE&re=US&spaceId=17)
lists all three Action models, but not Osmo 360. Mimo synchronizes system time;
the user then triggers timecode synchronization on the camera. DJI warns of
residual error and recommends an external generator for precision. USB-C
generators are documented; Mic 3 receiver workflow explicitly includes Action 5
Pro/6, not Action 4. The article inconsistently says unavailable at “60fps or
above” and “above 60 fps”; use a documented lower rate, such as 25 fps, for the
initial check and leave the 60 fps boundary unresolved. It describes 25/50 fps
counting and resets on firmware update/manual reset/24-hour rollover; none
provides a BLE API or accuracy guarantee.

DJI's [pre-recording article](https://repair.dji.com/help/content?customId=01700008789&lang=en&paperDocType=ARTICLE&re=US&spaceId=17)
explicitly excludes timecode in pre-recording on Action 4/5 Pro/6. Its Action 5
Pro version 01.02.03.10 qualifier concerns pre-record duration, not minimum
timecode firmware. Do not convert it into a timecode compatibility threshold.
Other video modes, DF/NDF selection and installed firmware still require exact
model evidence. Osmo 360's omission is insufficient to claim universal
infeasibility; native workflow, saved media and any synchronization interface
must be established independently before considering implementation.

## Official BLE route

The reviewed official HEAD was identical to RideSync's pin:
`92fe23e5a749f189593f980a26a105c3bb66aa1c`.
[Protocol](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/92fe23e5a749f189593f980a26a105c3bb66aa1c/docs/protocol.md)
uses `0xAA` framing with CRC16/CRC32;
[BLE source](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/92fe23e5a749f189593f980a26a105c3bb66aa1c/ble/ble.c#L83)
writes FFF5, not FFF3. The
[published command catalog](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/92fe23e5a749f189593f980a26a105c3bb66aa1c/docs/protocol_data_segment.md)
covers connection, version, keys, restart, GPS, mode, recording, status and power;
it supplies no timecode-setting or RTC-setting contract.

The nearest time-bearing command is **00/17 GPS Data Push**, a 48-byte GPS
payload: signed 32-bit date at offset 0 (`year*10000+month*100+day`), signed
32-bit time at offset 4 (`(hour+8)*10000+minute*100+second`), then coordinates,
height, velocities, accuracies and satellites. The schema has whole-second
resolution and no timecode frames, DF flag, fractional second or apply-at epoch.
The response is a one-byte return code; acknowledgement is optional. The
published purpose is GPS data, not camera RTC or native timecode synchronization.
Ten updates per second do not give its time field subsecond resolution, nor
prove native timecode follows it. No guessed use of 00/17 is authorized.

Official upstream [issue #13, Setting clock and Time Code](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/issues/13)
asks exactly whether Bluetooth can set the clock and synchronize timecode.
GitHub API inspection on this investigation date found it **open**, with one
[May 11, 2026 follow-up](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/issues/13#issuecomment-4423943815)
asking DJI for an update and no vendor command answer. This is a useful precise
evidence request, not proof that the camera cannot have an undocumented route.

## Existing GitHub solutions checked

Search was bounded: web queries for DJI Osmo Action BLE timecode/clock sync and
GitHub timecode/setTime, followed by cloned source inspection of these five
repositories. Search terms included `timecode`, `time.code`, `sync.?time`,
`set.?time`, `clock.?sync` and `date.?time`. Pins below identify inspected code;
this is not an exhaustive assertion that no solution exists anywhere.

| Candidate and inspected pin | Relevant result |
|---|---|
| [DJI official demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/tree/92fe23e5a749f189593f980a26a105c3bb66aa1c) | Working-route reference for recording/GPS protocol; no documented native timecode command. |
| [KonradIT/osmosis](https://github.com/KonradIT/osmosis/tree/6992036abc29a01126a728443f1d29a83a7ef467) | Actual RTC-setting implementation, but on the Wi-Fi media datalink; no qualified native timecode synchronization. Detail below. |
| [yigitkonur/lib-osmo-ble](https://github.com/yigitkonur/lib-osmo-ble/blob/021e96c2bec7e9a2a81296292545bc1ee432af49/PROTOCOL.md) | Reverse-engineered DUML BLE `0x55` framing/FFF5, pairing and camera/gimbal commands. No native timecode setter found in inspected source. |
| [datagutt/node-osmo](https://github.com/datagutt/node-osmo/tree/cec92aec9304a5cc3dae7f7de541eef38ebb680e) | BLE camera library; no native timecode setter found in inspected source. |
| [dstrat28/action-multicam-remote](https://github.com/dstrat28/action-multicam-remote/blob/b89cd0022e5a50b357a7fdbab154b9a5ff037e21/ActionCamRemote/Bluetooth/DJIExperimentalBLEClient.swift) | Multicamera BLE recording/GPS implementation; no native timecode setter found in inspected source. |

Osmosis is a concrete clock-setting lead, rather than a timecode solution.
[CameraSession.kt](https://github.com/KonradIT/osmosis/blob/6992036abc29a01126a728443f1d29a83a7ef467/app/src/main/java/dev/konraditurbe/osmosis/camera/CameraSession.kt#L2234)
builds `00/6A`, receiver `0x28`, with `01 00`, Unix seconds as `u64-LE`, signed
UTC-offset minutes as `i16-LE`, `u8` timezone-name length and ASCII IANA name.
Whole-second serialization discards milliseconds. Its
[protocol description](https://github.com/KonradIT/osmosis/blob/6992036abc29a01126a728443f1d29a83a7ef467/MEDIA_PROTOCOL.md#L821)
reports first response byte `00` as OK and says receiver `01` silently drops it.
However the implementation performs a best-effort drain/transport ACK and logs
time synced without independently establishing native timecode or accuracy.

The actual call occurs in
[fetchFileList after datalink registration](https://github.com/KonradIT/osmosis/blob/6992036abc29a01126a728443f1d29a83a7ef467/app/src/main/java/dev/konraditurbe/osmosis/camera/CameraSession.kt#L235),
and the class describes UDP/Wi-Fi/network binding. A `0x55` DUML RTC command on
that transport is not an interchangeable `0xAA` BLE command. Its presence does
not establish BLE transport, applicability to each Action model/firmware, a
timecode-refresh command, or saved-footage alignment. Wi-Fi remains outside #52's
conditional BLE scope. The same protocol document names `timecode_info` and
`audio_timecode_status` among Mimo subscriptions; names alone specify neither a
setter nor decoded semantics. No third-party code is copied into RideSync.

## Exact evidence needed to change the decision

For **each** Action model independently, obtain either a DJI SDK contract or an
annotated, reproducible capture demonstrating the relevant BLE operation:

1. Exact model, camera firmware, Mimo version, GATT characteristics, pairing and
   framing; identify whether clock setting and native timecode refresh are
   separate commands. Capture both directions around one explicitly annotated
   clock-set/refresh action, with ordinary background traffic separated.
2. Command set/ID, sender/receiver, payload byte order, epoch/timezone, units,
   frame rate and DF/NDF representation, precision and effective apply time.
   Supply matched response sequence, result/error semantics and any timecode
   readback; distinguish local submission, camera ACK and observed timecode.
3. Applicable idle/video modes and firmware, pre-rec rejection, behavior on
   invalid source time, disconnect, timeout, sleep/power cycle and restart.
   Include at least a sample saved file whose native timecode changed as
   expected, rather than merely a changed date/file timestamp.

For Osmo 360, first obtain model-specific native timecode evidence at all
(vendor manual/API or observed menu and saved-file timecode); do not inherit
the Action findings. Undocumented fields are not enabled merely because a
different camera accepts a packet. Future code would need an opt-in bounded
operation, valid/fresh time source, idle-only fresh request, finite timeout and
cancellation, honest Unknown/ACK/observed states, and no replay after reconnect.
Recording/local logging must continue independently. These are requirements
for a later evidenced design, not an implemented feature in this document.

## Finite handoff and verification

The investigation stops at this unresolved decision; unavailable captures do
not create an endless firmware or hardware prerequisite. If a usable BLE route
is established, implement only that evidenced per-model operation with focused
protocol/error tests, formatting, the affected pinned build and CI. This change
is documentation only, so unchanged firmware evidence is reusable.

[#46](https://github.com/llazzaro/RideSync/issues/46) schedules execution and
[#22](https://github.com/llazzaro/RideSync/issues/22) owns compatibility. For each
available, qualified model declare the alignment target and independent
reference before testing, record exact firmware/recording and timecode rates,
DF/NDF and source clock, then run three fresh pre-recording sync attempts and
inspect native timecode in saved footage. Check intended editor/export
preservation; measure initial offset and drift over one declared 30-minute
recording; check one reconnect and one sleep/power cycle and whether fresh sync
is required. Label unavailable stages Blocked, not passed. BLE writes, clock
timestamps and ACKs are not saved-footage accuracy results.

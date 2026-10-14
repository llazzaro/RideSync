# GO 3S ESP32 pairing and recording probe — October 14, 2026

Current result: an isolated ESP32 diagnostic authorized three connections and
received successful replies for four separately requested REC/STOP pairs, followed
by one successful preloaded short cycle. The owner confirmed actual recording
start and stop for the first cycle and the later short cycle. Further camera observations and playable-footage checks remain pending. This is
experimental probe evidence, not production-adapter qualification.

## Initial scope and result

The owner requested testing the powered GO 3S without an Action Pod, using the
mobile app for possible control approval. The owner reported updating the camera
that day; the installed firmware number was not read. The owner explicitly
requested proceeding without further version checks.

**Observed on the ESP32:** a unique GO 3S advertising-name candidate, successful
BLE connection, BE80 service, BE81 with properties 14 (Read/Write/Write Without
Response), and BE82 with property 16 (Notify). Negotiated MTU was 128. One
subscription session received a complete camera-to-controller FFFrame SYNC with
valid CRC16/MODBUS. These are discovery/handshake observations, not qualified
recording support.

**Not established:** authorization, REC/STOP, playable footage, production
security/bond commissioning, recording-state decoding or compatibility acceptance.
No REC or STOP serial command was sent. Physical recording checks in #46/#22
remain pending.

## Finite observations

1. The initial discovery probe connected and discovered the endpoints. The link
   remained connected for roughly one minute before explicit disconnect.
2. Subsequent connection attempts ended with NimBLE reason 531 before the
   expected connection callback; the probe timed out and required reset. The
   cause was not isolated. No control command was replayed.
3. After the owner power-cycled the camera, connection and discovery passed.
   Subscription received a 17-byte FFFrame SYNC. The probe sent the source-backed
   seven-zero sync response and one CheckAuth request with sequence 1 and an
   explicit diagnostic controller ID.
4. A 24-byte camera-to-controller message had correct outer length and CRC, but
   its header did not satisfy the existing qualified response decoder. Its code
   at the response/status offset was 0x205f and its low sequence byte was 255,
   not the requested sequence 1. It is retained privately as an unidentified
   message, not interpreted as an authorization ACK or a rejection. The first
   control diagnostic stopped on that packet and disconnected.
5. A revised diagnostic retained unidentified packets without treating them as
   ACKs, allowing the bounded wait to continue. A new connection/discovery passed,
   but no BE82 SYNC arrived within two seconds or within one second after the
   single zero-byte prompt. No CheckAuth or recording command was sent in this
   attempt. The phone/app state differed and was not isolated as the cause.

The owner explained that the app was open to approve control. The exact app
screen and approval route remain unresolved. App remote-control pairing should
not be assumed equivalent to the direct camera BE80 central route tested here.
No RequestAuthorization (0x56) trial was performed.

## Probe and preservation evidence

Testing used the fitted ESP32-WROVER-E board over its USB-C serial port and one
continuous PassiveSerial owner per capture. The temporary probe was prepared
privately from the existing audited HERO12 central startup and NVS refusal
wrappers, using the repository's GO 3S codec. It is an isolated protocol diagnostic,
not an activation of the production GO 3S runtime or a commissioned production
configuration. Production code and its security requirements were not changed.
The observed direct GATT connection was unencrypted and unbonded; the probe did not request SMP bonding
or bond deletion. Camera-side authorization persistence was not verified. Unknown recording state was
retained throughout.

Repository baseline: `f3e89a7e4cd7b0edbb7d4b926019948956bebc6d`.
Final temporary capture-probe firmware SHA256: `71e5baaf24740721fbea6c9a0619b2c10fff94230ff3e04539e25ce1e1fe1e0b`.

Pinned PlatformIO builds and ELF audits passed for the temporary images: strong
BLE HAL anchor, RAM-only store, empty persistence object, and NVS/PHY refusal
wrappers. Before each upload a retained/reconstructed complete 4 MiB image
verified against the device. Partition-table comparison passed. Each upload used
the existing four standard regions; independent region verification passed.
NVS at 0x9000, length 0x5000, matched the original baseline after uploads and
again after the final session. No full erase was issued. Full flash/NVS,
transcripts and temporary probe source remain private outside Git. The final
GO 3S probe is installed and was reset to idle after the preservation readback;
it performs no automatic connect or recording command.

## References and next bounded test

- [Existing GO 3S profile](../go3s_profile.md) and
  [finite hardware procedure](../testing.md#go-3s-source-backed-profile-verification-21).
- [Pinned community GO 3S protocol](https://github.com/OpenGraphLabs/syncfield-python/blob/88a74d84e09602d73c0e8a77f0fe583beb33cb61/src/syncfield/adapters/insta360_go3s/ble/protocol.py).
- [Pinned community direct-camera example](https://github.com/nicecx/insta360-ai-content-studio/blob/abf472566198aeefb493b742f3fe69d9132ff460/lib/camera_remote.py).
- [Upstream protocol documentation](https://github.com/xaionaro-go/insta360ctl/blob/main/doc/ble_protocol.md)
  was consulted for authorization context; that mutable document is not a new
  qualified implementation profile or proof of the unidentified packet's meaning.
- [Retained flashing and backup procedure](../device_connection.md#reusable-backed-up-probe-flashing-procedure).

Resolve which app control-approval screen is in use before selecting another
protocol route. For a further BE80 trial, capture a fresh complete handshake
without mistaking unsolicited messages for ACKs. Recording remains gated on a
valid correlated authorization response; then perform independently requested
REC/STOP with owner-observed behavior and playback. This report passes none of
those pending checks.

## Later CE80 remote-search trials

The owner requested preparing the controller with advertising off before powering
on the battery-limited GO 3S. The existing `x5_peripheral` CE80 diagnostic was
built/audited, installed with the verified retained backup procedure, and all
four upload regions verified. NVS comparisons passed after both trials. No L,
W or S command was selected; no legacy allowance, explicit wake advertisement
or shutter command was used. Private H capture was enabled before A.

The first A accepted an unidentified peer in about 0.64 s, negotiated MTU 23,
and disconnected about seven seconds later with reason 520 (supervision timeout).
No characteristic read, write or subscription was captured. The owner reported
that the ONE RS powered on. Its reaction is owner-observed; the connected peer's
identity was not verified. This attempt is **not GO 3S connection evidence**.
The probe was stopped and reset.

For the next attempt, the owner was instructed to remove/disconnect the ONE RS
battery and then confirmed searching. The probe advertised as `Insta360 GPS
Remote` with its default CE80 profile; the ESP32 reported advertising active,
no connection, and no dropped or truncated events. The owner reported that
nothing appeared in the GO 3S app remote-search screen. The attempt was stopped
explicitly after about 89 s to conserve camera battery. This does not establish
that all remote profiles are unsupported, or isolate an app/Pod prerequisite.
The CE80 probe remains installed, reset idle with advertising off. The earlier
statement about the final BE80 probe describes that earlier phase only.

See [subsequent pinned GitHub research](../go3s_pairing_research.md) for distinct
handshake and phone-authorization leads. Those sources do not turn either
failed local trial into successful control.

## Proactive direct SYNC and authorization check

The owner next powered the GO 3S on in normal idle mode. The new direct ESP32
probe, based on the pinned Kotlin handshake, connected and rediscovered BE80,
BE81 and BE82. MTU negotiation returned 247. Immediately after subscribing, it
wrote ten alternating AB/BA bytes; a CRC-valid 17-byte camera SYNC followed.
A CheckAuth request then received a valid, sequence-1-correlated status-200
response. Its protobuf body contained field 1 varint value 1.

The subsequently reviewed [pinned authorization schema](https://github.com/xaionaro-go/insta360ctl/blob/f94193ce03c5af0921a9992bfd1af6bd946150d0/pkg/protocol/protobuf/authorization.proto)
identifies that response value as Authorized. This is positive camera
protocol authorization evidence in the isolated, unencrypted/unbonded diagnostic.
There was no owner-observed approval prompt, no REC/STOP command, and no claim
of production security qualification or recording support.

After the check returned, the temporary probe's idle loop failed to drain its
notification queue. The queue overflowed; a later summary recorded 98 drops.
Those missing events cannot be reconstructed or used as state evidence. The
link later ended with reason 531. The owner was told the camera could be turned
off while preparing a corrected diagnostic. NVS readback again matched the
retained baseline. This is a probe defect, not evidence of a camera defect.

The pinned schema also identifies initiator_type as field 3, whereas the older
community packet builder used field 2. The just-observed request therefore did
not establish the intended user-initiated field, despite its positive reply.
The prepared replacement uses field 3, drains notifications continuously,
requires the exact observed positive authorization body as well as the correlated
ACK before manual control, and uses source-backed idle CheckAuth keepalive.
Native literal-packet, wrong-state/sequence, CRC and truncation checks passed;
its physical behavior still requires the next run. The production GO 3S codec
and adapter have not yet been changed based on these experimental results.

## Controlled recording run after the receive fix

The owner powered the GO 3S on in normal idle mode. No app GPS Remote search was
used for the direct BE80 route. The corrected probe performed immediate
proactive SYNC, negotiated MTU 247, used the stable private controller ID with
initiator field 3, and required a matching status-200 response with the observed
Authorized payload before accepting manual REC/STOP.

The initial connection and authorization passed. For each REC intent the probe
sent normal-video options once and StartCapture once; for each STOP it sent the
explicit Stop command once. All four separately requested pairs received valid
sequence-correlated status-200 replies. The extra fourth pair was requested by
the owner after they had not watched the later checks; it was not an automatic
retry. No write or shutter intent was replayed after reconnect.

| Check | ESP32 protocol result | Owner observation |
| --- | --- | --- |
| Cycle 1 REC then STOP | Both successful | Owner separately confirmed actual recording start and stop |
| Cycle 2 REC then STOP | Both successful | Not confirmed; owner requested another attempt |
| Cycle 3 REC then STOP | Both successful | Not confirmed; owner requested another attempt |
| Explicit disconnect/reconnect | Connection, proactive SYNC and authorization successful; no REC/STOP sent on reconnection | Owner reported not watching; camera no-replay observation not confirmed |
| Owner-requested repeat REC then STOP | Both successful after another explicit connection/authorization | Awaiting owner observation |
| Clip playback | Not inspected by ESP32 | Not confirmed |

There were three successfully authorized connections in this run. Every captured
status summary reported failed=0 and drops=0, in contrast with the earlier idle
queue-overflow diagnostic. Continuous notification draining and CheckAuth
keepalive ran while connected. Known recording-state decoding was not enabled:
all protocol ACKs and software summaries still reported recording UNKNOWN.
Camera-observed state above is an independently reported owner observation.

Controlled probe firmware SHA256: `db8a62d5f4f9b6e00d90fc297432119c16e6e612f74a958296016f122c6dbf4a`.
This remains a temporary, privately retained diagnostic built from the existing
pinned central startup/NVS guards and independently authored source-derived
codec helpers. Literal authorization-tag, positive-state gate, wrong-sequence,
wrong-state, CRC-mutation and truncation checks passed; the image's ELF audit,
all four uploaded region checks and post-session NVS byte comparison passed.
After the run the ESP32 connection was explicitly released and the board reset
to idle during NVS readback. It sends no automatic connect, wake or shutter
command. Private controller IDs, notification payloads, filenames and full
flash/NVS are excluded from this report.

This establishes an observed experimental GO 3S REC/STOP path on the ESP32
probe. It does not pass the whole finite GO 3S checklist: three camera-observed
cycles, camera no-replay observation, playable clips and production security/
commissioning remain unresolved. The production adapter was not activated or
changed during these physical tests. Reuse this evidence for the next phase
rather than restarting discovery or repeating firmware-version questions.

## Owner-confirmed preloaded short cycle

After the owner requested minimizing camera-on waiting time, the host prepared
and opened the entire short-test coordinator while the camera was off. Before
arming, its native host checks covered the authorization gate, one-cycle limit,
five-second recording interval, whole-run timeout, and rejection of late ACK
restart or duplicate STOP. The firmware and prior NVS preservation evidence
were reused; no new upload or camera command was needed for this preparation.

The owner then confirmed the camera was on. A single GO triggered exactly one
CONNECT, one REC, one STOP and one DISCONNECT, with no intermediate questions or
automatic retry. Connection/SYNC/authorization passed, both control commands
received matching success replies, and disconnect completed in 21.47 seconds
from CONNECT submission. The owner separately confirmed observed recording start
and stop with “yes works.” This is a second camera-observed successful cycle,
in addition to the earlier confirmed first cycle. Clip playback, the third
camera-observed cycle, observed reconnect/no-replay and production integration
remain unconfirmed. The short-test process exited after disconnect; no camera
operation remains scheduled or running.


## Targeted wake advertisement: observed failure

The owner requested power-off wake testing after confirming the short recording
cycle. The retained, previously audited CE80 peripheral image was restored only
after full on-device verification of the reconstructed current central backup.
Its four upload regions were independently verified and NVS remained identical
to the original baseline. The owner supplied the GO 3S's six-character camera
identifier; it was loaded privately into RAM using W, with wake-only mode
confirmed before asking for the observation window. No camera identifier is
included in this report.

One explicit GO submitted A. Advertising ran from boot time 318.812 s to
321.831 s, then stopped at its finite deadline. All 16 events were reported,
with zero dropped/truncated events, no connection, and no shutter operation.
The owner answered “no” when asked whether the camera powered on. This is an
observed failure of this particular source-derived three-second wake packet on
GO 3S, not evidence that GO 3S lacks Bluetooth wake. Post-session NVS readback
matched the preserved baseline byte for byte.

The owner then confirmed that the Insta360 app could wake the powered-off
camera. Official [app wake instructions](https://onlinemanual.insta360.com/app/en-us/operation-tutorial/camera-connection/bluetooth-wakeup)
describe tapping Connect for a previously connected GO-series camera on either
iOS or Android. The pinned Swift source exposes specific wake through the
vendor SDK and also writes SDK-generated wake authorization data; its six-byte
identifier encoding alone does not document the transmitted wake packet.
Therefore, the successful X5/ONE RS wake layout is not qualified for GO 3S.

A preliminary five-second passive Mac scan observed one distinct GO 3S candidate
matching the owner's identifier. This is discovery evidence only; it does not
prove ESP32 wake or that a BLE connection powers on the camera. The next
prepared hypothesis test reuses the exact previously successful central image
and attempts CONNECT/authorization followed by DISCONNECT, with no recording
command. The subsequent result is recorded below.


## CONNECT-only wake: observed failure

The exact previously successful central image was restored after verifying its
retained predecessor backup. All four written regions verified independently,
and pre/post-session NVS matched the original baseline. The preloaded host
accepted one GO and sent exactly CONNECT then DISCONNECT. The ESP32 discovered
one unambiguous GO 3S candidate, connected successfully, discovered BE80 and
subscribed to BE82. It wrote the ten-byte proactive SYNC and received one
six-byte notification that did not constitute a valid SYNC response. After the
five-second protocol timeout it sent no authorization request and disconnected.
No REC/STOP command was sent. The owner answered “no” to actual power-on.

This establishes that BLE connectivity to this powered-off camera does not by
itself wake it under the tested sequence. It does not test the special wake
packet identified by the subsequent source review. The ESP32 was reset to idle
by the final NVS read; no camera operation remains running or scheduled.

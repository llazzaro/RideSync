# Read-only setup commissioning report

Issue #80 adds an allocation-free copied prerequisite report. The serial setup
owner publishes `P1` payloads through the #76 `REPORT` response. The report omits
camera labels, addresses, firmware bytes, store digests/receipts and DJI controller
or challenge values. Model names and guidance come from a fixed local table;
user names must be escaped by the separate preferences presentation.

## Evidence and readiness

Settings validity, runtime group support and provider state are independent.
A saved preference cannot establish identity, restore a bond or qualify firmware.
The group supports at most four slots/capacity including disabled entries; X5
must occupy slot zero. Invalid preferences refuse; unsupported groups remain
intact and report unsupported. Disabled cameras refuse runtime admission.

Each slot carries identity, store/restore, selected profile, installed firmware,
API, requested wake/GPS prerequisites and actual runtime admission. Evidence is
`unknown`, `missing`, `matched`, `mismatched`, `revoked`, `unsupported`, `refused`,
`not_requested`, `denied` or `timeout`. Readiness is `unknown`,
`needs_commissioning`, `unsupported_profile`, `refused` or `ready`.

`setupQualificationEvidence` accepts only the existing private provider's
reviewed qualification objects. Printable declared firmware and declared API stay
unknown until observed; malformed or incompatible source firmware is unsupported.
It copies status only and performs no camera,
store or peripheral operation. A receipt being present leaves actual restoration
unknown: the independently reviewed proof/restore owner must supply matched,
mismatched or revoked. Changed store requires renewed independently reviewed
proof. Runtime admission stays unknown until the actual adapter owner supplies
its observed outcome. HERO12 actual returned model/firmware/API mismatch overrides
reviewed declarations. DJI grant denial/timeout is a refusal; DJI application
authorization does not assert SMP, encryption or bond restoration. Never feed
checkboxes, candidate names, minimum-version references, ATT delivery or bond
enumeration into these evidence fields.

No qualification setter, security handshake, bond creation/deletion, recording
command, SD namespace creation or configuration reset is exposed. Targeted bond
maintenance remains the existing separate verified-identity workflow. Missing
prerequisites never trigger maintenance automatically.

## Manual next steps by model

- **X5:** camera initiates the CE80 peripheral route from its remote UI. Have the
  evidence owner review stable identity, independent store receipt/revocation,
  exact installed firmware and captured-display premises. Observe actual CE82
  subscription. First-time bond creation remains manual/unresolved; BE80 scan
  advertisements do not qualify this route.
- **GO 3S:** select the camera, not the Pod. Record camera and Pod firmware
  separately. Retain approved pairing, verified stable identity and guarded
  restoration. Observe MTU, subscription, SYNC and successful CheckAuth separately.
  The current source route expects reviewed camera firmware `8.0.4.11`; its
  reference alone is not an installed observation. Recording stays Unknown.
- **ONE RS:** use camera-side remote selection with reviewed identity/bond,
  installed firmware, core, ordinary 360 lens and video premises. This BE80 route
  remains Experimental / Not tested; universal third-party authorization and
  observed recording remain unresolved. ATT delivery supplies no recording state.
- **HERO12 Black:** approved pairing/bond, management pairing-finish and external
  control claim must succeed. Actual Hardware Info model/exact firmware and API
  must match the reviewed profile. Bond alone or a documentary minimum version
  cannot clear setup admission.
- **DJI Action 4/5 Pro/6 and Osmo 360:** review exact installed firmware, stable
  identity, separately commissioned controller identity/challenge and the
  unencrypted protocol boundary. A new pair requires correlated camera-screen
  grant; declared prior pairing is separate. Denial/timeout leaves refusal. No
  universal minimum firmware, SMP/encrypted-channel or Bonded claim is made.

See [guided setup design](guided_camera_setup.md) and the existing per-model
profile documents for the evidence owner's full commissioning procedure.
Physical acceptance remains #46/#22, #30 and #31; synthetic reports/tests do
not promote camera support or establish installed hardware qualification.

## Bounded wire contract

`P1;settings,group,provider,overall,count,truncated` is followed by `count`
semicolon-separated rows:

`slot,model,enabled,readiness,identity,store,profile,firmware,api,wake,gps,runtime`

All values are unsigned decimal enum codes from `setup_prerequisites.h`; model
codes are the existing `CameraModel`. Evidence codes are 0 through 9 in the order
listed above. Provider codes are missing=0, declined=1, available=2, refused=3,
unknown=4. Readiness codes are unknown=0, needs_commissioning=1,
unsupported_profile=2, refused=3, ready=4. Count is bounded to eight; truncation is
explicit. Encoder failure clears the output and returns zero. Consumers reject
unknown versions, malformed/out-of-range values, duplicate slots and count
mismatches. There are no arbitrary strings or credentials in this report.

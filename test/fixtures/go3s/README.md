# GO 3S evidence fixtures

Status: **no local fixture captures are available**. GO 3S source-derived
request expectations and synthetic response fixtures now live in
`test/test_go3s_codec/test_main.cpp` and `test/test_go3s_adapter/test_main.cpp`.
They are independently authored from the pinned Community sources in the
[profile contract](../../../docs/go3s_profile.md), not captured camera bytes.
The literal Start/Stop/sync vectors include independently calculated
CRC16/MODBUS; synthetic ACKs do not qualify observed recording state. The fake
host operates the real codec/adapter/central/manager; no identity or status
fixture is presented as a fitted device observation.

When evidence is collected, add only the minimum reproducible public material:
annotated capture (prefer a documented text export when sufficient), capture
tool/version, test notes, and SHA-256 for any retained raw file. Preserve raw
captures privately when they contain unique identifiers or pairing material;
never commit keys, credentials, Bluetooth addresses, serial numbers, camera
names containing serial suffixes, Wi-Fi credentials, or precise location/time
metadata. Replace identities consistently with `camera-A`, `pod-A`, and
`controller-A`; document every redaction and avoid editing packet bytes except
to redact identified fields. Keep a private mapping separately from the repo.

Record model and exact camera and Action Pod firmware versions, test date,
pairing prerequisites, capture equipment/software, each manual or official
remote action, observed UI/indicator state, and saved-media confirmation.
Clearly distinguish `Official`, `Community`, `Observed`, and `Hypothesis`
claims. A public fixture must include its origin and license/provenance; official
manual text is linked, not copied. Do not include packet material from an
unlicensed community source. See [source inventory](../../../docs/sources.md)
and the [finite hardware verification procedure](../../../docs/testing.md#go-3s-source-backed-profile-verification-21).

Source-backed Start/Stop/normal-video/auth command tests do not require local
hardware captures. Actual GO 3S recording-state fields remain unqualified; no
state/timer heuristic is enabled. Synthetic test authorization labels are not
credentials. No source-derived packet example or upstream implementation is
copied from the unlicensed insta360ctl repository. The new codec/adapter are
independently authored, and the root MIT/Apache references are linked as
provenance rather than treated as relicensing inherited code.

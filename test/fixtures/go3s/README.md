# GO 3S evidence fixtures

Status: **no fixture captures are available**. This directory intentionally
contains no synthetic BLE packet, fabricated device identity, or unverified
command example. The issue #6 investigation is unresolved because target
hardware and local captures were unavailable.

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
and the [bounded capture procedure](../../../docs/testing.md#go-3s-and-action-pod-evidence-protocol).

Until an actual capture passes that procedure, there are no start, stop, toggle,
mode or state fixtures to test against.

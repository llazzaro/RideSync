# GoPro documentary fixtures and provenance

Research date: 2026-10-07. Official upstream checked out at
`0f963572611c4410a15678531e9681a6ff874edb`. None of these bytes is a RideSync
camera capture. No installed HERO12 firmware or API version is known.

`documentary_vectors.h` contains short wire facts transcribed from official
examples, independently exercised by `official_documentary_vectors_decode_end_to_end`.

| Fixture | Evidence class | Pinned source |
|---|---|---|
| Video group `04 3E 02 03 E8` | Official documentary golden | [Python tutorial, lines 37–42](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/tutorial/tutorial_modules/tutorial_2_send_ble_commands/ble_command_load_group.py#L37-L42) |
| Shutter on `20 03 01 01 01`; success `02 01 00` | Official documentary goldens | [Kotlin vectors, lines 58–62](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/kotlin/kmp_sdk/wsdk/src/commonTest/kotlin/vectors/bleByteData.kt#L58-L62) |
| Busy/Encoding registration replies and notifications | Official documentary goldens | [Kotlin vectors, lines 87–92](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/kotlin/kmp_sdk/wsdk/src/commonTest/kotlin/vectors/bleByteData.kt#L87-L92) |
| Keep alive response body `5B 00` | Official documentary payload; compact framing derived | [Kotlin vectors, line 62](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/kotlin/kmp_sdk/wsdk/src/commonTest/kotlin/vectors/bleByteData.kt#L62) |

Remaining inline literal test cases are separately classified:

- **Schema-derived:** shutter off; compact/extended alternate headers; settings
  keep alive `03 5B 01 42`; no-argument hardware/API requests; all Get/Register
  requests; Get replies; Ready 82 replies/notifications; combined status response;
  numeric errors; variable-length hardware/API response structure. The synthetic
  hardware body uses anonymous one-character fields, not an invented HERO12
  identity. Ready has no inspected raw upstream golden.
- **Synthetic adversarial:** fragmentation and counter sequences, interleaved
  peers/channels, malformed headers/TLV/boolean, unknown IDs, byte/packet/stream
  limits, truncation, reserved bits, zero progress, interruption, timeout,
  rollover and reset. No claim about actual camera fragmentation is made.

Pinned schema references:

- [Python commands, lines 121–188 and 198–213](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/api/ble_commands.py#L121-L188): hardware info, group **uint16**, API query.
- [Builder, lines 120–131 and 751–760](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/api/builders.py#L120-L131): no extra zero for absent parameters; query ID then status IDs.
- [Query IDs, lines 117–128](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/models/constants/constants.py#L117-L128): classic `13`/`53`/`93`.
- [Status IDs, lines 21–67](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/models/constants/statuses.py#L21-L67) and [boolean status schema](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/api/ble_statuses.py#L54): Busy8, Encoding10, Ready 82.
- [KeepAlive, lines 10–14](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/kotlin/kmp_sdk/wsdk/src/commonMain/kotlin/com/gopro/open_gopro/operation/commands/KeepAlive.kt#L10-L14) and [LED setting 91](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/models/constants/settings.py#L27).

Mutable official documentation separately accessed 2026-10-07:
[Data Protocol](https://gopro.github.io/OpenGoPro/docs/ble/protocol/data_protocol/),
[Control](https://gopro.github.io/OpenGoPro/docs/ble/control/),
[Query](https://gopro.github.io/OpenGoPro/docs/ble/query/).
These are date-qualified pages, not covered by the repository commit pin.
Hardware fields use their documented lengths (seven fields). Eleven reserved
bytes may trail the hardware payload; they are retained raw. Absence is accepted
because the documentation calls them outside the payload; partial tails reject.
API major/minor are checked as nonempty length-prefixed fields and retained raw,
without assuming a firmware/API value. The separate #36 identity extractor now
accepts only one-to-eight-byte big-endian unsigned model/API values and rejects
wider values; the original classic response decoder remains unchanged.

## Setup vectors (#36)

`setup_vectors.h` was authored 2026-10-08 from the pinned
[pairing schema](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/protobuf/network_management.proto#L201-L213),
[control schema](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/protobuf/set_camera_control_status.proto),
[generic response](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/protobuf/response_generic.proto),
and [pinned routing declarations](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/api/ble_commands.py#L384-L457).
All setup bytes are **synthetic schema-derived test inputs, not official raw
goldens or device captures**. The live official [reference matrix](https://gopro.github.io/OpenGoPro/references.json)
was independently fetched 2026-10-08, SHA256
`9670d448045f91f5a780e889a87723de0e4eacadd05afeffda9068d92827dc0c`.
It lists HERO12 model 62 and support for pairing and control; Mission-only
capability/two-byte operations are excluded.

| Synthetic vector | Bytes | Derivation |
|---|---|---|
| Pairing-finish request, compact | `0E 03 01 08 00 12 08 52 69 64 65 53 79 6E 63` | Management feature/action, required success enum zero, required nonempty eight-byte `RideSync` name |
| Pairing-finish request, extended-13 | `20 0E 03 01 08 00 12 08 52 69 64 65 53 79 6E 63` | Same payload, alternate packet header |
| External-control claim, compact/extended-13 | `04 F1 69 08 02` / `20 04 F1 69 08 02` | Command feature/action, required external-control enum two |
| Pairing success, compact | `04 03 81 08 01` | Generic result one on Management response |
| Control success, extended-13 | `20 04 F1 E9 08 01` | Generic result one on Command response |

The synthetic identity literals in tests use anonymous `HX`/`fw` bytes and
artificial API numbers. They do not identify any installed HERO12, firmware,
serial, MAC, or API. No SDK implementation, generated protobuf or schema file
is vendored; upstream root proto copyright has no blanket MIT grant.

## Conflicts and application policies

The Kotlin group encoder emits uint32, while its tutorial/Python command schema
and current Control agree on uint16 group 1000. The uint16 documentary golden
wins. Its transmitter also repeats `80` and permits outbound extended-16. Current
Data Protocol allows counters `80`..`8F`, and extended-16 is receive-only. Required
requests use extended-13 by default, with compact framing available. Reserved
bits reject and counters need not progress: these are explicit RideSync policies,
not firmware observations. Two-byte-ID profiles `16`/`56`/`96` are unsupported; qualify
capabilities on hardware before adding any alternative.

## License audit

No SDK implementation or dependencies were copied, linked or vendored. The
codec and tests are independently implemented from the above wire facts.
The upstream root [LICENSE](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/LICENSE)
is a component/third-party notice, not a blanket MIT declaration.
The [Python component LICENSE](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/LICENSE)
and [Kotlin component LICENSE](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/kotlin/kmp_sdk/LICENSE)
identify their SDKs as MIT, copyright GoPro 2021–2024, and list separate third-party
licenses. Short fixture byte strings are protocol facts, not copied SDK logic.
Future source reuse needs the specific component license and retained notices;
this audit does not authorize arbitrary reuse elsewhere in OpenGoPro.

# GNSS response provenance

`responses.h` contains only these response lines; no AT transport or unrelated
serial records are replayed.

| Fixture | Evidence | Hardware-qualified valid fix? |
| --- | --- | --- |
| `kManualCgpsinfo` | SIMCom A76XX Series AT Command Manual V1.09 (2023-04-27), §24.2.11 printed p.491 public example; field definitions p.490 | No: documentary example |
| `kObservedCgpsinfoNoFix` | [Factory probe](../../../docs/hardware-results/2026-10-07-bringup.md), 2026-10-07, A7670E-FASE, ATI A7670M7_V1.11.1, CGMR A110B01A7670M7_F | No: empty/no-fix |
| `kObservedCgnssinfoNoFix` | Same factory probe, exactly nine empty fields (eight commas) | No: empty/no-fix |

Primary source: [SIMCom V1.09 hosted by Waveshare](https://files.waveshare.com/wiki/A7670E-Cat-1-GNSS-HAT/A76XX_Series_AT_Command_Manual_V1.09.pdf).
The manual is © 2023 SIMCom Wireless Solutions Limited, all rights reserved;
it is not an open-source implementation license. Only the short public response
example is quoted here for interoperability testing; no manual PDF or vendor
parser code is copied. The observed empty lines contain no location or unique
device identifier. Synthetic edge cases in `test_gnss_parser` are authored test
inputs, not captures. Their date century interpretation (2000–2099) is our
explicit policy, not specified by the manual.

Manual-derived expected values: latitude 31.2223881°, longitude
121.3539010666667°, altitude 44.1 m MSL, speed 0 m/s, course 0°, and
2011-03-25 07:28:09.33 UTC. A synthetic 10-knot speed must become
5.144444444444444 m/s. CGPSINFO does not supply satellite counts or fix quality.
Nonempty CGNSSINFO fixtures exercise rejection only, never normalization.

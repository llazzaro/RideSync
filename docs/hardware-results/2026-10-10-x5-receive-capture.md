# X5 private receive capture — October 10, 2026

This is partial evidence for #19 and the first X5 capture row in #46, not
qualification of an authoritative recording-state decoder or production adapter.
The host transcript timestamps are October 9, 2026, 22:02–22:05 UTC (October 10
in Amsterdam). Repository revision at preparation was
`836394a645db2b6e39b182199a4311e04aa74f60`. The existing isolated CE80 probe was
reused without an upload; installed flash was not independently reread. Firmware
1.11.10 is the earlier owner report, not a new firmware-screen observation.

## Procedure and observations

The owner confirmed the camera powered on and stopped in video mode, with the
USB board connected. Serial was opened with DTR/RTS deasserted, private H output
was enabled, and one A started the existing 120-second window. No S, wake,
handshake, guessed response, upload, flash erase or bond maintenance was issued.
The private transcript remains outside Git. A host recorder retained serial
lines and operator-reply timestamps; those replies are not exact action times.

The camera connected at boot 19.137 s and subscribed to CE82 at 20.943 s.
Observed MTUs were 23 and then 251. GATT registration identified CE81 value
handle 16 and CE82 value handle 18. All 157 captured incoming writes used the
CE81 value handle. The owner was asked to start manually at approximately boot
55 s and confirmed the running timer at approximately boot 98 s. The actual
Start is bounded by that interval, not assigned to a packet timestamp.

| Incoming write length | Count | Observation |
|---|---:|---|
| 6 | 2 | Opaque; no state interpretation |
| 7 | 1 | Opaque; no state interpretation |
| 11 | 103 | One identical payload occurred 102 times across the capture; one differed |
| 12 | 1 | Opaque; no identity interpretation published |
| 15 | 7 | Identical payload, boot 25.173–65.568 s |
| 17 | 1 | Opaque; no state interpretation |
| 19 | 42 | Changing payloads, boot 81.978–123.003 s; each contains a colon |

All writes had the source-documented CE80-family `FE EF FE` prefix. The changing
19-byte stream overlaps the manually confirmed recording interval. This is a
candidate correlation only: length, colon presence and timer-like changes do
not establish authoritative recording, video mode, stopped state, freshness or
query semantics. The repeated 11-byte payload continued during recording; it
must not be treated as a stopped-state observation merely because it also
occurred before Start. No private payload bytes or identifiers are published.

At the window deadline the probe observed unsubscribe at boot 123.902 s and
actual disconnect at 123.904 s. Final reports showed 178 events seen/reported,
zero queued/dropped/truncated events, no peer, no pending shutter, no in-flight
SDK operation and four admitted/returned SDK operations. No shutter request or
result was recorded. NVS refusal counters were 1/2/0. The host recorder later
submitted a final X before closing serial; the earlier actual disconnect is the
cleanup evidence, not that submission return.

## Remaining checks

Manual Stop and a switch to photo mode were requested after the running-timer
reply. Their camera outcomes and timing were not confirmed within this capture.
Playback was not checked in this run. The first #46 capture row remains
incomplete: a bounded capture covering confirmed Stop, mode change and playback
is still required. Neither silence nor the deadline disconnect means stopped.
Keep #19/#3 open and software recording state Unknown. Reuse the previously
completed three remote Start/Stop cycles and isolated wake proof; this run does
not repeat or replace those checks.

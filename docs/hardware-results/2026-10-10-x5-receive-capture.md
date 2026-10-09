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
reply. The owner subsequently confirmed stopped, still in video mode, after the
capture had closed. Actual Stop timing remains unresolved and no mode change
was confirmed. The owner also confirmed that the new clip plays on the X5. At the end of this
first attempt, a bounded capture covering confirmed Stop and mode change was
still required; the follow-up below supplies that sequence with coarse timing. Neither silence nor the deadline disconnect means stopped.
Keep #19/#3 open and software recording state Unknown. Reuse the previously
completed three remote Start/Stop cycles and isolated wake proof; this run does
not repeat or replace those checks.

## Follow-up: completed manual sequence and photo-mode observation

The owner reset the USB board and again confirmed stopped video. A first setup
attempt connected/subscribed, but a reset during serial setup had cleared the
private H option. It was stopped with X before any manual camera sequence was
requested. Its length-only writes are not complete payload fixtures and are
excluded from the analysis below. No shutter was sent. The next serial session
observed a fresh idle boot and confirmed `hex=1` before submitting one A. This
was a fresh-boot diagnostic attempt, not a retry of a recording command.

On that second attempt, connection occurred at boot 48.602 s and CE82
subscription at 49.919 s, with MTU 251. The owner was asked, in one instruction,
to start manually, observe approximately ten seconds of running timer, stop,
then switch to photo mode. The owner replied “done. camera in photo mode” while
still connected, at approximately boot 110 s. After capture ended, the owner
also confirmed playback of the new video. These are camera-side observations;
there were no SDK shutter requests/results. The sequence request was recorded
at 22:09:19.457996 UTC and the completion reply at 22:10:32.702030 UTC. Individual
Start, Stop and mode-switch times remain intervals, not exact timestamps.

| Incoming write length | Count | Boot-time range (seconds) |
|---|---:|---|
| 6 | 3 | 51.801–98.511 |
| 7 | 3 | 50.796–51.216 |
| 9 | 1 | 50.046 |
| 11 | 92 | 50.226–132.307 |
| 14 | 7 | 103.146–126.367 |
| 15 | 11 | 54.111–101.616 |
| 17 | 3 | 50.436–97.371 |
| 19 | 28 | 80.902–102.726 |

All 148 CE81 writes were complete bounded copies with the `FE EF FE` prefix.
The 15-byte payload was identical across its eleven occurrences; the 14-byte
payload was identical across its seven occurrences, including samples after
the owner confirmed photo mode. Twenty-seven of the 28 nineteen-byte writes
contained a colon; the final one did not. An identical 11-byte payload occurred
81 times across the sequence. These distinctions are retained observations,
not a mapping from packet length or body content to authoritative state.
In particular, a nineteen-byte write is not necessarily the timer-like format.
No identity, token or raw private payload is published.

After retaining additional photo-mode traffic, X was submitted. Actual
unsubscribe occurred at boot 133.026 s and disconnect at 133.028 s. Final
reports showed 169 events seen/reported, zero queued/dropped/truncated events,
no peer, no pending shutter, no in-flight SDK operation, four admitted/returned
SDK operations and NVS refusal counters 1/2/0. The complete event sequence
1–169, CE81 handle, lengths/copy sizes, prefix, connection/disconnect and absence
of shutter events were checked against the private transcript.

This follow-up supplies the previously missing completed sequence, confirmed
photo-mode interval and playable new clip. Exact per-action timing and an
evidenced state/query interpretation are still missing. The timing uncertainty is retained rather than reported as precise action
timestamps. The online comparison below supplies field classification without
a further physical run. Production integration remains unqualified.

## Online-source reconciliation: retained captures are sufficient for decoding work

At the owner's request, an online search found a published X4 passive-capture
report with matching CE80 framing and typed display fields. The [protocol
comparison](../insta360_protocol.md#published-ce80-capture-compared-with-retained-x5-data-19)
records the pinned source and independent X5 counts. All 305 writes match its
framing; all 99 display messages can be classified structurally into elapsed
text, settings or remaining runtime/count. This supersedes the earlier claim
that all incoming fields are unclassified, while preserving the coarse timing
and production limitations. The completed manual sequence, mode observation,
playback and zero-loss transcripts satisfy the finite capture collection step
in #46 with recorded annotation uncertainty. No repeat of this generic sequence
is needed to start the typed decoder. State/query interpretation and final real
adapter checks remain separate incomplete rows.

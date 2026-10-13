# Local session bookmarks

Local bookmarks use the opted-in telemetry owner and isolated storage worker.
They work without connected cameras, a recording intent, a GNSS fix or UTC.
The [design investigation](highlight_bookmarks.md) explains the timing and
ownership decisions; [export usage](bookmark_export.md) covers offline markers.

## Activation and serial input

A commissioned provider sets `telemetry.runtime.bookmark_enabled = true` before
startup. This selects `StorageFormat::BookmarkV6` for that session. Defaults keep
bookmarks disabled; existing storage formats are unchanged. Camera-health V7 inherits
the bookmark envelope when both features are explicitly enabled. Bookmarks still
require their own activation flag; selecting health logging alone does not
enable MARK. Native bookmarks require local bookmarks enabled.
This preference does
not grant SD/pin/session-allocation qualification or create another logger.

In the composed bench application, send one uppercase `MARK` line (LF or CRLF).
Each separately processed command creates a distinct ID, even within the same
millisecond. Pasting several complete commands in a single burst retains the
console's `LostInput` refusal rather than replaying them later. There are no label
or timestamp parameters and no automatic REC, STOP, wake or resync side effects.

The result reports outcome, session ID, bookmark ID and owner monotonic receipt.
`admitted=1` means copied into the RAM queue; it does not prove an SD flush or a
saved camera tag. `STATUS` reports per-kind accepted, dropped, rejected, written,
flushed and lost counts. They saturate and do not give a per-marker durability ACK.
The original log remains the evidence record.

`BookmarkOutcome` codes are Admitted=0, Inactive=1, DisabledFormat=2,
InvalidTime=3, Exhausted=4, Overflow=5, StorageUnavailable=6, InvalidInput=7.
The local telemetry phase must be Running. Disabled format, stopped/terminal
storage, invalid monotonic/session time and exhausted IDs refuse admission.
A full queue refuses the new marker at occupancy six, retaining the two existing
protected slots. No old row is evicted, no caller blocks or synchronously flushes,
and failed admission is never retried automatically.

## Explicit physical mapping

The supported bookmark mapping is the enabled double slot:

```cpp
config.source.button.double_enabled = true;
config.source.button.double_action = ridesync::ButtonAction::Bookmark;
config.handlebar.actions = config.source.button;
config.telemetry.runtime.bookmark_enabled = true;
```

These source settings are startup defaults. Existing saved settings take
precedence, so persist the same button mapping when configuring an already
commissioned device; changing provider defaults does not overwrite NVS.
Keep the already qualified button/pin wiring. The short and long defaults remain
REC/STOP and wake/reconnect. This mapping replaces double Resync and delays a
single short action until the double window expires. Bookmark in short/long slots,
or in a disabled double slot, is refused by configuration validation. Existing
custom camera-action mappings remain supported. Persistence preserves the old
codes and appends Bookmark=3; old firmware will reject this new action code.

Debounce, held-at-boot arming, long suppression and double-window equality retain
the existing button contract. Camera-free local sessions poll input without
admitting camera actions. One recognized double emits a local attempt directly;
markers do not enter the camera-action FIFO. Reset/shutdown retires pending
input and does not replay it on reconnect. `ControlStatus::bookmark` and bench
`STATUS` retain the last physical receipt/outcome. No new LED acknowledgement
pulse or haptic hardware is delivered; existing status priority remains intact.

## Time, identity and footage

The key is `(session_id, bookmark_id)` with nonzero session-local IDs. A valid
owner attempt consumes an ID even if storage is full; parser/inactive failures do
not. Gaps and counters cannot reconstruct missing data after power loss.

Time is the owner's admission attempt: command execution or recognized-gesture
callback, rather than the raw electrical edge or camera frame acquisition.
The copied UTC anchor/estimate stays attached to that receipt; missing or expired
UTC does not prevent a valid monotonic marker. SD latency never restamps it.
Human reaction, hold/debounce, scheduling and camera pipeline delay remain outside
any frame-accuracy promise. See the design's conditional timing estimates.

The exporter retains unknown alignment and requires a user-supplied mapping to
place markers on a clip/project timeline. An ACK or REC intent cannot establish
clip zero. Generated editor data and synthetic tests do not certify an installed
editor import or physical alignment.

## Optional native camera path and physical ownership

Local admission is independent of the optional HERO12 HiLight path. Enabling
native requests also needs explicit runtime and per-model commissioning gates;
see [native HiLight documentation](hero12_hilight.md). Unsupported/busy/rejected
native requests do not erase the locally admitted record. Command ACK and saved
clip confirmation remain separate evidence.

Software fixtures establish admission, parsing and gesture behavior. Installed
button wiring remains with #30. #31 owns the finite input-to-owner timing, repeated
marker, camera-loss, reset/no-replay and affected SD observations. #46 owns the
optional HERO12 request/response and saved-clip checks. No hardware observations
or ride-readiness certification are added by this implementation.

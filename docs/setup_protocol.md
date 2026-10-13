# USB setup protocol v1

Issue #76 provides a copied, bounded serial session/draft service. The optional
setup firmware selects a safe boot-only route: recording, wake, recovery and
normal workers never start in that boot. Entry does not STOP, disconnect or
otherwise alter a camera to obtain isolation. Ordinary firmware refuses entry
when actual worker/recording state cannot establish this premise. Preference
validation is not commissioning or admission of a camera.

## Transport and limits

One existing serial owner reads and writes USB/UART; there is no second reader,
BLE callback parser or background writer. Setup requests and responses start with
`@RS1`; ordinary human diagnostics do not. Requests are ASCII with one space
between header fields and LF termination; CRLF, NUL, other control bytes, unknown
versions, extra fields and malformed decimal/hex values are refused. Replies
are ASCII, with hostile strings represented by hex. Default logs must not echo
requests, challenges, camera addresses, names, credentials or drafts. Explicit
INSPECT/DRAFT_GET replies disclose preferences only to the locally trusted host.

A frame has at most 2048 bytes excluding LF. The service has one fixed input
buffer, one fixed response slot and one copied save slot. A partial frame has a
2-second deadline from its first byte; subsequent bytes do not renew it.
Oversize/control-containing frames discard through LF; malformed frames return
`ERROR Error Malformed` with request ID zero when output capacity permits.
While a response is undrained, incoming burst frames discard through LF instead
of overwriting the response or executing requests. Hosts must send one request
at a time and await the matching result; Pending operations may produce a later
terminal result. There is no automatic resend, save retry or replay.

```
@RS1 SESSION REQUEST EPOCH GENERATION OP [PAYLOAD]\n
@RS1 SESSION REQUEST EPOCH GENERATION OP OUTCOME ERROR [PAYLOAD]\n
```

SESSION, REQUEST and EPOCH are unsigned decimal 32-bit values; GENERATION is
unsigned decimal 64-bit. Requests require nonzero REQUEST. Active-session IDs
must strictly increase, including continuation after ENTER; consumed IDs are
not reusable after an error. Session zero is reserved for HELLO/ENTER. EPOCH is
a boot nonce supplied by the production owner; the challenge and session nonce
must also be nonzero, fresh boot values. Unknown versions return Version;
unknown operations return Unavailable, never success. Extra fields on a known
operation return UnknownField. All mutation operations require exact current
epoch/generation, and DISPATCH additionally requires the generation at which
DRAFT_BEGIN copied its base. INSPECT, STATUS and REPORT accept an older generation
with the current epoch and return current generation to reconcile asynchronous
persistence completion.

Outcomes: `Ok`, `Pending`, `Error`. Errors: `None`, `Malformed`, `Version`,
`UnknownField`, `Unavailable`, `Busy`, `Session`, `Replay`, `Stale`, `Challenge`,
`EntryClosed`, `IsolationRequired`, `IsolationRefused`, `RecoveryRequired`,
`NoDraft`, `Invalid`, `OwnerRelease`, `NotDurable`, `Refused`, `PersistenceError`,
`Expired`, `Disconnected`, `ReadOnly`. None means operation-specific success, not camera
readiness or durable storage unless the operation is a terminal DISPATCH.

## Physical entry and lifetime

Within the first 30 seconds of monotonic boot time, HELLO with session zero
returns `challenge=N`, current epoch and settings generation. Challenge zero
means entry is closed. USB connection alone has no effect. ENTER declares
`N PARKED`; three wrong challenge/declaration attempts close entry until physical
reboot. HELLO exposes the challenge to an attached host: this is deliberate
physical entry, not authentication against an untrusted computer or a USB
attacker. The parked declaration is a declaration, not sensed motion evidence.

A correct ENTER initially returns Pending IsolationRequired. Only after the
owner actually closes recording/wake/recovery ingress does
`acknowledgeIsolation(true)` publish the correlated ENTER Ok None with the
nonzero session ID. Mutation admission starts after that acknowledgement.
Failed acknowledgement returns IsolationRefused. Pending and terminal replies
cannot overwrite each other; tick emits a deferred acknowledgement when the
response slot becomes free.

One session has 120-second inactivity and a 10-minute absolute deadline measured
with unsigned monotonic elapsed time, including timer rollover. A complete,
well-correlated request renews inactivity; partial/trickle bytes do not. The
absolute clock starts at accepted ENTER and never slides. Exit/expiry discards
unsent drafts and revokes entry until a reboot. No old recording intent resumes.
Session IDs/epoch prevent reuse after reboot; request IDs prevent duplicate
execution within a session. Concurrent clients receive Busy before entry or
Session/Replay after entry; the physical serial transport is not a multi-client
multiplexer.

The transport owner calls `disconnected()` when detach is observable. This
revokes the session and draft immediately, keeps ingress isolated and closes
edit entry until reboot; read-only recovery remains available. ESP32 UART bridges do not inherently report USB detach;
without a supported detach indication firmware cannot claim immediate unplug
recognition. Use explicit EXIT/CANCEL and idle expiry; reopening a host serial
port is not permission to reuse or automatically dispatch an old draft.

## Read-only recovery after a host reconnect

Normal edit entry remains closed after exit, expiry or detach. A locally trusted
host may reconcile an already admitted save without regaining edit authority:
send session-zero `HELLO RECOVERY`, then session-zero `RECOVER N PARKED` with
the returned current epoch. This handshake is available after the original
30-second window. It never restores drafts, original IDs or recording intent.
Only the safe boot-only setup image offers recovery; an owner that cannot
establish this route returns IsolationRefused.

RECOVER returns session-zero Pending OwnerRelease, revokes any old session and
drops an untaken save slot. A save already consumed by the config owner remains
owned until its correlated result and actual release. Pending terminal notices
from that prior session need not be replayed; STATUS retains the result.
After config/discovery ownership actually releases, RECOVER emits Ok None with
a fresh, incremented, nonzero session ID and the adopted generation. Prior
session frames remain invalid. The challenge rotates on acceptance; three wrong
recovery challenges close recovery until reboot. Session-nonce exhaustion
refuses recovery instead of wrapping and reusing an ID. Request IDs continue
above the RECOVER request in the new session.

A recovered session admits only INSPECT, STATUS, REPORT, EXIT and CANCEL. Drafts,
save, restart, discovery and selection return ReadOnly before any side effect.
The normal inactivity/absolute limits apply. Read-only recovery is a local
physical-trust convenience, not cryptographic host authentication. A fresh boot
forgets RAM request results; inspect persisted preferences rather than inferring
that an old host request succeeded.

## Commands

| Operation | Payload / response | Meaning |
|---|---|---|
| HELLO | none or `RECOVERY` / `challenge=N` | Inspect edit-entry availability or read-only recovery challenge with session zero. |
| RECOVER | `N PARKED` | Revoke old edit authority and wait for actual owner release, then create a fresh read-only session. |
| ENTER | `N PARKED` | Pending isolation, then acknowledged active session. |
| INSPECT | none / camera wire | Copied saved/effective camera preferences; no readiness claim. |
| STATUS | none / status fields below | Read retained save correlation and storage result. |
| REPORT | none / P1 report | Independent prerequisites; see setup_commissioning.md. |
| DRAFT_BEGIN | none | Copy complete settings snapshot; refuses unavailable/corrupt/future storage via RecoveryRequired. |
| DRAFT_SET | complete camera wire | Atomically replace all camera preferences, preserving button/GPIO fields from draft base. |
| DRAFT_GET | none / camera wire | Inspect unsaved draft. |
| DRAFT_DROP | none | Discard unsent draft; Busy while save owns it. |
| VALIDATE | none | Existing whole-settings validators plus four-slot limit and X5 at slot zero. |
| DISPATCH | none | Explicit single save producer handoff: Pending until correlated owner completion. |
| RESTART | none | Arm restart only after a verified Durable save; requires explicit EXIT afterward. |
| CANCEL | none | Discard unsent work and revoke session, waiting for actual owner release. |
| EXIT | none | Same release barrier; never resumes ride ingress in this boot. |

Scan, connect, pair, credential/qualification editing, resets, erase, formatting,
recording and network transports are unavailable. Never present an unavailable
operation as completed. Enabled/GPS/wake preferences remain requests and cannot
create identity, bond/store, firmware/profile, board or SD qualification.

Camera wire is one full candidate (not a patch):

```
COUNT,CAPACITY;FAMILY,MODEL,ADDRESS,ENABLED,GPS,NAMEHEX,IDHEX,WAKEHEX;...
```

There are exactly COUNT rows. Draft COUNT is 0..4, CAPACITY 1..4 and COUNT must
not exceed CAPACITY. INSPECT preserves existing legacy groups up to eight;
those groups cannot pass runtime validation unchanged. Strings are hex-encoded
bytes, `-` means empty; name maximum 64 bytes, identifiers/wake maximum 17 bytes.
Decoded NUL/control/DEL bytes are rejected. Names must be valid canonical UTF-8
(no overlong, surrogate, truncated or out-of-range sequences). The bounded
firmware name repertoire is printable ASCII U+0020..007E; Latin U+00A0..024F
except soft hyphen U+00AD; combining marks U+0300..036F; Greek U+0391..03A1
and U+03A3..03CE; Cyrillic U+0400..045F; Hiragana U+3041..3096; Katakana
U+30A1..30FA; CJK U+4E00..9FFF; Hangul U+AC00..D7A3; and emoji
U+1F600..1F64F. Other codepoints, including C1 controls, bidi/format controls,
private-use and unassigned characters, are refused. This finite assigned
repertoire avoids an additional Unicode classification dependency. The same
validation applies to inherited legacy names at VALIDATE/DISPATCH; inspection
can preserve legacy bytes without admitting them for save. Existing validators enforce MAC,
model/family/address/capability rules and conservative duplicate identities.
Hex length counts UTF-8 bytes, not display characters. Enum codes:

- FAMILY: Unknown=0, Insta360=1, GoPro=2, DJI=3.
- MODEL: Unknown=0, X5=1, GO3S=2, ONE_RS=3, HERO12_BLACK=4,
  DJI_ACTION4=5, DJI_ACTION5_PRO=6, DJI_ACTION6=7, DJI_OSMO360=8.
- ADDRESS: Unknown=0, Public=1, Random=2. ENABLED and GPS are 0 or 1.

Unknown/out-of-range values, omitted/extra rows or fields and partial strings
are rejected without changing the existing draft. Qualification/credential
fields have no encoding and cannot be written. X5 reorder is never automatic;
the host must preview any explicit reorder before DISPATCH.

STATUS payload:

```
status=STATE,session=S,request=R,epoch=E,generation=G,outcome=O,persist=P,code=C,indeterminate=I
```

STATE is Idle, Pending, Durable, Stale, Busy, Invalid, Refused or
PersistenceError. S/R identify the last admitted DISPATCH, separately from the
STATUS query ID in the response header. G is its retained result generation.
O uses SaveOutcome values None=0, Accepted=1, Stale=2, Busy=3, Invalid=4,
Refused=5, Durable=6, PersistenceError=7. P uses the pinned PersistStatus enum
in config_storage.h; C is its signed SDK code; I is 0/1 and marks an indeterminate
write requiring explicit recovery. Reporting a past result never resubmits it.
A fresh boot resets retained RAM status; persisted preferences are inspected
instead of claiming the previous request's success.

## Owner API and asynchronous barriers

SetupService lives in the one serial-owner task. reset, feed, tick, snapshot/report
publication, acknowledgeIsolation, ownerReleased and all hooks are serialized
there; no concurrent calls or retained references are allowed. Fixed payloads
are copied. The serial owner adds LF when writing takeResponse output.

`takeSave(SetupSaveDispatch&)` transfers exactly one copied candidate containing
session/request and SettingsSave epoch/generation/settings. The owner passes it
to the existing single-producer settings mailbox; full is terminal Busy, not an
automatic retry. Config task alone performs storage and publishes a copied
correlated result. The serial owner consumes that result and calls
`completeSave(session, request, outcome, result, epoch, generation)`.
Accepted is nonterminal. Durable, Stale, Busy, Invalid, Refused and PersistenceError
are terminal; wrong correlation/epoch and duplicate completion are ignored.
A terminal DISPATCH reply waits for the response slot and uses its original
request ID. Its header generation always describes the last officially adopted
snapshot; completion alone never relabels old settings with a new generation.
Production publishes the verified snapshot before delivering Durable completion;
STATUS retains the result generation separately during publication pressure. STATUS retains the result independently. The adopted SettingsSnapshot
comes from config publication; the service never promotes an unverified draft.

CANCEL/EXIT return Pending OwnerRelease. An unsent save slot is discarded; a
candidate already taken by the config owner cannot be rolled back and the
session waits for its result and actual ownership release. Only
`ownerReleased(true)` with no active save and a free response slot emits the
terminal EXIT/CANCEL Ok None and clears isolation ownership. Expiry/detach
emit request-zero EXIT Error Expired/Disconnected after the same release barrier.
The boot-only owner continues to keep normal workers closed. If RESTART was
armed after Durable, restartRequested becomes true only after this barrier;
the firmware then restarts explicitly. No reboot is implied by saving alone.

## Example

Example values are synthetic, not real commissioning evidence:

```
> @RS1 0 1 0 0 HELLO
< @RS1 0 1 7 0 HELLO Ok None challenge=123
> @RS1 0 2 7 0 ENTER 123 PARKED
< @RS1 0 2 7 0 ENTER Pending IsolationRequired
< @RS1 42 2 7 0 ENTER Ok None
> @RS1 42 3 7 0 DRAFT_BEGIN
< @RS1 42 3 7 0 DRAFT_BEGIN Ok None
> @RS1 42 4 7 0 DRAFT_SET 1,4;1,1,1,0,0,43616d,41413a42423a43433a44443a45453a4646,-
< @RS1 42 4 7 0 DRAFT_SET Ok None
> @RS1 42 5 7 0 VALIDATE
< @RS1 42 5 7 0 VALIDATE Ok None
> @RS1 42 6 7 0 DISPATCH
< @RS1 42 6 7 0 DISPATCH Pending None
< @RS1 42 6 7 1 DISPATCH Ok None
> @RS1 42 7 7 1 RESTART
< @RS1 42 7 7 1 RESTART Ok None
> @RS1 42 8 7 1 EXIT
< @RS1 42 8 7 1 EXIT Pending OwnerRelease
< @RS1 42 8 7 1 EXIT Ok None
```

Synthetic native tests establish framing/session/draft/ownership software
behavior only. They do not establish parked installation, recording state,
camera authentication, physical USB trust, power-cut durability or model support.

Discovery extension (#79): authenticated isolated sessions accept `DISCOVER`
and `CANCEL_DISCOVERY` without payload, `INVENTORY <index>` (0–16, 16 is
summary only), and `SELECT <index>,<model>,<slot>` (numeric explicit model enum,
zero-based slot). Every command uses the existing session/request/epoch/generation
checks. Inventory is private and names are hex encoded. `Pending` means a scan
or release is still owned; inspect `state,count,truncated,released`. Selection
always disables the candidate: `UnresolvedIdentity` is unsaved and blocks
VALIDATE/DISPATCH; verified selection returns `NeedsCommissioning`. X5 returns
`X5ManualCommissioning`. See [setup discovery](setup_discovery.md) for evidence
and ownership boundaries.

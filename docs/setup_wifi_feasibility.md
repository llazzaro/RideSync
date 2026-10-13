# Optional parked Wi-Fi feasibility (#81)

Decision: **infeasible within the declared pinned-build resource budgets;
transport remains disabled**. This is an isolated software
investigation, not working browser setup support. Serial remains the default.
No AP, TLS endpoint or camera was activated and no device IO occurred.

## Predeclared trial limits

Declared before the build trial on October 13, 2026: one associated client and one
TLS application socket, one request at a time, 2048-byte request, at most 8192
bytes of local assets, eight admitted/request attempts per second, 120-second
idle and 600-second absolute session lifetimes. Incremental budgets relative to
`serial_setup_application`: flash 384 KiB, static RAM 16 KiB, peak internal heap
96 KiB and added task stacks 16 KiB. Required internal free-heap minimum 48 KiB,
largest free block 24 KiB, each additional task at least 1024 bytes unused stack;
zero allocation failures and no retained allocations after ten teardown cycles.
These are proposed go/no-go thresholds, not measured device guarantees. PSRAM
does not satisfy internal-memory thresholds.

## Security selection and source findings

Select pinned SDK HTTPS with explicitly established certificate trust plus a
unique per-session application secret supplied over the physical serial entry.
A trusted certificate and possession check must precede all browser commands;
AP association alone never supplies application authorization. AP password and
application secret are independent, fresh random secrets. No default key,
example certificate/private key, insecure TLS mode or certificate-warning bypass
is accepted. Certificate provisioning/trust UX and secret generation/delivery
remain unimplemented gates; this trial deliberately embeds no key or credential.

The installed Arduino 2.0.17 SDK is IDF 4.4.7. Local `libesp_https_server.a`
exports `httpd_ssl_start/stop`; `libprotocomm.a` exports Security 1, session and
HTTP transport functions. No new dependency is required to retain HTTPS symbols.
Pinned [HTTPS documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/protocols/esp_https_server.html)
describes secure mode, certificate inputs and adjustable HTTP task configuration.
Its approximate per-socket allocation warns that static ELF RAM is insufficient
to establish runtime feasibility. Pinned
[protocomm documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/provisioning/protocomm.html)
offers Security 1 key exchange, encryption and proof of possession as an
alternative; no browser client/asset trust is supplied here. The archived BLE
transport imports Bluedroid GAP/GATT/controller lifecycle functions and cannot
be attached to the exclusive NimBLE host. It is excluded from this probe.

## Isolated trial and ownership

`tools/bench/setup_wifi/controller.py` is a pure injected lifecycle policy model.
It opens no sockets and asserts no cryptographic verification: its authenticated,
CSRF and certificate inputs represent decisions a reviewed future adapter must
actually establish. Exact Host/origin, POST-only commands, size/completion,
session/request correlation, one client, replay/rate limits and deadlines are
modeled. Authentication failures never dispatch. A camera operation remains
pending until actual network-stop acknowledgement; failed teardown refuses the
camera dispatch. Disconnect preserves a pending commit and retained result,
never resubmits it, and refuses another client. Camera release requires a new
lease rather than reusing old authentication or automatically restarting an AP.

The future adapter must copy bounded requests into the existing serial-owner
service task; HTTPS callbacks may not call SetupService or SettingsRequests.
Serial is the only producer and publishes correlated results; network/status
refresh never creates a second save producer. Transport arbitration, actual
session revocation, request parsing, CSRF/token verification and serial recovery
are not implemented by this model. Normal ride ingress remains closed in the
setup-only boot. Physical entry and parked declaration reuse the existing serial
premises; GNSS NoFix never infers parked operation. No Bluetooth release/reinit,
reset, erase, commissioning fields or recording commands are added.

`build_probe.py` makes a temporary configuration extending the actual serial
setup image and adds only `sdk_probe.cpp`. The retained callable references real
SDK HTTPS/Wi-Fi lifecycle and internal-heap/stack measurement APIs, with fixed
10 KiB buffers. It has no caller or activation path. Even invoking it would not
provide network-interface/event wiring or HTTP handlers; it is a link trial,
not a usable server. No browser assets are shipped because authenticated asset
trust and production transport arbitration have not been established.

## Evidence and exact gates

Run pure tests with `python3 tools/bench/setup_wifi/test_controller.py`.
Seven synthetic tests cover entry/allocation refusal, unauthorized and cross-origin
requests, CSRF rejection, method/size/partial limits, forged IDs, one-client
pressure/rate limits, replay, idle/absolute rollover deadlines, teardown failure,
disconnect during commit, wrong/duplicate completion, cancellation before camera
dispatch and camera pause ordering.
The modeled status is retained for a future serial recovery adapter; no physical
serial recovery or cryptographic handshake is claimed.

Build command (exclusive PlatformIO slot):
`PATH="$PWD/.venv/bin:$PATH" python3 tools/bench/setup_wifi/build_probe.py`;
baseline ` .venv/bin/pio run -e serial_setup_application`.

The accepted paired builds use one private mode-0700 temporary verification
snapshot of `src/`, `include/`, `scripts/`, `platformio.ini` and these isolated
trial files. This is a copied build fixture, not a Git checkout, feature branch
or worktree. No private header values were printed. Root changes continued
independently; the copied source hash remained unchanged after both builds.
Baseline and trial use separate snapshot build directories and their own
snapshot dependency directory. Pins and source bytes are identical, except
that the trial adds the probe source and linker retention flag.

Both pinned builds passed: baseline 49.696 seconds, trial 132.385 seconds.

| Measurement | Serial baseline | HTTPS/Wi-Fi trial | Increment | Budget |
|---|---:|---:|---:|---:|
| Static RAM bytes | 89996 | 117332 | **27336** | 16384 |
| Flash bytes | 801097 | 1298997 | **497900** | 393216 |

Both declared static gates fail: RAM exceeds its incremental budget by 10952
bytes and flash by 104684 bytes. Trial flash leaves only 11723 bytes in the
configured 1310720-byte application partition, before any real bounded browser
assets, credential material or request adapter is added. The probe already
retains the declared maximum 10 KiB transport buffers. These measurements show
that this HTTPS/Wi-Fi dependency selection is infeasible within the declared
budget on the pinned image; they do not show that all alternative transports or
a separately approved larger resource budget are infeasible.

Source snapshot and both ELF SHA-256 values are in
`tools/bench/setup_wifi/measurements.json`. `nm -S` confirms
`httpd_ssl_start/stop`, Wi-Fi init/stop, the retained probe and 8192/2048-byte
static buffers in the linked trial. SDK APIs therefore compile and link.
Earlier root-checkout measurements were discarded because concurrent source
edits invalidated the pairing; they are not used above. An overlapping
configuration invocation and a post-script source-builder rejection were
corrected before the accepted frozen-snapshot pair. No failed/discarded run
contributes measurements. A future resource redesign needs explicit scope;
this investigation does not iterate until a preferred result appears.

Runtime minimum internal free heap, largest block,
stack high-water marks, allocation failures and teardown leaks are **not
measured** without an activated device. A linked API proves availability only.
Before enabling any redesigned transport, it must pass a newly approved
paired resource budget, and a reviewed HTTPS authentication/trust adapter,
bounded parser/assets, exact origin/CSRF enforcement, owner arbitration and
network pause/reconnect lifecycle must pass software tests, then all declared
runtime resource thresholds must be recorded at maximum load. Failing any gate
keeps Wi-Fi disabled. Installed RF/parked-entry observations remain with
#46/#22/#30; this finding does not complete a production browser feature.

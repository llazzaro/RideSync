# Pure Insta360 GPS Encoder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Preserve the user's selected native/inline execution using executing-plans.

**Goal:** Complete #29's software boundary with an independently verified, source-backed 71-byte GPS encoder.

**Architecture:** A stateless portable function consumes copied GNSS and session observations and returns fixed storage or an explicit error. A separate Python oracle checks bytes emitted by the actual C++ function. A data-only link anchor retains that implementation in the existing opt-in ESP32 image without activating a camera.

**Tech Stack:** C++11, IEEE binary32/binary64, Unity 2.6.1, Python standard library, pinned PlatformIO native and ESP32 environments.

**Spec:** [Approved design](../specs/2026-10-09-insta360-gps-encoder-design.md). Read both documents before execution.

## Global Constraints

- Work directly on main; commit and push verified changes to origin/main. No PR, branch or worktree.
- Namespace `ridesync::insta360`; profile `GarminBe80VideoV1`; default profile Disabled and `max_age_ms` zero.
- Enabled age limit 1..60000 ms; sequence 1..254; packet always 71 bytes.
- Every failure has size zero and all-zero bytes. Validation order is exactly spec steps 1..8.
- Gregorian UTC years 2000..2099, hours 0..23, minutes/seconds 0..59 and centiseconds 0..99. Omit centiseconds.
- Narrow each validated scalar to IEEE binary32, then promote binary64; positive-zero canonicalization and explicit little-endian serialization.
- Reject missing required fields, negative altitude, nonfinite values, narrowing overflow/underflow and narrowed course >=360.
- No IO, clock reads, heap allocation, blocking, retained pointers, internal sequence or runtime activation.
- Reference SHA `39c51b3aa7c453227831d811355899371bbb8b94`; new derived files/fixtures carry MPL-2.0 notices and attribution.
- Camera capabilities remain disabled/unqualified. #14 owns delivery; #22/#30/#31 retain physical acceptance.
- Reuse unchanged evidence per `docs/acceptance_policy.md`; target compiler frames are not physical stack measurements.

## Review Focus

1. A retained Valid snapshot can become stale: recomputed age must agree and be within the limit (Task 1).
2. Negative zero and float32 subnormals must not reproduce the reference exponent bug (Tasks 1 and 2).
3. Course below 360 can round to 360; fail instead of emitting an out-of-range course (Task 1).
4. Simultaneous invalid inputs must return the first specified error and no usable bytes (Task 1).
5. Successful host compilation can hide discarded target code: prove symbol retention without activation (Task 3).

## File responsibilities

| File | Responsibility |
|---|---|
| `include/insta360_gps_encoder.h` | Public configuration, error, fixed-result types and pure function |
| `src/insta360_gps_encoder.cpp` | Validation, local calendar arithmetic, explicit wire serialization and retention anchor |
| `test/test_insta360_gps_encoder/test_main.cpp` | Unity contract and input/failure matrix |
| `test/fixtures/insta360/be80_gps.h` | Independently specified synthetic full-packet literals and provenance |
| `test/fixtures/insta360/gps_encoder_probe.cpp` | Small host executable emitting actual encoder packets |
| `test/test_insta360_gps_oracle.py` | Independent Python arithmetic/packet oracle and host compilation |
| `licenses/insta360-remote-ciq-MPL-2.0.txt` | Matching pinned upstream license |
| `scripts/probe_gps_encoder_resources.py` | Retained target symbol/ABI/frame audit |
| `platformio.ini`, `.github/workflows/ci.yml` | Existing opt-in retention and reproducible audit gate |
| `docs/gps_protocol.md`, `docs/sources.md`, `docs/testing.md`, `README.md`, `docs/issue_review.md` | Actual software evidence, licensing and remaining camera limits |

No GNSS/parser/session-clock or existing shutter codec API changes are required.

### Task 1: Implement the complete pure encoder contract

**Interfaces:** Consumes existing `RecordTimestamp`, `ModemSnapshot`, `GnssFix`, `SessionClock::kMaxDurationMs`. Produces the following public API, consumed unchanged by Tasks 2 and 3:

```cpp
namespace ridesync { namespace insta360 {
enum class GpsWireProfile { Disabled, GarminBe80VideoV1 };
struct GpsEncoderConfig {
  GpsWireProfile profile = GpsWireProfile::Disabled;
  uint32_t max_age_ms = 0;
};
enum class GpsEncodingError {
  None, Disabled, InvalidConfig, InvalidSequence, InvalidClock, InvalidFix,
  MissingField, InvalidCoordinate, InvalidUtc, InvalidMetric, UnsupportedAltitude
};
struct GpsEncodingResult {
  GpsEncodingError error = GpsEncodingError::Disabled;
  size_t size = 0;
  std::array<uint8_t, 71> bytes{};
};
GpsEncodingResult encodeGps(const GpsEncoderConfig&, const RecordTimestamp&,
                            const ModemSnapshot&, uint8_t sequence);
}}
```

**Files:** Create the public header, source, Unity test, packet fixture and license listed above. Update `docs/sources.md` with exact file coverage and pinned provenance in this task.

- [ ] Write a valid input helper and full literal packet test before the public API exists. Use member assignments because C++11 structs with member initializers are not aggregates:

```cpp
struct Inputs {
  ridesync::insta360::GpsEncoderConfig config;
  ridesync::RecordTimestamp now;
  ridesync::ModemSnapshot snapshot;
};
Inputs validInputs() {
  Inputs i;
  i.config.profile = ridesync::insta360::GpsWireProfile::GarminBe80VideoV1;
  i.config.max_age_ms = 1000;
  i.now.session_id = 7;
  i.now.monotonic_quality = ridesync::MonotonicQuality::Valid;
  i.now.monotonic_ms = 2000;
  i.snapshot.session_id = 7;
  i.snapshot.validity = ridesync::FixValidity::Valid;
  i.snapshot.age_available = true;
  i.snapshot.age_ms = 1000;
  auto& f = i.snapshot.fix;
  f.valid = true;
  f.receipt_monotonic_ms = 1000;
  f.latitude_degrees = 1;
  f.longitude_degrees = -2;
  f.utc_date.available = f.utc_time.available = true;
  f.utc_date.value.year = 2000;
  f.utc_date.value.month = f.utc_date.value.day = 1;
  f.speed_metres_per_second.available = true;
  f.speed_metres_per_second.value = 3;
  f.course_degrees.available = true;
  f.course_degrees.value = 90;
  f.altitude_msl_metres.available = true;
  f.altitude_msl_metres.value = 4;
  return i;
}
const uint8_t kNorthWest[71] = {
  0x47,0,0,0,4,0,0,0x35,0,2,1,0,0,0x80,0,0,0x0a,0x35,
  0x80,0x43,0x6d,0x38, 0,0,0,0,0,0,0x41,
  0,0,0,0,0,0,0xf0,0x3f,'N',
  0,0,0,0,0,0,0,0x40,'W',
  0,0,0,0,0,0,8,0x40,
  0,0,0,0,0,0x80,0x56,0x40,
  0,0,0,0,0,0,0x10,0x40
};
void literal_packet() {
  const auto i = validInputs();
  const auto r = ridesync::insta360::encodeGps(i.config,i.now,i.snapshot,1);
  TEST_ASSERT_EQUAL_UINT(71,r.size);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(kNorthWest,r.bytes.data(),71);
}
void expectError(const Inputs& i, uint8_t sequence,
                 ridesync::insta360::GpsEncodingError error) {
  const auto r = ridesync::insta360::encodeGps(i.config,i.now,i.snapshot,sequence);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(error),static_cast<int>(r.error));
  TEST_ASSERT_EQUAL_UINT(0,r.size);
  const uint8_t zero[71] = {};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(zero,r.bytes.data(),71);
}
```

- [ ] Run `.venv/bin/pio test -e native -f test_insta360_gps_encoder`; expect missing-header/API failure. Register each Unity test in `main()` with `RUN_TEST`; use empty `setUp`/`tearDown`.
- [ ] Add parameterized cases using `validInputs`/`expectError`, resetting inputs per case. Each row below is a required test with the named expected error; assertions also check zero payload. Commit no incomplete success path.

| Mutation / input | Expected |
|---|---|
| default config; unknown enum; age 0/60001 | Disabled; InvalidConfig; InvalidConfig |
| sequences 0/255; 1/254 | InvalidSequence; successful exact byte 10 |
| zero session, both invalid monotonic qualities, duration limit+1 | InvalidClock |
| each non-Valid fix validity; false valid; missing age; foreign session | InvalidFix |
| future receipt, mismatched age, matching age limit+1 | InvalidFix |
| matching age exactly limit; receipt/now above 2^32 with same age | success |
| each coordinate NaN/Inf, ±90/±180, just outside each bound | InvalidCoordinate or success at bounds |
| each nonempty mask of the five required availability flags (31 cases) | MissingField |
| 1999/2100, month 0/13, day 0, April 31, 2001-02-29 | InvalidUtc |
| 2000/2004-02-29, 2099-12-31, 2000-01-01 | independently checked UTC success |
| hour 24, minute/second 60, centisecond 100 | InvalidUtc |
| centisecond 0/99, changed anchor/UTC estimate | identical packet |
| each metric NaN/Inf, negative speed, course -1/360 | InvalidMetric |
| finite negative altitude | UnsupportedAltitude |
| scalar overflow and nonzero narrowing-to-zero, each field | InvalidMetric |
| course `std::nextafter(360.0,0.0)` | InvalidMetric after narrowing |
| ±0 on every scalar; representable float32 subnormal speed/altitude | positive-zero bytes/N/E; correct subnormal promotion |
| nonexact ordinary values such as 1.1; largest float32 speed/altitude | narrowed/promoted bytes, success |
| all invalid stages combined, removing earliest defect in turn | errors in exact spec order |
| negative altitude plus speed overflow; NaN altitude plus negative speed | UnsupportedAltitude; InvalidMetric |
| repeated call, mutated returned copy, unchanged input member values | stateless output and input immutability |
| absent/present satellites and fix-quality | identical packet |

Example edge tests:

```cpp
void retained_snapshot_expires() {
  auto i = validInputs();
  ++i.now.monotonic_ms;
  ++i.snapshot.age_ms;
  expectError(i,1,ridesync::insta360::GpsEncodingError::InvalidFix);
}
void course_rounding_is_rejected() {
  auto i = validInputs();
  i.snapshot.fix.course_degrees.value = std::nextafter(360.0,0.0);
  expectError(i,1,ridesync::insta360::GpsEncodingError::InvalidMetric);
}
```

- [ ] Implement the public types and encoder. Validate in the spec's eight stages, returning a fresh zero-initialized result on failure. Use local Gregorian arithmetic: start with 10957 days from 1970 to 2000, sum years before the selected year, then months before selected month and day-1; leap predicate `y%4==0 && (y%100!=0 || y%400==0)`. Accumulate epoch in uint64, check uint32 bound. Validate calendar before indexing month lengths.
- [ ] Serialize only after all validation succeeds. Require `sizeof(float)==4`, `sizeof(double)==8`, IEC559 and radix 2/digits 24/53. Use these private helpers (no public shared-clock refactor):

```cpp
bool narrowScalar(double value, double& wire) {
  if (std::fabs(value) > std::numeric_limits<float>::max()) return false;
  const float f = static_cast<float>(value);
  if (!std::isfinite(f) || (value != 0 && f == 0)) return false;
  wire = f == 0 ? 0.0 : static_cast<double>(f);
  return true;
}
void writeLe(uint8_t* out, uint64_t bits, size_t count) {
  for (size_t n=0;n<count;++n) out[n]=static_cast<uint8_t>(bits>>(8*n));
}
void writeDouble(uint8_t* out, double value) {
  uint64_t bits=0;
  std::memcpy(&bits,&value,sizeof(bits));
  writeLe(out,bits,8);
}
```

Coordinates narrow their absolute magnitude after range checks; hemisphere uses original `<0` comparison. Narrow all five scalars and reject promoted course >=360. Copy the specified 18-byte prefix, insert sequence at 10, epoch at 18, filler at 22..28, then write scalars/hemispheres at the exact spec offsets. Set error None/size71 last.

- [ ] Attribute the pinned reference in header/source/derived fixture, copy its full MPL-2.0 text into the listed license file, and document ownership/coverage. Independently written arithmetic tests use synthetic data; no camera captures are claimed.
- [ ] Run focused Unity tests and format changed C++ with `.venv/bin/clang-format -i`; run `PATH="$PWD/.venv/bin:$PATH" .venv/bin/python scripts/check_format.py` and `git diff --check`. Review tests against every table row and the spec; fix failures before committing.
- [ ] Commit and push the encoder/tests/license/source documentation as `feat(gps): add source-backed pure BE80 encoder` on main.

### Task 2: Check actual C++ packets with an independent Python oracle

**Interfaces:** Consumes Task 1's `encodeGps` unchanged. Produces a probe executable with `main(int argc,char** argv)` taking `ordinary`, `south-east`, `zero`, `subnormal` or `precision`, emitting exactly 71 lowercase hexadecimal bytes and a newline on success (nonzero exit on encoder failure). Python `decode_packet(payload: bytes) -> dict` rejects lengths other than 71; `unittest` cases compile and run the probe.

**Files:** Create the probe and Python oracle listed in the file map; derived expected packet data carries attribution. No production changes except correction of a demonstrated defect.

- [ ] Write Python tests that compile the probe in `TemporaryDirectory` with `c++ -std=c++11 -Iinclude test/fixtures/insta360/gps_encoder_probe.cpp src/insta360_gps_encoder.cpp -o <temporary executable>` using `subprocess.run(check=True)`. Expected initial failure: probe missing. In the probe, explicitly construct the same copied input values as Task 1; never include its test helper or expected-packet fixture.
- [ ] Implement the independent decoder and arithmetic oracle:

```python
import calendar
import struct

def decode_packet(payload: bytes) -> dict:
    if len(payload) != 71:
        raise ValueError("GPS packet must contain exactly 71 bytes")
    return {
        "epoch": struct.unpack_from("<I", payload, 18)[0],
        "lat": struct.unpack_from("<d", payload, 29)[0],
        "ns": chr(payload[37]),
        "lon": struct.unpack_from("<d", payload, 38)[0],
        "ew": chr(payload[46]),
        "speed": struct.unpack_from("<d", payload, 47)[0],
        "course": struct.unpack_from("<d", payload, 55)[0],
        "altitude": struct.unpack_from("<d", payload, 63)[0],
    }

def expected_scalar(value: float) -> bytes:
    narrowed = struct.unpack("<f", struct.pack("<f", value))[0]
    return struct.pack("<d", 0.0 if narrowed == 0 else narrowed)

assert calendar.timegm((2000, 1, 1, 0, 0, 0)) == 946684800
assert expected_scalar(0.0) == bytes(8)
assert expected_scalar(2.0**-149) == struct.pack("<d", 2.0**-149)
assert expected_scalar(0.0) != struct.pack("<d", 2.0**-127)
```

- [ ] Add concrete oracle assertions to a `unittest.TestCase`, with `self.probe` set to the compiled executable path in `setUpClass`:

```python
def test_zero_and_packet_bounds(self):
    payload = bytes.fromhex(subprocess.check_output(
        [str(self.probe), "zero"], text=True).strip())
    decoded = decode_packet(payload)
    self.assertEqual((decoded["ns"], decoded["ew"]), ("N", "E"))
    for offset in (29, 38, 47, 55, 63):
        self.assertEqual(payload[offset:offset+8], bytes(8))
    for invalid in (payload[:-1], payload + b"\x00"):
        with self.assertRaises(ValueError):
            decode_packet(invalid)
```

- [ ] Implement probe inputs: ordinary=(1,-2,3,90,4), south-east=(-1,2,3,90,4), zero=(-0,-0,-0,-0,-0), subnormal=(1,-2,2^-149,90,2^-149), precision=(1.1,-2.2,3.3,90.1,4.4). For each actual packet, check complete bytes built independently using literal prefix/filler, Python epoch and scalar functions, and explicit hemisphere bytes. Check 70/72-byte decoder failures and original packet after copied-byte mutation. Label fixtures synthetic/source-derived, never captured.
- [ ] Run `.venv/bin/python -m unittest discover -s test -p test_insta360_gps_oracle.py -v`; run Task 1 focused Unity suite if production changed. Expect all pass; commit/push as `test(gps): independently verify emitted BE80 packets`.

### Task 3: Retain target code, document evidence and complete software acceptance

**Interfaces:** Consumes the unchanged API and passing oracle. Produces C-linkage data symbol `ridesync_insta360_gps_encoder_backend`, holding an `encodeGps` function pointer; resource script `main()` accepts `--elf`, `--build-dir`, `--nm` and `--objdump`, exits nonzero on missing proof. No call or runtime registration is introduced.

**Files:** Modify source, `platformio.ini`, CI and five documentation files listed in the file map. Create the resource script.

- [ ] Write the audit to require the anchor and demangled encoder symbol in the retained ELF, resolve its referenced encoder function, reject allocator/driver/IO calls reachable from encoder helpers, and report the encoder/helper `.su` individual frames (reject dynamic/unbounded markers or missing entries). Record ELF/section/symbol evidence separately from physical claims. Use `subprocess.run` argument arrays; reuse the existing `scripts/probe_motion_resources.py` tool-path conventions. Run against the current retained image: expect missing encoder/anchor failure.
- [ ] Add a constant, externally visible function-pointer anchor in the encoder translation unit:

```cpp
extern "C" {
extern ridesync::insta360::GpsEncodingResult (*const ridesync_insta360_gps_encoder_backend)(
    const ridesync::insta360::GpsEncoderConfig&, const ridesync::RecordTimestamp&,
    const ridesync::ModemSnapshot&, uint8_t) = &ridesync::insta360::encodeGps;
}
```

Add `-Wl,-u,ridesync_insta360_gps_encoder_backend` only to `hero12_adapter_compile`, which already enables `-fstack-usage`. No application call site is added. Confirm target return ABI by emitted disassembly, result storage and successful link; report observed sizes rather than assuming host padding equals target padding.

- [ ] Build `.venv/bin/pio run -e lilygo_t_a7670e_r2` and `.venv/bin/pio run -e hero12_adapter_compile`. Run resource audit on `.pio/build/hero12_adapter_compile/firmware.elf` and its build directory with the pinned Xtensa nm/objdump under `$HOME/.platformio/packages/toolchain-xtensa-esp32/bin/`. Confirm anchor references actual encoder and camera activation/capability declarations remain unchanged.
- [ ] Add an explicit CI resource-audit step after retained HERO12 build using those same arguments. Existing Python discovery/native jobs already discover Tasks 1/2. Do not change dependency pins.
- [ ] Update docs with exact source/functions/offsets, UTC and numeric policy, fixed-result/errors, license coverage, test commands/results, retained symbols/observed frames and experimental profile limits. Remove obsolete statements that no real encoding path exists; preserve unsupported photo/accuracy/signed-altitude/optional variants and all physical gates. Update #29 software status in issue review while keeping #14 separate.
- [ ] Run focused Python/Unity tests, C++ format and diff checks. Push the verified commit `build(gps): retain and audit pure encoder on ESP32`; wait for the full existing main CI run and fix any new failure. No hardware result is inferred.
- [ ] Once full CI passes, update #29 with concise evidence links and close its software outcome. This goal authorizes issue maintenance; do not message other people. Leave #14/#22/#30/#31 and other hardware issues open. Report completed software work, main commit, tests and remaining physical limits to the user.

## Plan self-review and execution handoff

Spec coverage: Task 1 owns API, all validation stages, exact packet, numerical policy and license; Task 2 owns independent actual-output arithmetic; Task 3 owns retained target proof, CI, documentation and software acceptance. All five Review Focus conditions have owning tests/checks. No shared parser/calendar refactor or new runtime scope is needed. Public names/signatures are identical throughout; source fixtures remain explicitly synthetic.

Written spec approved on 2026-10-09. The user approved this implementation plan on 2026-10-09. Preserve native/inline execution, using executing-plans, and continue direct-main commits. Implementation progress is recorded in the execution ledger and task commits.

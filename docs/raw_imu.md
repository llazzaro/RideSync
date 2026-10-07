# Selected raw BMI270 software path (#12)

Physical #12 remains OPEN. Exact SparkFun SKU/revision, PCB/bus connections,
mount/calibration, stationary/known-axis checks, 200 Hz throughput/jitter under
SD/GNSS load, and camera/GPS continuity on real IMU failure remain unverified.
No peripheral starts in main. No purchase, pin, interrupt or mount is selected.

`Bmi270Imu` uses the Bosch 2.86.1 API bundled in SparkFun v1.0.3, immutable
21ea234de321da07c552f7a43cb36f7df4f73a27. PlatformIO preserves vendor sources and
MIT/BSD-3-Clause notices; no SDK/vendor source is modified or copied. It owns
its own bmi2_dev. It never calls wrapper getFIFOData or Bosch extractors that
apply host remapping/cross-axis compensation. Original LE signed pairs remain
specific-force/angular-rate counts, gyro XYZ then accel XYZ, without calibration
application, gravity removal, lean or converted-float recovery.

## Qualified composition and ownership

Caller supplies a dedicated, already initialized TwoWire object; qualify voltage,
address 0x68/0x69, >=128-byte Wire buffer, finite transaction timeout and exclusive
ownership through shutdown. Wire lock acquisition and device upload latency are
not proven bounded by a timeout. Bmi270Qualification defaults disabled, requires
all qualification flags and nonzero caller identity. No Wire.begin, pin routing
or board power API is called. Sensor identity is caller evidence; mount/calibration metadata is copied
unchanged (defaults unknown). Never turn qualification on just to obtain output.

Construct MixedV2 ArduinoSdStorage using its existing qualified card/bus config,
SessionClock with unique session, ImuInbox, TelemetryAdmission, Bmi270Imu,
ImuManager(port,inbox,supervisor.progress(Worker::Imu),session,metadata), then
Bmi270Worker(manager,progress). All are caller owned and remain alive. Configure
supervisor's optional IMU policy from qualification; start worker only outside
safe mode after independently starting qualified SD/GPS/camera services. Failure
of worker.start or sensor initialization must not gate those independent services.
The worker alone calls init/config/read/flush. Admission/control owner calls GPS
with its shared owner snapshot first, then admission.tick; it never calls IMU
step. Transport never calls SessionClock, Storage or camera.

Shutdown: owner admission.requestStop, worker finishes its current synchronous
operation, manager flushes publication batch then inbox.finish/progress.finished;
owner continues admission.tick through admission.stopped; SD worker closes.
Manager inbox.finish/progress.finished signals final publication, not task
quiescence. Bmi270Worker.workerFinished separately acquire-observes its own
completion flag, release-published at the final caller-owned access before RTOS
task exit. No worker/manager/progress access follows that flag. Keep bus/objects
alive through workerFinished and SD close. Adapter and worker cannot be copied
or moved, preserving self-referential callbacks and sole ownership. No forced task deletion
or concurrent bus deinit. Manager.stop is an atomic alternative stop request.
Health generation follows returned service work only; it cannot advance while
an I/O call is blocked. Terminal idle passes are completed no-I/O state checks
with RetryExhausted outcome; they do not retry initialization. One recovery flush
per malformed/read-error pass, at most two attempts before the third consecutive
fault retires acquisition; a nonempty valid burst clears this streak. Flush
failure is terminal. Stop performs no new I/O.

## Profile, FIFO and evidence

Init/readback verifies both channel enable bits and sensors 200 Hz, +/-16 g,
+/-2000 dps, accel normal AVG4/performance and gyro normal/performance/noise performance; filter IDs 0x502
(accel) and 0x702 (gyro): bwp bits0..7, performance bit8, gyro noise
performance bit9, FIFO filtered bit10, downsample exponent bits16..18. Both
device offset compensations disabled/read back; exact scales 1/2048 g/count, 125/2048 dps/count.
Generation increases only after successful readback/initial flush. Sensor-frame
FIFO bytes bypass host compensation/remapping; device filters still affect bytes.
Header/time/accel/gyro FIFO enabled; AUX/interrupt tags disabled and config read
back. FIFO filtered output and zero downsampling exponent are set/read back for both
sensors; advanced power save is disabled/read back for FIFO burst operation.
Configured sensor ODR alone does not prove FIFO sample throughput.

One burst reads reported FIFO length plus four trailer bytes, max 112 bytes;
reported length >108 refuses the read and enters a flush barrier. This is a
capacity refusal (health code1, two LE bytes of observed FIFO length), distinct
from a transport-error code2. It increments a saturating capacity-overflow
counter; no exact dropped-frame count or physical FIFO-full claim follows. This
conservative profile deliberately sacrifices backlog instead of splitting unverified bursts.
FIFO growth during a burst can still produce a partial frame: this is reported
and discarded through the barrier. No carry, realloc, ordinal merge,
resynchronization scanning or backdating. Only
0x8c complete paired frames, 0x44 time, 0x40 skip, 0x48 input config and 0x80 end
are accepted. Unpaired, tagged, AUX, partial and unknown frames publish evidence
and force next-pass flush. Input-config bytes/position are retained then retire
acquisition because effective settings cannot be assumed; restart needs a fresh qualified lifetime
and configuration readback. Overread/end retains an ImuControl code0 record
with one-byte payload0x80 and original byte position, then ends parsing; all
remaining burst bytes are ignored. This code0 source-profile representation
distinguishes observed end presence from simply exhausting the supplied buffer.
Short/no transport reads return error without exposing partial initialized data;
no samples from that transaction are admitted. Earlier complete pairs before a
parser error remain valid observations, with following discontinuity event.

Every evidence record copies complete config and source session/batch/epoch.
Frame sequence stays monotonic through flush; exhaustion retires acquisition.
Epoch increases after confirmed recovery flush; no claim flush resets sensor
clock. Controls preserve position/source byte order, including tick zero. Tick
unit 39.0625 us and nominal wrap 655.36 s are documentary. Repeated ticks mark
stale; decreasing ticks or host gaps >=655360 ms mark wrap/reset ambiguity, never
unwrap. A host receipt that repeats marks stale receipt. Receipt/drain start/end
are raw modulo Clock millis32, NOT UTC/session elapsed or frame acquisition time.
No known acquisition time is supplied. An unobserved full host wrap cannot be
resolved; supervisor/lifetime qualification must prevent relying on it.

Software endpoint bit checks +/-32767 in each raw axis (source BMI270 endpoints);
it is not proof of unclipped history. Nonzero saturation register status is a
separate ImuHealth code0, one-byte receipt-status payload (six axis bits), never
attached as historical frame hardware flags. Absent status event proves nothing.
Skip255 is a lower bound. Parser health saturates samples/unsupported/partial/
skipped-lower-bound/discontinuities. Manager health saturates init/read refusals,
successful recovery flushes and recovery errors. Counter getters belong to the
worker owner or a quiescent lifetime, never concurrent unsynchronized reads.
Inbox per-kind drops track overflow; Storage per-kind counters remain a separate stage. Transport errors have discontinuity
events but no invented lost-frame count. Capacity overflow/recovery loss is
unknown, not an exact sample number.

Fixed manager buffers: 112 bytes input + 968-byte publication batch + copied
240-byte base evidence and codec state. Strict bus owns 112-byte staging buffer;
no partial bytes leak. Per-step parser iterations <=112, publication <=28 batches
(conservative all one-byte controls), actual supported non-end minimum 2 bytes;
records/batches are fixed. Bosch config upload uses 32-byte callbacks, reads <=112;
worker stack allocation 8192 bytes is a chosen capacity, not measured peak. SDK,
Wire, Bosch upload delays/locks and whole-task stack/latency remain bench gates.
Synthetic native tests are author-created MIT fixtures, never device captures.

# Complete bench workflow

The owner requested implementation now so the intended workflow can be tested
before motorcycle installation. Work on main, with parallel agents, retaining
private device facts and existing data. This plan addresses integration missing
from the closed component tickets; it does not reinterpret hardware observations
as software implementation or invent undocumented commands.

## Delivery

1. Replace the HERO12-only application seam with a shared model-dispatched
   camera runtime. X5 CE80, ONE RS BE80, GO 3S BE80 and HERO12 share one manager,
   group intent, audit/logging route and serialized advancement. Split component
   servicing from manager advancement; preserve old component callers.
2. Supply an explicit bench application provider with private source settings,
   camera/store evidence, declared UART/SPI/I2C/button/LED routing and existing SD
   namespace. Validate conflicts before device IO. Load private defaults only
   for absent settings, never overwrite valid/corrupt/future persisted settings.
3. Add bounded explicit serial CONNECT/REC/STOP/QUERY/WAKE/STATUS/SHUTDOWN on the
   composed runtime. Button control and serial share the same owner. Boot sends
   no recording intent; commands never replay on reset or reconnect.
4. Integrate the existing experimental dynamic estimator into actual IMU
   admission and separate MotionV5 diagnostics. Require declared external
   stationary initialization and timing provenance; record invalid/unreliable
   results, gaps and bounded-horizon faults. Preserve MotionV4 byte compatibility.
5. Provide a pinned bench build and complete setup/testing instructions. Test
   mixed dispatch, single advancement, activation failures, defaults handling,
   console framing/refusal and live motion boundaries. Run the required pipeline,
   affected ESP32 builds and independent review before commit/push.

## Explicit remaining limits

GO 3S/ONE RS ACK or ATT completion is not recording observation. Their unsupported
state/wake/GPS semantics cannot be filled with guessed bytes. ONE RS GPS has a
separate source-backed forwarding profile. X5 CE80 requires fresh video/state
before toggle. Motion diagnostics are experimental, not achieved dynamic accuracy.
These limits must appear in bench status and #46, not be silently disabled or
described as a complete physical pass. Hardware checks remain in #46 and their
existing acceptance owners. No firmware upload, bond erasure, card provisioning
or motorcycle installation is part of this software delivery.

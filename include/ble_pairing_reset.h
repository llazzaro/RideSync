#pragma once
#include "ble_remote.h"
namespace ridesync {
// Independent durable commissioning evidence. New qualifications must use a
// strictly increasing nonzero record; never wrap or manufacture a replacement.
struct BleStoreProof {
  uint32_t qualification_record = 0;
  std::array<uint8_t, 32> digest{};
  std::array<unsigned, 7> counts{};
};
struct BleStoreObservation {
  bool complete = false;
  int error = 0;
  BleStoreProof snapshot;
};
enum class BondResetSubmission { Queued, Busy, Refused, Stale };
struct BleBondResetResult {
  uint32_t operation = 0;
  BondOutcome outcome = BondOutcome::Busy;
  int error = 0;
  bool finished = false, releasable = false, mutation = false;
  bool requalification_required = false, cancelled = false, timed_out = false;
};
} // namespace ridesync

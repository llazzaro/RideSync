#include "session_identity.h"
#include <cerrno>
#include <cstring>
#include <limits>
namespace ridesync {
namespace {
using Bytes = uint8_t[SessionIdentityAllocator::kSlotBytes];
uint32_t get(const uint8_t *b) {
  return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}
void put(uint8_t *b, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i)
    b[i] = uint8_t(v >> (8 * i));
}
uint32_t crc(const uint8_t *b, size_t n) {
  uint32_t v = 0xffffffff;
  for (size_t i = 0; i < n; ++i) {
    v ^= b[i];
    for (unsigned j = 0; j < 8; ++j)
      v = (v >> 1) ^ (0xedb88320u & (0u - (v & 1)));
  }
  return ~v;
}
void record(Bytes &b, uint32_t ns, uint32_t counter) {
  put(b, 0x44495352);
  put(b + 4, 1);
  put(b + 8, ns);
  put(b + 12, counter);
  put(b + 16, counter);
  put(b + 20, ~ns);
  put(b + 24, ~counter);
  put(b + 28, ~counter);
  put(b + 32, 0);
  put(b + 36, crc(b, 36));
}
bool valid(const Bytes &b, uint32_t ns) {
  return get(b) == 0x44495352 && get(b + 4) == 1 && get(b + 8) == ns &&
         get(b + 12) == get(b + 16) && get(b + 20) == ~ns && get(b + 24) == ~get(b + 12) &&
         get(b + 28) == ~get(b + 16) && get(b + 32) == 0 && get(b + 36) == crc(b, 36);
}
IdentityAllocation outcome(IdentityStatus status, int error = 0, uint64_t id = 0) {
  IdentityAllocation r;
  r.status = status;
  r.error = error;
  r.id = id;
  return r;
}
IdentityStatus readSlot(IdentityLedgerIO &io, unsigned slot, Bytes &b) {
  if (!io.open(slot, false, false))
    return io.error() == ENOENT ? IdentityStatus::IdentityUnavailable : IdentityStatus::MediaError;
  const int n = io.read(b, sizeof(b));
  uint8_t extra;
  const int end = n == int(sizeof(b)) ? io.read(&extra, 1) : 0;
  const bool closed = io.close();
  if (n < 0 || end < 0 || !closed)
    return IdentityStatus::MediaError;
  return n == int(sizeof(b)) && end == 0 ? IdentityStatus::Committed
                                         : IdentityStatus::LedgerCorrupt;
}
bool commit(IdentityLedgerIO &io, unsigned slot, const Bytes &b, bool create) {
  if (!io.open(slot, true, create))
    return false;
  const bool written = io.write(b, sizeof(b)) == int(sizeof(b));
  const bool synced = written && io.sync();
  const bool closed = io.close(); // Always close, including uncertain write/sync.
  if (!written || !synced || !closed)
    return false;
  Bytes observed;
  return readSlot(io, slot, observed) == IdentityStatus::Committed &&
         std::memcmp(b, observed, sizeof(b)) == 0;
}
} // namespace
IdentityAllocation SessionIdentityAllocator::reserve() {
  if (attempted_)
    return result_;
  attempted_ = true;
  if (!namespace_)
    return result_ = outcome(IdentityStatus::IdentityUnavailable);
  Bytes slots[2];
  for (unsigned i = 0; i < 2; ++i) {
    const auto status = readSlot(io_, i, slots[i]);
    if (status != IdentityStatus::Committed)
      return result_ = outcome(status, io_.error());
    if (!valid(slots[i], namespace_))
      return result_ = outcome(IdentityStatus::LedgerCorrupt);
  }
  const uint32_t a = get(slots[0] + 12), b = get(slots[1] + 12);
  const uint32_t high = a > b ? a : b, low = a > b ? b : a;
  if ((high != 0 && high - low != 1) || (high == 0 && low != 0))
    return result_ = outcome(IdentityStatus::LedgerCorrupt);
  if (high == std::numeric_limits<uint32_t>::max())
    return result_ = outcome(IdentityStatus::Exhausted);
  Bytes next;
  record(next, namespace_, high + 1);
  if (!commit(io_, a <= b ? 0 : 1, next, false))
    return result_ = outcome(IdentityStatus::CommitUncertain, io_.error());
  return result_ = outcome(IdentityStatus::Committed, 0, (uint64_t(namespace_) << 32) | (high + 1));
}
IdentityAllocation SessionIdentityAllocator::commission(IdentityLedgerIO &io, uint32_t ns) {
  if (!ns)
    return outcome(IdentityStatus::IdentityUnavailable);
  Bytes b;
  record(b, ns, 0);
  for (unsigned i = 0; i < 2; ++i)
    if (!commit(io, i, b, true))
      return outcome(IdentityStatus::CommitUncertain, io.error());
  return outcome(IdentityStatus::Committed); // Baseline, deliberately no session ID.
}
} // namespace ridesync

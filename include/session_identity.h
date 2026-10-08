#pragma once
#include <cstddef>
#include <cstdint>
namespace ridesync {
enum class IdentityStatus : uint8_t {
  Pending,
  Committed,
  IdentityUnavailable,
  LedgerCorrupt,
  Exhausted,
  MediaError,
  CommitUncertain
};
struct IdentityAllocation {
  IdentityStatus status = IdentityStatus::Pending;
  uint64_t id = 0;
  int error = 0;
};
// Exclusive mounted-volume worker only. open selects exactly one fixed slot;
// ordinary writes open an existing file without truncation. Commissioning alone
// uses exclusive_create. A negative read/write is an IO error; read zero is EOF.
class IdentityLedgerIO {
public:
  virtual ~IdentityLedgerIO() = default;
  virtual bool open(unsigned slot, bool write, bool exclusive_create) = 0;
  virtual int read(uint8_t *bytes, size_t length) = 0;
  virtual int write(const uint8_t *bytes, size_t length) = 0;
  virtual bool sync() = 0;
  virtual bool close() = 0;
  virtual int error() const = 0;
};
class SessionIdentityAllocator {
public:
  static constexpr size_t kSlotBytes = 40;
  SessionIdentityAllocator(IdentityLedgerIO &io, uint32_t commissioned_namespace)
      : io_(io), namespace_(commissioned_namespace) {}
  // One reservation per allocator object; subsequent calls return the copied
  // outcome. Create a fresh owner for a new session. No retry after uncertainty.
  IdentityAllocation reserve();
  // Explicit offline provisioning only, never invoked by reserve or boot.
  // A partial commission refuses normal recovery; retire this namespace/card.
  static IdentityAllocation commission(IdentityLedgerIO &io, uint32_t never_used_namespace);

private:
  IdentityLedgerIO &io_;
  const uint32_t namespace_;
  bool attempted_ = false;
  IdentityAllocation result_;
};
} // namespace ridesync

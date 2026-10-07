#pragma once
#include <array>
#include <cstdint>
namespace ridesync {
// Only an authenticated identity or verified stack identity mapping may create
// this target; configured/observed MAC strings are never identity evidence.
enum class IdentityType { Public, RandomStatic, UnresolvedPrivate };
struct BondIdentity {
  std::array<uint8_t, 6> address{};
  IdentityType type = IdentityType::UnresolvedPrivate;
  bool verified = false;
};
enum class BondOutcome { Removed, Present, Absent, Busy, Error, Indeterminate, Refused };
class BondResetPort {
public:
  virtual ~BondResetPort() = default;
  virtual bool admitted() const = 0;
  virtual BondOutcome remove(const BondIdentity &) = 0;
  virtual bool quiesce() = 0;
  // Must verify all targeted stack-owned security/CCCD records; simple
  // isBonded enumeration alone cannot establish complete deletion.
  virtual BondOutcome verify(const BondIdentity &) = 0;
};
BondOutcome resetPairing(BondResetPort &, const BondIdentity &);
// Future stack store-status callback MUST refuse overflow before pairing.
// Never delegate to NimBLE's default oldest-peer eviction callback.
bool admitBond(bool stack_ready, bool restore_verified, bool overflow_refusal_installed,
               unsigned used, unsigned capacity, bool existing_verified_identity);
} // namespace ridesync

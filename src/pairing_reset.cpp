#include "pairing_reset.h"
namespace ridesync {
BondOutcome resetPairing(BondResetPort &port, const BondIdentity &identity) {
  bool nonzero = false;
  for (auto b : identity.address)
    nonzero = nonzero || b != 0;
  if (!identity.verified || !nonzero ||
      (identity.type != IdentityType::Public && identity.type != IdentityType::RandomStatic) ||
      (identity.type == IdentityType::RandomStatic && (identity.address[5] & 0xc0) != 0xc0) ||
      !port.admitted())
    return BondOutcome::Refused;
  auto result = port.remove(identity);
  if (result == BondOutcome::Busy) {
    if (!port.admitted() || !port.quiesce() || !port.admitted())
      return BondOutcome::Busy;
    result = port.remove(identity);
  }
  if (result != BondOutcome::Removed && result != BondOutcome::Absent)
    return result;
  if (!port.admitted())
    return BondOutcome::Indeterminate;
  return port.verify(identity) == BondOutcome::Absent ? BondOutcome::Removed
                                                      : BondOutcome::Indeterminate;
}
bool admitBond(bool ready, bool restored, bool overflow_refusal, unsigned used, unsigned capacity,
               bool existing) {
  return ready && restored && overflow_refusal && capacity && used <= capacity &&
         (existing || used < capacity);
}
} // namespace ridesync

#pragma once
#include "session_identity.h"
#include "storage.h"
namespace ridesync {
// One worker owns mount, allocator, Storage IO and final unmount. One control
// context polls copied allocation, constructs its clock/session only on Committed,
// then binds once. Sink/IO/Storage/owner live through workerFinished; producers
// quiesce before destruction. cancel is terminal, including while waiting bind.
class SessionStorageOwner {
public:
  SessionStorageOwner(StorageSink &sink, IdentityLedgerIO &io, uint32_t ns)
      : sink_(sink), allocator_(io, ns) {}
  IdentityAllocation allocation() const;
  bool bind(Storage &storage);
  void cancel();
  // Worker only. true means final object access is complete.
  bool workerStep();
  bool workerFinished() const { return finished_.load(std::memory_order_acquire); }

private:
  StorageSink &sink_;
  SessionIdentityAllocator allocator_;
  IdentityAllocation allocation_;
  std::atomic<bool> published_{false}, cancelled_{false}, finished_{false};
  std::atomic<Storage *> bound_{nullptr};
  bool control_bound_ = false, control_cancelled_ = false;
  enum class Phase { Allocate, WaitBind, Storage, Done } phase_ = Phase::Allocate;
  Storage *storage_ = nullptr;
};
} // namespace ridesync

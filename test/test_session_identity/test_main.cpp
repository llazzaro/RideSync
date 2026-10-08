#include "config_bootstrap.h"
#include "session_identity.h"
#include "session_storage_owner.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <string>
#include <thread>
#include <unity.h>
#include <vector>
using namespace ridesync;
// Ordered durable medium model. Each completed sync survives reboot. Every
// boundary can fail; partial writes may persist even without acknowledged sync.
struct Medium : IdentityLedgerIO {
  std::array<std::vector<uint8_t>, 2> slots, before_write;
  bool pending_write = false, persist_unsynced = false;
  int selected = -1, err = 0, fail = -1, boundary = 0;
  size_t offset = 0, partial = 40;
  bool corrupt_verify = false;
  bool hit() {
    if (boundary++ == fail) {
      err = EIO;
      return true;
    }
    return false;
  }
  bool open(unsigned s, bool write, bool create) override {
    if (hit())
      return false;
    if (create && !slots[s].empty()) {
      err = EEXIST;
      return false;
    }
    if (!create && slots[s].empty()) {
      err = ENOENT;
      return false;
    }
    (void)write;
    selected = int(s);
    offset = 0;
    return true;
  }
  int read(uint8_t *b, size_t n) override {
    if (hit())
      return -1;
    n = std::min(n, slots[selected].size() - offset);
    memcpy(b, slots[selected].data() + offset, n);
    if (corrupt_verify && boundary > 10 && n == 40)
      b[12] ^= 1;
    offset += n;
    return int(n);
  }
  int write(const uint8_t *b, size_t n) override {
    if (hit())
      return -1;
    if (!pending_write)
      before_write = slots;
    pending_write = true;
    n = std::min(n, partial);
    slots[selected].resize(std::max(slots[selected].size(), offset + n));
    memcpy(slots[selected].data() + offset, b, n);
    offset += n;
    return int(n);
  }
  bool sync() override {
    if (hit())
      return false;
    pending_write = false;
    return true;
  }
  bool close() override {
    selected = -1;
    return !hit();
  }
  int error() const override { return err; }
  void reboot() {
    if (pending_write && !persist_unsynced)
      slots = before_write;
    pending_write = false;
    selected = -1;
    offset = 0;
    err = 0;
    fail = -1;
    boundary = 0;
    partial = 40;
  }
};
Medium baseline() {
  Medium m;
  TEST_ASSERT_EQUAL(int(IdentityStatus::Committed),
                    int(SessionIdentityAllocator::commission(m, 7).status));
  m.reboot();
  return m;
}
void boot_auto_advances_and_repeated_reserve_is_one_session() {
  auto m = baseline();
  SessionIdentityAllocator first(m, 7);
  TEST_ASSERT_EQUAL_UINT64(0x0000000700000001ULL, first.reserve().id);
  TEST_ASSERT_EQUAL_UINT64(0x0000000700000001ULL, first.reserve().id);
  m.reboot();
  SessionIdentityAllocator second(m, 7);
  TEST_ASSERT_EQUAL_UINT64(0x0000000700000002ULL, second.reserve().id);
  // No CSV file exists in this fixture: committed-but-unused and deleted logs
  // cannot make a counter free again. No camera/NVS object participates.
  m.reboot();
  SessionIdentityAllocator third(m, 7);
  TEST_ASSERT_EQUAL_UINT64(0x0000000700000003ULL, third.reserve().id);
}
void blank_replacement_and_corruption_refuse_without_writes() {
  Medium blank;
  SessionIdentityAllocator missing(blank, 7);
  TEST_ASSERT_EQUAL(int(IdentityStatus::IdentityUnavailable), int(missing.reserve().status));
  TEST_ASSERT_TRUE(blank.slots[0].empty());
  auto m = baseline();
  SessionIdentityAllocator other(m, 8);
  TEST_ASSERT_EQUAL(int(IdentityStatus::LedgerCorrupt), int(other.reserve().status));
  m = baseline();
  m.slots[1].clear();
  SessionIdentityAllocator lone(m, 7);
  TEST_ASSERT_EQUAL(int(IdentityStatus::IdentityUnavailable), int(lone.reserve().status));
  m = baseline();
  m.slots[1][12] ^= 1;
  SessionIdentityAllocator corrupt(m, 7);
  TEST_ASSERT_EQUAL(int(IdentityStatus::LedgerCorrupt), int(corrupt.reserve().status));
  m = baseline();
  SessionIdentityAllocator zero(m, 0);
  TEST_ASSERT_EQUAL(int(IdentityStatus::IdentityUnavailable), int(zero.reserve().status));
}
void every_io_boundary_refuses_uncertainty_and_reboot_never_reuses_admitted_id() {
  auto initial = baseline();
  SessionIdentityAllocator admitted(initial, 7);
  TEST_ASSERT_EQUAL_UINT64(0x700000001ULL, admitted.reserve().id);
  initial.reboot();
  auto probe = initial;
  SessionIdentityAllocator complete(probe, 7);
  TEST_ASSERT_EQUAL_UINT64(0x700000002ULL, complete.reserve().id);
  const int boundaries = probe.boundary;
  for (int cut = 0; cut < boundaries; ++cut) {
    auto m = initial;
    m.fail = cut;
    SessionIdentityAllocator interrupted(m, 7);
    auto r = interrupted.reserve();
    TEST_ASSERT_NOT_EQUAL(int(IdentityStatus::Committed), int(r.status));
    TEST_ASSERT_EQUAL_UINT64(0, r.id);
    m.reboot();
    SessionIdentityAllocator rebooted(m, 7);
    auto next = rebooted.reserve();
    if (next.status == IdentityStatus::Committed)
      TEST_ASSERT_TRUE(next.id > 0x700000001ULL);
  }
  for (bool persist_partial : {false, true}) {
    for (size_t tear = 0; tear < SessionIdentityAllocator::kSlotBytes; ++tear) {
      auto m = initial;
      m.partial = tear;
      m.persist_unsynced = persist_partial;
      SessionIdentityAllocator torn(m, 7);
      TEST_ASSERT_EQUAL(int(IdentityStatus::CommitUncertain), int(torn.reserve().status));
      m.reboot();
      SessionIdentityAllocator rebooted(m, 7);
      auto next = rebooted.reserve();
      if (next.status == IdentityStatus::Committed)
        TEST_ASSERT_TRUE(next.id > 0x700000001ULL);
    }
  }
}
void detectable_stale_slot_refuses_but_whole_valid_rollback_is_indistinguishable() {
  auto m = baseline();
  auto old = m.slots;
  for (unsigned i = 0; i < 3; ++i) {
    m.reboot();
    SessionIdentityAllocator a(m, 7);
    TEST_ASSERT_TRUE(a.reserve().id != 0);
  }
  auto latest = m.slots;
  m.slots[0] = old[0];
  m.reboot();
  SessionIdentityAllocator conflict(m, 7);
  TEST_ASSERT_EQUAL(int(IdentityStatus::LedgerCorrupt), int(conflict.reserve().status));
  m.slots = old;
  m.reboot();
  SessionIdentityAllocator rollback(m, 7);
  // Explicit limitation: without an independent anchor this valid whole-state
  // rollback repeats 1. It is outside the acknowledged-commit-survives premise.
  TEST_ASSERT_EQUAL_UINT64(0x700000001ULL, rollback.reserve().id);
  (void)latest;
}
void commissioning_never_overwrites_and_errors_are_copied() {
  auto m = baseline();
  auto before = m.slots;
  TEST_ASSERT_EQUAL(int(IdentityStatus::CommitUncertain),
                    int(SessionIdentityAllocator::commission(m, 7).status));
  TEST_ASSERT_TRUE(m.slots == before);
  m.reboot();
  m.fail = 0;
  SessionIdentityAllocator io(m, 7);
  auto r = io.reserve();
  TEST_ASSERT_EQUAL(int(IdentityStatus::MediaError), int(r.status));
  TEST_ASSERT_EQUAL(EIO, r.error);
}
struct Sink : StorageSink {
  unsigned mounts = 0, closes = 0, opens = 0;
  bool mounted = false, mount_ok = true, collision = false;
  std::string bytes;
  bool mount() override {
    if (!mounted)
      ++mounts;
    return mounted = mount_ok;
  }
  bool openExclusive(const char *) override {
    ++opens;
    return !collision;
  }
  size_t write(const char *b, size_t n) override {
    bytes.append(b, n);
    return n;
  }
  bool flush() override { return true; }
  void close() override {
    ++closes;
    mounted = false;
  }
};
void owner_requires_committed_matching_storage_and_final_close() {
  auto m = baseline();
  Sink sink, other;
  SessionStorageOwner owner(sink, m, 7);
  TEST_ASSERT_EQUAL(int(IdentityStatus::Pending), int(owner.allocation().status));
  TEST_ASSERT_EQUAL_UINT32(0, sink.mounts);
  TEST_ASSERT_FALSE(owner.workerStep());
  auto allocation = owner.allocation();
  TEST_ASSERT_EQUAL_UINT64(0x700000001ULL, allocation.id);
  TEST_ASSERT_EQUAL_UINT32(0, sink.opens);
  Storage wrong_id(sink, {42, "fw", "test"});
  Storage wrong_sink(other, {allocation.id, "fw", "test"});
  Storage invalid(sink, {allocation.id, "bad\nfw", "test"});
  TEST_ASSERT_FALSE(owner.bind(wrong_id));
  TEST_ASSERT_FALSE(owner.bind(wrong_sink));
  TEST_ASSERT_FALSE(owner.bind(invalid));
  Storage storage(sink, {allocation.id, "fw", "test"});
  TEST_ASSERT_TRUE(owner.bind(storage));
  TEST_ASSERT_FALSE(owner.bind(storage));
  storage.requestStop();
  for (unsigned i = 0; i < 100 && !owner.workerFinished(); ++i)
    owner.workerStep();
  TEST_ASSERT_TRUE(owner.workerFinished());
  TEST_ASSERT_TRUE(storage.health().stopped);
  TEST_ASSERT_EQUAL_UINT32(1, sink.mounts);
  TEST_ASSERT_EQUAL_UINT32(1, sink.closes);
  TEST_ASSERT_EQUAL_UINT32(0, other.mounts);
}
void unbound_cancel_and_allocator_refusal_release_mount() {
  auto m = baseline();
  Sink sink;
  SessionStorageOwner owner(sink, m, 7);
  owner.workerStep();
  owner.cancel();
  owner.workerStep();
  TEST_ASSERT_TRUE(owner.workerFinished());
  TEST_ASSERT_EQUAL_UINT32(1, sink.closes);
  Storage late(sink, {0x700000001ULL, "fw", "test"});
  TEST_ASSERT_FALSE(owner.bind(late));
  m.reboot();
  SessionIdentityAllocator next(m, 7);
  TEST_ASSERT_EQUAL_UINT64(0x700000002ULL, next.reserve().id);
  Medium blank;
  Sink absent;
  SessionStorageOwner refused(absent, blank, 7);
  TEST_ASSERT_TRUE(refused.workerStep());
  TEST_ASSERT_TRUE(refused.workerFinished());
  TEST_ASSERT_EQUAL(int(IdentityStatus::IdentityUnavailable), int(refused.allocation().status));
  TEST_ASSERT_EQUAL_UINT32(1, absent.closes);
  TEST_ASSERT_EQUAL_UINT32(0, absent.opens);
  auto card = baseline();
  Sink failed;
  failed.mount_ok = false;
  SessionStorageOwner no_media(failed, card, 7);
  TEST_ASSERT_TRUE(no_media.workerStep());
  TEST_ASSERT_EQUAL(int(IdentityStatus::MediaError), int(no_media.allocation().status));
  TEST_ASSERT_EQUAL_UINT32(1, failed.closes);
}
void bound_cancel_drains_storage_and_collision_burns_id() {
  for (bool collision : {false, true}) {
    auto m = baseline();
    Sink sink;
    sink.collision = collision;
    SessionStorageOwner owner(sink, m, 7);
    owner.workerStep();
    Storage storage(sink, {owner.allocation().id, "fw", "test"});
    TEST_ASSERT_TRUE(owner.bind(storage));
    owner.cancel();
    for (unsigned i = 0; i < 100 && !owner.workerFinished(); ++i)
      owner.workerStep();
    TEST_ASSERT_TRUE(owner.workerFinished());
    TEST_ASSERT_EQUAL_UINT32(1, sink.closes);
    TEST_ASSERT_EQUAL(collision, storage.health().terminal);
    m.reboot();
    SessionIdentityAllocator next(m, 7);
    TEST_ASSERT_EQUAL_UINT64(0x700000002ULL, next.reserve().id);
  }
}
void change_counter(std::vector<uint8_t> &b, uint32_t value) {
  auto put = [&](unsigned offset, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
      b[offset + i] = uint8_t(v >> (8 * i));
  };
  put(12, value);
  put(16, value);
  put(24, ~value);
  put(28, ~value);
  uint32_t crc = 0xffffffff;
  for (unsigned i = 0; i < 36; ++i) {
    crc ^= b[i];
    for (unsigned j = 0; j < 8; ++j)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  put(36, ~crc);
}
void exhaustion_conflicts_and_oversized_ledgers_refuse() {
  auto m = baseline();
  change_counter(m.slots[0], 0xffffffff);
  change_counter(m.slots[1], 0xfffffffe);
  SessionIdentityAllocator exhausted(m, 7);
  TEST_ASSERT_EQUAL(int(IdentityStatus::Exhausted), int(exhausted.reserve().status));
  m = baseline();
  change_counter(m.slots[0], 4);
  change_counter(m.slots[1], 4);
  SessionIdentityAllocator equal(m, 7);
  TEST_ASSERT_EQUAL(int(IdentityStatus::LedgerCorrupt), int(equal.reserve().status));
  m = baseline();
  m.slots[1].push_back(0);
  SessionIdentityAllocator oversized(m, 7);
  TEST_ASSERT_EQUAL(int(IdentityStatus::LedgerCorrupt), int(oversized.reserve().status));
  m = baseline();
  change_counter(m.slots[0], 0xfffffffe);
  change_counter(m.slots[1], 0xfffffffd);
  SessionIdentityAllocator last(m, 7);
  TEST_ASSERT_EQUAL_UINT64(0x7ffffffffULL, last.reserve().id);
  m.reboot();
  SessionIdentityAllocator final(m, 7);
  TEST_ASSERT_EQUAL(int(IdentityStatus::Exhausted), int(final.reserve().status));
}
struct RefusedNvs : ConfigStore {
  unsigned calls = 0;
  bool allowed() const override { return false; }
  StoreResult read(unsigned, ConfigRecord &) override {
    ++calls;
    return {StoreStatus::Refused};
  }
  StoreResult write(unsigned, const ConfigRecord &) override {
    ++calls;
    return {StoreStatus::Refused};
  }
};
void nvs_refusal_and_camera_safe_mode_do_not_prevent_local_identity() {
  RefusedNvs nvs;
  ConfigPersistence persistence(nvs);
  SettingsPublication publication;
  SettingsRequests requests;
  ConfigBootstrap config(persistence, publication, requests, 1);
  config.start(true, false);
  SettingsSnapshot snapshot;
  TEST_ASSERT_TRUE(publication.take(snapshot));
  TEST_ASSERT_EQUAL(int(PersistStatus::Refused), int(snapshot.outcome.status));
  SettingsQualification q;
  q.local_telemetry = true;
  q.cameras = true;
  q.safe_mode = true;
  auto admission = admitSettings(snapshot, q);
  TEST_ASSERT_TRUE(admission.local_telemetry);
  TEST_ASSERT_FALSE(admission.cameras);
  auto medium = baseline();
  Sink sink;
  SessionStorageOwner owner(sink, medium, 7);
  owner.workerStep();
  TEST_ASSERT_EQUAL_UINT64(0x700000001ULL, owner.allocation().id);
  TEST_ASSERT_EQUAL_UINT32(0, nvs.calls);
  owner.cancel();
  TEST_ASSERT_TRUE(owner.workerStep());
}
void cancelled_before_worker_does_no_media_or_session_io() {
  auto m = baseline();
  Sink sink;
  SessionStorageOwner owner(sink, m, 7);
  owner.cancel();
  TEST_ASSERT_TRUE(owner.workerStep());
  TEST_ASSERT_EQUAL_UINT32(0, sink.mounts);
  TEST_ASSERT_EQUAL_UINT32(0, sink.opens);
  TEST_ASSERT_TRUE(owner.workerFinished());
  TEST_ASSERT_EQUAL_UINT32(0, m.boundary);
}
void successful_io_with_wrong_readback_never_publishes() {
  auto m = baseline();
  m.corrupt_verify = true;
  SessionIdentityAllocator corrupt(m, 7);
  auto r = corrupt.reserve();
  TEST_ASSERT_EQUAL(int(IdentityStatus::CommitUncertain), int(r.status));
  TEST_ASSERT_EQUAL_UINT64(0, r.id);
  m.corrupt_verify = false;
  m.reboot();
  SessionIdentityAllocator next(m, 7);
  TEST_ASSERT_EQUAL_UINT64(0x700000002ULL, next.reserve().id);
}
void control_bind_cancel_publications_have_no_unserviced_storage() {
  for (unsigned i = 0; i < 64; ++i) {
    auto m = baseline();
    Sink sink;
    SessionStorageOwner owner(sink, m, 7);
    std::thread worker([&] {
      while (!owner.workerStep())
        std::this_thread::yield();
    });
    IdentityAllocation a;
    while ((a = owner.allocation()).status == IdentityStatus::Pending)
      std::this_thread::yield();
    TEST_ASSERT_EQUAL_UINT64(0x700000001ULL, a.id);
    Storage storage(sink, {a.id, "fw", "test"});
    TEST_ASSERT_TRUE(owner.bind(storage));
    owner.cancel();
    while (!owner.workerFinished())
      std::this_thread::yield();
    worker.join();
    TEST_ASSERT_TRUE(storage.health().stopped);
    TEST_ASSERT_EQUAL_UINT32(1, sink.closes);
  }
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(boot_auto_advances_and_repeated_reserve_is_one_session);
  RUN_TEST(blank_replacement_and_corruption_refuse_without_writes);
  RUN_TEST(every_io_boundary_refuses_uncertainty_and_reboot_never_reuses_admitted_id);
  RUN_TEST(detectable_stale_slot_refuses_but_whole_valid_rollback_is_indistinguishable);
  RUN_TEST(commissioning_never_overwrites_and_errors_are_copied);
  RUN_TEST(owner_requires_committed_matching_storage_and_final_close);
  RUN_TEST(unbound_cancel_and_allocator_refusal_release_mount);
  RUN_TEST(bound_cancel_drains_storage_and_collision_burns_id);
  RUN_TEST(exhaustion_conflicts_and_oversized_ledgers_refuse);
  RUN_TEST(nvs_refusal_and_camera_safe_mode_do_not_prevent_local_identity);
  RUN_TEST(cancelled_before_worker_does_no_media_or_session_io);
  RUN_TEST(successful_io_with_wrong_readback_never_publishes);
  RUN_TEST(control_bind_cancel_publications_have_no_unserviced_storage);
  return UNITY_END();
}

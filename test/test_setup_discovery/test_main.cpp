#include "setup_discovery.h"
#include "setup_service.h"
#include <cassert>
#include <cstring>
#include <unity.h>
using namespace ridesync;
struct Host : BleHost {
  BleContext *context = nullptr;
  bool quiet = false, release = false;
  unsigned starts = 0, cancels = 0;
  BleBondAdmission admission;
  BleHostState start(bool, bool) override {
    ++starts;
    return BleHostState::Ready;
  }
  BleHostState state() const override { return BleHostState::Ready; }
  BleFault fault() const override { return BleFault::None; }
  void sealStartup() override {}
  BleBondAdmission bondAdmission(const BondIdentity &) override { return admission; }
  int submit(const BleCommand &c, BleContext &ctx) override {
    assert(c.phase == BlePhase::Scan && c.duration_ms == 10000);
    context = &ctx;
    quiet = false;
    return 0;
  }
  int retire(BleContext &) override {
    assert(false);
    return 0;
  }
  int cancelScan(BleContext &) override {
    ++cancels;
    return 0;
  }
  bool quiescent(const BleContext &) const override { return quiet; }
  bool releaseContext(BleContext &) override { return release; }
  uint16_t mtu(uint16_t) const override { return 23; }
  void advertisement(unsigned value, uint8_t type = 0, uint32_t generation = 0) {
    BleEvent e;
    e.kind = BleEventKind::Advertisement;
    e.peer = context->peer;
    e.generation = generation ? generation : context->generation;
    e.phase = BlePhase::Scan;
    e.identity.address[0] = value;
    e.observed_address_type = type;
    e.rssi = -42;
    e.size = 7;
    e.bytes[0] = 2;
    e.bytes[1] = 9;
    e.bytes[2] = 'A';
    e.bytes[3] = 3;
    e.bytes[4] = 3;
    e.bytes[5] = 0x80;
    e.bytes[6] = 0xbe;
    context->receiver->copied(*context, e);
  }
  void terminal() {
    BleEvent e;
    e.kind = BleEventKind::ScanComplete;
    e.peer = context->peer;
    e.generation = context->generation;
    e.phase = BlePhase::Scan;
    context->terminal.store(true);
    context->receiver->copied(*context, e);
  }
};
void bounded_discovery_and_selection() {
  Host h;
  SetupDiscovery owner(h);
  assert(!owner.begin(false, 0) && h.starts == 0);
  assert(owner.begin(true, 0));
  assert(owner.scan(10, 0));
  assert(!owner.scan(11, 1));
  for (unsigned i = 0; i < 17; ++i) {
    h.advertisement(i);
    owner.service(2);
  }
  assert(owner.count() == 16 && owner.truncated());
  h.advertisement(0);
  owner.service(3);
  assert(owner.count() == 16);
  assert(owner.candidate(0).rssi == -42 && owner.candidate(0).service_count == 1);
  SourceConfig config;
  CameraConfig draft;
  assert(owner.select(10, 0, CameraModel::GO3S, config, 0, draft) == SetupSelectionOutcome::Busy);
  h.terminal();
  owner.service(4);
  assert(!owner.released());
  h.quiet = true;
  owner.service(5);
  assert(!owner.released());
  h.release = true;
  owner.service(6);
  assert(owner.released());
  assert(owner.select(9, 0, CameraModel::GO3S, config, 0, draft) == SetupSelectionOutcome::Stale);
  assert(owner.select(10, 0, CameraModel::X5, config, 0, draft) == SetupSelectionOutcome::X5Manual);
  assert(owner.select(10, 0, CameraModel::GO3S, config, 0, draft) ==
         SetupSelectionOutcome::Unresolved);
  assert(!draft.enabled);
  SetupIdentityMapping map;
  map.observed_type = 0;
  map.identity.address[0] = 5;
  map.identity.verified = true;
  map.identity.type = IdentityType::Public;
  owner.setIdentityMappings(&map, 1);
  assert(owner.select(10, 0, CameraModel::GO3S, config, 0, draft) ==
         SetupSelectionOutcome::BondCapacity);
  h.admission.stack_ready = h.admission.restore_verified = h.admission.refusal_installed = true;
  h.admission.existing_verified_identity = h.admission.identity_matches =
      h.admission.persistence_allowed = true;
  h.admission.used = h.admission.capacity = 4;
  assert(owner.select(10, 0, CameraModel::GO3S, config, 0, draft) ==
         SetupSelectionOutcome::NeedsCommissioning);
  h.admission.identity_matches = h.admission.existing_verified_identity = false;
  h.admission.used = 0;
  assert(owner.select(10, 0, CameraModel::DJI_ACTION4, config, 0, draft) ==
         SetupSelectionOutcome::NeedsCommissioning);
  assert(!draft.enabled);
  h.admission.identity_matches = h.admission.existing_verified_identity = true;
  assert(owner.select(10, 0, CameraModel::GO3S, config, 0, draft) ==
         SetupSelectionOutcome::NeedsCommissioning);
  config.count = 1;
  config.cameras[0] = draft;
  assert(owner.select(10, 0, CameraModel::GO3S, config, 1, draft) ==
         SetupSelectionOutcome::DuplicateIdentity);
  config.count = 4;
  assert(owner.select(10, 0, CameraModel::GO3S, config, 4, draft) ==
         SetupSelectionOutcome::Capacity);
  uint32_t old_generation = h.context->generation;
  assert(owner.scan(11, 10));
  h.advertisement(0, 1);
  owner.service(11);
  assert(owner.count() == 1 && owner.candidate(0).address_type == 1);
  h.advertisement(0, 0);
  owner.service(12);
  assert(owner.count() == 2);
  BleContext late;
  late.peer = kBlePeers;
  late.phase = BlePhase::Scan;
  late.generation = old_generation;
  BleEvent stale;
  stale.kind = BleEventKind::Advertisement;
  stale.observed_address_type = 0;
  stale.identity.address[0] = 99;
  h.context->receiver->copied(late, stale);
  owner.service(13);
  assert(owner.count() == 2);
  owner.cancel(10);
  assert(h.cancels == 0);
  owner.service(10010);
  assert(h.cancels == 1 && !owner.released());
  h.terminal();
  h.quiet = true;
  owner.service(10011);
  assert(owner.released());
  assert(owner.count() == 2);
}

void discovery_transcript() {
  Host h;
  SetupDiscovery owner(h);
  assert(owner.begin(true, 0));
  SetupService service;
  service.reset(0, 1, 11, 22);
  service.setDiscovery(&owner);
  SettingsSnapshot snapshot;
  snapshot.epoch = 1;
  snapshot.generation = 1;
  snapshot.completed = snapshot.effective = true;
  snapshot.settings.capacity = 4;
  service.setSnapshot(snapshot);
  service.setIsolationAvailable(true);
  char response[kSetupFrameBytes + 1];
  auto command = [&](const char *text) {
    for (const char *p = text; *p; ++p)
      service.feed(*p, 1);
    service.feed('\n', 1);
    assert(service.takeResponse(response, sizeof response));
  };
  command("@RS1 0 1 1 1 ENTER 11 PARKED");
  service.acknowledgeIsolation(true);
  assert(service.takeResponse(response, sizeof response));
  command("@RS1 22 2 1 1 DRAFT_BEGIN");
  command("@RS1 22 3 1 1 DISCOVER");
  assert(strstr(response, "Pending None"));
  h.advertisement(4);
  h.advertisement(5);
  service.tick(2);
  command("@RS1 22 4 1 1 INVENTORY 0");
  assert(strstr(response, "name=41"));
  command("@RS1 22 5 1 1 SELECT 0,2,0");
  assert(strstr(response, "Error Busy"));
  h.terminal();
  h.quiet = h.release = true;
  service.tick(3);
  command("@RS1 22 6 1 1 SELECT 0,2,0");
  assert(strstr(response, "UnresolvedIdentity"));
  command("@RS1 22 7 1 1 DISPATCH");
  assert(strstr(response, "Error Invalid"));
  SetupIdentityMapping mapping;
  mapping.observed[0] = 5;
  mapping.observed_type = 0;
  mapping.identity.address[0] = 5;
  mapping.identity.type = IdentityType::Public;
  mapping.identity.verified = true;
  owner.setIdentityMappings(&mapping, 1);
  // Verified selection also needs the independently admitted guarded bond store.
  h.admission.stack_ready = h.admission.restore_verified = h.admission.refusal_installed = true;
  h.admission.identity_matches = h.admission.persistence_allowed = true;
  h.admission.existing_verified_identity = true;
  h.admission.used = 1;
  h.admission.capacity = 4;
  command("@RS1 22 8 1 1 SELECT 1,2,1");
  assert(strstr(response, "NeedsCommissioning") && strstr(response, "save_allowed=0"));
  command("@RS1 22 9 1 1 DISPATCH");
  assert(strstr(response, "Error Invalid"));
  command("@RS1 22 10 1 1 SELECT 0,1,0");
  assert(strstr(response, "X5ManualCommissioning"));
  command("@RS1 22 11 1 1 DISCOVER");
  command("@RS1 22 12 1 1 EXIT");
  service.ownerReleased(true);
  assert(service.closing());
  h.terminal();
  h.quiet = true;
  service.tick(4);
  service.ownerReleased(true);
  assert(!service.closing());
  assert(service.takeResponse(response, sizeof response));
}
void explicit_lazy_initialization() {
  Host h;
  SetupDiscovery owner(h);
  assert(h.starts == 0 && !owner.initializationRequested());
  assert(owner.scan(7, 0) && owner.initializationRequested() && h.starts == 0);
  assert(!owner.scan(8, 1));
  owner.cancel(7);
  assert(!owner.initializationRequested() && h.starts == 0);
  assert(owner.scan(9, 2));
  owner.initialize(false, 3);
  assert(h.starts == 0 && owner.outcome() == SetupDiscoveryOutcome::Unavailable);
  assert(owner.scan(10, 4));
  owner.initialize(true, 5);
  assert(h.starts == 1 && owner.outcome() == SetupDiscoveryOutcome::Scanning);
  owner.cancel(10);
  h.terminal();
  h.quiet = h.release = true;
  owner.service(6);
  assert(owner.released());
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(bounded_discovery_and_selection);
  RUN_TEST(discovery_transcript);
  RUN_TEST(explicit_lazy_initialization);
  return UNITY_END();
}

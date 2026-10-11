#pragma once
// Synthetic identity/configuration and transport-event setup only; expected
// camera display bytes remain independent fixture observations.
#include "x5_runtime.h"
#include <cstring>

namespace x5_fixture {
inline void configure(ridesync::X5Qualification &q, ridesync::SourceConfig &source) {
  q.enabled = true;
  q.identity.verified = true;
  q.identity.type = ridesync::IdentityType::Public;
  q.identity.address = {{1, 2, 3, 4, 5, 6}};
  q.store.qualification_record = 1;
  q.display.profile = ridesync::insta360::Ce80DisplayProfile::X5CapturedDisplayV1;
  std::memcpy(q.firmware.data(), "1.11.10", 7);
  q.firmware_size = 7;
  source.count = 1;
  auto &c = source.cameras[0];
  c.name = "Synthetic X5";
  c.model = ridesync::CameraModel::X5;
  c.family = ridesync::CameraFamily::Insta360;
  c.identifier = "06:05:04:03:02:01";
  c.address_type = ridesync::AddressType::Public;
}
template <typename Harness> void event(Harness &h, ridesync::X5InputKind kind) {
  ridesync::X5Input e;
  e.kind = kind;
  e.connection = h.port.token.connection;
  e.operation = h.port.token.operation;
  e.handle = 7;
  e.sequence = ++h.port.seq;
  e.received_ms = h.clock.time;
  e.identity = h.q.identity;
  h.port.events.push_back(e);
}
} // namespace x5_fixture

#pragma once
// Shared transport scheduling only. Camera replies/CRC and expected bytes stay
// independent in each model's synthetic fixture.
#include "ble_remote.h"
#include <algorithm>
#include <vector>

namespace adapter_fixture {
template <typename Host> void complete(Host &host, size_t n) {
  ridesync::BleEvent e;
  e.connection = host.commands[n].connection;
  e.handle = host.commands[n].handle;
  if (host.commands[n].phase == ridesync::BlePhase::VerifySubscription) {
    e.size = 2;
    e.bytes[0] = 1;
  }
  host.deliver(*host.contexts[n], e, true);
}
template <typename Host>
void notify(Host &host, uint8_t peer, const std::vector<uint8_t> &packet, size_t from,
            size_t size) {
  ridesync::BleEvent e;
  e.kind = ridesync::BleEventKind::Notification;
  e.connection = 10 + peer;
  e.handle = 6;
  if (!size)
    size = packet.size() - from;
  e.size = size;
  std::copy(packet.begin() + from, packet.begin() + from + size, e.bytes.begin());
  host.deliver(*host.links[peer], e);
}
template <typename Rig> bool synchronize(Rig &rig) {
  bool injected = false;
  for (unsigned i = 0; i < rig.count; ++i) {
    if (rig.auto_sync && rig.subscribed[i] && !rig.synced[i]) {
      rig.cameraSync(i);
      rig.synced[i] = injected = true;
    }
  }
  return injected;
}
template <typename Rig> void pump(Rig &rig) {
  for (unsigned n = 0; n < 100; ++n) {
    rig.adapter.service();
    rig.adapter.service();
    if (rig.handled < rig.host.commands.size())
      rig.handle(rig.handled++);
    else if (!synchronize(rig))
      break;
  }
  rig.adapter.service();
}
template <typename Host>
void services(Host &host, size_t n, const ridesync::BleCommand &c, ridesync::BleContext &x,
              ridesync::BleEvent &e) {
  if (!host.missing) {
    e.kind = ridesync::BleEventKind::Service;
    e.uuid = c.uuid;
    e.start = 1;
    e.end = 12;
    host.deliver(x, e);
  }
  host.complete(n);
}
template <typename Host>
void descriptors(Host &host, size_t n, const ridesync::BleCommand &c, ridesync::BleContext &x,
                 ridesync::BleEvent &e) {
  if (host.cccd) {
    e.kind = ridesync::BleEventKind::Descriptor;
    e.uuid = ridesync::BleUuid::shortUuid(0x2902);
    e.handle = c.start + 1;
    host.deliver(x, e);
  }
  host.complete(n);
}
template <typename Host>
void characteristics(Host &host, size_t n, ridesync::BleContext &x, ridesync::BleEvent &e,
                     const ridesync::BleUuid &write_uuid, const ridesync::BleUuid &notify_uuid,
                     uint8_t write_property, uint8_t wrong_property) {
  for (unsigned j = 0; j < 2; ++j) {
    e.kind = ridesync::BleEventKind::Characteristic;
    e.uuid = j ? notify_uuid : write_uuid;
    e.start = 2 + j * 3;
    e.handle = e.start + 1;
    e.properties = j ? 0x10 : (host.property ? write_property : wrong_property);
    host.deliver(x, e);
  }
  host.complete(n);
}
// Only finite transport phases are shared. Model-specific responses, expected
// characteristic UUIDs/properties, and incoming bytes remain in each Rig.
template <typename Rig> void handle(Rig &rig, size_t n, ridesync::BlePhase write_phase) {
  const auto c = rig.host.commands[n];
  auto &x = *rig.host.contexts[n];
  ridesync::BleEvent e;
  e.connection = 10 + x.peer;
  switch (c.phase) {
  case ridesync::BlePhase::Connect:
    rig.connected(n, c, x, e);
    break;
  case ridesync::BlePhase::Security:
    rig.secured(n, c, x, e);
    break;
  case ridesync::BlePhase::Services:
    services(rig.host, n, c, x, e);
    break;
  case ridesync::BlePhase::Characteristics:
    rig.characteristics(n, c, x, e);
    break;
  case ridesync::BlePhase::Descriptors:
    descriptors(rig.host, n, c, x, e);
    break;
  case ridesync::BlePhase::Subscribe:
  case ridesync::BlePhase::VerifySubscription:
    rig.subscribedPhase(n, c, x, e);
    break;
  default:
    if (c.phase == write_phase)
      rig.writeResponse(n, c, x, e);
    break;
  }
}
} // namespace adapter_fixture

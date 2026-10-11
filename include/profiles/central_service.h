#pragma once
#include "ble_remote.h"
#include "recording_manager.h"
namespace ridesync {
// Shared serialized-owner chronology. Callbacks retain model-specific transaction
// policy, while transport retirement is drained before any manager progression.
template <typename ServicePeer, typename Drain>
void serviceCentralProfile(bool &servicing, BleCentral &central, Clock &clock,
                           CameraManager *&manager, RecordingManager *&group, bool advance,
                           ServicePeer service_peer, Drain drain) {
  if (servicing || !manager)
    return;
  servicing = true;
  central.service(clock.now());
  for (uint8_t peer = 0; peer < kBlePeers; ++peer)
    service_peer(peer);
  drain();
  if (advance && group)
    group->tick();
  else if (advance)
    manager->tick();
  servicing = false;
}
} // namespace ridesync

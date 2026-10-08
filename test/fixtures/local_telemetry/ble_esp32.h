#pragma once
#include "ble_remote.h"
namespace ridesync {
class Esp32BleHost : public BleHost {
public:
  static Esp32BleHost &instance() {
    static Esp32BleHost h;
    return h;
  }
  BleHostState start(bool, bool) override { return BleHostState::Ready; }
  BleHostState state() const override { return BleHostState::Ready; }
  BleFault fault() const override { return BleFault::None; }
  void sealStartup() override {}
  BleBondAdmission bondAdmission(const BondIdentity &) override { return {}; }
  int submit(const BleCommand &, BleContext &) override { return 0; }
  int retire(BleContext &) override { return 0; }
  int cancelScan(BleContext &) override { return 0; }
  bool quiescent(const BleContext &) const override { return true; }
  bool releaseContext(BleContext &) override { return true; }
  uint16_t mtu(uint16_t) const override { return 67; }
};
} // namespace ridesync

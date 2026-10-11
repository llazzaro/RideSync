#pragma once
#include "ble_remote.h"
namespace ridesync {
// Shared BE80 control GATT layout; model qualification and wire codecs remain separate.
inline BleProfileSpec insta360Be80ProfileSpec() {
  BleProfileSpec s;
  s.service_count = 1;
  s.services[0] = BleUuid::shortUuid(0xbe80);
  s.endpoint_count = 2;
  s.endpoints[0].uuid = BleUuid::shortUuid(0xbe81);
  s.endpoints[0].properties = 8; // ATT write response, not a camera ACK.
  s.endpoints[1].uuid = BleUuid::shortUuid(0xbe82);
  s.endpoints[1].properties = 0x10;
  s.endpoints[1].subscribe = 1;
  return s;
}
} // namespace ridesync

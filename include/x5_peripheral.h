#pragma once
#include "ble_pairing_reset.h"
#include "camera_manager.h"
#include "protocol/insta360_codec.h"
namespace ridesync {
enum class X5InputKind : uint8_t {
  Connected,
  Subscribed,
  Unsubscribed,
  Display,
  SendReturned,
  Disconnected,
  Terminal,
  Fault
};
enum class X5Failure : uint8_t {
  None,
  Disabled,
  Qualification,
  Busy,
  Host,
  Subscription,
  UnknownState,
  WrongMode,
  Stale,
  Timeout,
  Transport,
  Cancelled,
  LostInput,
  Exhausted
};
struct X5Qualification {
  bool enabled = false;
  BondIdentity identity;
  BleStoreProof store;
  insta360::Ce80DisplayConfig display;
  std::array<char, 16> firmware{};
  uint8_t firmware_size = 0;
};
struct X5Input {
  X5InputKind kind = X5InputKind::Fault;
  uint32_t connection = 0, operation = 0, sequence = 0, received_ms = 0;
  uint16_t handle = kBleNoHandle, size = 0;
  int status = 0;
  BondIdentity identity;
  std::array<uint8_t, 256> bytes{};
};
struct X5ShutterRequest {
  Token token;
  uint16_t handle = kBleNoHandle;
  uint32_t deadline_ms = 0, observation_sequence = 0, observation_ms = 0;
};
class X5PeripheralPort {
public:
  virtual ~X5PeripheralPort() = default;
  virtual bool configure(const X5Qualification &) = 0;
  virtual bool connect(Token, uint32_t deadline_ms) = 0;
  virtual bool notify(const X5ShutterRequest &) = 0;
  virtual void cancel(Token) = 0;
  virtual void close(uint32_t connection) = 0;
  virtual bool poll(X5Input &) = 0;
  virtual bool takeLoss(uint32_t connection) = 0;
  virtual bool released(uint32_t connection) const = 0;
  virtual void service(uint32_t now_ms) = 0;
};
} // namespace ridesync

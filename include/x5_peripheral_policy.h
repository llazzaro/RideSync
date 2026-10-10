#pragma once
#include "x5_peripheral.h"
namespace ridesync {
// Caller serializes these fixed objects; the ESP32 backend uses a short critical section.
class PeripheralMailbox {
public:
  bool begin(uint32_t generation);
  bool push(const X5Input &);
  bool poll(X5Input &);
  bool takeLoss(uint32_t generation);

private:
  std::array<X5Input, 32> entries_{};
  uint32_t generation_ = 0;
  size_t head_ = 0, count_ = 0;
  bool lost_ = false;
};
class X5PeripheralPolicy {
public:
  bool begin(Token, uint32_t deadline, uint32_t now);
  bool connected(uint16_t handle, uint32_t now);
  void subscription(bool enabled);
  bool enqueue(const X5ShutterRequest &);
  bool pending(X5ShutterRequest &) const;
  bool admit(uint32_t now);
  void cancel(Token);
  void seal();
  void loss();
  void terminal();
  void barrier();
  void callbackEnter();
  void callbackExit();
  void sdkEnter();
  void sdkExit();
  bool released() const;
  bool alive() const { return !sealed_ && connected_; }
  bool subscribed() const { return alive() && subscribed_; }
  uint16_t handle() const { return handle_; }
  uint32_t generation() const { return token_.connection; }

private:
  Token token_;
  X5ShutterRequest request_;
  uint32_t deadline_ = 0, last_operation_ = 0;
  uint16_t handle_ = kBleNoHandle;
  unsigned callbacks_ = 0, sdk_calls_ = 0;
  bool pending_ = false, connected_ = false, subscribed_ = false, sealed_ = true, terminal_ = true,
       quiet_ = true;
};
} // namespace ridesync

#include "x5_peripheral_policy.h"
namespace ridesync {
namespace {
bool due(uint32_t now, uint32_t deadline) { return now - deadline < 0x80000000UL; }
} // namespace
bool PeripheralMailbox::begin(uint32_t generation) {
  if (!generation || generation <= generation_)
    return false;
  generation_ = generation;
  head_ = count_ = 0;
  lost_ = false;
  return true;
}
bool PeripheralMailbox::push(const X5Input &in) {
  if (!generation_ || in.connection != generation_)
    return false;
  if (count_ == entries_.size()) {
    lost_ = true;
    return false;
  }
  entries_[(head_ + count_) % entries_.size()] = in;
  ++count_;
  return true;
}
bool PeripheralMailbox::poll(X5Input &out) {
  if (!count_)
    return false;
  out = entries_[head_];
  head_ = (head_ + 1) % entries_.size();
  --count_;
  return true;
}
bool PeripheralMailbox::takeLoss(uint32_t generation) {
  if (generation != generation_)
    return false;
  const bool out = lost_;
  lost_ = false;
  return out;
}
bool X5PeripheralPolicy::begin(Token token, uint32_t deadline, uint32_t now) {
  if (!released() || !token.connection || !token.operation || !token.hasRoom() ||
      token.connection <= token_.connection || token.operation <= last_operation_ ||
      due(now, deadline) || deadline - now > 15000)
    return false;
  token_ = token;
  last_operation_ = token.operation;
  deadline_ = deadline;
  handle_ = kBleNoHandle;
  pending_ = connected_ = subscribed_ = false;
  sealed_ = terminal_ = quiet_ = false;
  return true;
}
bool X5PeripheralPolicy::connected(uint16_t handle, uint32_t now) {
  if (sealed_ || connected_ || handle == kBleNoHandle || due(now, deadline_)) {
    seal();
    return false;
  }
  handle_ = handle;
  connected_ = true;
  return true;
}
void X5PeripheralPolicy::subscription(bool enabled) {
  subscribed_ = enabled && alive();
  if (!subscribed_)
    pending_ = false;
}
bool X5PeripheralPolicy::enqueue(const X5ShutterRequest &request) {
  if (!subscribed() || pending_ || !request.token.hasRoom() ||
      request.token.connection != token_.connection || request.token.operation <= last_operation_ ||
      request.handle != handle_)
    return false;
  request_ = request;
  last_operation_ = request.token.operation;
  pending_ = true;
  return true;
}
bool X5PeripheralPolicy::pending(X5ShutterRequest &out) const {
  if (!pending_)
    return false;
  out = request_;
  return true;
}
bool X5PeripheralPolicy::admit(uint32_t now) {
  if (!pending_)
    return false;
  pending_ = false; // Consume even a failed final check; no old request can recur.
  return subscribed() && request_.token.connection == token_.connection &&
         request_.handle == handle_ && !due(now, request_.deadline_ms) &&
         request_.deadline_ms - now <= 5000;
}
void X5PeripheralPolicy::cancel(Token token) {
  if (pending_ && token.connection == token_.connection &&
      token.operation == request_.token.operation)
    pending_ = false;
}
void X5PeripheralPolicy::seal() {
  sealed_ = true;
  pending_ = subscribed_ = false;
}
void X5PeripheralPolicy::loss() { seal(); }
void X5PeripheralPolicy::terminal() {
  seal();
  connected_ = false;
  terminal_ = true;
  quiet_ = false;
}
void X5PeripheralPolicy::barrier() {
  if (terminal_)
    quiet_ = true;
}
void X5PeripheralPolicy::callbackEnter() { ++callbacks_; }
void X5PeripheralPolicy::callbackExit() {
  if (callbacks_)
    --callbacks_;
}
void X5PeripheralPolicy::sdkEnter() { ++sdk_calls_; }
void X5PeripheralPolicy::sdkExit() {
  if (sdk_calls_)
    --sdk_calls_;
}
bool X5PeripheralPolicy::released() const {
  return terminal_ && quiet_ && callbacks_ == 0 && sdk_calls_ == 0;
}
} // namespace ridesync

#include "wake_radio_policy.h"
namespace ridesync {
bool WakeRadioPolicy::matches(const WakeOperation &op) const {
  return active_ && sameWakeOperation(result_.operation, op);
}
bool WakeRadioPolicy::reserve(const WakeOperation &op, uint32_t deadline, uint32_t now) {
  const auto duration = deadline - now;
  if ((active_ && !reusable()) || op.peer >= kWakePeers || !op.id || op.id == UINT32_MAX ||
      !op.generation || op.generation == UINT32_MAX || op.id <= last_ids_[op.peer] ||
      op.generation < generations_[op.peer] || !duration || duration >= 0x80000000u)
    return false;
  result_ = {};
  result_.operation = op;
  deadline_ = deadline;
  last_ids_[op.peer] = op.id;
  generations_[op.peer] = op.generation;
  active_ = true;
  sealed_ = admitted_ = inflight_ = barrier_ = quarantined_ = false;
  callbacks_ = 0;
  connection_ = 0xffff;
  return true;
}
bool WakeRadioPolicy::admitStart(const WakeOperation &op, uint32_t now) {
  if (!matches(op) || sealed_ || admitted_ || result_.terminal ||
      static_cast<int32_t>(now - deadline_) >= 0)
    return false;
  admitted_ = inflight_ = true;
  return true;
}
void WakeRadioPolicy::seal(const WakeOperation &op) {
  if (matches(op))
    sealed_ = true;
}
void WakeRadioPolicy::returned(const WakeOperation &op, int error) {
  if (!matches(op))
    return;
  inflight_ = false;
  result_.sdk_error = error;
  result_.submitted = admitted_ && !error;
}
bool WakeRadioPolicy::incoming(const WakeOperation &op, uint16_t connection) {
  if (!matches(op))
    return false;
  if (connection == 0xffff || (connection_ != 0xffff && connection_ != connection)) {
    quarantined_ = sealed_ = true;
    return false;
  }
  connection_ = connection;
  barrier_ = false;
  return true;
}
void WakeRadioPolicy::disconnected(const WakeOperation &op, uint16_t connection) {
  if (matches(op) && connection_ == connection) {
    connection_ = 0xffff;
    barrier_ = false;
  }
}
void WakeRadioPolicy::terminal(const WakeOperation &op) {
  if (matches(op)) {
    result_.terminal = true;
    sealed_ = true;
    barrier_ = false;
  }
}
void WakeRadioPolicy::callbackEnter() {
  if (active_) {
    if (callbacks_ == UINT32_MAX)
      quarantined_ = true;
    else
      ++callbacks_;
    barrier_ = false;
  }
}
void WakeRadioPolicy::callbackExit() {
  if (active_) {
    if (callbacks_)
      --callbacks_;
    else
      quarantined_ = true;
  }
}
void WakeRadioPolicy::barrierReleased(const WakeOperation &op) {
  if (matches(op) && result_.terminal)
    barrier_ = true;
}
bool WakeRadioPolicy::reusable() const {
  return active_ && result_.terminal && barrier_ && !inflight_ && !callbacks_ &&
         connection_ == 0xffff && !quarantined_;
}
WakeRadioResult WakeRadioPolicy::status() const {
  auto out = result_;
  out.released = reusable();
  return out;
}
} // namespace ridesync

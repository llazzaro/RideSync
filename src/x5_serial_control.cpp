#include "x5_serial_control.h"
#include <cstring>
namespace ridesync {
void X5SerialControl::reject() {
  if (rejected_ != UINT32_MAX)
    ++rejected_;
}
void X5SerialControl::consume(char c) {
  if (pending_)
    discard_ = true;
  if (c == '\n') {
    X5SerialCommand parsed = X5SerialCommand::Invalid;
    if (!discard_) {
      line_[size_] = 0;
      const char *names[] = {"CONNECT", "REC", "STOP", "QUERY", "STATUS", "DISCONNECT"};
      for (unsigned i = 0; i < 6; ++i)
        if (std::strcmp(line_.data(), names[i]) == 0)
          parsed = static_cast<X5SerialCommand>(i + 1);
    }
    if (parsed == X5SerialCommand::Invalid)
      reject();
    if (!pending_) {
      command_ = parsed;
      pending_ = true;
    }
    size_ = 0;
    discard_ = cr_ = false;
    return;
  }
  if (discard_)
    return;
  if (cr_ || (c != '\r' && (c < 0x20 || c > 0x7e))) {
    discard_ = true;
    return;
  }
  if (c == '\r') {
    cr_ = true;
    return;
  }
  if (size_ >= line_.size() - 1) {
    discard_ = true;
    return;
  }
  line_[size_++] = c;
}
X5SerialCommand X5SerialControl::service() {
  if (!pending_)
    return X5SerialCommand::None;
  pending_ = false;
  const auto command = command_;
  command_ = X5SerialCommand::None;
  error_ = CameraError::None;
  switch (command) {
  case X5SerialCommand::Connect:
    error_ = port_.request(Operation::Connect);
    break;
  case X5SerialCommand::Rec:
    error_ = port_.request(Operation::Start);
    break;
  case X5SerialCommand::Stop:
    error_ = port_.request(Operation::Stop);
    break;
  case X5SerialCommand::Query:
    error_ = port_.request(Operation::Query);
    break;
  case X5SerialCommand::Status:
    port_.status();
    break;
  case X5SerialCommand::Disconnect:
    port_.disconnect();
    break;
  default:
    error_ = CameraError::Unsupported;
    break;
  }
  return command;
}
} // namespace ridesync

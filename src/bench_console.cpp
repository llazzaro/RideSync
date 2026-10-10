#include "bench_console.h"
#include <cstring>
namespace ridesync {
void BenchConsole::push(uint8_t c) {
  if (c == '\n') {
    if (!size_ && !invalid_) {
      cr_ = false;
      return;
    }
    if (pending_) {
      error_ = BenchConsoleError::LostInput;
    } else {
      pending_ = true;
      error_ = BenchConsoleError::Invalid;
      if (!invalid_) {
        const char *names[] = {"STATUS", "CONNECT",  "REC",   "STOP",      "QUERY",
                               "WAKE",   "SHUTDOWN", "CLEAR", "STATIONARY"};
        for (unsigned i = 0; i < 9; ++i)
          if (!std::strcmp(line_, names[i])) {
            command_ = static_cast<BenchCommand>(i);
            error_ = BenchConsoleError::None;
            break;
          }
      }
    }
    size_ = 0;
    line_[0] = 0;
    invalid_ = cr_ = false;
    return;
  }
  if (c == '\r') {
    invalid_ = invalid_ || cr_;
    cr_ = true;
    return;
  }
  if (cr_ || c < 32 || c > 126 || size_ >= sizeof(line_) - 1) {
    invalid_ = true;
    return;
  }
  if (!invalid_) {
    line_[size_++] = static_cast<char>(c);
    line_[size_] = 0;
  }
}
bool BenchConsole::take(BenchCommand &command, BenchConsoleError &error) {
  if (!pending_)
    return false;
  command = command_;
  error = error_;
  pending_ = false;
  error_ = BenchConsoleError::None;
  return true;
}
} // namespace ridesync

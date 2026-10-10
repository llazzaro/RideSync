#pragma once
#include <cstddef>
#include <cstdint>
namespace ridesync {
enum class BenchCommand : uint8_t {
  Status,
  Connect,
  Record,
  Stop,
  Query,
  Wake,
  Shutdown,
  Clear,
  Stationary
};
enum class BenchConsoleError : uint8_t { None, Invalid, LostInput };
// One serialized owner. A pass admits at most one complete command; a burst
// containing another complete line loses admission rather than replaying intent.
class BenchConsole {
public:
  void push(uint8_t);
  bool take(BenchCommand &, BenchConsoleError &);

private:
  char line_[16]{};
  size_t size_ = 0;
  BenchCommand command_ = BenchCommand::Status;
  BenchConsoleError error_ = BenchConsoleError::None;
  bool invalid_ = false, pending_ = false, cr_ = false;
};
#if defined(ARDUINO_ARCH_ESP32) && defined(RIDESYNC_BENCH_APPLICATION)
void benchConsoleService();
bool benchConsoleTakeClear();
#endif
} // namespace ridesync

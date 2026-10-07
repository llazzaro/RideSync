#pragma once
#include "gnss_parser.h"
#include "session_clock.h"
#include <cstddef>
namespace ridesync {
class ModemUart {
public:
  virtual ~ModemUart() = default;
  virtual size_t available() = 0;
  virtual int read() = 0;
  virtual size_t writable() = 0;
  // Must return promptly, accepting at most length bytes; never flush/wait.
  virtual size_t write(const char *data, size_t length) = 0;
};
enum class ModemState { Disabled, Startup, Power, WaitReady, Poll, Desynchronized, Failed };
enum class UartHealth {
  Disabled,
  Healthy,
  Timeout,
  Overflow,
  ProtocolError,
  Exhausted,
  InvalidClock
};
enum class FixValidity { Missing, Valid, NoFix, Invalid, Stale };
struct ModemConfig {
  bool documentary_profile_opt_in = false;
  bool terminal_retires_transaction = false;
  uint32_t command_ms = 10000, ready_ms = 15000, poll_ms = 1000, stale_ms = 3000;
  size_t rx_bytes_per_tick = 64, tx_bytes_per_tick = 16;
  uint8_t max_attempts = 3, max_restarts = 2;
};
struct ModemSnapshot {
  ModemState state = ModemState::Disabled;
  UartHealth health = UartHealth::Disabled;
  bool gnss_power_enabled = false, receiver_ready = false;
  FixValidity validity = FixValidity::Missing;
  bool age_available = false;
  uint64_t age_ms = 0, session_id = 0;
  GnssFix fix;
};
class ModemGnss {
public:
  ModemGnss(ModemUart &uart, const ModemConfig &config);
  void tick(const RecordTimestamp &now);
  ModemSnapshot snapshot(const RecordTimestamp &now) const;
  // Caller asserts a qualified physical reset/receive barrier: all prior command
  // executions and buffered/in-flight response bytes are retired. Silence is insufficient.
  bool restartAfterVerifiedBarrier(const RecordTimestamp &now);

private:
  ModemUart &uart_;
  ModemConfig config_;
  ModemSnapshot status_;
  uint64_t last_ms_ = 0, deadline_start_ = 0, poll_start_ = 0;
  bool barrier_required_ = false;
  bool initialized_ = false, outstanding_ = false, dropping_ = false, candidate_ = false;
  char line_[kGnssMaxLineBytes + 1]{};
  size_t line_size_ = 0, tx_offset_ = 0;
  uint8_t attempts_ = 0, restarts_ = 0;
  GnssParseResult pending_;
  const char *command_ = nullptr;
  void schedule(const char *command, uint64_t now);
  void line(uint64_t now);
  void terminal(bool ok, uint64_t now);
  void desynchronize(UartHealth reason);
  bool validTime(const RecordTimestamp &now) const;
};
} // namespace ridesync

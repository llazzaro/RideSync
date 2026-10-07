#include "modem_gnss.h"
#include <algorithm>
#include <cstring>
namespace ridesync {
namespace {
bool duration(uint32_t n) { return n > 0 && n < 0x80000000UL; }
bool starts(const char *s, const char *prefix) {
  return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}
} // namespace
ModemGnss::ModemGnss(ModemUart &uart, const ModemConfig &config) : uart_(uart), config_(config) {
  if (config.documentary_profile_opt_in && config.terminal_retires_transaction &&
      duration(config.command_ms) && duration(config.ready_ms) && duration(config.poll_ms) &&
      duration(config.stale_ms) && config.rx_bytes_per_tick > 0 &&
      config.rx_bytes_per_tick <= 256 && config.tx_bytes_per_tick > 0 &&
      config.tx_bytes_per_tick <= 32 && config.max_attempts > 0 && config.max_attempts <= 8 &&
      config.max_restarts <= 8) {
    status_.state = ModemState::Startup;
    status_.health = UartHealth::Healthy;
  }
}
bool ModemGnss::validTime(const RecordTimestamp &now) const {
  return now.monotonic_quality == MonotonicQuality::Valid && now.session_id != 0 &&
         (!initialized_ || (now.session_id == status_.session_id && now.monotonic_ms >= last_ms_));
}
void ModemGnss::schedule(const char *command, uint64_t now) {
  if (attempts_ >= config_.max_attempts) {
    status_.state = ModemState::Failed;
    status_.health = UartHealth::Exhausted;
    return;
  }
  ++attempts_;
  command_ = command;
  outstanding_ = true;
  tx_offset_ = 0;
  deadline_start_ = now;
  candidate_ = false;
}
void ModemGnss::desynchronize(UartHealth reason) {
  status_.state = ModemState::Desynchronized;
  status_.health = reason;
  status_.fix = GnssFix{};
  status_.validity = FixValidity::Invalid;
  candidate_ = false;
}
void ModemGnss::terminal(bool ok, uint64_t now) {
  if (barrier_required_ || !outstanding_ || tx_offset_ != std::strlen(command_))
    return;
  const bool drained = status_.state == ModemState::Desynchronized;
  outstanding_ = false;
  deadline_start_ = now;
  if (drained) {
    // FIFO, one outstanding exchange, exactly one unambiguous terminal per
    // command are required by the explicit profile contract. Never use old data.
    status_.state = std::strcmp(command_, "AT\r") == 0              ? ModemState::Startup
                    : std::strcmp(command_, "AT+CGNSSPWR=1\r") == 0 ? ModemState::Power
                                                                    : ModemState::Poll;
    poll_start_ = now - config_.poll_ms;
    return;
  }
  if (!ok) {
    status_.health = UartHealth::ProtocolError;
    status_.fix = GnssFix{};
    status_.validity = FixValidity::Invalid;
    poll_start_ = now - config_.poll_ms;
    return;
  }
  if (status_.state == ModemState::Startup) {
    attempts_ = 0;
    status_.state = ModemState::Power;
  } else if (status_.state == ModemState::Power) {
    status_.gnss_power_enabled = true;
    attempts_ = 0;
    status_.state = ModemState::WaitReady;
    deadline_start_ = now;
  } else if (status_.state == ModemState::Poll) {
    if (!candidate_ || pending_.status == GnssStatus::ParseError) {
      status_.fix = GnssFix{};
      status_.validity = FixValidity::Invalid;
      status_.health = UartHealth::ProtocolError;
      poll_start_ = now - config_.poll_ms;
      return;
    }
    status_.fix = pending_.fix;
    status_.validity = pending_.status == GnssStatus::Fix ? FixValidity::Valid : FixValidity::NoFix;
    attempts_ = 0;
    poll_start_ = now;
  }
  status_.health = UartHealth::Healthy;
}
void ModemGnss::line(uint64_t now) {
  line_[line_size_] = 0;
  if (std::strcmp(line_, "+CGNSSPWR: READY!") == 0) {
    if (status_.gnss_power_enabled || (outstanding_ && status_.state == ModemState::Power))
      status_.receiver_ready = true;
  } else if (std::strcmp(line_, "RDY") == 0) {
    barrier_required_ = true;
    status_.gnss_power_enabled = false;
    status_.receiver_ready = false;
    if (outstanding_)
      desynchronize(UartHealth::ProtocolError);
    else {
      status_.state = ModemState::Failed;
      status_.health = UartHealth::ProtocolError;
      status_.fix = GnssFix{};
      status_.validity = FixValidity::Invalid;
    }
  } else if (std::strcmp(line_, "OK") == 0)
    terminal(true, now);
  else if (std::strcmp(line_, "ERROR") == 0 || starts(line_, "+CME ERROR:") ||
           starts(line_, "+CMS ERROR:"))
    terminal(false, now);
  else if (outstanding_ && status_.state == ModemState::Poll &&
           tx_offset_ == std::strlen(command_) &&
           (starts(line_, "+CGPSINFO:") || starts(line_, "+CGNSSINFO:"))) {
    if (candidate_) {
      pending_ = GnssParseResult{};
    } else {
      pending_ = parseGnssLine(line_, line_size_, now);
      candidate_ = true;
    }
    // A CGNSSINFO line cannot substitute for the command-specific CGPSINFO reply.
    if (starts(line_, "+CGNSSINFO:"))
      pending_ = GnssParseResult{};
  }
}
void ModemGnss::tick(const RecordTimestamp &now) {
  if (status_.state == ModemState::Disabled)
    return;
  if (!validTime(now)) {
    barrier_required_ = true;
    desynchronize(UartHealth::InvalidClock);
    return;
  }
  if (!initialized_)
    deadline_start_ = now.monotonic_ms;
  initialized_ = true;
  status_.session_id = now.session_id;
  last_ms_ = now.monotonic_ms;
  if (status_.state == ModemState::Failed)
    return;
  if (!outstanding_ &&
      (((status_.state == ModemState::Startup || status_.state == ModemState::Power) &&
        now.monotonic_ms - deadline_start_ >= config_.command_ms) ||
       (status_.state == ModemState::Poll &&
        now.monotonic_ms - poll_start_ >= uint64_t(config_.poll_ms) + config_.command_ms))) {
    status_.state = ModemState::Failed;
    status_.health = UartHealth::Timeout;
    status_.fix = GnssFix{};
    status_.validity = FixValidity::Invalid;
    return;
  }
  if (status_.state == ModemState::WaitReady && !status_.receiver_ready &&
      now.monotonic_ms - deadline_start_ >= config_.ready_ms) {
    status_.state = ModemState::Failed;
    status_.health = UartHealth::Timeout;
    return;
  }
  // Expiry precedes input: a terminal arriving in a late tick cannot rescue data.
  if (outstanding_ && status_.state != ModemState::Desynchronized &&
      now.monotonic_ms - deadline_start_ >= config_.command_ms)
    desynchronize(UartHealth::Timeout);
  for (size_t n = 0; n < config_.rx_bytes_per_tick && uart_.available(); ++n) {
    const int c = uart_.read();
    if (c < 0)
      break;
    if (c == '\r' || c == '\n') {
      if (!dropping_ && line_size_)
        line(now.monotonic_ms);
      dropping_ = false;
      line_size_ = 0;
    } else if (!dropping_) {
      if (c < 32 || c > 126 || line_size_ == kGnssMaxLineBytes) {
        dropping_ = true;
        line_size_ = 0;
        desynchronize(UartHealth::Overflow);
      } else
        line_[line_size_++] = static_cast<char>(c);
    }
  }
  if (status_.state == ModemState::Desynchronized || status_.state == ModemState::Failed)
    return;
  if (status_.state == ModemState::WaitReady) {
    if (status_.receiver_ready) {
      status_.state = ModemState::Poll;
      poll_start_ = now.monotonic_ms - config_.poll_ms;
    } else if (now.monotonic_ms - deadline_start_ >= config_.ready_ms) {
      status_.state = ModemState::Failed;
      status_.health = UartHealth::Timeout;
      return;
    }
  }
  // No queue: do not send while old bytes/partial frames remain in receive path.
  if (!outstanding_ && uart_.available() == 0 && line_size_ == 0 && !dropping_) {
    if (status_.state == ModemState::Startup)
      schedule("AT\r", now.monotonic_ms);
    else if (status_.state == ModemState::Power)
      schedule("AT+CGNSSPWR=1\r", now.monotonic_ms);
    else if (status_.state == ModemState::Poll && now.monotonic_ms - poll_start_ >= config_.poll_ms)
      schedule("AT+CGPSINFO\r", now.monotonic_ms);
  }
  if (outstanding_ && tx_offset_ < std::strlen(command_)) {
    const size_t n = std::min(std::min(uart_.writable(), config_.tx_bytes_per_tick),
                              std::strlen(command_) - tx_offset_);
    if (n) {
      const size_t wrote = uart_.write(command_ + tx_offset_, n);
      if (wrote > n)
        desynchronize(UartHealth::ProtocolError);
      else
        tx_offset_ += wrote;
    }
  }
}
ModemSnapshot ModemGnss::snapshot(const RecordTimestamp &now) const {
  ModemSnapshot result = status_;
  if (!validTime(now)) {
    result.health = UartHealth::InvalidClock;
    result.validity = FixValidity::Invalid;
    result.fix = GnssFix{};
    return result;
  }
  if (result.validity == FixValidity::Valid || result.validity == FixValidity::NoFix) {
    result.age_available = true;
    result.age_ms = now.monotonic_ms - result.fix.receipt_monotonic_ms;
    if (result.age_ms >= config_.stale_ms) {
      result.validity = FixValidity::Stale;
      result.fix.valid = false;
    }
  }
  return result;
}
bool ModemGnss::restartAfterVerifiedBarrier(const RecordTimestamp &now) {
  if (status_.state == ModemState::Disabled || restarts_ >= config_.max_restarts ||
      now.monotonic_quality != MonotonicQuality::Valid || now.session_id == 0 || uart_.available())
    return false;
  ++restarts_;
  status_ = ModemSnapshot{};
  status_.state = ModemState::Startup;
  status_.health = UartHealth::Healthy;
  barrier_required_ = false;
  initialized_ = false;
  outstanding_ = false;
  dropping_ = false;
  candidate_ = false;
  line_size_ = 0;
  attempts_ = 0;
  command_ = nullptr;
  tick(now);
  return true;
}
} // namespace ridesync

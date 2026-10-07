#pragma once
#include "health_supervisor.h"
#include "telemetry_admission.h"
namespace ridesync {
class ImuEmitter {
public:
  virtual ~ImuEmitter() = default;
  virtual void emit(const ImuEvidence &) = 0;
};
struct ImuCodecHealth {
  uint32_t samples = 0, unsupported = 0, partial = 0, skipped_lower_bound = 0, discontinuities = 0;
};
class ImuFifoCodec {
public:
  bool decode(const uint8_t *, uint16_t, ImuEvidence, ImuEmitter &);
  void barrier();
  const ImuCodecHealth &health() const { return health_; }

private:
  ImuCodecHealth health_;
  uint32_t receipt_ = 0;
  uint32_t frame_ = 0, tick_ = 0;
  bool time_ = false;
};
class ImuPort {
public:
  virtual ~ImuPort() = default;
  virtual bool begin(ImuConfig &) = 0;
  virtual bool read(uint8_t *, uint16_t, uint16_t &, uint8_t &) = 0;
  virtual bool flush() = 0;
  virtual void describeReadFailure(ImuEvidence &e) const { e.event_code = 2; }
  virtual bool drainTimes(uint32_t &, uint32_t &) const { return false; }
};
struct ImuManagerHealth {
  uint32_t init_errors = 0, read_errors = 0, recovery_flushes = 0, recovery_errors = 0;
};
class ImuManager : private ImuEmitter {
public:
  static constexpr uint16_t kBytes = 112;
  ImuManager(ImuPort &port, ImuInbox &inbox, HealthProgress &progress, uint64_t session,
             const ImuConfig &metadata);
  void step(uint32_t millis32);
  // Worker-owner only; capture a copy after quiescence for runtime loss auditing.
  ImuManagerHealth health() const { return health_; }
  ImuCodecHealth codecHealth() const { return codec_.health(); }
  void stop() { stop_.store(true, std::memory_order_release); }

private:
  void emit(const ImuEvidence &) override;
  void publish();
  ImuPort &port_;
  ImuInbox &inbox_;
  HealthProgress &progress_;
  ImuManagerHealth health_;
  ImuEvidence base_;
  ImuBatch batch_;
  ImuFifoCodec codec_;
  uint8_t bytes_[kBytes]{};
  enum class State { Initial, Ready, Barrier, Failed, Finished };
  State state_ = State::Initial;
  std::atomic<bool> stop_{false};
  bool receipt_ = false, profile_unknown_ = false;
  uint32_t previous_ = 0;
  uint8_t failures_ = 0;
};
} // namespace ridesync

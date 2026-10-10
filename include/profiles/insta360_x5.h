#pragma once
#include "x5_peripheral.h"
namespace ridesync {
class X5Adapter final : public CameraTransport {
public:
  X5Adapter(X5PeripheralPort &port, Clock &clock) : port_(port), clock_(clock) {}
  bool configure(const X5Qualification &);
  bool attach(CameraManager &);
  bool begin(size_t, const CameraConfig &, Operation, Token) override;
  void cancel(size_t, Token) override;
  void close(size_t, Token) override;
  void service();
  bool ready(Operation) const;
  X5Failure failure() const { return failure_; }
  RecordingState observed() const;
  static RetryPolicy managerPolicy();

private:
  X5PeripheralPort &port_;
  Clock &clock_;
  CameraManager *manager_ = nullptr;
  X5Qualification qualification_;
  X5Failure failure_ = X5Failure::Disabled;
  Token token_;
  Operation operation_ = Operation::Connect;
  RecordingState recording_ = RecordingState::Unknown;
  uint16_t handle_ = kBleNoHandle;
  uint32_t deadline_ = 0, last_sequence_ = 0, observation_sequence_ = 0, observed_ms_ = 0,
           cutoff_ = 0;
  bool configured_ = false, connected_ = false, subscribed_ = false, active_ = false,
       video_ = false, has_observation_ = false, send_pending_ = false, send_consumed_ = false,
       returned_ = false, complete_pending_ = false;
  X5Input input_;
  void invalidate(bool mode = true, bool publish = true);
  void publishObservation();
  void fail(X5Failure);
  void complete();
  void input(const X5Input &);
  void display(const X5Input &);
  bool fresh() const;
  bool desiredObserved() const;
};
} // namespace ridesync

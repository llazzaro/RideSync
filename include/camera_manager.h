#pragma once
#include "config.h"
namespace ridesync {
enum class Lifecycle { Disabled, Idle, Connecting, Ready, Operating, Backoff, Failed };
enum class Operation { Connect, Start, Stop, Query, Wake };
enum class CameraError {
  None,
  InvalidPeer,
  Disabled,
  NotConnected,
  Unsupported,
  QueueFull,
  Busy,
  Timeout,
  Transport,
  Cancelled,
  InvalidPolicy
};
// RecordingObserved/Disconnected are connection-scoped. CommandRecordingObserved,
// Completed and Failed are operation-scoped responses.
enum class EventKind {
  Completed,
  RecordingObserved,
  CommandRecordingObserved,
  Disconnected,
  Failed
};
struct Token {
  uint32_t connection = 0;
  uint32_t operation = 0;
};
struct Event {
  Event(size_t p, Token t, EventKind k) : peer(p), token(t), kind(k) {}
  // Persistent connection callbacks capture this generation at connection setup;
  // they do not need a current operation token.
  Event(size_t p, uint32_t connection, EventKind k) : peer(p), kind(k) {
    token.connection = connection;
  }
  size_t peer;
  Token token;
  EventKind kind;
  RecordingState recording = RecordingState::Unknown;
  Capabilities capabilities;
};
struct RetryPolicy {
  uint32_t timeout_ms = 1000;
  uint32_t backoff_ms = 200;
  uint8_t max_attempts = 3;
};
struct CameraState {
  Lifecycle lifecycle = Lifecycle::Idle;
  RecordingState desired = RecordingState::Unknown;
  RecordingState observed = RecordingState::Unknown;
  Capabilities capabilities;
  CameraError error = CameraError::None;
  uint32_t last_seen_ms = 0;
  bool has_last_seen = false;
  uint32_t last_observed_ms = 0;
  bool has_observation = false;
  uint32_t deadline_ms = 0;
  uint8_t attempts = 0;
  Token token;
};
class Clock {
public:
  virtual ~Clock() = default;
  virtual uint32_t now() const = 0;
};
// begin() accepts a request for delivery; acceptance never proves recording.
// Callbacks must be marshalled to the same execution context as tick/request/event,
// after begin/cancel/close returns (no inline callbacks). cancel retires an operation;
// close idempotently retires all connection resources, including partial handshakes.
class CameraTransport {
public:
  virtual ~CameraTransport() = default;
  virtual bool begin(size_t peer, const CameraConfig &camera, Operation operation, Token token) = 0;
  virtual void cancel(size_t peer, Token token) = 0;
  virtual void close(size_t peer, Token token) = 0;
};
class CameraManager {
public:
  static constexpr size_t kQueueDepth = 4;
  CameraManager(Clock &clock, CameraTransport &transport, RetryPolicy policy = {});
  ConfigResult configure(const SourceConfig &config);
  CameraError request(size_t peer, Operation operation);
  CameraError cancel(size_t peer);
  void reset();
  void tick();
  bool event(const Event &event);
  const CameraState *state(size_t peer) const;
  size_t size() const { return config_.count; }

private:
  struct Peer {
    CameraState state;
    std::array<Operation, kQueueDepth> queue;
    size_t queued = 0;
    bool active = false;
    Operation current = Operation::Connect;
  };
  Clock &clock_;
  CameraTransport &transport_;
  RetryPolicy policy_;
  SourceConfig config_;
  std::array<Peer, kMaxCameras> peers_;
  bool validPolicy() const;
  void start(size_t peer, Operation op);
  void attempt(size_t peer);
  void fail(size_t peer, CameraError error);
  void next(size_t peer);
};
} // namespace ridesync

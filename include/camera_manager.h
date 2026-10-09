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
  // Reserve room for retries and retirement; never reuse a wrapped generation.
  static constexpr uint32_t kHeadroom = 64;
  bool hasRoom() const {
    return connection < UINT32_MAX - kHeadroom && operation < UINT32_MAX - kHeadroom;
  }
  bool valid() const { return connection != UINT32_MAX && operation != UINT32_MAX; }
  static void advance(uint32_t &generation) {
    if (generation != UINT32_MAX)
      ++generation;
  }
};
enum class CameraAckDomain : uint8_t { None, Classic, Protobuf };
enum class CameraAckAction : uint8_t {
  None,
  Pair,
  Claim,
  Hardware,
  Api,
  RegisterBusy,
  RegisterEncoding,
  RegisterReady,
  GetBusy,
  GetEncoding,
  GetReady,
  Video,
  ShutterOn,
  ShutterOff,
  ConfirmEncoding,
  QueryEncoding
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
// Called only in the serialized camera owner. Implementations must be bounded and
// cannot affect admission, camera control, or call back into the manager.
class CameraAudit {
public:
  virtual ~CameraAudit() = default;
  virtual void request(size_t peer, Operation, CameraError, bool queued, uint32_t intent_id) = 0;
  virtual void attempt(size_t peer, Operation, Token, uint32_t intent_id, bool delivered) = 0;
  virtual void accepted(const Event &, Operation, uint32_t intent_id) = 0;
  virtual void cancelled(size_t peer, Operation, Token, uint32_t intent_id) = 0;
  virtual void failure(size_t peer, Operation, Token, uint32_t intent_id, CameraError) = 0;
  virtual void wireAck(size_t peer, Operation, Token, uint32_t intent_id, CameraAckDomain,
                       CameraAckAction) = 0;
};
class CameraManager {
public:
  static constexpr size_t kQueueDepth = 4;
  CameraManager(Clock &clock, CameraTransport &transport, RetryPolicy policy = {});
  ConfigResult configure(const SourceConfig &config);
  CameraError request(size_t peer, Operation operation);
  CameraError cancel(size_t peer);
  // Permanent for this manager lifetime; configuration/reset cannot reopen it.
  CameraError seal(size_t peer);
  bool sealed(size_t peer) const { return peer < kMaxCameras && maintenance_[peer]; }
  void reset();
  void tick();
  uint32_t ticks() const { return ticks_; }
  uint32_t resets() const { return resets_; }
  bool event(const Event &event);
  bool attachAudit(CameraAudit &audit) {
    if (audit_ && audit_ != &audit)
      return false;
    audit_ = &audit;
    return true;
  }
  void detachAudit(const CameraAudit *expected = nullptr) {
    if (!expected || audit_ == expected)
      audit_ = nullptr;
  }
  bool wireAck(size_t peer, Token token, CameraAckDomain domain, CameraAckAction action);
  const CameraState *state(size_t peer) const;
  const CameraConfig *configuredCamera(size_t peer) const {
    return peer < size() ? &config_.cameras[peer] : nullptr;
  }
  size_t size() const { return config_.count; }

private:
  struct Peer {
    CameraState state;
    struct Queued {
      Operation operation;
      uint32_t intent_id;
    };
    std::array<Queued, kQueueDepth> queue;
    size_t queued = 0;
    bool active = false;
    Operation current = Operation::Connect;
    uint32_t next_intent_id = 0, active_intent_id = 0;
    uint32_t ack_mask = 0;
    uint32_t completed_observation_deadline_ms = 0;
    bool completed_observation_open = false;
  };
  Clock &clock_;
  CameraTransport &transport_;
  RetryPolicy policy_;
  SourceConfig config_;
  CameraAudit *audit_ = nullptr;
  uint32_t ticks_ = 0, resets_ = 0;
  std::array<Peer, kMaxCameras> peers_;
  std::array<bool, kMaxCameras> maintenance_{};
  bool validPolicy() const;
  bool validateEvent(const Event &, Peer *&, bool &, uint32_t &, Operation &);
  bool handleCompleted(size_t, const Event &, Peer &);
  bool handleObservation(const Event &, Peer &);
  bool handleDisconnected(size_t, Peer &);
  bool handleFailed(size_t, Peer &);
  void start(size_t peer, Operation op, uint32_t intent_id);
  void attempt(size_t peer);
  void fail(size_t peer, CameraError error);
  void next(size_t peer);
};
} // namespace ridesync

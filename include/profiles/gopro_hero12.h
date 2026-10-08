#pragma once
#include "ble_remote.h"
#include "camera_manager.h"
#include "protocol/gopro_setup_codec.h"
#include <array>

namespace ridesync {
class RecordingManager;
// Commissioning supplies independent evidence. A configured BLE address and the
// published minimum firmware are not observations of this camera.
struct Hero12Qualification {
  BondIdentity identity;
  std::array<uint8_t, 32> firmware{};
  uint8_t firmware_size = 0;
  uint64_t api_major = 0, api_minor = 0;
  bool source_qualified = false, classic_profile_confirmed = false;
};
enum class Hero12Fault : uint8_t {
  None,
  Disabled,
  Qualification,
  Capacity,
  Transport,
  SetupRejected,
  IdentityMismatch,
  CameraRejected,
  Protocol,
  Timeout,
  DeliveryUncertain,
  Store,
  MissingService,
  MissingProperty,
  MissingCccd,
  Att
};
// These are caller-supplied conditions, not classifications inferred from a scan.
enum class Hero12PowerCondition : uint8_t { Unknown, BleConnectable, PowerRemoved };
enum class Hero12RecoveryPhase : uint8_t {
  Idle,
  Pending,
  Scanning,
  Connecting,
  Observing,
  Starting,
  Ready,
  Recording,
  Unavailable,
  Timeout,
  Cancelled,
  Unsupported,
  Failed
};
struct Hero12RecoveryState {
  Hero12RecoveryPhase phase = Hero12RecoveryPhase::Idle;
  uint8_t scans = 0;
};
// One serialized owner calls start, service, manager request/event/tick and
// configurePeer. Host callbacks enter only BleCentral's fixed copied queue.
class Hero12Adapter final : public CameraTransport, public BleResultSink {
public:
  Hero12Adapter(BleHost &, Clock &);
  ~Hero12Adapter() = default;
  Hero12Adapter(const Hero12Adapter &) = delete;
  Hero12Adapter &operator=(const Hero12Adapter &) = delete;
  void attach(CameraManager &);
  void attachGroup(RecordingManager &group) { group_ = &group; }
  bool start(bool enabled, bool source_qualified);
  bool configurePeer(uint8_t peer, const Hero12Qualification &);
  void service(); // Central faults, profile, manager events, then manager.tick().
  void stop();
  bool canDestroy() const;
  Hero12Fault fault(uint8_t peer) const;
  CameraError requestRecovery(uint8_t peer, bool ensure_recording,
                              Hero12PowerCondition condition = Hero12PowerCondition::Unknown);
  CameraError cancelRecovery(uint8_t peer);
  Hero12RecoveryState recoveryState(uint8_t peer) const;
  const BleCentral &central() const { return central_; }
  static BleProfileSpec profileSpec();
  static RetryPolicy managerPolicy();
  bool begin(size_t peer, const CameraConfig &, Operation, Token) override;
  void cancel(size_t peer, Token) override;
  void close(size_t peer, Token) override;
  void result(const BleResult &) override;

private:
  enum class Step : uint8_t {
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
    Shutter,
    ConfirmEncoding,
    QueryEncoding,
    KeepAlive
  };
  struct Peer {
    Hero12Qualification qualification;
    Hero12Fault fault = Hero12Fault::None;
    Token token;
    uint32_t connection = 0, deadline = 0, keepalive_due = 0, hardware_retry_due = 0;
    Step step = Step::None, resume = Step::None;
    Operation operation = Operation::Connect;
    gopro::Channel route = gopro::Channel::Command;
    uint8_t expected_id = 0, expected_status = 0;
    bool qualified = false, connecting = false, ready = false, sealed = false;
    bool att_done = false, camera_done = false, waiting_write = false, fragment = false;
    uint8_t write_endpoint = 0;
    bool busy_known = false, busy = false, ready_known = false, camera_ready = false;
    bool encoding_known = false, encoding = false, target_encoding = false;
    bool hardware_not_ready = false;
    uint8_t hardware_attempts = 0;
    uint32_t fragment_started = 0;
    gopro::Channel fragment_route = gopro::Channel::Command;
  };
  struct PendingEvent {
    uint8_t peer = 0;
    Token token;
    EventKind kind = EventKind::Failed;
    RecordingState recording = RecordingState::Unknown;
    Capabilities capabilities;
  };
  struct Recovery {
    Hero12RecoveryState state;
    uint32_t deadline = 0, operation = 0, connection = 0, source_connection = 0;
    bool ensure_recording = false, observed_advertisement = false;
  };
  Clock &clock_;
  CameraManager *manager_ = nullptr;
  RecordingManager *group_ = nullptr;
  BleCentral central_;
  gopro::Reassembler reassembler_;
  std::array<Peer, kBlePeers> peers_{};
  std::array<bool, kBlePeers> disconnect_pending_{};
  std::array<PendingEvent, kBlePeers * 2> events_{};
  std::array<Recovery, kBlePeers> recovery_{};
  uint8_t scan_owner_ = kBlePeers, scan_cursor_ = 0;
  uint32_t scan_generation_ = 0;
  uint8_t event_count_ = 0, cursor_ = 0;
  bool enabled_ = false, started_ = false;
  void retire(uint8_t, Hero12Fault);
  void enqueue(Event);
  void drain();
  void advance(uint8_t);
  void advanceRecovery();
  static bool hasFea6(const BleEvent &);
  bool send(uint8_t, Step);
  void observe(uint8_t, bool, bool);
  void cameraMessage(uint8_t, gopro::Channel, const gopro::Message &);
  static bool reached(uint32_t now, uint32_t deadline);
};
} // namespace ridesync

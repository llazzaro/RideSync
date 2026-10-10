#pragma once
#include "ble_remote.h"
#include "camera_manager.h"
#include "protocol/insta360_go3s_codec.h"
namespace ridesync {
class RecordingManager;
struct Go3sQualification {
  BondIdentity identity;
  std::array<uint8_t, 16> firmware{};
  uint8_t firmware_size = 0;
  std::array<char, 32> authorization_id{};
  uint8_t authorization_size = 0;
  bool source_qualified = false;
};
enum class Go3sFault : uint8_t {
  None,
  Disabled,
  Qualification,
  Transport,
  Mtu,
  Protocol,
  Rejected,
  Timeout,
  SequenceExhausted,
  Cancelled
};
// Serialized owner, fixed queues and shared BleCentral. Never interprets an ACK
// as observed recording. Lifetime extends through host final-access barriers.
class Go3sAdapter final : public CameraTransport, public BleResultSink {
public:
  static constexpr uint32_t kResponseMs = 5000, kFragmentMs = 1000, kKeepAliveMs = 3000;
  Go3sAdapter(BleHost &, Clock &);
  void attach(CameraManager &manager) { manager_ = &manager; }
  void attachGroup(RecordingManager &group) { group_ = &group; }
  bool configurePeer(uint8_t, const Go3sQualification &);
  bool start(bool enabled, bool source_qualified);
  void service();
  void stop();
  bool canDestroy() const { return central_.canDestroy(); }
  bool commandReady(uint8_t) const;
  Go3sFault fault(uint8_t) const;
  const HealthProgress &progress() const { return progress_; }
  static BleProfileSpec profileSpec();
  static RetryPolicy managerPolicy();
  bool begin(size_t, const CameraConfig &, Operation, Token) override;
  void cancel(size_t, Token) override;
  void close(size_t, Token) override;
  void result(const BleResult &) override;

private:
  enum class Step : uint8_t {
    None,
    AwaitSync,
    Nudge,
    Sync,
    Authorize,
    Video,
    Start,
    Stop,
    Heartbeat
  };
  struct Peer {
    Go3sQualification qualification;
    go3s::Receiver receiver;
    Token token;
    Operation operation = Operation::Connect;
    Step step = Step::None, resume = Step::None;
    Go3sFault fault = Go3sFault::Disabled;
    uint32_t deadline = 0, response_deadline = 0, fragment_started = 0, keepalive_due = 0;
    uint16_t handle = kBleNoHandle;
    uint8_t next_sequence = 0, expected_sequence = 0;
    bool qualified = false, connecting = false, ready = false, sealed = false;
    bool att_done = false, camera_done = false, sync_seen = false, nudged = false;
  };
  struct Pending {
    uint8_t peer = 0;
    Token token;
    Capabilities capabilities;
  };
  BleHost &host_;
  Clock &clock_;
  CameraManager *manager_ = nullptr;
  RecordingManager *group_ = nullptr;
  HealthProgress progress_;
  BleCentral central_;
  std::array<Peer, kBlePeers> peers_{};
  std::array<bool, kBlePeers> disconnected_{};
  std::array<Pending, kBlePeers * 2> events_{};
  size_t event_count_ = 0;
  bool started_ = false, enabled_ = false, servicing_ = false;
  static bool valid(const Go3sQualification &);
  static bool reached(uint32_t, uint32_t);
  void retire(uint8_t, Go3sFault);
  bool send(uint8_t, Step);
  void advance(uint8_t);
  void complete(uint8_t);
  void drain();
};
} // namespace ridesync

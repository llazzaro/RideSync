#pragma once
#include "ble_remote.h"
#include "camera_manager.h"
#include "gps_forwarding.h"
namespace ridesync {
class RecordingManager;
struct OneRsQualification {
  BondIdentity identity;
  std::array<char, 32> firmware{};
  uint8_t firmware_size = 0;
  bool source_qualified = false, core_one_rs = false, ordinary_360_lens = false,
       video_mode_declared = false;
};
enum class OneRsFault : uint8_t {
  None,
  Disabled,
  Qualification,
  Transport,
  Mtu,
  Timeout,
  SequenceExhausted,
  Cancelled
};
// Experimental source-profile route; ACK and state semantics remain unknown.
// Serialized owner; retain through shared-host terminal and quiescence barriers.
class OneRsAdapter final : public CameraTransport, public BleResultSink {
public:
  static constexpr uint32_t kConnectMs = 60000, kCommandMs = 5000;
  OneRsAdapter(BleHost &, Clock &);
  void attach(CameraManager &m) { manager_ = &m; }
  void attachGroup(RecordingManager &g) { group_ = &g; }
  bool configurePeer(uint8_t, const OneRsQualification &);
  bool start(bool enabled, bool source_qualified);
  void service(bool advance = true);
  void stop();
  bool configureGps(uint8_t i, const CameraConfig &c, const GpsForwardingConfig &q) {
    return forwarding_.configure(i, c, q, sequences_[i]);
  }
  Be80GpsForwarder &forwarding() { return forwarding_; }
  const Be80GpsForwarder &forwarding() const { return forwarding_; }
  void forwardingTime(const RecordTimestamp &t) { forwarding_time_ = t; }
  void serviceGps(uint8_t mask) { forwarding_.service(forwarding_time_, mask); }
  bool canDestroy() const { return central_.canDestroy(); }
  bool commandReady(uint8_t) const;
  bool linkRetiring(uint8_t i) const { return central_.phase(i) == BlePhase::Retiring; }
  bool linkReleased(uint8_t i) const {
    return central_.phase(i) == BlePhase::Empty || central_.phase(i) == BlePhase::Closed;
  }
  OneRsFault fault(uint8_t) const;
  const HealthProgress &progress() const { return progress_; }
  static BleProfileSpec profileSpec();
  static RetryPolicy managerPolicy();
  bool begin(size_t, const CameraConfig &, Operation, Token) override;
  void cancel(size_t, Token) override;
  void close(size_t, Token) override;
  void result(const BleResult &) override;

private:
  struct Peer {
    OneRsQualification qualification;
    Token token;
    OneRsFault fault = OneRsFault::Disabled;
    uint32_t deadline = 0;
    uint16_t handle = kBleNoHandle;
    uint8_t sequence = 0;
    bool qualified = false, connecting = false, ready = false, pending = false, sealed = false;
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
  Be80GpsForwarder forwarding_;
  std::array<Be80CommandSequence, kBlePeers> sequences_;
  RecordTimestamp forwarding_time_;
  std::array<Peer, kBlePeers> peers_{};
  std::array<bool, kBlePeers> disconnected_{};
  std::array<Pending, kBlePeers * 2> events_{};
  size_t event_count_ = 0;
  bool started_ = false, enabled_ = false, servicing_ = false;
  static bool valid(const OneRsQualification &);
  static bool matches(const CameraConfig &, const BondIdentity &);
  static bool reached(uint32_t, uint32_t);
  void retire(uint8_t, OneRsFault);
  void complete(uint8_t);
  void drain();
};
} // namespace ridesync

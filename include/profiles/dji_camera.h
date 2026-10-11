#pragma once
#include "ble_remote.h"
#include "camera_manager.h"
#include "protocol/dji_codec.h"
namespace ridesync {
class RecordingManager;
struct DjiQualification {
  BondIdentity identity;
  CameraModel model = CameraModel::Unknown;
  std::array<uint8_t, 32> firmware{};
  uint8_t firmware_size = 0;
  bool source_qualified = false;
  std::array<uint8_t, 6> controller_mac{};
  bool controller_mac_verified = false;
  uint16_t connection_challenge = 0;
  bool already_paired = false;
  bool unencrypted_protocol_reviewed = false;
  uint32_t controller_id = 0;
};
enum class DjiFault : uint8_t {
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
class DjiAdapter final : public CameraTransport, public BleResultSink {
public:
  static constexpr uint32_t kResponseMs = 5000, kPairMs = 30000, kFragmentMs = 1000,
                            kStatusMs = 1500, kPollMs = 500;
  DjiAdapter(BleHost &, Clock &);
  void attach(CameraManager &manager) { manager_ = &manager; }
  void attachGroup(RecordingManager &group) { group_ = &group; }
  void detachGroup(const RecordingManager *expected) {
    if (group_ == expected)
      group_ = nullptr;
  }
  bool configurePeer(uint8_t, const DjiQualification &);
  bool start(bool enabled, bool source_qualified);
  void service(bool advance = true);
  void stop();
  bool canDestroy() const { return central_.canDestroy(); }
  bool commandReady(uint8_t) const;
  bool linkRetiring(uint8_t i) const { return central_.phase(i) == BlePhase::Retiring; }
  bool linkReleased(uint8_t i) const {
    return central_.phase(i) == BlePhase::Empty || central_.phase(i) == BlePhase::Closed;
  }
  DjiFault fault(uint8_t) const;
  const HealthProgress &progress() const { return progress_; }
  static BleProfileSpec profileSpec();
  static RetryPolicy managerPolicy();
  bool begin(size_t, const CameraConfig &, Operation, Token) override;
  void cancel(size_t, Token) override;
  void close(size_t, Token) override;
  void result(const BleResult &) override;

private:
  void advanceAcknowledged(uint8_t i);
  bool beginCommand(uint8_t i, Operation op, Token t);
  bool transactionExpired(uint8_t i) const;
  void servicePeer(uint8_t i);
  bool beginConnection(uint8_t i, Token t);
  bool recordResponse(uint8_t i, const dji::Decoded &d);
  bool connectionResponse(uint8_t i, const dji::Decoded &d);
  bool connectionRequest(uint8_t i, const dji::Decoded &d);
  void writeComplete(uint8_t i, const BleResult &r);
  void notification(uint8_t i, const BleResult &r);
  enum class Step : uint8_t { None, Pair, Authorize, Subscribe, Query, Poll, Start, Stop };
  struct Peer {
    DjiQualification qualification;
    dji::Receiver receiver;
    Token token;
    Operation operation = Operation::Connect;
    Step step = Step::None, deferred = Step::None;
    DjiFault fault = DjiFault::Disabled;
    uint32_t deadline = 0, response_deadline = 0, fragment_started = 0, poll_due = 0,
             status_seen = 0;
    uint16_t handle = kBleNoHandle, next_sequence = 0, expected_sequence = 0, grant_sequence = 0,
             status_sequence = 0;
    RecordingState observed = RecordingState::Unknown;
    uint8_t mode = 0xff, camera_status = 0xff, power = 0xff;
    bool qualified = false, connecting = false, ready = false, sealed = false;
    bool att_done = false, camera_done = false, grant = false, has_status = false,
         have_status_sequence = false;
  };
  struct Pending {
    uint8_t peer = 0;
    Token token;
    EventKind kind = EventKind::Completed;
    RecordingState recording = RecordingState::Unknown;
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
  std::array<Pending, kBlePeers * 8> events_{};
  size_t event_count_ = 0;
  bool started_ = false, enabled_ = false, servicing_ = false;
  static bool valid(const DjiQualification &);
  static uint32_t deviceId(CameraModel);
  static bool reached(uint32_t, uint32_t);
  bool videoReady(uint8_t) const;
  static bool awaitingResponse(const Peer &, dji::Kind);
  static bool acceptsStatus(const Peer &);
  bool cameraResponse(uint8_t i, const dji::Decoded &);
  dji::Packet packetForStep(uint8_t i, Step step, uint16_t sequence) const;
  void admitPacket(uint8_t i, Step step, uint16_t seq);
  void retire(uint8_t, DjiFault);
  bool send(uint8_t, Step);
  void advance(uint8_t);
  void complete(uint8_t);
  void observe(uint8_t, const dji::Decoded &);
  void drain();
};
} // namespace ridesync

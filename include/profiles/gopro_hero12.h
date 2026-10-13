#pragma once
#include "ble_remote.h"
#include "camera_manager.h"
#include "camera_runtime_port.h"
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
  bool health_enabled = false;
  // Explicit commissioned ordinary-video premise. Encoding alone proves no mode.
  bool ordinary_video_confirmed = false;
};
// One serialized owner calls start, service, manager request/event/tick and
// configurePeer. Host callbacks enter only BleCentral's fixed copied queue.
class Hero12Adapter final : public CameraTransport, public BleResultSink, public CameraRuntimePort {
public:
  Hero12Adapter(BleHost &, Clock &);
  ~Hero12Adapter() = default;
  Hero12Adapter(const Hero12Adapter &) = delete;
  Hero12Adapter &operator=(const Hero12Adapter &) = delete;
  void attach(CameraManager &);
  bool attachHealthSink(CameraHealthSampleSink &) override;
  void detachHealthSink(const CameraHealthSampleSink *) override;
  uint32_t healthCoalesced() const override { return health_coalesced_; }
  uint32_t healthFailures() const override { return health_failures_; }
  void attachGroup(RecordingManager &group) override { group_ = &group; }
  void detachGroup(const RecordingManager *expected) override {
    if (group_ == expected)
      group_ = nullptr;
  }
  bool start(bool enabled, bool source_qualified);
  bool configurePeer(uint8_t peer, const Hero12Qualification &);
  void service(CameraServiceAction *action = nullptr) override { service(action, true); }
  void service(CameraServiceAction *, bool advance);
  void stop() override;
  bool canDestroy() const override;
  Hero12Fault fault(uint8_t peer) const override;
  CameraError
  requestRecovery(uint8_t peer, bool ensure_recording,
                  Hero12PowerCondition condition = Hero12PowerCondition::Unknown) override;
  CameraError cancelRecovery(uint8_t peer) override;
  Hero12RecoveryState recoveryState(uint8_t peer) const override;
  bool recoveryReady(uint8_t peer) const override;
  bool commandReady(uint8_t peer) const override;
  // Sealed links require replacement; Closed proves final per-peer host release.
  bool linkRetiring(uint8_t peer) const override;
  bool linkReleased(uint8_t peer) const override;
  // Permanent target seal, plus temporary shared-discovery drain. Existing
  // unrelated links continue; an interrupted shared scan has explicit Cancelled outcome.
  bool sealForMaintenance(uint8_t peer);
  bool maintenanceReleased(uint8_t peer) const;
  void finishMaintenanceDrain() { maintenance_scan_ = false; }
  const HealthProgress &progress() const { return progress_; }
  const BleCentral &central() const { return central_; }
  static BleProfileSpec profileSpec();
  static RetryPolicy managerPolicy();
  bool begin(size_t peer, const CameraConfig &, Operation, Token) override;
  void cancel(size_t peer, Token) override;
  void close(size_t peer, Token) override;
  void result(const BleResult &) override;

private:
  void advanceManagers(bool advance_managers);
  bool transactionExpired(uint8_t i) const;
  bool validateIdentity(uint8_t i, const gopro::IdentityResponse &r);
  bool notificationRoute(uint8_t endpoint, gopro::Channel &route);
  void recordAdmissionFailure(size_t index);
  void cancelRecoveryOperation(uint8_t i);
  CameraError recoveryAdmission(uint8_t i);
  static bool activeRecoveryPhase(Hero12RecoveryPhase);
  void beginRecoveryScan();
  bool hardwareRetryReady(uint8_t i) const;
  static bool serviceListHasFea6(const BleEvent &, size_t offset, size_t length);
  bool failedRecoveryOperation(uint8_t i);
  void requestRecoveryOperation(uint8_t i, Operation operation);
  void finishRecoveryObservation(uint8_t i, bool require_known);
  void transportClosed(const BleResult &r);
  void recoveryScanComplete(const BleResult &r);
  void recoveryAdvertisement(const BleResult &r);
  bool beginCommand(uint8_t i, Operation op, Token token);
  bool beginConnection(uint8_t i, Token token);
  void servicePeer(uint8_t i);
  void recoveryStart(uint8_t i);
  void recoveryObserve(uint8_t i);
  void recoveryConnect(uint8_t i);
  void advanceRecoveryPeer(uint8_t i);
  void unsolicitedStatus(uint8_t i, gopro::Channel route, const gopro::Message &message);
  void publishWireAck(uint8_t i);
  bool classicResponse(uint8_t i, gopro::Channel route, const gopro::Message &message);
  bool identityResponse(uint8_t i, gopro::Channel route, const gopro::Message &message);
  bool setupResponse(uint8_t i, gopro::Channel route, const gopro::Message &message);
  void writeComplete(uint8_t i, const BleResult &r);
  void notification(uint8_t i, const BleResult &r);
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
    KeepAlive,
    HealthRegister,
    HealthQuery
  };
  struct StepSpec {
    gopro::Request request;
    uint8_t status;
    CameraAckAction ack;
    Step next;
    bool classic;
  };
  static const StepSpec &stepSpec(Step);
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
    bool deferred_stop = false;
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
    uint32_t received_ms = 0, sequence = 0;
    bool qualified_receipt = false;
  };
  struct HealthPoll {
    uint32_t floor = 0, query_at = 0, encoding_due = 0, resource_due = 0;
    uint32_t sequence = 0, received_ms = 0, battery_received_ms = 0;
    uint8_t query_mask = 0;
    bool query_known = false, registered = false, bootstrapped = false;
    bool receipt_known = false, video_valid = false;
    bool battery_known = false, battery_present = false;
  };
  static_assert(sizeof(HealthPoll) <= 36, "bounded health polling state");
  struct Recovery {
    Hero12RecoveryState state;
    uint32_t deadline = 0, operation = 0, connection = 0, source_connection = 0;
    bool ensure_recording = false, observed_advertisement = false;
  };
  Clock &clock_;
  CameraManager *manager_ = nullptr;
  CameraHealthSampleSink *health_sink_ = nullptr;
  std::array<HealthPoll, kBlePeers> health_poll_{};
  uint32_t health_admitted_ms_ = 0, health_coalesced_ = 0, health_failures_ = 0;
  uint8_t health_cursor_ = 0;
  bool health_admitted_ = false;
  RecordingManager *group_ = nullptr;
  HealthProgress progress_;
  BleCentral central_;
  gopro::Reassembler reassembler_;
  std::array<Peer, kBlePeers> peers_{};
  std::array<bool, kBlePeers> disconnect_pending_{};
  std::array<bool, kBlePeers> maintenance_{};
  bool maintenance_scan_ = false;
  std::array<PendingEvent, kBlePeers * 2> events_{};
  std::array<Recovery, kBlePeers> recovery_{};
  uint8_t scan_owner_ = kBlePeers, scan_cursor_ = 0;
  uint32_t scan_generation_ = 0;
  uint8_t event_count_ = 0, cursor_ = 0;
  bool enabled_ = false, started_ = false, servicing_ = false;
  static bool requestedStatusPresent(const Peer &, const gopro::Response &);
  static bool registrationStatusPresent(uint8_t, const gopro::Response &);
  Step advanceHardware(uint8_t i);
  Step advanceReady(uint8_t i);
  Step advanceQuery(uint8_t i);
  Step advanceConfirmation(uint8_t i);
  Step nextStep(uint8_t i, Step done);
  bool acknowledged(uint8_t i) const;
  static bool setupStep(Step);
  static bool matchesCameraResponse(const Peer &, gopro::Channel, const gopro::Message &);
  struct OutgoingPacket {
    uint8_t bytes[20] = {};
    size_t length = 0;
    gopro::Channel route = gopro::Channel::Command;
    uint8_t id = 0, status = 0;
  };
  bool preparePacket(uint8_t i, Step step, OutgoingPacket &) const;
  bool ownsScanResult(const BleResult &) const;
  void transportEvent(uint8_t i, const BleResult &);
  bool completeCameraResponse(uint8_t, gopro::Channel, const gopro::Message &);
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
  bool admitPacket(uint8_t i, Step step, const OutgoingPacket &out);
  static bool healthStep(Step);
  bool healthEnabled(uint8_t) const;
  void serviceHealth();
  void healthResponse(uint8_t, const gopro::Response &);
  void healthField(uint8_t, HealthField, HealthUnit, uint64_t, HealthValidity);
  void finishHealth(uint8_t, bool failure);
  bool querySpacingReady(uint8_t) const;
};
} // namespace ridesync

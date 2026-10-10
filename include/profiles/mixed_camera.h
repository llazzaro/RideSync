#pragma once
#include "camera_runtime_port.h"
#include "profiles/gopro_hero12.h"
#include "profiles/insta360_go3s.h"
#include "profiles/insta360_one_rs.h"
#include "profiles/insta360_x5.h"
#include "recording_manager.h"
#include "wake_manager.h"
namespace ridesync {
struct MixedCameraQualifications {
  X5Qualification x5;
  WakePeerConfig x5_wake;
  WakePolicy x5_wake_policy;
  std::array<OneRsQualification, kMaxCameras> one_rs{};
  std::array<Go3sQualification, kMaxCameras> go3s{};
  std::array<Hero12Qualification, kMaxCameras> hero12{};
  std::array<GpsForwardingConfig, kMaxCameras> gps{};
};
// One manager/group/audit for every configured model on the shared host.
// The X5 peripheral route occupies slot zero; no per-model singleton is ticked.
class MixedCameraAdapter final : public CameraTransport,
                                 public CameraRuntimePort,
                                 public GpsSnapshotConsumer {
public:
  MixedCameraAdapter(BleHost &, X5PeripheralPort &, Clock &, WakeRadio *radio = nullptr);
  void attach(CameraManager &, RecordingManager &);
  bool configure(const SourceConfig &, const MixedCameraQualifications &);
  bool start(bool enabled, bool qualified);
  static RetryPolicy managerPolicy();
  bool begin(size_t, const CameraConfig &, Operation, Token) override;
  void cancel(size_t, Token) override;
  void close(size_t, Token) override;
  void attachGroup(RecordingManager &) override;
  void detachGroup(const RecordingManager *) override;
  void service(CameraServiceAction *action = nullptr) override;
  void stop() override;
  bool canDestroy() const override;
  Hero12Fault fault(uint8_t) const override;
  CameraError requestRecovery(uint8_t, bool,
                              Hero12PowerCondition = Hero12PowerCondition::Unknown) override;
  CameraError cancelRecovery(uint8_t) override;
  Hero12RecoveryState recoveryState(uint8_t) const override;
  bool recoveryReady(uint8_t) const override;
  bool commandReady(uint8_t) const override;
  bool wakeSupported(uint8_t i) const override {
    return i == 0 && wake_config_.enabled && wake_config_.source_qualified;
  }
  bool linkRetiring(uint8_t) const override;
  bool linkReleased(uint8_t) const override;
  void offer(const RecordTimestamp &, const ModemSnapshot &) override;
  void cancel() override { one_.forwarding().cancel(); }
  Hero12Adapter &hero12() { return hero_; }
  const HealthProgress &progress() const { return progress_; }

private:
  class X5Recovery final : public WakeRecovery {
  public:
    explicit X5Recovery(MixedCameraAdapter &owner) : owner_(owner) {}
    RecordingState currentObserved(uint8_t) const override;
    CameraError begin(const WakeOperation &, uint32_t) override;
    WakeRecoveryResult poll(const WakeOperation &, uint32_t) override;
    void cancel(const WakeOperation &) override;

  private:
    MixedCameraAdapter &owner_;
    WakeRecoveryResult result_;
    uint32_t deadline_ = 0;
    bool used_ = false;
    void fail(CameraError);
  };
  Clock &clock_;
  X5Recovery wake_recovery_;
  WakeManager wake_;
  WakePeerConfig wake_config_;
  WakePolicy wake_policy_;
  uint32_t wake_id_ = 0;
  X5Adapter x5_;
  OneRsAdapter one_;
  Go3sAdapter go_;
  Hero12Adapter hero_;
  CameraManager *manager_ = nullptr;
  RecordingManager *group_ = nullptr;
  SourceConfig source_;
  std::array<bool, kMaxCameras> recovery_{};
  HealthProgress progress_;
  bool configured_ = false, servicing_ = false, stopped_ = false;
  CameraTransport *route(size_t);
};
} // namespace ridesync

#pragma once
#include "camera_manager.h"
namespace ridesync {
enum class HealthReason : uint8_t {
  UnexpectedStop,
  RecordingUnconfirmed,
  RecordingEvidenceLost,
  BatteryLow,
  StorageLow,
  Overheating,
  MonitoringUnavailable,
  ResourceUnavailable,
  Count
};
enum class HealthSeverity : uint8_t { None, Status, Warning, Critical };
enum class HealthTransitionKind : uint8_t { Enter, SeverityChanged, Recovered, Retired };
struct CameraHealthField {
  uint64_t value = 0; // Historical when validity != Valid; never fabricated zero.
  uint32_t received_ms = 0, sequence = 0, sample_received_ms = 0, connection = 0;
  CapabilityState capability = CapabilityState::Unknown;
  HealthValidity validity = HealthValidity::Unknown;
  HealthProfile profile = HealthProfile::None;
  HealthUnit unit = HealthUnit::None;
  bool historical = false;
};
struct CameraHealthPeer {
  std::array<CameraHealthField, static_cast<size_t>(HealthField::Count)> fields{};
  std::array<HealthSeverity, static_cast<size_t>(HealthReason::Count)> alerts{};
  std::array<uint32_t, 3> recovery_since{};
  std::array<HealthSeverity, 3> published_resources{};
  uint32_t connection = 0, operation = 0, intent_id = 0, group_generation = 0;
  uint32_t episode = 0, first_deadline = 0, minimum_poll_ms = 0;
  CameraModel model = CameraModel::Unknown;
  uint8_t recovery_pending = 0, fields_seen = 0, receipt_order_known = 0;
  bool connected = false, armed = false, saw_recording = false;
  bool first_waiting = false;
};
struct CameraHealthTransition {
  uint64_t value = 0;
  uint32_t connection = 0, operation = 0, intent_id = 0, group_generation = 0;
  uint32_t episode = 0, sequence = 0, received_ms = 0, age_ms = 0;
  uint32_t threshold_enter = 0, threshold_clear = 0;
  uint8_t peer = 0;
  HealthReason reason = HealthReason::UnexpectedStop;
  HealthSeverity severity = HealthSeverity::None;
  HealthTransitionKind kind = HealthTransitionKind::Enter;
  CapabilityState capability = CapabilityState::Unknown;
  HealthValidity validity = HealthValidity::Unknown;
  HealthProfile profile = HealthProfile::None;
  HealthUnit unit = HealthUnit::None;
  HealthField field = HealthField::Recording;
  bool historical = false;
};
class CameraHealthEvaluator final : public CameraHealthObserver {
public:
  static constexpr size_t kTransitionDepth = 8;
  // Configure an owner slot; poll floor must be qualified (1..30000 ms).
  void configure(size_t peer, CameraModel model, uint32_t connection, bool connected, uint32_t now,
                 uint32_t minimum_poll_ms = 0);
  void pollPeriod(size_t peer, uint32_t minimum_poll_ms, uint32_t now);
  bool observe(size_t peer, const QualifiedHealthSample &, uint32_t now);
  void tick(uint32_t now);
  const CameraHealthPeer *snapshot(size_t peer) const;
  HealthSeverity severity(size_t peer, HealthReason) const;
  bool popTransition(CameraHealthTransition &);
  size_t pendingTransitions() const { return count_; }
  uint32_t dropped() const { return dropped_; }
  uint32_t coalesced() const { return coalesced_; }
  void noteCoalesced(uint32_t count);
  uint32_t historyCoalesced() const { return history_coalesced_; }
  void healthIntent(size_t, Operation, uint32_t, uint32_t, bool, Token, uint32_t,
                    uint32_t) override;
  void healthAttempt(size_t, Operation, Token, uint32_t, uint32_t) override;
  void healthGroupIntent(size_t, RecordingState, uint32_t, Token, uint32_t) override;
  void healthRetired(size_t, Token, uint32_t) override;
  void healthReset(size_t, Token, uint32_t) override;
  void healthAccepted(const Event &, Operation, uint32_t, uint32_t) override;
  void healthFailure(size_t, Token, bool, uint32_t) override;
  void healthConnection(size_t, CameraModel, Token, bool, uint32_t) override;

private:
  std::array<CameraHealthPeer, kMaxCameras> peers_{};
  std::array<CameraHealthTransition, kTransitionDepth> transitions_{};
  uint32_t sequence_ = 0, dropped_ = 0, coalesced_ = 0, last_tick_ = 0, history_coalesced_ = 0;
  uint8_t head_ = 0, count_ = 0;
  bool ticked_ = false;
  void alert(size_t, HealthReason, HealthSeverity, uint32_t,
             HealthTransitionKind clear = HealthTransitionKind::Recovered, bool force = false);
  void arm(size_t, uint32_t, uint32_t, Token, uint32_t, uint32_t);
  void expire(size_t, uint32_t, bool discontinuity);
  void resource(size_t, HealthField, uint32_t);
  void availability(size_t, uint32_t);
  uint32_t expiry(const CameraHealthPeer &, HealthField) const;
  bool fresh(const CameraHealthPeer &, HealthField, uint32_t) const;
};
static_assert(sizeof(CameraHealthPeer) <= 256, "Health per-peer memory cap");
static_assert(sizeof(CameraHealthTransition) <= 96, "Health transition memory cap");
static_assert(sizeof(CameraHealthEvaluator) <= 3200, "Health evaluator leaves scheduler budget");
} // namespace ridesync

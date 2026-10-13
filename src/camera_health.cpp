#include "camera_health.h"
#include <algorithm>
namespace ridesync {
void CameraHealthEvaluator::noteCoalesced(uint32_t count) {
  history_coalesced_ =
      count > UINT32_MAX - history_coalesced_ ? UINT32_MAX : history_coalesced_ + count;
}
namespace {
constexpr uint32_t kHalf = 0x80000000UL;
size_t index(HealthField f) { return static_cast<size_t>(f); }
size_t index(HealthReason r) { return static_cast<size_t>(r); }
void increment(uint32_t &value) {
  if (value != UINT32_MAX)
    ++value;
}
bool djiModel(CameraModel m) {
  return m == CameraModel::DJI_ACTION4 || m == CameraModel::DJI_ACTION5_PRO ||
         m == CameraModel::DJI_ACTION6 || m == CameraModel::DJI_OSMO360;
}
HealthProfile profile(CameraModel m) {
  if (m == CameraModel::X5)
    return HealthProfile::X5Ce80;
  if (djiModel(m))
    return HealthProfile::DjiStatus;
  if (m == CameraModel::HERO12_BLACK)
    return HealthProfile::Hero12V1;
  return HealthProfile::None;
}
HealthUnit unit(HealthField f) {
  switch (f) {
  case HealthField::Recording:
    return HealthUnit::RecordingState;
  case HealthField::BatteryPercent:
    return HealthUnit::Percent;
  case HealthField::RemainingVideoSeconds:
    return HealthUnit::Seconds;
  case HealthField::SdRemainingKilobytes:
    return HealthUnit::Kilobytes;
  case HealthField::Overheating:
    return HealthUnit::Boolean;
  default:
    return HealthUnit::None;
  }
}
HealthField reasonField(HealthReason r) {
  if (r == HealthReason::BatteryLow)
    return HealthField::BatteryPercent;
  if (r == HealthReason::StorageLow)
    return HealthField::RemainingVideoSeconds;
  if (r == HealthReason::Overheating)
    return HealthField::Overheating;
  return HealthField::Recording;
}
bool recordingReason(HealthReason r) {
  return r == HealthReason::UnexpectedStop || r == HealthReason::RecordingUnconfirmed ||
         r == HealthReason::RecordingEvidenceLost || r == HealthReason::MonitoringUnavailable;
}
bool forward(uint32_t current, uint32_t previous) {
  const auto delta = current - previous;
  return delta && delta < kHalf;
}
} // namespace
const CameraHealthPeer *CameraHealthEvaluator::snapshot(size_t i) const {
  return i < kMaxCameras ? &peers_[i] : nullptr;
}
uint32_t CameraHealthEvaluator::expiry(const CameraHealthPeer &p, HealthField f) const {
  if (f == HealthField::Recording) {
    if (p.model == CameraModel::X5)
      return 5000;
    if (djiModel(p.model))
      return 1500;
  }
  if (p.model == CameraModel::HERO12_BLACK && p.minimum_poll_ms && p.minimum_poll_ms <= 30000)
    return 2 * std::max(f == HealthField::Recording ? 5000UL : 15000UL,
                        static_cast<unsigned long>(p.minimum_poll_ms)) +
           1000;
  return 0;
}
bool CameraHealthEvaluator::fresh(const CameraHealthPeer &p, HealthField f, uint32_t now) const {
  const auto &s = p.fields[index(f)];
  const uint32_t age = now - s.received_ms, limit = expiry(p, f);
  return p.connected && s.capability == CapabilityState::Supported &&
         s.validity == HealthValidity::Valid && age < kHalf && limit &&
         (p.model == CameraModel::X5 && f == HealthField::Recording ? age <= limit : age < limit);
}
HealthSeverity CameraHealthEvaluator::severity(size_t i, HealthReason r) const {
  if (i >= kMaxCameras || index(r) >= index(HealthReason::Count))
    return HealthSeverity::None;
  const auto &p = peers_[i];
  const auto severity = p.alerts[index(r)];
  const auto field = reasonField(r);
  if ((r == HealthReason::BatteryLow || r == HealthReason::StorageLow ||
       r == HealthReason::Overheating) &&
      severity != HealthSeverity::None && p.fields[index(field)].validity != HealthValidity::Valid)
    return HealthSeverity::Warning; // Unresolved fault history, current evidence is Unknown.
  return severity;
}
void CameraHealthEvaluator::alert(size_t i, HealthReason reason, HealthSeverity next, uint32_t now,
                                  HealthTransitionKind clear, bool force) {
  auto &p = peers_[i];
  auto &stored = p.alerts[index(reason)];
  const bool resourceReason = reason == HealthReason::BatteryLow ||
                              reason == HealthReason::StorageLow ||
                              reason == HealthReason::Overheating;
  const unsigned resourceIndex = reason == HealthReason::BatteryLow   ? 0
                                 : reason == HealthReason::StorageLow ? 1
                                                                      : 2;
  const auto current = resourceReason ? p.published_resources[resourceIndex] : stored;
  stored = next;
  if (resourceReason && next != HealthSeverity::None &&
      p.fields[index(reasonField(reason))].validity != HealthValidity::Valid)
    next = HealthSeverity::Warning;
  if (current == next && !force) {
    if (next != HealthSeverity::None)
      increment(coalesced_);
    return;
  }
  if (resourceReason)
    p.published_resources[resourceIndex] = next;
  CameraHealthTransition t;
  t.peer = static_cast<uint8_t>(i);
  t.reason = reason;
  t.severity = next;
  t.kind = next == HealthSeverity::None      ? clear
           : current == HealthSeverity::None ? HealthTransitionKind::Enter
                                             : HealthTransitionKind::SeverityChanged;
  t.connection = p.connection;
  t.operation = p.operation;
  t.intent_id = p.intent_id;
  t.group_generation = p.group_generation;
  t.episode = p.episode;
  increment(sequence_);
  t.sequence = sequence_;
  t.field = reasonField(reason);
  if (reason == HealthReason::ResourceUnavailable)
    for (size_t j = 1; j < index(HealthField::Count); ++j)
      if (p.fields[j].capability != CapabilityState::Supported ||
          p.fields[j].validity != HealthValidity::Valid) {
        t.field = static_cast<HealthField>(j);
        break;
      }
  const auto &field = p.fields[index(t.field)];
  t.historical = field.historical;
  t.value = field.value;
  t.received_ms = field.received_ms;
  t.age_ms = field.historical ? now - field.received_ms : 0;
  t.capability = field.capability;
  t.validity = field.validity;
  t.profile = field.profile;
  t.unit = field.unit;
  if (reason == HealthReason::BatteryLow) {
    t.threshold_enter = next == HealthSeverity::Critical ? 5 : 20;
    t.threshold_clear = next == HealthSeverity::Critical ? 10 : 25;
  } else if (reason == HealthReason::StorageLow) {
    t.threshold_enter = next == HealthSeverity::Critical ? 60 : 300;
    t.threshold_clear = next == HealthSeverity::Critical ? 120 : 420;
  } else if (reason == HealthReason::Overheating) {
    t.threshold_enter = 1;
    t.threshold_clear = 0;
  }
  if (sequence_ == UINT32_MAX) {
    increment(dropped_);
    return;
  }
  if (count_ == kTransitionDepth) {
    increment(dropped_);
    return;
  }
  transitions_[(head_ + count_) % kTransitionDepth] = t;
  ++count_;
}
bool CameraHealthEvaluator::popTransition(CameraHealthTransition &out) {
  if (!count_)
    return false;
  out = transitions_[head_];
  head_ = (head_ + 1) % kTransitionDepth;
  --count_;
  return true;
}
void CameraHealthEvaluator::configure(size_t i, CameraModel model, uint32_t connection,
                                      bool connected, uint32_t now, uint32_t period) {
  if (i >= kMaxCameras)
    return;
  auto &p = peers_[i];
  if (p.model != model)
    healthReset(i, Token{}, now);
  const bool generation = p.connection != connection || p.model != model;
  if (generation) {
    p.fields_seen = 0;
    p.receipt_order_known = 0;
    p.recovery_pending = 0;
    for (size_t j = 0; j < index(HealthField::Count); ++j) {
      auto &f = p.fields[j];
      f.capability =
          (j == 0 ? profile(model) != HealthProfile::None : model == CameraModel::HERO12_BLACK)
              ? CapabilityState::Supported
              : CapabilityState::Unsupported;
      f.validity = HealthValidity::Unknown;
      f.profile = profile(model);
      f.unit = unit(static_cast<HealthField>(j));
    }
  }
  p.model = model;
  p.connection = connection;
  p.connected = connected;
  p.minimum_poll_ms = period && period <= 30000 ? period : 0;
  if (!ticked_) {
    last_tick_ = now;
    ticked_ = true;
  }
  expire(i, now, false);
  availability(i, now);
}
void CameraHealthEvaluator::pollPeriod(size_t i, uint32_t period, uint32_t now) {
  if (i >= kMaxCameras)
    return;
  peers_[i].minimum_poll_ms = period && period <= 30000 ? period : 0;
  expire(i, now, false);
  availability(i, now);
}
void CameraHealthEvaluator::availability(size_t i, uint32_t now) {
  auto &p = peers_[i];
  const auto unavailable = p.armed && (p.fields[0].capability != CapabilityState::Supported ||
                                       !expiry(p, HealthField::Recording));
  alert(i, HealthReason::MonitoringUnavailable,
        unavailable ? HealthSeverity::Warning : HealthSeverity::None, now);
  bool resources = false;
  if (p.model == CameraModel::HERO12_BLACK)
    for (size_t f = 1; f < index(HealthField::Count); ++f)
      resources |= p.fields[f].capability != CapabilityState::Supported ||
                   p.fields[f].validity != HealthValidity::Valid;
  alert(i, HealthReason::ResourceUnavailable,
        resources ? HealthSeverity::Status : HealthSeverity::None, now);
  alert(i, HealthReason::BatteryLow, p.alerts[index(HealthReason::BatteryLow)], now);
  alert(i, HealthReason::StorageLow, p.alerts[index(HealthReason::StorageLow)], now);
  alert(i, HealthReason::Overheating, p.alerts[index(HealthReason::Overheating)], now);
}
void CameraHealthEvaluator::arm(size_t i, uint32_t intent, uint32_t group, Token token,
                                uint32_t now, uint32_t deadline) {
  if (i >= kMaxCameras)
    return;
  auto &p = peers_[i];
  if (p.armed && group && group == p.group_generation) {
    p.intent_id = intent;
    p.operation = token.operation;
    p.first_deadline = deadline;
    return;
  }
  healthRetired(i, token, now);
  if (p.episode == UINT32_MAX)
    return; // Never alias an exhausted episode identity.
  ++p.episode;
  p.armed = true;
  p.intent_id = intent;
  p.group_generation = group;
  p.operation = token.operation;
  p.first_deadline = deadline;
  p.saw_recording = fresh(p, HealthField::Recording, now) &&
                    p.fields[0].value == static_cast<uint64_t>(RecordingState::Recording);
  p.first_waiting = !p.saw_recording;
  availability(i, now);
}
void CameraHealthEvaluator::healthIntent(size_t i, Operation op, uint32_t intent, uint32_t group,
                                         bool, Token token, uint32_t now, uint32_t deadline) {
  if (op == Operation::Start)
    arm(i, intent, group, token, now, deadline);
  else if (op == Operation::Stop)
    healthRetired(i, token, now);
}
void CameraHealthEvaluator::healthAttempt(size_t i, Operation op, Token token, uint32_t intent,
                                          uint32_t) {
  if (i < kMaxCameras && op == Operation::Start && peers_[i].armed && peers_[i].intent_id == intent)
    peers_[i].operation = token.operation;
}
void CameraHealthEvaluator::healthGroupIntent(size_t i, RecordingState state, uint32_t group,
                                              Token token, uint32_t now) {
  if (state == RecordingState::Recording) {
    token.operation = 0; // A group admission may need no shutter operation.
    arm(i, 0, group, token, now, now + 142000);
  } else if (state == RecordingState::Stopped)
    healthRetired(i, token, now);
}
void CameraHealthEvaluator::healthRetired(size_t i, Token, uint32_t now) {
  if (i >= kMaxCameras)
    return;
  auto &p = peers_[i];
  bool recording_alert = false;
  for (size_t j = 0; j < index(HealthReason::Count); ++j)
    if (recordingReason(static_cast<HealthReason>(j))) {
      recording_alert |= p.alerts[j] != HealthSeverity::None;
      alert(i, static_cast<HealthReason>(j), HealthSeverity::None, now,
            HealthTransitionKind::Retired);
    }
  if (p.armed && !recording_alert)
    alert(i, HealthReason::RecordingEvidenceLost, HealthSeverity::None, now,
          HealthTransitionKind::Retired, true);
  p.armed = p.saw_recording = p.first_waiting = false;
}
void CameraHealthEvaluator::healthReset(size_t i, Token token, uint32_t now) {
  if (i >= kMaxCameras)
    return;
  healthRetired(i, token, now);
  auto &p = peers_[i];
  for (size_t j = 0; j < index(HealthReason::Count); ++j)
    alert(i, static_cast<HealthReason>(j), HealthSeverity::None, now,
          HealthTransitionKind::Retired);
  p.fields = {};
  p.fields_seen = 0;
  p.receipt_order_known = 0;
  p.recovery_pending = 0;
  p.minimum_poll_ms = 0;
  p.connected = false;
}
void CameraHealthEvaluator::healthConnection(size_t i, CameraModel model, Token token,
                                             bool connected, uint32_t now) {
  if (i >= kMaxCameras)
    return;
  const auto &p = peers_[i];
  const uint32_t period =
      p.connection == token.connection && p.model == model ? p.minimum_poll_ms : 0;
  configure(i, model, token.connection, connected, now, period);
}
void CameraHealthEvaluator::healthAccepted(const Event &e, Operation operation, uint32_t intent,
                                           uint32_t now) {
  if (e.peer >= kMaxCameras)
    return;
  auto &p = peers_[e.peer];
  if (e.has_health &&
      (e.kind == EventKind::RecordingObserved || e.kind == EventKind::CommandRecordingObserved))
    observe(e.peer, e.health, now);
  if (e.kind == EventKind::Completed && operation == Operation::Start && p.armed &&
      p.intent_id == intent && !p.saw_recording) {
    p.first_deadline = now + 1000;
    p.first_waiting = true;
  }
}
void CameraHealthEvaluator::healthFailure(size_t i, Token, bool terminal, uint32_t now) {
  if (i >= kMaxCameras)
    return;
  auto &p = peers_[i];
  if (terminal && p.armed && !p.saw_recording && expiry(p, HealthField::Recording)) {
    p.first_waiting = false;
    alert(i, HealthReason::RecordingUnconfirmed, HealthSeverity::Critical, now);
  }
}
void CameraHealthEvaluator::expire(size_t i, uint32_t now, bool discontinuity) {
  auto &p = peers_[i];
  if (discontinuity)
    p.receipt_order_known = 0;
  for (size_t j = 0; j < index(HealthField::Count); ++j) {
    const auto f = static_cast<HealthField>(j);
    if (p.fields[j].validity == HealthValidity::Valid && (discontinuity || !fresh(p, f, now))) {
      p.fields[j].validity = HealthValidity::Unknown;
      if (j > 0 && j != index(HealthField::SdRemainingKilobytes)) {
        const unsigned n = j == 1 ? 0 : j == 2 ? 1 : 2;
        p.recovery_pending &= ~(1U << n);
      }
    }
  }
  if (p.armed && p.saw_recording && !fresh(p, HealthField::Recording, now))
    alert(i, HealthReason::RecordingEvidenceLost, HealthSeverity::Warning, now);
  if (p.armed && !p.saw_recording && p.first_waiting && expiry(p, HealthField::Recording) &&
      (discontinuity || now - p.first_deadline < kHalf)) {
    p.first_waiting = false;
    alert(i, HealthReason::RecordingUnconfirmed, HealthSeverity::Critical, now);
  }
}
void CameraHealthEvaluator::tick(uint32_t now) {
  const bool discontinuity = ticked_ && now - last_tick_ >= kHalf;
  last_tick_ = now;
  ticked_ = true;
  for (size_t i = 0; i < kMaxCameras; ++i) {
    expire(i, now, discontinuity);
    availability(i, now);
  }
}
void CameraHealthEvaluator::resource(size_t i, HealthField field, uint32_t now) {
  if (field == HealthField::SdRemainingKilobytes || field == HealthField::Recording)
    return;
  auto &p = peers_[i];
  const auto &f = p.fields[index(field)];
  const unsigned n = field == HealthField::BatteryPercent          ? 0
                     : field == HealthField::RemainingVideoSeconds ? 1
                                                                   : 2;
  const auto reason = n == 0   ? HealthReason::BatteryLow
                      : n == 1 ? HealthReason::StorageLow
                               : HealthReason::Overheating;
  const uint64_t warning = n == 0 ? 20 : n == 1 ? 300 : 0, critical = n == 0 ? 5 : n == 1 ? 60 : 1;
  const uint64_t clear = n == 0 ? 25 : n == 1 ? 420 : 0, downgrade = n == 0 ? 10 : n == 1 ? 120 : 0;
  const auto current = p.alerts[index(reason)];
  const auto unhealthy = n == 2 ? (f.value ? HealthSeverity::Critical : HealthSeverity::None)
                         : f.value <= critical ? HealthSeverity::Critical
                         : f.value <= warning  ? HealthSeverity::Warning
                                               : HealthSeverity::None;
  if (static_cast<unsigned>(unhealthy) > static_cast<unsigned>(current)) {
    p.recovery_pending &= ~(1U << n);
    alert(i, reason, unhealthy, now);
    return;
  }
  if (current == HealthSeverity::None) {
    p.recovery_pending &= ~(1U << n);
    return;
  }
  const bool canRecover = n == 2                                ? f.value == 0
                          : current == HealthSeverity::Critical ? f.value >= downgrade
                                                                : f.value >= clear;
  if (!canRecover) {
    p.recovery_pending &= ~(1U << n);
    return;
  }
  if (!(p.recovery_pending & (1U << n))) {
    p.recovery_pending |= (1U << n);
    p.recovery_since[n] = f.received_ms;
    return;
  }
  const auto gap = f.received_ms - p.recovery_since[n];
  if (gap < 5000 || gap >= kHalf)
    return;
  p.recovery_pending &= ~(1U << n);
  // Critical may downgrade; warning clear still needs its own two fresh samples.
  alert(i, reason,
        current == HealthSeverity::Critical && n != 2 ? HealthSeverity::Warning
                                                      : HealthSeverity::None,
        now);
}
bool CameraHealthEvaluator::observe(size_t i, const QualifiedHealthSample &s, uint32_t now) {
  if (i >= kMaxCameras || index(s.field) >= index(HealthField::Count))
    return false;
  auto &p = peers_[i];
  auto &f = p.fields[index(s.field)];
  if (!p.connected || s.connection != p.connection || s.profile == HealthProfile::None ||
      s.profile != profile(p.model) || s.unit != unit(s.field) ||
      (s.field != HealthField::Recording && p.model != CameraModel::HERO12_BLACK)) {
    increment(dropped_);
    return false;
  }
  const auto mask = uint8_t(1U << index(s.field));
  if ((p.fields_seen & mask) &&
      (!forward(s.sequence, f.sequence) ||
       ((p.receipt_order_known & mask) && s.received_ms - f.sample_received_ms >= kHalf))) {
    increment(coalesced_);
    return false;
  }
  if (s.validity == HealthValidity::Valid && s.capability == CapabilityState::Supported &&
      ((s.field == HealthField::Recording && (s.value < 1 || s.value > 2)) ||
       (s.field == HealthField::BatteryPercent && s.value > 100) ||
       (s.field == HealthField::Overheating && s.value > 1))) {
    increment(dropped_);
    return false;
  }
  expire(i, now, false);
  f.capability = s.capability;
  f.profile = s.profile;
  f.unit = s.unit;
  f.sequence = s.sequence;
  f.sample_received_ms = s.received_ms;
  p.fields_seen |= mask;
  p.receipt_order_known |= mask;
  f.validity =
      s.capability == CapabilityState::Supported ? s.validity : HealthValidity::Unavailable;
  if (f.validity == HealthValidity::Valid) {
    const auto age = now - s.received_ms, limit = expiry(p, s.field);
    if (age >= kHalf || !limit ||
        (p.model == CameraModel::X5 && s.field == HealthField::Recording ? age > limit
                                                                         : age >= limit)) {
      f.validity = HealthValidity::Unknown;
      increment(dropped_);
    } else {
      f.value = s.value;
      f.received_ms = s.received_ms;
      f.historical = true;
      f.connection = s.connection;
    }
  }
  if (f.validity != HealthValidity::Valid) {
    if (s.field != HealthField::Recording && s.field != HealthField::SdRemainingKilobytes) {
      const unsigned n = s.field == HealthField::BatteryPercent          ? 0
                         : s.field == HealthField::RemainingVideoSeconds ? 1
                                                                         : 2;
      p.recovery_pending &= ~(1U << n);
    }
    expire(i, now, false);
    availability(i, now);
    return true;
  }
  if (s.field == HealthField::Recording && p.armed) {
    alert(i, HealthReason::RecordingEvidenceLost, HealthSeverity::None, now);
    if (s.value == static_cast<uint64_t>(RecordingState::Recording)) {
      p.saw_recording = true;
      p.first_waiting = false;
      alert(i, HealthReason::UnexpectedStop, HealthSeverity::None, now);
      alert(i, HealthReason::RecordingUnconfirmed, HealthSeverity::None, now);
    } else if (p.saw_recording)
      alert(i, HealthReason::UnexpectedStop, HealthSeverity::Critical, now);
  } else if (s.field != HealthField::Recording)
    resource(i, s.field, now);
  availability(i, now);
  return true;
}
} // namespace ridesync

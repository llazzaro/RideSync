#include "config_bootstrap.h"
#include <limits>
#include <utility>
namespace ridesync {
namespace {
bool effectiveLoad(PersistStatus status) {
  return status == PersistStatus::Defaults || status == PersistStatus::Loaded ||
         status == PersistStatus::Migrated || status == PersistStatus::Recovered;
}
template <size_t N> bool terminated(const char (&s)[N]) { return std::memchr(s, 0, N) != nullptr; }
} // namespace
bool copySettings(const SourceConfig &source, Settings &out) {
  if (!validateSettings(source))
    return false;
  for (size_t i = 0; i < source.count; ++i)
    if (source.cameras[i].name.find('\0') != std::string::npos)
      return false;
  Settings candidate;
  candidate.count = source.count;
  candidate.capacity = source.capacity;
  candidate.button = source.button;
  candidate.button_gpio = source.button_gpio;
  for (size_t i = 0; i < source.count; ++i) {
    const auto &in = source.cameras[i];
    auto &p = candidate.cameras[i];
    std::memcpy(p.name, in.name.c_str(), in.name.size() + 1);
    std::memcpy(p.identifier, in.identifier.c_str(), in.identifier.size() + 1);
    std::memcpy(p.wake_identifier, in.wake_identifier.c_str(), in.wake_identifier.size() + 1);
    p.family = in.family;
    p.model = in.model;
    p.address_type = in.address_type;
    p.enabled = in.enabled;
    p.gps_telemetry = in.gps_telemetry;
  }
  out = candidate;
  return true;
}
bool expandSettings(const Settings &source, SourceConfig &out) {
  if (source.count > kMaxCameras)
    return false;
  // Check all string bounds before constructing any std::string.
  for (size_t i = 0; i < source.count; ++i) {
    const auto &p = source.cameras[i];
    if (!terminated(p.name) || !terminated(p.identifier) || !terminated(p.wake_identifier))
      return false;
  }
  SourceConfig candidate;
  candidate.count = source.count;
  candidate.capacity = source.capacity;
  candidate.button = source.button;
  candidate.button_gpio = source.button_gpio;
  for (size_t i = 0; i < source.count; ++i) {
    const auto &in = source.cameras[i];
    auto &p = candidate.cameras[i];
    p.name = in.name;
    p.identifier = in.identifier;
    p.wake_identifier = in.wake_identifier;
    p.family = in.family;
    p.model = in.model;
    p.address_type = in.address_type;
    p.enabled = in.enabled;
    p.gps_telemetry = in.gps_telemetry;
  }
  if (!validateSettings(candidate))
    return false;
  out = std::move(candidate);
  return true;
}
bool ApplicationSettings::accept(const SettingsSnapshot &s) {
  if (!epoch_ || !s.completed || s.epoch != epoch_ || s.generation <= snapshot_.generation)
    return false;
  snapshot_ = s;
  return true;
}
SettingsAdmission admitSettings(const SettingsSnapshot &s, const SettingsQualification &q) {
  SettingsAdmission a;
  // Independent eligibility only: this does not start or qualify any hardware.
  a.local_telemetry = q.local_telemetry;
  if (!s.completed || !s.effective || q.safe_mode || !q.nvs_allowed)
    return a;
  bool enabled = false;
  for (size_t i = 0; i < s.settings.count && i < kMaxCameras; ++i)
    enabled = enabled || s.settings.cameras[i].enabled;
  a.cameras = q.cameras && enabled && s.peers_valid;
  a.button = q.button && s.settings.button_gpio.enabled;
  return a;
}
bool ConfigBootstrap::peersValid() const {
  const auto &s = snapshot_.settings;
  if (!peers_stable_ || peers_.count != s.count || peers_.count > kMaxCameras)
    return false;
  for (size_t i = 0; i < peers_.count; ++i) {
    const auto &p = peers_.entries[i];
    if (!p.id || p.slot != i || p.model != s.cameras[i].model)
      return false;
    for (size_t j = 0; j < i; ++j)
      if (p.id == peers_.entries[j].id)
        return false;
  }
  return true;
}
void ConfigBootstrap::publish() {
  if (dirty_ && publication_.put(snapshot_))
    dirty_ = false;
}
void ConfigBootstrap::unavailable(PersistResult result) {
  if (started_)
    return;
  started_ = true;
  snapshot_.epoch = epoch_;
  snapshot_.generation = 1;
  snapshot_.completed = true;
  snapshot_.load = snapshot_.outcome = result;
  dirty_ = true;
  publish();
}
void ConfigBootstrap::start(bool safe_mode, bool nvs_allowed) {
  if (started_)
    return;
  if (!epoch_ || safe_mode || !nvs_allowed) {
    unavailable(PersistStatus::Refused);
    return;
  }
  started_ = true;
  snapshot_.epoch = epoch_;
  snapshot_.generation = 1;
  snapshot_.load = snapshot_.outcome = persistence_.load(ram_);
  if (snapshot_.load.status == PersistStatus::Defaults && defaults_supplied_) {
    if (!defaults_valid_ || !expandSettings(defaults_, ram_))
      snapshot_.outcome = PersistStatus::Invalid;
  }
  snapshot_.effective = effectiveLoad(snapshot_.load.status) &&
                        snapshot_.outcome.status != PersistStatus::Invalid &&
                        copySettings(ram_, snapshot_.settings);
  if (effectiveLoad(snapshot_.load.status) && !snapshot_.effective)
    snapshot_.outcome = PersistStatus::Invalid;
  snapshot_.completed = true;
  snapshot_.peers = peers_;
  snapshot_.peers_valid = snapshot_.effective && peersValid();
  dirty_ = true;
  publish();
}
void ConfigBootstrap::refuse(PersistResult result) {
  persistence_.discardPending();
  saving_ = false;
  save_outcome_ = SaveOutcome::Refused;
  save_result_ = result;
  if (!snapshot_.effective)
    return;
  snapshot_.effective = snapshot_.peers_valid = false;
  // No partial/prior camera settings may be treated as currently effective.
  snapshot_.settings = Settings{};
  snapshot_.outcome = result;
  ++snapshot_.generation;
  dirty_ = true;
}
void ConfigBootstrap::service(uint32_t now, bool safe_mode, bool nvs_allowed) {
  if (!started_)
    return;
  // Refusal is terminal for this boot-lifetime owner. Operator clear alone cannot
  // reopen/replay settings or admit cameras; a future supervised restart must quiesce.
  if (safe_mode || !nvs_allowed)
    refuse(PersistStatus::Refused);
  if (requests_.take(request_)) {
    if (!snapshot_.effective)
      save_outcome_ = SaveOutcome::Refused;
    else if (request_.epoch != epoch_ || request_.generation != snapshot_.generation)
      save_outcome_ = SaveOutcome::Stale;
    else if (saving_)
      save_outcome_ = SaveOutcome::Busy;
    else if (snapshot_.generation >= std::numeric_limits<uint64_t>::max() - 1 ||
             !expandSettings(request_.settings, pending_))
      save_outcome_ = SaveOutcome::Invalid;
    else {
      const auto result = persistence_.request(pending_, now);
      save_result_ = result;
      saving_ = result.status == PersistStatus::Pending;
      save_outcome_ = saving_ ? SaveOutcome::Accepted : SaveOutcome::Invalid;
      if (result.status == PersistStatus::Refused)
        refuse(result);
    }
  }
  if (snapshot_.effective) {
    const auto result = persistence_.service(now);
    if (saving_)
      save_result_ = result;
    if (result.status == PersistStatus::Refused || result.status == PersistStatus::Future ||
        result.status == PersistStatus::Corrupt || result.status == PersistStatus::Ambiguous ||
        result.status == PersistStatus::ReadError)
      refuse(result);
    else if (saving_ && (result.status == PersistStatus::Durable ||
                         result.status == PersistStatus::Unchanged)) {
      // A session mapping may never rebind an opaque ID to another peer, even
      // after settings change back. Compare identity; never derive IDs from it.
      if (pending_.count != snapshot_.settings.count)
        peers_stable_ = false;
      else
        for (size_t i = 0; i < pending_.count; ++i) {
          const auto &before = snapshot_.settings.cameras[i];
          const auto &after = pending_.cameras[i];
          if (before.model != after.model || before.address_type != after.address_type ||
              after.identifier != before.identifier ||
              after.wake_identifier != before.wake_identifier)
            peers_stable_ = false;
        }
      // Persistence never updates caller RAM. Adopt only this verified candidate.
      ram_ = std::move(pending_);
      copySettings(ram_, snapshot_.settings);
      snapshot_.peers_valid = peersValid();
      snapshot_.outcome = result;
      ++snapshot_.generation;
      saving_ = false;
      save_outcome_ = SaveOutcome::Durable;
      dirty_ = true;
    } else if (saving_ && result.status != PersistStatus::Pending) {
      save_outcome_ = SaveOutcome::PersistenceError;
    }
  }
  publish();
}
} // namespace ridesync

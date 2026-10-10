#pragma once
#include "config_storage.h"
#include <atomic>
#include <cstring>
#include <type_traits>
namespace ridesync {
// Fixed canonical active-slot data. No strings, pointers, SDK handles or heap.
struct CameraSettings {
  char name[65]{};
  char identifier[18]{};
  char wake_identifier[18]{};
  CameraFamily family = CameraFamily::Unknown;
  CameraModel model = CameraModel::Unknown;
  AddressType address_type = AddressType::Unknown;
  bool enabled = false, gps_telemetry = false;
};
struct Settings {
  std::array<CameraSettings, kMaxCameras> cameras{};
  size_t count = 0, capacity = kMaxCameras;
  ButtonConfig button;
  ButtonGpioConfig button_gpio;
};
bool copySettings(const SourceConfig &, Settings &);
// Owner only: bounded allocations in SourceConfig, never in callback/control paths.
bool expandSettings(const Settings &, SourceConfig &);
struct CameraPeer {
  uint32_t id = 0;
  CameraModel model = CameraModel::Unknown;
  size_t slot = 0;
};
struct CameraPeers {
  std::array<CameraPeer, kMaxCameras> entries{};
  size_t count = 0;
};
struct SettingsSnapshot {
  uint32_t epoch = 0;
  uint64_t generation = 0;
  bool completed = false, effective = false, peers_valid = false;
  PersistResult load;
  PersistResult outcome;
  Settings settings;
  CameraPeers peers;
};
struct SettingsSave {
  uint32_t epoch = 0;
  uint64_t generation = 0;
  Settings settings;
};
// Exactly one producer and one consumer, both live for the mailbox lifetime.
// No reset/destruction until both are quiescent. Reject full; never overwrite.
// release is each side's FINAL slot access; acquire transfers exclusive access.
// Payloads must contain no owning pointers. Caller buffers must not alias slot.
static_assert(ATOMIC_INT_LOCK_FREE == 2, "mailbox state must be lock free");
template <typename T> class SettingsMailbox {
public:
  static_assert(std::is_trivially_copyable<T>::value, "mailbox requires copied fixed data");
  SettingsMailbox() = default;
  SettingsMailbox(const SettingsMailbox &) = delete;
  SettingsMailbox &operator=(const SettingsMailbox &) = delete;
  bool put(const T &value) {
    unsigned empty = 0;
    if (!state_.compare_exchange_strong(empty, 1, std::memory_order_acquire))
      return false;
    slot_ = value;
    state_.store(2, std::memory_order_release);
    return true;
  }
  bool take(T &value) {
    unsigned ready = 2;
    if (!state_.compare_exchange_strong(ready, 3, std::memory_order_acquire))
      return false;
    value = slot_;
    state_.store(0, std::memory_order_release);
    return true;
  }

private:
  std::atomic<unsigned> state_{0};
  T slot_{};
};
using SettingsPublication = SettingsMailbox<SettingsSnapshot>;
using SettingsRequests = SettingsMailbox<SettingsSave>;
class ApplicationSettings {
public:
  explicit ApplicationSettings(uint32_t epoch) : epoch_(epoch) {}
  bool accept(const SettingsSnapshot &);
  const SettingsSnapshot &snapshot() const { return snapshot_; }

private:
  uint32_t epoch_;
  SettingsSnapshot snapshot_;
};
struct SettingsQualification {
  bool cameras = false, button = false, local_telemetry = false;
  bool safe_mode = false, nvs_allowed = false;
};
struct SettingsAdmission {
  bool cameras = false, button = false, local_telemetry = false;
};
SettingsAdmission admitSettings(const SettingsSnapshot &, const SettingsQualification &);
enum class SaveOutcome { None, Accepted, Stale, Busy, Invalid, Refused, Durable, PersistenceError };
// Exclusive owner of persistence, RAM strings, pending save and publication.
// Construct before workers; start once; service only from the config owner.
// Peer provisioning is copied once at construction and cannot be rebound.
class ConfigBootstrap {
public:
  ConfigBootstrap(ConfigPersistence &p, SettingsPublication &s, SettingsRequests &r, uint32_t epoch,
                  const CameraPeers &peers = {}, const SourceConfig *defaults = nullptr)
      : persistence_(p), publication_(s), requests_(r), epoch_(epoch), peers_(peers),
        defaults_supplied_(defaults != nullptr),
        defaults_valid_(!defaults || copySettings(*defaults, defaults_)) {}
  ConfigBootstrap(const ConfigBootstrap &) = delete;
  ConfigBootstrap &operator=(const ConfigBootstrap &) = delete;
  void start(bool safe_mode, bool nvs_allowed);
  void unavailable(PersistResult);
  void service(uint32_t now, bool safe_mode, bool nvs_allowed);
  SaveOutcome saveOutcome() const { return save_outcome_; }
  PersistResult saveResult() const { return save_result_; }

private:
  void publish();
  void refuse(PersistResult);
  bool peersValid() const;
  ConfigPersistence &persistence_;
  SettingsPublication &publication_;
  SettingsRequests &requests_;
  const uint32_t epoch_;
  const CameraPeers peers_;
  // Private commissioning defaults are copied before workers. Missing-store RAM
  // defaults only: never repair errors or replace any persisted settings.
  Settings defaults_;
  bool defaults_supplied_ = false, defaults_valid_ = true;
  SourceConfig ram_, pending_;
  SettingsSnapshot snapshot_;
  SettingsSave request_;
  bool started_ = false, dirty_ = false, saving_ = false, peers_stable_ = true;
  SaveOutcome save_outcome_ = SaveOutcome::None;
  PersistResult save_result_;
};
} // namespace ridesync

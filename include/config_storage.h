#pragma once
#include "config.h"
#include <array>
#include <cstdint>
namespace ridesync {
constexpr size_t kConfigRecordMax = 2048;
struct ConfigRecord {
  std::array<uint8_t, kConfigRecordMax> bytes{};
  size_t size = 0;
};
enum class PersistStatus {
  Defaults,
  Loaded,
  Migrated,
  Recovered,
  Pending,
  Durable,
  Unchanged,
  Invalid,
  Corrupt,
  Future,
  Ambiguous,
  ReadError,
  WriteError,
  CommitError,
  VerificationError,
  Indeterminate,
  GenerationLimit,
  Refused,
  Latched,
  Encoded
};
struct PersistResult {
  PersistStatus status;
  int32_t code = 0;
  bool indeterminate = false;
  PersistStatus cause = PersistStatus::Defaults;
  PersistResult(PersistStatus s = PersistStatus::Defaults, int32_t c = 0) : status(s), code(c) {}
};
enum class StoreStatus { Ok, Missing, Error, Oversized, Refused, SetError, CommitError };
struct StoreResult {
  StoreStatus status;
  int32_t code;
  StoreResult(StoreStatus s = StoreStatus::Ok, int32_t c = 0) : status(s), code(c) {}
};
// Each operation checks admission. Single serialized owner; never called from
// control/acquisition ticks or BLE callbacks. No retained SDK handles.
class ConfigStore {
public:
  virtual ~ConfigStore() = default;
  virtual bool allowed() const = 0;
  virtual StoreResult read(unsigned slot, ConfigRecord &) = 0;
  virtual StoreResult write(unsigned slot, const ConfigRecord &) = 0;
};
PersistResult encodeConfig(const SourceConfig &, uint64_t generation, ConfigRecord &);
PersistResult decodeConfig(const ConfigRecord &, SourceConfig &, uint64_t &generation);
class ConfigPersistence {
public:
  explicit ConfigPersistence(ConfigStore &store) : store_(store) {}
  PersistResult load(SourceConfig &);
  PersistResult request(const SourceConfig &, uint32_t now);
  PersistResult reset(uint32_t now);
  PersistResult retry(uint32_t now);
  PersistResult service(uint32_t now);
  bool pending() const { return pending_; }
  PersistResult result() const { return result_; }

private:
  ConfigStore &store_;
  ConfigRecord desired_;
  bool pending_ = false, latched_ = false, attempted_ = false, uncertain_ = false;
  uint32_t requested_ = 0, attempted_at_ = 0;
  unsigned failures_ = 0;
  PersistResult result_;
};
#if defined(ARDUINO_ARCH_ESP32)
class NvsConfigStore : public ConfigStore {
public:
  bool allowed() const override;
  StoreResult read(unsigned, ConfigRecord &) override;
  StoreResult write(unsigned, const ConfigRecord &) override;
};
#endif
} // namespace ridesync

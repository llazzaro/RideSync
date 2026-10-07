#pragma once
#include "telemetry_record.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace ridesync {
// All calls run in ONE worker context. They may block, including mount/close.
// openExclusive must create a new path without truncating or opening an existing
// log, including on allocation/probe errors. close releases owned mount state.
class StorageSink {
public:
  virtual ~StorageSink() = default;
  virtual bool mount() = 0;
  virtual bool openExclusive(const char *path) = 0;
  virtual size_t write(const char *bytes, size_t length) = 0;
  virtual bool flush() = 0;
  virtual void close() = 0;
};
struct StorageConfig {
  uint64_t session_id;
  const char *firmware;
  const char *provenance;
  uint8_t mount_attempts;
  uint8_t flush_records;
  StorageFormat format;
  StorageConfig(uint64_t id, const char *fw, const char *source, uint8_t attempts = 2,
                uint8_t flush_count = 4, StorageFormat selected = StorageFormat::GpsV1)
      : session_id(id), firmware(fw), provenance(source), mount_attempts(attempts),
        flush_records(flush_count), format(selected) {}
};
struct StorageHealth {
  uint32_t accepted, dropped, rejected, written, flushed, lost, progress;
  bool terminal, stopped;
};
// One producer, one worker, no other filesystem users. Object/sink lifetime must
// exceed worker lifetime. No reset/restart: new object + unique session after stop.
// uint32 counters saturate. A health snapshot is observational, not transactional.
class Storage {
public:
  static constexpr uint32_t kCapacity = 8;
  static constexpr size_t kMaxRowBytes = 2048, kChunkBytes = 256;
  Storage(StorageSink &sink, const StorageConfig &config);
  bool enqueue(const RecordTimestamp &timestamp, const ModemSnapshot &sample);
  bool enqueueImu(const RecordTimestamp &timestamp, const ImuEvidence &evidence,
                  bool reserve = false);
  KindHealth kindHealth(RecordKind kind) const;
  void requestStop();
  // ONE operation/chunk per step; sink latency is unbounded. Never call on control task.
  void workerStep();
  StorageHealth health() const;

private:
  friend class TelemetryAdmission;
  void drop(RecordKind kind);
  using Record = TelemetryRecord;
  const StorageFormat format_;
  struct Counters {
    std::atomic<uint32_t> accepted{0}, dropped{0}, rejected{0}, written{0}, flushed{0};
  };
  Counters kinds_[5];
  uint8_t cached_kinds_[5]{};
  RecordKind in_flight_kind_ = RecordKind::Gps;
  bool publish(const Record &record, bool reserve);
  bool validTimestamp(const RecordTimestamp &timestamp) const;
  bool formatImu(const Record &record);
  StorageSink &sink_;
  const uint64_t session_;
  const uint8_t max_mounts_, flush_records_;
  bool valid_;
  char firmware_[49]{}, provenance_[49]{}, path_[48]{};
  Record queue_[kCapacity];
  std::atomic<uint32_t> read_{0}, write_{0};
  std::atomic<uint32_t> accepted_{0}, dropped_{0}, rejected_{0}, written_{0}, flushed_{0}, lost_{0},
      progress_{0};
  std::atomic<bool> terminal_{false}, stop_{false}, stopped_{false};
  enum class Stage { Mount, Open, Header, Rows, Flush, Close, Done };
  Stage stage_ = Stage::Mount;
  uint8_t mounts_ = 0, cached_ = 0;
  bool row_in_flight_ = false;
  char buffer_[kMaxRowBytes]{};
  size_t length_ = 0, offset_ = 0;
  bool format(const Record &record);
  void fail();
  void writeChunk();
  static void add(std::atomic<uint32_t> &counter, uint32_t amount = 1);
};
} // namespace ridesync

#include "pairing_proof_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "ble_esp32.h"
#include "nvs_boot_guard.h"
#include <algorithm>
#include <cstring>
#include <esp_timer.h>
#include <nvs.h>
namespace ridesync {
namespace {
constexpr const char *kNamespace = "ridesync_maint", *kKey = "ble_proof";
constexpr int kInvalid = -31001, kRevoked = -31002;
using Marker = std::array<uint8_t, 16>;
uint32_t crc(const uint8_t *bytes, size_t length) {
  uint32_t value = UINT32_MAX;
  for (size_t i = 0; i < length; ++i) {
    value ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      value = (value >> 1) ^ ((0U - (value & 1)) & 0xedb88320UL);
  }
  return ~value;
}
uint32_t get32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void put32(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = uint8_t(v >> (i * 8));
}
Marker marker(uint32_t floor) {
  Marker out{{'R', 'B', 'P', 'R', 1, 0, 0, 0}};
  put32(out.data() + 8, floor);
  put32(out.data() + 12, crc(out.data(), 12));
  return out;
}
bool decode(const Marker &bytes, uint32_t &floor) {
  if (bytes[0] != 'R' || bytes[1] != 'B' || bytes[2] != 'P' || bytes[3] != 'R' || bytes[4] != 1 ||
      bytes[5] || bytes[6] || bytes[7] || get32(bytes.data() + 12) != crc(bytes.data(), 12))
    return false;
  floor = get32(bytes.data() + 8);
  return floor != 0;
}
struct Handle {
  nvs_handle_t value = 0;
  bool opened = false;
  ~Handle() {
    if (opened && nvsBootStatus().persistenceAllowed())
      nvs_close(value);
  }
};
int readFloor(uint32_t &floor) {
  floor = 0;
  if (!nvsBootStatus().persistenceAllowed())
    return kRevoked;
  Handle h;
  auto rc = nvs_open(kNamespace, NVS_READONLY, &h.value);
  if (rc == ESP_ERR_NVS_NOT_FOUND)
    return 0; // Genuine legacy absence; never Pending/uninitialized.
  if (rc != ESP_OK)
    return rc;
  h.opened = true;
  if (!nvsBootStatus().persistenceAllowed())
    return kRevoked;
  Marker bytes{};
  size_t length = bytes.size();
  rc = nvs_get_blob(h.value, kKey, bytes.data(), &length);
  if (rc == ESP_ERR_NVS_NOT_FOUND)
    return 0;
  if (rc != ESP_OK)
    return rc;
  return length == bytes.size() && decode(bytes, floor) ? 0 : kInvalid;
}
bool equalProof(const BleStoreProof &a, const BleStoreProof &b) {
  return a.qualification_record == b.qualification_record && a.digest == b.digest &&
         a.counts == b.counts;
}
} // namespace
PairingProofMaintenance &pairingProofMaintenance() {
  static PairingProofMaintenance owner;
  return owner;
}
void PairingProofMaintenance::beginOwner() {
  if (initialized_)
    return;
  initialized_ = true;
  uint32_t floor = 0;
  const auto error = readFloor(floor);
  floor_.store(floor);
  boot_.store(error ? Boot::Error : Boot::Ready, std::memory_order_release);
}
bool PairingProofMaintenance::restorationAllowed(uint32_t record) const {
  return boot_.load(std::memory_order_acquire) == Boot::Ready && record && record > floor_.load();
}
BondResetSubmission PairingProofMaintenance::request(uint32_t operation,
                                                     const BleStoreProof &admitted,
                                                     uint32_t deadline, uint32_t now) {
  if (phase_.load(std::memory_order_acquire))
    return BondResetSubmission::Busy;
  if (!operation || operation <= last_operation_)
    return BondResetSubmission::Stale;
  BleStoreProof actual;
  if (boot_.load(std::memory_order_acquire) != Boot::Ready ||
      !Esp32BleHost::instance().admittedProof(actual) || !equalProof(actual, admitted) ||
      actual.qualification_record == UINT32_MAX || now - deadline < 0x80000000UL ||
      deadline - now > 5000 || !nvsBootStatus().persistenceAllowed())
    return BondResetSubmission::Refused;
  operation_ = last_operation_ = operation;
  proof_ = actual; // EXACT authoritative host-admitted proof, never caller higher evidence.
  deadline_ = deadline;
  result_ = {};
  cancelled_.store(false);
  phase_.store(1, std::memory_order_release);
  return BondResetSubmission::Queued;
}
bool PairingProofMaintenance::cancel(uint32_t operation) {
  if (!phase_.load(std::memory_order_acquire) || operation_ != operation)
    return false;
  cancelled_.store(true, std::memory_order_release);
  return true;
}
ProofRevocationResult PairingProofMaintenance::result(uint32_t operation) const {
  ProofRevocationResult out;
  if (!phase_.load(std::memory_order_acquire) || operation != operation_)
    return out;
  out.operation = operation;
  if (phase_.load(std::memory_order_acquire) == 3)
    return result_; // Only after worker FINAL access; blocked work retains state.
  return out;
}
bool PairingProofMaintenance::release(uint32_t operation) {
  if (operation != operation_ || phase_.load(std::memory_order_acquire) != 3)
    return false;
  phase_.store(0, std::memory_order_release);
  return true;
}
bool PairingProofMaintenance::active() const {
  return !cancelled_.load(std::memory_order_acquire) && nvsBootStatus().persistenceAllowed() &&
         uint32_t(esp_timer_get_time() / 1000) - deadline_ >= 0x80000000UL;
}
void PairingProofMaintenance::perform() {
  if (!active()) {
    result_.error = kRevoked;
    return;
  }
  uint32_t previous = 0;
  result_.error = readFloor(previous);
  if (result_.error || previous < floor_.load() || !active()) {
    if (!result_.error)
      result_.error = kRevoked;
    boot_.store(Boot::Error, std::memory_order_release);
    return;
  }
  const uint32_t next = std::max(previous, proof_.qualification_record);
  if (next == previous) {
    floor_.store(previous);
    result_.durable = true; // Prior successful boot/transaction, not failed-write laundering.
    return;
  }
  Handle h;
  auto rc = nvs_open(kNamespace, NVS_READWRITE, &h.value);
  if (rc != ESP_OK) {
    result_.error = rc;
    return;
  }
  h.opened = true;
  const auto bytes = marker(next);
  if (!active()) {
    result_.error = kRevoked;
    return;
  }
  result_.write_attempted = true; // set_blob itself may persist, even before commit.
  rc = nvs_set_blob(h.value, kKey, bytes.data(), bytes.size());
  if (rc == ESP_OK && active())
    rc = nvs_commit(h.value);
  else if (rc == ESP_OK)
    rc = kRevoked;
  if (rc == ESP_OK && active()) {
    Marker observed{};
    size_t size = observed.size();
    rc = nvs_get_blob(h.value, kKey, observed.data(), &size);
    if (rc == ESP_OK && (size != observed.size() || observed != bytes))
      rc = kInvalid;
  } else if (rc == ESP_OK) {
    rc = kRevoked;
  }
  result_.error = rc;
  if (rc) {
    // Never reuse matching uncertain/uncommitted bytes as a Durable ACK later
    // in this boot. Independent next-boot reading may conservatively deny proof.
    boot_.store(Boot::Error, std::memory_order_release);
    return;
  }
  floor_.store(next);
  result_.durable = true;
}
void PairingProofMaintenance::service() {
  unsigned queued = 1;
  if (!phase_.compare_exchange_strong(queued, 2, std::memory_order_acq_rel))
    return;
  perform(); // Handle close completes before any copied successful ACK.
  result_.operation = operation_;
  result_.cancelled = cancelled_.load(std::memory_order_acquire);
  result_.timed_out = uint32_t(esp_timer_get_time() / 1000) - deadline_ < 0x80000000UL;
  result_.finished = result_.releasable = true;
  phase_.store(3, std::memory_order_release); // FINAL worker access to request/result storage.
}
} // namespace ridesync
#endif

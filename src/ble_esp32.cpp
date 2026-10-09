#include "ble_esp32.h"
#include "pairing_proof_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "nvs_boot_guard.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <esp_bt.h>
#include <esp_timer.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#if CONFIG_BT_LE_CONTROLLER_NPL_OS_PORTING_SUPPORT
#error "RideSync bounded barrier requires the pinned exposed FreeRTOS NPL queue ABI"
#endif
#include <mbedtls/sha256.h>
#include <nimble/esp_port/esp-hci/include/esp_nimble_hci.h>
#include <nimble/nimble/host/include/host/ble_hs.h>
#include <nimble/nimble/host/include/host/ble_store.h>
#include <nimble/nimble/host/src/ble_hs_resolv_priv.h>
#include <nimble/nimble/host/store/config/include/store/config/ble_store_config.h>
#include <nimble/nimble/host/store/config/src/ble_store_config_priv.h>
#include <nimble/porting/nimble/include/nimble/nimble_port.h>
#include <nimble/porting/nimble/include/os/os_mbuf.h>
#include <nvs.h>

// Reset inventory and deletion are proved only for this exact pinned schema.
static_assert(MYNEWT_VAL(BLE_HOST_BASED_PRIVACY) == 1 && MYNEWT_VAL(ENC_ADV_DATA) == 0 &&
                  MYNEWT_VAL(BLE_SMP_ID_RESET) == 0 && MYNEWT_VAL(BLE_STORE_MAX_BONDS) == 5 &&
                  MYNEWT_VAL(BLE_STORE_MAX_CCCDS) == 32,
              "Requalify targeted reset before changing the pinned store/privacy schema");

static_assert(sizeof(ble_hs_peer_sec) == 24 && sizeof(ble_hs_dev_records) == 44 &&
                  offsetof(ble_hs_dev_records, peer_sec) == 20,
              "Requalify the pinned private peer-record ABI");

extern "C" void ble_store_config_init(void);
extern "C" int ridesync_ble_resolv_read(unsigned, ble_hs_resolv_entry *);
namespace ridesync {
namespace {
constexpr unsigned kStoreKeys = 80;
struct CallbackAccess {
  std::atomic<unsigned> &count;
  explicit CallbackAccess(std::atomic<unsigned> &counter) : count(counter) {
    count.fetch_add(1, std::memory_order_acq_rel);
  }
  ~CallbackAccess() { count.fetch_sub(1, std::memory_order_release); }
};
const char *prefixes[] = {"our_sec_",   "peer_sec_", "cccd_sec_", "csfc_sec_",
                          "local_irk_", "rpa_rec_",  "p_dev_rec_"};
const int types[] = {BLE_STORE_OBJ_TYPE_OUR_SEC,     BLE_STORE_OBJ_TYPE_PEER_SEC,
                     BLE_STORE_OBJ_TYPE_CCCD,        BLE_STORE_OBJ_TYPE_CSFC,
                     BLE_STORE_OBJ_TYPE_LOCAL_IRK,   BLE_STORE_OBJ_TYPE_PEER_ADDR,
                     BLE_STORE_OBJ_TYPE_PEER_DEV_REC};
const size_t sizes[] = {sizeof(ble_store_value_sec),       sizeof(ble_store_value_sec),
                        sizeof(ble_store_value_cccd),      sizeof(ble_store_value_csfc),
                        sizeof(ble_store_value_local_irk), sizeof(ble_store_value_rpa_rec),
                        sizeof(ble_hs_dev_records)};
BleUuid uuid(const ble_uuid_any_t &source) {
  BleUuid out;
  out.size = source.u.type == BLE_UUID_TYPE_16 ? 2 : source.u.type == BLE_UUID_TYPE_128 ? 16 : 0;
  if (out.size == 2) {
    out.bytes[0] = source.u16.value & 0xff;
    out.bytes[1] = source.u16.value >> 8;
  } else if (out.size == 16) {
    std::memcpy(out.bytes.data(), source.u128.value, 16);
  }
  return out;
}
ble_uuid_any_t uuid(const BleUuid &source) {
  ble_uuid_any_t out{};
  if (source.size == 2) {
    out.u16.u.type = BLE_UUID_TYPE_16;
    out.u16.value = source.bytes[0] | uint16_t(source.bytes[1]) << 8;
  } else {
    out.u128.u.type = BLE_UUID_TYPE_128;
    std::memcpy(out.u128.value, source.bytes.data(), 16);
  }
  return out;
}
ble_addr_t address(const BondIdentity &source) {
  ble_addr_t out{};
  out.type = source.type == IdentityType::Public ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM;
  std::memcpy(out.val, source.address.data(), 6);
  return out;
}
BondIdentity identity(const ble_addr_t &source) {
  BondIdentity out;
  out.type = source.type == BLE_ADDR_PUBLIC ? IdentityType::Public
             : source.type == BLE_ADDR_RANDOM && (source.val[5] & 0xc0) == 0xc0
                 ? IdentityType::RandomStatic
                 : IdentityType::UnresolvedPrivate;
  std::memcpy(out.address.data(), source.val, 6);
  // Caller supplies the provenance; observations never claim verification alone.
  return out;
}
bool copyValue(os_mbuf *mbuf, BleEvent &out) {
  if (!mbuf)
    return false;
  const auto length = os_mbuf_len(mbuf);
  if (length > kBlePayload)
    return false;
  out.size = length;
  return os_mbuf_copydata(mbuf, 0, length, out.bytes.data()) == 0;
}
int guardedRead(int type, const ble_store_key *key, ble_store_value *value) {
  auto &host = Esp32BleHost::instance();
  if (!nvsBootStatus().persistenceAllowed()) {
    host.onStoreStatus(nullptr, nullptr);
    return BLE_HS_ESTORE_FAIL;
  }
  if (!host.wakeStoreAllowed(type, key, nullptr))
    return BLE_HS_ESTORE_FAIL;
  const int rc = ble_store_config_read(type, key, value);
  if (!rc && !host.wakeStoreAllowed(type, nullptr, value)) {
    std::memset(value, 0, sizeof *value);
    return BLE_HS_ESTORE_FAIL;
  }
  return rc;
}
int guardedWrite(int type, const ble_store_value *value) {
  auto &host = Esp32BleHost::instance();
  if (!nvsBootStatus().persistenceAllowed()) {
    host.onStoreStatus(nullptr, nullptr);
    return BLE_HS_ESTORE_FAIL;
  }
  if (!host.wakeStoreAllowed(type, nullptr, value))
    return BLE_HS_ESTORE_FAIL;
  const int rc = ble_store_config_write(type, value);
  if (rc && rc != BLE_HS_ESTORE_CAP)
    host.onStoreStatus(nullptr, nullptr);
  return rc;
}
int guardedDelete(int type, const ble_store_key *key) {
  if (!nvsBootStatus().persistenceAllowed()) {
    Esp32BleHost::instance().onStoreStatus(nullptr, nullptr);
    return BLE_HS_ESTORE_FAIL;
  }
  if (!Esp32BleHost::instance().wakeStoreAllowed(type, key, nullptr))
    return BLE_HS_ESTORE_FAIL;
  return ble_store_config_delete(type, key);
}
bool stackMatches(unsigned schema, const void *blob) {
  ble_store_key key{};
  ble_store_value value{};
  const void *result = nullptr;
  int rc = BLE_HS_ENOTSUP;
  switch (schema) {
  case 0:
  case 1: {
    const auto &sec = *static_cast<const ble_store_value_sec *>(blob);
    key.sec.peer_addr = sec.peer_addr;
    rc = schema == 0 ? ble_store_read_our_sec(&key.sec, &value.sec)
                     : ble_store_read_peer_sec(&key.sec, &value.sec);
    result = &value.sec;
    break;
  }
  case 2: {
    const auto &cccd = *static_cast<const ble_store_value_cccd *>(blob);
    key.cccd.peer_addr = cccd.peer_addr;
    key.cccd.chr_val_handle = cccd.chr_val_handle;
    rc = ble_store_read_cccd(&key.cccd, &value.cccd);
    result = &value.cccd;
    break;
  }
  case 3:
    key.csfc.peer_addr = static_cast<const ble_store_value_csfc *>(blob)->peer_addr;
    rc = ble_store_read_csfc(&key.csfc, &value.csfc);
    result = &value.csfc;
    break;
  case 4:
    key.local_irk.addr = static_cast<const ble_store_value_local_irk *>(blob)->addr;
    rc = ble_store_read_local_irk(&key.local_irk, &value.local_irk);
    result = &value.local_irk;
    break;
  case 5:
    key.rpa_rec.peer_rpa_addr = static_cast<const ble_store_value_rpa_rec *>(blob)->peer_rpa_addr;
    rc = ble_store_read_rpa_rec(&key.rpa_rec, &value.rpa_rec);
    result = &value.rpa_rec;
    break;
  case 6: {
    const auto &record = *static_cast<const ble_hs_dev_records *>(blob);
    const auto *records = ble_rpa_get_peer_dev_records();
    const int count = ble_rpa_get_num_peer_dev_records();
    if (count < 0 || count > CONFIG_BT_NIMBLE_MAX_BONDS + 1)
      return false;
    for (int i = 0; i < count; ++i)
      if (std::memcmp(&records[i], &record, sizeof record) == 0)
        return true;
    return false;
  }
  }
  return rc == 0 && result && std::memcmp(blob, result, sizes[schema]) == 0;
}
} // namespace
Esp32BleHost &Esp32BleHost::instance() {
  static Esp32BleHost host;
  return host;
}
void Esp32BleHost::fail(BleFault f, int error) {
  error_.store(error);
  fault_.store(f);
  state_.store(BleHostState::Failed);
}
bool Esp32BleHost::configureRestore(const BleStoreProof &proof) {
  if (leased_ || proof.qualification_record == 0)
    return false;
  expected_ = proof;
  return true;
}
bool Esp32BleHost::admittedProof(BleStoreProof &out) const {
  if (state() != BleHostState::Ready || fault() != BleFault::None || !restore_verified_ ||
      !refusal_installed_ || !nvsBootStatus().persistenceAllowed())
    return false;
  out = expected_;
  return true;
}
BleStoreObservation Esp32BleHost::inspectStore(bool compare_stack) {
  BleStoreObservation out;
  if (host_started_ || !nvsBootStatus().persistenceAllowed()) {
    out.error = BLE_HS_ESTORE_FAIL;
    return out;
  }
  nvs_handle_t handle = 0;
  const auto rc = nvs_open("nimble_bond", NVS_READONLY, &handle);
  if (rc != ESP_OK && rc != ESP_ERR_NVS_NOT_FOUND) {
    out.error = rc;
    return out;
  }
  std::array<std::array<char, 16>, kStoreKeys> names{};
  unsigned count = 0;
  bool valid = true;
  if (rc == ESP_OK) {
    auto iterator = nvs_entry_find("nvs", "nimble_bond", NVS_TYPE_ANY);
    while (iterator) {
      nvs_entry_info_t info{};
      nvs_entry_info(iterator, &info);
      if (count == names.size() || info.type != NVS_TYPE_BLOB) {
        valid = false;
        break;
      }
      std::memcpy(names[count++].data(), info.key, 16);
      iterator = nvs_entry_next(iterator);
    }
    if (iterator)
      nvs_release_iterator(iterator);
  }
  std::sort(names.begin(), names.begin() + count,
            [](const std::array<char, 16> &a, const std::array<char, 16> &b) {
              return std::strncmp(a.data(), b.data(), 16) < 0;
            });
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  valid &= mbedtls_sha256_starts_ret(&hash, 0) == 0;
  // Version binds names/bytes/ABI to this exact source. Empty is SHA256(version).
  const uint8_t version[] = {1, 2, 3, 6};
  valid &= mbedtls_sha256_update_ret(&hash, version, sizeof version) == 0;
  for (unsigned n = 0; n < count && valid; ++n) {
    const auto *name = names[n].data();
    unsigned schema = 7;
    unsigned index = 0;
    for (unsigned t = 0; t < 7; ++t) {
      const auto prefix_size = std::strlen(prefixes[t]);
      if (std::strncmp(name, prefixes[t], prefix_size) == 0) {
        const char *digit = name + prefix_size;
        if (*digit < '1' || *digit > '9')
          break;
        bool digits = true;
        for (; *digit; ++digit) {
          if (*digit < '0' || *digit > '9') {
            digits = false;
            break;
          }
          index = index * 10 + *digit - '0';
          if (index >= kStoreKeys) {
            digits = false;
            break;
          }
        }
        if (digits)
          schema = t;
        break;
      }
    }
    if (schema == 7 || !index ||
        index > (schema == 2 ? CONFIG_BT_NIMBLE_MAX_CCCDS
                             : CONFIG_BT_NIMBLE_MAX_BONDS + (schema == 6 ? 1 : 0))) {
      valid = false;
      break;
    }
    union Scratch {
      ble_store_value value;
      ble_hs_dev_records record;
      uint8_t bytes[512];
    } scratch{};
    auto &bytes = scratch.bytes;
    static_assert(sizeof(ble_store_value) <= 512, "Store scratch bound");
    size_t length = sizeof bytes;
    if (!nvsBootStatus().persistenceAllowed() ||
        nvs_get_blob(handle, name, bytes, &length) != ESP_OK || length != sizes[schema]) {
      valid = false;
      break;
    }
    if (compare_stack && !stackMatches(schema, bytes)) {
      valid = false;
      break;
    }
    ++out.snapshot.counts[schema];
    const uint8_t name_length = std::strlen(name);
    const uint8_t blob_length[] = {uint8_t(length), uint8_t(length >> 8)};
    valid &= mbedtls_sha256_update_ret(&hash, &name_length, 1) == 0;
    valid &=
        mbedtls_sha256_update_ret(&hash, reinterpret_cast<const uint8_t *>(name), name_length) == 0;
    valid &= mbedtls_sha256_update_ret(&hash, blob_length, 2) == 0;
    valid &= mbedtls_sha256_update_ret(&hash, bytes, length) == 0;
  }
  if (compare_stack && valid) {
    for (unsigned schema = 0; schema < 6; ++schema) {
      int actual_count = -1;
      const int result = schema == 5 ? (actual_count = ble_store_config_num_rpa_recs, 0)
                                     : ble_store_util_count(types[schema], &actual_count);
      if (result != 0 || actual_count < 0 ||
          unsigned(actual_count) != out.snapshot.counts[schema]) {
        valid = false;
        break;
      }
    }
    valid &= unsigned(ble_rpa_get_num_peer_dev_records()) == out.snapshot.counts[6];
  }
  valid &= mbedtls_sha256_finish_ret(&hash, out.snapshot.digest.data()) == 0;
  mbedtls_sha256_free(&hash);
  if (rc == ESP_OK)
    nvs_close(handle);
  out.complete = valid && nvsBootStatus().persistenceAllowed();
  out.error = out.complete ? 0 : BLE_HS_ESTORE_FAIL;
  return out;
}
BleHostState Esp32BleHost::start(bool enabled, bool qualified) {
  if (!enabled || !qualified)
    return BleHostState::Disabled;
  if (leased_)
    return state();
  leased_ = true; // Irreversible boot lease even on partial init failure.
  if (!nvsBootStatus().persistenceAllowed() || NimBLEDevice::isInitialized() ||
      esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE ||
      !pairingProofMaintenance().restorationAllowed(expected_.qualification_record)) {
    fail(BleFault::Admission, BLE_HS_EDISABLED);
    return state();
  }
  const auto before = inspectStore(false);
  if (!before.complete || before.snapshot.digest != expected_.digest ||
      before.snapshot.counts != expected_.counts) {
    fail(BleFault::Store, before.error ? before.error : BLE_HS_ESTORE_FAIL);
    return state();
  }
  state_.store(BleHostState::Starting);
  esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  config.mode = ESP_BT_MODE_BLE;
  config.ble_max_conn = CONFIG_BT_NIMBLE_MAX_CONNECTIONS;
  int rc = esp_bt_controller_init(&config);
  if (rc == ESP_OK)
    rc = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (rc == ESP_OK)
    rc = esp_nimble_hci_init();
  if (rc == ESP_OK)
    rc = nimble_port_init();
  if (rc != ESP_OK) {
    fail(BleFault::Host, rc);
    return state();
  }
  // The wrapper's indefinite init sync loop is intentionally never entered.
  NimBLEDevice::setDeviceCallbacks(this);
  ble_hs_cfg.reset_cb = [](int reason) { instance().fail(BleFault::Host, reason); };
  ble_hs_cfg.sync_cb = [] {
    auto &host = instance();
    if (host.state_.load() != BleHostState::Starting)
      return;
    const int result = ble_hs_id_infer_auto(0, &host.own_address_type_);
    if (result)
      host.fail(BleFault::Host, result);
    else
      host.state_.store(BleHostState::Ready);
  };
  ble_hs_cfg.store_status_cb = [](ble_store_status_event *event, void *arg) {
    return instance().onStoreStatus(event, arg);
  };
  refusal_installed_ = true;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0; // Explicit Just Works, never claim authenticated MITM.
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  if (!nvsBootStatus().persistenceAllowed()) {
    fail(BleFault::Store, BLE_HS_ESTORE_FAIL);
    return state();
  }
  // Default stack-owned store, with exact-pin zero-count compatibility patch.
  ble_store_config_init();
  ble_hs_cfg.store_read_cb = guardedRead;
  ble_hs_cfg.store_write_cb = guardedWrite;
  ble_hs_cfg.store_delete_cb = guardedDelete;
  const auto after = inspectStore(true);
  restore_verified_ = after.complete && after.snapshot.digest == expected_.digest &&
                      after.snapshot.counts == expected_.counts;
  if (!restore_verified_) {
    fail(BleFault::Store, after.error ? after.error : BLE_HS_ESTORE_FAIL);
    return state();
  }
  TaskHandle_t task = nullptr;
  host_started_ = true;
  if (xTaskCreatePinnedToCore(hostTask, "ridesync_nimble", 4096, nullptr, 5, &task, 0) != pdPASS) {
    fail(BleFault::Host, BLE_HS_ENOMEM);
    return state();
  }
  return state();
}
void Esp32BleHost::hostTask(void *) {
  nimble_port_run();
  // There is no runtime host stop/delete. A return is an unrecoverable fault;
  // boot-lifetime owner/contexts remain allocated. Never spawn replacements.
  instance().fail(BleFault::Host, BLE_HS_EDISABLED);
  vTaskSuspend(nullptr);
}
void Esp32BleHost::sealStartup() { fail(BleFault::Timeout, BLE_HS_ETIMEOUT); }
BleHostState Esp32BleHost::state() const { return state_.load(); }
BleFault Esp32BleHost::fault() const {
  return nvsBootStatus().persistenceAllowed() ? fault_.load() : BleFault::Store;
}
int Esp32BleHost::onStoreStatus(ble_store_status_event *event, void *) {
  // Refuse FULL/OVERFLOW without deleting another peer. Where the SDK supplies
  // a peer, seal only that peer; unknown store/NVS corruption seals the host.
  if (event &&
      ((event->event_code == BLE_STORE_EVENT_FULL && wakeOwnsConnection(event->full.conn_handle)) ||
       (event->event_code == BLE_STORE_EVENT_OVERFLOW &&
        !wakeStoreAllowed(event->overflow.obj_type, nullptr, event->overflow.value))))
    return BLE_HS_ESTORE_CAP;
  error_.store(BLE_HS_ESTORE_CAP);
  if (routing_lock_.test_and_set(std::memory_order_acquire)) {
    fail(BleFault::Store, BLE_HS_ESTORE_CAP);
    return BLE_HS_ESTORE_CAP;
  }
  bool routed = false;
  for (size_t i = 0; event && i < kBlePeers; ++i) {
    auto &entry = slots_[i];
    if (!entry.context)
      continue;
    bool matches = event->event_code == BLE_STORE_EVENT_FULL &&
                   event->full.conn_handle == entry.context->connection.load();
    if (event->event_code == BLE_STORE_EVENT_OVERFLOW && event->overflow.value &&
        (event->overflow.obj_type == BLE_STORE_OBJ_TYPE_OUR_SEC ||
         event->overflow.obj_type == BLE_STORE_OBJ_TYPE_PEER_SEC)) {
      const auto peer = address(entry.identity);
      const auto &stored = event->overflow.value->sec.peer_addr;
      matches = peer.type == stored.type && std::memcmp(peer.val, stored.val, 6) == 0;
    }
    if (matches) {
      entry.context->sealed.store(true);
      entry.context->terminal_status.store(BLE_HS_ESTORE_CAP);
      entry.context->fault.store(BleFault::Store);
      routed = true;
    }
  }
  routing_lock_.clear(std::memory_order_release);
  if (!routed)
    fail(BleFault::Store, BLE_HS_ESTORE_CAP);
  return BLE_HS_ESTORE_CAP;
}
namespace {
bool resetExpired(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}
bool sameAddress(const ble_addr_t &a, const ble_addr_t &b) {
  return a.type == b.type && std::memcmp(a.val, b.val, 6) == 0;
}
bool validResetIdentity(const BondIdentity &id) {
  bool nonzero = false;
  for (auto b : id.address)
    nonzero |= b != 0;
  return id.verified && nonzero &&
         (id.type == IdentityType::Public ||
          (id.type == IdentityType::RandomStatic && (id.address[5] & 0xc0) == 0xc0));
}
} // namespace
namespace {
class RoutingLease {
public:
  explicit RoutingLease(std::atomic_flag &flag)
      : flag_(flag), held_(!flag.test_and_set(std::memory_order_acquire)) {}
  ~RoutingLease() {
    if (held_)
      flag_.clear(std::memory_order_release);
  }
  bool held() const { return held_; }

private:
  std::atomic_flag &flag_;
  bool held_;
};
} // namespace

BondResetSubmission Esp32BleHost::requestBondReset(const BondIdentity &id, uint32_t operation,
                                                   uint32_t deadline, uint32_t now) {
  RoutingLease admission(routing_lock_);
  if (!admission.held() || wakeReserved())
    return BondResetSubmission::Busy;
  if (reset_.phase.load(std::memory_order_acquire) == 1 || reset_.phase.load() == 2)
    return BondResetSubmission::Busy;
  if (!operation || operation <= reset_.last_operation)
    return BondResetSubmission::Stale;
  if (!validResetIdentity(id) || resetExpired(now, deadline) || deadline - now > 5000 ||
      state() != BleHostState::Ready || fault() != BleFault::None || !restore_verified_ ||
      !refusal_installed_ || !nvsBootStatus().persistenceAllowed())
    return BondResetSubmission::Refused;
  reset_.last_operation = reset_.operation = operation;
  reset_.identity = id;
  reset_.deadline = deadline;
  reset_.cancelled.store(false);
  reset_.timed_out.store(false);
  reset_.mutation.store(false);
  reset_.result = {};
  reset_.ambiguous = false;
  reset_.result.operation = operation;
  if (!reset_.initialized) {
    ble_npl_event_init(&reset_.event, resetEvent, this);
    reset_.initialized = true;
  }
  reset_.phase.store(1, std::memory_order_release);
  auto *event = &reset_.event;
  event->queued = true;
  if (xQueueSendToBack(nimble_port_get_dflt_eventq()->q, &event, 0) != pdPASS) {
    event->queued = false;
    reset_.result.finished = reset_.result.releasable = true;
    reset_.result.outcome = BondOutcome::Busy;
    reset_.result.error = BLE_HS_ENOMEM;
    reset_.phase.store(3, std::memory_order_release);
    return BondResetSubmission::Busy;
  }
  return BondResetSubmission::Queued;
}
bool Esp32BleHost::cancelBondReset(uint32_t operation) {
  if (!operation || operation != reset_.operation ||
      reset_.phase.load(std::memory_order_acquire) == 3)
    return false;
  reset_.cancelled.store(true, std::memory_order_release);
  return true;
}
BleBondResetResult Esp32BleHost::bondResetResult(uint32_t operation, uint32_t now) {
  BleBondResetResult out;
  out.operation = operation;
  if (!operation || operation != reset_.operation) {
    out.outcome = BondOutcome::Refused;
    out.finished = true;
    return out;
  }
  if (reset_.phase.load(std::memory_order_acquire) == 3)
    return reset_.result;
  if (resetExpired(now, reset_.deadline)) {
    reset_.timed_out.store(true, std::memory_order_release);
    reset_.cancelled.store(true, std::memory_order_release);
  }
  out.cancelled = reset_.cancelled.load(std::memory_order_acquire);
  out.timed_out = reset_.timed_out.load(std::memory_order_acquire);
  out.mutation = reset_.mutation.load(std::memory_order_acquire);
  out.requalification_required = out.mutation;
  out.finished = out.cancelled;
  out.outcome = out.cancelled ? (out.mutation ? BondOutcome::Indeterminate : BondOutcome::Refused)
                              : BondOutcome::Busy;
  return out;
}
bool Esp32BleHost::resetAllowed() {
  if (resetExpired(static_cast<uint32_t>(esp_timer_get_time() / 1000), reset_.deadline)) {
    reset_.timed_out.store(true, std::memory_order_release);
    reset_.cancelled.store(true, std::memory_order_release);
  }
  return !reset_.cancelled.load(std::memory_order_acquire) && state() == BleHostState::Ready &&
         fault() == BleFault::None && restore_verified_ && refusal_installed_ &&
         nvsBootStatus().persistenceAllowed();
}
void Esp32BleHost::resetEvent(ble_npl_event *event) {
  auto &host = *static_cast<Esp32BleHost *>(ble_npl_event_get_arg(event));
  host.reset_.phase.store(2, std::memory_order_release);
  host.reset_gate_.store(true, std::memory_order_release);
  host.performBondReset();
  host.reset_.result.mutation = host.reset_.mutation.load();
  host.reset_.result.requalification_required = host.reset_.result.mutation;
  host.reset_.result.cancelled = host.reset_.cancelled.load();
  host.reset_.result.timed_out = host.reset_.timed_out.load();
  host.reset_.result.finished = host.reset_.result.releasable = true;
  host.reset_gate_.store(false, std::memory_order_release);
  // FINAL host access. The pinned NPL has already cleared event->queued before
  // invocation. A late/blocked event retains this singleton state until here.
  host.reset_.phase.store(3, std::memory_order_release);
}
int Esp32BleHost::resetClassify(unsigned schema, const void *blob) const {
  const auto target = address(reset_.identity);
  const ble_addr_t *peer = nullptr;
  if (schema < 2)
    peer = &static_cast<const ble_store_value_sec *>(blob)->peer_addr;
  else if (schema == 2)
    peer = &static_cast<const ble_store_value_cccd *>(blob)->peer_addr;
  else if (schema == 3)
    peer = &static_cast<const ble_store_value_csfc *>(blob)->peer_addr;
  else if (schema == 4)
    return 0; // Local/global IRK is never one camera's credential.
  else if (schema == 5) {
    const auto &rpa = *static_cast<const ble_store_value_rpa_rec *>(blob);
    if (sameAddress(rpa.peer_addr, target))
      return 1;
    return sameAddress(rpa.peer_rpa_addr, target) ? -1 : 0;
  } else if (schema == 6) {
    const auto &record = *static_cast<const ble_hs_dev_records *>(blob);
    const bool bytes_match = !std::memcmp(record.identity_addr, target.val, 6) ||
                             !std::memcmp(record.rand_addr, target.val, 6) ||
                             !std::memcmp(record.pseudo_addr, target.val, 6);
    if (record.rec_used && sameAddress(record.peer_sec.peer_addr, target) &&
        !std::memcmp(record.identity_addr, target.val, 6))
      return 1;
    // The SDK's private peer-record lookup is untyped. Never authorize it from
    // raw aliases when independently typed ownership cannot be established.
    return bytes_match || sameAddress(record.peer_sec.peer_addr, target) ? -1 : 0;
  }
  return peer && sameAddress(*peer, target) ? 1 : 0;
}
bool Esp32BleHost::resetInventory(std::array<uint8_t, 32> &foreign, unsigned &target_records) {
  target_records = 0;
  std::array<unsigned, 7> counts{};
  nvs_handle_t handle = 0;
  if (!resetAllowed())
    return false;
  const auto opened = nvs_open("nimble_bond", NVS_READONLY, &handle);
  if (opened != ESP_OK && opened != ESP_ERR_NVS_NOT_FOUND)
    return false;
  unsigned count = 0;
  bool valid = true;
  if (opened == ESP_OK) {
    auto iterator = nvs_entry_find("nvs", "nimble_bond", NVS_TYPE_ANY);
    while (iterator) {
      nvs_entry_info_t info{};
      nvs_entry_info(iterator, &info);
      if (count == reset_.names.size() || info.type != NVS_TYPE_BLOB ||
          !std::memchr(info.key, 0, sizeof info.key)) {
        valid = false;
        break;
      }
      std::memcpy(reset_.names[count++].data(), info.key, 16);
      iterator = nvs_entry_next(iterator);
    }
    if (iterator)
      nvs_release_iterator(iterator);
  }
  std::sort(reset_.names.begin(), reset_.names.begin() + count,
            [](const std::array<char, 16> &a, const std::array<char, 16> &b) {
              return std::strncmp(a.data(), b.data(), 16) < 0;
            });
  unsigned foreign_count = 0;
  // The pinned NVS selector uses byte membership, not multiplicity. A deleted
  // target value must become absent from RAM, and every durable target member
  // must map bijectively to an owned live member before any mutation. Foreign
  // duplicates are harmless: they remain represented throughout target removal.
  static_assert(CONFIG_BT_NIMBLE_MAX_CCCDS <= 32, "target membership bitmap capacity");
  std::array<uint32_t, 7> target_matches{};
  const void *tables[] = {ble_store_config_our_secs,     ble_store_config_peer_secs,
                          ble_store_config_cccds,        ble_store_config_csfcs,
                          ble_store_config_local_irks,   ble_store_config_rpa_recs,
                          ble_rpa_get_peer_dev_records()};
  const int live_counts[] = {ble_store_config_num_our_secs,     ble_store_config_num_peer_secs,
                             ble_store_config_num_cccds,        ble_store_config_num_csfcs,
                             ble_store_config_num_local_irks,   ble_store_config_num_rpa_recs,
                             ble_rpa_get_num_peer_dev_records()};
  auto digestRecord = [&](unsigned schema, const uint8_t *bytes, size_t length) {
    if (foreign_count == reset_.digests.size())
      return false;
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    const uint8_t domain[] = {uint8_t(schema), uint8_t(length), uint8_t(length >> 8)};
    bool ok = mbedtls_sha256_starts_ret(&hash, 0) == 0 &&
              mbedtls_sha256_update_ret(&hash, domain, sizeof domain) == 0 &&
              mbedtls_sha256_update_ret(&hash, bytes, length) == 0 &&
              mbedtls_sha256_finish_ret(&hash, reset_.digests[foreign_count++].data()) == 0;
    mbedtls_sha256_free(&hash);
    return ok;
  };
  auto finish = [&](std::array<uint8_t, 32> &digest) {
    std::sort(reset_.digests.begin(), reset_.digests.begin() + foreign_count);
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool ok = mbedtls_sha256_starts_ret(&hash, 0) == 0;
    for (unsigned n = 0; n < foreign_count && ok; ++n)
      ok = mbedtls_sha256_update_ret(&hash, reset_.digests[n].data(), 32) == 0;
    ok &= mbedtls_sha256_finish_ret(&hash, digest.data()) == 0;
    mbedtls_sha256_free(&hash);
    return ok;
  };
  for (unsigned n = 0; n < count && valid; ++n) {
    const auto *name = reset_.names[n].data();
    unsigned schema = 7, index = 0;
    for (unsigned t = 0; t < 7; ++t) {
      const auto length = std::strlen(prefixes[t]);
      if (std::strncmp(name, prefixes[t], length))
        continue;
      const char *digit = name + length;
      bool digits = *digit >= '1' && *digit <= '9';
      for (; *digit && digits; ++digit) {
        digits = *digit >= '0' && *digit <= '9';
        if (digits) {
          index = index * 10 + *digit - '0';
          digits = index < kStoreKeys;
        }
      }
      if (digits)
        schema = t;
      break;
    }
    if (schema == 7 || !index ||
        index > (schema == 2 ? CONFIG_BT_NIMBLE_MAX_CCCDS
                             : CONFIG_BT_NIMBLE_MAX_BONDS + (schema == 6 ? 1 : 0))) {
      valid = false;
      break;
    }
    size_t length = reset_.bytes.size();
    if (!resetAllowed() || nvs_get_blob(handle, name, reset_.bytes.data(), &length) != ESP_OK ||
        length != sizes[schema]) {
      valid = false;
      break;
    }
    ++counts[schema];
    const int classification = resetClassify(schema, reset_.bytes.data());
    if (classification < 0) {
      reset_.ambiguous = true;
      valid = false;
      break;
    }
    if (classification == 1) {
      const int total = live_counts[schema];
      const unsigned bound = schema == 2 ? CONFIG_BT_NIMBLE_MAX_CCCDS
                                         : CONFIG_BT_NIMBLE_MAX_BONDS + (schema == 6 ? 1 : 0);
      unsigned matches = 0, matched_index = 0;
      if (total < 0 || unsigned(total) > bound)
        valid = false;
      for (int i = 0; valid && i < total; ++i) {
        const auto *entry = static_cast<const uint8_t *>(tables[schema]) + i * sizes[schema];
        const void *durable_member = reset_.bytes.data(), *live_member = entry;
        size_t member_size = sizes[schema];
        if (schema == 6) {
          // Private persistence compares peer_sec, with the corrected enclosing
          // record stride; every other deleted schema compares its entire value.
          durable_member = &reinterpret_cast<const ble_hs_dev_records *>(durable_member)->peer_sec;
          live_member = &reinterpret_cast<const ble_hs_dev_records *>(entry)->peer_sec;
          member_size = sizeof(ble_hs_peer_sec);
        }
        if (!std::memcmp(durable_member, live_member, member_size)) {
          ++matches;
          matched_index = unsigned(i);
          valid = resetClassify(schema, entry) == 1;
        }
      }
      const uint32_t bit = uint32_t(1) << matched_index;
      if (!valid || matches != 1 || (target_matches[schema] & bit)) {
        reset_.ambiguous = true;
        valid = false;
        break;
      }
      target_matches[schema] |= bit;
      ++target_records;
      continue;
    }
    valid &= digestRecord(schema, reset_.bytes.data(), length);
  }
  valid &= finish(foreign);
  foreign_count = 0;
  unsigned live_targets = 0;
  for (unsigned schema = 0; schema < 7 && valid; ++schema) {
    const unsigned bound = schema == 2 ? CONFIG_BT_NIMBLE_MAX_CCCDS
                                       : CONFIG_BT_NIMBLE_MAX_BONDS + (schema == 6 ? 1 : 0);
    const int actual = live_counts[schema];
    if (actual < 0 || unsigned(actual) > bound || unsigned(actual) != counts[schema]) {
      valid = false;
      break;
    }
    for (int n = 0; n < actual && valid; ++n) {
      const auto *bytes = static_cast<const uint8_t *>(tables[schema]) + n * sizes[schema];
      const int classification = resetClassify(schema, bytes);
      if (classification < 0) {
        reset_.ambiguous = true;
        valid = false;
      } else if (classification == 1) {
        if (!(target_matches[schema] & (uint32_t(1) << unsigned(n)))) {
          reset_.ambiguous = true;
          valid = false;
          break;
        }
        ++live_targets;
      } else {
        valid &= digestRecord(schema, bytes, sizes[schema]);
      }
    }
  }
  std::array<uint8_t, 32> live_foreign{};
  valid &= finish(live_foreign) && foreign == live_foreign && live_targets == target_records;
  if (opened == ESP_OK)
    nvs_close(handle);
  return valid && resetAllowed();
}
bool Esp32BleHost::resetResolving(std::array<uint8_t, 32> &foreign, bool &found) {
  found = false;
  const auto target = address(reset_.identity);
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool valid = mbedtls_sha256_starts_ret(&hash, 0) == 0;
  for (unsigned index = 0; index <= CONFIG_BT_NIMBLE_MAX_BONDS && valid; ++index) {
    ble_hs_resolv_entry entry{};
    const int rc = ridesync_ble_resolv_read(index, &entry);
    if (rc == BLE_HS_ENOENT)
      break;
    if (rc || index == CONFIG_BT_NIMBLE_MAX_BONDS) {
      valid = false;
      break;
    }
    const bool identity_match = std::memcmp(entry.rl_identity_addr, target.val, 6) == 0;
    const bool typed_target = identity_match && entry.rl_addr_type == target.type;
    const bool alias = identity_match || std::memcmp(entry.rl_pseudo_id, target.val, 6) == 0 ||
                       std::memcmp(entry.rl_peer_rpa, target.val, 6) == 0;
    if ((alias && !typed_target) || (typed_target && found)) {
      valid = false;
      break;
    }
    if (typed_target)
      found = true;
    else
      valid = mbedtls_sha256_update_ret(&hash, reinterpret_cast<const uint8_t *>(&entry),
                                        sizeof entry) == 0;
  }
  valid &= mbedtls_sha256_finish_ret(&hash, foreign.data()) == 0;
  mbedtls_sha256_free(&hash);
  return valid;
}
void Esp32BleHost::performBondReset() {
  auto &result = reset_.result;
  result.outcome = BondOutcome::Refused;
  if (!resetAllowed())
    return;
  if (wakeReserved() || sdk_calls_.load(std::memory_order_acquire) || ble_gap_disc_active() ||
      ble_gap_adv_active() || ble_gap_conn_active() ||
      routing_lock_.test_and_set(std::memory_order_acquire)) {
    result.outcome = BondOutcome::Busy;
    return;
  }
  bool busy = false;
  const auto target = address(reset_.identity);
  for (const auto &slot : slots_)
    if (slot.context && (slot.context->phase == BlePhase::Scan || !slot.identity.verified ||
                         sameAddress(address(slot.identity), target)))
      busy = true;
  routing_lock_.clear(std::memory_order_release);
  ble_gap_conn_desc connection{};
  if (busy || ble_gap_conn_find_by_addr(&target, &connection) != BLE_HS_ENOTCONN) {
    result.outcome = BondOutcome::Busy;
    return;
  }
  std::array<uint8_t, 32> resolving_before{}, resolving_after{};
  bool resolving = false, resolving_remaining = false;
  if (!resetResolving(resolving_before, resolving))
    return; // Complete typed mapping must be unambiguous before any deletion.
  std::array<uint8_t, 32> before{}, after{};
  unsigned records = 0, remaining = 0;
  if (!resetInventory(before, records)) {
    result.outcome =
        !resetAllowed() || reset_.ambiguous ? BondOutcome::Refused : BondOutcome::Error;
    result.error = BLE_HS_ESTORE_FAIL;
    return;
  }
  if (!records && !resolving) {
    result.outcome = BondOutcome::Absent;
    return;
  }
  int error = 0;
  auto mutate = [&]() {
    if (!resetAllowed())
      return false;
    reset_.mutation.store(true, std::memory_order_release);
    return true;
  };
  if (resolving) {
    if (!mutate())
      return;
    error = ble_hs_resolv_list_rmv(target.type, const_cast<uint8_t *>(target.val));
  }
  const unsigned schemas[] = {0, 1, 2, 3, 5};
  for (unsigned schema : schemas) {
    if (error)
      break;
    ble_store_key key{};
    if (schema < 2)
      key.sec.peer_addr = target;
    else if (schema == 2)
      key.cccd.peer_addr = target;
    else if (schema == 3)
      key.csfc.peer_addr = target;
    else
      key.rpa_rec.peer_rpa_addr = target;
    const unsigned bound = schema == 2 ? CONFIG_BT_NIMBLE_MAX_CCCDS : CONFIG_BT_NIMBLE_MAX_BONDS;
    for (unsigned n = 0; n <= bound; ++n) {
      if (!mutate()) {
        error = BLE_HS_EDISABLED;
        break;
      }
      const int rc = ble_store_delete(types[schema], &key);
      if (rc == BLE_HS_ENOENT)
        break;
      if (rc || n == bound) {
        error = rc ? rc : BLE_HS_ESTORE_FAIL;
        break;
      }
    }
  }
  for (unsigned n = 0; !error && n <= CONFIG_BT_NIMBLE_MAX_BONDS + 1; ++n) {
    const int count = ble_rpa_get_num_peer_dev_records();
    if (count < 0 || count > CONFIG_BT_NIMBLE_MAX_BONDS + 1) {
      error = BLE_HS_ESTORE_FAIL;
      break;
    }
    auto *entries = ble_rpa_get_peer_dev_records();
    ble_hs_dev_records *found = nullptr;
    for (int i = 0; i < count; ++i)
      if (resetClassify(6, &entries[i]) == 1) {
        found = &entries[i];
        break;
      }
    if (!found)
      break;
    if (!mutate()) {
      error = BLE_HS_EDISABLED;
      break;
    }
    error = ble_rpa_remove_peer_dev_rec(found);
    if (n == CONFIG_BT_NIMBLE_MAX_BONDS + 1)
      error = BLE_HS_ESTORE_FAIL;
  }
  const bool verified = resetInventory(after, remaining) && !remaining && before == after &&
                        resetResolving(resolving_after, resolving_remaining) &&
                        !resolving_remaining && resolving_before == resolving_after;
  result.error = error;
  result.outcome = !error && verified ? BondOutcome::Removed : BondOutcome::Indeterminate;
  if (!verified || error)
    fail(BleFault::Store, error ? error : BLE_HS_ESTORE_FAIL);
}
BleBondAdmission Esp32BleHost::bondAdmission(const BondIdentity &id) {
  BleBondAdmission out;
  CallbackAccess sdk(sdk_calls_);
  if (reset_gate_.load(std::memory_order_acquire)) {
    out.reserved = true;
    return out;
  }
  out.stack_ready = state() == BleHostState::Ready && fault() == BleFault::None;
  out.restore_verified = restore_verified_;
  out.refusal_installed = refusal_installed_;
  out.persistence_allowed = nvsBootStatus().persistenceAllowed();
  out.capacity = CONFIG_BT_NIMBLE_MAX_BONDS;
  out.identity_matches =
      id.verified && (id.type == IdentityType::Public ||
                      (id.type == IdentityType::RandomStatic && (id.address[5] & 0xc0) == 0xc0));
  int ours = -1, peers = -1;
  if (!out.stack_ready || !out.persistence_allowed ||
      ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &ours) != 0 ||
      ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &peers) != 0 || ours < 0 || peers < 0)
    return out;
  out.used = std::max(ours, peers);
  ble_store_key_sec key{};
  key.peer_addr = address(id);
  ble_store_value_sec value{};
  const int peer_result = ble_store_read_peer_sec(&key, &value);
  bool record_present = peer_result == 0;
  bool known = peer_result == 0 && value.ltk_present && value.key_size >= 7 && value.key_size <= 16;
  if (peer_result == 0 && value.ltk_present && !known) {
    out.identity_matches = false;
    return out;
  }
  if (!known) {
    const int our_result = ble_store_read_our_sec(&key, &value);
    record_present = record_present || our_result == 0;
    known = our_result == 0 && value.ltk_present && value.key_size >= 7 && value.key_size <= 16;
    if ((peer_result != 0 && peer_result != BLE_HS_ENOENT) ||
        (our_result != 0 && our_result != BLE_HS_ENOENT) || (record_present && !known)) {
      // A partial/invalid existing record is not a new peer. Refuse re-pairing
      // rather than overwrite its security or invent a verified identity.
      out.identity_matches = false;
      return out;
    }
  }
  out.existing_verified_identity = known && value.peer_addr.type == key.peer_addr.type &&
                                   std::memcmp(value.peer_addr.val, key.peer_addr.val, 6) == 0;
  return out;
}
Esp32BleHost::Slot &Esp32BleHost::slot(BleContext &ctx) {
  const size_t index = ctx.phase == BlePhase::Scan      ? kBlePeers * 2
                       : ctx.phase == BlePhase::Connect ? ctx.peer
                                                        : kBlePeers + ctx.peer;
  auto &entry = slots_[index];
  if (!entry.initialized) {
    ble_npl_event_init(&entry.barrier, barrier, &entry);
    entry.initialized = true;
  }
  entry.context = &ctx;
  return entry;
}
void Esp32BleHost::barrier(ble_npl_event *event) {
  auto &entry = *static_cast<Slot *>(ble_npl_event_get_arg(event));
  entry.quiet.store(true, std::memory_order_release);
  // Last access: synchronize the next enqueue with NPL clearing event->queued.
  entry.barrier_queued.store(false, std::memory_order_release);
}
void Esp32BleHost::terminal(Slot &entry) {
  entry.quiet.store(false, std::memory_order_release);
  entry.context->terminal.store(true, std::memory_order_release);
  // The pinned public NPL helper uses portMAX_DELAY. Preserve its queued
  // flag/event-pointer representation but refuse a full queue without waiting.
  // The default host task consumes and clears queued before running the barrier.
  auto *event = &entry.barrier;
  if (entry.barrier_queued.exchange(true, std::memory_order_acq_rel))
    return;
  event->queued = true;
  if (xQueueSendToBack(nimble_port_get_dflt_eventq()->q, &event, 0) != pdPASS) {
    event->queued = false;
    entry.context->sealed.store(true, std::memory_order_release);
    entry.context->fault.store(BleFault::Host, std::memory_order_release);
    instance().fail(BleFault::Host, BLE_HS_ENOMEM);
    entry.barrier_queued.store(false, std::memory_order_release);
    // quiet stays false: refusal does not prove final callback access.
  }
}
void Esp32BleHost::deliver(Slot &entry, BleEvent event, bool final) {
  auto &ctx = *entry.context;
  if (ctx.receiver)
    ctx.receiver->copied(ctx, event);
  if (final)
    terminal(entry);
}
bool Esp32BleHost::quiescent(const BleContext &ctx) const {
  for (const auto &entry : slots_)
    if (entry.context == &ctx)
      return ctx.terminal.load() && !entry.barrier_queued.load(std::memory_order_acquire) &&
             entry.quiet.load(std::memory_order_acquire) &&
             entry.callbacks.load(std::memory_order_acquire) == 0;
  return ctx.terminal.load();
}
bool Esp32BleHost::releaseContext(BleContext &ctx) {
  if (!quiescent(ctx) || routing_lock_.test_and_set(std::memory_order_acquire))
    return false;
  // Another callback can schedule a fresh barrier before this lock is acquired.
  if (!quiescent(ctx)) {
    routing_lock_.clear(std::memory_order_release);
    return false;
  }
  for (auto &entry : slots_)
    if (entry.context == &ctx)
      entry.context = nullptr;
  routing_lock_.clear(std::memory_order_release);
  return true;
}
uint16_t Esp32BleHost::mtu(uint16_t connection) const {
  CallbackAccess sdk(sdk_calls_);
  return reset_gate_.load(std::memory_order_acquire) ? kBleMtuReserved : ble_att_mtu(connection);
}
int Esp32BleHost::submit(const BleCommand &cmd, BleContext &ctx) {
  CallbackAccess sdk(sdk_calls_);
  if (reset_gate_.load(std::memory_order_acquire) ||
      (wakeReserved() && (cmd.phase == BlePhase::Scan || cmd.phase == BlePhase::Connect))) {
    if (cmd.phase != BlePhase::Security)
      ctx.terminal.store(true);
    return kBleHostReserved;
  }
  if (state() != BleHostState::Ready || fault() != BleFault::None || ctx.sealed.load()) {
    if (cmd.phase != BlePhase::Security)
      ctx.terminal.store(true);
    return BLE_HS_EDISABLED;
  }
  if (cmd.phase == BlePhase::Security)
    return wakeSecurityAllowed(cmd.connection) ? ble_gap_security_initiate(cmd.connection)
                                               : BLE_HS_ENOTSUP;
  if (routing_lock_.test_and_set(std::memory_order_acquire)) {
    ctx.terminal.store(true);
    return cmd.phase == BlePhase::Scan || cmd.phase == BlePhase::Connect ? kBleHostReserved
                                                                         : BLE_HS_EBUSY;
  }
  // Linearize GAP admission with reserveWake/requestBondReset, not just the
  // preliminary atomic check made before acquiring the routing gate.
  if (reset_gate_.load(std::memory_order_acquire) ||
      (wakeReserved() && (cmd.phase == BlePhase::Scan || cmd.phase == BlePhase::Connect))) {
    routing_lock_.clear(std::memory_order_release);
    ctx.terminal.store(true);
    return kBleHostReserved;
  }
  auto &entry = slot(ctx);
  entry.identity = {};
  if (cmd.phase == BlePhase::Connect)
    entry.identity = cmd.identity;
  else if (cmd.phase != BlePhase::Scan) {
    // A retained ATT slot must keep peer ownership even after the link slot is
    // released. Unknown provenance remains a conservative reset Busy barrier.
    for (const auto &link : slots_)
      if (link.context && link.context->phase == BlePhase::Connect &&
          link.context->peer == ctx.peer && link.context->generation == ctx.generation &&
          link.context->connection.load() == cmd.connection)
        entry.identity = link.identity;
  }
  entry.quiet.store(false);
  routing_lock_.clear(std::memory_order_release);
  int rc = BLE_HS_ENOTSUP;
  switch (cmd.phase) {
  case BlePhase::Scan: {
    ble_gap_disc_params parameters{};
    parameters.passive = 1;
    parameters.filter_duplicates = 1;
    rc = ble_gap_disc(own_address_type_, cmd.duration_ms, &parameters, gap, &entry);
    break;
  }
  case BlePhase::Connect: {
    const auto peer = address(cmd.identity);
    rc = ble_gap_connect(own_address_type_, &peer, cmd.duration_ms, nullptr, gap, &entry);
    break;
  }
  case BlePhase::Services: {
    const auto service_uuid = uuid(cmd.uuid);
    rc = ble_gattc_disc_svc_by_uuid(cmd.connection, &service_uuid.u, service, &entry);
    break;
  }
  case BlePhase::Characteristics:
    rc = ble_gattc_disc_all_chrs(cmd.connection, cmd.start, cmd.end, characteristic, &entry);
    break;
  case BlePhase::Descriptors:
    rc = ble_gattc_disc_all_dscs(cmd.connection, cmd.start, cmd.end, descriptor, &entry);
    break;
  case BlePhase::Read:
  case BlePhase::VerifySubscription:
    rc = ble_gattc_read(cmd.connection, cmd.handle, attribute, &entry);
    break;
  case BlePhase::Write:
  case BlePhase::Subscribe:
    rc = ble_gattc_write_flat(cmd.connection, cmd.handle, cmd.bytes.data(), cmd.size, attribute,
                              &entry);
    break;
  default:
    break;
  }
  if (rc)
    terminal(entry); // Failed submission owns no SDK procedure; still drain a barrier.
  return rc;
}
int Esp32BleHost::retire(BleContext &ctx) {
  CallbackAccess sdk(sdk_calls_);
  if (reset_gate_.load(std::memory_order_acquire))
    return kBleHostReserved;
  return ctx.connection.load() == kBleNoHandle
             ? ble_gap_conn_cancel()
             : ble_gap_terminate(ctx.connection.load(), BLE_ERR_REM_USER_CONN_TERM);
}
int Esp32BleHost::cancelScan(BleContext &ctx) {
  CallbackAccess sdk(sdk_calls_);
  if (reset_gate_.load(std::memory_order_acquire))
    return kBleHostReserved;
  const int rc = ble_gap_disc_cancel();
  // Pinned cancel resets scan state without emitting DISC_COMPLETE. Success is
  // documented fully aborted; queue barrier also accounts for an in-flight copy.
  if (rc == 0)
    for (auto &entry : slots_)
      if (entry.context == &ctx)
        terminal(entry);
  return rc;
}
int Esp32BleHost::gap(ble_gap_event *event, void *argument) {
  auto &entry = *static_cast<Slot *>(argument);
  CallbackAccess access(entry.callbacks);
  BleEvent out;
  bool final = false;
  switch (event->type) {
  case BLE_GAP_EVENT_DISC:
    out.kind = BleEventKind::Advertisement;
    out.identity = identity(event->disc.addr);
    // Legacy GAP report types from this pinned NimBLE ABI. Unknown types stay
    // Unknown and cannot establish a connectable recovery candidate.
    switch (event->disc.event_type) {
    case BLE_HCI_ADV_RPT_EVTYPE_ADV_IND:
      out.advertisement_type = BleAdvertisementType::ConnectableUndirected;
      break;
    case BLE_HCI_ADV_RPT_EVTYPE_DIR_IND:
      out.advertisement_type = BleAdvertisementType::ConnectableDirected;
      break;
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND:
      out.advertisement_type = BleAdvertisementType::Scannable;
      break;
    case BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND:
      out.advertisement_type = BleAdvertisementType::NonConnectable;
      break;
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP:
      out.advertisement_type = BleAdvertisementType::ScanResponse;
      break;
    default:
      break;
    }
    if (event->disc.length_data > kBlePayload || (event->disc.length_data && !event->disc.data)) {
      entry.context->sealed.store(true);
      entry.context->fault.store(BleFault::Malformed);
      return 0;
    }
    out.size = event->disc.length_data;
    std::memcpy(out.bytes.data(), event->disc.data, out.size);
    break;
  case BLE_GAP_EVENT_DISC_COMPLETE:
    out.kind = BleEventKind::ScanComplete;
    out.status = event->disc_complete.reason;
    final = true;
    break;
  case BLE_GAP_EVENT_CONNECT:
    out.kind = BleEventKind::Connected;
    out.status = event->connect.status;
    out.connection = event->connect.conn_handle;
    if (!out.status)
      entry.context->connection.store(out.connection);
    final = out.status != 0;
    break;
  case BLE_GAP_EVENT_DISCONNECT: {
    out.kind = BleEventKind::Disconnected;
    out.connection = event->disconnect.conn.conn_handle;
    out.status = event->disconnect.reason;
    if (out.connection != entry.context->connection.load()) {
      entry.context->sealed.store(true);
      entry.context->fault.store(BleFault::Malformed);
      return 0;
    }
    // Pinned GAP dispatch occurs after all connection GATT procedures are
    // removed. Even a malformed/missing GATT terminal cannot leave a live SDK
    // reference after this normal terminal; retain storage through a new barrier.
    auto &host = instance();
    if (host.routing_lock_.test_and_set(std::memory_order_acquire)) {
      entry.context->sealed.store(true);
      entry.context->fault.store(BleFault::Host);
      host.fail(BleFault::Host, BLE_HS_EBUSY);
      // Uninspected procedure contexts remain quarantined.
      final = true;
      break;
    }
    for (size_t index = kBlePeers; index < kBlePeers * 2; ++index) {
      auto &procedure = instance().slots_[index];
      if (procedure.context && procedure.context->generation == entry.context->generation &&
          procedure.context->peer == entry.context->peer &&
          procedure.context->connection.load() == out.connection)
        terminal(procedure);
    }
    host.routing_lock_.clear(std::memory_order_release);
    final = true;
    break;
  }
  case BLE_GAP_EVENT_ENC_CHANGE: {
    out.kind = BleEventKind::Security;
    out.status = event->enc_change.status;
    out.connection = event->enc_change.conn_handle;
    ble_gap_conn_desc description{};
    const int rc = ble_gap_conn_find(out.connection, &description);
    if (rc)
      out.status = rc;
    else {
      out.identity = identity(description.peer_id_addr);
      out.encrypted = description.sec_state.encrypted;
      out.authenticated = description.sec_state.authenticated;
      out.bonded = description.sec_state.bonded;
    }
    break;
  }
  case BLE_GAP_EVENT_NOTIFY_RX:
    out.kind = BleEventKind::Notification;
    out.connection = event->notify_rx.conn_handle;
    out.handle = event->notify_rx.attr_handle;
    if (!copyValue(event->notify_rx.om, out)) {
      entry.context->sealed.store(true);
      entry.context->fault.store(BleFault::Malformed);
      return 0;
    }
    break;
  case BLE_GAP_EVENT_PASSKEY_ACTION:
    entry.context->sealed.store(true);
    entry.context->fault.store(BleFault::Identity); // No unknown/passkey auto acceptance.
    return 0;
  case BLE_GAP_EVENT_REPEAT_PAIRING:
    entry.context->sealed.store(true);
    entry.context->fault.store(BleFault::Identity);
    return BLE_GAP_REPEAT_PAIRING_IGNORE; // No automatic removal of an existing bond.
  default:
    return 0;
  }
  deliver(entry, out, final);
  return 0;
}
int Esp32BleHost::service(uint16_t connection, const ble_gatt_error *error,
                          const ble_gatt_svc *service, void *argument) {
  auto &entry = *static_cast<Slot *>(argument);
  CallbackAccess access(entry.callbacks);
  BleEvent out;
  out.connection = connection;
  const bool final = error && error->status != 0;
  if (!error || (error->status == 0 && !service)) {
    entry.context->sealed.store(true);
    entry.context->fault.store(BleFault::Malformed);
  } else if (final) {
    out.kind = BleEventKind::Complete;
    out.status = error->status == BLE_HS_EDONE ? 0 : error->status;
  } else {
    out.kind = BleEventKind::Service;
    out.uuid = uuid(service->uuid);
    out.start = service->start_handle;
    out.end = service->end_handle;
  }
  deliver(entry, out, final);
  return 0;
}
int Esp32BleHost::characteristic(uint16_t connection, const ble_gatt_error *error,
                                 const ble_gatt_chr *chr, void *argument) {
  auto &entry = *static_cast<Slot *>(argument);
  CallbackAccess access(entry.callbacks);
  BleEvent out;
  out.connection = connection;
  const bool final = error && error->status != 0;
  if (!error || (error->status == 0 && !chr)) {
    entry.context->sealed.store(true);
    entry.context->fault.store(BleFault::Malformed);
  } else if (final) {
    out.kind = BleEventKind::Complete;
    out.status = error->status == BLE_HS_EDONE ? 0 : error->status;
  } else {
    out.kind = BleEventKind::Characteristic;
    out.uuid = uuid(chr->uuid);
    out.start = chr->def_handle;
    out.handle = chr->val_handle;
    out.properties = chr->properties;
  }
  deliver(entry, out, final);
  return 0;
}
int Esp32BleHost::descriptor(uint16_t connection, const ble_gatt_error *error,
                             uint16_t value_handle, const ble_gatt_dsc *descriptor,
                             void *argument) {
  (void)value_handle;
  auto &entry = *static_cast<Slot *>(argument);
  CallbackAccess access(entry.callbacks);
  BleEvent out;
  out.connection = connection;
  const bool final = error && error->status != 0;
  if (!error || (error->status == 0 && !descriptor)) {
    entry.context->sealed.store(true);
    entry.context->fault.store(BleFault::Malformed);
  } else if (final) {
    out.kind = BleEventKind::Complete;
    out.status = error->status == BLE_HS_EDONE ? 0 : error->status;
  } else {
    out.kind = BleEventKind::Descriptor;
    out.uuid = uuid(descriptor->uuid);
    out.handle = descriptor->handle;
  }
  deliver(entry, out, final);
  return 0;
}
int Esp32BleHost::attribute(uint16_t connection, const ble_gatt_error *error,
                            ble_gatt_attr *attribute, void *argument) {
  auto &entry = *static_cast<Slot *>(argument);
  CallbackAccess access(entry.callbacks);
  BleEvent out;
  out.kind = BleEventKind::Complete;
  out.connection = connection;
  out.status = error ? error->status : BLE_HS_EINVAL;
  if (out.status == 0 && attribute) {
    out.handle = attribute->handle;
    if ((entry.context->phase == BlePhase::Read ||
         entry.context->phase == BlePhase::VerifySubscription) &&
        !copyValue(attribute->om, out)) {
      entry.context->sealed.store(true);
      entry.context->fault.store(BleFault::Malformed);
    }
  } else if (out.status == 0) {
    entry.context->sealed.store(true);
    entry.context->fault.store(BleFault::Malformed);
  }
  deliver(entry, out, error != nullptr);
  return 0;
}
} // namespace ridesync
#endif
#if defined(ARDUINO_ARCH_ESP32)
// The dedicated compile environment retains the real vtable and every raw SDK
// operation. Default main does not call this accessor or activate a BLE host.
struct PairingResetBackendSymbols {
  decltype(&ridesync::Esp32BleHost::requestBondReset) request;
  decltype(&ridesync::Esp32BleHost::cancelBondReset) cancel;
  decltype(&ridesync::Esp32BleHost::bondResetResult) result;
};
// Data-only linker anchor: retaining these member pointers does not execute reset.
extern "C" const PairingResetBackendSymbols ridesync_ble_pairing_reset_backend = {
    &ridesync::Esp32BleHost::requestBondReset, &ridesync::Esp32BleHost::cancelBondReset,
    &ridesync::Esp32BleHost::bondResetResult};
extern "C" ridesync::BleHost *ridesync_ble_qualification_backend() {
  return &ridesync::Esp32BleHost::instance();
}
#endif

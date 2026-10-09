// Isolated capture diagnostic with explicit one-shot operator shutter event.
#include "capture.h"
#include "nvs_guard.h"
#include "protocol/insta360_codec.h"
#include "wake.h"
#include <Arduino.h>
#include <NimBLEDevice.h> // Select the pinned dependency; never call its NVS init wrapper.
#include <esp_bt.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/task.h>
#include <nimble/esp_port/esp-hci/include/esp_nimble_hci.h>
#include <nimble/nimble/host/include/host/ble_hs.h>
#include <nimble/nimble/host/include/host/ble_hs_mbuf.h>
#include <nimble/nimble/host/include/host/ble_store.h>
#include <nimble/nimble/host/services/gap/include/services/gap/ble_svc_gap.h>
#include <nimble/nimble/host/services/gatt/include/services/gatt/ble_svc_gatt.h>
#include <nimble/porting/nimble/include/nimble/nimble_port.h>
#include <nimble/porting/nimble/include/os/os_mbuf.h>
#include <nimble/porting/npl/freertos/include/nimble/nimble_port_freertos.h>

static_assert(MYNEWT_VAL(BLE_STORE_CONFIG_PERSIST) == 0 && MYNEWT_VAL(BLE_SM_BONDING) == 0 &&
                  MYNEWT_VAL(BLE_MAX_CONNECTIONS) == 1 && MYNEWT_VAL(BLE_GATT_NOTIFY) == 1,
              "Bench requires RAM-only nonbonding store and one peer");
extern "C" void ble_store_config_init(void);

namespace {
using namespace x5_probe;
WakeOption wake;        // Identifier stays in RAM; immutable once the attempt begins.
Capture capture;        // Boot lifetime, including stopped, blocked SDK and late callbacks.
SdkControl sdk_control; // Sticky stop admission and retained submission/publication state.
ShutterControl shutter;
portMUX_TYPE capture_lock = portMUX_INITIALIZER_UNLOCKED;
uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
bool host_synced = false, private_hex = false;
bool stop_reported = false;
uint32_t last_summary = 0;

struct Lock {
  Lock() { portENTER_CRITICAL(&capture_lock); }
  ~Lock() { portEXIT_CRITICAL(&capture_lock); }
};
uint64_t nowMs() { return uint64_t(esp_timer_get_time()) / 1000; }
void record(Kind kind, uint16_t conn = BLE_HS_CONN_HANDLE_NONE, uint16_t attr = 0,
            int32_t value = 0, const uint8_t *bytes = nullptr, uint16_t length = 0) {
  const uint64_t time = nowMs();
  Lock lock;
  capture.push(kind, time, conn, attr, value, bytes, length);
}
void fail(int status, StopReason reason = StopReason::Error) {
  record(Kind::Error, BLE_HS_CONN_HANDLE_NONE, 0, status);
  Lock lock;
  capture.stop(reason);
  sdk_control.requestStop();
  shutter.cancel();
}

// Documentary prototype fields from the MIT ESP32 example pin in README;
// values are not claimed as X5 requirements. Both services are primary GATT
// services (the example calls the additional one "secondary").
ble_uuid16_t remote_uuid = BLE_UUID16_INIT(0xce80);
ble_uuid128_t extra_uuid = BLE_UUID128_INIT(0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e, 0x93,
                                            0xd2, 0x17, 0x3c, 0xff, 0xd0, 0x00, 0x00);
struct Attribute {
  ble_uuid16_t uuid;
  uint16_t flags;
  uint8_t read[4];
  uint8_t read_size;
  uint16_t handle;
};
Attribute attributes[] = {
    {BLE_UUID16_INIT(0xce81), BLE_GATT_CHR_F_WRITE, {}, 0},
    {BLE_UUID16_INIT(0xce82), BLE_GATT_CHR_F_NOTIFY, {}, 0},
    {BLE_UUID16_INIT(0xce83), BLE_GATT_CHR_F_READ, {0x01, 0x02}, 2},
    {BLE_UUID16_INIT(0xffd1), BLE_GATT_CHR_F_WRITE, {}, 0},
    {BLE_UUID16_INIT(0xffd2), BLE_GATT_CHR_F_READ, {}, 0},
    {BLE_UUID16_INIT(0xffd3), BLE_GATT_CHR_F_READ, {0x01, 0x90, 0x1e, 0x30}, 4},
    {BLE_UUID16_INIT(0xffd4), BLE_GATT_CHR_F_READ, {0x01, 0x20, 0x00, 0x18}, 4},
    {BLE_UUID16_INIT(0xffd5), BLE_GATT_CHR_F_READ, {}, 0},
    {BLE_UUID16_INIT(0xffd8), BLE_GATT_CHR_F_WRITE, {}, 0},
    {BLE_UUID16_INIT(0xfff1), BLE_GATT_CHR_F_READ, {}, 0},
    {BLE_UUID16_INIT(0xfff2), BLE_GATT_CHR_F_WRITE, {}, 0},
    {BLE_UUID16_INIT(0xffe0), BLE_GATT_CHR_F_READ, {}, 0},
};
ble_gatt_chr_def remote_chars[4]{}, extra_chars[10]{};
ble_gatt_svc_def services[3]{};

int access(uint16_t conn, uint16_t handle, ble_gatt_access_ctxt *context, void *arg) {
  auto &attribute = *static_cast<Attribute *>(arg);
  bool active;
  {
    Lock lock;
    capture.tick(uint32_t(nowMs()));
    active = capture.active() && connection == conn;
  }
  if (context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    record(Kind::Read, conn, handle, active ? 0 : BLE_ATT_ERR_READ_NOT_PERMITTED, attribute.read,
           attribute.read_size);
    if (!active)
      return BLE_ATT_ERR_READ_NOT_PERMITTED;
    return os_mbuf_append(context->om, attribute.read, attribute.read_size) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  if (context->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    const uint16_t length = OS_MBUF_PKTLEN(context->om);
    uint8_t bytes[256];
    const uint16_t copied = length < sizeof(bytes) ? length : sizeof(bytes);
    if (os_mbuf_copydata(context->om, 0, copied, bytes) != 0) {
      fail(BLE_HS_EBADDATA);
      return BLE_ATT_ERR_UNLIKELY;
    }
    record(Kind::Write, conn, handle, active ? 0 : BLE_ATT_ERR_WRITE_NOT_PERMITTED, bytes, length);
    // SDK owns/frees the mbuf. No retained pointer, decode, reply or notify.
    return active ? 0 : BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }
  return BLE_ATT_ERR_UNLIKELY;
}

int gap(ble_gap_event *event, void *) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT: {
    const uint16_t conn = event->connect.conn_handle;
    if (event->connect.status) {
      record(Kind::Connect, conn, 0, event->connect.status);
      fail(event->connect.status);
      break;
    }
    {
      Lock lock;
      connection = conn;
      shutter.connected(conn);
    }
    ble_gap_conn_desc info{};
    const int rc = ble_gap_conn_find(conn, &info);
    record(Kind::Connect, conn, rc ? 0xffff : info.peer_ota_addr.type, rc);
    record(Kind::Mtu, conn, BLE_L2CAP_CID_ATT, ble_att_mtu(conn));
    break;
  }
  case BLE_GAP_EVENT_DISCONNECT:
    record(Kind::Disconnect, event->disconnect.conn.conn_handle, 0, event->disconnect.reason);
    {
      Lock lock;
      connection = BLE_HS_CONN_HANDLE_NONE;
      shutter.disconnected();
      capture.stop(StopReason::Disconnected);
      sdk_control.requestStop();
    }
    break;
  case BLE_GAP_EVENT_MTU:
    record(Kind::Mtu, event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
    break;
  case BLE_GAP_EVENT_SUBSCRIBE:
    if (event->subscribe.attr_handle == attributes[1].handle) {
      Lock lock;
      shutter.subscription(event->subscribe.conn_handle, event->subscribe.attr_handle,
                           event->subscribe.cur_notify != 0);
    }
    record(Kind::Subscribe, event->subscribe.conn_handle, event->subscribe.attr_handle,
           event->subscribe.cur_notify | (event->subscribe.cur_indicate << 1) |
               (event->subscribe.reason << 8));
    break;
  case BLE_GAP_EVENT_ENC_CHANGE: {
    ble_gap_conn_desc info{};
    const int rc = ble_gap_conn_find(event->enc_change.conn_handle, &info);
    const uint16_t flags = rc ? 0xffff
                              : info.sec_state.encrypted | (info.sec_state.authenticated << 1) |
                                    (info.sec_state.bonded << 2);
    record(Kind::Security, event->enc_change.conn_handle, flags, event->enc_change.status);
    if (event->enc_change.status || (!rc && info.sec_state.bonded))
      fail(event->enc_change.status ? event->enc_change.status : BLE_HS_ENOTSUP,
           StopReason::Security);
    break;
  }
  case BLE_GAP_EVENT_PASSKEY_ACTION:
    // Never auto-confirm unknown numeric comparison/passkeys; never log them.
    record(Kind::Passkey, event->passkey.conn_handle, 0, event->passkey.params.action);
    fail(BLE_HS_ENOTSUP, StopReason::Security);
    break;
  case BLE_GAP_EVENT_REPEAT_PAIRING:
    record(Kind::RepeatPairing, event->repeat_pairing.conn_handle);
    fail(BLE_HS_ENOTSUP, StopReason::Security);
    return BLE_GAP_REPEAT_PAIRING_IGNORE; // Never delete a bond to retry.
  case BLE_GAP_EVENT_ADV_COMPLETE:
    record(Kind::AdvertisingEnd, BLE_HS_CONN_HANDLE_NONE, 0, event->adv_complete.reason);
    {
      Lock lock;
      wake.advertisingEnded(capture, connection);
      if (capture.used() && !capture.active()) {
        sdk_control.requestStop();
        shutter.cancel();
      }
    }
    break;
  default:
    break;
  }
  return 0;
}

void sync() {
  {
    Lock lock;
    host_synced = true;
  }
  record(Kind::Sync);
  // Owner performs prep/admission/submission. Sync never clears a pending stop.
}
int prepareAdvertising(uint8_t &address_type) {
  int rc = ble_hs_id_infer_auto(0, &address_type);
  if (wake.enabled()) {
    const auto advertisement = wake.advertisement();
    const auto response = wake.scanResponse();
    if (!rc)
      rc = ble_gap_adv_set_data(advertisement.data(), advertisement.size());
    if (!rc)
      rc = ble_gap_adv_rsp_set_data(response.data(), response.size());
    return rc;
  }
  // Pinned ble_hs_start() registers the queued services before invoking sync.
  ble_hs_adv_fields fields{};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids16 = &remote_uuid;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;
  fields.uuids128 = &extra_uuid;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  if (!rc)
    rc = ble_gap_adv_set_fields(&fields); // 3 + 4 + 18 = 25 bytes, within legacy 31.
  ble_hs_adv_fields response{};
  response.name = reinterpret_cast<const uint8_t *>(kRemoteName);
  response.name_len = sizeof(kRemoteName) - 1;
  response.name_is_complete = 1;
  if (!rc)
    rc = ble_gap_adv_rsp_set_fields(&response); // 2 + 19 = 21 bytes.
  return rc;
}
void submitAdvertising(uint8_t address_type) {
  uint32_t remaining;
  {
    Lock lock;
    // Serialized acceptance boundary AFTER all preparatory SDK calls. Time,
    // policy and sticky stop are revalidated together; no SDK under this lock.
    remaining = wake.duration(sdk_control.admitAdvertising(capture, uint32_t(nowMs())));
  }
  if (!remaining)
    return;
  // X may arrive after acceptance or this SDK call may stall. In-flight state
  // remains published and the stop latch survives; owner compensates on return.
  ble_gap_adv_params params{};
  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  const int rc =
      ble_gap_adv_start(address_type, nullptr, int32_t(remaining), &params, gap, nullptr);
  {
    Lock lock;
    sdk_control.complete(rc);
    capture.tick(uint32_t(nowMs()));
    if (!rc)
      capture.ready();
    if (!capture.active())
      sdk_control.requestStop();
  }
  if (rc)
    fail(rc);
}

void hostTask(void *) {
  nimble_port_run();
  // No deinit or object deletion beneath late callbacks. Boot lifetime always.
  vTaskDelete(nullptr);
}
void submitShutter() {
  bool pending;
  {
    Lock lock;
    pending = wake.shutterAllowed() && shutter.pending();
    if (!wake.shutterAllowed())
      shutter.cancel();
  }
  if (!pending)
    return;
  const auto bytes = ridesync::insta360::encodeShutterEvent();
  // Preparation may stall: keep the single pending slot occupied, then recheck
  // active time/current peer/subscription/stop immediately at SDK admission.
  os_mbuf *payload = ble_hs_mbuf_from_flat(bytes.data(), bytes.size());
  if (!payload) {
    {
      Lock lock;
      shutter.cancel();
    }
    record(Kind::ShutterResult, BLE_HS_CONN_HANDLE_NONE, 0, BLE_HS_ENOMEM);
    return;
  }
  uint16_t conn = BLE_HS_CONN_HANDLE_NONE, attr = 0;
  bool admitted;
  {
    Lock lock;
    admitted =
        wake.shutterAllowed() && shutter.admit(capture, sdk_control, uint32_t(nowMs()), conn, attr);
  }
  if (!admitted) {
    os_mbuf_free_chain(payload);
    record(Kind::ShutterResult, conn, attr, -1); // Local pre-submission cancellation, not SDK rc.
    return;
  }
  // Pinned SDK consumes the mbuf on every return path. No lock is held; a stop
  // after acceptance cannot retract the operation, and never causes a retry.
  const int rc = ble_gatts_notify_custom(conn, attr, payload);
  {
    Lock lock;
    sdk_control.complete(rc);
  }
  record(Kind::ShutterResult, conn, attr, rc); // Not delivery/recording proof.
}
int initializeSdk() {
  // Match the pinned NimBLE Arduino linkage anchor: pulling esp32-hal-bt.c
  // supplies strong btInUse() before initArduino runs. Otherwise its weak
  // false fallback releases all BTDM memory at boot, before this owner starts.
  // btStarted only queries status; it neither starts BLE nor accesses NVS.
  (void)btStarted();
  esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  config.mode = ESP_BT_MODE_BLE;
  config.ble_max_conn = 1;
  int rc = esp_bt_controller_init(&config);
  if (!rc)
    rc = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (!rc)
    rc = esp_nimble_hci_init();
  if (!rc)
    rc = nimble_port_init();
  if (rc)
    return rc;
  ble_hs_cfg.sync_cb = sync;
  ble_hs_cfg.reset_cb = [](int reason) { fail(reason); };
  ble_hs_cfg.gatts_register_cb = [](ble_gatt_register_ctxt *context, void *) {
    if (context->op != BLE_GATT_REGISTER_OP_CHR)
      return;
    for (const auto &attribute : attributes) {
      if (context->chr.chr_def->arg == &attribute) {
        record(Kind::Attribute, BLE_HS_CONN_HANDLE_NONE, context->chr.val_handle,
               attribute.uuid.value);
        break;
      }
    }
  };
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 0;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = 0;
  ble_hs_cfg.sm_their_key_dist = 0;
  ble_store_config_init(); // Config store, with compile-time persistence=0.
  ble_hs_cfg.store_status_cb = [](ble_store_status_event *, void *) {
    fail(BLE_HS_ESTORE_CAP, StopReason::Security);
    return BLE_HS_ESTORE_CAP; // Never oldest-peer eviction / silent retry.
  };
  ble_svc_gap_init();
  ble_svc_gatt_init();
  rc = ble_svc_gap_device_name_set(kRemoteName);
  for (unsigned i = 0; i < sizeof(attributes) / sizeof(attributes[0]); ++i) {
    auto &definition = i < 3 ? remote_chars[i] : extra_chars[i - 3];
    definition.uuid = &attributes[i].uuid.u;
    definition.access_cb = access;
    definition.arg = &attributes[i];
    definition.flags = attributes[i].flags;
    definition.val_handle = &attributes[i].handle;
  }
  services[0].type = services[1].type = BLE_GATT_SVC_TYPE_PRIMARY;
  services[0].uuid = &remote_uuid.u;
  services[0].characteristics = remote_chars;
  services[1].uuid = &extra_uuid.u;
  services[1].characteristics = extra_chars;
  if (!rc)
    rc = ble_gatts_count_cfg(services);
  if (!rc)
    rc = ble_gatts_add_svcs(services);
  if (!rc)
    nimble_port_freertos_init(hostTask);
  return rc;
}
void ownerTask(void *) {
  const int startup = initializeSdk();
  {
    Lock lock;
    sdk_control.complete(startup);
  }
  if (startup)
    fail(startup);
  bool preparation_attempted = false, termination_sent = false;
  uint32_t last_stop_attempt = 0;
  // Retained SDK owner for the entire boot; blocked mutex/HCI work cannot hold
  // control admission or reporting. No stack/controller deinit on any path.
  for (;;) {
    bool synced, prepare = false, stop;
    uint16_t conn;
    {
      Lock lock;
      capture.tick(uint32_t(nowMs()));
      if (capture.used() && !capture.active())
        sdk_control.requestStop();
      synced = host_synced;
      if (synced && !preparation_attempted) {
        preparation_attempted = true;
        prepare = sdk_control.admitPreparation(capture, uint32_t(nowMs()));
      }
    }
    if (prepare) {
      uint8_t address_type = 0;
      const int rc = prepareAdvertising(address_type);
      {
        Lock lock;
        sdk_control.complete(rc);
      }
      if (rc)
        fail(rc);
      else
        submitAdvertising(address_type);
    }
    submitShutter();
    const uint32_t now = uint32_t(nowMs());
    {
      Lock lock;
      capture.tick(now);
      if (!capture.active()) {
        sdk_control.requestStop();
        shutter.cancel();
      }
      stop = sdk_control.snapshot().stop_requested;
      conn = connection;
    }
    if (synced && stop && uint32_t(now - last_stop_attempt) >= 100) {
      last_stop_attempt = now;
      if (ble_gap_adv_active()) {
        bool admitted;
        {
          Lock lock;
          admitted = sdk_control.begin(SdkAction::StopAdvertising);
        }
        if (admitted) {
          const int rc = ble_gap_adv_stop();
          {
            Lock lock;
            sdk_control.complete(rc);
          }
          record(Kind::AdvertisingEnd, BLE_HS_CONN_HANDLE_NONE, 0, rc);
        }
      }
      if (conn != BLE_HS_CONN_HANDLE_NONE && !termination_sent) {
        bool admitted;
        {
          Lock lock;
          admitted = sdk_control.begin(SdkAction::Terminate);
        }
        if (admitted) {
          const int rc = ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
          {
            Lock lock;
            sdk_control.complete(rc);
          }
          record(Kind::Error, conn, 0, rc); // Return status, never disconnected proof.
          termination_sent = rc == 0 || rc == BLE_HS_ENOTCONN;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
bool emit(const char *line, size_t length) {
  return canReport(length, size_t(Serial.availableForWrite())) &&
         Serial.write(reinterpret_cast<const uint8_t *>(line), length) == length;
}
} // namespace

void setup() {
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  esp_log_level_set("*", ESP_LOG_NONE); // No SDK peer addresses/credentials in output.
  const char banner[] =
      "X5_PROBE: A once=capture120s X=stop S=ONE shutter toggle after observation "
      "W+6ASCII+LF=idle wake-only3s H=private sensitive hex; recording UNKNOWN\n";
  emit(banner, sizeof(banner) - 1);
}
void handleCommand(uint32_t now) {
  if (Serial.available()) {
    int command = Serial.read();
    WakeInput input;
    {
      Lock lock;
      input = wake.feed(uint8_t(command), capture.used());
    }
    if (input != WakeInput::Command)
      command = -1; // Consume malformed/private lines, never dispatch their suffix.
    if (input == WakeInput::Accepted || input == WakeInput::Refused)
      record(Kind::WakeOption, BLE_HS_CONN_HANDLE_NONE, 0, input == WakeInput::Accepted ? 1 : 0);
    if (command == 'A') {
      bool begin;
      {
        Lock lock;
        begin = wake.allowBegin() && capture.begin(now);
        if (begin)
          sdk_control.begin(SdkAction::Startup);
      }
      if (begin && xTaskCreate(ownerTask, "x5_owner", 6144, nullptr, 1, nullptr) != pdPASS) {
        {
          Lock lock;
          sdk_control.complete(ESP_ERR_NO_MEM);
        }
        fail(ESP_ERR_NO_MEM);
      }
    } else if (command == 'X') {
      Lock lock;
      capture.stop(StopReason::Requested);
      if (capture.used())
        sdk_control.requestStop();
      shutter.cancel();
    } else if (command == 'S') {
      Lock lock;
      const bool accepted =
          wake.shutterAllowed() && shutter.request(capture, sdk_control, uint32_t(nowMs()));
      capture.push(Kind::ShutterRequest, nowMs(), connection, attributes[1].handle,
                   accepted ? 1 : 0);
    } else if (command == 'H') {
      private_hex = true; // Explicit opt-in. Never print SM keys/passkeys.
    }
  }
}

struct LoopState {
  bool active, used, synced;
  uint16_t conn;
  StopReason reason;
};

LoopState updateCapture(uint32_t now) {
  LoopState state;
  {
    Lock lock;
    capture.tick(now);
    if (capture.used() && !capture.active()) {
      sdk_control.requestStop();
      shutter.cancel();
    }
    state.active = capture.active();
    state.used = capture.used();
    state.synced = host_synced;
    state.conn = connection;
    state.reason = capture.reason();
  }
  return state;
}

void reportStop(const LoopState &state) {
  if (state.used && !state.active && !stop_reported) {
    record(Kind::Stop, state.conn, 0, int(state.reason));
    stop_reported = true;
  }
}

void reportQueuedEvent() {
  // The loop only latches cleanup. SDK owner may be stalled indefinitely while
  // control continues serial, policy deadlines and bounded reporting below.
  Event event;
  bool queued;
  {
    Lock lock;
    queued = capture.front() != nullptr;
    if (queued)
      event = *capture.front();
  }
  char line[768];
  if (queued) {
    const size_t length = formatEvent(event, private_hex, line, sizeof(line));
    if (emit(line, length)) {
      Lock lock;
      capture.pop();
    }
  }
}

void reportSummary(uint32_t now, const LoopState &state, char *line) {
  if (uint32_t(now - last_summary) >= 1000) {
    last_summary = now;
    Stats stats;
    SdkPublication sdk;
    unsigned depth;
    bool shutter_pending;
    {
      Lock lock;
      stats = capture.stats();
      depth = capture.queued();
      sdk = sdk_control.snapshot();
      shutter_pending = shutter.pending();
    }
    const auto nvs = nvsRefusals();
    const int n = snprintf(
        line, sizeof(line),
        "X5_PROBE: boot_ms=%lu used=%u active=%u synced=%u advertising=%u conn=%u stop=%u hex=%u "
        "seen=%lu reported=%lu queued=%u dropped=%lu truncated=%lu "
        "sdk_stop=%u sdk_inflight=%u sdk_op=%u sdk_ops_admitted/returned=%lu/%lu "
        "sdk_last_op/rc=%u/%d shutter_pending=%u wake_only=%u "
        "nvs_refused_init/open/erase=%lu/%lu/%lu recording=UNKNOWN kind_counts=",
        static_cast<unsigned long>(now), state.used, state.active, state.synced,
        state.synced ? unsigned(ble_gap_adv_active()) : 0, state.conn, unsigned(state.reason),
        private_hex, static_cast<unsigned long>(stats.seen),
        static_cast<unsigned long>(stats.reported), depth,
        static_cast<unsigned long>(stats.dropped), static_cast<unsigned long>(stats.truncated),
        sdk.stop_requested, sdk.in_flight, unsigned(sdk.action),
        static_cast<unsigned long>(sdk.accepted), static_cast<unsigned long>(sdk.returned),
        unsigned(sdk.last_returned_action), sdk.last_status, shutter_pending, wake.enabled(),
        static_cast<unsigned long>(nvs.init), static_cast<unsigned long>(nvs.open),
        static_cast<unsigned long>(nvs.erase));
    if (n > 0 && size_t(n) < sizeof(line)) {
      size_t length = size_t(n);
      for (unsigned i = 0; i < unsigned(Kind::Count); ++i) {
        const int added = snprintf(line + length, sizeof(line) - length, "%s%lu", i ? "/" : "",
                                   static_cast<unsigned long>(stats.by_kind[i]));
        if (added < 0 || size_t(added) >= sizeof(line) - length) {
          length = 0;
          break;
        }
        length += size_t(added);
      }
      if (length && length + 1 < sizeof(line)) {
        line[length++] = '\n';
        emit(line, length);
      }
    }
  }
}

void loop() {
  const uint32_t now = uint32_t(nowMs());
  handleCommand(now);
  const LoopState state = updateCapture(now);
  reportStop(state);
  char line[768];
  reportQueuedEvent();
  reportSummary(now, state, line);
  delay(1);
}

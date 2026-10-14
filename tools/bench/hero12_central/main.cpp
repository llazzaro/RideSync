// Experimental single-HERO12 central probe. RAM-only bonds, no board buses.
#include "../x5_peripheral/nvs_guard.h"
#include "policy.h"
#include "protocol/gopro_setup_codec.h"
#include <Arduino.h>
#include <NimBLEDevice.h> // Dependency selection only; init wrapper is forbidden.
#include <atomic>
#include <cstdarg>
#include <cstring>
#include <esp_bt.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <nimble/esp_port/esp-hci/include/esp_nimble_hci.h>
#include <nimble/nimble/host/include/host/ble_hs.h>
#include <nimble/nimble/host/include/host/ble_hs_mbuf.h>
#include <nimble/nimble/host/include/host/ble_store.h>
#include <nimble/nimble/host/services/gap/include/services/gap/ble_svc_gap.h>
#include <nimble/nimble/host/services/gatt/include/services/gatt/ble_svc_gatt.h>
#include <nimble/porting/nimble/include/nimble/nimble_port.h>
#include <nimble/porting/npl/freertos/include/nimble/nimble_port_freertos.h>
static_assert(MYNEWT_VAL(BLE_STORE_CONFIG_PERSIST) == 0 && MYNEWT_VAL(BLE_SM_BONDING) == 1,
              "Temporary bonds must remain in RAM");
extern "C" void ble_store_config_init(void);
namespace {
using namespace ridesync::gopro;
struct Log {
  char text[160];
};
struct Notification {
  uint16_t conn, handle, size;
  uint8_t bytes[64];
};
QueueHandle_t logs, notifications, commands;
SemaphoreHandle_t completed;
std::atomic<bool> synced{false}, failed{false}, stopping{false}, encrypted{false}, bonded{false};
std::atomic<bool> accepting{true};
std::atomic<uint16_t> conn{BLE_HS_CONN_HANDLE_NONE};
std::atomic<int> completion_rc{0}, security_rc{-1}, stage{0};
std::atomic<uint32_t> drops{0}, sdk_calls{0}, sdk_returns{0};
ble_addr_t candidate{};
bool candidate_present = false, candidates_ambiguous = false;
uint16_t service_start[2]{}, service_end[2]{};
// Command, Settings, Query, Management order follows Channel enum.
struct Endpoint {
  uint16_t write = 0, notify = 0, cccd = 0;
  uint8_t wp = 0, np = 0;
} endpoints[4];
uint16_t char_definitions[32]{};
size_t char_count = 0;
Reassembler reassembly;
hero12_probe::Policy policy;
uint32_t generation = 0, last_keepalive = 0;
bool poisoned = false;
void log(const char *fmt, ...) {
  Log line{};
  va_list args;
  va_start(args, fmt);
  vsnprintf(line.text, sizeof(line.text), fmt, args);
  va_end(args);
  if (xQueueSend(logs, &line, 0) != pdTRUE) {
    ++drops;
    failed = true;
  }
}
void done(int rc) {
  completion_rc = rc;
  xSemaphoreGive(completed);
}
bool waitOperation(int rc, uint32_t timeout = 5000) {
  ++sdk_calls;
  if (rc) {
    ++sdk_returns;
    log("SDK submission rc=%d", rc);
    return false;
  }
  if (xSemaphoreTake(completed, pdMS_TO_TICKS(timeout)) != pdTRUE) {
    log("SDK callback timeout; reboot required");
    poisoned = true;
    return false;
  }
  ++sdk_returns;
  rc = completion_rc.load();
  if (rc)
    log("SDK callback rc=%d", rc);
  return !rc && !failed && !stopping;
}
int attribute(uint16_t, const ble_gatt_error *e, ble_gatt_attr *a, void *arg) {
  int rc = e->status;
  if (!rc && arg) {
    uint8_t b[2]{};
    if (!a || OS_MBUF_PKTLEN(a->om) != 2 || os_mbuf_copydata(a->om, 0, 2, b) || b[0] != 1 ||
        b[1] != 0)
      rc = BLE_HS_EINVAL;
  }
  done(rc);
  return 0;
}
int services(uint16_t, const ble_gatt_error *e, const ble_gatt_svc *s, void *) {
  if (e->status) {
    done(e->status == BLE_HS_EDONE ? 0 : e->status);
    return 0;
  }
  char uuid[BLE_UUID_STR_LEN];
  ble_uuid_to_str(&s->uuid.u, uuid);
  int i = !strcmp(uuid, "0xfea6")                                 ? 0
          : !strcmp(uuid, "b5f90090-aa8d-11e3-9046-0002a5d5c51b") ? 1
                                                                  : -1;
  if (i >= 0) {
    if (service_start[i]) {
      failed = true;
      return BLE_HS_EINVAL;
    }
    service_start[i] = s->start_handle;
    service_end[i] = s->end_handle;
  }
  return 0;
}
int characteristics(uint16_t, const ble_gatt_error *e, const ble_gatt_chr *c, void *) {
  if (e->status) {
    done(e->status == BLE_HS_EDONE ? 0 : e->status);
    return 0;
  }
  if (char_count == 32) {
    failed = true;
    return BLE_HS_EINVAL;
  }
  char_definitions[char_count++] = c->def_handle;
  char uuid[BLE_UUID_STR_LEN];
  ble_uuid_to_str(&c->uuid.u, uuid);
  const char *ids[] = {"0072", "0073", "0074", "0075", "0076", "0077", "0091", "0092"};
  for (unsigned i = 0; i < 8; ++i) {
    char expected[40];
    snprintf(expected, sizeof(expected), "b5f9%s-aa8d-11e3-9046-0002a5d5c51b", ids[i]);
    if (strcmp(uuid, expected))
      continue;
    auto &p = endpoints[i / 2];
    auto &h = (i % 2) ? p.notify : p.write;
    if (h)
      failed = true;
    h = c->val_handle;
    if (i % 2)
      p.np = c->properties;
    else
      p.wp = c->properties;
  }
  return 0;
}
int descriptors(uint16_t, const ble_gatt_error *e, uint16_t chr, const ble_gatt_dsc *d, void *) {
  if (e->status) {
    done(e->status == BLE_HS_EDONE ? 0 : e->status);
    return 0;
  }
  if (ble_uuid_u16(&d->uuid.u) == 0x2902)
    for (auto &p : endpoints)
      if (p.notify == chr) {
        if (p.cccd)
          failed = true;
        p.cccd = d->handle;
      }
  return 0;
}
bool goproAdvertisement(const uint8_t *data, size_t length) {
  for (size_t at = 0; at < length;) {
    size_t n = data[at++];
    if (!n || n > length - at)
      return false;
    uint8_t type = data[at];
    if (type == 2 || type == 3)
      for (size_t i = at + 1; i + 1 < at + n; i += 2)
        if (data[i] == 0xa6 && data[i + 1] == 0xfe)
          return true;
    at += n;
  }
  return false;
}
int gap(ble_gap_event *e, void *) {
  switch (e->type) {
  case BLE_GAP_EVENT_DISC:
    if (goproAdvertisement(e->disc.data, e->disc.length_data)) {
      if (!candidate_present) {
        candidate = e->disc.addr;
        candidate_present = true;
      } else if (candidate.type != e->disc.addr.type || memcmp(candidate.val, e->disc.addr.val, 6))
        candidates_ambiguous = true;
    }
    break;
  case BLE_GAP_EVENT_DISC_COMPLETE:
    done(e->disc_complete.reason);
    break;
  case BLE_GAP_EVENT_CONNECT:
    if (!e->connect.status)
      conn = e->connect.conn_handle;
    done(e->connect.status);
    log("CONNECT callback rc=%d", e->connect.status);
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    conn = BLE_HS_CONN_HANDLE_NONE;
    encrypted = false;
    bonded = false;
    log("DISCONNECT reason=%d", e->disconnect.reason);
    break;
  case BLE_GAP_EVENT_ENC_CHANGE: {
    ble_gap_conn_desc d{};
    int rc = ble_gap_conn_find(e->enc_change.conn_handle, &d);
    encrypted = !rc && d.sec_state.encrypted;
    bonded = !rc && d.sec_state.bonded;
    security_rc = e->enc_change.status;
    if (security_rc || !encrypted || !bonded)
      failed = true;
    log("SECURITY rc=%d encrypted=%d bonded=%d", e->enc_change.status, int(encrypted.load()),
        int(bonded.load()));
    break;
  }
  case BLE_GAP_EVENT_NOTIFY_RX: {
    Notification n{};
    n.conn = e->notify_rx.conn_handle;
    n.handle = e->notify_rx.attr_handle;
    n.size = OS_MBUF_PKTLEN(e->notify_rx.om);
    if (n.size > sizeof(n.bytes) || os_mbuf_copydata(e->notify_rx.om, 0, n.size, n.bytes) ||
        xQueueSend(notifications, &n, 0) != pdTRUE) {
      ++drops;
      failed = true;
    }
    break;
  }
  case BLE_GAP_EVENT_REPEAT_PAIRING:
    log("Repeat pairing refused");
    return BLE_GAP_REPEAT_PAIRING_IGNORE;
  case BLE_GAP_EVENT_PASSKEY_ACTION:
    log("Unexpected passkey action; refusing");
    failed = true;
    break;
  default:
    break;
  }
  return 0;
}
void hostTask(void *) {
  nimble_port_run();
  vTaskSuspend(nullptr);
}
int initialize() {
  (void)btStarted();
  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  cfg.mode = ESP_BT_MODE_BLE;
  cfg.ble_max_conn = 1;
  int rc = esp_bt_controller_init(&cfg);
  if (!rc)
    rc = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (!rc)
    rc = esp_nimble_hci_init();
  if (!rc)
    rc = nimble_port_init();
  if (rc)
    return rc;
  ble_hs_cfg.sync_cb = []() {
    ble_addr_t address{};
    int status = ble_hs_id_gen_rnd(0, &address);
    if (!status)
      status = ble_hs_id_set_rnd(address.val);
    if (status) {
      failed = true;
      log("Ephemeral identity rc=%d", status);
    } else
      synced = true;
  };
  ble_hs_cfg.reset_cb = [](int reason) {
    failed = true;
    log("Host reset reason=%d", reason);
  };
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 0; // Experimental legacy pairing candidate; no fallback retry.
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_store_config_init();
  ble_hs_cfg.store_status_cb = [](ble_store_status_event *, void *) {
    failed = true;
    return BLE_HS_ESTORE_CAP;
  };
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_svc_gap_device_name_set("RideSync bench");
  nimble_port_freertos_init(hostTask);
  return 0;
}
bool exchange(Channel channel, const uint8_t *bytes, size_t size, Message &reply) {
  if (conn == BLE_HS_CONN_HANDLE_NONE || failed || stopping || poisoned || !encrypted || !bonded ||
      (policy.linked && uint32_t(millis() - policy.started) >= 600000))
    return false;
  auto &p = endpoints[unsigned(channel)];
  if (!waitOperation(ble_gattc_write_flat(conn, p.write, bytes, size, attribute, nullptr)))
    return false;
  uint32_t began = millis();
  while (uint32_t(millis() - began) < 5000 && !failed && !stopping &&
         conn != BLE_HS_CONN_HANDLE_NONE) {
    Notification n{};
    if (xQueueReceive(notifications, &n, pdMS_TO_TICKS(20)) != pdTRUE)
      continue;
    if (n.conn != conn)
      continue;
    int route = -1;
    for (unsigned i = 0; i < 4; ++i)
      if (endpoints[i].notify == n.handle)
        route = i;
    if (route < 0) {
      failed = true;
      break;
    }
    Message message{};
    auto outcome = reassembly.feed(generation, Channel(route), n.bytes, n.size, millis(), message);
    if (outcome == Outcome::Pending)
      continue;
    if (outcome != Outcome::Complete) {
      log("Fragment error=%u", unsigned(outcome));
      break;
    }
    if (Channel(route) != channel) {
      log("Unexpected response route");
      break;
    }
    reply = message;
    return true;
  }
  log("Response absent/ambiguous; connection retired");
  poisoned = true;
  return false;
}
bool request(Request req, Response &r) {
  Packet p{};
  Message m{};
  if (!encode(req, p, false) || !exchange(p.channel, p.bytes, p.size, m))
    return false;
  r = decode(p.channel, m);
  if (r.outcome != Outcome::Complete || r.id != p.id || r.result) {
    log("Classic response request=%u outcome=%u id=%u result=%u", p.id, unsigned(r.outcome), r.id,
        r.result);
    return false;
  }
  return true;
}
bool setupCommand(SetupOperation op) {
  SetupPacket p{};
  Message m{};
  if (!encodeSetup(op, p, false) || !exchange(p.channel, p.bytes, p.size, m))
    return false;
  auto r = decodeSetup(p.channel, m, op);
  log("SETUP operation=%u result=%llu success=%d", unsigned(op), r.result, int(r.success));
  return r.outcome == Outcome::Complete && r.success;
}
bool identity() {
  const char expected[] = "H23.01.02.32.00";
  for (auto req : {Request::HardwareInfo, Request::ApiVersion}) {
    Packet p{};
    Message m{};
    if (!encode(req, p, false) || !exchange(p.channel, p.bytes, p.size, m))
      return false;
    auto r = decodeIdentity(p.channel, m);
    if (r.outcome != Outcome::Complete || r.classic_result || r.id != p.id)
      return false;
    if (req == Request::HardwareInfo) {
      auto f = r.fields[3];
      bool match = r.model_known && r.model == 62 && f.size == sizeof(expected) - 1 &&
                   !memcmp(r.raw.bytes + f.offset, expected, f.size);
      log("IDENTITY model=%llu firmware_match=%d expected=%s", r.model, int(match), expected);
      if (!match)
        return false;
    } else {
      log("API major=%llu minor=%llu", r.api_major, r.api_minor);
      if (!r.api_known || r.api_major != 2 || r.api_minor != 0)
        return false;
    }
  }
  return true;
}
bool observe() {
  Response e{}, b{}, r{};
  uint32_t sampled = millis();
  if (!request(Request::GetEncoding, e) || !e.encoding_known || !request(Request::GetBusy, b) ||
      !b.busy_known || !request(Request::GetReady, r) || !r.ready_known)
    return false;
  if (uint32_t(millis() - sampled) > 2000) {
    log("Status snapshot stale");
    return false;
  }
  policy.observe(e.encoding, b.busy, r.ready, sampled);
  log("OBSERVED encoding=%d busy=%d ready=%d", int(e.encoding), int(b.busy), int(r.ready));
  return true;
}
bool connectCamera() {
  stage = 1;
  candidate_present = candidates_ambiguous = false;
  char_count = 0;
  memset(service_start, 0, sizeof(service_start));
  memset(service_end, 0, sizeof(service_end));
  for (auto &p : endpoints)
    p = Endpoint{};
  xQueueReset(notifications);
  while (xSemaphoreTake(completed, 0) == pdTRUE) {
  }
  ble_gap_disc_params params{};
  params.passive = 0;
  params.filter_duplicates = 1;
  if (!waitOperation(ble_gap_disc(BLE_OWN_ADDR_RANDOM, 8000, &params, gap, nullptr), 10000))
    return false;
  log("SCAN candidate=%d ambiguous=%d", int(candidate_present), int(candidates_ambiguous));
  if (!candidate_present || candidates_ambiguous)
    return false;
  stage = 2;
  if (!waitOperation(ble_gap_connect(BLE_OWN_ADDR_RANDOM, &candidate, 10000, nullptr, gap, nullptr),
                     12000))
    return false;
  policy.connected(++generation, millis());
  if (!waitOperation(ble_gattc_disc_all_svcs(conn, services, nullptr)))
    return false;
  if (!service_start[0] || !service_start[1])
    return false;
  stage = 3;
  security_rc = -1;
  encrypted = false;
  bonded = false;
  int rc = ble_gap_security_initiate(conn);
  log("SECURITY submission rc=%d", rc);
  if (rc)
    return false;
  uint32_t began = millis();
  while (security_rc < 0 && conn != BLE_HS_CONN_HANDLE_NONE && !failed && !stopping &&
         uint32_t(millis() - began) < 15000)
    vTaskDelay(pdMS_TO_TICKS(20));
  if (security_rc != 0 || !encrypted || !bonded)
    return false;
  stage = 4;
  for (unsigned i = 0; i < 2; ++i) {
    if (!waitOperation(ble_gattc_disc_all_chrs(conn, service_start[i], service_end[i],
                                               characteristics, nullptr)))
      return false;
  }
  for (unsigned i = 0; i < 4; ++i) {
    auto &p = endpoints[i];
    if (!p.notify)
      return false;
    uint16_t end = hero12_probe::descriptorEnd(p.notify, service_end[i == 3 ? 1 : 0],
                                               char_definitions, char_count);
    if (end <= p.notify ||
        !waitOperation(ble_gattc_disc_all_dscs(conn, p.notify, end, descriptors, nullptr)))
      return false;
    if (!p.write || !p.notify || !p.cccd || !(p.wp & BLE_GATT_CHR_PROP_WRITE) ||
        !(p.np & BLE_GATT_CHR_PROP_NOTIFY))
      return false;
    uint8_t value[] = {1, 0};
    if (!waitOperation(ble_gattc_write_flat(conn, p.cccd, value, 2, attribute, nullptr)) ||
        !waitOperation(ble_gattc_read(conn, p.cccd, attribute, reinterpret_cast<void *>(1))))
      return false;
  }
  log("GATT eight endpoints; four CCCDs enabled/read back");
  stage = 5;
  // Official setup requires successful Hardware Info readiness before control.
  // Classic tutorial path does not require a protobuf control claim.
  // Do not reinterpret the previously observed Unknown claim as success.
  if (!setupCommand(SetupOperation::PairingFinish) || !identity())
    return false;
  policy.qualified = true;
  if (!observe())
    return false;
  last_keepalive = millis();
  stage = 6;
  log("READY CLASSIC diagnostic; explicit REC/STOP; control claim UNQUALIFIED; no replay");
  return true;
}
void disconnectCamera() {
  policy.disconnect();
  if (conn != BLE_HS_CONN_HANDLE_NONE) {
    int rc = ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    log("Disconnect submission rc=%d", rc);
    uint32_t began = millis();
    while (conn != BLE_HS_CONN_HANDLE_NONE && uint32_t(millis() - began) < 5000)
      vTaskDelay(pdMS_TO_TICKS(20));
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
      poisoned = true;
      log("Disconnect unresolved; reboot required");
    }
  }
  stage = 0;
}
bool shutter(bool start) {
  if (!observe())
    return false;
  if (!policy.admit(start, millis())) {
    log("SHUTTER refused by fresh state");
    return true;
  }
  Response r{};
  if (start && (!request(Request::Video, r) || !observe()))
    return false;
  if (stopping || !policy.admit(start, millis()))
    return false;
  if (!request(start ? Request::ShutterOn : Request::ShutterOff, r))
    return false;
  log("SHUTTER camera_ack=0 requested=%s", start ? "REC" : "STOP");
  // Query after acknowledged operation; never retry shutter.
  for (unsigned i = 0; i < 5; ++i) {
    if (!observe())
      return false;
    if (policy.encoding == start) {
      log("CONFIRMED encoding=%d; camera screen/playback still required", int(start));
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  log("Recording transition unconfirmed");
  return false;
}
void owner(void *) {
  int rc = initialize();
  log("STARTUP rc=%d RAM-only legacy security", rc);
  if (rc)
    failed = true;
  uint32_t began = millis();
  while (!synced && !failed && uint32_t(millis() - began) < 5000)
    vTaskDelay(pdMS_TO_TICKS(20));
  if (!synced)
    failed = true;
  for (;;) {
    if (stopping.load() || failed || (policy.linked && conn == BLE_HS_CONN_HANDLE_NONE) ||
        (policy.linked && uint32_t(millis() - policy.started) >= 600000)) {
      xQueueReset(commands);
      disconnectCamera();
      stopping = false;
      accepting = true;
    }
    char command = 0;
    if (xQueueReceive(commands, &command, pdMS_TO_TICKS(20)) == pdTRUE) {
      if (failed || poisoned) {
        log("Refused: reboot required");
        accepting = true;
        continue;
      }
      bool ok = true;
      if (command == 'C' && !policy.linked)
        ok = connectCamera();
      else if (command == 'D')
        disconnectCamera();
      else if (policy.qualified && command == 'R')
        ok = shutter(true);
      else if (policy.qualified && command == 'S')
        ok = shutter(false);
      else if (policy.qualified && command == 'Q')
        ok = observe();
      else {
        log("Command refused: admission/state");
        accepting = true;
        continue;
      }
      if (!ok) {
        log("Operation failed; no retry");
        disconnectCamera();
      }
      accepting = true;
    }
    if (policy.qualified && uint32_t(millis() - last_keepalive) >= 3000) {
      Response r{};
      last_keepalive = millis();
      if (!request(Request::KeepAlive, r)) {
        log("Keepalive failed");
        disconnectCamera();
      }
    }
  }
}
hero12_probe::LineParser parser;
uint32_t last_status = 0;
} // namespace
void setup() {
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  logs = xQueueCreate(32, sizeof(Log));
  notifications = xQueueCreate(32, sizeof(Notification));
  commands = xQueueCreate(1, sizeof(char));
  completed = xSemaphoreCreateBinary();
  if (!logs || !notifications || !commands || !completed) {
    Serial.println("Allocation refusal");
    return;
  }
  Serial.println("HERO12_PROBE idle; CONNECT REC STOP QUERY DISCONNECT STATUS; RAM-only bonds; no "
                 "board buses");
  if (xTaskCreate(owner, "hero12_owner", 8192, nullptr, 1, nullptr) != pdPASS) {
    failed = true;
    Serial.println("Owner allocation refusal");
  }
}
void loop() {
  if (!logs) {
    delay(20);
    return;
  }
  Log line{};
  if (Serial.availableForWrite() >= 160 && xQueueReceive(logs, &line, 0) == pdTRUE)
    Serial.println(line.text);
  while (Serial.available()) {
    char ch = char(Serial.read());
    char code = parser.feed(ch, Serial.available() > 0);
    if (code == '?')
      last_status = 0;
    else if (code == 'D') {
      stopping = true;
      accepting = false;
      xQueueReset(commands);
    } else if (code == '!')
      Serial.println("Command refused: invalid/burst");
    else if (code) {
      if (!accepting.exchange(false))
        Serial.println("Command refused: busy");
      else if (xQueueSend(commands, &code, 0) != pdTRUE) {
        accepting = true;
        Serial.println("Command refused: busy");
      }
    }
  }
  parser.drained();
  if (uint32_t(millis() - last_status) >= 2000) {
    last_status = millis();
    auto n = x5_probe::nvsRefusals();
    Serial.printf("STATUS ms=%lu stage=%d connected=%d encrypted=%d bonded=%d failed=%d drops=%lu "
                  "sdk=%lu/%lu nvs_init=%lu open=%lu erase=%lu\n",
                  millis(), stage.load(), int(conn != BLE_HS_CONN_HANDLE_NONE),
                  int(encrypted.load()), int(bonded.load()), int(failed.load()), drops.load(),
                  sdk_returns.load(), sdk_calls.load(), n.init, n.open, n.erase);
  }
  delay(2);
}

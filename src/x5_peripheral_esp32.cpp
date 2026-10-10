#include "x5_peripheral_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "nvs_boot_guard.h"
#include <cstring>
#include <esp32-hal-bt.h>
#include <esp_timer.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <nimble/nimble/host/services/gap/include/services/gap/ble_svc_gap.h>
#include <nimble/nimble/host/services/gatt/include/services/gatt/ble_svc_gatt.h>
#include <nimble/porting/nimble/include/nimble/nimble_port.h>
#include <nimble/porting/nimble/include/os/os_mbuf.h>
namespace ridesync {
struct Esp32X5Peripheral::Guard {
  Esp32X5Peripheral &owner;
  explicit Guard(Esp32X5Peripheral &p) : owner(p) { portENTER_CRITICAL(&owner.mux_); }
  ~Guard() { portEXIT_CRITICAL(&owner.mux_); }
};
struct Esp32X5Peripheral::CallbackGuard {
  Esp32X5Peripheral &owner;
  explicit CallbackGuard(Esp32X5Peripheral &p) : owner(p) {
    owner.callback_refs_.fetch_add(1, std::memory_order_acq_rel);
    Guard lock(owner);
    owner.policy_.callbackEnter();
  }
  ~CallbackGuard() {
    {
      Guard lock(owner);
      owner.policy_.callbackExit();
    }
    owner.callback_refs_.fetch_sub(1, std::memory_order_release);
  }
};
namespace {
bool due(uint32_t now, uint32_t deadline) { return now - deadline < 0x80000000UL; }
constexpr uint8_t kAdvertisement[] = {0x02, 0x01, 0x06, 0x03, 0x03, 0x80, 0xce, 0x11, 0x07,
                                      0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e, 0x93,
                                      0xd2, 0x17, 0x3c, 0xff, 0xd0, 0x00, 0x00};
constexpr uint8_t kName[] = {0x14, 0x09, 'I', 'n', 's', 't', 'a', '3', '6', '0', ' ',
                             'G',  'P',  'S', ' ', 'R', 'e', 'm', 'o', 't', 'e'};
} // namespace
uint32_t Esp32X5Peripheral::now() { return uint32_t(esp_timer_get_time() / 1000); }
bool Esp32X5Peripheral::configure(const X5Qualification &q) {
  Guard lock(*this);
  if (worker_created_ || !q.enabled || !q.identity.verified || !q.firmware_size ||
      q.firmware_size >= q.firmware.size() || !q.store.qualification_record ||
      q.display.profile != insta360::Ce80DisplayProfile::X5CapturedDisplayV1 ||
      (q.identity.type != IdentityType::Public && q.identity.type != IdentityType::RandomStatic))
    return false;
  auto &host = Esp32BleHost::instance();
  if (!host.configureRestore(q.store) || !host.configurePeripheral(this, registration))
    return false;
  qualification_ = q;
  configured_ = true;
  ble_npl_event_init(&barrier_event_, barrier, this);
  return true;
}
bool Esp32X5Peripheral::connect(Token token, uint32_t deadline) {
  {
    Guard lock(*this);
    if (!configured_ || reserved_ || callback_refs_.load() ||
        !policy_.begin(token, deadline, now()) || !mailbox_.begin(token.connection))
      return false;
    token_ = token;
    deadline_ = deadline;
    connecting_ = true;
    stop_pending_ = stop_sent_ = terminate_sent_ = terminal_marked_ = gap_entered_ = false;
    loss_notice_ = loss_revocation_ = critical_pending_ = false;
    cleanup_handle_ = kBleNoHandle;
  }
  if (!worker_created_) {
    worker_created_ = true; // Sticky even if task creation fails; never spawn a replacement.
    TaskHandle_t task = nullptr;
    if (xTaskCreate(worker, "x5_sdk", 4096, this, 2, &task) != pdPASS) {
      Guard lock(*this);
      policy_.seal();
      terminal();
      return false;
    }
  }
  return true;
}
bool Esp32X5Peripheral::notify(const X5ShutterRequest &request) {
  Guard lock(*this);
  return !loss_revocation_ && !stop_pending_ && reserved_ && policy_.enqueue(request);
}
void Esp32X5Peripheral::cancel(Token token) {
  Guard lock(*this);
  policy_.cancel(token);
}
void Esp32X5Peripheral::close(uint32_t generation) {
  Guard lock(*this);
  if (generation == token_.connection) {
    policy_.seal();
    stop_pending_ = true;
    connecting_ = false;
  }
}
bool Esp32X5Peripheral::poll(X5Input &out) {
  Guard lock(*this);
  if (critical_pending_) {
    out = critical_;
    critical_pending_ = false;
    return true;
  }
  return mailbox_.poll(out);
}
bool Esp32X5Peripheral::takeLoss(uint32_t generation) {
  Guard lock(*this);
  if (generation != token_.connection)
    return false;
  const bool out = loss_notice_ || mailbox_.takeLoss(generation);
  loss_notice_ = false;
  return out;
}
bool Esp32X5Peripheral::released(uint32_t generation) const {
  Guard lock(const_cast<Esp32X5Peripheral &>(*this));
  return generation == token_.connection && !reserved_ && !callback_refs_.load() &&
         policy_.released();
}
void Esp32X5Peripheral::service(uint32_t time) {
  Guard lock(*this);
  const auto &host = Esp32BleHost::instance();
  if (connecting_ && due(time, deadline_)) {
    policy_.seal();
    connecting_ = false;
    stop_pending_ = true;
    emit(X5InputKind::Fault, BLE_HS_ETIMEOUT);
  }
  if (startup_called_ && host.state() == BleHostState::Failed && !terminal_marked_) {
    policy_.seal();
    stop_pending_ = true;
    emit(X5InputKind::Fault, host.error());
  }
}
void Esp32X5Peripheral::enqueue(const X5Input &e) {
  if (!mailbox_.push(e)) {
    loss_notice_ = loss_revocation_ = true;
    policy_.loss();
    stop_pending_ = true;
  }
  if (e.kind == X5InputKind::Fault || e.kind == X5InputKind::Disconnected) {
    critical_ = e; // Independent terminal slot survives a full regular ring.
    critical_pending_ = true;
  }
}
void Esp32X5Peripheral::emit(X5InputKind kind, int status, uint32_t operation, uint32_t cutoff) {
  X5Input e;
  e.kind = kind;
  e.connection = token_.connection;
  e.operation = operation ? operation : token_.operation;
  e.received_ms = now();
  e.handle = cleanup_handle_;
  e.status = status;
  e.identity = qualification_.identity;
  if (kind == X5InputKind::SendReturned) {
    e.sequence = cutoff;
  } else if (sequence_ != UINT32_MAX) {
    e.sequence = ++sequence_;
  } else {
    loss_notice_ = loss_revocation_ = true;
    policy_.loss();
    stop_pending_ = true;
    return;
  }
  enqueue(e);
}
int Esp32X5Peripheral::registration(void *arg) {
  return static_cast<Esp32X5Peripheral *>(arg)->registerServices();
}
int Esp32X5Peripheral::registerServices() {
  // Retains the HAL's strong btInUse before Arduino boot can release BT memory.
  (void)btStarted(); // Status only; never another controller/NVS initialization.
  ble_svc_gap_init();
  ble_svc_gatt_init();
  int rc = ble_svc_gap_device_name_set("Insta360 GPS Remote");
  const uint16_t uuids[] = {0xce81, 0xce82, 0xce83, 0xffd1, 0xffd2, 0xffd3,
                            0xffd4, 0xffd5, 0xffd8, 0xfff1, 0xfff2, 0xffe0};
  const uint16_t flags[] = {BLE_GATT_CHR_F_WRITE, BLE_GATT_CHR_F_NOTIFY, BLE_GATT_CHR_F_READ,
                            BLE_GATT_CHR_F_WRITE, BLE_GATT_CHR_F_READ,   BLE_GATT_CHR_F_READ,
                            BLE_GATT_CHR_F_READ,  BLE_GATT_CHR_F_READ,   BLE_GATT_CHR_F_WRITE,
                            BLE_GATT_CHR_F_READ,  BLE_GATT_CHR_F_WRITE,  BLE_GATT_CHR_F_READ};
  for (size_t i = 0; i < attributes_.size(); ++i) {
    auto &a = attributes_[i];
    a.uuid = BLE_UUID16_INIT(uuids[i]);
    a.flags = flags[i];
    a.owner = this;
    auto &c = i < 3 ? remote_chars_[i] : extra_chars_[i - 3];
    c.uuid = &a.uuid.u;
    c.flags = a.flags;
    c.access_cb = access;
    c.arg = &a;
    c.val_handle = &a.handle;
  }
  attributes_[2].read = {{0x01, 0x02, 0, 0}};
  attributes_[2].read_size = 2;
  attributes_[5].read = {{0x01, 0x90, 0x1e, 0x30}};
  attributes_[5].read_size = 4;
  attributes_[6].read = {{0x01, 0x20, 0x00, 0x18}};
  attributes_[6].read_size = 4;
  services_[0].type = services_[1].type = BLE_GATT_SVC_TYPE_PRIMARY;
  services_[0].uuid = &remote_uuid_.u;
  services_[0].characteristics = remote_chars_.data();
  services_[1].uuid = &extra_uuid_.u;
  services_[1].characteristics = extra_chars_.data();
  if (!rc)
    rc = ble_gatts_count_cfg(services_.data());
  if (!rc)
    rc = ble_gatts_add_svcs(services_.data());
  return rc;
}
int Esp32X5Peripheral::access(uint16_t connection, uint16_t handle, ble_gatt_access_ctxt *ctx,
                              void *arg) {
  auto &attribute = *static_cast<Attribute *>(arg);
  auto &p = *attribute.owner;
  CallbackGuard callback(p);
  Guard lock(p);
  if (!p.policy_.alive() || connection != p.cleanup_handle_ || handle != attribute.handle)
    return BLE_ATT_ERR_UNLIKELY;
  if (ctx->op == BLE_GATT_ACCESS_OP_READ_CHR)
    return os_mbuf_append(ctx->om, attribute.read.data(), attribute.read_size) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
  if (ctx->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
    return BLE_ATT_ERR_UNLIKELY;
  if (attribute.uuid.value != 0xce81)
    return 0; // Opaque additional-service writes never supply recording evidence.
  const unsigned size = OS_MBUF_PKTLEN(ctx->om);
  if (size > p.access_input_.bytes.size() ||
      os_mbuf_copydata(ctx->om, 0, size, p.access_input_.bytes.data()) != 0 ||
      p.sequence_ == UINT32_MAX) {
    p.loss_notice_ = p.loss_revocation_ = true;
    p.policy_.loss();
    p.stop_pending_ = true;
    return BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  auto &e = p.access_input_;
  e.kind = X5InputKind::Display;
  e.connection = p.token_.connection;
  e.handle = connection; // Port inputs use connection handles, never attribute handles.
  e.size = size;
  e.sequence = ++p.sequence_;
  e.received_ms = now();
  p.enqueue(e);
  return 0;
}
int Esp32X5Peripheral::gap(ble_gap_event *event, void *arg) {
  auto &p = *static_cast<Esp32X5Peripheral *>(arg);
  CallbackGuard callback(p);
  // NimBLE lookup takes its host mutex. Never call it with interrupts disabled.
  ble_gap_conn_desc desc{};
  int lookup = 0;
  if (event->type == BLE_GAP_EVENT_CONNECT && !event->connect.status)
    lookup = ble_gap_conn_find(event->connect.conn_handle, &desc);
  Guard lock(p);
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT: {
    p.advertising_ = false;
    if (event->connect.status) {
      p.emit(X5InputKind::Fault, event->connect.status);
      p.terminal();
      break;
    }
    p.cleanup_handle_ = event->connect.conn_handle;
    const int rc = lookup;
    const auto &q = p.qualification_.identity;
    const uint8_t type = q.type == IdentityType::Public ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM;
    if (rc || desc.peer_id_addr.type != type ||
        std::memcmp(desc.peer_id_addr.val, q.address.data(), 6) != 0 ||
        !p.policy_.connected(p.cleanup_handle_, now())) {
      p.policy_.seal();
      p.stop_pending_ = true;
      p.emit(X5InputKind::Fault, rc ? rc : BLE_HS_EDISABLED);
      break;
    }
    if (!Esp32BleHost::instance().peripheralConnected(&p, p.token_.connection)) {
      p.policy_.seal();
      p.stop_pending_ = true;
      p.emit(X5InputKind::Fault, BLE_HS_EDISABLED);
      break;
    }
    p.emit(X5InputKind::Connected);
    break;
  }
  case BLE_GAP_EVENT_SUBSCRIBE:
    if (event->subscribe.conn_handle == p.cleanup_handle_ &&
        event->subscribe.attr_handle == p.attributes_[1].handle) {
      p.policy_.subscription(event->subscribe.cur_notify != 0);
      if (p.policy_.subscribed())
        p.connecting_ = false;
      p.emit(p.policy_.subscribed() ? X5InputKind::Subscribed : X5InputKind::Unsubscribed);
    }
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    if (event->disconnect.conn.conn_handle == p.cleanup_handle_) {
      p.emit(X5InputKind::Disconnected, event->disconnect.reason);
      p.cleanup_handle_ = kBleNoHandle;
      p.terminal();
    }
    break;
  case BLE_GAP_EVENT_ADV_COMPLETE:
    p.advertising_ = false;
    p.emit(X5InputKind::Fault, BLE_HS_ETIMEOUT);
    p.terminal();
    break;
  case BLE_GAP_EVENT_REPEAT_PAIRING:
    p.policy_.seal();
    p.stop_pending_ = true;
    p.emit(X5InputKind::Fault, BLE_HS_EDISABLED);
    return BLE_GAP_REPEAT_PAIRING_IGNORE;
  case BLE_GAP_EVENT_PASSKEY_ACTION:
  case BLE_GAP_EVENT_ENC_CHANGE:
    p.policy_.seal();
    p.stop_pending_ = true;
    p.emit(X5InputKind::Fault, BLE_HS_EDISABLED);
    break;
  default:
    break;
  }
  return 0;
}
void Esp32X5Peripheral::terminal() {
  connecting_ = false;
  stop_pending_ = false;
  if (!terminal_marked_) {
    terminal_marked_ = true;
    policy_.terminal();
  }
  if (!gap_entered_)
    policy_.barrier(); // No GATT/GAP callback reference was ever submitted.
  else
    queueBarrier();
}
void Esp32X5Peripheral::queueBarrier() {
  if (barrier_queued_)
    return;
  auto *event = &barrier_event_;
  event->queued = true;
  barrier_queued_ = true;
  if (xQueueSendToBack(nimble_port_get_dflt_eventq()->q, &event, 0) != pdPASS) {
    event->queued = false;
    barrier_queued_ = false; // Retain/quarantine until a real barrier can be queued.
  }
}
void Esp32X5Peripheral::barrier(ble_npl_event *event) {
  auto &p = *static_cast<Esp32X5Peripheral *>(ble_npl_event_get_arg(event));
  CallbackGuard callback(p);
  Guard lock(p);
  p.policy_.barrier();
  p.barrier_queued_ = false;
}
void Esp32X5Peripheral::cleanup() {
  uint16_t handle;
  bool stop;
  {
    Guard lock(*this);
    if (!stop_pending_)
      return;
    handle = cleanup_handle_;
    if (handle != kBleNoHandle) {
      if (terminate_sent_)
        return;
      terminate_sent_ = true;
      stop = false;
    } else if (advertising_) {
      if (stop_sent_)
        return;
      stop_sent_ = true;
      stop = true;
    } else {
      if (!gap_entered_)
        terminal();
      return;
    }
    policy_.sdkEnter();
  }
  const int rc = stop ? ble_gap_adv_stop() : ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
  {
    Guard lock(*this);
    policy_.sdkExit();
    if (stop && !rc) {
      advertising_ = false;
      terminal();
    } else if (rc && rc != BLE_HS_EALREADY && rc != BLE_HS_ENOTCONN) {
      emit(X5InputKind::Fault, rc);
    }
    // A missing SDK connection alone does not establish callback termination.
  }
}
void Esp32X5Peripheral::send() {
  X5ShutterRequest request;
  {
    Guard lock(*this);
    if (!policy_.pending(request))
      return;
    policy_.sdkEnter();
  }
  const auto bytes = insta360::encodeShutterEvent();
  auto *mbuf = ble_hs_mbuf_from_flat(bytes.data(), bytes.size());
  uint32_t cutoff = 0;
  bool admitted;
  {
    Guard lock(*this);
    admitted = mbuf && request.observation_sequence != 0 &&
               sequence_ == request.observation_sequence &&
               now() - request.observation_ms <= 5000 && !loss_revocation_ && !stop_pending_ &&
               Esp32BleHost::instance().fault() == BleFault::None &&
               nvsBootStatus().persistenceAllowed() && policy_.admit(now());
    cutoff = sequence_;
    if (!admitted)
      policy_.cancel(request.token);
  }
  int rc = BLE_HS_EDISABLED;
  if (admitted)
    rc = ble_gatts_notify_custom(request.handle, attributes_[1].handle, mbuf);
  else if (mbuf)
    os_mbuf_free_chain(mbuf);
  {
    Guard lock(*this);
    policy_.sdkExit();
    emit(X5InputKind::SendReturned, admitted ? rc : BLE_HS_EDISABLED, request.token.operation,
         cutoff);
  }
}
void Esp32X5Peripheral::workerStep() {
  auto &host = Esp32BleHost::instance();
  bool start = false;
  {
    Guard lock(*this);
    if (connecting_ && !startup_called_ && !stop_pending_) {
      startup_called_ = true;
      policy_.sdkEnter();
      start = true;
    }
  }
  if (start) {
    const auto state = host.start(true, true);
    Guard lock(*this);
    policy_.sdkExit();
    if (state == BleHostState::Failed) {
      emit(X5InputKind::Fault, host.error());
      policy_.seal();
      stop_pending_ = true;
    }
  }
  bool advertise = false;
  {
    Guard lock(*this);
    if (connecting_ && !stop_pending_ && !advertising_ && !gap_entered_ &&
        host.state() == BleHostState::Ready && !due(now(), deadline_)) {
      if (!reserved_)
        reserved_ = host.reservePeripheral(this, token_.connection);
      advertise = reserved_;
      if (advertise)
        policy_.sdkEnter();
    }
  }
  if (advertise) {
    int rc = ble_gap_adv_set_data(kAdvertisement, sizeof kAdvertisement);
    if (!rc)
      rc = ble_gap_adv_rsp_set_data(kName, sizeof kName);
    bool admit;
    uint32_t remaining = 0;
    {
      Guard lock(*this);
      admit = !rc && connecting_ && !stop_pending_ && !due(now(), deadline_) &&
              host.fault() == BleFault::None && nvsBootStatus().persistenceAllowed();
      if (admit) {
        remaining = deadline_ - now();
        gap_entered_ = advertising_ = true;
      }
    }
    if (admit) {
      ble_gap_adv_params params{};
      params.conn_mode = BLE_GAP_CONN_MODE_UND;
      params.disc_mode = BLE_GAP_DISC_MODE_GEN;
      rc = ble_gap_adv_start(host.wakeOwnAddressType(), nullptr, remaining, &params, gap, this);
    } else if (!rc) {
      rc = BLE_HS_EDISABLED;
    }
    Guard lock(*this);
    policy_.sdkExit();
    if (rc) {
      advertising_ = false;
      emit(X5InputKind::Fault, rc);
      policy_.seal();
      terminal(); // A failed start acquired no ongoing advertising procedure.
    }
  }
  send();
  cleanup();
  {
    Guard lock(*this);
    if (terminal_marked_ && gap_entered_ && !barrier_queued_ && !policy_.released())
      queueBarrier();
    if (reserved_ && policy_.released() && !callback_refs_.load())
      if (host.releasePeripheral(this, token_.connection))
        reserved_ = false;
  }
}
void Esp32X5Peripheral::worker(void *arg) {
  auto &p = *static_cast<Esp32X5Peripheral *>(arg);
  for (;;) {
    p.workerStep();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
} // namespace ridesync
extern "C" ridesync::X5PeripheralPort *ridesync_x5_peripheral_backend() {
  static ridesync::Esp32X5Peripheral port;
  return &port;
}
#endif

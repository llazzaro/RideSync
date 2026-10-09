#include "insta360_wake_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <nimble/nimble/host/include/host/ble_gap.h>
#include <nimble/porting/nimble/include/nimble/nimble_port.h>
namespace ridesync {
Esp32Insta360Wake &insta360WakeRadio() {
  static Esp32Insta360Wake radio;
  return radio;
}
bool Esp32Insta360Wake::activate(bool opt_in) {
  auto &host = Esp32BleHost::instance();
  BleStoreProof proof;
  if (!opt_in || host.state() != BleHostState::Ready || host.fault() != BleFault::None ||
      !host.admittedProof(proof))
    return false;
  if (active_)
    return true;
  lease_.owner = this;
  ble_npl_event_init(&lease_.barrier, barrier, &lease_);
  TaskHandle_t task = nullptr;
  active_ = xTaskCreate(worker, "ridesync_wake", 4096, this, 2, &task) == pdPASS;
  return active_;
}
WakeSubmit Esp32Insta360Wake::begin(const WakeOperation &op, const insta360::WakeEncoding &bytes,
                                    uint32_t deadline, uint32_t now) {
  if (control_.test_and_set(std::memory_order_acquire))
    return WakeSubmit::Busy;
  WakeSubmit result = WakeSubmit::Unsupported;
  if (active_ && bytes.error == insta360::WakeCodecError::None) {
    result = WakeSubmit::Busy;
    if (!occupied_) {
      result = Esp32BleHost::instance().reserveWake(op, deadline, now);
      if (result == WakeSubmit::Accepted) {
        lease_.operation = op;
        lease_.bytes = bytes;
        lease_.deadline = deadline;
        lease_.connection.store(kBleNoHandle);
        lease_.complete.store(false);
        prepared_ = started_ = stop_attempted_ = terminate_attempted_ = false;
        occupied_ = true;
        worker_done_ = false;
        work_phase_.store(1, std::memory_order_release);
      }
    }
  }
  control_.clear(std::memory_order_release);
  return result;
}
void Esp32Insta360Wake::cancel(const WakeOperation &op) { Esp32BleHost::instance().sealWake(op); }
WakeRadioResult Esp32Insta360Wake::poll(const WakeOperation &op, uint32_t now) {
  auto &host = Esp32BleHost::instance();
  auto result = host.wakeStatus();
  if (!sameWakeOperation(result.operation, op) || !host.wakeReserved())
    return result;
  if (static_cast<int32_t>(now - lease_.deadline) >= 0)
    host.sealWake(op);
  result.released = false;
  if (control_.test_and_set(std::memory_order_acquire))
    return result;
  unsigned finished = 3;
  if (work_phase_.compare_exchange_strong(finished, 4, std::memory_order_acq_rel)) {
    // Claim final release before inspecting other owners. A late incoming
    // callback can retain the lease, but the worker cannot race payload reuse.
    if (!lease_.queued.load() && !lease_.barrier_running.load() && !lease_.callbacks.load() &&
        lease_.connection.load() == kBleNoHandle && host.releaseWake(op)) {
      occupied_ = false;
      work_phase_.store(0, std::memory_order_release);
      result.released = true;
    } else {
      work_phase_.store(3, std::memory_order_release);
    }
  }
  control_.clear(std::memory_order_release);
  return result;
}
bool Esp32Insta360Wake::open(uint32_t now) const {
  return !Esp32BleHost::instance().wakeCancelled() &&
         static_cast<int32_t>(now - lease_.deadline) < 0 && !lease_.complete.load();
}
void Esp32Insta360Wake::worker(void *argument) {
  auto &self = *static_cast<Esp32Insta360Wake *>(argument);
  for (;;) {
    unsigned waiting = 1;
    bool acquired = self.work_phase_.compare_exchange_strong(waiting, 2, std::memory_order_acq_rel);
    if (!acquired && self.lease_.connection.load(std::memory_order_acquire) != kBleNoHandle) {
      unsigned finished = 3;
      acquired = self.work_phase_.compare_exchange_strong(finished, 2, std::memory_order_acq_rel);
    }
    if (acquired) {
      self.cycle();
      self.work_phase_.store(self.worker_done_ ? 3 : 1, std::memory_order_release);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
void Esp32Insta360Wake::finish() {
  auto &host = Esp32BleHost::instance();
  host.wakeTerminal(lease_.operation);
  queueBarrier();
  worker_done_ = true;
}
void Esp32Insta360Wake::cycle() {
  auto &host = Esp32BleHost::instance();
  const auto &op = lease_.operation;
  if (worker_done_) {
    const auto late = lease_.connection.load(std::memory_order_acquire);
    if (late != kBleNoHandle && !terminate_attempted_) {
      terminate_attempted_ = true;
      if (ble_gap_terminate(late, BLE_ERR_REM_USER_CONN_TERM))
        host.quarantineWake();
    }
    return;
  }
  if (!prepared_) {
    prepared_ = true;
    int rc = 0;
    if (open(millis()))
      rc = ble_gap_adv_set_data(lease_.bytes.advertisement.data(), 31);
    if (!rc && open(millis()))
      rc = ble_gap_adv_rsp_set_data(lease_.bytes.scan_response.data(), 25);
    const uint32_t now = millis();
    if (!rc && open(now) && host.admitWakeStart(op, now)) {
      ble_gap_adv_params params{};
      params.conn_mode = BLE_GAP_CONN_MODE_UND;
      params.disc_mode = BLE_GAP_DISC_MODE_GEN;
      rc = ble_gap_adv_start(host.wakeOwnAddressType(), nullptr, lease_.deadline - now, &params,
                             gap, &lease_);
      started_ = !rc;
    }
    host.wakeReturned(op, rc);
    if (!started_) {
      finish();
      return;
    }
  }
  const auto connection = lease_.connection.load(std::memory_order_acquire);
  if (connection != kBleNoHandle && !terminate_attempted_) {
    terminate_attempted_ = true;
    if (ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM))
      host.quarantineWake();
  }
  if (lease_.complete.load(std::memory_order_acquire)) {
    finish();
    return;
  }
  if (!open(millis()) && !stop_attempted_) {
    stop_attempted_ = true;
    if (ble_gap_adv_stop()) {
      host.quarantineWake();
      return;
    }
    finish();
  }
}
void Esp32Insta360Wake::queueBarrier() {
  if (lease_.queued.exchange(true, std::memory_order_acq_rel))
    return;
  auto *event = &lease_.barrier;
  // Pinned NPL event has a queued flag; publish with zero wait.
  event->queued = true;
  if (xQueueSendToBack(nimble_port_get_dflt_eventq()->q, &event, 0) != pdPASS) {
    Esp32BleHost::instance().quarantineWake();
    // Keep queued latched: failed publication is never release evidence.
  }
}
void Esp32Insta360Wake::barrier(ble_npl_event *event) {
  auto &lease = *static_cast<Lease *>(ble_npl_event_get_arg(event));
  lease.barrier_running.store(true, std::memory_order_release);
  Esp32BleHost::instance().wakeBarrierReleased(lease.operation);
  lease.queued.store(false, std::memory_order_release);
  lease.barrier_running.store(false, std::memory_order_release);
}
int Esp32Insta360Wake::gap(ble_gap_event *event, void *argument) {
  auto &lease = *static_cast<Lease *>(argument);
  auto &host = Esp32BleHost::instance();
  lease.callbacks.fetch_add(1, std::memory_order_acq_rel);
  host.wakeCallbackEnter();
  int result = 0;
  switch (event->type) {
  case BLE_GAP_EVENT_ADV_COMPLETE:
    lease.complete.store(true, std::memory_order_release);
    host.wakeTerminal(lease.operation);
    break;
  case BLE_GAP_EVENT_CONNECT:
    if (host.wakeIdentify(lease.operation, event->connect.conn_handle, !event->connect.status))
      lease.connection.store(event->connect.conn_handle, std::memory_order_release);
    lease.complete.store(true, std::memory_order_release);
    host.wakeTerminal(lease.operation);
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    host.wakeDisconnected(lease.operation, event->disconnect.conn.conn_handle);
    if (lease.connection.load() == event->disconnect.conn.conn_handle)
      lease.connection.store(kBleNoHandle, std::memory_order_release);
    break;
  case BLE_GAP_EVENT_REPEAT_PAIRING:
    result = BLE_GAP_REPEAT_PAIRING_IGNORE;
    break;
  case BLE_GAP_EVENT_PASSKEY_ACTION:
  case BLE_GAP_EVENT_ENC_CHANGE:
    host.sealWake(lease.operation);
    result = BLE_HS_ENOTSUP;
    break;
  default:
    break;
  }
  host.wakeCallbackExit();
  lease.owner->queueBarrier();
  lease.callbacks.fetch_sub(1, std::memory_order_release);
  return result;
}
} // namespace ridesync
extern "C" ridesync::Esp32Insta360Wake *ridesync_insta360_wake_backend(bool opt_in) {
  auto &radio = ridesync::insta360WakeRadio();
  radio.activate(opt_in);
  return &radio;
}
#endif

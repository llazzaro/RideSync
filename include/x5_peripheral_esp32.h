#pragma once
#include "x5_peripheral_policy.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "ble_esp32.h"
#include <freertos/FreeRTOS.h>
namespace ridesync {
// Services, callback contexts and worker remain alive for the entire boot.
class Esp32X5Peripheral final : public X5PeripheralPort {
public:
  bool configure(const X5Qualification &) override;
  bool connect(Token, uint32_t deadline_ms) override;
  bool notify(const X5ShutterRequest &) override;
  void cancel(Token) override;
  void close(uint32_t connection) override;
  bool poll(X5Input &) override;
  bool takeLoss(uint32_t connection) override;
  bool released(uint32_t connection) const override;
  void service(uint32_t now_ms) override;

private:
  struct CallbackGuard;
  struct Guard;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  std::atomic<unsigned> callback_refs_{0};
  PeripheralMailbox mailbox_;
  X5PeripheralPolicy policy_;
  X5Qualification qualification_;
  Token token_;
  uint32_t deadline_ = 0, sequence_ = 0;
  uint16_t cleanup_handle_ = kBleNoHandle;
  bool configured_ = false, worker_created_ = false, startup_called_ = false, connecting_ = false,
       reserved_ = false, advertising_ = false, gap_entered_ = false, stop_pending_ = false,
       stop_sent_ = false, terminate_sent_ = false, terminal_marked_ = false,
       barrier_queued_ = false, loss_notice_ = false, loss_revocation_ = false,
       critical_pending_ = false;
  X5Input critical_, access_input_;
  ble_npl_event barrier_event_{};
  ble_uuid16_t remote_uuid_ = BLE_UUID16_INIT(0xce80);
  ble_uuid128_t extra_uuid_ = BLE_UUID128_INIT(0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e, 0x93,
                                               0xd2, 0x17, 0x3c, 0xff, 0xd0, 0x00, 0x00);
  struct Attribute {
    ble_uuid16_t uuid;
    uint16_t flags = 0;
    std::array<uint8_t, 4> read{};
    uint8_t read_size = 0;
    uint16_t handle = 0;
    Esp32X5Peripheral *owner = nullptr;
  };
  std::array<Attribute, 12> attributes_{};
  std::array<ble_gatt_chr_def, 4> remote_chars_{};
  std::array<ble_gatt_chr_def, 10> extra_chars_{};
  std::array<ble_gatt_svc_def, 3> services_{};
  static int registration(void *);
  int registerServices();
  static int access(uint16_t, uint16_t, ble_gatt_access_ctxt *, void *);
  static int gap(ble_gap_event *, void *);
  static void barrier(ble_npl_event *);
  static void worker(void *);
  void workerStep();
  void emit(X5InputKind, int status = 0, uint32_t operation = 0, uint32_t cutoff = 0);
  void enqueue(const X5Input &);
  void terminal();
  void queueBarrier();
  void cleanup();
  void send();
  static uint32_t now();
};
} // namespace ridesync
extern "C" ridesync::X5PeripheralPort *ridesync_x5_peripheral_backend();
#endif

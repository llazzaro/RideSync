#include "bench_application.h"
namespace ridesync {
bool validateBenchMotionMetadata(const MotionAdmissionConfig &motion, const ImuConfig &metadata) {
  if (!motion.requested)
    return true;
  const auto &m = motion.estimator;
  if (!motionAdmissionQualified(motion) || metadata.mount_state != Qualification::Qualified ||
      metadata.calibration_state != Qualification::Qualified || metadata.mount_id != m.mount_id ||
      metadata.calibration_id != m.calibration_id || !metadata.calibration_offsets_known ||
      !metadata.calibration_gains_known ||
      metadata.accel_offset_compensation != m.accel_compensation ||
      metadata.gyro_offset_compensation != m.gyro_compensation || m.accel_compensation != 1 ||
      m.gyro_compensation != 1)
    return false;
  // The selected BMI270 raw route reads back both device compensations disabled.
  // Mount/calibration facts and residual count gains must match that delivered profile.
  for (unsigned i = 0; i < 3; ++i)
    if (!metadata.accel_gain_numerator[i] || !metadata.accel_gain_denominator[i] ||
        !metadata.gyro_gain_numerator[i] || !metadata.gyro_gain_denominator[i])
      return false;
  return true;
}
bool BenchDynamicReference::arm(uint32_t now_ms) {
  const auto elapsed = now_ms - receipt_cutoff_ms_;
  if (armed_ && (elapsed <= 1000 || elapsed >= 0x80000000UL))
    return false;
  armed_ = false;
  if (declaration_ == UINT32_MAX)
    return false;
  ++declaration_;
  armed_ = true;
  receipt_cutoff_ms_ = now_ms;
  return true;
}
DynamicMotionReference BenchDynamicReference::referenceFor(const ImuEvidence &e) {
  if (!armed_ || !e.receipt_known)
    return {};
  const auto elapsed = e.receipt_millis32 - receipt_cutoff_ms_;
  if (elapsed >= 0x80000000UL)
    return {}; // Queued pre-command samples cannot consume operator stationarity.
  if (elapsed > 1000) {
    armed_ = false;
    return {};
  }
  if (e.kind != RecordKind::ImuSample || (e.timing_flags & 7))
    return {};
  StaticMotionReference reference;
  reference.externally_stationary = true;
  reference.session_id = e.session_id;
  reference.config_generation = e.config.generation;
  reference.batch_sequence = e.batch_sequence;
  reference.declaration = declaration_;
  MotionEstimator conversion(config_);
  if (!conversion.update(e, reference).static_tilt_valid)
    return {};
  armed_ = false;
  DynamicMotionReference out;
  out.externally_stationary = true;
  out.declaration = declaration_;
  return out;
}
namespace {
bool pinValid(int pin, bool output) {
  return pin >= 0 && pin <= (output ? 33 : 39) && pin != 0 && pin != 1 && pin != 3 &&
         !(pin >= 6 && pin <= 11) && pin != 16 && pin != 17 && pin != 20 && pin != 24 &&
         !(pin >= 28 && pin <= 31);
}
} // namespace
bool validateBenchBusPins(const BenchBusPins &p) {
  uint64_t used = 0;
  const auto add = [&used](int pin, bool output) {
    if (!pinValid(pin, output) || (used & (uint64_t(1) << pin)))
      return false;
    used |= uint64_t(1) << pin;
    return true;
  };
  if (!add(p.power_enable, true) || !add(p.spi_sck, true) || !add(p.spi_miso, false) ||
      !add(p.spi_mosi, true) || !add(p.spi_cs, true) || !add(p.modem_tx, true) ||
      !add(p.modem_rx, false))
    return false;
  if (p.imu_enabled && (!add(p.i2c_sda, true) || !add(p.i2c_scl, true)))
    return false;
  if (p.modem_key != -1 && !add(p.modem_key, true))
    return false;
  if (p.button != -1 && !add(p.button, false))
    return false;
  for (int pin : p.led)
    if (pin != -1 && !add(pin, true))
      return false;
  return true;
}
} // namespace ridesync
#if defined(ARDUINO_ARCH_ESP32) && defined(RIDESYNC_BENCH_APPLICATION)
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <new>
#if defined(RIDESYNC_BENCH_PRIVATE_HEADER)
#include RIDESYNC_BENCH_PRIVATE_HEADER
#define RIDESYNC_HAS_BENCH_PROVIDER 1
#elif defined(__has_include)
#if __has_include("bench_commissioning.local.h")
#include "bench_commissioning.local.h"
#define RIDESYNC_HAS_BENCH_PROVIDER 1
#endif
#endif
namespace ridesync {
namespace {
BenchApplicationConfig config;
BenchProviderStatus status;
SupervisedEsp32Application *owner = nullptr;
void *application_memory = nullptr;
bool attempted = false;
HardwareSerial modem(2);
SPIClass storage_spi(HSPI);
TwoWire imu_wire(0);
BenchDynamicReference stationary_reference;
bool validConfig() {
  const auto &q = config.telemetry;
  Settings fixed;
  if (!config.enabled || !config.board_routing_reviewed || !copySettings(config.source, fixed) ||
      !q.runtime.opt_in || !q.runtime.gps_qualified || !q.runtime.firmware ||
      !q.runtime.provenance || !q.runtime.modem.documentary_profile_opt_in ||
      !q.runtime.modem.terminal_retires_transaction ||
      (!q.modem_already_powered && !q.modem_power_sequence) || !q.runtime.power_timing.qualified ||
      !q.modem.pins_qualified || !q.modem.documentary_profile_opt_in || !q.modem.baud ||
      !q.sd.opt_in || !q.sd.wiring_card_qualified || !q.sd.exclusive_volume ||
      !q.sd.namespace_commissioned || !q.sd.commissioned_namespace || !q.sd.frequency_hz ||
      q.sd.chip_select != config.pins.spi_cs || q.modem.tx != config.pins.modem_tx ||
      q.modem.rx != config.pins.modem_rx || q.runtime.imu_enabled != config.pins.imu_enabled)
    return false;
  if (q.modem_power_sequence &&
      (q.modem_already_powered || q.modem.key != config.pins.modem_key ||
       config.pins.modem_key == -1 || q.modem.supply != config.pins.power_enable ||
       q.modem.supply_active_high != config.power_enable_active_high ||
       !q.runtime.power_timing.key_active_ms || !q.runtime.power_timing.settle_ms ||
       q.runtime.power_timing.key_active_ms > 60000 || q.runtime.power_timing.settle_ms > 60000 ||
       q.runtime.power_timing.pre_key_ms > 60000))
    return false;
  if (!q.modem_power_sequence && config.pins.modem_key != -1)
    return false;
  if (q.runtime.imu_enabled &&
      (!q.runtime.imu_qualified || !q.imu.enabled || !q.imu.dedicated_bus ||
       !q.imu.electrically_qualified || !q.imu.sensor_id ||
       (q.imu.address != 0x68 && q.imu.address != 0x69) || !config.i2c_hz ||
       !config.i2c_timeout_ms))
    return false;
  const auto &h = config.handlebar;
  if (h.opt_in != config.source.button_gpio.enabled ||
      (h.opt_in && (!h.acknowledge_qualification || !validateButtonGpioConfig(h.button) ||
                    !validateButtonConfig(h.actions) || h.button.pin != config.pins.button ||
                    h.button.pin != config.source.button_gpio.pin ||
                    h.button.pull != config.source.button_gpio.pull ||
                    h.button.active_low != config.source.button_gpio.active_low)))
    return false;
  if (!h.opt_in && config.pins.button != -1)
    return false;
  const unsigned leds = h.led.mode == LedMode::Rgb ? 3 : h.led.mode == LedMode::Mono ? 1 : 0;
  for (unsigned i = 0; i < 3; ++i)
    if (config.pins.led[i] != (i < leds ? h.led.pins[i] : -1))
      return false;
  if (leds && (!h.opt_in || !h.led.board_qualified || !h.led.reservations_complete))
    return false;
  if (h.led.mode != LedMode::Disabled && h.led.mode != LedMode::Mono && h.led.mode != LedMode::Rgb)
    return false;
  if (leds && !validateLedWiring(h.led, h.acknowledge_qualification, h.button))
    return false;
  if (!leds)
    for (unsigned i = 0; i < 3; ++i)
      if (h.led.pins[i] != -1 || h.led.polarity[i] != LedPolarity::Unspecified)
        return false;
  if (q.runtime.motion_enabled || q.runtime.dynamic_motion.enabled) {
    MotionAdmissionConfig motion;
    motion.requested = true;
    motion.imu_qualified = q.runtime.imu_enabled && q.runtime.imu_qualified;
    motion.estimator = q.runtime.motion_config;
    motion.snapshot_max_age_ms = q.runtime.motion_snapshot_max_age_ms;
    motion.dynamic = q.runtime.dynamic_motion;
    motion.dynamic_cadence_us = q.runtime.dynamic_cadence_us;
    motion.dynamic_reference = q.runtime.dynamic_reference;
    if (!validateBenchMotionMetadata(motion, q.metadata))
      return false;
  }
  if (q.runtime.cameras_qualified != (config.source.count != 0) ||
      q.runtime.peers.count != config.source.count)
    return false;
  for (size_t i = 0; i < config.source.count; ++i)
    if (!config.source.cameras[i].enabled || q.runtime.peers.entries[i].slot != i ||
        !q.runtime.peers.entries[i].id ||
        q.runtime.peers.entries[i].model != config.source.cameras[i].model)
      return false;
  return true;
}
} // namespace
const SourceConfig *benchApplicationDefaults() {
  return status.supplied ? &config.source : nullptr;
}
SupervisedEsp32Application *benchApplicationOwner() { return owner; }
BenchProviderStatus benchApplicationStatus() { return status; }
bool benchArmStationaryReference() {
  if (!owner || !owner->telemetry() || !config.telemetry.runtime.dynamic_motion.enabled ||
      config.telemetry.runtime.dynamic_reference != &stationary_reference)
    return false;
  const auto s = owner->telemetry()->status();
  if (s.phase != TelemetryPhase::Running || s.imu != SensorAdmission::Admitted ||
      s.motion_admission != MotionAdmission::Enabled || s.safe_mode || s.supervision_fault ||
      s.worker_stalls || s.worker_refused)
    return false;
  return stationary_reference.arm(millis());
}
ApplicationWorkers *commissionedApplication() {
  if (attempted)
    return owner;
  attempted = true;
#ifdef RIDESYNC_HAS_BENCH_PROVIDER
  status.supplied = true;
  if (!ridesyncPrivateBenchConfig(config)) {
    status.error = BenchProviderError::Declined;
    return nullptr;
  }
#else
  return nullptr;
#endif
  if (config.telemetry.runtime.dynamic_motion.enabled &&
      !config.telemetry.runtime.dynamic_reference) {
    stationary_reference.configure(config.telemetry.runtime.motion_config);
    config.telemetry.runtime.dynamic_reference = &stationary_reference;
  }
  if (!validConfig()) {
    status.error = BenchProviderError::Configuration;
    return nullptr;
  }
  if (!validateBenchBusPins(config.pins)) {
    status.error = BenchProviderError::PinConflict;
    return nullptr;
  }
  // Keep the large boot-lifetime application in checked internal RAM. Allocation
  // failure precedes peripheral IO; never retry or free active owner resources.
  application_memory =
      heap_caps_malloc(sizeof(SupervisedEsp32Application), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!application_memory) {
    status.error = BenchProviderError::Allocation;
    return nullptr;
  }
  // Set the declared output level before enabling its driver; no modem reset/pulse.
  digitalWrite(config.pins.power_enable, config.power_enable_active_high ? HIGH : LOW);
  pinMode(config.pins.power_enable, OUTPUT);
  storage_spi.begin(config.pins.spi_sck, config.pins.spi_miso, config.pins.spi_mosi,
                    config.pins.spi_cs);
  status.spi_initialized = true; // SPI begin is void; this is invocation, not card readiness.
  if (config.pins.imu_enabled) {
    if (imu_wire.setBufferSize(128) < 128) {
      status.error = BenchProviderError::I2cBuffer;
      return nullptr;
    }
    imu_wire.setTimeOut(config.i2c_timeout_ms);
    if (!imu_wire.begin(config.pins.i2c_sda, config.pins.i2c_scl, config.i2c_hz)) {
      status.error = BenchProviderError::I2cStart;
      return nullptr;
    }
    status.i2c_initialized = true;
  }
  owner = new (application_memory)
      SupervisedEsp32Application(modem, storage_spi, imu_wire, config.telemetry, config.handlebar,
                                 config.cameras, config.motion_reference);
  status.configured = true;
  status.error = BenchProviderError::None;
  return owner;
}
} // namespace ridesync
#endif

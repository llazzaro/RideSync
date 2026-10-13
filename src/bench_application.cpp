#include "bench_application.h"
namespace ridesync {
namespace {
bool metadataIdentityQualified(const MotionEstimatorConfig &m, const ImuConfig &metadata) {
  return metadata.mount_state == Qualification::Qualified &&
         metadata.calibration_state == Qualification::Qualified &&
         metadata.mount_id == m.mount_id && metadata.calibration_id == m.calibration_id &&
         metadata.calibration_offsets_known && metadata.calibration_gains_known;
}
bool metadataCompensationQualified(const MotionEstimatorConfig &m, const ImuConfig &metadata) {
  return metadata.accel_offset_compensation == m.accel_compensation &&
         metadata.gyro_offset_compensation == m.gyro_compensation && m.accel_compensation == 1 &&
         m.gyro_compensation == 1;
}
bool metadataGainsQualified(const ImuConfig &metadata) {
  // The selected BMI270 raw route reads back both device compensations disabled.
  // Mount/calibration facts and residual count gains must match that delivered profile.
  for (unsigned i = 0; i < 3; ++i)
    if (!metadata.accel_gain_numerator[i] || !metadata.accel_gain_denominator[i] ||
        !metadata.gyro_gain_numerator[i] || !metadata.gyro_gain_denominator[i])
      return false;
  return true;
}
} // namespace
bool validateBenchMotionMetadata(const MotionAdmissionConfig &motion, const ImuConfig &metadata) {
  if (!motion.requested)
    return true;
  return motionAdmissionQualified(motion) &&
         metadataIdentityQualified(motion.estimator, metadata) &&
         metadataCompensationQualified(motion.estimator, metadata) &&
         metadataGainsQualified(metadata);
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
  if (pin < 0 || pin > (output ? 33 : 39))
    return false;
  const int unavailable[] = {0, 1, 3, 6, 7, 8, 9, 10, 11, 16, 17, 20, 24, 28, 29, 30, 31};
  for (int blocked : unavailable)
    if (pin == blocked)
      return false;
  return true;
}
struct PinReservations {
  uint64_t used = 0;
  bool add(int pin, bool output) {
    if (!pinValid(pin, output) || (used & (uint64_t(1) << pin)))
      return false;
    used |= uint64_t(1) << pin;
    return true;
  }
};
bool requiredBusPins(const BenchBusPins &p, PinReservations &pins) {
  struct PinUse {
    int pin;
    bool output;
  };
  const PinUse required[] = {{p.power_enable, true}, {p.spi_sck, true}, {p.spi_miso, false},
                             {p.spi_mosi, true},     {p.spi_cs, true},  {p.modem_tx, true},
                             {p.modem_rx, false}};
  for (const auto &use : required)
    if (!pins.add(use.pin, use.output))
      return false;
  return true;
}
bool optionalBusPins(const BenchBusPins &p, PinReservations &pins) {
  if (p.modem_key != -1 && !pins.add(p.modem_key, true))
    return false;
  if (p.button != -1 && !pins.add(p.button, false))
    return false;
  for (int pin : p.led)
    if (pin != -1 && !pins.add(pin, true))
      return false;
  return true;
}
} // namespace
bool validateBenchBusPins(const BenchBusPins &p) {
  PinReservations pins;
  if (!requiredBusPins(p, pins))
    return false;
  if (p.imu_enabled && (!pins.add(p.i2c_sda, true) || !pins.add(p.i2c_scl, true)))
    return false;
  return optionalBusPins(p, pins);
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
bool runtimeDeclared(const QualifiedLocalTelemetry &q) {
  return q.runtime.opt_in && q.runtime.gps_qualified && q.runtime.firmware &&
         q.runtime.provenance && q.runtime.modem.documentary_profile_opt_in &&
         q.runtime.modem.terminal_retires_transaction &&
         (q.modem_already_powered || q.modem_power_sequence) && q.runtime.power_timing.qualified;
}
bool modemQualified(const QualifiedLocalTelemetry &q) {
  return q.modem.pins_qualified && q.modem.documentary_profile_opt_in && q.modem.baud;
}
bool storageQualified(const QualifiedLocalTelemetry &q) {
  return q.sd.opt_in && q.sd.wiring_card_qualified && q.sd.exclusive_volume &&
         q.sd.namespace_commissioned && q.sd.commissioned_namespace && q.sd.frequency_hz;
}
bool transportRoutingMatches(const BenchApplicationConfig &c) {
  const auto &q = c.telemetry;
  return q.sd.chip_select == c.pins.spi_cs && q.modem.tx == c.pins.modem_tx &&
         q.modem.rx == c.pins.modem_rx && q.runtime.imu_enabled == c.pins.imu_enabled;
}
bool powerTimingQualified(const QualifiedLocalTelemetry &q) {
  return q.runtime.power_timing.key_active_ms && q.runtime.power_timing.settle_ms &&
         q.runtime.power_timing.key_active_ms <= 60000 &&
         q.runtime.power_timing.settle_ms <= 60000 && q.runtime.power_timing.pre_key_ms <= 60000;
}
bool powerRoutingQualified(const BenchApplicationConfig &c) {
  const auto &q = c.telemetry;
  if (!q.modem_power_sequence)
    return c.pins.modem_key == -1;
  return !q.modem_already_powered && q.modem.key == c.pins.modem_key && c.pins.modem_key != -1 &&
         q.modem.supply == c.pins.power_enable &&
         q.modem.supply_active_high == c.power_enable_active_high && powerTimingQualified(q);
}
bool imuQualified(const BenchApplicationConfig &c) {
  const auto &q = c.telemetry;
  if (!q.runtime.imu_enabled)
    return true;
  return q.runtime.imu_qualified && q.imu.enabled && q.imu.dedicated_bus &&
         q.imu.electrically_qualified && q.imu.sensor_id &&
         (q.imu.address == 0x68 || q.imu.address == 0x69) && c.i2c_hz && c.i2c_timeout_ms;
}
bool telemetryQualified(const BenchApplicationConfig &c) {
  return runtimeDeclared(c.telemetry) && modemQualified(c.telemetry) &&
         storageQualified(c.telemetry) && transportRoutingMatches(c) && powerRoutingQualified(c) &&
         imuQualified(c);
}
bool buttonQualified(const BenchApplicationConfig &c) {
  const auto &h = c.handlebar;
  if (h.opt_in != c.source.button_gpio.enabled)
    return false;
  if (!h.opt_in)
    return c.pins.button == -1;
  return h.acknowledge_qualification && validateButtonGpioConfig(h.button) &&
         validateButtonConfig(h.actions) && h.button.pin == c.pins.button &&
         h.button.pin == c.source.button_gpio.pin && h.button.pull == c.source.button_gpio.pull &&
         h.button.active_low == c.source.button_gpio.active_low;
}
unsigned ledCount(LedMode mode) { return mode == LedMode::Rgb ? 3 : mode == LedMode::Mono ? 1 : 0; }
bool ledRoutingMatches(const BenchApplicationConfig &c, unsigned leds) {
  for (unsigned i = 0; i < 3; ++i)
    if (c.pins.led[i] != (i < leds ? c.handlebar.led.pins[i] : -1))
      return false;
  return true;
}
bool ledPolicyQualified(const QualifiedHandlebar &h, unsigned leds) {
  if (leds && (!h.opt_in || !h.led.board_qualified || !h.led.reservations_complete))
    return false;
  if (h.led.mode != LedMode::Disabled && h.led.mode != LedMode::Mono && h.led.mode != LedMode::Rgb)
    return false;
  return !leds || validateLedWiring(h.led, h.acknowledge_qualification, h.button);
}
bool disabledLedPinsCleared(const QualifiedHandlebar &h, unsigned leds) {
  if (leds)
    return true;
  for (unsigned i = 0; i < 3; ++i)
    if (h.led.pins[i] != -1 || h.led.polarity[i] != LedPolarity::Unspecified)
      return false;
  return true;
}
bool handlebarQualified(const BenchApplicationConfig &c) {
  if (!buttonQualified(c))
    return false;
  const unsigned leds = ledCount(c.handlebar.led.mode);
  return ledRoutingMatches(c, leds) && ledPolicyQualified(c.handlebar, leds) &&
         disabledLedPinsCleared(c.handlebar, leds);
}
bool motionQualified(const BenchApplicationConfig &c) {
  const auto &q = c.telemetry;
  if (!q.runtime.motion_enabled && !q.runtime.dynamic_motion.enabled)
    return true;
  MotionAdmissionConfig motion;
  motion.requested = true;
  motion.imu_qualified = q.runtime.imu_enabled && q.runtime.imu_qualified;
  motion.estimator = q.runtime.motion_config;
  motion.snapshot_max_age_ms = q.runtime.motion_snapshot_max_age_ms;
  motion.dynamic = q.runtime.dynamic_motion;
  motion.dynamic_cadence_us = q.runtime.dynamic_cadence_us;
  motion.dynamic_reference = q.runtime.dynamic_reference;
  return validateBenchMotionMetadata(motion, q.metadata);
}
bool peersQualified(const BenchApplicationConfig &c) {
  const auto &q = c.telemetry;
  if (q.runtime.cameras_qualified != (c.source.count != 0) ||
      q.runtime.peers.count != c.source.count)
    return false;
  for (size_t i = 0; i < c.source.count; ++i)
    if (!c.source.cameras[i].enabled || q.runtime.peers.entries[i].slot != i ||
        !q.runtime.peers.entries[i].id ||
        q.runtime.peers.entries[i].model != c.source.cameras[i].model)
      return false;
  return true;
}
bool validConfig() {
  Settings fixed;
  return config.enabled && config.board_routing_reviewed && copySettings(config.source, fixed) &&
         telemetryQualified(config) && handlebarQualified(config) && motionQualified(config) &&
         peersQualified(config);
}
bool stationaryReferenceConfigured() {
  return owner && owner->telemetry() && config.telemetry.runtime.dynamic_motion.enabled &&
         config.telemetry.runtime.dynamic_reference == &stationary_reference;
}
bool stationaryReferenceAdmitted(const LocalTelemetryStatus &s) {
  return s.phase == TelemetryPhase::Running && s.imu == SensorAdmission::Admitted &&
         s.motion_admission == MotionAdmission::Enabled && !s.safe_mode && !s.supervision_fault &&
         !s.worker_stalls && !s.worker_refused;
}
bool initializeBuses() {
  // Set the declared output level before enabling its driver; no modem reset/pulse.
  digitalWrite(config.pins.power_enable, config.power_enable_active_high ? HIGH : LOW);
  pinMode(config.pins.power_enable, OUTPUT);
  storage_spi.begin(config.pins.spi_sck, config.pins.spi_miso, config.pins.spi_mosi,
                    config.pins.spi_cs);
  status.spi_initialized = true; // SPI begin is void; this is invocation, not card readiness.
  if (config.pins.imu_enabled) {
    if (imu_wire.setBufferSize(128) < 128) {
      status.error = BenchProviderError::I2cBuffer;
      return false;
    }
    imu_wire.setTimeOut(config.i2c_timeout_ms);
    if (!imu_wire.begin(config.pins.i2c_sda, config.pins.i2c_scl, config.i2c_hz)) {
      status.error = BenchProviderError::I2cStart;
      return false;
    }
    status.i2c_initialized = true;
  }
  return true;
}
} // namespace
const BenchApplicationConfig *benchSetupConfiguration() {
  if (!attempted) {
    attempted = true; // This boot is setup-only; no later peripheral admission.
#ifdef RIDESYNC_HAS_BENCH_PROVIDER
    status.supplied = true;
    if (!ridesyncPrivateBenchConfig(config)) {
      status.error = BenchProviderError::Declined;
      return nullptr;
    }
    if (!validConfig()) {
      status.error = BenchProviderError::Configuration;
      return nullptr;
    }
    if (!validateBenchBusPins(config.pins)) {
      status.error = BenchProviderError::PinConflict;
      return nullptr;
    }
    status.error = BenchProviderError::None;
#else
    return nullptr;
#endif
  }
  return status.supplied && status.error == BenchProviderError::None ? &config : nullptr;
}
const SourceConfig *benchApplicationDefaults() {
  return status.supplied ? &config.source : nullptr;
}
SupervisedEsp32Application *benchApplicationOwner() { return owner; }
BenchProviderStatus benchApplicationStatus() { return status; }
bool benchArmStationaryReference() {
  if (!stationaryReferenceConfigured())
    return false;
  const auto s = owner->telemetry()->status();
  if (!stationaryReferenceAdmitted(s))
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
  if (!ensureMixedCameraRuntime()) {
    status.error = BenchProviderError::Allocation;
    return nullptr;
  }
  application_memory =
      heap_caps_malloc(sizeof(SupervisedEsp32Application), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!application_memory) {
    status.error = BenchProviderError::Allocation;
    return nullptr;
  }
  if (!initializeBuses())
    return nullptr;
  owner = new (application_memory)
      SupervisedEsp32Application(modem, storage_spi, imu_wire, config.telemetry, config.handlebar,
                                 config.cameras, config.motion_reference);
  status.configured = true;
  status.error = BenchProviderError::None;
  return owner;
}
} // namespace ridesync
#endif

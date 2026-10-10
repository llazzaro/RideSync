#pragma once
#include "motion_admission.h"
#include "status_led.h"
#include <array>
#include <cstdint>
namespace ridesync {
struct BenchBusPins {
  int power_enable = -1, spi_sck = -1, spi_miso = -1, spi_mosi = -1, spi_cs = -1;
  int modem_tx = -1, modem_rx = -1, modem_key = -1, i2c_sda = -1, i2c_scl = -1, button = -1;
  std::array<int, 3> led{{-1, -1, -1}};
  bool imu_enabled = false;
};
bool validateBenchBusPins(const BenchBusPins &);
bool validateBenchMotionMetadata(const MotionAdmissionConfig &, const ImuConfig &);
class BenchDynamicReference final : public DynamicMotionReferenceSource {
public:
  void configure(const MotionEstimatorConfig &config) {
    config_ = config;
    armed_ = false;
  }
  bool arm(uint32_t now_ms);
  DynamicMotionReference referenceFor(const ImuEvidence &) override;

private:
  MotionEstimatorConfig config_;
  uint32_t declaration_ = 0;
  uint32_t receipt_cutoff_ms_ = 0;
  bool armed_ = false;
};
} // namespace ridesync
#if defined(ARDUINO_ARCH_ESP32)
#include "application_esp32.h"
#include "profiles/mixed_camera_esp32.h"
namespace ridesync {
struct BenchApplicationConfig {
  bool enabled = false, board_routing_reviewed = false;
  bool power_enable_active_high = false;
  BenchBusPins pins;
  uint32_t i2c_hz = 0;
  uint16_t i2c_timeout_ms = 0;
  SourceConfig source;
  QualifiedLocalTelemetry telemetry;
  QualifiedHandlebar handlebar;
  MixedCameraQualifications cameras;
  StaticMotionReferenceSource *motion_reference = nullptr;
};
enum class BenchProviderError : uint8_t {
  None,
  MissingProvider,
  Declined,
  Configuration,
  PinConflict,
  I2cStart,
  I2cBuffer,
  Allocation
};
struct BenchProviderStatus {
  bool supplied = false, configured = false, spi_initialized = false, i2c_initialized = false;
  BenchProviderError error = BenchProviderError::MissingProvider;
};
// The private provider supplies real commissioning facts and stable provenance strings.
// No ledger provisioning, settings persistence or camera IO occurs in this entrypoint.
const SourceConfig *benchApplicationDefaults();
SupervisedEsp32Application *benchApplicationOwner();
BenchProviderStatus benchApplicationStatus();
bool benchArmStationaryReference();
} // namespace ridesync
#endif

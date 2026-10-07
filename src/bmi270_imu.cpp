#ifdef ARDUINO
#include "bmi270_imu.h"
#include <Arduino.h>
namespace ridesync {
Bmi270Imu::Bmi270Imu(TwoWire &w, const Bmi270Qualification &q)
    : wire_(w), qualification_(q), bus_(*this) {}
bool Bmi270Imu::selectRegister(uint8_t reg) {
  wire_.beginTransmission(qualification_.address);
  const bool wrote = wire_.write(reg) == 1;
  const uint8_t error = wire_.endTransmission(wrote ? false : true);
  return wrote && error == 0;
}
uint32_t Bmi270Imu::receive(uint8_t *p, uint32_t n) {
  uint32_t got = wire_.requestFrom(qualification_.address, static_cast<size_t>(n), true),
           copied = 0;
  while (wire_.available() && copied < n)
    p[copied++] = static_cast<uint8_t>(wire_.read());
  return got == n && copied == n ? n : 0;
}
uint32_t Bmi270Imu::transmit(uint8_t reg, const uint8_t *p, uint32_t n) {
  wire_.beginTransmission(qualification_.address);
  size_t a = wire_.write(reg), b = wire_.write(p, n);
  uint8_t error = wire_.endTransmission(true);
  return a == 1 && b == n && error == 0 ? n : 0;
}
int8_t Bmi270Imu::readCallback(uint8_t r, uint8_t *p, uint32_t n, void *self) {
  return static_cast<Bmi270Imu *>(self)->bus_.read(r, p, n) ? 0 : -1;
}
int8_t Bmi270Imu::writeCallback(uint8_t r, const uint8_t *p, uint32_t n, void *self) {
  return static_cast<Bmi270Imu *>(self)->bus_.write(r, p, n) ? 0 : -1;
}
void Bmi270Imu::delayCallback(uint32_t us, void *) { delayMicroseconds(us); }
bool Bmi270Imu::begin(ImuConfig &metadata) {
  if (ready_ || !qualification_.enabled || !qualification_.dedicated_bus ||
      !qualification_.electrically_qualified || !qualification_.sensor_id ||
      (qualification_.address != 0x68 && qualification_.address != 0x69) ||
      metadata.generation == UINT32_MAX)
    return false;
  device_.intf = BMI2_I2C_INTF;
  device_.read = readCallback;
  device_.write = writeCallback;
  device_.delay_us = delayCallback;
  device_.intf_ptr = this;
  device_.read_write_len = 32;
  if (bmi270_init(&device_) != BMI2_OK)
    return false;
  uint8_t aps = 1;
  if (bmi2_set_adv_power_save(0, &device_) != BMI2_OK ||
      bmi2_get_adv_power_save(&aps, &device_) != BMI2_OK || aps)
    return false;
  uint8_t sensors[] = {BMI2_ACCEL, BMI2_GYRO};
  bmi2_sens_config c[2]{};
  c[0].type = BMI2_ACCEL;
  c[1].type = BMI2_GYRO;
  if (bmi270_get_sensor_config(c, 2, &device_) != BMI2_OK)
    return false;
  c[0].cfg.acc.odr = BMI2_ACC_ODR_200HZ;
  c[0].cfg.acc.range = BMI2_ACC_RANGE_16G;
  c[0].cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
  c[0].cfg.acc.filter_perf = 1;
  c[1].cfg.gyr.odr = BMI2_GYR_ODR_200HZ;
  c[1].cfg.gyr.range = BMI2_GYR_RANGE_2000;
  c[1].cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
  c[1].cfg.gyr.filter_perf = 1;
  c[1].cfg.gyr.noise_perf = 1;
  if (bmi270_set_sensor_config(c, 2, &device_) != BMI2_OK ||
      bmi270_sensor_enable(sensors, 2, &device_) != BMI2_OK ||
      bmi2_set_accel_offset_comp(0, &device_) != BMI2_OK ||
      bmi2_set_gyro_offset_comp(0, &device_) != BMI2_OK)
    return false;
  bmi2_sens_config readback[2]{};
  readback[0].type = BMI2_ACCEL;
  readback[1].type = BMI2_GYRO;
  for (uint8_t sensor : sensors) {
    uint8_t filtered = 0, downsample = 1;
    if (bmi2_set_fifo_filter_data(sensor, BMI2_FIFO_FILTERED_DATA, &device_) != BMI2_OK ||
        bmi2_set_fifo_down_sample(sensor, 0, &device_) != BMI2_OK ||
        bmi2_get_fifo_filter_data(sensor, &filtered, &device_) != BMI2_OK ||
        bmi2_get_fifo_down_sample(sensor, &downsample, &device_) != BMI2_OK ||
        filtered != BMI2_FIFO_FILTERED_DATA || downsample)
      return false;
  }
  uint8_t a = 1, g = 1;
  uint16_t fifo = 0;
  const uint16_t wanted =
      BMI2_FIFO_HEADER_EN | BMI2_FIFO_TIME_EN | BMI2_FIFO_ACC_EN | BMI2_FIFO_GYR_EN;
  if (bmi2_set_fifo_config(0xffff, 0, &device_) != BMI2_OK ||
      bmi2_set_fifo_config(wanted, 1, &device_) != BMI2_OK ||
      bmi2_get_fifo_config(&fifo, &device_) != BMI2_OK || fifo != wanted ||
      bmi270_get_sensor_config(readback, 2, &device_) != BMI2_OK ||
      bmi2_get_accel_offset_comp(&a, &device_) != BMI2_OK ||
      bmi2_get_gyro_offset_comp(&g, &device_) != BMI2_OK || a || g)
    return false;
  if (readback[0].cfg.acc.odr != c[0].cfg.acc.odr ||
      readback[0].cfg.acc.range != c[0].cfg.acc.range ||
      readback[0].cfg.acc.bwp != c[0].cfg.acc.bwp || readback[0].cfg.acc.filter_perf != 1 ||
      readback[1].cfg.gyr.odr != c[1].cfg.gyr.odr ||
      readback[1].cfg.gyr.range != c[1].cfg.gyr.range ||
      readback[1].cfg.gyr.bwp != c[1].cfg.gyr.bwp || readback[1].cfg.gyr.filter_perf != 1 ||
      readback[1].cfg.gyr.noise_perf != 1)
    return false;
  uint8_t power = 0;
  if (!bus_.read(BMI2_PWR_CTRL_ADDR, &power, 1) ||
      (power & (BMI2_ACC_EN_MASK | BMI2_GYR_EN_MASK)) != (BMI2_ACC_EN_MASK | BMI2_GYR_EN_MASK))
    return false;
  if (!flush())
    return false;
  ++metadata.generation;
  metadata.sensor_id = qualification_.sensor_id;
  metadata.sensor_state = Qualification::Qualified;
  metadata.accel_range_mg = 16000;
  metadata.gyro_range_mdps = 2000000;
  metadata.accel_scale_numerator = 1;
  metadata.accel_scale_denominator = 2048;
  metadata.gyro_scale_numerator = 125;
  metadata.gyro_scale_denominator = 2048;
  metadata.accel_odr_millihz = metadata.gyro_odr_millihz = 200000;
  metadata.accel_filter = 0x502;
  metadata.gyro_filter = 0x702;
  metadata.accel_offset_compensation = metadata.gyro_offset_compensation = 1;
  ready_ = true;
  return true;
}
bool Bmi270Imu::read(uint8_t *p, uint16_t cap, uint16_t &n, uint8_t &flags) {
  n = 0;
  flags = 0;
  uint16_t length = 0;
  drained_ = false;
  capacity_refusal_ = false;
  drain_start_ = millis();
  if (!ready_ || !p || cap < 4 || cap > 112 || bmi2_get_fifo_length(&length, &device_) != BMI2_OK)
    return false;
  if (length > cap - 4) {
    capacity_refusal_ = true;
    refused_length_ = length;
    if (capacity_overflows_ != UINT32_MAX)
      ++capacity_overflows_;
    return false;
  }
  if (!length) {
    drain_end_ = millis();
    drained_ = true;
    return true;
  }
  // One complete burst; never split FIFO or guess continuation after a failed read.
  if (!bus_.read(BMI2_FIFO_DATA_ADDR, p, length + 4) ||
      bmi2_get_saturation_status(&flags, &device_) != BMI2_OK)
    return false;
  n = length + 4;
  drain_end_ = millis();
  drained_ = true;
  return true;
}
void Bmi270Imu::describeReadFailure(ImuEvidence &e) const {
  e.event_code = capacity_refusal_ ? 1 : 2;
  if (capacity_refusal_) {
    e.event_length = 2;
    e.event_bytes[0] = uint8_t(refused_length_);
    e.event_bytes[1] = uint8_t(refused_length_ >> 8);
  }
}
bool Bmi270Imu::flush() {
  return bmi2_set_command_register(BMI2_FIFO_FLUSH_CMD, &device_) == BMI2_OK;
}
bool Bmi270Worker::start(bool qualified, bool safe_mode) {
  if (started_ || !qualified || safe_mode)
    return false;
  started_ = true;
  if (xTaskCreatePinnedToCore(run, "imu", 8192, this, 1, nullptr, 1) != pdPASS) {
    started_ = false;
    return false;
  }
  return true;
}
void Bmi270Worker::run(void *p) {
  auto &self = *static_cast<Bmi270Worker *>(p);
  while (!self.progress_.isFinished()) {
    self.manager_.step(millis());
    vTaskDelay(pdMS_TO_TICKS(5) > 0 ? pdMS_TO_TICKS(5) : 1);
  }
  vTaskDelete(nullptr);
}
} // namespace ridesync
#endif

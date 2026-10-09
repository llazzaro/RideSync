#include <bmi270_api/bmi270.h>
#include <cstring>
extern bool test_motion_fixture;
static bmi2_sens_config profile[2];
static uint16_t fifo_config;
static uint8_t filters[3]{}, downsampling[3] = {2, 2, 2};
static bool mismatch = false;
static uint16_t fifo_length = 13;
extern "C" {
int8_t bmi270_init(bmi2_dev *d) {
  uint8_t p[2] = {9, 9};
  int8_t r = d->read(0, p, 2, d->intf_ptr);
  if (r)
    return r;
  return d->write(1, p, 2, d->intf_ptr);
}
int8_t bmi270_get_sensor_config(bmi2_sens_config *c, uint8_t, bmi2_dev *) {
  std::memcpy(c, profile, sizeof(profile));
  if (mismatch)
    c[0].cfg.acc.odr = 1;
  return 0;
}
int8_t bmi270_set_sensor_config(bmi2_sens_config *c, uint8_t, bmi2_dev *) {
  std::memcpy(profile, c, sizeof(profile));
  return 0;
}
int8_t bmi270_sensor_enable(const uint8_t *, uint8_t, bmi2_dev *) { return 0; }
int8_t bmi2_set_accel_offset_comp(uint8_t, bmi2_dev *) { return 0; }
int8_t bmi2_set_gyro_offset_comp(uint8_t, bmi2_dev *) { return 0; }
int8_t bmi2_get_accel_offset_comp(uint8_t *v, bmi2_dev *) {
  *v = 0;
  return 0;
}
int8_t bmi2_get_gyro_offset_comp(uint8_t *v, bmi2_dev *) {
  *v = 0;
  return 0;
}
int8_t bmi2_set_fifo_config(uint16_t c, uint8_t enable, bmi2_dev *) {
  fifo_config = enable ? c : 0;
  return 0;
}
int8_t bmi2_get_fifo_config(uint16_t *c, bmi2_dev *) {
  *c = fifo_config;
  return 0;
}
int8_t bmi2_get_fifo_length(uint16_t *n, bmi2_dev *) {
  *n = fifo_length;
  return 0;
}
int8_t bmi2_get_saturation_status(uint8_t *s, bmi2_dev *) {
  *s = test_motion_fixture ? 0 : 1;
  return 0;
}
int8_t bmi2_set_command_register(uint8_t, bmi2_dev *) { return 0; }
int8_t bmi2_set_adv_power_save(uint8_t, bmi2_dev *) { return 0; }
int8_t bmi2_get_adv_power_save(uint8_t *v, bmi2_dev *) {
  *v = 0;
  return 0;
}
int8_t bmi2_set_fifo_filter_data(uint8_t sensor, uint8_t value, bmi2_dev *) {
  filters[sensor] = value;
  return 0;
}
int8_t bmi2_get_fifo_filter_data(uint8_t sensor, uint8_t *v, bmi2_dev *) {
  *v = filters[sensor];
  return 0;
}
int8_t bmi2_set_fifo_down_sample(uint8_t sensor, uint8_t value, bmi2_dev *) {
  downsampling[sensor] = value;
  return 0;
}
int8_t bmi2_get_fifo_down_sample(uint8_t sensor, uint8_t *v, bmi2_dev *) {
  *v = downsampling[sensor];
  return 0;
}
}

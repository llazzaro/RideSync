#include "dynamic_motion_estimator.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::string> fields(const std::string &line) {
  std::vector<std::string> result;
  size_t start = 0;
  for (size_t end = 0; end <= line.size(); ++end) {
    if (end == line.size() || line[end] == ',') {
      result.push_back(line.substr(start, end - start));
      start = end + 1;
    }
  }
  return result;
}
uint64_t integer(const std::string &value, uint64_t maximum = UINT64_MAX) {
  if (value.empty() || value.size() > 20)
    throw std::runtime_error("invalid unsigned integer");
  uint64_t result = 0;
  for (char c : value) {
    if (c < '0' || c > '9' || result > maximum / 10 ||
        (result == maximum / 10 && uint64_t(c - '0') > maximum % 10))
      throw std::runtime_error("invalid or overflowing unsigned integer");
    result = result * 10 + uint64_t(c - '0');
  }
  return result;
}
float real(const std::string &value) {
  if (value.empty() || value.size() > 64)
    throw std::runtime_error("invalid measurement");
  errno = 0;
  char *end = nullptr;
  float result = std::strtof(value.c_str(), &end);
  if (end != value.c_str() + value.size() || errno == ERANGE || !std::isfinite(result))
    throw std::runtime_error("invalid or nonfinite measurement");
  return result;
}
const char *fault(ridesync::DynamicMotionFault f) {
  switch (f) {
  case ridesync::DynamicMotionFault::None:
    return "none";
  case ridesync::DynamicMotionFault::Disabled:
    return "disabled";
  case ridesync::DynamicMotionFault::Configuration:
    return "configuration";
  case ridesync::DynamicMotionFault::Reference:
    return "reference";
  case ridesync::DynamicMotionFault::Measurement:
    return "measurement";
  case ridesync::DynamicMotionFault::Identity:
    return "identity";
  case ridesync::DynamicMotionFault::Timing:
    return "timing";
  case ridesync::DynamicMotionFault::Sequence:
    return "sequence";
  case ridesync::DynamicMotionFault::Discontinuity:
    return "discontinuity";
  case ridesync::DynamicMotionFault::Horizon:
    return "horizon";
  case ridesync::DynamicMotionFault::Uninitialized:
    return "uninitialized";
  }
  return "unknown";
}
} // namespace
int main(int argc, char **argv) {
  size_t row = 0;
  try {
    if (argc != 3)
      throw std::runtime_error("expected step and horizon limits");
    ridesync::DynamicMotionConfig config;
    config.enabled = true;
    config.max_step_us = static_cast<uint32_t>(integer(argv[1], UINT32_MAX));
    config.max_horizon_us = static_cast<uint32_t>(integer(argv[2], UINT32_MAX));
    if (!ridesync::DynamicMotionEstimator::configValid(config))
      throw std::runtime_error("invalid configuration");
    ridesync::DynamicMotionEstimator estimator(config);
    const std::string header =
        "session_id,sensor_id,config_generation,mount_id,calibration_id,sensor_epoch,sequence,"
        "sample_time_us,timing_source,measurements_valid,discontinuity,stationary,declaration,"
        "force_x_mps2,force_y_mps2,force_z_mps2,rate_x_rad_s,rate_y_rad_s,rate_z_rad_s";
    std::string line;
    if (!std::getline(std::cin, line))
      throw std::runtime_error("missing header");
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line != header)
      throw std::runtime_error("unexpected CSV header");
    std::cout
        << "session_id,sensor_id,config_generation,mount_id,calibration_id,sensor_epoch,sequence,"
           "sample_time_us,timing_source,quality,fault,numeric_available,angles_available,elapsed_"
           "us,q_w,q_x,q_y,q_z,gravity_x_mps2,gravity_y_mps2,gravity_z_mps2,linear_x_mps2,linear_y_"
           "mps2,linear_z_mps2,roll_rad,pitch_rad,dynamic_lean_valid,dynamic_acceleration_valid\n";
    std::cout << std::setprecision(9);
    while (std::getline(std::cin, line)) {
      ++row;
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (line.size() > 2048)
        throw std::runtime_error("row too long");
      auto cells = fields(line);
      if (cells.size() != 19)
        throw std::runtime_error("expected 19 fields");
      ridesync::DynamicMotionInput input;
      input.session_id = integer(cells[0]);
      input.sensor_id = static_cast<uint32_t>(integer(cells[1], UINT32_MAX));
      input.config_generation = static_cast<uint32_t>(integer(cells[2], UINT32_MAX));
      input.mount_id = static_cast<uint32_t>(integer(cells[3], UINT32_MAX));
      input.calibration_id = static_cast<uint32_t>(integer(cells[4], UINT32_MAX));
      input.sensor_epoch = static_cast<uint32_t>(integer(cells[5], UINT32_MAX));
      input.sequence = static_cast<uint32_t>(integer(cells[6], UINT32_MAX));
      input.sample_time_us = integer(cells[7]);
      if (cells[8] == "modelled")
        input.timing_source = ridesync::DynamicTimingSource::ModelledCadence;
      else if (cells[8] == "qualified")
        input.timing_source = ridesync::DynamicTimingSource::QualifiedAcquisition;
      else if (cells[8] != "unknown")
        throw std::runtime_error("unknown timing source");
      input.measurements_valid = integer(cells[9], 1) != 0;
      input.discontinuity = integer(cells[10], 1) != 0;
      ridesync::DynamicMotionReference reference;
      reference.externally_stationary = integer(cells[11], 1) != 0;
      reference.declaration = static_cast<uint32_t>(integer(cells[12], UINT32_MAX));
      input.specific_force_mps2 = {real(cells[13]), real(cells[14]), real(cells[15])};
      input.angular_rate_rad_s = {real(cells[16]), real(cells[17]), real(cells[18])};
      auto estimate = reference.externally_stationary || reference.declaration
                          ? estimator.initialize(input, reference)
                          : estimator.update(input);
      for (size_t index = 0; index < 9; ++index)
        std::cout << cells[index] << ',';
      std::cout << (estimate.numeric_available ? "unreliable" : "invalid") << ','
                << fault(estimate.fault) << ',' << estimate.numeric_available << ','
                << estimate.angles_available << ',' << estimate.elapsed_us;
      if (estimate.numeric_available) {
        for (float q : estimate.quaternion_wxyz)
          std::cout << ',' << q;
        for (auto v : {estimate.gravity_body_mps2, estimate.linear_acceleration_body_mps2})
          std::cout << ',' << v.x << ',' << v.y << ',' << v.z;
      } else
        for (int index = 0; index < 10; ++index)
          std::cout << ',';
      if (estimate.angles_available)
        std::cout << ',' << estimate.roll_rad << ',' << estimate.pitch_rad;
      else
        std::cout << ",,";
      std::cout << ',' << estimate.dynamic_lean_valid << ',' << estimate.dynamic_acceleration_valid
                << '\n';
    }
    if (std::cin.bad() || !row)
      throw std::runtime_error("empty input or input read failure");
    std::cout.flush();
    if (!std::cout)
      throw std::runtime_error("output write failure");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Replay failed at data row " << row << ": " << error.what() << '\n';
    return 1;
  }
}

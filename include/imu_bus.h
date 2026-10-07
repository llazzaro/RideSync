#pragma once
#include <cstdint>
namespace ridesync {
class ImuBus {
public:
  virtual ~ImuBus() = default;
  virtual bool selectRegister(uint8_t) = 0;
  virtual uint32_t receive(uint8_t *, uint32_t) = 0;
  virtual uint32_t transmit(uint8_t, const uint8_t *, uint32_t) = 0;
};
class StrictImuBus {
public:
  explicit StrictImuBus(ImuBus &b) : bus_(b) {}
  bool read(uint8_t reg, uint8_t *out, uint32_t n) {
    if (!out || !n || n > 112 || !bus_.selectRegister(reg) || bus_.receive(scratch_, n) != n)
      return false;
    for (uint32_t i = 0; i < n; ++i)
      out[i] = scratch_[i];
    return true;
  }
  bool write(uint8_t reg, const uint8_t *p, uint32_t n) {
    return p && n && n <= 32 && bus_.transmit(reg, p, n) == n;
  }

private:
  ImuBus &bus_;
  uint8_t scratch_[112]{};
};
} // namespace ridesync

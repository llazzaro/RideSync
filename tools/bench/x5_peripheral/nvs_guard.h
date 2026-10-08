#pragma once
#include <cstdint>
namespace x5_probe {
struct NvsRefusals {
  uint32_t init, open, erase;
};
NvsRefusals nvsRefusals();
} // namespace x5_probe

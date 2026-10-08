#pragma once
#include <cstdint>
namespace gnss_bench {
struct NvsRefusals {
  uint32_t init, open, erase;
};
NvsRefusals nvsRefusals();
} // namespace gnss_bench

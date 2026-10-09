// Independent documentary literals. MIT field references: M5 c76e140 and
// ESP32 83d4748; artificial identifier, not a camera capture.
#pragma once
#include <cstdint>
namespace wake_fixture {
constexpr uint8_t ad[] = {2,    1,    6,    27,   255, 0x4c, 0,    2,    0x15, 9,   0x4f,
                          0x52, 0x42, 0x49, 0x54, 9,   0xff, 0x0f, 0,    'A',  '1', 'B',
                          '2',  'C',  '3',  0,    0,   0,    0,    0xe4, 1};
constexpr uint8_t rsp[] = {3,   3,   0x80, 0xce, 20,  9,   'I', 'n', 's', 't', 'a', '3', '6',
                           '0', ' ', 'G',  'P',  'S', ' ', 'R', 'e', 'm', 'o', 't', 'e'};
} // namespace wake_fixture

#include "protocol/insta360_codec.h"
namespace ridesync {
namespace insta360 {
ShutterEvent encodeShutterEvent() {
  // Independently authored from pinned MIT community wire facts; see fixtures.
  return {{0xfc, 0xef, 0xfe, 0x86, 0x00, 0x03, 0x01, 0x02, 0x00}};
}
} // namespace insta360
} // namespace ridesync

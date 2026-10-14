#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace hero12_probe {
struct Policy {
  bool linked = false, qualified = false, observed = false;
  bool encoding = false, busy = true, ready = false;
  uint32_t generation = 0, started = 0, sampled = 0;
  void disconnect() { linked = qualified = observed = false; }
  void connected(uint32_t g, uint32_t now) {
    disconnect();
    linked = true;
    generation = g;
    started = now;
  }
  void observe(bool e, bool b, bool r, uint32_t now) {
    encoding = e;
    busy = b;
    ready = r;
    sampled = now;
    observed = true;
  }
  bool admit(bool start, uint32_t now) const {
    return linked && qualified && observed && uint32_t(now - started) < 600000 &&
           uint32_t(now - sampled) <= 2000 && (start ? !encoding && !busy && ready : encoding);
  }
};
} // namespace hero12_probe
namespace hero12_probe {
inline uint16_t descriptorEnd(uint16_t value, uint16_t serviceEnd, const uint16_t *defs,
                              size_t count) {
  uint16_t end = serviceEnd;
  for (size_t i = 0; i < count; ++i)
    if (defs[i] > value && defs[i] <= end)
      end = defs[i] - 1;
  return end;
}
class LineParser {
public:
  char feed(char ch, bool more) {
    if (ch == '\r')
      return 0;
    if (ch != '\n') {
      if (used < sizeof(line) - 1)
        line[used++] = ch;
      else
        overflow = true;
      return 0;
    }
    if (more)
      discarding = true;
    line[used] = 0;
    char result = 0;
    if (used && !overflow) {
      if (!strcmp(line, "DISCONNECT"))
        result = 'D';
      else if (more || discarding) {
        discarding = true;
        result = '!';
      } else if (!strcmp(line, "CONNECT"))
        result = 'C';
      else if (!strcmp(line, "REC"))
        result = 'R';
      else if (!strcmp(line, "STOP"))
        result = 'S';
      else if (!strcmp(line, "QUERY"))
        result = 'Q';
      else if (!strcmp(line, "STATUS"))
        result = '?';
      else
        result = '!';
    }
    used = 0;
    overflow = false;
    return result;
  }
  void drained() {
    if (!used && !overflow)
      discarding = false;
  }

private:
  char line[24]{};
  size_t used = 0;
  bool overflow = false, discarding = false;
};
} // namespace hero12_probe

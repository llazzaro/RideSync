#pragma once
#include <algorithm>
#include <cstddef>
#include <cstring>
namespace gnss_bench {
struct Output {
  virtual ~Output() = default;
  virtual size_t writable() = 0;
  virtual size_t write(const char *, size_t) = 0;
};
class Reporter {
public:
  bool queue(const char *line, size_t length) {
    if (pending() || length > sizeof(buffer_))
      return false;
    std::memcpy(buffer_, line, length);
    size_ = length;
    offset_ = 0;
    return true;
  }
  void service(Output &out) {
    if (!pending())
      return;
    const size_t n = std::min(size_ - offset_, std::min(size_t(64), out.writable()));
    if (n) {
      const size_t accepted = out.write(buffer_ + offset_, n);
      if (accepted <= n)
        offset_ += accepted;
    }
  }
  bool pending() const { return offset_ < size_; }

private:
  char buffer_[768]{};
  size_t size_ = 0, offset_ = 0;
};
} // namespace gnss_bench

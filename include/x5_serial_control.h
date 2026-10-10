#pragma once
#include "camera_manager.h"
namespace ridesync {
class X5CommandPort {
public:
  virtual ~X5CommandPort() = default;
  virtual CameraError request(Operation) = 0;
  virtual void disconnect() = 0;
  virtual void status() = 0;
};
enum class X5SerialCommand { None, Connect, Rec, Stop, Query, Status, Disconnect, Invalid };
// One complete line, no delayed command queue. Busy input is discarded through LF.
class X5SerialControl {
public:
  explicit X5SerialControl(X5CommandPort &port) : port_(port) {}
  void consume(char);
  X5SerialCommand service();
  uint32_t rejected() const { return rejected_; }
  CameraError error() const { return error_; }

private:
  X5CommandPort &port_;
  std::array<char, 32> line_{};
  size_t size_ = 0;
  bool discard_ = false, cr_ = false, pending_ = false;
  X5SerialCommand command_ = X5SerialCommand::None;
  CameraError error_ = CameraError::None;
  uint32_t rejected_ = 0;
  void reject();
};
} // namespace ridesync

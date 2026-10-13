#pragma once
#include "config_bootstrap.h"
#include "setup_discovery.h"
#include <cstddef>
#include <cstdint>
namespace ridesync {
constexpr size_t kSetupFrameBytes = 2048;
struct SetupSaveDispatch {
  uint32_t session = 0, request = 0;
  SettingsSave save;
};
// Single serial-owner task only. All payloads are copied; no storage or radio calls.
class SetupService {
public:
  void reset(uint32_t now, uint32_t epoch, uint32_t challenge, uint32_t session_nonce);
  void setDiscovery(SetupDiscovery *owner) { discovery_ = owner; }
  void setSnapshot(const SettingsSnapshot &s);
  void setIsolationAvailable(bool safe) { isolation_available_ = safe; }
  bool isolationRequested() const { return entering_; }
  void acknowledgeIsolation(bool isolated);
  bool isolated() const { return isolated_; }
  bool active() const { return active_; }
  bool closing() const { return closing_; }
  void ownerReleased(bool released);
  void feed(char byte, uint32_t now);
  void tick(uint32_t now);
  // Transport owner calls on an observed detach; fail closed until physical reboot.
  void disconnected();
  bool takeResponse(char *out, size_t capacity);
  bool takeSave(SetupSaveDispatch &out);
  void completeSave(uint32_t session, uint32_t request, SaveOutcome outcome, PersistResult result,
                    uint32_t epoch, uint64_t generation);
  void setPrerequisiteReport(const char *report);
  bool restartRequested() const { return restart_ready_; }
  uint32_t session() const { return session_; }

private:
  SetupDiscovery *discovery_ = nullptr;
  bool unresolved_draft_ = false;
  void discoveryCommand(const char *op, char *arguments, uint32_t now);
  void process(uint32_t now);
  void reply(const char *outcome, const char *error, const char *payload = nullptr);
  void revoke();
  bool editable() const;
  bool validateDraft();
  uint32_t now_ = 0, boot_ = 0, started_ = 0, activity_ = 0, partial_ = 0;
  uint32_t epoch_ = 0, challenge_ = 0, nonce_ = 0, session_ = 0, request_ = 0, last_request_ = 0;
  uint32_t save_session_ = 0, save_request_ = 0;
  uint32_t recovery_challenge_ = 0, recovery_request_ = 0;
  uint8_t failures_ = 0, recovery_failures_ = 0;
  bool readonly_ = false, recovery_pending_ = false;
  bool isolation_available_ = false, entering_ = false, isolated_ = false, active_ = false,
       closing_ = false;
  bool draft_valid_ = false, response_ready_ = false, dropping_ = false, save_ready_ = false,
       saving_ = false;
  const char *close_error_ = nullptr;
  bool isolation_ack_pending_ = false, isolation_ack_value_ = false;
  bool terminal_ready_ = false;
  SaveOutcome terminal_outcome_ = SaveOutcome::None;
  uint64_t draft_generation_ = 0;
  uint64_t terminal_generation_ = 0;
  PersistResult terminal_result_;
  uint32_t close_request_ = 0;
  char close_operation_[20]{};
  bool durable_ = false, restart_after_exit_ = false, restart_ready_ = false;
  size_t used_ = 0;
  char input_[kSetupFrameBytes + 1]{}, output_[kSetupFrameBytes + 1]{}, operation_[20]{};
  char report_[769]{};
  SettingsSnapshot snapshot_;
  Settings draft_;
  SetupSaveDispatch dispatch_;
};
bool encodeSetupCameras(const Settings &, char *out, size_t capacity);
bool decodeSetupCameras(const char *, const Settings &base, Settings &out);
} // namespace ridesync

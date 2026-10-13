#include "setup_service.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
namespace ridesync {
namespace {
bool number(const char *s, uint64_t &n) {
  if (!s || !*s)
    return false;
  n = 0;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9' || n > (UINT64_MAX - (*s - '0')) / 10)
      return false;
    n = n * 10 + (*s - '0');
  }
  return true;
}
char *token(char *&p, char separator) {
  char *s = p;
  if (!p)
    return nullptr;
  char *end = strchr(p, separator);
  if (end) {
    *end = 0;
    p = end + 1;
  } else
    p = nullptr;
  return s;
}
int nibble(char c) {
  return c >= '0' && c <= '9'   ? c - '0'
         : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                : -1;
}
// Deliberately small assigned Unicode label repertoire; no format/control
// characters or dependence on an unbounded Unicode classification database.
bool safeName(const char *text, size_t bound) {
  const unsigned char *p = reinterpret_cast<const unsigned char *>(text);
  const void *end = memchr(text, 0, bound);
  if (!end || !text[0])
    return false;
  size_t n = static_cast<const char *>(end) - text;
  for (size_t i = 0; i < n;) {
    uint32_t cp = p[i++];
    unsigned count = 0;
    uint32_t minimum = 0;
    if (cp >= 0xc2 && cp <= 0xdf) {
      cp &= 31;
      count = 1;
      minimum = 0x80;
    } else if (cp >= 0xe0 && cp <= 0xef) {
      cp &= 15;
      count = 2;
      minimum = 0x800;
    } else if (cp >= 0xf0 && cp <= 0xf4) {
      cp &= 7;
      count = 3;
      minimum = 0x10000;
    } else if (cp >= 0x80)
      return false;
    if (i + count > n)
      return false;
    for (unsigned j = 0; j < count; ++j) {
      unsigned c = p[i++];
      if ((c & 0xc0) != 0x80)
        return false;
      cp = (cp << 6) | (c & 63);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      return false;
    bool allowed = (cp >= 0x20 && cp <= 0x7e) || (cp >= 0xa0 && cp <= 0x24f && cp != 0xad) ||
                   (cp >= 0x300 && cp <= 0x36f) || (cp >= 0x391 && cp <= 0x3a1) ||
                   (cp >= 0x3a3 && cp <= 0x3ce) || (cp >= 0x400 && cp <= 0x45f) ||
                   (cp >= 0x3041 && cp <= 0x3096) || (cp >= 0x30a1 && cp <= 0x30fa) ||
                   (cp >= 0x4e00 && cp <= 0x9fff) || (cp >= 0xac00 && cp <= 0xd7a3) ||
                   (cp >= 0x1f600 && cp <= 0x1f64f);
    if (!allowed)
      return false;
  }
  return true;
}
bool unhex(const char *s, char *out, size_t cap) {
  if (!s)
    return false;
  if (!strcmp(s, "-")) {
    out[0] = 0;
    return true;
  }
  size_t n = strlen(s);
  if (n % 2 || n / 2 >= cap)
    return false;
  for (size_t i = 0; i < n; i += 2) {
    int a = nibble(s[i]), b = nibble(s[i + 1]);
    if (a < 0 || b < 0)
      return false;
    int c = a * 16 + b;
    if (c < 32 || c == 127)
      return false;
    out[i / 2] = char(c);
  }
  out[n / 2] = 0;
  return true;
}
bool hex(const char *s, size_t bound, char *out, size_t cap) {
  const char *end = static_cast<const char *>(memchr(s, 0, bound));
  if (!end)
    return false;
  size_t n = end - s;
  if (cap < 2 * n + 1)
    return false;
  if (!n) {
    if (cap < 2)
      return false;
    strcpy(out, "-");
    return true;
  }
  const char *h = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) {
    unsigned c = static_cast<unsigned char>(s[i]);
    out[2 * i] = h[c >> 4];
    out[2 * i + 1] = h[c & 15];
  }
  out[2 * n] = 0;
  return true;
}
} // namespace
bool encodeSetupCameras(const Settings &s, char *out, size_t capacity) {
  if (s.count > 8 || s.capacity > 8)
    return false;
  int n = snprintf(out, capacity, "%u,%u", unsigned(s.count), unsigned(s.capacity));
  if (n < 0 || size_t(n) >= capacity)
    return false;
  size_t used = n;
  for (size_t i = 0; i < s.count; ++i) {
    const auto &c = s.cameras[i];
    char name[131], id[37], wake[37];
    if (!hex(c.name, sizeof c.name, name, sizeof name) ||
        !hex(c.identifier, sizeof c.identifier, id, sizeof id) ||
        !hex(c.wake_identifier, sizeof c.wake_identifier, wake, sizeof wake))
      return false;
    n = snprintf(out + used, capacity - used, ";%u,%u,%u,%u,%u,%s,%s,%s", unsigned(c.family),
                 unsigned(c.model), unsigned(c.address_type), unsigned(c.enabled),
                 unsigned(c.gps_telemetry), name, id, wake);
    if (n < 0 || size_t(n) >= capacity - used)
      return false;
    used += n;
  }
  return true;
}
bool decodeSetupCameras(const char *wire, const Settings &base, Settings &out) {
  if (!wire || strlen(wire) > kSetupFrameBytes)
    return false;
  char copy[kSetupFrameBytes + 1];
  strcpy(copy, wire);
  char *rows = copy, *row = token(rows, ';'), *fields = row;
  uint64_t count, cap;
  if (!number(token(fields, ','), count) || !number(token(fields, ','), cap) || fields ||
      count > 4 || cap > 4 || count > cap || !cap)
    return false;
  Settings candidate = base;
  candidate.count = count;
  candidate.capacity = cap;
  candidate.cameras = {};
  for (size_t i = 0; i < count; ++i) {
    row = token(rows, ';');
    if (!row)
      return false;
    fields = row;
    uint64_t values[5];
    for (int j = 0; j < 5; ++j)
      if (!number(token(fields, ','), values[j]))
        return false;
    if (values[0] > 3 || values[1] > 8 || values[2] > 2 || values[3] > 1 || values[4] > 1)
      return false;
    auto &c = candidate.cameras[i];
    c.family = CameraFamily(values[0]);
    c.model = CameraModel(values[1]);
    c.address_type = AddressType(values[2]);
    c.enabled = values[3];
    c.gps_telemetry = values[4];
    if (!unhex(token(fields, ','), c.name, sizeof c.name) ||
        !unhex(token(fields, ','), c.identifier, sizeof c.identifier) ||
        !unhex(token(fields, ','), c.wake_identifier, sizeof c.wake_identifier) || fields ||
        !safeName(c.name, sizeof c.name))
      return false;
  }
  if (rows)
    return false;
  out = candidate;
  return true;
}
void SetupService::reset(uint32_t now, uint32_t epoch, uint32_t challenge, uint32_t nonce) {
  this->~SetupService();
  new (this) SetupService;
  boot_ = now_ = now;
  epoch_ = epoch;
  challenge_ = recovery_challenge_ = challenge;
  nonce_ = nonce;
}
void SetupService::setSnapshot(const SettingsSnapshot &s) {
  if (s.epoch == epoch_)
    snapshot_ = s;
}
void SetupService::reply(const char *outcome, const char *error, const char *payload) {
  if (response_ready_)
    return;
  snprintf(output_, sizeof output_, "@RS1 %u %u %u %llu %s %s %s%s%s", session_, request_, epoch_,
           static_cast<unsigned long long>(snapshot_.generation),
           operation_[0] ? operation_ : "ERROR", outcome, error, payload ? " " : "",
           payload ? payload : "");
  response_ready_ = true;
}
bool SetupService::takeResponse(char *out, size_t capacity) {
  if (!response_ready_ || !out || strlen(output_) + 1 > capacity)
    return false;
  strcpy(out, output_);
  response_ready_ = false;
  return true;
}
void SetupService::disconnected() {
  close_error_ = "Disconnected";
  close_request_ = 0;
  strcpy(close_operation_, "EXIT");
  revoke();
}
void SetupService::revoke() {
  if (discovery_)
    discovery_->cancel(session_);
  if (save_ready_) {
    save_ready_ = saving_ = false;
    terminal_outcome_ = SaveOutcome::None;
  }
  terminal_ready_ = false;
  if (close_error_ || recovery_pending_) {
    restart_after_exit_ = false;
    restart_ready_ = false;
  }
  isolation_ack_pending_ = false;
  active_ = false;
  entering_ = false;
  closing_ = isolated_;
  draft_valid_ = false;
  challenge_ = 0;
}
void SetupService::ownerReleased(bool released) {
  if (!released || !closing_ || saving_ || (discovery_ && !discovery_->released()))
    return;
  if (response_ready_ || terminal_ready_)
    return;
  if (recovery_pending_) {
    recovery_pending_ = false;
    closing_ = false;
    readonly_ = true;
    isolated_ = active_ = true;
    session_ = ++nonce_;
    request_ = last_request_ = recovery_request_;
    activity_ = now_;
    close_error_ = nullptr;
    strcpy(operation_, "RECOVER");
    reply("Ok", "None");
    return;
  }
  request_ = close_request_;
  strcpy(operation_, close_operation_[0] ? close_operation_ : "EXIT");
  reply(close_error_ ? "Error" : "Ok", close_error_ ? close_error_ : "None");
  closing_ = false;
  isolated_ = false;
  session_ = 0;
  restart_ready_ = restart_after_exit_ && durable_;
}
void SetupService::acknowledgeIsolation(bool isolated) {
  if (!entering_)
    return;
  if (response_ready_) {
    isolation_ack_pending_ = true;
    isolation_ack_value_ = isolated;
    return;
  }
  isolation_ack_pending_ = false;
  entering_ = false;
  if (!isolated) {
    reply("Error", "IsolationRefused");
    return;
  }
  isolated_ = active_ = true;
  session_ = nonce_;
  activity_ = now_;
  last_request_ = request_;
  reply("Ok", "None");
}
void SetupService::tick(uint32_t now) {
  now_ = now;
  if (discovery_)
    discovery_->service(now);
  if (isolation_ack_pending_ && !response_ready_)
    acknowledgeIsolation(isolation_ack_value_);
  if (terminal_ready_ && !response_ready_) {
    request_ = save_request_;
    strcpy(operation_, "DISPATCH");

    const char *error = terminal_outcome_ == SaveOutcome::Durable   ? "None"
                        : terminal_outcome_ == SaveOutcome::Stale   ? "Stale"
                        : terminal_outcome_ == SaveOutcome::Busy    ? "Busy"
                        : terminal_outcome_ == SaveOutcome::Invalid ? "Invalid"
                        : terminal_outcome_ == SaveOutcome::Refused ? "Refused"
                                                                    : "PersistenceError";
    reply(terminal_outcome_ == SaveOutcome::Durable ? "Ok" : "Error", error);
    terminal_ready_ = false;
  }
  if (used_ && uint32_t(now - partial_) >= 2000) {
    used_ = 0;
    dropping_ = true;
  }
  if ((active_ || entering_) &&
      (uint32_t(now - activity_) >= 120000 || uint32_t(now - started_) >= 600000)) {
    close_error_ = "Expired";
    close_request_ = 0;
    strcpy(close_operation_, "EXIT");
    revoke();
  }
}
void SetupService::feed(char byte, uint32_t now) {
  tick(now);
  if (byte == '\n') {
    if (dropping_) {
      dropping_ = false;
      used_ = 0;
      if (!response_ready_) {
        strcpy(operation_, "ERROR");
        request_ = 0;
        reply("Error", "Malformed");
      }
      return;
    }
    input_[used_] = 0;
    if (used_ && !response_ready_)
      process(now);
    used_ = 0;
    return;
  }
  if (dropping_ || response_ready_) {
    dropping_ = true;
    return;
  }
  if (!used_)
    partial_ = now;
  if (static_cast<unsigned char>(byte) < 32 || byte == 127 || used_ == kSetupFrameBytes) {
    dropping_ = true;
    used_ = 0;
    return;
  }
  input_[used_++] = byte;
}
bool SetupService::editable() const { return snapshot_.completed && snapshot_.effective; }
bool SetupService::validateDraft() {
  SourceConfig expanded;
  if (unresolved_draft_ || !draft_valid_ || draft_.capacity > 4 || draft_.count > 4 ||
      !expandSettings(draft_, expanded))
    return false;
  for (size_t i = 0; i < draft_.count; ++i)
    if (!safeName(draft_.cameras[i].name, sizeof draft_.cameras[i].name))
      return false;
  bool x5 = false;
  for (size_t i = 0; i < draft_.count; ++i)
    if (draft_.cameras[i].model == CameraModel::X5) {
      if (x5 || i)
        return false;
      x5 = true;
    }
  return true;
}
void SetupService::process(uint32_t now) {
  char *p = input_;
  char *version = token(p, ' ');
  strcpy(operation_, "ERROR");
  request_ = 0;
  if (strcmp(version, "@RS1")) {
    reply("Error", "Version");
    return;
  }
  uint64_t session, request, epoch, generation;
  if (!number(token(p, ' '), session) || !number(token(p, ' '), request) ||
      !number(token(p, ' '), epoch) || !number(token(p, ' '), generation) || session > UINT32_MAX ||
      request == 0 || request > UINT32_MAX || epoch > UINT32_MAX) {
    reply("Error", "Malformed");
    return;
  }
  char *op = token(p, ' ');
  if (!op || strlen(op) >= sizeof operation_) {
    reply("Error", "Malformed");
    return;
  }
  strcpy(operation_, op);
  request_ = request;
  if (!strcmp(op, "HELLO")) {
    bool recovery = p && !strcmp(p, "RECOVERY");
    if (session || (p && !recovery) || (!recovery && (active_ || entering_ || closing_))) {
      reply("Error", p && !recovery ? "UnknownField" : "Busy");
      return;
    }
    char text[50];
    uint32_t challenge = recovery
                             ? (recovery_failures_ < 3 ? recovery_challenge_ : 0)
                             : (uint32_t(now - boot_) < 30000 && failures_ < 3 ? challenge_ : 0);
    snprintf(text, sizeof text, "challenge=%u", challenge);
    uint32_t old = session_;
    session_ = 0;
    reply("Ok", "None", text);
    session_ = old;
    return;
  }
  if (!strcmp(op, "RECOVER")) {
    if (session || entering_ || recovery_pending_ || nonce_ == UINT32_MAX || !nonce_ ||
        recovery_failures_ >= 3 || !recovery_challenge_) {
      reply("Error", "EntryClosed");
      return;
    }
    char *argument = token(p, ' ');
    uint64_t challenge;
    if (!number(argument, challenge) || !p || strcmp(p, "PARKED") ||
        challenge != recovery_challenge_) {
      ++recovery_failures_;
      reply("Error", "Challenge");
      return;
    }
    if (epoch != epoch_) {
      reply("Error", "Stale");
      return;
    }
    if (!isolation_available_) {
      reply("Error", "IsolationRefused");
      return;
    }
    recovery_pending_ = true;
    recovery_request_ = request_;
    started_ = activity_ = now;
    recovery_challenge_ += 0x9e3779b9u;
    if (!recovery_challenge_)
      ++recovery_challenge_;
    revoke();
    closing_ = true;
    close_error_ = nullptr;
    uint32_t old = session_;
    session_ = 0;
    reply("Pending", "OwnerRelease");
    session_ = old;
    return;
  }
  if (!strcmp(op, "ENTER")) {
    if (session || active_ || entering_ || closing_) {
      reply("Error", "Busy");
      return;
    }
    if (uint32_t(now - boot_) >= 30000 || failures_ >= 3 || !challenge_ || !nonce_) {
      reply("Error", "EntryClosed");
      return;
    }
    char *argument = token(p, ' ');
    uint64_t challenge;
    if (!number(argument, challenge) || !p || strcmp(p, "PARKED") || challenge != challenge_) {
      ++failures_;
      reply("Error", "Challenge");
      return;
    }
    if (epoch != epoch_ || generation != snapshot_.generation) {
      reply("Error", "Stale");
      return;
    }
    if (!isolation_available_) {
      reply("Error", "IsolationRefused");
      return;
    }
    entering_ = true;
    started_ = activity_ = now;
    reply("Pending", "IsolationRequired");
    return;
  }
  if (!active_ || session != session_) {
    reply("Error", "Session");
    return;
  }
  if (request <= last_request_) {
    reply("Error", "Replay");
    return;
  }
  last_request_ = request;
  if (readonly_ && strcmp(op, "INSPECT") && strcmp(op, "STATUS") && strcmp(op, "REPORT") &&
      strcmp(op, "EXIT") && strcmp(op, "CANCEL")) {
    reply("Error", "ReadOnly");
    return;
  }
  if (epoch != epoch_ ||
      (generation != snapshot_.generation && strcmp(op, "INSPECT") && strcmp(op, "STATUS") &&
       strcmp(op, "REPORT") && !(readonly_ && (!strcmp(op, "EXIT") || !strcmp(op, "CANCEL"))))) {
    reply("Error", "Stale");
    return;
  }
  activity_ = now;
  if (!strcmp(op, "DISCOVER") || !strcmp(op, "INVENTORY") || !strcmp(op, "SELECT") ||
      !strcmp(op, "CANCEL_DISCOVERY")) {
    discoveryCommand(op, p, now);
    return;
  }
  if (strcmp(op, "DRAFT_SET") && p) {
    reply("Error", "UnknownField");
    return;
  }
  if (!strcmp(op, "INSPECT") || !strcmp(op, "DRAFT_GET")) {
    char payload[1900];
    if (!strcmp(op, "DRAFT_GET") && !draft_valid_) {
      reply("Error", "NoDraft");
      return;
    }
    if (!encodeSetupCameras(!strcmp(op, "INSPECT") ? snapshot_.settings : draft_, payload,
                            sizeof payload)) {
      reply("Error", "Invalid");
      return;
    }
    reply("Ok", "None", payload);
  } else if (!strcmp(op, "STATUS")) {
    char status[256];
    const char *state = saving_                                              ? "Pending"
                        : terminal_outcome_ == SaveOutcome::Durable          ? "Durable"
                        : terminal_outcome_ == SaveOutcome::Stale            ? "Stale"
                        : terminal_outcome_ == SaveOutcome::Busy             ? "Busy"
                        : terminal_outcome_ == SaveOutcome::Invalid          ? "Invalid"
                        : terminal_outcome_ == SaveOutcome::Refused          ? "Refused"
                        : terminal_outcome_ == SaveOutcome::PersistenceError ? "PersistenceError"
                                                                             : "Idle";
    snprintf(status, sizeof status,
             "status=%s,session=%u,request=%u,epoch=%u,generation=%llu,outcome=%u,persist=%u,code=%"
             "ld,indeterminate=%u",
             state, save_session_, save_request_, epoch_,
             static_cast<unsigned long long>(terminal_generation_), unsigned(terminal_outcome_),
             unsigned(terminal_result_.status), static_cast<long>(terminal_result_.code),
             unsigned(terminal_result_.indeterminate));
    reply("Ok", "None", status);
  } else if (!strcmp(op, "REPORT"))
    reply("Ok", "None", report_[0] ? report_ : "overall=Unknown");
  else if (!strcmp(op, "DRAFT_BEGIN")) {
    if (saving_) {
      reply("Error", "Busy");
      return;
    }
    if (!editable()) {
      reply("Error", "RecoveryRequired");
      return;
    }
    unresolved_draft_ = false;
    draft_ = snapshot_.settings;
    draft_generation_ = snapshot_.generation;
    draft_valid_ = true;
    reply("Ok", "None");
  } else if (!strcmp(op, "DRAFT_SET")) {
    Settings candidate;
    if (saving_) {
      reply("Error", "Busy");
      return;
    }
    if (!draft_valid_) {
      reply("Error", "NoDraft");
      return;
    }
    if (!decodeSetupCameras(p, draft_, candidate)) {
      reply("Error", "Invalid");
      return;
    }
    draft_ = candidate;
    reply("Ok", "None");
  } else if (!strcmp(op, "DRAFT_DROP")) {
    if (saving_) {
      reply("Error", "Busy");
      return;
    }
    draft_valid_ = false;
    reply("Ok", "None");
  } else if (!strcmp(op, "VALIDATE")) {
    bool valid = validateDraft();
    reply(valid ? "Ok" : "Error", valid ? "None" : "Invalid");
  } else if (!strcmp(op, "DISPATCH")) {
    if (saving_ || save_ready_) {
      reply("Error", "Busy");
      return;
    }
    if (draft_generation_ != snapshot_.generation) {
      reply("Error", "Stale");
      return;
    }
    if (!editable() || !validateDraft()) {
      reply("Error", "Invalid");
      return;
    }
    dispatch_.session = session_;
    dispatch_.request = request_;
    dispatch_.save.epoch = epoch_;
    dispatch_.save.generation = snapshot_.generation;
    dispatch_.save.settings = draft_;
    save_ready_ = saving_ = true;
    save_session_ = session_;
    save_request_ = request_;
    terminal_outcome_ = SaveOutcome::Accepted;
    terminal_generation_ = snapshot_.generation;
    durable_ = false;
    reply("Pending", "None");
  } else if (!strcmp(op, "RESTART")) {
    if (!durable_) {
      reply("Error", "NotDurable");
      return;
    }
    restart_after_exit_ = true;
    reply("Ok", "None");
  } else if (!strcmp(op, "EXIT") || !strcmp(op, "CANCEL")) {
    close_request_ = request_;
    strcpy(close_operation_, op);
    if (save_ready_) {
      save_ready_ = saving_ = false;
    }
    reply("Pending", "OwnerRelease");
    revoke();
  } else
    reply("Error", "Unavailable");
}
void SetupService::discoveryCommand(const char *op, char *arguments, uint32_t now) {
  if (!discovery_) {
    reply("Error", "Unavailable");
    return;
  }
  if (!strcmp(op, "DISCOVER")) {
    if (arguments) {
      reply("Error", "UnknownField");
      return;
    }
    if (saving_) {
      reply("Error", "Busy");
      return;
    }
    bool started = discovery_->scan(session_, now);
    reply(started ? "Pending" : "Error",
          started ? "None"
                  : (discovery_->outcome() == SetupDiscoveryOutcome::Unavailable ? "NotQualified"
                                                                                 : "Busy"));
    return;
  }
  if (!strcmp(op, "CANCEL_DISCOVERY")) {
    if (arguments) {
      reply("Error", "UnknownField");
      return;
    }
    discovery_->cancel(session_);
    reply(discovery_->released() ? "Ok" : "Pending", "None");
    return;
  }
  if (!strcmp(op, "INVENTORY")) {
    uint64_t index;
    if (!number(arguments, index) || index > kSetupCandidates) {
      reply("Error", "Invalid");
      return;
    }
    if (discovery_->session() != session_) {
      reply("Error", "NoInventory");
      return;
    }
    char payload[512];
    int used = snprintf(payload, sizeof payload, "state=%u,count=%u,truncated=%u,released=%u",
                        unsigned(discovery_->outcome()), unsigned(discovery_->count()),
                        unsigned(discovery_->truncated()), unsigned(discovery_->released()));
    if (index < discovery_->count()) {
      const auto &c = discovery_->candidate(index);
      char name[67];
      hex(c.name, sizeof c.name, name, sizeof name);
      used += snprintf(payload + used, sizeof payload - used,
                       ";index=%u,address=%02X:%02X:%02X:%02X:%02X:%02X,type=%u,rssi=%d,name=%s,"
                       "hints_truncated=%u,services=",
                       unsigned(index), c.address[5], c.address[4], c.address[3], c.address[2],
                       c.address[1], c.address[0], unsigned(c.address_type), int(c.rssi), name,
                       unsigned(c.hints_truncated));
      const char *digits = "0123456789abcdef";
      for (size_t i = 0; i < c.service_count; ++i) {
        if (i)
          payload[used++] = ',';
        for (size_t j = 0; j < c.services[i].size; ++j) {
          unsigned byte = c.services[i].bytes[j];
          payload[used++] = digits[byte >> 4];
          payload[used++] = digits[byte & 15];
        }
      }
      payload[used] = 0;
    }
    reply("Ok", "None", payload);
    return;
  }
  uint64_t index, model, slot;
  if (!number(token(arguments, ','), index) || !number(token(arguments, ','), model) ||
      !number(token(arguments, ','), slot) || arguments || index >= kSetupCandidates ||
      model > unsigned(CameraModel::DJI_OSMO360) || slot >= kBlePeers) {
    reply("Error", "Invalid");
    return;
  }
  if (saving_) {
    reply("Error", "Busy");
    return;
  }
  if (!draft_valid_) {
    reply("Error", "NoDraft");
    return;
  }
  SourceConfig base;
  CameraConfig selected;
  if (!expandSettings(draft_, base)) {
    reply("Error", "Invalid");
    return;
  }
  const auto outcome =
      discovery_->select(session_, index, CameraModel(model), base, slot, selected);
  const char *error = outcome == SetupSelectionOutcome::NeedsCommissioning ? "NeedsCommissioning"
                      : outcome == SetupSelectionOutcome::Unresolved       ? "UnresolvedIdentity"
                      : outcome == SetupSelectionOutcome::X5Manual         ? "X5ManualCommissioning"
                      : outcome == SetupSelectionOutcome::Capacity         ? "Capacity"
                      : outcome == SetupSelectionOutcome::DuplicateIdentity ? "DuplicateIdentity"
                      : outcome == SetupSelectionOutcome::BondCapacity      ? "BondAdmission"
                      : outcome == SetupSelectionOutcome::Busy              ? "Busy"
                      : outcome == SetupSelectionOutcome::Stale             ? "Stale"
                                                                            : "Unsupported";
  if (outcome != SetupSelectionOutcome::NeedsCommissioning &&
      outcome != SetupSelectionOutcome::Unresolved) {
    reply("Error", error);
    return;
  }
  base.cameras[slot] = selected;
  if (slot == base.count)
    ++base.count;
  Settings candidate;
  base.capacity = kBlePeers;
  if (!copySettings(base, candidate)) {
    reply("Error", "Invalid");
    return;
  }
  draft_ = candidate;
  unresolved_draft_ |= outcome == SetupSelectionOutcome::Unresolved;
  reply("Ok", error,
        outcome == SetupSelectionOutcome::Unresolved
            ? "enabled=0,identity_verified=0,profile_qualified=0,save_allowed=0"
        : unresolved_draft_ ? "enabled=0,identity_verified=1,profile_qualified=0,save_allowed=0"
                            : "enabled=0,identity_verified=1,profile_qualified=0,save_allowed=1");
}
bool SetupService::takeSave(SetupSaveDispatch &out) {
  if (!save_ready_)
    return false;
  out = dispatch_;
  save_ready_ = false;
  return true;
}
void SetupService::completeSave(uint32_t session, uint32_t request, SaveOutcome outcome,
                                PersistResult result, uint32_t epoch, uint64_t generation) {
  if (!saving_ || session != save_session_ || request != save_request_ || epoch != epoch_ ||
      outcome == SaveOutcome::None || outcome == SaveOutcome::Accepted)
    return;
  saving_ = false;
  durable_ = outcome == SaveOutcome::Durable;
  terminal_outcome_ = outcome;
  terminal_generation_ = generation;
  terminal_result_ = result;
  if (!active_)
    return;
  terminal_ready_ = true;
  tick(now_);
}
void SetupService::setPrerequisiteReport(const char *report) {
  report_[0] = 0;
  if (!report)
    return;
  size_t n = strlen(report);
  if (n >= sizeof report_)
    return;
  for (size_t i = 0; i < n; ++i)
    if (static_cast<unsigned char>(report[i]) < 32 || report[i] == 127)
      return;
  strcpy(report_, report);
}
} // namespace ridesync

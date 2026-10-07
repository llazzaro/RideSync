#include "config_storage.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
namespace ridesync {
namespace {
constexpr size_t header = 24;
// Reconstruct the same non-const value in place: no SourceConfig-sized stack
// temporary or allocation, and defaults stay defined by SourceConfig itself.
// The exclusive owner holds no references into this value during replacement.
void defaultConfig(SourceConfig &config) {
  config.~SourceConfig();
  new (&config) SourceConfig;
}
static_assert(header + 2 + kMaxCameras * (3 + 64 + 17 + 17 + 5) + 24 <= kConfigRecordMax,
              "bounded record");
uint64_t get(const uint8_t *p, unsigned n) {
  uint64_t v = 0;
  for (unsigned i = 0; i < n; ++i)
    v |= uint64_t(p[i]) << (8 * i);
  return v;
}
void put(uint8_t *p, uint64_t v, unsigned n) {
  for (unsigned i = 0; i < n; ++i)
    p[i] = uint8_t(v >> (8 * i));
}
uint32_t crc(const ConfigRecord &r) {
  uint32_t c = 0xffffffffu;
  for (size_t i = 0; i < r.size; ++i) {
    if (i >= 20 && i < 24)
      continue;
    c ^= r.bytes[i];
    for (unsigned b = 0; b < 8; ++b)
      c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
  }
  return ~c;
}
bool settingsValid(const SourceConfig &c) {
  if (!validate(c).ok() || !validateButtonConfig(c.button))
    return false;
  const auto &g = c.button_gpio;
  if (g.pull != ButtonPull::External && g.pull != ButtonPull::Up && g.pull != ButtonPull::Down)
    return false;
  if (g.pin == -1)
    return !g.enabled && !g.board_qualified;
  auto probe = g;
  probe.enabled = true;
  probe.board_qualified = true;
  return validateButtonGpioConfig(probe) && (!g.enabled || g.board_qualified);
}
unsigned action(ButtonAction a) {
  switch (a) {
  case ButtonAction::RecordingIntent:
    return 0;
  case ButtonAction::WakeReconnect:
    return 1;
  case ButtonAction::Resync:
    return 2;
  }
  return 255;
}
unsigned model(CameraModel m) {
  switch (m) {
  case CameraModel::X5:
    return 1;
  case CameraModel::GO3S:
    return 2;
  case CameraModel::ONE_RS:
    return 3;
  case CameraModel::HERO12_BLACK:
    return 4;
  default:
    return 255;
  }
}
struct Reader {
  explicit Reader(const ConfigRecord &record) : r(record) {}
  const ConfigRecord &r;
  size_t at = header;
  bool ok = true;
  uint64_t number(unsigned n) {
    if (at + n > r.size) {
      ok = false;
      return 0;
    }
    auto v = get(r.bytes.data() + at, n);
    at += n;
    return v;
  }
  bool boolean() {
    auto v = number(1);
    if (v > 1)
      ok = false;
    return v == 1;
  }
  std::string string(size_t max) {
    auto n = number(1);
    if (!ok || n > max || at + n > r.size) {
      ok = false;
      return {};
    }
    std::string s(reinterpret_cast<const char *>(r.bytes.data() + at), size_t(n));
    at += n;
    return s;
  }
};
bool samePayload(const ConfigRecord &a, const ConfigRecord &b) {
  return a.size == b.size && a.size >= header &&
         std::equal(a.bytes.begin() + header, a.bytes.begin() + a.size, b.bytes.begin() + header);
}
} // namespace
void ConfigPersistence::scan() {
  auto &s = scan_;
  s.result = {};
  defaultConfig(s.config);
  s.winner = -1;
  s.generation = 0;
  s.blocked = s.migration = false;
  bool bad = false, future = false;
  auto &r = scratch_;
  for (unsigned i = 0; i < 2; ++i) {
    if (!store_.allowed()) {
      s.result = PersistStatus::Refused;
      s.blocked = true;
      return;
    }
    const auto io = store_.read(i, r);
    if (io.status == StoreStatus::Missing)
      continue;
    if (io.status == StoreStatus::Refused || io.status == StoreStatus::Error) {
      s.result = {io.status == StoreStatus::Refused ? PersistStatus::Refused
                                                    : PersistStatus::ReadError,
                  io.code};
      s.blocked = true;
      return;
    }
    if (io.status == StoreStatus::Oversized) {
      bad = true;
      continue;
    }
    SourceConfig c;
    uint64_t gen = 0;
    const auto decoded = decodeConfig(r, c, gen);
    if (decoded.status == PersistStatus::Future) {
      future = true;
      continue;
    }
    if (decoded.status != PersistStatus::Loaded && decoded.status != PersistStatus::Migrated) {
      bad = true;
      continue;
    }
    // Decoding has completed; raw bytes can now be replaced by canonical bytes.
    encodeConfig(c, gen, r);
    if (s.winner >= 0 && gen == s.generation) {
      if (!samePayload(r, s.canonical) || decoded.status != s.result.status) {
        s.result = PersistStatus::Ambiguous;
        s.blocked = true;
        return;
      }
    } else if (s.winner < 0 || gen > s.generation) {
      s.winner = int(i);
      s.generation = gen;
      s.config = std::move(c);
      s.canonical = r;
      s.result = decoded;
      s.migration = decoded.status == PersistStatus::Migrated;
    }
  }
  if (future) {
    s.result = PersistStatus::Future;
    s.blocked = true;
  } else if (s.winner < 0 && bad) {
    s.result = PersistStatus::Corrupt;
    s.blocked = true;
  } else if (bad && s.winner >= 0)
    s.result = PersistStatus::Recovered;
  return;
}
PersistResult encodeConfig(const SourceConfig &c, uint64_t generation, ConfigRecord &r) {
  if (!generation || !settingsValid(c))
    return PersistStatus::Invalid;
  r.bytes.fill(0);
  r.size = header;
  auto number = [&](uint64_t v, unsigned n) {
    put(r.bytes.data() + r.size, v, n);
    r.size += n;
  };
  auto string = [&](const std::string &s) {
    number(s.size(), 1);
    std::memcpy(r.bytes.data() + r.size, s.data(), s.size());
    r.size += s.size();
  };
  number(c.count, 1);
  number(c.capacity, 1);
  for (size_t i = 0; i < c.count; ++i) {
    const auto &p = c.cameras[i];
    string(p.name);
    string(p.identifier);
    string(p.wake_identifier);
    number(p.family == CameraFamily::Insta360 ? 1 : 2, 1);
    number(model(p.model), 1);
    number(p.address_type == AddressType::Public   ? 1
           : p.address_type == AddressType::Random ? 2
                                                   : 0,
           1);
    number(p.enabled, 1);
    number(p.gps_telemetry, 1);
  }
  const auto &b = c.button;
  number(b.debounce_ms, 4);
  number(b.long_ms, 4);
  number(b.double_ms, 4);
  number(b.double_enabled, 1);
  number(action(b.short_action), 1);
  number(action(b.long_action), 1);
  number(action(b.double_action), 1);
  const auto &g = c.button_gpio;
  number(g.enabled, 1);
  number(g.board_qualified, 1);
  number(uint32_t(g.pin), 4);
  number(g.pull == ButtonPull::External ? 0 : g.pull == ButtonPull::Up ? 1 : 2, 1);
  number(g.active_low, 1);
  std::memcpy(r.bytes.data(), "RSCF", 4);
  r.bytes[4] = 1;
  put(r.bytes.data() + 6, 2, 2);
  put(r.bytes.data() + 8, generation, 8);
  put(r.bytes.data() + 16, r.size - header, 2);
  put(r.bytes.data() + 20, crc(r), 4);
  return PersistStatus::Encoded;
}
PersistResult decodeConfig(const ConfigRecord &r, SourceConfig &out, uint64_t &generation) {
  if (r.size < header || r.size > kConfigRecordMax || std::memcmp(r.bytes.data(), "RSCF", 4))
    return PersistStatus::Corrupt;
  if (r.bytes[4] != 1)
    return PersistStatus::Future;
  if (r.bytes[5] || r.bytes[18] || r.bytes[19] || get(r.bytes.data() + 16, 2) != r.size - header ||
      get(r.bytes.data() + 20, 4) != crc(r) || !get(r.bytes.data() + 8, 8))
    return PersistStatus::Corrupt;
  auto schema = get(r.bytes.data() + 6, 2);
  if (schema > 2)
    return PersistStatus::Future;
  if (schema < 1)
    return PersistStatus::Corrupt;
  SourceConfig c;
  Reader reader{r};
  c.count = size_t(reader.number(1));
  c.capacity = size_t(reader.number(1));
  if (!c.capacity || c.capacity > kMaxCameras || c.count > c.capacity)
    return PersistStatus::Corrupt;
  for (size_t i = 0; i < c.count; ++i) {
    auto &p = c.cameras[i];
    p.name = reader.string(64);
    p.identifier = reader.string(17);
    p.wake_identifier = reader.string(17);
    auto f = reader.number(1), m = reader.number(1), a = reader.number(1);
    if (f < 1 || f > 2 || m < 1 || m > 4 || a > 2)
      return PersistStatus::Corrupt;
    p.family = f == 1 ? CameraFamily::Insta360 : CameraFamily::GoPro;
    const CameraModel models[] = {CameraModel::X5, CameraModel::GO3S, CameraModel::ONE_RS,
                                  CameraModel::HERO12_BLACK};
    p.model = models[m - 1];
    p.address_type = a == 1   ? AddressType::Public
                     : a == 2 ? AddressType::Random
                              : AddressType::Unknown;
    p.enabled = reader.boolean();
    p.gps_telemetry = reader.boolean();
  }
  if (schema == 2) {
    auto &b = c.button;
    b.debounce_ms = uint32_t(reader.number(4));
    b.long_ms = uint32_t(reader.number(4));
    b.double_ms = uint32_t(reader.number(4));
    b.double_enabled = reader.boolean();
    const ButtonAction actions[] = {ButtonAction::RecordingIntent, ButtonAction::WakeReconnect,
                                    ButtonAction::Resync};
    auto a = reader.number(1), l = reader.number(1), d = reader.number(1);
    if (a > 2 || l > 2 || d > 2)
      return PersistStatus::Corrupt;
    b.short_action = actions[a];
    b.long_action = actions[l];
    b.double_action = actions[d];
    auto &g = c.button_gpio;
    g.enabled = reader.boolean();
    g.board_qualified = reader.boolean();
    auto pin = reader.number(4);
    if (pin != 0xffffffffu && pin > 39)
      return PersistStatus::Corrupt;
    g.pin = pin == 0xffffffffu ? -1 : int(pin);
    auto pull = reader.number(1);
    if (pull > 2)
      return PersistStatus::Corrupt;
    g.pull = pull == 0 ? ButtonPull::External : pull == 1 ? ButtonPull::Up : ButtonPull::Down;
    g.active_low = reader.boolean();
  }
  if (!reader.ok || reader.at != r.size || !settingsValid(c))
    return PersistStatus::Corrupt;
  out = std::move(c);
  generation = get(r.bytes.data() + 8, 8);
  return schema == 1 ? PersistStatus::Migrated : PersistStatus::Loaded;
}
PersistResult ConfigPersistence::load(SourceConfig &out) {
  scan();
  auto &s = scan_;
  if (s.result.status == PersistStatus::Refused)
    pending_ = false;
  if (s.blocked)
    defaultConfig(out);
  else
    out = std::move(s.config);
  return result_ = s.result;
}
PersistResult ConfigPersistence::request(const SourceConfig &c, uint32_t now) {
  if (!store_.allowed()) {
    pending_ = false;
    return result_ = PersistStatus::Refused;
  }
  auto &r = scratch_;
  auto v = encodeConfig(c, 1, r);
  if (v.status != PersistStatus::Encoded)
    return result_ = v;
  if (pending_ && samePayload(r, desired_))
    return result_;
  desired_ = r;
  pending_ = true;
  latched_ = false;
  failures_ = 0;
  requested_ = now;
  return result_ = PersistStatus::Pending;
}
PersistResult ConfigPersistence::reset(uint32_t now) { return request(SourceConfig{}, now); }
PersistResult ConfigPersistence::retry(uint32_t now) {
  if (!store_.allowed()) {
    pending_ = false;
    return result_ = PersistStatus::Refused;
  }
  latched_ = false;
  failures_ = 0;
  requested_ = now;
  return result_ = pending_ ? PersistStatus::Pending : PersistStatus::Unchanged;
}
PersistResult ConfigPersistence::service(uint32_t now) {
  if (!store_.allowed()) {
    // Refusal discards the mailbox: reopening a gate must not replay commands.
    pending_ = false;
    return result_ = PersistStatus::Refused;
  }
  if (!pending_ || latched_)
    return result_;
  const uint32_t wait = failures_ > 1 ? 10000 : 5000;
  if (uint32_t(now - requested_) < 1000 || (attempted_ && uint32_t(now - attempted_at_) < wait))
    return result_;
  scan();
  auto &s = scan_;
  if (s.blocked) {
    latched_ = true;
    if (s.result.status == PersistStatus::Refused)
      pending_ = false;
    return result_ = s.result;
  }
  // Schema1 must be upgraded even if its decoded settings equal schema2.
  if (s.winner >= 0 && !s.migration && !uncertain_ && samePayload(s.canonical, desired_)) {
    pending_ = false;
    return result_ = PersistStatus::Unchanged;
  }
  if (s.generation == std::numeric_limits<uint64_t>::max()) {
    latched_ = true;
    return result_ = PersistStatus::GenerationLimit;
  }
  auto &r = desired_;
  put(r.bytes.data() + 8, s.generation + 1, 8);
  put(r.bytes.data() + 20, crc(r), 4);
  const unsigned target = s.winner == 0 ? 1 : 0;
  attempted_ = true;
  attempted_at_ = now;
  const auto io = store_.write(target, r);
  if (io.status == StoreStatus::Ok) {
    auto &verified = scratch_;
    const auto read =
        store_.allowed() ? store_.read(target, verified) : StoreResult{StoreStatus::Refused};
    if (store_.allowed() && read.status == StoreStatus::Ok && verified.size == r.size &&
        std::equal(r.bytes.begin(), r.bytes.begin() + r.size, verified.bytes.begin())) {
      pending_ = false;
      failures_ = 0;
      uncertain_ = false;
      return result_ = PersistStatus::Durable;
    }
    result_ = {read.status == StoreStatus::Refused || !store_.allowed()
                   ? PersistStatus::Refused
                   : PersistStatus::VerificationError,
               read.code};
  } else
    result_ = {io.status == StoreStatus::Refused       ? PersistStatus::Refused
               : io.status == StoreStatus::SetError    ? PersistStatus::WriteError
               : io.status == StoreStatus::CommitError ? PersistStatus::CommitError
                                                       : PersistStatus::Indeterminate,
               io.code};
  if (result_.status == PersistStatus::Refused)
    pending_ = false;
  uncertain_ = true;
  result_.indeterminate = true;
  result_.cause = result_.status;
  if (++failures_ >= 3) {
    latched_ = true;
    result_.status = PersistStatus::Latched;
  }
  return result_;
}
} // namespace ridesync

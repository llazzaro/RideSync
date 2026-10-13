#include "setup_discovery.h"
#include <cctype>
#include <cstdio>
#include <cstring>
namespace ridesync {
namespace {
bool stable(const BondIdentity &id) {
  bool nonzero = false, all_ff = true;
  for (auto byte : id.address) {
    nonzero |= byte != 0;
    all_ff &= byte == 0xff;
  }
  return id.verified && nonzero && !all_ff &&
         (id.type == IdentityType::Public ||
          (id.type == IdentityType::RandomStatic && (id.address[5] & 0xc0) == 0xc0));
}
bool same(const BondIdentity &a, const BondIdentity &b) {
  return stable(a) && stable(b) && a.type == b.type && a.address == b.address;
}
bool equalAddress(const std::string &a, const std::string &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::toupper(static_cast<unsigned char>(a[i])) !=
        std::toupper(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}
CameraFamily family(CameraModel m) {
  switch (m) {
  case CameraModel::GO3S:
  case CameraModel::ONE_RS:
    return CameraFamily::Insta360;
  case CameraModel::HERO12_BLACK:
    return CameraFamily::GoPro;
  case CameraModel::DJI_ACTION4:
  case CameraModel::DJI_ACTION5_PRO:
  case CameraModel::DJI_ACTION6:
  case CameraModel::DJI_OSMO360:
    return CameraFamily::DJI;
  default:
    return CameraFamily::Unknown;
  }
}
std::string address(const std::array<uint8_t, 6> &a) {
  char text[18];
  std::snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1],
                a[0]);
  return text;
}
} // namespace
bool SetupDiscovery::begin(bool qualified, uint32_t now) {
  if (initialized_ || !qualified)
    return false;
  initialized_ = central_.begin(true, true, now);
  outcome_ = initialized_ ? SetupDiscoveryOutcome::Idle : SetupDiscoveryOutcome::Unavailable;
  return initialized_;
}
void SetupDiscovery::initialize(bool qualified, uint32_t now) {
  if (!start_requested_)
    return;
  start_requested_ = false;
  if (!qualified || !begin(true, now)) {
    pending_scan_ = false;
    outcome_ = SetupDiscoveryOutcome::Unavailable;
    return;
  }
  outcome_ = SetupDiscoveryOutcome::Initializing;
  service(now);
}
bool SetupDiscovery::scan(uint32_t session, uint32_t now) {
  if (!session || start_requested_ || pending_scan_)
    return false;
  if (!initialized_) {
    session_ = session;
    count_ = 0;
    candidates_ = {};
    truncated_ = false;
    start_requested_ = pending_scan_ = true;
    outcome_ = SetupDiscoveryOutcome::Initializing;
    return true;
  }
  if (!initialized_ || !session || !released() || !central_.scan(10000, now))
    return false;
  candidates_ = {};
  count_ = 0;
  truncated_ = false;
  session_ = session;
  generation_ = central_.scanGeneration();
  outcome_ = SetupDiscoveryOutcome::Scanning;
  return true;
}
void SetupDiscovery::cancel(uint32_t session) {
  if (session != session_)
    return;
  if (pending_scan_) {
    start_requested_ = pending_scan_ = false;
    outcome_ = SetupDiscoveryOutcome::Complete;
    return;
  }
  if (released())
    return;
  outcome_ = SetupDiscoveryOutcome::Releasing;
  central_.cancelScan();
}
void SetupDiscovery::service(uint32_t now) {
  if (!initialized_)
    return;
  central_.service(now);
  if (pending_scan_) {
    if (host_.state() == BleHostState::Failed || central_.stopped()) {
      pending_scan_ = false;
      outcome_ = SetupDiscoveryOutcome::Failed;
    } else if (hostReady()) {
      pending_scan_ = false;
      if (!scan(session_, now))
        outcome_ = SetupDiscoveryOutcome::Failed;
    }
  }
  if (outcome_ == SetupDiscoveryOutcome::Releasing && released())
    outcome_ = SetupDiscoveryOutcome::Complete;
}
void SetupDiscovery::result(const BleResult &r) {
  if (r.event.generation != generation_)
    return;
  if (r.kind == BleResultKind::Advertisement && outcome_ == SetupDiscoveryOutcome::Scanning)
    observe(r.event);
  if (r.kind == BleResultKind::ScanComplete) {
    outcome_ =
        (r.fault == BleFault::None || r.fault == BleFault::Stopped || r.fault == BleFault::Timeout)
            ? SetupDiscoveryOutcome::Releasing
            : SetupDiscoveryOutcome::Failed;
  }
  if (r.kind == BleResultKind::ScanCancelFailed)
    outcome_ = SetupDiscoveryOutcome::Failed;
}
void SetupDiscovery::observe(const BleEvent &e) {
  if (e.observed_address_type > 1 || e.size > e.bytes.size())
    return;
  size_t i = 0;
  for (; i < count_; ++i)
    if (candidates_[i].address_type == e.observed_address_type &&
        candidates_[i].address == e.identity.address)
      break;
  if (i == count_) {
    if (count_ == candidates_.size()) {
      truncated_ = true;
      return;
    }
    ++count_;
    candidates_[i].address = e.identity.address;
    candidates_[i].address_type = e.observed_address_type;
  }
  auto &c = candidates_[i];
  c.rssi = e.rssi;
  for (size_t p = 0; p < e.size;) {
    const size_t length = e.bytes[p++];
    if (!length)
      break;
    if (length > e.size - p) {
      c.hints_truncated = true;
      break;
    }
    const uint8_t type = e.bytes[p];
    const size_t n = length - 1;
    if (type == 8 || type == 9) {
      const size_t copy = n < sizeof(c.name) - 1 ? n : sizeof(c.name) - 1;
      for (size_t j = 0; j < copy; ++j) {
        const uint8_t ch = e.bytes[p + 1 + j];
        c.name[j] = ch >= 32 && ch != 127 ? ch : '?';
      }
      c.name[copy] = 0;
      c.hints_truncated |= copy != n;
    } else if (type == 2 || type == 3 || type == 6 || type == 7) {
      const size_t width = type <= 3 ? 2 : 16;
      if (n % width)
        c.hints_truncated = true;
      for (size_t j = 0; j + width <= n; j += width) {
        BleUuid uuid;
        uuid.size = width;
        std::memcpy(uuid.bytes.data(), e.bytes.data() + p + 1 + j, width);
        bool found = false;
        for (size_t k = 0; k < c.service_count; ++k)
          found |= c.services[k] == uuid;
        if (!found && c.service_count < c.services.size())
          c.services[c.service_count++] = uuid;
        else if (!found)
          c.hints_truncated = true;
      }
    }
    p += length;
  }
}
void SetupDiscovery::setIdentityMappings(const SetupIdentityMapping *maps, size_t n) {
  mappings_ = {};
  mapping_count_ = 0;
  if (!maps || n > mappings_.size())
    return;
  for (size_t i = 0; i < n; ++i)
    if (stable(maps[i].identity))
      mappings_[mapping_count_++] = maps[i];
}
const BondIdentity *SetupDiscovery::verified(const SetupCandidate &c) const {
  const BondIdentity *result = nullptr;
  for (size_t i = 0; i < mapping_count_; ++i) {
    const auto &m = mappings_[i];
    if (m.observed == c.address && m.observed_type == c.address_type) {
      if (result && !same(*result, m.identity))
        return nullptr;
      result = &m.identity;
    }
  }
  return result;
}
SetupSelectionOutcome SetupDiscovery::select(uint32_t session, size_t index, CameraModel model,
                                             const SourceConfig &base, size_t slot,
                                             CameraConfig &out) {
  if (model == CameraModel::X5)
    return SetupSelectionOutcome::X5Manual;
  if (family(model) == CameraFamily::Unknown)
    return SetupSelectionOutcome::Unsupported;
  if (session != session_ || index >= count_)
    return SetupSelectionOutcome::Stale;
  if (!released())
    return SetupSelectionOutcome::Busy;
  if (base.count > kBlePeers || slot > base.count || slot >= kBlePeers ||
      (slot == base.count && base.count == kBlePeers))
    return SetupSelectionOutcome::Capacity;
  const auto &c = candidates_[index];
  const auto *id = verified(c);
  if (id) {
    for (size_t i = 0; i < base.count; ++i)
      if (i != slot) {
        for (size_t j = 0; j < mapping_count_; ++j)
          if ((equalAddress(base.cameras[i].identifier, address(mappings_[j].identity.address)) ||
               (equalAddress(base.cameras[i].identifier, address(mappings_[j].observed)) &&
                base.cameras[i].address_type == (mappings_[j].observed_type == 0
                                                     ? AddressType::Public
                                                     : AddressType::Random))) &&
              same(*id, mappings_[j].identity))
            return SetupSelectionOutcome::DuplicateIdentity;
      }
    const auto admission = host_.bondAdmission(*id);
    const bool dji = family(model) == CameraFamily::DJI;
    const bool guarded_store = admission.stack_ready && admission.restore_verified &&
                               admission.refusal_installed && admission.persistence_allowed &&
                               admission.capacity && admission.used <= admission.capacity;
    if ((dji && !guarded_store) || (!dji && !admission.allowed()))
      return SetupSelectionOutcome::BondCapacity;
  }
  CameraConfig selected;
  selected.model = model;
  selected.family = family(model);
  selected.name = "Camera"; // Advertised bytes remain hints; operator supplies the label.
  selected.identifier = address(id ? id->address : c.address);
  selected.address_type =
      id ? (id->type == IdentityType::Public ? AddressType::Public : AddressType::Random)
         : (c.address_type == 0 ? AddressType::Public : AddressType::Random);
  selected.enabled = false;
  SourceConfig candidate = base;
  candidate.cameras[slot] = selected;
  if (slot == candidate.count)
    ++candidate.count;
  if (!validate(candidate).ok())
    return SetupSelectionOutcome::DuplicateIdentity;
  out = selected;
  return id ? SetupSelectionOutcome::NeedsCommissioning : SetupSelectionOutcome::Unresolved;
}
} // namespace ridesync

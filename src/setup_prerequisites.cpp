#include "setup_prerequisites.h"
#include "profiles/mixed_camera.h"
#include "profiles/qualification.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace ridesync {
namespace {
bool isDji(CameraModel m) { return m >= CameraModel::DJI_ACTION4 && m <= CameraModel::DJI_OSMO360; }
bool failed(SetupEvidence e) {
  return e == SetupEvidence::Mismatched || e == SetupEvidence::Revoked ||
         e == SetupEvidence::Refused || e == SetupEvidence::Denied || e == SetupEvidence::Timeout;
}
SetupReadiness readiness(const SetupCameraEvidence &e) {
  if (e.profile == SetupEvidence::Unsupported || e.firmware == SetupEvidence::Mismatched ||
      e.api == SetupEvidence::Mismatched || e.firmware == SetupEvidence::Unsupported ||
      e.api == SetupEvidence::Unsupported || e.wake == SetupEvidence::Unsupported ||
      e.gps == SetupEvidence::Unsupported)
    return SetupReadiness::UnsupportedProfile;
  const SetupEvidence values[] = {e.identity, e.store, e.profile, e.firmware,
                                  e.api,      e.wake,  e.gps,     e.runtime};
  bool missing = false, unknown = false;
  for (auto value : values) {
    if (failed(value))
      return SetupReadiness::Refused;
    missing |= value == SetupEvidence::Missing;
    unknown |= value == SetupEvidence::Unknown;
  }
  if (missing)
    return SetupReadiness::NeedsCommissioning;
  if (unknown)
    return SetupReadiness::Unknown;
  return SetupReadiness::Ready;
}
int severity(SetupReadiness r) {
  switch (r) {
  case SetupReadiness::Refused:
    return 4;
  case SetupReadiness::UnsupportedProfile:
    return 3;
  case SetupReadiness::NeedsCommissioning:
    return 2;
  case SetupReadiness::Unknown:
    return 1;
  default:
    return 0;
  }
}
SetupEvidence identity(const CameraConfig &c, const BondIdentity &id) {
  if (!qualification::resolvedIdentity(id))
    return SetupEvidence::Missing;
  const bool type = id.type == IdentityType::Public ? c.address_type == AddressType::Public
                                                    : c.address_type == AddressType::Random;
  return type && qualification::identifierAddress(c.identifier, id, true)
             ? SetupEvidence::Matched
             : SetupEvidence::Mismatched;
}
template <typename T> SetupEvidence firmware(const T &bytes, size_t size) {
  if (!size)
    return SetupEvidence::Missing;
  return size <= bytes.size() && qualification::printable(bytes.data(), size, 0x20)
             ? SetupEvidence::Unknown
             : SetupEvidence::Unsupported;
}
SetupEvidence present(bool value) {
  return value ? SetupEvidence::Matched : SetupEvidence::Missing;
}
} // namespace
SetupPrerequisiteReport setupPrerequisites(const SourceConfig &s,
                                           const SetupPrerequisiteEvidence &e) {
  SetupPrerequisiteReport r;
  r.settings = validate(s).ok() ? SetupEvidence::Matched : SetupEvidence::Refused;
  r.group = s.count <= 4 && s.capacity <= 4 ? SetupEvidence::Matched : SetupEvidence::Unsupported;
  r.provider = e.provider;
  r.count = static_cast<uint8_t>(std::min(s.count, kMaxCameras));
  r.truncated = s.count > kMaxCameras;
  r.overall = SetupReadiness::Ready;
  for (size_t i = 0; i < r.count; ++i) {
    auto &p = r.cameras[i];
    const auto &c = s.cameras[i];
    p.slot = static_cast<uint8_t>(i);
    p.model = c.model;
    p.enabled = c.enabled;
    p.evidence = e.cameras[i];
    if (c.model == CameraModel::X5 && i != 0)
      r.group = SetupEvidence::Unsupported;
    if (!c.gps_telemetry)
      p.evidence.gps = SetupEvidence::NotRequested;
    else if (c.model != CameraModel::ONE_RS)
      p.evidence.gps = SetupEvidence::Unsupported;
    if (c.wake_identifier.empty())
      p.evidence.wake = SetupEvidence::NotRequested;
    else if (c.model != CameraModel::X5)
      p.evidence.wake = SetupEvidence::Unsupported;
    if (c.model != CameraModel::HERO12_BLACK)
      p.evidence.api = SetupEvidence::NotRequested;
    // DJI application authorization does not assert SMP/encryption/bond restoration.
    if (isDji(c.model))
      p.evidence.store = SetupEvidence::NotRequested;
    p.readiness = readiness(p.evidence);
    if (!c.enabled)
      p.readiness = SetupReadiness::Refused;
    if (severity(p.readiness) > severity(r.overall))
      r.overall = p.readiness;
  }
  if (r.group == SetupEvidence::Unsupported)
    r.overall = SetupReadiness::UnsupportedProfile;
  if (e.provider == SetupProvider::Missing)
    r.overall = SetupReadiness::NeedsCommissioning;
  if (e.provider == SetupProvider::Unknown && severity(r.overall) < 2)
    r.overall = SetupReadiness::Unknown;
  if (r.settings == SetupEvidence::Refused || e.provider == SetupProvider::Declined ||
      e.provider == SetupProvider::Refused)
    r.overall = SetupReadiness::Refused;
  return r;
}
SetupPrerequisiteEvidence setupQualificationEvidence(const SourceConfig &s,
                                                     const MixedCameraQualifications *q,
                                                     SetupProvider provider) {
  SetupPrerequisiteEvidence out;
  out.provider = provider;
  if (!q)
    return out;
  for (size_t i = 0; i < std::min(s.count, kMaxCameras); ++i) {
    const auto &c = s.cameras[i];
    auto &e = out.cameras[i];
    switch (c.model) {
    case CameraModel::X5:
      e.identity = identity(c, q->x5.identity);
      e.profile = present(q->x5.enabled && q->x5.display.profile ==
                                               insta360::Ce80DisplayProfile::X5CapturedDisplayV1);
      e.firmware = firmware(q->x5.firmware, q->x5.firmware_size);
      e.store = q->x5.store.qualification_record ? SetupEvidence::Unknown : SetupEvidence::Missing;
      break;
    case CameraModel::GO3S:
      e.identity = identity(c, q->go3s[i].identity);
      e.profile = present(q->go3s[i].source_qualified);
      e.firmware = firmware(q->go3s[i].firmware, q->go3s[i].firmware_size);
      if (e.firmware == SetupEvidence::Unknown &&
          (q->go3s[i].firmware_size != 8 || std::memcmp(q->go3s[i].firmware.data(), "8.0.4.11", 8)))
        e.firmware = SetupEvidence::Unsupported;
      if (!q->go3s[i].authorization_size)
        e.profile = SetupEvidence::Missing;
      break;
    case CameraModel::ONE_RS:
      e.identity = identity(c, q->one_rs[i].identity);
      e.profile = present(q->one_rs[i].source_qualified && q->one_rs[i].core_one_rs &&
                          q->one_rs[i].ordinary_360_lens && q->one_rs[i].video_mode_declared);
      e.firmware = firmware(q->one_rs[i].firmware, q->one_rs[i].firmware_size);
      break;
    case CameraModel::HERO12_BLACK:
      e.identity = identity(c, q->hero12[i].identity);
      e.profile = present(q->hero12[i].source_qualified && q->hero12[i].classic_profile_confirmed);
      e.firmware = firmware(q->hero12[i].firmware, q->hero12[i].firmware_size);
      e.api = q->hero12[i].api_major ? SetupEvidence::Unknown : SetupEvidence::Missing;
      break;
    default:
      if (!isDji(c.model)) {
        e.profile = SetupEvidence::Unsupported;
        break;
      }
      e.identity = identity(c, q->dji[i].identity);
      e.profile = present(q->dji[i].source_qualified && q->dji[i].controller_mac_verified &&
                          q->dji[i].controller_id && q->dji[i].connection_challenge <= 9999 &&
                          qualification::addressBytes(q->dji[i].controller_mac) &&
                          q->dji[i].unencrypted_protocol_reviewed);
      if (q->dji[i].model != c.model)
        e.profile = SetupEvidence::Unsupported;
      e.firmware = firmware(q->dji[i].firmware, q->dji[i].firmware_size);
      break;
    }
    if (!c.wake_identifier.empty() && c.model == CameraModel::X5)
      e.wake = present(q->x5_wake.enabled && q->x5_wake.source_qualified &&
                       c.wake_identifier.size() == 6 &&
                       std::equal(q->x5_wake.identifier.begin(), q->x5_wake.identifier.end(),
                                  c.wake_identifier.begin()));
    if (c.gps_telemetry && c.model == CameraModel::ONE_RS)
      e.gps = present(q->gps[i].enabled && q->gps[i].source_qualified);
    // Runtime/store values stay unknown: provider declarations are not observations.
  }
  return out;
}
const char *setupCommissioningGuidance(CameraModel m) {
  switch (m) {
  case CameraModel::X5:
    return "Camera initiates CE80 from remote UI; review stable identity, store receipt/revocation "
           "and captured-display profile; require actual CE82 subscription. First-time bond "
           "creation remains manual/unresolved.";
  case CameraModel::GO3S:
    return "Select camera, not Pod; retain separate camera/Pod firmware and approved stable "
           "identity/bond. MTU, subscription, SYNC and CheckAuth are separate observed "
           "prerequisites; recording state remains Unknown.";
  case CameraModel::ONE_RS:
    return "Experimental / Not tested BE80 route; use camera-side remote selection and reviewed "
           "stable identity/bond, core/lens/video profile. Universal authorization and recording "
           "state remain unresolved; ATT delivery is not recording evidence.";
  case CameraModel::HERO12_BLACK:
    return "Camera-approved pairing/bond, pairing-finish and external-control claim must succeed; "
           "actual Hardware Info model/firmware and API must match reviewed profile. Documentary "
           "minimum firmware and bond alone do not qualify.";
  case CameraModel::DJI_ACTION4:
  case CameraModel::DJI_ACTION5_PRO:
  case CameraModel::DJI_ACTION6:
  case CameraModel::DJI_OSMO360:
    return "Review installed firmware, stable identity, controller identity/challenge and "
           "unencrypted protocol; new pair requires correlated camera-screen grant. Prior pairing "
           "is separate; denial/timeout refuses. No SMP/encryption or Bonded claim.";
  default:
    return "Unsupported profile; select a supported reviewed model. A model declaration never "
           "creates identity or qualification.";
  }
}
const char *setupEvidenceName(SetupEvidence e) {
  const char *names[] = {"unknown",     "missing", "matched",       "mismatched", "revoked",
                         "unsupported", "refused", "not_requested", "denied",     "timeout"};
  const auto n = static_cast<unsigned>(e);
  return n < 10 ? names[n] : "unknown";
}
const char *setupReadinessName(SetupReadiness e) {
  const char *names[] = {"unknown", "needs_commissioning", "unsupported_profile", "refused",
                         "ready"};
  const auto n = static_cast<unsigned>(e);
  return n < 5 ? names[n] : "unknown";
}
const char *setupProviderName(SetupProvider e) {
  const char *names[] = {"missing", "declined", "available", "refused", "unknown"};
  const auto n = static_cast<unsigned>(e);
  return n < 5 ? names[n] : "unknown";
}
size_t encodePrerequisiteReport(const SetupPrerequisiteReport &r, char *out, size_t cap) {
  if (!out || !cap)
    return 0;
  out[0] = 0;
  if (r.count > kMaxCameras)
    return 0;
  int n = std::snprintf(out, cap, "P1;%u,%u,%u,%u,%u,%u", unsigned(r.settings), unsigned(r.group),
                        unsigned(r.provider), unsigned(r.overall), unsigned(r.count),
                        unsigned(r.truncated));
  if (n < 0 || size_t(n) >= cap) {
    out[0] = 0;
    return 0;
  }
  size_t used = size_t(n);
  for (size_t i = 0; i < r.count; ++i) {
    const auto &p = r.cameras[i];
    const auto &e = p.evidence;
    n = std::snprintf(out + used, cap - used, ";%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                      unsigned(p.slot), unsigned(p.model), unsigned(p.enabled),
                      unsigned(p.readiness), unsigned(e.identity), unsigned(e.store),
                      unsigned(e.profile), unsigned(e.firmware), unsigned(e.api), unsigned(e.wake),
                      unsigned(e.gps), unsigned(e.runtime));
    if (n < 0 || size_t(n) >= cap - used) {
      out[0] = 0;
      return 0;
    }
    used += size_t(n);
  }
  return used;
}
} // namespace ridesync

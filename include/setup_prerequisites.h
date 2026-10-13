#pragma once
#include "config.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace ridesync {
// These values describe copied evidence, never commands or writable qualifications.
enum class SetupEvidence : uint8_t {
  Unknown,
  Missing,
  Matched,
  Mismatched,
  Revoked,
  Unsupported,
  Refused,
  NotRequested,
  Denied,
  Timeout
};
enum class SetupProvider : uint8_t { Missing, Declined, Available, Refused, Unknown };
enum class SetupReadiness : uint8_t {
  Unknown,
  NeedsCommissioning,
  UnsupportedProfile,
  Refused,
  Ready
};
struct SetupCameraEvidence {
  SetupEvidence identity = SetupEvidence::Unknown, store = SetupEvidence::Unknown;
  SetupEvidence profile = SetupEvidence::Unknown, firmware = SetupEvidence::Unknown;
  SetupEvidence api = SetupEvidence::Unknown, wake = SetupEvidence::Unknown;
  SetupEvidence gps = SetupEvidence::Unknown, runtime = SetupEvidence::Unknown;
};
struct SetupPrerequisiteEvidence {
  SetupProvider provider = SetupProvider::Missing;
  std::array<SetupCameraEvidence, kMaxCameras> cameras{};
};
struct SetupCameraPrerequisite {
  uint8_t slot = 0;
  CameraModel model = CameraModel::Unknown;
  bool enabled = false;
  SetupReadiness readiness = SetupReadiness::Unknown;
  SetupCameraEvidence evidence;
};
// No strings, addresses, digests, receipts, challenge or controller values.
struct SetupPrerequisiteReport {
  SetupEvidence settings = SetupEvidence::Unknown, group = SetupEvidence::Unknown;
  SetupProvider provider = SetupProvider::Missing;
  SetupReadiness overall = SetupReadiness::Unknown;
  uint8_t count = 0;
  bool truncated = false;
  std::array<SetupCameraPrerequisite, kMaxCameras> cameras{};
};
SetupPrerequisiteReport setupPrerequisites(const SourceConfig &, const SetupPrerequisiteEvidence &);
// Trusted read-only adapter/provider extraction only. Null means unavailable;
// declared private evidence does not establish live store restoration or readiness.
struct MixedCameraQualifications;
SetupPrerequisiteEvidence
setupQualificationEvidence(const SourceConfig &, const MixedCameraQualifications *, SetupProvider);
const char *setupCommissioningGuidance(CameraModel);
const char *setupEvidenceName(SetupEvidence);
const char *setupReadinessName(SetupReadiness);
const char *setupProviderName(SetupProvider);
// ASCII numeric contract P1;settings,group,provider,overall,count,truncated; then
// slot,model,enabled,readiness,identity,store,profile,firmware,api,wake,gps,runtime.
// Returns zero and clears destination on overflow; never returns partial output.
size_t encodePrerequisiteReport(const SetupPrerequisiteReport &, char *, size_t);
} // namespace ridesync

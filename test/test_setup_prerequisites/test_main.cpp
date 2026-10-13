#include "profiles/mixed_camera.h"
#include "setup_prerequisites.h"
#ifndef RIDESYNC_MANUAL_TEST
#include <unity.h>
#endif
#include <cassert>
#include <cstring>
using namespace ridesync;
void report_cases() {
  SourceConfig s;
  s.count = 1;
  s.capacity = 4;
  s.cameras[0].name = "camera";
  s.cameras[0].identifier = "12:34:56:78:90:AB";
  s.cameras[0].address_type = AddressType::Public;
  s.cameras[0].model = CameraModel::HERO12_BLACK;
  s.cameras[0].family = CameraFamily::GoPro;
  SetupPrerequisiteEvidence e;
  auto r = setupPrerequisites(s, e);
  assert(r.overall == SetupReadiness::NeedsCommissioning);
  e.provider = SetupProvider::Available;
  auto &p = e.cameras[0];
  p.identity = p.store = p.profile = p.firmware = p.api = SetupEvidence::Matched;
  p.runtime = SetupEvidence::Matched;
  r = setupPrerequisites(s, e);
  assert(r.overall == SetupReadiness::Ready);
  p.store = SetupEvidence::Revoked;
  assert(setupPrerequisites(s, e).overall == SetupReadiness::Refused);
  p.store = SetupEvidence::Mismatched;
  assert(setupPrerequisites(s, e).overall == SetupReadiness::Refused);
  p.store = SetupEvidence::Matched;
  p.firmware = SetupEvidence::Mismatched;
  assert(setupPrerequisites(s, e).overall == SetupReadiness::UnsupportedProfile);
  p.firmware = SetupEvidence::Unknown;
  assert(setupPrerequisites(s, e).overall == SetupReadiness::Unknown);
  p.firmware = SetupEvidence::Matched;
  p.runtime = SetupEvidence::Denied;
  assert(setupPrerequisites(s, e).overall == SetupReadiness::Refused);
  p.runtime = SetupEvidence::Timeout;
  assert(setupPrerequisites(s, e).overall == SetupReadiness::Refused);
  p.runtime = SetupEvidence::Matched;
  e.provider = SetupProvider::Declined;
  assert(setupPrerequisites(s, e).overall == SetupReadiness::Refused);
  e.provider = SetupProvider::Available;
  s.cameras[0].gps_telemetry = true;
  assert(setupPrerequisites(s, e).overall == SetupReadiness::UnsupportedProfile);
  s.cameras[0].gps_telemetry = false;
  p.identity = SetupEvidence::Unknown;
  assert(setupPrerequisites(s, e).overall == SetupReadiness::Unknown);
  p.identity = SetupEvidence::Matched;
  char output[768];
  r = setupPrerequisites(s, e);
  assert(encodePrerequisiteReport(r, output, sizeof output));
  assert(!std::strstr(output, "12:34"));
  assert(!std::strstr(output, "camera"));
  char small[8] = "secret";
  assert(!encodePrerequisiteReport(r, small, sizeof small));
  assert(small[0] == 0);
  for (int model = 1; model <= 8; ++model)
    assert(std::strlen(setupCommissioningGuidance(CameraModel(model))) > 20);
  s.count = 5;
  assert(setupPrerequisites(s, e).group == SetupEvidence::Unsupported);
  e.provider = SetupProvider::Missing;
  assert(setupPrerequisites(s, e).group == SetupEvidence::Unsupported);
}
void extraction_cases() {
  SourceConfig s;
  s.count = 1;
  s.capacity = 4;
  auto &c = s.cameras[0];
  c.name = "hostile\nname";
  c.identifier = "12:34:56:78:90:AB";
  c.model = CameraModel::GO3S;
  c.family = CameraFamily::Insta360;
  c.address_type = AddressType::Public;
  MixedCameraQualifications q;
  auto &g = q.go3s[0];
  g.source_qualified = true;
  g.authorization_size = 1;
  g.firmware_size = 8;
  std::memcpy(g.firmware.data(), "8.0.4.11", 8);
  auto e = setupQualificationEvidence(s, &q, SetupProvider::Available);
  assert(e.cameras[0].identity == SetupEvidence::Missing);
  assert(e.cameras[0].store == SetupEvidence::Unknown);
  assert(e.cameras[0].runtime == SetupEvidence::Unknown);
  assert(e.cameras[0].firmware == SetupEvidence::Unknown);
  g.identity.verified = true;
  g.identity.type = IdentityType::Public;
  g.identity.address = {{0xAB, 0x90, 0x78, 0x56, 0x34, 0x12}};
  e = setupQualificationEvidence(s, &q, SetupProvider::Available);
  assert(e.cameras[0].identity == SetupEvidence::Matched);
  g.identity.address[0] = 1;
  assert(setupQualificationEvidence(s, &q, SetupProvider::Available).cameras[0].identity ==
         SetupEvidence::Mismatched);
  g.firmware[0] = '9';
  assert(setupQualificationEvidence(s, &q, SetupProvider::Available).cameras[0].firmware ==
         SetupEvidence::Unsupported);
  char output[768];
  auto r = setupPrerequisites(s, e);
  assert(encodePrerequisiteReport(r, output, sizeof output));
  assert(!std::strstr(output, "hostile"));
}
int main() {
#ifdef RIDESYNC_MANUAL_TEST
  report_cases();
  extraction_cases();
  return 0;
#else
  UNITY_BEGIN();
  RUN_TEST(report_cases);
  RUN_TEST(extraction_cases);
  return UNITY_END();
#endif
}

#include "config.h"
#include <unity.h>
using namespace ridesync;

CameraConfig camera(unsigned i = 0) {
  CameraConfig c;
  c.name = "Front";
  c.family = CameraFamily::Insta360;
  c.model = CameraModel::X5;
  c.identifier = i == 0 ? "01:23:45:67:89:AB" : "01:23:45:67:89:AC";
  c.address_type = AddressType::Public;
  return c;
}
void counts_and_capacity() {
  SourceConfig c;
  TEST_ASSERT_TRUE(validate(c).ok());
  c.count = 1;
  c.cameras[0] = camera();
  TEST_ASSERT_TRUE(validate(c).ok());
  c.count = 4;
  for (size_t i = 0; i < c.count; ++i) {
    c.cameras[i] = camera();
    c.cameras[i].identifier.back() = "ABCD"[i];
  }
  TEST_ASSERT_TRUE(validate(c).ok());
  c.capacity = 3;
  TEST_ASSERT_EQUAL((int)ConfigError::CapacityExceeded, (int)validate(c).error);
  c.capacity = kMaxCameras + 1;
  TEST_ASSERT_EQUAL((int)ConfigError::InvalidCapacity, (int)validate(c).error);
  c.capacity = kMaxCameras;
  c.count = kMaxCameras;
  for (size_t i = 0; i < c.count; ++i) {
    c.cameras[i] = camera();
    c.cameras[i].identifier.back() = "01234567"[i];
  }
  TEST_ASSERT_TRUE(validate(c).ok());
  c.count = kMaxCameras + 1;
  TEST_ASSERT_EQUAL((int)ConfigError::CapacityExceeded, (int)validate(c).error);
}
void invalid_entries_are_actionable() {
  SourceConfig c;
  c.count = 1;
  c.cameras[0] = camera();
  c.cameras[0].identifier = "bad";
  auto r = validate(c);
  TEST_ASSERT_EQUAL((int)ConfigError::InvalidIdentifier, (int)r.error);
  TEST_ASSERT_EQUAL(0, r.index);
  TEST_ASSERT_NOT_EQUAL('\0', r.message[0]);
  c.cameras[0] = camera();
  c.cameras[0].model = CameraModel::Unknown;
  TEST_ASSERT_EQUAL((int)ConfigError::UnknownModel, (int)validate(c).error);
  c.cameras[0] = camera();
  c.cameras[0].family = CameraFamily::GoPro;
  TEST_ASSERT_EQUAL((int)ConfigError::FamilyMismatch, (int)validate(c).error);
  c.cameras[0] = camera();
  c.cameras[0].address_type = AddressType::Unknown;
  TEST_ASSERT_EQUAL((int)ConfigError::InvalidAddressType, (int)validate(c).error);
  c.cameras[0] = camera();
  c.cameras[0].wake_identifier = "not-a-mac";
  TEST_ASSERT_EQUAL((int)ConfigError::InvalidWakeIdentifier, (int)validate(c).error);
  c.cameras[0] = camera();
  c.cameras[0].name = "";
  TEST_ASSERT_EQUAL((int)ConfigError::InvalidName, (int)validate(c).error);
}
void duplicates_and_disabled_placeholders() {
  SourceConfig c;
  c.count = 2;
  c.cameras[0] = camera();
  c.cameras[1] = camera();
  c.cameras[1].identifier = "01:23:45:67:89:ab";
  TEST_ASSERT_EQUAL((int)ConfigError::DuplicateIdentifier, (int)validate(c).error);
  c.cameras[1].enabled = false;
  TEST_ASSERT_EQUAL((int)ConfigError::DuplicateIdentifier, (int)validate(c).error);
  c.cameras[1].identifier.clear();
  TEST_ASSERT_TRUE(validate(c).ok());
  c.cameras[1].enabled = true;
  TEST_ASSERT_EQUAL((int)ConfigError::MissingIdentifier, (int)validate(c).error);
}
void malformed_enum_and_source_bounds() {
  SourceConfig c;
  c.capacity = 0;
  TEST_ASSERT_EQUAL((int)ConfigError::InvalidCapacity, (int)validate(c).error);
  c.capacity = kMaxCameras;
  c.count = 1;
  c.cameras[0] = camera();
  c.cameras[0].model = static_cast<CameraModel>(999);
  TEST_ASSERT_EQUAL((int)ConfigError::UnknownModel, (int)validate(c).error);
  c.cameras[0] = camera();
  c.cameras[0].name = std::string(65, 'x');
  TEST_ASSERT_EQUAL((int)ConfigError::InvalidName, (int)validate(c).error);
  c.cameras[0] = camera();
  c.cameras[0].identifier = "01:23:45:67:89:ZZ";
  TEST_ASSERT_EQUAL((int)ConfigError::InvalidIdentifier, (int)validate(c).error);
  c.cameras[0] = camera();
  c.cameras[0].enabled = false;
  c.cameras[0].identifier.clear();
  c.cameras[0].address_type = static_cast<AddressType>(999);
  TEST_ASSERT_EQUAL((int)ConfigError::InvalidAddressType, (int)validate(c).error);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(malformed_enum_and_source_bounds);
  RUN_TEST(counts_and_capacity);
  RUN_TEST(invalid_entries_are_actionable);
  RUN_TEST(duplicates_and_disabled_placeholders);
  return UNITY_END();
}
void setUp() {}
void tearDown() {}

#include "setup_service.h"
#include <cstdio>
#include <cstring>
#include <unity.h>
using namespace ridesync;
void setUp() {}
void tearDown() {}
static char response[2049];
static void send(SetupService &s, const char *p, uint32_t now = 1) {
  while (*p)
    s.feed(*p++, now);
  s.feed('\n', now);
}
static void enter(SetupService &s) {
  s.reset(0, 7, 123, 42);
  s.setIsolationAvailable(true);
  send(s, "@RS1 0 1 7 0 ENTER 123 PARKED");
  TEST_ASSERT_TRUE(s.takeResponse(response, sizeof response));
  TEST_ASSERT_NOT_NULL(strstr(response, "Pending IsolationRequired"));
  s.acknowledgeIsolation(true);
  TEST_ASSERT_TRUE(s.takeResponse(response, sizeof response));
  TEST_ASSERT_TRUE(s.active());
}
void challenge_barrier() {
  SetupService s;
  s.reset(0, 7, 123, 42);
  s.setIsolationAvailable(true);
  send(s, "@RS1 0 1 7 0 ENTER 124 PARKED");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_FALSE(s.active());
  TEST_ASSERT_NOT_NULL(strstr(response, "Challenge"));
  enter(s);
  TEST_ASSERT_EQUAL_UINT32(42, s.session());
}
void expiry_barrier() {
  SetupService s;
  enter(s);
  s.tick(120001);
  TEST_ASSERT_FALSE(s.active());
  TEST_ASSERT_TRUE(s.isolated());
  s.ownerReleased(true);
  TEST_ASSERT_FALSE(s.isolated());
}
void framing_and_replay() {
  SetupService s;
  enter(s);
  send(s, "@RS1 42 2 7 0 INSPECT");
  s.takeResponse(response, sizeof response);
  send(s, "@RS1 42 2 7 0 DRAFT_BEGIN");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Replay"));
  send(s, "@RS2 42 3 7 0 INSPECT");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Version"));
}
void challenge_lockout() {
  SetupService s;
  s.reset(0, 7, 123, 42);
  s.setIsolationAvailable(true);
  for (unsigned i = 1; i <= 3; ++i) {
    send(s, "@RS1 0 1 7 0 ENTER 0 PARKED");
    s.takeResponse(response, sizeof response);
  }
  send(s, "@RS1 0 2 7 0 ENTER 123 PARKED");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "EntryClosed"));
}
void absolute_and_rollover() {
  SetupService s;
  s.reset(UINT32_MAX - 1000, 7, 123, 42);
  s.setIsolationAvailable(true);
  send(s, "@RS1 0 1 7 0 ENTER 123 PARKED", UINT32_MAX - 900);
  s.takeResponse(response, sizeof response);
  s.acknowledgeIsolation(true);
  s.takeResponse(response, sizeof response);
  for (unsigned i = 1; i < 6; ++i) {
    char cmd[100];
    snprintf(cmd, sizeof cmd, "@RS1 42 %u 7 0 STATUS", i + 1);
    send(s, cmd, uint32_t(UINT32_MAX - 900 + i * 100000));
    s.takeResponse(response, sizeof response);
  }
  s.tick(uint32_t(UINT32_MAX - 900 + 600000));
  TEST_ASSERT_FALSE(s.active());
  TEST_ASSERT_TRUE(s.isolated());
}
void malformed_and_pressure() {
  SetupService s;
  enter(s);
  s.feed('@', 10);
  s.tick(2010);
  s.feed('\n', 2010);
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Malformed"));
  for (size_t i = 0; i < 2050; ++i)
    s.feed('x', 2011);
  s.feed('\n', 2011);
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Malformed"));
  send(s, "@RS1 42 2 7 0 STATUS");
  send(s, "@RS1 42 3 7 0 EXIT");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_TRUE(s.active());
  send(s, "@RS1 42 4 7 0 STATUS extra");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "UnknownField"));
}
void draft_atomic_validation() {
  SetupService s;
  enter(s);
  SettingsSnapshot snapshot;
  snapshot.epoch = 7;
  snapshot.completed = snapshot.effective = true;
  snapshot.settings.capacity = 4;
  s.setSnapshot(snapshot);
  send(s, "@RS1 42 2 7 0 DRAFT_BEGIN");
  s.takeResponse(response, sizeof response);
  send(s, "@RS1 42 3 7 0 DRAFT_SET 1,4;1,1,1,0,0,43616d,41413a42423a43433a44443a45453a4646,-");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Ok None"));
  send(s, "@RS1 42 4 7 0 VALIDATE");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Ok None"));
  send(s, "@RS1 42 5 7 0 DISPATCH");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Pending None"));
  SetupSaveDispatch save;
  TEST_ASSERT_TRUE(s.takeSave(save));
  TEST_ASSERT_FALSE(s.takeSave(save));
  TEST_ASSERT_EQUAL_UINT32(5, save.request);
  send(s, "@RS1 42 6 7 0 DISPATCH");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Busy"));
  snapshot.generation = 1;
  snapshot.settings = save.save.settings;
  s.setSnapshot(snapshot);
  s.completeSave(42, 5, SaveOutcome::Durable, PersistResult{}, 7, 1);
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "42 5 7 1 DISPATCH Ok None"));
  send(s, "@RS1 42 7 7 1 EXIT");
  s.takeResponse(response, sizeof response);
  s.ownerReleased(false);
  TEST_ASSERT_TRUE(s.isolated());
  s.ownerReleased(true);
  TEST_ASSERT_FALSE(s.isolated());
  TEST_ASSERT_TRUE(s.takeResponse(response, sizeof response));
  TEST_ASSERT_NOT_NULL(strstr(response, "EXIT Ok None"));
}
void wrong_session_and_reset() {
  SetupService s;
  enter(s);
  send(s, "@RS1 41 2 7 0 DRAFT_BEGIN");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Session"));
  s.reset(0, 8, 456, 43);
  send(s, "@RS1 42 3 7 0 DRAFT_BEGIN");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Session"));
}
void isolation_ack_waits_for_output() {
  SetupService s;
  s.reset(0, 7, 123, 42);
  s.setIsolationAvailable(true);
  send(s, "@RS1 0 1 7 0 ENTER 123 PARKED");
  s.acknowledgeIsolation(true);
  TEST_ASSERT_TRUE(s.takeResponse(response, sizeof response));
  TEST_ASSERT_NOT_NULL(strstr(response, "Pending IsolationRequired"));
  s.tick(2);
  TEST_ASSERT_TRUE(s.takeResponse(response, sizeof response));
  TEST_ASSERT_NOT_NULL(strstr(response, "Ok None"));
}
void completion_correlation_and_expiry() {
  SetupService s;
  enter(s);
  SettingsSnapshot snapshot;
  snapshot.epoch = 7;
  snapshot.completed = snapshot.effective = true;
  snapshot.settings.capacity = 4;
  s.setSnapshot(snapshot);
  send(s, "@RS1 42 2 7 0 DRAFT_BEGIN");
  s.takeResponse(response, sizeof response);
  send(s, "@RS1 42 3 7 0 DISPATCH");
  s.takeResponse(response, sizeof response);
  SetupSaveDispatch save;
  TEST_ASSERT_TRUE(s.takeSave(save));
  s.completeSave(41, 3, SaveOutcome::Durable, PersistResult{}, 7, 1);
  TEST_ASSERT_FALSE(s.takeResponse(response, sizeof response));
  s.completeSave(42, 3, SaveOutcome::Durable, PersistResult{}, 8, 1);
  TEST_ASSERT_FALSE(s.takeResponse(response, sizeof response));
  s.tick(120001);
  s.ownerReleased(true);
  TEST_ASSERT_TRUE(s.isolated());
  s.completeSave(42, 3, SaveOutcome::Durable, PersistResult{}, 7, 1);
  s.ownerReleased(true);
  TEST_ASSERT_FALSE(s.isolated());
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Error Expired"));
}
void invalid_draft_preserves_candidate() {
  Settings base;
  base.capacity = 4;
  Settings candidate;
  TEST_ASSERT_TRUE(decodeSetupCameras("0,4", base, candidate));
  TEST_ASSERT_FALSE(decodeSetupCameras("1,4;1,1,1,0,0,00,-,-", base, candidate));
  TEST_ASSERT_EQUAL_UINT32(0, candidate.count);
  TEST_ASSERT_FALSE(decodeSetupCameras("1,4;1,1,1,0,0,43616d,-,-,credential", base, candidate));
  TEST_ASSERT_FALSE(decodeSetupCameras("5,5", base, candidate));
  TEST_ASSERT_FALSE(decodeSetupCameras("0,4;extra", base, candidate));
}
static void prepareSave(SetupService &s) {
  enter(s);
  SettingsSnapshot snapshot;
  snapshot.epoch = 7;
  snapshot.completed = snapshot.effective = true;
  snapshot.settings.capacity = 4;
  s.setSnapshot(snapshot);
  send(s, "@RS1 42 2 7 0 DRAFT_BEGIN");
  s.takeResponse(response, sizeof response);
  send(s, "@RS1 42 3 7 0 DISPATCH");
  s.takeResponse(response, sizeof response);
}
void revoked_unsent_save_never_dispatches() {
  SetupService s;
  prepareSave(s);
  s.tick(120001);
  SetupSaveDispatch save;
  TEST_ASSERT_FALSE(s.takeSave(save));
  SetupService detached;
  prepareSave(detached);
  detached.disconnected();
  TEST_ASSERT_FALSE(detached.takeSave(save));
}
void readonly_recovery_retains_owner_result() {
  SetupService s;
  prepareSave(s);
  SetupSaveDispatch save;
  TEST_ASSERT_TRUE(s.takeSave(save));
  send(s, "@RS1 0 4 7 0 HELLO RECOVERY", 31000);
  TEST_ASSERT_TRUE(s.takeResponse(response, sizeof response));
  TEST_ASSERT_NOT_NULL(strstr(response, "challenge=123"));
  send(s, "@RS1 0 5 7 0 RECOVER 123 PARKED", 31001);
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Pending OwnerRelease"));
  s.ownerReleased(true);
  TEST_ASSERT_FALSE(s.active());
  s.completeSave(42, 3, SaveOutcome::Durable, PersistResult{PersistStatus::Durable}, 7, 1);
  s.ownerReleased(true);
  TEST_ASSERT_TRUE(s.active());
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "43 5 7 0 RECOVER Ok None"));
  send(s, "@RS1 43 6 7 0 STATUS", 31002);
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "status=Durable,session=42,request=3"));
  send(s, "@RS1 43 7 7 0 DRAFT_BEGIN", 31003);
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "ReadOnly"));
  send(s, "@RS1 42 8 7 0 DISPATCH", 31004);
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Session"));
}
void hostile_unicode_and_legacy_name() {
  Settings base;
  base.capacity = 4;
  Settings out;
  const char *bad[] = {"ff", "c080", "c285", "e280ae", "eda080", "f4908080"};
  for (auto name : bad) {
    char wire[160];
    snprintf(wire, sizeof wire, "1,4;1,1,1,0,0,%s,41413a42423a43433a44443a45453a4646,-", name);
    TEST_ASSERT_FALSE(decodeSetupCameras(wire, base, out));
  }
  TEST_ASSERT_TRUE(decodeSetupCameras(
      "1,4;1,1,1,0,0,436166c3a9,41413a42423a43433a44443a45453a4646,-", base, out));
  SetupService s;
  enter(s);
  SettingsSnapshot snapshot;
  snapshot.epoch = 7;
  snapshot.completed = snapshot.effective = true;
  snapshot.settings = out;
  snapshot.settings.cameras[0].name[0] = '\x01';
  s.setSnapshot(snapshot);
  send(s, "@RS1 42 2 7 0 DRAFT_BEGIN");
  s.takeResponse(response, sizeof response);
  send(s, "@RS1 42 3 7 0 VALIDATE");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "Invalid"));
}
void result_never_relabels_old_snapshot() {
  SetupService s;
  prepareSave(s);
  SetupSaveDispatch save;
  TEST_ASSERT_TRUE(s.takeSave(save));
  s.completeSave(42, 3, SaveOutcome::Durable, PersistResult{PersistStatus::Durable}, 7, 9);
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "42 3 7 0 DISPATCH Ok None"));
  send(s, "@RS1 42 4 7 0 INSPECT");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "42 4 7 0 INSPECT Ok None 0,4"));
  send(s, "@RS1 42 5 7 0 STATUS");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "generation=9"));
  SettingsSnapshot adopted;
  adopted.epoch = 7;
  adopted.generation = 9;
  adopted.completed = adopted.effective = true;
  adopted.settings.capacity = 3;
  s.setSnapshot(adopted);
  send(s, "@RS1 42 6 7 0 INSPECT");
  s.takeResponse(response, sizeof response);
  TEST_ASSERT_NOT_NULL(strstr(response, "42 6 7 9 INSPECT Ok None 0,3"));
}
int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(result_never_relabels_old_snapshot);
  RUN_TEST(hostile_unicode_and_legacy_name);
  RUN_TEST(revoked_unsent_save_never_dispatches);
  RUN_TEST(readonly_recovery_retains_owner_result);
  RUN_TEST(completion_correlation_and_expiry);
  RUN_TEST(invalid_draft_preserves_candidate);
  RUN_TEST(isolation_ack_waits_for_output);
  RUN_TEST(challenge_barrier);
  RUN_TEST(expiry_barrier);
  RUN_TEST(framing_and_replay);
  RUN_TEST(challenge_lockout);
  RUN_TEST(absolute_and_rollover);
  RUN_TEST(malformed_and_pressure);
  RUN_TEST(draft_atomic_validation);
  RUN_TEST(wrong_session_and_reset);
  return UNITY_END();
}

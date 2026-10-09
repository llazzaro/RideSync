#include "wake_manager.h"
#include <unity.h>
using namespace ridesync;
struct Radio : WakeRadio {
  unsigned calls = 0, accepted = 0, cancels = 0;
  std::array<WakeOperation, 64> requests{};
  int busy_peer = -1;
  WakeSubmit result = WakeSubmit::Accepted;
  WakeRadioResult publication;
  uint32_t deadline = 0;
  WakeSubmit begin(const WakeOperation &op, const insta360::WakeEncoding &, uint32_t d,
                   uint32_t) override {
    requests[calls++] = op;
    if (int(op.peer) == busy_peer)
      return WakeSubmit::Busy;
    if (result != WakeSubmit::Accepted)
      return result;
    ++accepted;
    deadline = d;
    publication = {};
    publication.operation = op;
    return result;
  }
  void cancel(const WakeOperation &) override { ++cancels; }
  WakeRadioResult poll(const WakeOperation &, uint32_t) override { return publication; }
  void finish() {
    publication.submitted = true;
    publication.terminal = true;
    publication.released = true;
  }
};
struct Recovery : WakeRecovery {
  unsigned begins = 0, cancels = 0;
  CameraError result = CameraError::None;
  std::array<WakeRecoveryResult, 4> publications{};
  CameraError begin(const WakeOperation &op, uint32_t) override {
    ++begins;
    publications[op.peer] = {};
    publications[op.peer].operation = op;
    return result;
  }
  WakeRecoveryResult poll(const WakeOperation &op, uint32_t) override {
    return publications[op.peer];
  }
  void cancel(const WakeOperation &) override { ++cancels; }
  void finish(unsigned peer, RecordingState state = RecordingState::Stopped, bool fresh = true) {
    auto &p = publications[peer];
    p.terminal = true;
    p.released = true;
    p.fresh = fresh;
    p.observed = state;
  }
};
WakePeerConfig config() {
  WakePeerConfig c;
  c.enabled = c.source_qualified = true;
  c.profile = insta360::WakeProfile::M5WakeV1;
  c.identifier = {{'A', '1', 'B', '2', 'C', '3'}};
  return c;
}
WakeOperation op(unsigned peer, uint32_t id = 1, uint32_t generation = 1) {
  WakeOperation result;
  result.peer = peer;
  result.id = id;
  result.generation = generation;
  return result;
}
WakePolicy policy() {
  WakePolicy p;
  p.total_ms = 100;
  p.slice_ms = 30;
  return p;
}
void admit(WakeManager &m, unsigned peer = 0, uint32_t now = 0) {
  TEST_ASSERT_EQUAL(int(CameraError::None), int(m.request(op(peer), config(), policy(), now)));
}
void four_peers_progress_independently_with_single_radio() {
  Radio r;
  Recovery recovery;
  WakeManager m(r, &recovery);
  for (unsigned i = 0; i < 4; ++i)
    admit(m, i);
  m.service(0);
  TEST_ASSERT_EQUAL(1, r.accepted);
  for (unsigned i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL(i, r.publication.operation.peer);
    r.finish();
    m.service(2 * i + 1);
    recovery.finish(i);
    m.service(2 * i + 2);
    TEST_ASSERT_EQUAL(int(WakePhase::Ready), int(m.status(i).phase));
    TEST_ASSERT_TRUE(m.status(i).released);
  }
  TEST_ASSERT_EQUAL(4, r.accepted);
  TEST_ASSERT_EQUAL(4, recovery.begins);
}
void cancellation_keeps_unreleased_radio_and_ignores_late_success() {
  Radio r;
  Recovery recovery;
  WakeManager m(r, &recovery);
  admit(m);
  admit(m, 1);
  m.service(0);
  m.cancel(0);
  m.cancel(0);
  r.publication.terminal = true;
  m.service(20);
  TEST_ASSERT_EQUAL(1, r.cancels);
  TEST_ASSERT_EQUAL(1, r.accepted);
  TEST_ASSERT_EQUAL(0, recovery.begins);
  TEST_ASSERT_FALSE(m.status(0).released);
  TEST_ASSERT_EQUAL(int(CameraError::Busy), int(m.request(op(0, 2), config(), policy(), 20)));
  r.finish();
  m.service(21);
  TEST_ASSERT_EQUAL(2, r.accepted);
  TEST_ASSERT_EQUAL(int(WakePhase::Cancelled), int(m.status(0).phase));
  TEST_ASSERT_TRUE(m.status(0).released);
}
void queued_and_stalled_requests_expire_without_replay() {
  Radio r;
  Recovery recovery;
  WakeManager m(r, &recovery);
  admit(m);
  admit(m, 1);
  m.service(0);
  m.service(100);
  m.service(101);
  TEST_ASSERT_EQUAL(int(WakePhase::Timeout), int(m.status(0).phase));
  TEST_ASSERT_EQUAL(int(WakePhase::Timeout), int(m.status(1).phase));
  TEST_ASSERT_FALSE(m.status(0).released);
  TEST_ASSERT_TRUE(m.status(1).released);
  TEST_ASSERT_EQUAL(1, r.accepted);
  TEST_ASSERT_EQUAL(1, r.cancels);
  r.finish();
  m.service(102);
  TEST_ASSERT_EQUAL(0, recovery.begins);
  TEST_ASSERT_EQUAL(int(WakePhase::Timeout), int(m.status(0).phase));
}
void busy_peer_does_not_starve_another_peer() {
  Radio r;
  Recovery recovery;
  r.busy_peer = 0;
  WakeManager m(r, &recovery);
  admit(m);
  admit(m, 1);
  m.service(0);
  TEST_ASSERT_EQUAL(0, r.accepted);
  m.service(1);
  TEST_ASSERT_EQUAL(1, r.accepted);
  TEST_ASSERT_EQUAL(1, r.publication.operation.peer);
  r.finish();
  m.service(2);
  recovery.finish(1);
  m.service(3);
  TEST_ASSERT_EQUAL(int(WakePhase::Ready), int(m.status(1).phase));
  m.service(100);
  TEST_ASSERT_EQUAL(int(WakePhase::Timeout), int(m.status(0).phase));
}
void missing_provider_or_unsupported_recovery_is_honest() {
  Radio r;
  WakeManager absent(r);
  admit(absent);
  absent.service(0);
  r.finish();
  absent.service(1);
  TEST_ASSERT_EQUAL(int(WakePhase::Unsupported), int(absent.status(0).phase));
  Radio r2;
  Recovery recovery;
  recovery.result = CameraError::Unsupported;
  WakeManager m(r2, &recovery);
  admit(m);
  m.service(0);
  r2.finish();
  m.service(1);
  TEST_ASSERT_EQUAL(int(WakePhase::Unsupported), int(m.status(0).phase));
  TEST_ASSERT_TRUE(m.status(0).released);
}
void wrong_radio_identity_and_stale_observations_cannot_advance() {
  Radio r;
  Recovery recovery;
  WakeManager m(r, &recovery);
  admit(m);
  m.service(0);
  r.finish();
  ++r.publication.operation.id;
  m.service(1);
  TEST_ASSERT_EQUAL(0, recovery.begins);
  r.publication.operation = op(0);
  m.service(2);
  TEST_ASSERT_EQUAL(1, recovery.begins);
  recovery.finish(0);
  ++recovery.publications[0].operation.generation;
  m.service(3);
  TEST_ASSERT_EQUAL(int(WakePhase::Recovering), int(m.status(0).phase));
  recovery.publications[0].operation = op(0);
  recovery.publications[0].fresh = false;
  m.service(4);
  TEST_ASSERT_NOT_EQUAL(int(WakePhase::Ready), int(m.status(0).phase));
}
void unknown_observation_or_unreleased_recovery_is_not_ready() {
  for (auto state : {RecordingState::Unknown, static_cast<RecordingState>(255)}) {
    Radio r;
    Recovery rec;
    WakeManager m(r, &rec);
    admit(m);
    m.service(0);
    r.finish();
    m.service(1);
    rec.finish(0, state);
    m.service(2);
    TEST_ASSERT_NOT_EQUAL(int(WakePhase::Ready), int(m.status(0).phase));
  }
  Radio r;
  Recovery rec;
  WakeManager m(r, &rec);
  admit(m);
  m.service(0);
  r.finish();
  m.service(1);
  rec.finish(0, RecordingState::Recording);
  rec.publications[0].released = false;
  m.service(2);
  TEST_ASSERT_NOT_EQUAL(int(WakePhase::Ready), int(m.status(0).phase));
  rec.publications[0].released = true;
  m.service(3);
  TEST_ASSERT_EQUAL(int(WakePhase::Ready), int(m.status(0).phase));
  TEST_ASSERT_EQUAL(int(RecordingState::Recording), int(m.status(0).observed));
}
void reset_and_reused_operations_never_revive_intent() {
  Radio r;
  Recovery rec;
  WakeManager m(r, &rec);
  admit(m);
  admit(m, 1);
  m.service(0);
  m.invalidate(2);
  r.finish();
  m.service(1);
  TEST_ASSERT_EQUAL(0, rec.begins);
  TEST_ASSERT_EQUAL(int(WakePhase::Cancelled), int(m.status(1).phase));
  TEST_ASSERT_NOT_EQUAL(int(CameraError::None), int(m.request(op(0, 2, 1), config(), policy(), 2)));
  TEST_ASSERT_NOT_EQUAL(int(CameraError::None), int(m.request(op(0, 1, 2), config(), policy(), 2)));
  TEST_ASSERT_EQUAL(int(CameraError::None), int(m.request(op(0, 2, 2), config(), policy(), 2)));
  m.service(2);
  TEST_ASSERT_EQUAL(2, r.accepted);
}
void invalid_configuration_and_identity_never_calls_driver() {
  Radio r;
  WakeManager m(r);
  auto c = config();
  auto p = policy();
  for (uint32_t invalid : {0u, UINT32_MAX}) {
    TEST_ASSERT_NOT_EQUAL(int(CameraError::None), int(m.request(op(0, invalid), c, p, 0)));
    TEST_ASSERT_NOT_EQUAL(int(CameraError::None), int(m.request(op(0, 1, invalid), c, p, 0)));
  }
  TEST_ASSERT_EQUAL(int(CameraError::InvalidPeer), int(m.request(op(4), c, p, 0)));
  c.enabled = false;
  TEST_ASSERT_EQUAL(int(CameraError::Disabled), int(m.request(op(0), c, p, 0)));
  c = config();
  c.source_qualified = false;
  TEST_ASSERT_EQUAL(int(CameraError::Unsupported), int(m.request(op(0), c, p, 0)));
  c = config();
  c.identifier[0] = 0;
  TEST_ASSERT_NOT_EQUAL(int(CameraError::None), int(m.request(op(0), c, p, 0)));
  c = config();
  for (uint32_t bad : {0u, 0x80000000u, UINT32_MAX}) {
    p = policy();
    p.total_ms = bad;
    TEST_ASSERT_NOT_EQUAL(int(CameraError::None), int(m.request(op(0), c, p, 0)));
    p = policy();
    p.slice_ms = bad;
    TEST_ASSERT_NOT_EQUAL(int(CameraError::None), int(m.request(op(0), c, p, 0)));
  }
  p = policy();
  p.slice_ms = 101;
  TEST_ASSERT_NOT_EQUAL(int(CameraError::None), int(m.request(op(0), c, p, 0)));
  m.service(0);
  TEST_ASSERT_EQUAL(0, r.calls);
}
void rollover_and_queue_time_preserve_original_deadline() {
  Radio r;
  Recovery rec;
  WakeManager m(r, &rec);
  const uint32_t start = UINT32_MAX - 40;
  admit(m, 0, start);
  admit(m, 1, start);
  m.service(start);
  TEST_ASSERT_EQUAL(uint32_t(start + 30), r.deadline);
  r.finish();
  m.service(start + 80);
  TEST_ASSERT_EQUAL(uint32_t(start + 100), r.deadline);
  m.service(start + 100);
  TEST_ASSERT_EQUAL(int(WakePhase::Timeout), int(m.status(1).phase));
  TEST_ASSERT_EQUAL(int(WakePhase::Timeout), int(m.status(0).phase));
}
void recovery_cancel_is_once_and_terminal_while_release_pending() {
  Radio r;
  Recovery rec;
  WakeManager m(r, &rec);
  admit(m);
  m.service(0);
  r.finish();
  m.service(1);
  m.cancel(0);
  m.cancel(0);
  rec.finish(0);
  rec.publications[0].released = false;
  m.service(2);
  TEST_ASSERT_EQUAL(1, rec.cancels);
  TEST_ASSERT_FALSE(m.status(0).released);
  rec.publications[0].released = true;
  m.service(3);
  TEST_ASSERT_TRUE(m.status(0).released);
  TEST_ASSERT_EQUAL(int(WakePhase::Cancelled), int(m.status(0).phase));
}
void failed_submission_does_not_recover_or_stop_other_peer() {
  Radio r;
  Recovery rec;
  WakeManager m(r, &rec);
  r.result = WakeSubmit::Failed;
  admit(m);
  admit(m, 1);
  m.service(0);
  TEST_ASSERT_EQUAL(int(WakePhase::Failed), int(m.status(0).phase));
  r.result = WakeSubmit::Accepted;
  m.service(1);
  TEST_ASSERT_EQUAL(1, r.publication.operation.peer);
  r.finish();
  r.publication.sdk_error = 42;
  m.service(2);
  TEST_ASSERT_EQUAL(int(WakePhase::Failed), int(m.status(1).phase));
  TEST_ASSERT_EQUAL(0, rec.begins);
}
void active_duplicate_and_invalid_generation_are_rejected() {
  Radio r;
  Recovery rec;
  WakeManager m(r, &rec);
  admit(m);
  TEST_ASSERT_EQUAL(int(CameraError::Busy), int(m.request(op(0), config(), policy(), 0)));
  m.service(0);
  m.invalidate(1);
  r.finish();
  m.service(1);
  TEST_ASSERT_NOT_EQUAL(int(CameraError::None), int(m.request(op(0, 2, 2), config(), policy(), 2)));
  TEST_ASSERT_EQUAL(0, rec.begins);
}
void wrong_peer_and_unsubmitted_completion_are_not_success() {
  Radio r;
  Recovery rec;
  WakeManager m(r, &rec);
  admit(m);
  m.service(0);
  r.finish();
  r.publication.operation.peer = 1;
  m.service(1);
  TEST_ASSERT_EQUAL(0, rec.begins);
  r.publication.operation = op(0);
  r.publication.submitted = false;
  m.service(2);
  TEST_ASSERT_EQUAL(int(WakePhase::Failed), int(m.status(0).phase));
  TEST_ASSERT_TRUE(m.status(0).released);
  TEST_ASSERT_EQUAL(0, rec.begins);
}
void recovery_error_does_not_release_borrowed_procedure() {
  Radio r;
  Recovery rec;
  WakeManager m(r, &rec);
  admit(m);
  m.service(0);
  r.finish();
  m.service(1);
  rec.finish(0);
  rec.publications[0].error = CameraError::Transport;
  rec.publications[0].released = false;
  m.service(2);
  TEST_ASSERT_EQUAL(int(WakePhase::Failed), int(m.status(0).phase));
  TEST_ASSERT_FALSE(m.status(0).released);
  TEST_ASSERT_EQUAL(1, rec.cancels);
  rec.publications[0].released = true;
  m.service(3);
  TEST_ASSERT_TRUE(m.status(0).released);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(four_peers_progress_independently_with_single_radio);
  RUN_TEST(cancellation_keeps_unreleased_radio_and_ignores_late_success);
  RUN_TEST(queued_and_stalled_requests_expire_without_replay);
  RUN_TEST(busy_peer_does_not_starve_another_peer);
  RUN_TEST(missing_provider_or_unsupported_recovery_is_honest);
  RUN_TEST(wrong_radio_identity_and_stale_observations_cannot_advance);
  RUN_TEST(unknown_observation_or_unreleased_recovery_is_not_ready);
  RUN_TEST(reset_and_reused_operations_never_revive_intent);
  RUN_TEST(invalid_configuration_and_identity_never_calls_driver);
  RUN_TEST(rollover_and_queue_time_preserve_original_deadline);
  RUN_TEST(recovery_cancel_is_once_and_terminal_while_release_pending);
  RUN_TEST(failed_submission_does_not_recover_or_stop_other_peer);
  RUN_TEST(active_duplicate_and_invalid_generation_are_rejected);
  RUN_TEST(wrong_peer_and_unsubmitted_completion_are_not_success);
  RUN_TEST(recovery_error_does_not_release_borrowed_procedure);
  return UNITY_END();
}

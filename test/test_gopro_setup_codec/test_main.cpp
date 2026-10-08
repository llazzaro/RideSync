#include "../fixtures/gopro/setup_vectors.h"
#include "protocol/gopro_setup_codec.h"
#include <initializer_list>
#include <string.h>
#include <unity.h>
using namespace ridesync::gopro;
void setUp() {}
void tearDown() {}
static Message body(const uint8_t *bytes, size_t size) {
  Message m;
  m.size = size;
  if (size <= sizeof(m.bytes))
    memcpy(m.bytes, bytes, size);
  return m;
}
void requests_use_required_values_and_separate_packet() {
  SetupPacket p;
  TEST_ASSERT_TRUE(encodeSetup(SetupOperation::PairingFinish, p, false));
  TEST_ASSERT_EQUAL_INT((int)Channel::Management, (int)p.channel);
  TEST_ASSERT_EQUAL_UINT(sizeof(setup_fixture::pair_request), p.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(setup_fixture::pair_request, p.bytes, p.size);
  TEST_ASSERT_TRUE(encodeSetup(SetupOperation::PairingFinish, p));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(setup_fixture::pair_request_extended, p.bytes, p.size);
  TEST_ASSERT_TRUE(encodeSetup(SetupOperation::ClaimExternalControl, p, false));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(setup_fixture::claim_request, p.bytes, p.size);
  TEST_ASSERT_TRUE(encodeSetup(SetupOperation::ClaimExternalControl, p));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(setup_fixture::claim_request_extended, p.bytes, p.size);
  TEST_ASSERT_FALSE(encodeSetup(static_cast<SetupOperation>(99), p));
}
void generic_replies_require_route_ids_result_and_full_validity() {
  Reassembler a;
  Message m;
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete,
                        (int)a.feed(1, Channel::Management, setup_fixture::pair_success,
                                    sizeof(setup_fixture::pair_success), 0, m));
  auto r = decodeSetup(Channel::Management, m, SetupOperation::PairingFinish);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)r.outcome);
  TEST_ASSERT_TRUE(r.result_present && r.success);
  TEST_ASSERT_EQUAL_UINT64(1, r.result);
  TEST_ASSERT_EQUAL_INT((int)SetupResultDomain::GenericProtobuf, (int)r.domain);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(m.bytes, r.raw.bytes, m.size);
  TEST_ASSERT_EQUAL_INT(
      (int)Outcome::Unknown,
      (int)decodeSetup(Channel::Command, m, SetupOperation::PairingFinish).outcome);
  TEST_ASSERT_EQUAL_INT(
      (int)Outcome::Unknown,
      (int)decodeSetup(Channel::Management, m, SetupOperation::ClaimExternalControl).outcome);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete,
                        (int)a.feed(1, Channel::Command, setup_fixture::claim_success_extended,
                                    sizeof(setup_fixture::claim_success_extended), 1, m));
  r = decodeSetup(Channel::Command, m, SetupOperation::ClaimExternalControl);
  TEST_ASSERT_TRUE(r.success);
  const uint8_t missing[] = {0xf1, 0xe9, 0x10, 0};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid,
                        (int)decodeSetup(Channel::Command, body(missing, sizeof(missing)),
                                         SetupOperation::ClaimExternalControl)
                            .outcome);
  const uint8_t classic[] = {0xf1, 0xe9, 0};
  TEST_ASSERT_FALSE(decodeSetup(Channel::Command, body(classic, sizeof(classic)),
                                SetupOperation::ClaimExternalControl)
                        .success);
  const uint8_t wrong[] = {0xf1, 0x69, 8, 1};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Unknown,
                        (int)decodeSetup(Channel::Command, body(wrong, sizeof(wrong)),
                                         SetupOperation::ClaimExternalControl)
                            .outcome);
}
void result_domain_rejects_unknown_and_preserves_numeric_rejections() {
  for (uint8_t value = 0; value <= 7; ++value) {
    const uint8_t b[] = {0x03, 0x81, 8, value};
    auto r = decodeSetup(Channel::Management, body(b, sizeof(b)), SetupOperation::PairingFinish);
    TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)r.outcome);
    TEST_ASSERT_TRUE(r.result_present);
    TEST_ASSERT_EQUAL_UINT64(value, r.result);
    TEST_ASSERT_EQUAL_INT(value == 1, r.success);
    TEST_ASSERT_EQUAL_INT(value <= 6, r.known_result);
  }
  const uint8_t duplicate_unknown[] = {3, 0x81, 8, 7, 8, 1};
  auto r = decodeSetup(Channel::Management, body(duplicate_unknown, sizeof(duplicate_unknown)),
                       SetupOperation::PairingFinish);
  TEST_ASSERT_FALSE(r.success);
  TEST_ASSERT_FALSE(r.known_result);
  TEST_ASSERT_EQUAL_UINT64(1, r.result);
  const uint8_t max_uint64[] = {3,    0x81, 8,    0xff, 0xff, 0xff, 0xff,
                                0xff, 0xff, 0xff, 0xff, 0xff, 0x01};
  r = decodeSetup(Channel::Management, body(max_uint64, sizeof(max_uint64)),
                  SetupOperation::PairingFinish);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)r.outcome);
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, r.result);
  TEST_ASSERT_FALSE(r.success);
}
void protobuf_bounds_reject_malformed_without_partial_publication() {
  const uint8_t valid[] = {3, 0x81, 8, 1, 0x10, 0xac, 0x02, 0x1a, 2, 7, 8, 0x25, 1, 2, 3, 4};
  auto good =
      decodeSetup(Channel::Management, body(valid, sizeof(valid)), SetupOperation::PairingFinish);
  TEST_ASSERT_TRUE(good.success && good.unknown_fields);
  for (size_t n = 0; n < sizeof(valid); ++n) {
    auto r = decodeSetup(Channel::Management, body(valid, n), SetupOperation::PairingFinish);
    // At a complete field boundary a shorter message is valid protobuf.
    if (n != 4 && n != 7 && n != 11) {
      TEST_ASSERT_FALSE(r.success);
      TEST_ASSERT_FALSE(r.result_present);
    }
  }
  const uint8_t malformed[][14] = {
      {3, 0x81, 0, 8, 1},
      {3, 0x81, 0x0e, 1},
      {3, 0x81, 0x0b, 1},
      {3, 0x81, 0x09, 1, 2, 3, 4, 5, 6, 7, 8, 9},
      {3, 0x81, 8, 1, 0x12, 9},
      {3, 0x81, 8, 1, 0x80},
      {3, 0x81, 8, 1, 0x0c},
      {3, 0x81, 0x0a, 1, 1},
      {3, 0x81, 8, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x02}};
  const size_t lengths[] = {5, 4, 4, 12, 6, 5, 5, 5, 14};
  for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
    auto r = decodeSetup(Channel::Management, body(malformed[i], lengths[i]),
                         SetupOperation::PairingFinish);
    TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid, (int)r.outcome);
    TEST_ASSERT_FALSE(r.result_present);
  }
  Message oversized;
  oversized.size = 257;
  TEST_ASSERT_EQUAL_INT(
      (int)Outcome::Oversize,
      (int)decodeSetup(Channel::Management, oversized, SetupOperation::PairingFinish).outcome);
}
void identity_values_are_owned_and_strict() {
  const uint8_t hw[] = {0x3c, 0, 1, 0x3e, 2, 'H', 'X', 1, 0, 2, 'f', 'w', 1, 's', 1, 'a', 1, 'm'};
  Message m = body(hw, sizeof(hw));
  auto id = decodeIdentity(Channel::Command, m);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)id.outcome);
  TEST_ASSERT_TRUE(id.model_known);
  TEST_ASSERT_EQUAL_UINT64(62, id.model);
  TEST_ASSERT_EQUAL_UINT(2, id.fields[3].size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY("fw", id.raw.bytes + id.fields[3].offset, 2);
  m.bytes[11] = 'x';
  TEST_ASSERT_EQUAL_UINT8('w', id.raw.bytes[id.fields[3].offset + 1]);
  const uint8_t api[] = {0x51, 0, 2, 1, 2, 1, 3};
  id = decodeIdentity(Channel::Command, body(api, sizeof(api)));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)id.outcome);
  TEST_ASSERT_TRUE(id.api_known);
  TEST_ASSERT_EQUAL_UINT64(258, id.api_major);
  TEST_ASSERT_EQUAL_UINT64(3, id.api_minor);
  const uint8_t over[] = {0x51, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1};
  id = decodeIdentity(Channel::Command, body(over, sizeof(over)));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid, (int)id.outcome);
  TEST_ASSERT_FALSE(id.api_known);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Unknown,
                        (int)decodeIdentity(Channel::Query, body(api, sizeof(api))).outcome);
}
void identity_truncation_and_reserved_tail_never_publish_values() {
  const uint8_t hw[] = {0x3c, 0, 1, 0x3e, 2, 'H', 'X', 1, 0, 2, 'f', 'w', 1, 's', 1, 'a', 1, 'm'};
  for (size_t n = 0; n < sizeof(hw); ++n) {
    auto r = decodeIdentity(Channel::Command, body(hw, n));
    TEST_ASSERT_FALSE(r.model_known);
    TEST_ASSERT_EQUAL_UINT(0, r.field_count);
  }
  uint8_t reserved[sizeof(hw) + 11];
  memcpy(reserved, hw, sizeof(hw));
  memset(reserved + sizeof(hw), 0, 11);
  auto r = decodeIdentity(Channel::Command, body(reserved, sizeof(reserved)));
  TEST_ASSERT_TRUE(r.model_known);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)r.outcome);
  for (size_t n = sizeof(hw) + 1; n < sizeof(reserved); ++n) {
    r = decodeIdentity(Channel::Command, body(reserved, n));
    TEST_ASSERT_FALSE(r.model_known);
  }
  const uint8_t api[] = {0x51, 0, 2, 1, 2, 1, 3};
  for (size_t n = 0; n < sizeof(api); ++n)
    TEST_ASSERT_FALSE(decodeIdentity(Channel::Command, body(api, n)).api_known);
  const uint8_t width[] = {0x51, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3};
  r = decodeIdentity(Channel::Command, body(width, sizeof(width)));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid, (int)r.outcome);
  const uint8_t reject[] = {0x51, 2};
  r = decodeIdentity(Channel::Command, body(reject, sizeof(reject)));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)r.outcome);
  TEST_ASSERT_FALSE(r.api_known);
}
void management_reassembly_remains_bounded_and_peer_isolated() {
  Reassembler a;
  Message m;
  const uint8_t start[] = {0x20, 4, 3};
  const uint8_t end[] = {0x80, 0x81, 8, 1};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Pending,
                        (int)a.feed(1, Channel::Management, start, sizeof(start), 0, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::MissingStart,
                        (int)a.feed(2, Channel::Management, end, sizeof(end), 1, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete,
                        (int)a.feed(1, Channel::Management, end, sizeof(end), 1, m));
  TEST_ASSERT_TRUE(decodeSetup(Channel::Management, m, SetupOperation::PairingFinish).success);
  for (uint32_t peer = 1; peer <= 4; ++peer)
    TEST_ASSERT_EQUAL_INT((int)Outcome::Pending,
                          (int)a.feed(peer, Channel::Management, start, sizeof(start), 2, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Capacity,
                        (int)a.feed(5, Channel::Management, start, sizeof(start), 2, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Pending,
                        (int)a.feed(5, Channel::Management, start, sizeof(start), 1002, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Timeout,
                        (int)a.feed(5, Channel::Management, end, sizeof(end), 2002, m));
}
void bounded_random_inputs_never_publish_success_without_exact_route_and_result() {
  uint32_t state = 0x368f7811;
  for (size_t iteration = 0; iteration < 4096; ++iteration) {
    Message m;
    m.size = state % 257;
    for (size_t i = 0; i < m.size; ++i) {
      state = state * 1664525u + 1013904223u;
      m.bytes[i] = static_cast<uint8_t>(state >> 24);
    }
    for (Channel channel : {Channel::Command, Channel::Management, Channel::Query}) {
      for (SetupOperation op :
           {SetupOperation::PairingFinish, SetupOperation::ClaimExternalControl}) {
        auto r = decodeSetup(channel, m, op);
        TEST_ASSERT_TRUE(r.raw.size == m.size);
        if (r.success) {
          TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)r.outcome);
          TEST_ASSERT_TRUE(r.result_present && r.known_result);
          TEST_ASSERT_EQUAL_UINT64(1, r.result);
          TEST_ASSERT_EQUAL_INT((int)SetupResultDomain::GenericProtobuf, (int)r.domain);
          TEST_ASSERT_EQUAL_INT(
              (int)(op == SetupOperation::PairingFinish ? Channel::Management : Channel::Command),
              (int)channel);
        }
      }
    }
  }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(requests_use_required_values_and_separate_packet);
  RUN_TEST(generic_replies_require_route_ids_result_and_full_validity);
  RUN_TEST(result_domain_rejects_unknown_and_preserves_numeric_rejections);
  RUN_TEST(protobuf_bounds_reject_malformed_without_partial_publication);
  RUN_TEST(identity_values_are_owned_and_strict);
  RUN_TEST(identity_truncation_and_reserved_tail_never_publish_values);
  RUN_TEST(management_reassembly_remains_bounded_and_peer_isolated);
  RUN_TEST(bounded_random_inputs_never_publish_success_without_exact_route_and_result);
  return UNITY_END();
}

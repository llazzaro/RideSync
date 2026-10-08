#pragma once
#include <stdint.h>
// Author-created synthetic schema-derived fixtures, not camera captures.
namespace setup_fixture {
static const uint8_t pair_request[] = {0x0e, 0x03, 0x01, 0x08, 0x00, 0x12, 0x08, 'R',
                                       'i',  'd',  'e',  'S',  'y',  'n',  'c'};
static const uint8_t pair_request_extended[] = {0x20, 0x0e, 0x03, 0x01, 0x08, 0x00, 0x12, 0x08,
                                                'R',  'i',  'd',  'e',  'S',  'y',  'n',  'c'};
static const uint8_t claim_request[] = {0x04, 0xf1, 0x69, 0x08, 0x02};
static const uint8_t claim_request_extended[] = {0x20, 0x04, 0xf1, 0x69, 0x08, 0x02};
static const uint8_t pair_success[] = {0x04, 0x03, 0x81, 0x08, 0x01};
static const uint8_t claim_success_extended[] = {0x20, 0x04, 0xf1, 0xe9, 0x08, 0x01};
} // namespace setup_fixture

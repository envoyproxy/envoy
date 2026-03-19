// Minimal BER helpers for LDAP message inspection.
// Only what we need for StartTLS detection - no generic ASN.1 stuff.

#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LdapProxy {

// 16 KB limit to prevent memory exhaustion
constexpr size_t kMaxInspectBufferSize = 16 * 1024;

// BER tags we care about
constexpr uint8_t kBerTagSequence = 0x30;
constexpr uint8_t kBerTagInteger = 0x02;
constexpr uint8_t kBerTagContext0 = 0x80;
constexpr uint8_t kBerTagExtendedRequest = 0x77;
constexpr uint8_t kLdapExtendedRequestTag = 0x77;

// StartTLS OID "1.3.6.1.4.1.1466.20037" as ASCII bytes
constexpr uint8_t kStartTlsOid[] = {0x31, 0x2e, 0x33, 0x2e, 0x36, 0x2e, 0x31, 0x2e,
                                    0x34, 0x2e, 0x31, 0x2e, 0x31, 0x34, 0x36, 0x36,
                                    0x2e, 0x32, 0x30, 0x30, 0x33, 0x37};
constexpr size_t kStartTlsOidLength = sizeof(kStartTlsOid);

struct BerLengthResult {
  bool valid{false};
  bool need_more_data{false};
  size_t length{0};
  size_t header_bytes{0};
};

struct MessageIdResult {
  bool valid{false};
  uint32_t message_id{0};
  size_t total_bytes{0};
};

// Extract BER length field. Handles both short and long forms.
inline BerLengthResult extractBerLength(const uint8_t* data, size_t data_len, size_t offset) {
  BerLengthResult result;

  if (offset >= data_len) {
    result.need_more_data = true;
    return result;
  }

  uint8_t first_byte = data[offset];

  if ((first_byte & 0x80) == 0) {
    result.valid = true;
    result.length = first_byte;
    result.header_bytes = 1;
    return result;
  }

  uint8_t num_length_bytes = first_byte & 0x7F;

  if (num_length_bytes == 0 || num_length_bytes > 4) {
    return result;
  }

  if (offset + 1 + num_length_bytes > data_len) {
    result.need_more_data = true;
    return result;
  }

  size_t length = 0;
  for (uint8_t i = 0; i < num_length_bytes; ++i) {
    length = (length << 8) | data[offset + 1 + i];
  }

  if (length > kMaxInspectBufferSize) {
    return result;
  }

  result.valid = true;
  result.length = length;
  result.header_bytes = 1 + num_length_bytes;
  return result;
}

// Pull out the messageID from an LDAPMessage envelope
inline MessageIdResult extractMessageId(const uint8_t* data, size_t data_len) {
  MessageIdResult result;
  size_t offset = 0;

  if (data_len < 2 || data[offset] != kBerTagSequence) {
    return result;
  }
  offset++;

  auto seq_len = extractBerLength(data, data_len, offset);
  if (!seq_len.valid) {
    return result;
  }
  offset += seq_len.header_bytes;

  if (offset >= data_len || data[offset] != kBerTagInteger) {
    return result;
  }
  offset++;

  auto int_len = extractBerLength(data, data_len, offset);
  if (!int_len.valid || int_len.need_more_data) {
    return result;
  }
  offset += int_len.header_bytes;

  if (int_len.length == 0 || int_len.length > 4 || offset + int_len.length > data_len) {
    return result;
  }

  uint32_t msg_id = 0;
  for (size_t i = 0; i < int_len.length; ++i) {
    msg_id = (msg_id << 8) | data[offset + i];
  }

  result.valid = true;
  result.message_id = msg_id;
  result.total_bytes = offset + int_len.length;
  return result;
}

// Check if we have a complete LDAP message (just looks at outer SEQUENCE)
inline std::pair<bool, size_t> isCompleteBerMessage(const uint8_t* data, size_t data_len) {
  if (data_len < 2 || data[0] != kBerTagSequence) {
    return {false, 0};
  }

  auto len_result = extractBerLength(data, data_len, 1);
  if (!len_result.valid) {
    return {false, 0};
  }

  size_t total_len = 1 + len_result.header_bytes + len_result.length;
  if (data_len < total_len) {
    return {false, 0};
  }

  return {true, total_len};
}

} // namespace LdapProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

// © 2026 Nokia
// Licensed under the Apache License 2.0
// SPDX-License-Identifier: Apache-2.0

// Pre-baked LDAP response templates.
// We use static byte arrays instead of building ASN.1 dynamically.

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LdapProxy {

// LDAP result codes (RFC 4511)
constexpr uint8_t kLdapResultSuccess = 0;
constexpr uint8_t kLdapResultProtocolError = 2;
constexpr uint8_t kLdapResultConfidentialityRequired = 13;

// BER-encode a uint32 as INTEGER
inline std::vector<uint8_t> encodeBerInteger(uint32_t value) {
  std::vector<uint8_t> result;
  result.push_back(0x02);

  std::vector<uint8_t> value_bytes;

  if (value == 0) {
    value_bytes.push_back(0x00);
  } else {
    bool started = false;
    for (int i = 3; i >= 0; --i) {
      uint8_t byte = (value >> (i * 8)) & 0xFF;
      if (byte != 0 || started) {
        value_bytes.push_back(byte);
        started = true;
      }
    }
    // need leading zero if high bit set (BER sign rules)
    if (!value_bytes.empty() && (value_bytes[0] & 0x80)) {
      value_bytes.insert(value_bytes.begin(), 0x00);
    }
  }

  result.push_back(static_cast<uint8_t>(value_bytes.size()));
  result.insert(result.end(), value_bytes.begin(), value_bytes.end());
  return result;
}

// Static templates - these get the messageID patched in at runtime
// 
// StartTLS success with OID in responseName
constexpr std::array<uint8_t, 38> kStartTlsSuccessTemplate = {
    0x30, 0x24,
    0x02, 0x01, 0x01,       // msgID placeholder
    0x78, 0x1f,
    0x0a, 0x01, 0x00,       // resultCode = 0
    0x04, 0x00,
    0x04, 0x00,
    0x8a, 0x16,
    0x31, 0x2e, 0x33, 0x2e, 0x36, 0x2e, 0x31, 0x2e,
    0x34, 0x2e, 0x31, 0x2e, 0x31, 0x34, 0x36, 0x36,
    0x2e, 0x32, 0x30, 0x30, 0x33, 0x37
};
constexpr size_t kStartTlsSuccessMsgIdOffset = 4;

constexpr std::array<uint8_t, 14> kConfidentialityRequiredTemplate = {
    0x30, 0x0c,
    0x02, 0x01, 0x01,
    0x78, 0x07,
    0x0a, 0x01, 0x0d,       // resultCode = 13
    0x04, 0x00,
    0x04, 0x00
};
constexpr size_t kConfidentialityRequiredMsgIdOffset = 4;

constexpr std::array<uint8_t, 14> kProtocolErrorTemplate = {
    0x30, 0x0c,
    0x02, 0x01, 0x01,
    0x78, 0x07,
    0x0a, 0x01, 0x02,       // resultCode = 2
    0x04, 0x00,
    0x04, 0x00
};
constexpr size_t kProtocolErrorMsgIdOffset = 4;

// Response bodies (without outer SEQUENCE and messageID)
constexpr std::array<uint8_t, 33> kStartTlsSuccessBody = {
    0x78, 0x1f,
    0x0a, 0x01, 0x00,
    0x04, 0x00,
    0x04, 0x00,
    0x8a, 0x16,
    0x31, 0x2e, 0x33, 0x2e, 0x36, 0x2e, 0x31, 0x2e,
    0x34, 0x2e, 0x31, 0x2e, 0x31, 0x34, 0x36, 0x36,
    0x2e, 0x32, 0x30, 0x30, 0x33, 0x37
};

constexpr std::array<uint8_t, 9> kConfidentialityRequiredBody = {
    0x78, 0x07,
    0x0a, 0x01, 0x0d,
    0x04, 0x00,
    0x04, 0x00
};

constexpr std::array<uint8_t, 9> kProtocolErrorBody = {
    0x78, 0x07,
    0x0a, 0x01, 0x02,
    0x04, 0x00,
    0x04, 0x00
};

// Build complete response by combining msgID with body
template <size_t N>
inline std::vector<uint8_t> buildLdapResponse(uint32_t msg_id,
                                               const std::array<uint8_t, N>& body) {
  std::vector<uint8_t> msg_id_encoded = encodeBerInteger(msg_id);
  size_t content_len = msg_id_encoded.size() + body.size();

  std::vector<uint8_t> response;
  response.reserve(2 + content_len);

  response.push_back(0x30);

  if (content_len <= 127) {
    response.push_back(static_cast<uint8_t>(content_len));
  } else if (content_len <= 255) {
    response.push_back(0x81);
    response.push_back(static_cast<uint8_t>(content_len));
  } else {
    response.push_back(0x82);
    response.push_back(static_cast<uint8_t>((content_len >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(content_len & 0xFF));
  }

  response.insert(response.end(), msg_id_encoded.begin(), msg_id_encoded.end());
  response.insert(response.end(), body.begin(), body.end());

  return response;
}

inline std::vector<uint8_t> createStartTlsSuccessResponse(uint32_t msg_id) {
  return buildLdapResponse(msg_id, kStartTlsSuccessBody);
}

inline std::vector<uint8_t> createConfidentialityRequiredResponse(uint32_t msg_id) {
  return buildLdapResponse(msg_id, kConfidentialityRequiredBody);
}

inline std::vector<uint8_t> createProtocolErrorResponse(uint32_t msg_id) {
  return buildLdapResponse(msg_id, kProtocolErrorBody);
}

// StartTLS request to send upstream
constexpr std::array<uint8_t, 26> kStartTlsRequestBody = {
    0x77, 0x18,
    0x80, 0x16,
    0x31, 0x2e, 0x33, 0x2e, 0x36, 0x2e, 0x31, 0x2e,
    0x34, 0x2e, 0x31, 0x2e, 0x31, 0x34, 0x36, 0x36,
    0x2e, 0x32, 0x30, 0x30, 0x33, 0x37
};

inline std::vector<uint8_t> createStartTlsRequest(uint32_t msg_id) {
  return buildLdapResponse(msg_id, kStartTlsRequestBody);
}

// Parse ExtendedResponse to get resultCode - just enough for StartTLS handling
inline bool parseExtendedResponse(const uint8_t* data, size_t length,
                                   uint8_t& result_code, uint32_t& msg_id) {
  if (length < 10) {
    return false;
  }

  size_t pos = 0;

  if (data[pos++] != 0x30) {
    return false;
  }

  if (pos >= length) return false;
  if (data[pos] & 0x80) {
    uint8_t len_bytes = data[pos] & 0x7F;
    if (len_bytes > 4 || pos + 1 + len_bytes > length) {
      return false;
    }
    pos += 1 + len_bytes;
  } else {
    pos++;
  }

  if (pos >= length) return false;

  if (data[pos++] != 0x02) {
    return false;
  }

  if (pos >= length) return false;

  uint8_t id_len = data[pos++];
  if (id_len > 4 || pos + id_len > length) {
    return false;
  }

  msg_id = 0;
  for (uint8_t i = 0; i < id_len; ++i) {
    msg_id = (msg_id << 8) | data[pos++];
  }

  if (pos >= length) return false;

  if (data[pos++] != 0x78) {
    return false;
  }

  if (pos >= length) return false;
  if (data[pos] & 0x80) {
    uint8_t len_bytes = data[pos] & 0x7F;
    if (len_bytes > 4 || pos + 1 + len_bytes > length) {
      return false;
    }
    pos += 1 + len_bytes;
  } else {
    pos++;
  }

  if (pos >= length) return false;

  if (data[pos++] != 0x0a) {
    return false;
  }

  if (pos >= length) return false;

  if (data[pos++] != 0x01) {
    return false;
  }

  if (pos >= length) return false;

  result_code = data[pos];

  return true;
}

} // namespace LdapProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

#include "extensions/quic_listeners/quiche/platform/spdy_string_utils_impl.h"

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <arpa/inet.h>
#include <cstring>
#include <string>

#include "common/common/assert.h"

namespace spdy {

namespace {
/* clang-format off */
constexpr char kHexValue[256] = {
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 0, 0, 0, 0, 0, 0,  // '0'..'9'
    0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 'A'..'F'
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 'a'..'f'
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
/* clang-format on */
} // namespace

char SpdyHexDigitToIntImpl(char c) {
  ASSERT(std::isxdigit(c));

  if (std::isdigit(c))
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return 0;
}

bool SpdyHexDecodeToUInt32Impl(absl::string_view data, uint32_t* out) {
  if (data.empty() || data.size() > 8u)
    return false;
  // Pad with leading zeros.
  std::string data_padded(8u, '0');
  memcpy(&data_padded[8u - data.size()], data.data(), data.size());

  std::string byte_string;
  byte_string.resize(4);
  std::string::const_iterator hex_p = data_padded.begin();
  for (std::string::iterator bin_p = byte_string.begin(); bin_p != byte_string.end(); ++bin_p) {
    unsigned char c1 = *hex_p++;
    unsigned char c2 = *hex_p++;

    if (absl::ascii_isxdigit(c1) && absl::ascii_isxdigit(c2)) {
      *bin_p = (kHexValue[c1] << 4) + kHexValue[c2];
    } else {
      return false;
    }
  }

  ASSERT(byte_string.size() == 4u);
  *out = ntohl(*reinterpret_cast<const uint32_t*>(byte_string.c_str()));
  return true;
}

std::string SpdyHexDumpImpl(absl::string_view data) {
  const int kBytesPerLine = 16;
  const char* buf = data.data();
  int bytes_remaining = data.size();
  int offset = 0;
  std::string out;
  const char* p = buf;
  while (bytes_remaining > 0) {
    const int line_bytes = std::min(bytes_remaining, kBytesPerLine);
    absl::StrAppendFormat(&out, "0x%04x:  ", offset); // Do the line header
    for (int i = 0; i < kBytesPerLine; ++i) {
      if (i < line_bytes) {
        absl::StrAppendFormat(&out, "%02x", p[i]);
      } else {
        out += "  "; // two-space filler instead of two-space hex digits
      }
      if (i % 2) {
        out += ' ';
      }
    }
    out += ' ';
    for (int i = 0; i < line_bytes; ++i) { // Do the ASCII dump
      out += absl::ascii_isgraph(p[i]) ? p[i] : '.';
    }

    bytes_remaining -= line_bytes;
    offset += line_bytes;
    p += line_bytes;
    out += '\n';
  }
  return out;
}

} // namespace spdy

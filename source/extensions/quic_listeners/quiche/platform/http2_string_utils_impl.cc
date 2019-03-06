#include "extensions/quic_listeners/quiche/platform/http2_string_utils_impl.h"

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace http2 {

std::string Http2HexDumpImpl(absl::string_view data) {
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

} // namespace http2

#include "extensions/quic_listeners/quiche/platform/quic_text_utils_impl.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

// static
void QuicTextUtilsImpl::Base64Encode(const uint8_t* data, size_t data_len, QuicString* output) {
  absl::Base64Escape(QuicString(reinterpret_cast<const char*>(data), data_len), output);
  // Remove padding.
  size_t len = output->size();
  if (len >= 2) {
    if ((*output)[len - 1] == '=') {
      len--;
      if ((*output)[len - 1] == '=') {
        len--;
      }
      output->resize(len);
    }
  }
}

// static
QuicString QuicTextUtilsImpl::HexDump(QuicStringPiece binary_data) {
  const int kBytesPerLine = 16;
  const char* buf = binary_data.data();
  int bytes_remaining = binary_data.size();
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

} // namespace quic

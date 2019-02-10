#pragma once

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "quiche/quic/platform/api/quic_string.h"
#include "quiche/quic/platform/api/quic_string_piece.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

class QuicTextUtilsImpl {
public:
  static bool StartsWith(QuicStringPiece data, QuicStringPiece prefix) {
    return absl::StartsWith(data, prefix);
  }

  static bool EndsWithIgnoreCase(QuicStringPiece data, QuicStringPiece suffix) {
    return absl::EndsWithIgnoreCase(data, suffix);
  }

  static QuicString ToLower(QuicStringPiece data) { return absl::AsciiStrToLower(data); }

  static void RemoveLeadingAndTrailingWhitespace(QuicStringPiece* data) {
    size_t count = 0;
    const char* ptr = data->data();
    while (count < data->size() && absl::ascii_isspace(*ptr)) {
      ++count;
      ++ptr;
    }
    data->remove_prefix(count);

    count = 0;
    ptr = data->data() + data->size() - 1;
    while (count < data->size() && absl::ascii_isspace(*ptr)) {
      ++count;
      --ptr;
    }
    data->remove_suffix(count);
  }

  static bool StringToUint64(QuicStringPiece in, uint64_t* out) {
    return absl::SimpleAtoi(in, out);
  }

  static bool StringToInt(QuicStringPiece in, int* out) { return absl::SimpleAtoi(in, out); }

  static bool StringToUint32(QuicStringPiece in, uint32_t* out) {
    return absl::SimpleAtoi(in, out);
  }

  static bool StringToSizeT(QuicStringPiece in, size_t* out) { return absl::SimpleAtoi(in, out); }

  static QuicString Uint64ToString(uint64_t in) { return absl::StrCat(in); }

  static QuicString HexEncode(QuicStringPiece data) { return absl::BytesToHexString(data); }

  static QuicString Hex(uint32_t v) { return absl::StrCat(absl::Hex(v)); }

  static QuicString HexDecode(QuicStringPiece data) { return absl::HexStringToBytes(data); }

  static void Base64Encode(const uint8_t* data, size_t data_len, QuicString* output) {
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

  static QuicString HexDump(QuicStringPiece binary_data) {
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

  static bool ContainsUpperCase(QuicStringPiece data) {
    return std::any_of(data.begin(), data.end(), absl::ascii_isupper);
  }

  static std::vector<QuicStringPiece> Split(QuicStringPiece data, char delim) {
    return absl::StrSplit(data, delim);
  }
};

} // namespace quic

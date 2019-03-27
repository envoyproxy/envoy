#pragma once

#include "extensions/quic_listeners/quiche/platform/string_utils.h"

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
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

  static std::string ToLower(QuicStringPiece data) { return absl::AsciiStrToLower(data); }

  static void RemoveLeadingAndTrailingWhitespace(QuicStringPiece* data) {
    *data = absl::StripAsciiWhitespace(*data);
  }

  static bool StringToUint64(QuicStringPiece in, uint64_t* out) {
    return absl::SimpleAtoi(in, out);
  }

  static bool StringToInt(QuicStringPiece in, int* out) { return absl::SimpleAtoi(in, out); }

  static bool StringToUint32(QuicStringPiece in, uint32_t* out) {
    return absl::SimpleAtoi(in, out);
  }

  static bool StringToSizeT(QuicStringPiece in, size_t* out) { return absl::SimpleAtoi(in, out); }

  static std::string Uint64ToString(uint64_t in) { return absl::StrCat(in); }

  static std::string HexEncode(QuicStringPiece data) { return absl::BytesToHexString(data); }

  static std::string Hex(uint32_t v) { return absl::StrCat(absl::Hex(v)); }

  static std::string HexDecode(QuicStringPiece data) { return absl::HexStringToBytes(data); }

  static void Base64Encode(const uint8_t* data, size_t data_len, std::string* output) {
    return quiche::Base64Encode(data, data_len, output);
  }

  static std::string HexDump(QuicStringPiece binary_data) { return quiche::HexDump(binary_data); }

  static bool ContainsUpperCase(QuicStringPiece data) {
    return std::any_of(data.begin(), data.end(), absl::ascii_isupper);
  }

  static std::vector<QuicStringPiece> Split(QuicStringPiece data, char delim) {
    return absl::StrSplit(data, delim);
  }
};

} // namespace quic

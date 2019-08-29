#pragma once

#include "extensions/quic_listeners/quiche/platform/quic_string_piece_impl.h"
#include "extensions/quic_listeners/quiche/platform/string_utils.h"

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

class QuicTextUtilsImpl {
public:
  static bool StartsWith(QuicStringPieceImpl data, QuicStringPieceImpl prefix) {
    return absl::StartsWith(data, prefix);
  }

  static bool EndsWithIgnoreCase(QuicStringPieceImpl data, QuicStringPieceImpl suffix) {
    return absl::EndsWithIgnoreCase(data, suffix);
  }

  static std::string ToLower(QuicStringPieceImpl data) { return absl::AsciiStrToLower(data); }

  static void RemoveLeadingAndTrailingWhitespace(QuicStringPieceImpl* data) {
    *data = absl::StripAsciiWhitespace(*data);
  }

  static bool StringToUint64(QuicStringPieceImpl in, uint64_t* out) {
    return absl::SimpleAtoi(in, out);
  }

  static bool StringToInt(QuicStringPieceImpl in, int* out) { return absl::SimpleAtoi(in, out); }

  static bool StringToUint32(QuicStringPieceImpl in, uint32_t* out) {
    return absl::SimpleAtoi(in, out);
  }

  static bool StringToSizeT(QuicStringPieceImpl in, size_t* out) {
    return absl::SimpleAtoi(in, out);
  }

  static std::string Uint64ToString(uint64_t in) { return absl::StrCat(in); }

  static std::string HexEncode(QuicStringPieceImpl data) { return absl::BytesToHexString(data); }

  static std::string Hex(uint32_t v) { return absl::StrCat(absl::Hex(v)); }

  static std::string HexDecode(QuicStringPieceImpl data) { return absl::HexStringToBytes(data); }

  static void Base64Encode(const uint8_t* data, size_t data_len, std::string* output) {
    return quiche::Base64Encode(data, data_len, output);
  }

  static std::string HexDump(QuicStringPieceImpl binary_data) {
    return quiche::HexDump(binary_data);
  }

  static bool ContainsUpperCase(QuicStringPieceImpl data) {
    return std::any_of(data.begin(), data.end(), absl::ascii_isupper);
  }

  static bool IsAllDigits(QuicStringPieceImpl data) {
    return std::all_of(data.begin(), data.end(), absl::ascii_isdigit);
  }

  static std::vector<QuicStringPieceImpl> Split(QuicStringPieceImpl data, char delim) {
    return absl::StrSplit(data, delim);
  }
};

} // namespace quic

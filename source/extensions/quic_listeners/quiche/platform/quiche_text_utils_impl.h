#pragma once

#include "extensions/quic_listeners/quiche/platform/quiche_string_piece_impl.h"
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

namespace quiche {

class QuicheTextUtilsImpl {
public:
  static bool StartsWith(QuicheStringPieceImpl data, QuicheStringPieceImpl prefix) {
    return absl::StartsWith(data, prefix);
  }

  static bool EndsWithIgnoreCase(QuicheStringPieceImpl data, QuicheStringPieceImpl suffix) {
    return absl::EndsWithIgnoreCase(data, suffix);
  }

  static std::string ToLower(QuicheStringPieceImpl data) { return absl::AsciiStrToLower(data); }

  static void RemoveLeadingAndTrailingWhitespace(QuicheStringPieceImpl* data) {
    *data = absl::StripAsciiWhitespace(*data);
  }

  static bool StringToUint64(QuicheStringPieceImpl in, uint64_t* out) {
    return absl::SimpleAtoi(in, out);
  }

  static bool StringToInt(QuicheStringPieceImpl in, int* out) { return absl::SimpleAtoi(in, out); }

  static bool StringToUint32(QuicheStringPieceImpl in, uint32_t* out) {
    return absl::SimpleAtoi(in, out);
  }

  static bool StringToSizeT(QuicheStringPieceImpl in, size_t* out) {
    return absl::SimpleAtoi(in, out);
  }

  static std::string Uint64ToString(uint64_t in) { return absl::StrCat(in); }

  static std::string HexEncode(QuicheStringPieceImpl data) { return absl::BytesToHexString(data); }

  static std::string Hex(uint32_t v) { return absl::StrCat(absl::Hex(v)); }

  static std::string HexDecode(QuicheStringPieceImpl data) { return absl::HexStringToBytes(data); }

  static void Base64Encode(const uint8_t* data, size_t data_len, std::string* output) {
    return quiche::Base64Encode(data, data_len, output);
  }

  static std::string HexDump(QuicheStringPieceImpl binary_data) {
    return quiche::HexDump(binary_data);
  }

  static bool ContainsUpperCase(QuicheStringPieceImpl data) {
    return std::any_of(data.begin(), data.end(), absl::ascii_isupper);
  }

  static bool IsAllDigits(QuicheStringPieceImpl data) {
    return std::all_of(data.begin(), data.end(), absl::ascii_isdigit);
  }

  static std::vector<QuicheStringPieceImpl> Split(QuicheStringPieceImpl data, char delim) {
    return absl::StrSplit(data, delim);
  }
};

} // namespace quiche

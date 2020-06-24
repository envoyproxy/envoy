#pragma once

#include "common/common/base64.h"

#include "extensions/quic_listeners/quiche/platform/quiche_optional_impl.h"
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
  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool StartsWith(QuicheStringPieceImpl data, QuicheStringPieceImpl prefix) {
    return absl::StartsWith(data, prefix);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool EndsWith(QuicheStringPieceImpl data, QuicheStringPieceImpl suffix) {
    return absl::EndsWith(data, suffix);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool EndsWithIgnoreCase(QuicheStringPieceImpl data, QuicheStringPieceImpl suffix) {
    return absl::EndsWithIgnoreCase(data, suffix);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static std::string ToLower(QuicheStringPieceImpl data) { return absl::AsciiStrToLower(data); }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static void RemoveLeadingAndTrailingWhitespace(QuicheStringPieceImpl* data) {
    *data = absl::StripAsciiWhitespace(*data);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool StringToUint64(QuicheStringPieceImpl in, uint64_t* out) {
    return absl::SimpleAtoi(in, out);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool StringToInt(QuicheStringPieceImpl in, int* out) { return absl::SimpleAtoi(in, out); }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool StringToUint32(QuicheStringPieceImpl in, uint32_t* out) {
    return absl::SimpleAtoi(in, out);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool StringToSizeT(QuicheStringPieceImpl in, size_t* out) {
    return absl::SimpleAtoi(in, out);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static std::string Uint64ToString(uint64_t in) { return absl::StrCat(in); }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static std::string HexEncode(QuicheStringPieceImpl data) { return absl::BytesToHexString(data); }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static std::string Hex(uint32_t v) { return absl::StrCat(absl::Hex(v)); }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static std::string HexDecode(QuicheStringPieceImpl data) { return absl::HexStringToBytes(data); }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static void Base64Encode(const uint8_t* data, size_t data_len, std::string* output) {
    *output =
        Envoy::Base64::encode(reinterpret_cast<const char*>(data), data_len, /*add_padding=*/false);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static QuicheOptionalImpl<std::string> Base64Decode(QuicheStringPieceImpl input) {
    return Envoy::Base64::decodeWithoutPadding(input);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static std::string HexDump(QuicheStringPieceImpl binary_data) {
    return quiche::HexDump(binary_data);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool ContainsUpperCase(QuicheStringPieceImpl data) {
    return std::any_of(data.begin(), data.end(), absl::ascii_isupper);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool IsAllDigits(QuicheStringPieceImpl data) {
    return std::all_of(data.begin(), data.end(), absl::ascii_isdigit);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static std::vector<QuicheStringPieceImpl> Split(QuicheStringPieceImpl data, char delim) {
    return absl::StrSplit(data, delim);
  }
};

} // namespace quiche

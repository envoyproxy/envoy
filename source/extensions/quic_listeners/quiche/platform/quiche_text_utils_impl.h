#pragma once

#include "common/common/base64.h"

#include "extensions/quic_listeners/quiche/platform/quiche_string_piece_impl.h"
#include "extensions/quic_listeners/quiche/platform/string_utils.h"

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/types/optional.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quiche {

class QuicheTextUtilsImpl {
public:
  // NOLINTNEXTLINE(readability-identifier-naming)
  static std::string ToLower(absl::string_view data) { return absl::AsciiStrToLower(data); }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static void RemoveLeadingAndTrailingWhitespace(absl::string_view* data) {
    *data = absl::StripAsciiWhitespace(*data);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static std::string Hex(uint32_t v) { return absl::StrCat(absl::Hex(v)); }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static void Base64Encode(const uint8_t* data, size_t data_len, std::string* output) {
    *output =
        Envoy::Base64::encode(reinterpret_cast<const char*>(data), data_len, /*add_padding=*/false);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static absl::optional<std::string> Base64Decode(absl::string_view input) {
    return Envoy::Base64::decodeWithoutPadding(input);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static std::string Uint64ToString(uint64_t in) { return absl::StrCat(in); }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static std::string HexDump(absl::string_view binary_data) { return quiche::HexDump(binary_data); }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool ContainsUpperCase(absl::string_view data) {
    return std::any_of(data.begin(), data.end(), absl::ascii_isupper);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool IsAllDigits(absl::string_view data) {
    return std::all_of(data.begin(), data.end(), absl::ascii_isdigit);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  static std::vector<absl::string_view> Split(absl::string_view data, char delim) {
    return absl::StrSplit(data, delim);
  }
};

} // namespace quiche

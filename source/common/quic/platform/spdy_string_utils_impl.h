#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "common/quic/platform/string_utils.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "fmt/printf.h"

namespace spdy {

// NOLINTNEXTLINE(readability-identifier-naming)
inline char SpdyHexDigitToIntImpl(char c) { return quiche::HexDigitToInt(c); }

// NOLINTNEXTLINE(readability-identifier-naming)
inline std::string SpdyHexDecodeImpl(absl::string_view data) {
  return absl::HexStringToBytes(data);
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline bool SpdyHexDecodeToUInt32Impl(absl::string_view data, uint32_t* out) {
  return quiche::HexDecodeToUInt32(data, out);
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline std::string SpdyHexEncodeImpl(const void* bytes, size_t size) {
  return absl::BytesToHexString(absl::string_view(static_cast<const char*>(bytes), size));
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline std::string SpdyHexDumpImpl(absl::string_view data) { return quiche::HexDump(data); }

} // namespace spdy

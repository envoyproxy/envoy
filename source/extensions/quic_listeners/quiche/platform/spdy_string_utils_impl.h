#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/string_utils.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "fmt/printf.h"

namespace spdy {

template <typename... Args>
inline void SpdyStrAppendImpl(std::string* output, const Args&... args) {
  absl::StrAppend(output, std::forward<const Args&>(args)...);
}

inline char SpdyHexDigitToIntImpl(char c) { return quiche::HexDigitToInt(c); }

inline std::string SpdyHexDecodeImpl(absl::string_view data) {
  return absl::HexStringToBytes(data);
}

inline bool SpdyHexDecodeToUInt32Impl(absl::string_view data, uint32_t* out) {
  return quiche::HexDecodeToUInt32(data, out);
}

inline std::string SpdyHexEncodeImpl(const void* bytes, size_t size) {
  return absl::BytesToHexString(absl::string_view(static_cast<const char*>(bytes), size));
}

inline std::string SpdyHexEncodeUInt32AndTrimImpl(uint32_t data) {
  return absl::StrCat(absl::Hex(data));
}

inline std::string SpdyHexDumpImpl(absl::string_view data) { return quiche::HexDump(data); }

struct SpdyStringPieceCaseHashImpl {
  size_t operator()(quiche::QuicheStringPiece data) const {
    std::string lower = absl::AsciiStrToLower(data);
    return absl::Hash<std::string>()(lower);
  }
};

struct SpdyStringPieceCaseEqImpl {
  bool operator()(absl::string_view piece1, absl::string_view piece2) const {
    return absl::EqualsIgnoreCase(piece1, piece2);
  }
};

} // namespace spdy

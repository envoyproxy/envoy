#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/string_utils.h"

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "fmt/printf.h"

namespace http2 {

template <typename... Args> inline std::string Http2StrCatImpl(const Args&... args) {
  return absl::StrCat(std::forward<const Args&>(args)...);
}

template <typename... Args>
inline void Http2StrAppendImpl(std::string* output, const Args&... args) {
  absl::StrAppend(output, std::forward<const Args&>(args)...);
}

template <typename... Args> inline std::string Http2StringPrintfImpl(const Args&... args) {
  return fmt::sprintf(std::forward<const Args&>(args)...);
}

inline std::string Http2HexEncodeImpl(const void* bytes, size_t size) {
  return absl::BytesToHexString(absl::string_view(static_cast<const char*>(bytes), size));
}

inline std::string Http2HexDecodeImpl(absl::string_view data) {
  return absl::HexStringToBytes(data);
}

inline std::string Http2HexDumpImpl(absl::string_view data) { return quiche::HexDump(data); }

inline std::string Http2HexEscapeImpl(absl::string_view data) { return absl::CHexEscape(data); }

template <typename Number> inline std::string Http2HexImpl(Number number) {
  return absl::StrCat(absl::Hex(number));
}

} // namespace http2

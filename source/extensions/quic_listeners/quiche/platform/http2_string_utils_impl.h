#pragma once

// NOLINT(namespace-envoy)

namespace http2 {

template <typename... Args> inline Http2String Http2StrCatImpl(const Args&... args) {
  return Http2String();
}

template <typename... Args>
inline void Http2StrAppendImpl(Http2String* output, const Args&... args) {}

template <typename... Args> inline Http2String Http2StringPrintfImpl(const Args&... args) {
  return Http2String();
}

inline Http2String Http2HexEncodeImpl(const void* bytes, size_t size) { return Http2String(); }

inline Http2String Http2HexDecodeImpl(Http2StringPiece data) { return Http2String(); }

inline Http2String Http2HexDumpImpl(Http2StringPiece data) { return Http2String(); }

inline Http2String Http2HexEscapeImpl(Http2StringPiece data) { return Http2String(); }

template <typename Number> inline Http2String Http2HexImpl(Number number) { return Http2String(); }

} // namespace http2

#pragma once

#include <cstddef>

// The following includes should be:
//
// #include "extensions/quic_listeners/quiche/platform/http2_string_impl.h"
// #include "extensions/quic_listeners/quiche/platform/http2_string_piece_impl.h"
//
// However, for some reason, bazel.clang_tidy cannot resolve the files when specified this way.
// TODO(mpw): fix includes to use full paths.
#include "http2_string_impl.h"
#include "http2_string_piece_impl.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

// TODO: implement

namespace http2 {

template <typename... Args> inline Http2StringImpl Http2StrCatImpl(const Args&... /*args*/) {
  return Http2StringImpl();
}

template <typename... Args>
inline void Http2StrAppendImpl(Http2StringImpl* /*output*/, const Args&... /*args*/) {}

template <typename... Args> inline Http2StringImpl Http2StringPrintfImpl(const Args&... /*args*/) {
  return Http2StringImpl();
}

inline Http2StringImpl Http2HexEncodeImpl(const void* /*bytes*/, size_t /*size*/) {
  return Http2StringImpl();
}

inline Http2StringImpl Http2HexDecodeImpl(Http2StringPieceImpl /*data*/) {
  return Http2StringImpl();
}

inline Http2StringImpl Http2HexDumpImpl(Http2StringPieceImpl /*data*/) { return Http2StringImpl(); }

inline Http2StringImpl Http2HexEscapeImpl(Http2StringPieceImpl /*data*/) {
  return Http2StringImpl();
}

template <typename Number> inline Http2StringImpl Http2HexImpl(Number /*number*/) {
  return Http2StringImpl();
}

} // namespace http2

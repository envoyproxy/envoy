#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_string.h"
#include "quiche/quic/platform/api/quic_string_piece.h"

namespace quic {

class QUIC_EXPORT_PRIVATE QuicHostnameUtilsImpl {
public:
  // Returns true if the sni is valid, false otherwise.
  //  (1) disallow IP addresses;
  //  (2) check that the hostname contains valid characters only; and
  //  (3) contains at least one dot.
  // NOTE(wub): Only (3) is implemented for now.
  static bool IsValidSNI(QuicStringPiece sni);

  // Convert hostname to lowercase and remove the trailing '.'.
  // WARNING: mutates |hostname| in place and returns |hostname|.
  // NOTE(wub): This only does lowercasing and removes trailing dots for now.
  static QuicString NormalizeHostname(QuicStringPiece hostname);

private:
  QuicHostnameUtilsImpl() = delete;
};

} // namespace quic

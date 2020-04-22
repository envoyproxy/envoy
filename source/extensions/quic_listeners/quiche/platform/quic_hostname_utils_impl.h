#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche/common/platform/api/quiche_string_piece.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

class QUIC_EXPORT_PRIVATE QuicHostnameUtilsImpl {
public:
  // Returns true if the sni is valid, false otherwise.
  //  (1) disallow IP addresses;
  //  (2) check that the hostname contains valid characters only; and
  //  (3) contains at least one dot.
  // NOTE(wub): Only (3) is implemented for now.
  static bool IsValidSNI(quiche::QuicheStringPiece sni);

  // Normalize a hostname:
  //  (1) Canonicalize it, similar to what Chromium does in
  //  https://cs.chromium.org/chromium/src/net/base/url_util.h?q=net::CanonicalizeHost
  //  (2) Convert it to lower case.
  //  (3) Remove the trailing '.'.
  // WARNING: May mutate |hostname| in place.
  // NOTE(wub): Only (2) and (3) are implemented for now.
  static std::string NormalizeHostname(quiche::QuicheStringPiece hostname);

private:
  QuicHostnameUtilsImpl() = delete;
};

} // namespace quic

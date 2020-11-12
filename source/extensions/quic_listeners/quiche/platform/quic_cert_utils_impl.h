#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/strings/string_view.h"
#include "openssl/base.h"

namespace quic {

class QuicCertUtilsImpl {
public:
  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool ExtractSubjectNameFromDERCert(absl::string_view cert, absl::string_view* subject_out);
};

} // namespace quic

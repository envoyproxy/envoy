#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "openssl/base.h"
#include "quiche/quic/platform/api/quic_string_piece.h"

namespace quic {

class QuicCertUtilsImpl {
public:
  static bool ExtractSubjectNameFromDERCert(QuicStringPiece cert, QuicStringPiece* subject_out);

private:
  static bool SeekToSubject(QuicStringPiece cert, CBS* tbs_certificate);
};

} // namespace quic

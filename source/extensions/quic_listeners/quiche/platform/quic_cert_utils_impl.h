#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "openssl/base.h"
#include "quiche/common/platform/api/quiche_string_piece.h"

namespace quic {

class QuicCertUtilsImpl {
public:
  static bool ExtractSubjectNameFromDERCert(quiche::QuicheStringPiece cert,
                                            quiche::QuicheStringPiece* subject_out);

private:
  static bool SeekToSubject(quiche::QuicheStringPiece cert, CBS* tbs_certificate);
};

} // namespace quic

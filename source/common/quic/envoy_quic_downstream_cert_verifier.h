#pragma once

#include <memory>
#include <string>
#include <vector>

#include "source/common/tls/context_impl.h"

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_verifier.h"

namespace Envoy {
namespace Quic {

// Validates a downstream client certificate chain presented during a QUIC handshake. The
// DER-encoded `certs` are checked against `context`, the server context of the connection's matched
// filter chain, so validation always uses the trust anchor of the chain that requested the
// certificate. Sets `*error_details` and `*details`, sets `*out_alert` when a TLS alert is
// available, and returns `QUIC_SUCCESS` or `QUIC_FAILURE`.
quic::QuicAsyncStatus verifyQuicClientCertChain(
    const std::vector<absl::string_view>& certs,
    Extensions::TransportSockets::Tls::ContextImpl& context, std::string* error_details,
    std::unique_ptr<quic::ProofVerifyDetails>* details, uint8_t* out_alert = nullptr);

} // namespace Quic
} // namespace Envoy

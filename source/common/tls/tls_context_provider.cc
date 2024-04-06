#include "source/common/tls/tls_context_provider.h"

namespace Envoy {
namespace Ssl {

Ssl::SelectionResult
TlsContextProviderImpl::selectTlsContext(const SSL_CLIENT_HELLO* ssl_client_hello,
                                         Ssl::CertSelectionCallbackPtr cb) {
  auto selection_ctx = cb_.lock();
  if (selection_ctx == nullptr) {
    // ENVOY_LOG(debug, "");
    return Ssl::SelectionResult::Terminate;
  }

  auto server_ctx = std::dynamic_pointer_cast<Extensions::TransportSockets::Tls::ServerContextImpl>(
      selection_ctx);

  absl::string_view sni = absl::NullSafeStringView(
      SSL_get_servername(ssl_client_hello->ssl, TLSEXT_NAMETYPE_host_name));
  const bool client_ecdsa_capable = server_ctx->isClientEcdsaCapable(ssl_client_hello);
  const bool client_ocsp_capable = server_ctx->isClientOcspCapable(ssl_client_hello);

  auto [selected_ctx, ocsp_staple_action] =
      server_ctx->findTlsContext(sni, client_ecdsa_capable, client_ocsp_capable, nullptr);

  auto stats = server_ctx->stats();
  if (client_ocsp_capable) {
    stats.ocsp_staple_requests_.inc();
  }

  switch (ocsp_staple_action) {
  case Extensions::TransportSockets::Tls::OcspStapleAction::Staple: {
    stats.ocsp_staple_responses_.inc();
  } break;
  case Extensions::TransportSockets::Tls::OcspStapleAction::NoStaple:
    stats.ocsp_staple_omitted_.inc();
    break;
  case Extensions::TransportSockets::Tls::OcspStapleAction::Fail:
    stats.ocsp_staple_failed_.inc();
    return Ssl::SelectionResult::Terminate;
  case Extensions::TransportSockets::Tls::OcspStapleAction::ClientNotCapable:
    break;
  }
  cb->onCertSelectionResult(true, selected_ctx,
                            ocsp_staple_action ==
                                Extensions::TransportSockets::Tls::OcspStapleAction::Staple);

  return Ssl::SelectionResult::Continue;
}

TlsContextProviderSharedPtr
TlsContextProviderFactoryCbImpl(Ssl::ContextSelectionCallbackWeakPtr cb) {
  return std::make_shared<TlsContextProviderImpl>(cb);
}

} // namespace Ssl
} // namespace Envoy

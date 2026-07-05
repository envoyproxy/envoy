#include "source/common/quic/quic_client_transport_socket_factory.h"

#include <memory>

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.validate.h"

#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tls/client_context_impl.h"
#include "source/common/tls/context_config_impl.h"

#include "quiche/quic/core/crypto/quic_client_session_cache.h"

namespace Envoy {
namespace Quic {

namespace {

// Installs the configured client certificate chain and private key on the QUICHE client SSL
// context so that upstream QUIC connections present a certificate when the peer requests one.
// QUICHE's SSL context uses the CRYPTO_BUFFER-based method, so the chain is installed via
// SSL_CTX_set_chain_and_key rather than the X509-based APIs.
absl::Status configureQuicClientCertChain(SSL_CTX* quic_ssl_ctx,
                                          const Ssl::TlsContext& tls_context) {
  if (tls_context.cert_chain_ == nullptr) {
    // No client certificate configured.
    return absl::OkStatus();
  }
  EVP_PKEY* private_key = SSL_CTX_get0_privatekey(tls_context.ssl_ctx_.get());
  if (private_key == nullptr) {
    // The private key is not directly accessible when a private key provider is configured.
    return absl::UnimplementedError(
        "client certificates with a private key provider are not supported on QUIC");
  }

  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> chain;
  auto append_cert = [&chain](X509* cert) {
    uint8_t* der = nullptr;
    const int len = i2d_X509(cert, &der);
    if (len <= 0) {
      return false;
    }
    bssl::UniquePtr<uint8_t> free_der(der);
    chain.emplace_back(CRYPTO_BUFFER_new(der, len, nullptr));
    return chain.back() != nullptr;
  };
  if (!append_cert(tls_context.cert_chain_.get())) {
    return absl::InvalidArgumentError("failed to convert client certificate for QUIC");
  }
  STACK_OF(X509)* intermediates = nullptr;
  SSL_CTX_get0_chain_certs(tls_context.ssl_ctx_.get(), &intermediates);
  for (size_t i = 0; intermediates != nullptr && i < sk_X509_num(intermediates); i++) {
    if (!append_cert(sk_X509_value(intermediates, i))) {
      return absl::InvalidArgumentError("failed to convert client certificate chain for QUIC");
    }
  }

  std::vector<CRYPTO_BUFFER*> raw_chain;
  raw_chain.reserve(chain.size());
  for (const auto& cert : chain) {
    raw_chain.push_back(cert.get());
  }
  if (SSL_CTX_set_chain_and_key(quic_ssl_ctx, raw_chain.data(), raw_chain.size(), private_key,
                                nullptr) != 1) {
    return absl::InvalidArgumentError("failed to install client certificate chain for QUIC");
  }
  return absl::OkStatus();
}

} // namespace

absl::StatusOr<std::unique_ptr<QuicClientTransportSocketFactory>>
QuicClientTransportSocketFactory::create(
    Ssl::ClientContextConfigPtr config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  if (config->tlsCertificateSelectorFactory()) {
    return absl::UnimplementedError("Client certificate selector not supported on QUIC");
  }
  absl::Status creation_status = absl::OkStatus();
  auto factory = std::unique_ptr<QuicClientTransportSocketFactory>(
      new QuicClientTransportSocketFactory(std::move(config), context, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  factory->initialize();
  return factory;
}

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
QuicClientTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  auto quic_transport = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport&>(
      config, context.messageValidationVisitor());
  absl::StatusOr<std::unique_ptr<Extensions::TransportSockets::Tls::ClientContextConfigImpl>>
      client_config_or_error = Extensions::TransportSockets::Tls::ClientContextConfigImpl::create(
          quic_transport.upstream_tls_context(), context);
  RETURN_IF_NOT_OK(client_config_or_error.status());
  return QuicClientTransportSocketFactory::create(std::move(*client_config_or_error), context);
}

QuicClientTransportSocketFactory::QuicClientTransportSocketFactory(
    Ssl::ClientContextConfigPtr config,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    absl::Status& creation_status)
    : QuicTransportSocketFactoryBase(factory_context.statsScope(), "client"),
      tls_slot_(factory_context.serverFactoryContext().threadLocal()) {
  auto factory_or_error = Extensions::TransportSockets::Tls::ClientSslSocketFactory::create(
      std::move(config), factory_context.serverFactoryContext().sslContextManager(),
      factory_context.statsScope());
  SET_AND_RETURN_IF_NOT_OK(factory_or_error.status(), creation_status);
  fallback_factory_ = std::move(*factory_or_error);
  tls_slot_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalQuicConfig>(); });
}

void QuicClientTransportSocketFactory::initialize() {
  if (!fallback_factory_->clientContextConfig()->alpnProtocols().empty()) {
    supported_alpns_ =
        absl::StrSplit(fallback_factory_->clientContextConfig()->alpnProtocols(), ',');
  }
}

ProtobufTypes::MessagePtr QuicClientTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport>();
}

std::shared_ptr<quic::QuicCryptoClientConfig> QuicClientTransportSocketFactory::getCryptoConfig() {
  Envoy::Ssl::ClientContextSharedPtr context = sslCtx();
  // If the secrets haven't been loaded, there is no crypto config.
  if (context == nullptr) {
    ENVOY_LOG(warn, "SDS hasn't finished updating Ssl context config yet.");
    stats_.upstream_context_secrets_not_ready_.inc();
    return nullptr;
  }

  ASSERT(tls_slot_.currentThreadRegistered());
  ThreadLocalQuicConfig& tls_config = *tls_slot_;

  if (tls_config.client_context_ != context) {
    bool accept_untrusted =
        clientContextConfig() && clientContextConfig()->certificateValidationContext() &&
        clientContextConfig()->certificateValidationContext()->trustChainVerification() ==
            envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
                ACCEPT_UNTRUSTED;
    // If the context has been updated, update the crypto config.
    tls_config.client_context_ = context;
    tls_config.crypto_config_ = std::make_shared<quic::QuicCryptoClientConfig>(
        std::make_unique<Quic::EnvoyQuicProofVerifier>(std::move(context), accept_untrusted),
        std::make_unique<quic::QuicClientSessionCache>());

    registerCertCompression(tls_config.crypto_config_->ssl_ctx());

    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.quic_upstream_client_certificates")) {
      // The cert selector is rejected at config load time for QUIC, so a production context is
      // always a plain ClientContextImpl with at most one TLS certificate. The downcast can only
      // fail for mock contexts in tests.
      auto* client_context_impl =
          dynamic_cast<Extensions::TransportSockets::Tls::ClientContextImpl*>(
              tls_config.client_context_.get());
      if (client_context_impl != nullptr && !client_context_impl->getTlsContexts().empty()) {
        absl::Status status = configureQuicClientCertChain(
            tls_config.crypto_config_->ssl_ctx(), client_context_impl->getTlsContexts()[0]);
        if (!status.ok()) {
          ENVOY_LOG(warn, "Not sending client certificates on QUIC connections: {}",
                    status.message());
        }
      }
    }
  }
  // Return the latest crypto config.
  return tls_config.crypto_config_;
}

REGISTER_FACTORY(QuicClientTransportSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace Quic
} // namespace Envoy

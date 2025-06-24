#include "source/common/quic/quic_client_transport_socket_factory.h"

#include <memory>

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.validate.h"

#include "source/common/common/logger.h"
#include "source/common/quic/cert_compression.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tls/client_context_impl.h"
#include "source/common/tls/context_config_impl.h"

#include "quiche/quic/core/crypto/quic_client_session_cache.h"

namespace Envoy {
namespace Quic {

namespace {
class QuicClientCertInitializer : public Logger::Loggable<Logger::Id::quic> {
public:
  static absl::Status
  initializeQuicClientCertAndKey(SSL_CTX* quic_ssl_ctx,
                                 const std::vector<Ssl::TlsContext>& tls_contexts) {
    QuicClientCertInitializer initializer;
    return initializer.initializeInternal(quic_ssl_ctx, tls_contexts);
  }

private:
  static absl::Status initializeInternal(SSL_CTX* quic_ssl_ctx,
                                         const std::vector<Ssl::TlsContext>& tls_contexts) {
    if (tls_contexts.empty()) {
      return absl::OkStatus(); // No client certificates configured
    }

    const auto& first_ctx = tls_contexts[0];

    // Load the client certificate chain.
    if (first_ctx.cert_chain_ != nullptr) {
      std::vector<std::string> chain;

      auto process_one_cert = [&](X509* cert) {
        const bssl::UniquePtr<BIO> bio(BIO_new(BIO_s_mem()));
        int result = PEM_write_bio_X509(bio.get(), cert);
        if (result != 1) {
          return absl::InvalidArgumentError("failed to write certificate to BIO.");
        }
        BUF_MEM* buf_mem = nullptr;
        result = BIO_get_mem_ptr(bio.get(), &buf_mem);
        if (result != 1) {
          return absl::InvalidArgumentError("failed to get BIO memory pointer.");
        }
        std::string cert_str(buf_mem->data, buf_mem->length);
        std::istringstream pem_stream(cert_str);
        auto pem_result = quic::ReadNextPemMessage(&pem_stream);
        if (pem_result.status != quic::PemReadResult::Status::kOk) {
          return absl::InvalidArgumentError(
              "error loading certificate in QUIC context: error from ReadNextPemMessage.");
        }
        chain.push_back(std::move(pem_result.contents));
        return absl::OkStatus();
      };

      RETURN_IF_NOT_OK(process_one_cert(first_ctx.cert_chain_.get()));

      // Get the certificate chain from the SSL context.
      STACK_OF(X509)* chain_stack = nullptr;
      int result = SSL_CTX_get0_chain_certs(first_ctx.ssl_ctx_.get(), &chain_stack);
      if (result == 1 && chain_stack != nullptr) {
        for (size_t i = 0; i < sk_X509_num(chain_stack); i++) {
          RETURN_IF_NOT_OK(process_one_cert(sk_X509_value(chain_stack, i)));
        }
      }

      // Convert to QUIC format and set it on the SSL context.
      if (!chain.empty()) {
        // NOTE: For QUIC clients, we need to set the certificate in ``CRYPTO_BUFFER`` format which
        // is expected by the QUIC handshake.

        // Convert first certificate to ``CRYPTO_BUFFER`` format.
        const std::string& cert_data = chain[0];
        bssl::UniquePtr<CRYPTO_BUFFER> cert_buffer(CRYPTO_BUFFER_new(
            reinterpret_cast<const uint8_t*>(cert_data.data()), cert_data.size(), nullptr));

        if (cert_buffer) {
          // Create a ``CRYPTO_BUFFER`` stack for the certificate chain.
          const bssl::UniquePtr<STACK_OF(CRYPTO_BUFFER)> cert_chain_stack(
              sk_CRYPTO_BUFFER_new_null());
          if (cert_chain_stack &&
              bssl::PushToStack(cert_chain_stack.get(), std::move(cert_buffer))) {

            // Add additional certificates if they are present.
            for (size_t i = 1; i < chain.size(); i++) {
              bssl::UniquePtr<CRYPTO_BUFFER> additional_cert(CRYPTO_BUFFER_new(
                  reinterpret_cast<const uint8_t*>(chain[i].data()), chain[i].size(), nullptr));
              if (additional_cert) {
                bssl::PushToStack(cert_chain_stack.get(), std::move(additional_cert));
              }
            }

            // Set the certificate chain on the SSL context.
            const size_t cert_count = sk_CRYPTO_BUFFER_num(cert_chain_stack.get());
            std::vector<CRYPTO_BUFFER*> cert_array(cert_count);
            for (size_t i = 0; i < cert_count; i++) {
              cert_array[i] = sk_CRYPTO_BUFFER_value(cert_chain_stack.get(), i);
            }

            if (SSL_CTX_set_chain_and_key(quic_ssl_ctx, cert_array.data(), cert_count,
                                          SSL_CTX_get0_privatekey(first_ctx.ssl_ctx_.get()),
                                          nullptr) == 1) {
              ENVOY_LOG(debug, "QUIC client: successfully set client certificate chain using "
                               "CRYPTO_BUFFER format");
            } else {
              return absl::InvalidArgumentError("failed to set QUIC client certificate chain.");
            }
          }
        }
      }
    }

    return absl::OkStatus();
  }
};
} // namespace

absl::StatusOr<std::unique_ptr<QuicClientTransportSocketFactory>>
QuicClientTransportSocketFactory::create(
    Ssl::ClientContextConfigPtr config,
    Server::Configuration::TransportSocketFactoryContext& context) {
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

    // Get the SSL context for QUIC client configuration
    SSL_CTX* quic_ssl_ctx = tls_config.crypto_config_->ssl_ctx();
    ENVOY_LOG(debug, "QUIC client: configuring SSL context with client certificates");

    CertCompression::registerSslContext(quic_ssl_ctx);

    // Configure client certificates if they are configured in the TLS context
    // This ensures that the QUIC client can present client certificates during mTLS handshakes
    if (clientContextConfig() && clientContextConfig()->tlsCertificates().size() > 0) {
      ENVOY_LOG(debug, "QUIC client: found {} client certificate(s) to configure",
                clientContextConfig()->tlsCertificates().size());

      // Get the main client context to copy certificates from
      auto client_context_impl =
          std::dynamic_pointer_cast<Extensions::TransportSockets::Tls::ClientContextImpl>(
              tls_config.client_context_);

      if (client_context_impl != nullptr) {
        ENVOY_LOG(debug, "QUIC client: copying client certificates to QUIC SSL context");

        // For QUIC, client certificates need to be loaded in CRYPTO_BUFFER format
        // similar to how the server side uses initializeQuicCertAndKey

        ENVOY_LOG(debug, "QUIC client: loading client certificates in QUIC-compatible format");

        // Initialize client certificates using QUIC-compatible format
        auto status = QuicClientCertInitializer::initializeQuicClientCertAndKey(
            quic_ssl_ctx, client_context_impl->getTlsContexts());
        if (!status.ok()) {
          ENVOY_LOG(warn, "QUIC client: failed to initialize client certificates: {}",
                    status.message());
        } else {
          ENVOY_LOG(debug, "QUIC client: successfully initialized client certificates for QUIC");
        }
      } else {
        ENVOY_LOG(warn,
                  "QUIC client: could not cast to ClientContextImpl to configure certificates");
      }
    } else {
      ENVOY_LOG(debug, "QUIC client: no client certificates configured");
    }
  }
  // Return the latest crypto config.
  return tls_config.crypto_config_;
}

REGISTER_FACTORY(QuicClientTransportSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace Quic
} // namespace Envoy

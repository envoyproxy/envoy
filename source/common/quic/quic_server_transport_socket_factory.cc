#include "source/common/quic/quic_server_transport_socket_factory.h"

#include <memory>

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.validate.h"

#include "source/common/runtime/runtime_features.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"

namespace Envoy {
namespace Quic {

Network::DownstreamTransportSocketFactoryPtr
QuicServerTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>& server_names) {
  auto quic_transport = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport&>(
      config, context.messageValidationVisitor());
  auto server_config = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
      quic_transport.downstream_tls_context(), context);
  // TODO(RyanTheOptimist): support TLS client authentication.
  if (server_config->requireClientCertificate()) {
    throw EnvoyException("TLS Client Authentication is not supported over QUIC");
  }

  auto factory = std::make_unique<QuicServerTransportSocketFactory>(
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(quic_transport, enable_early_data, true),
      context.statsScope(), std::move(server_config), context.sslContextManager(), server_names);
  factory->initialize();
  return factory;
}

namespace {
void initializeQuicCertAndKey(Ssl::TlsContext& context,
                              const Ssl::TlsCertificateConfig& cert_config) {
  const std::string& chain_str = cert_config.certificateChain();
  std::stringstream pem_stream(chain_str);
  std::vector<std::string> chain = quic::CertificateView::LoadPemFromStream(&pem_stream);

  quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain> cert_chain(
      new quic::ProofSource::Chain(chain));
  std::string error_details;
  bssl::UniquePtr<X509> cert = parseDERCertificate(cert_chain->certs[0], &error_details);
  if (cert == nullptr) {
    throwEnvoyExceptionOrPanic(absl::StrCat("Invalid leaf cert: ", error_details));
  }

  bssl::UniquePtr<EVP_PKEY> pub_key(X509_get_pubkey(cert.get()));
  int sign_alg = deduceSignatureAlgorithmFromPublicKey(pub_key.get(), &error_details);
  if (sign_alg == 0) {
    throwEnvoyExceptionOrPanic(
        absl::StrCat("Failed to deduce signature algorithm from public key: ", error_details));
  }

  context.quic_cert_ = std::move(cert_chain);

  const std::string& pkey = cert_config.privateKey();
  std::stringstream pem_str(pkey);
  std::unique_ptr<quic::CertificatePrivateKey> pem_key =
      quic::CertificatePrivateKey::LoadPemFromStream(&pem_str);
  if (pem_key == nullptr) {
    throwEnvoyExceptionOrPanic("Failed to load QUIC private key.");
  }

  context.quic_private_key_ = std::move(pem_key);
}
} // namespace

QuicServerTransportSocketFactory::QuicServerTransportSocketFactory(
    bool enable_early_data, Stats::Scope& scope, Ssl::ServerContextConfigPtr config,
    Envoy::Ssl::ContextManager& manager, const std::vector<std::string>& server_names)
    : QuicTransportSocketFactoryBase(scope, "server"), manager_(manager), stats_scope_(scope),
      config_(std::move(config)), server_names_(server_names),
      ssl_ctx_(manager_.createSslServerContext(stats_scope_, *config_, server_names_,
                                               initializeQuicCertAndKey)),
      enable_early_data_(enable_early_data) {}

QuicServerTransportSocketFactory::~QuicServerTransportSocketFactory() {
  manager_.removeContext(ssl_ctx_);
}

ProtobufTypes::MessagePtr QuicServerTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport>();
}

void QuicServerTransportSocketFactory::initialize() {
  config_->setSecretUpdateCallback([this]() {
    // The callback also updates config_ with the new secret.
    onSecretUpdated();
  });
  if (!config_->alpnProtocols().empty()) {
    supported_alpns_ = absl::StrSplit(config_->alpnProtocols(), ',');
  }
}

std::pair<quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain>,
          std::shared_ptr<quic::CertificatePrivateKey>>
QuicServerTransportSocketFactory::getTlsCertificateAndKey(absl::string_view sni,
                                                          bool* cert_matched_sni) const {
  // onSecretUpdated() could be invoked in the middle of checking the existence of , and using,
  // ssl_ctx. Capture ssl_ctx_ into a local variable so that we check and use the same ssl_ctx.
  Envoy::Ssl::ServerContextSharedPtr ssl_ctx;
  {
    absl::ReaderMutexLock l(&ssl_ctx_mu_);
    ssl_ctx = ssl_ctx_;
  }
  if (!ssl_ctx) {
    ENVOY_LOG(warn, "SDS hasn't finished updating Ssl context config yet.");
    stats_.downstream_context_secrets_not_ready_.inc();
    return {};
  }
  auto ctx =
      std::dynamic_pointer_cast<Extensions::TransportSockets::Tls::ServerContextImpl>(ssl_ctx);
  auto [tls_context, ocsp_staple_action] = ctx->findTlsContext(
      sni, true /* TODO: ecdsa_capable */, false /* TODO: ocsp_capable */, cert_matched_sni);

  // Thread safety note: accessing the tls_context requires holding a shared_ptr to the ``ssl_ctx``.
  // Both of these members are themselves refcounted, so it is safe to use them after ``ssl_ctx``
  // goes out of scope after the function returns.
  return {tls_context.quic_cert_, tls_context.quic_private_key_};
}

void QuicServerTransportSocketFactory::onSecretUpdated() {
  ENVOY_LOG(debug, "Secret is updated.");
  auto ctx = manager_.createSslServerContext(stats_scope_, *config_, server_names_,
                                             initializeQuicCertAndKey);
  {
    absl::WriterMutexLock l(&ssl_ctx_mu_);
    std::swap(ctx, ssl_ctx_);
  }
  manager_.removeContext(ctx);

  stats_.context_config_update_by_sds_.inc();
}

REGISTER_FACTORY(QuicServerTransportSocketConfigFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory);

} // namespace Quic
} // namespace Envoy

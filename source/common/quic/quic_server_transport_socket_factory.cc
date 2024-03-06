#include "source/common/quic/quic_server_transport_socket_factory.h"

#include <memory>

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.validate.h"

#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tls/context_config_impl.h"

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
                              const Ssl::TlsCertificateConfig& /*cert_config*/) {
  // Convert the certificate chain loaded into the context into PEM, as that is what the QUICHE
  // API expects. By using the version already loaded, instead of loading it from the source,
  // we can reuse all the code that loads from different formats, allows using passwords on the key,
  // etc.
  std::vector<std::string> chain;
  auto process_one_cert = [&](X509* cert) {
    bssl::UniquePtr<BIO> bio(BIO_new(BIO_s_mem()));
    int result = PEM_write_bio_X509(bio.get(), cert);
    ASSERT(result == 1);
    BUF_MEM* buf_mem = nullptr;
    result = BIO_get_mem_ptr(bio.get(), &buf_mem);
    std::string cert_str(buf_mem->data, buf_mem->length);
    std::istringstream pem_stream(cert_str);
    auto pem_result = quic::ReadNextPemMessage(&pem_stream);
    if (pem_result.status != quic::PemReadResult::Status::kOk) {
      throwEnvoyExceptionOrPanic(
          "Error loading certificate in QUIC context: error from ReadNextPemMessage");
    }
    chain.push_back(std::move(pem_result.contents));
  };

  process_one_cert(SSL_CTX_get0_certificate(context.ssl_ctx_.get()));

  STACK_OF(X509)* chain_stack = nullptr;
  int result = SSL_CTX_get0_chain_certs(context.ssl_ctx_.get(), &chain_stack);
  ASSERT(result == 1);
  for (size_t i = 0; i < sk_X509_num(chain_stack); i++) {
    process_one_cert(sk_X509_value(chain_stack, i));
  }

  quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain> cert_chain(
      new quic::ProofSource::Chain(chain));

  std::string error_details;
  bssl::UniquePtr<EVP_PKEY> pub_key(X509_get_pubkey(context.cert_chain_.get()));
  int sign_alg = deduceSignatureAlgorithmFromPublicKey(pub_key.get(), &error_details);
  if (sign_alg == 0) {
    throwEnvoyExceptionOrPanic(
        absl::StrCat("Failed to deduce signature algorithm from public key: ", error_details));
  }

  context.quic_cert_ = std::move(cert_chain);

  bssl::UniquePtr<EVP_PKEY> privateKey(
      bssl::UpRef(SSL_CTX_get0_privatekey(context.ssl_ctx_.get())));
  std::unique_ptr<quic::CertificatePrivateKey> pem_key =
      std::make_unique<quic::CertificatePrivateKey>(std::move(privateKey));
  if (pem_key == nullptr) {
    throwEnvoyExceptionOrPanic("Failed to load QUIC private key.");
  }

  context.quic_private_key_ = std::move(pem_key);
}
} // namespace

QuicServerTransportSocketFactory::QuicServerTransportSocketFactory(
    bool enable_early_data, Stats::Scope& scope, Ssl::ServerContextConfigPtr config,
    Envoy::Ssl::ContextManager& manager, const std::vector<std::string>& server_names)
    : QuicTransportSocketFactoryBase(scope, "server"),
      handle_certs_with_shared_tls_code_(Runtime::runtimeFeatureEnabled(
          "envoy.restart_features.quic_handle_certs_with_shared_tls_code")),
      manager_(manager), stats_scope_(scope), config_(std::move(config)),
      server_names_(server_names),
      ssl_ctx_(handle_certs_with_shared_tls_code_ ? createSslServerContext() : nullptr),
      enable_early_data_(enable_early_data) {}

QuicServerTransportSocketFactory::~QuicServerTransportSocketFactory() {
  manager_.removeContext(ssl_ctx_);
}

Envoy::Ssl::ServerContextSharedPtr
QuicServerTransportSocketFactory::createSslServerContext() const {
  return manager_.createSslServerContext(stats_scope_, *config_, server_names_,
                                         initializeQuicCertAndKey);
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
    *cert_matched_sni = false;
    return {};
  }
  auto ctx =
      std::dynamic_pointer_cast<Extensions::TransportSockets::Tls::ServerContextImpl>(ssl_ctx);
  auto [tls_context, ocsp_staple_action] = ctx->findTlsContext(
      sni, true /* TODO: ecdsa_capable */, false /* TODO: ocsp_capable */, cert_matched_sni);

  // Thread safety note: accessing the tls_context requires holding a shared_ptr to the ``ssl_ctx``.
  // Both of these members are themselves reference counted, so it is safe to use them after
  // ``ssl_ctx`` goes out of scope after the function returns.
  return {tls_context.quic_cert_, tls_context.quic_private_key_};
}

void QuicServerTransportSocketFactory::onSecretUpdated() {
  ENVOY_LOG(debug, "Secret is updated.");

  if (handle_certs_with_shared_tls_code_) {
    auto ctx = createSslServerContext();
    {
      absl::WriterMutexLock l(&ssl_ctx_mu_);
      std::swap(ctx, ssl_ctx_);
    }
    manager_.removeContext(ctx);
  }

  stats_.context_config_update_by_sds_.inc();
}

REGISTER_FACTORY(QuicServerTransportSocketConfigFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory);

} // namespace Quic
} // namespace Envoy

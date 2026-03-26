#include "source/extensions/transport_sockets/tls/cert_selectors/filter_state/config.h"

#include "envoy/router/string_accessor.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/ssl/tls_certificate_config_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/server/generic_factory_context.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace FilterState {

namespace {
// Well-known filter state keys. Listener filters (including dynamic modules) write PEM-encoded
// certificate data to these keys. The selector reads them during TLS handshake.
constexpr absl::string_view kDefaultCertChainKey = "envoy.tls.certificate.cert_chain";
constexpr absl::string_view kDefaultPrivateKeyKey = "envoy.tls.certificate.private_key";
} // namespace

FilterStateCertSelector::FilterStateCertSelector(
    Ssl::TlsCertificateMapperPtr&& mapper, ThreadLocal::TypedSlot<ThreadLocalCerts>& cert_contexts,
    Stats::Scope& scope, Server::Configuration::ServerFactoryContext& factory_context,
    const Ssl::ServerContextConfig& tls_config, const std::string& cert_chain_key,
    const std::string& private_key_key, uint32_t max_cache_size)
    : mapper_(std::move(mapper)), cert_contexts_(cert_contexts),
      stats_scope_(scope.createScope("")), factory_context_(factory_context),
      tls_config_(tls_config), cert_chain_key_(cert_chain_key), private_key_key_(private_key_key),
      max_cache_size_(max_cache_size) {}

Ssl::SelectionResult
FilterStateCertSelector::selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                          Ssl::CertificateSelectionCallbackPtr /*cb*/) {
  // Derive the certificate name using the mapper (typically SNI).
  const std::string name = mapper_->deriveFromClientHello(ssl_client_hello);
  if (name.empty()) {
    ENVOY_LOG(debug, "filter_state_cert: empty certificate name from mapper");
    return Ssl::SelectionResult{
        .status = Ssl::SelectionResult::SelectionStatus::Failed,
    };
  }

  // Check thread-local cache.
  OptRef<ThreadLocalCerts> tls_certs = cert_contexts_.get();
  if (tls_certs.has_value()) {
    auto it = tls_certs->ctx_by_name_.find(name);
    if (it != tls_certs->ctx_by_name_.end()) {
      ENVOY_LOG(trace, "filter_state_cert: cache hit for '{}'", name);
      auto handle = std::make_shared<Handle>(it->second);
      return Ssl::SelectionResult{
          .status = Ssl::SelectionResult::SelectionStatus::Success,
          .selected_ctx = &it->second->tlsContext(),
          .staple = false,
          .handle = std::move(handle),
      };
    }
  }

  // Read PEM data from connection filter state via SSL ex_data.
  // The chain: SSL* -> ex_data[sslSocketIndex] -> TransportSocketCallbacks*
  //   -> connection() -> streamInfo() -> filterState()
  auto* callbacks = static_cast<Network::TransportSocketCallbacks*>(
      SSL_get_ex_data(ssl_client_hello.ssl, ContextImpl::sslSocketIndex()));
  if (callbacks == nullptr) {
    ENVOY_LOG(debug, "filter_state_cert: no transport socket callbacks on SSL object");
    return Ssl::SelectionResult{
        .status = Ssl::SelectionResult::SelectionStatus::Failed,
    };
  }

  const auto& filter_state = callbacks->connection().streamInfo().filterState();
  const auto* cert_accessor =
      filter_state->getDataReadOnly<Router::StringAccessor>(cert_chain_key_);
  const auto* key_accessor =
      filter_state->getDataReadOnly<Router::StringAccessor>(private_key_key_);

  if (cert_accessor == nullptr || key_accessor == nullptr) {
    ENVOY_LOG(debug, "filter_state_cert: PEM not found in filter state for '{}' (keys: '{}', '{}')",
              name, cert_chain_key_, private_key_key_);
    return Ssl::SelectionResult{
        .status = Ssl::SelectionResult::SelectionStatus::Failed,
    };
  }

  // Create a TLS context from the PEM data.
  auto ctx =
      createContext(std::string(cert_accessor->asString()), std::string(key_accessor->asString()));
  if (ctx == nullptr) {
    ENVOY_LOG(warn, "filter_state_cert: failed to create TLS context for '{}'", name);
    return Ssl::SelectionResult{
        .status = Ssl::SelectionResult::SelectionStatus::Failed,
    };
  }

  // Cache the compiled context.
  if (tls_certs.has_value()) {
    if (max_cache_size_ > 0 && tls_certs->ctx_by_name_.size() >= max_cache_size_) {
      tls_certs->ctx_by_name_.erase(tls_certs->ctx_by_name_.begin());
    }
    tls_certs->ctx_by_name_[name] = ctx;
  }

  ENVOY_LOG(trace, "filter_state_cert: created and cached context for '{}'", name);
  auto handle = std::make_shared<Handle>(ctx);
  return Ssl::SelectionResult{
      .status = Ssl::SelectionResult::SelectionStatus::Success,
      .selected_ctx = &ctx->tlsContext(),
      .staple = false,
      .handle = std::move(handle),
  };
}

DynamicContextConstSharedPtr FilterStateCertSelector::createContext(const std::string& cert_pem,
                                                                    const std::string& key_pem) {
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate cert_proto;
  cert_proto.mutable_certificate_chain()->set_inline_string(cert_pem);
  cert_proto.mutable_private_key()->set_inline_string(key_pem);

  Server::GenericFactoryContextImpl generic_context(factory_context_,
                                                    factory_context_.messageValidationVisitor());
  auto config_or_error = Ssl::TlsCertificateConfigImpl::create(
      cert_proto, generic_context, factory_context_.api(), "filter_state_cert");
  if (!config_or_error.ok()) {
    ENVOY_LOG(warn, "filter_state_cert: invalid PEM: {}", config_or_error.status().message());
    return nullptr;
  }

  absl::Status creation_status = absl::OkStatus();
  auto ctx = std::make_shared<DynamicContext>(*stats_scope_, factory_context_, tls_config_,
                                              *config_or_error, creation_status);
  if (!creation_status.ok()) {
    ENVOY_LOG(warn, "filter_state_cert: context creation failed: {}", creation_status.message());
    return nullptr;
  }

  return ctx;
}

absl::Status FilterStateCertSelectorFactory::onConfigUpdate() {
  // Clear the thread-local cache so new connections pick up updated TLS config
  // (e.g., changed cipher suites or CA certs from the parent context).
  cert_contexts_.runOnAllThreads([](OptRef<ThreadLocalCerts> certs) {
    if (certs.has_value()) {
      certs->ctx_by_name_.clear();
    }
  });
  return absl::OkStatus();
}

Ssl::TlsCertificateSelectorPtr
FilterStateCertSelectorFactory::create(Ssl::TlsCertificateSelectorContext&) {
  return std::make_unique<FilterStateCertSelector>(mapper_factory_(), cert_contexts_, *scope_,
                                                   factory_context_, tls_config_, cert_chain_key_,
                                                   private_key_key_, max_cache_size_);
}

absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
FilterStateCertSelectorConfigFactory::createTlsCertificateSelectorFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ServerContextConfig& tls_config, bool for_quic) {
  if (for_quic) {
    return absl::InvalidArgumentError(
        "filter_state cert selector does not support QUIC listeners.");
  }

  if (!tls_config.disableStatelessSessionResumption() ||
      !tls_config.disableStatefulSessionResumption()) {
    return absl::InvalidArgumentError(
        "Filter state certificates are not compatible with session resumption. "
        "Disable both stateless and stateful session resumption.");
  }

  const ConfigProto& config = MessageUtil::downcastAndValidate<const ConfigProto&>(
      proto_config, factory_context.messageValidationVisitor());

  // Create the certificate mapper factory.
  Ssl::TlsCertificateMapperConfigFactory& mapper_config =
      Config::Utility::getAndCheckFactory<Ssl::TlsCertificateMapperConfigFactory>(
          config.certificate_mapper());
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      config.certificate_mapper().typed_config(), factory_context.messageValidationVisitor(),
      mapper_config);
  auto mapper_factory = mapper_config.createTlsCertificateMapperFactory(*message, factory_context);
  RETURN_IF_NOT_OK(mapper_factory.status());

  std::string cert_chain_key = config.cert_chain_filter_state_key().empty()
                                   ? std::string(kDefaultCertChainKey)
                                   : config.cert_chain_filter_state_key();
  std::string private_key_key = config.private_key_filter_state_key().empty()
                                    ? std::string(kDefaultPrivateKeyKey)
                                    : config.private_key_filter_state_key();

  auto stats_scope = factory_context.scope().createScope("filter_state_cert.");

  ThreadLocal::TypedSlot<ThreadLocalCerts> cert_contexts(
      factory_context.serverFactoryContext().threadLocal());
  cert_contexts.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalCerts>(); });

  return std::make_unique<FilterStateCertSelectorFactory>(
      *std::move(mapper_factory), std::move(cert_contexts), std::move(stats_scope),
      factory_context.serverFactoryContext(), tls_config, cert_chain_key, private_key_key,
      config.max_cache_size());
}

REGISTER_FACTORY(FilterStateCertSelectorConfigFactory, Ssl::TlsCertificateSelectorConfigFactory);

} // namespace FilterState
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

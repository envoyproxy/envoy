#include "source/extensions/certificate_selectors/on_demand/config.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tls/context_impl.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace CertificateSelectors {
namespace OnDemand {

AsyncContextConfig::AsyncContextConfig(absl::string_view cert_name,
                                       Server::Configuration::ServerFactoryContext& factory_context,
                                       const envoy::config::core::v3::ConfigSource& config_source,
                                       OptRef<Init::Manager> init_manager, UpdateCb update_cb)
    : factory_context_(factory_context), cert_name_(cert_name), update_cb_(update_cb),
      cert_provider_(factory_context_.secretManager().findOrCreateTlsCertificateProvider(
          config_source, cert_name_, factory_context_, init_manager)),
      cert_callback_handle_(cert_provider_->addUpdateCallback([this]() { return loadCert(); })) {}

absl::Status AsyncContextConfig::loadCert() {
  // Called on main, possibly during the constructor.
  auto* secret = cert_provider_->secret();
  if (secret != nullptr) {
    Server::GenericFactoryContextImpl generic_context(factory_context_,
                                                      factory_context_.messageValidationVisitor());
    auto config_or_error = Ssl::TlsCertificateConfigImpl::create(
        *secret, generic_context, factory_context_.api(), cert_name_);
    RETURN_IF_NOT_OK(config_or_error.status());
    cert_config_.emplace(*std::move(config_or_error));
    return update_cb_(cert_name_, *cert_config_);
  }
  return absl::OkStatus();
}

const Ssl::TlsContext& AsyncContext::tlsContext() const { return tls_contexts_[0]; }

void Handle::notify(AsyncContextConstSharedPtr cert_ctx) {
  ASSERT(cb_);
  active_context_ = cert_ctx;
  Event::Dispatcher& dispatcher = cb_->dispatcher();
  dispatcher.post([cb = std::move(cb_), cert_ctx] {
    cb->onCertificateSelectionResult(cert_ctx->tlsContext(), false /* TODO(kuat) OCSP */);
  });
  cb_ = nullptr;
}

CertSelectionStats generateCertSelectionStats(Stats::Scope& store) {
  return {ALL_CERT_SELECTION_STATS(POOL_COUNTER(store), POOL_GAUGE(store), POOL_HISTOGRAM(store))};
}

SecretManager::SecretManager(const ConfigProto& config,
                             Server::Configuration::GenericFactoryContext& factory_context,
                             const Ssl::ServerContextConfig& tls_config)
    : stats_scope_(factory_context.scope().createScope("on_demand_secret.")),
      stats_(generateCertSelectionStats(*stats_scope_)),
      factory_context_(factory_context.serverFactoryContext()),
      config_source_(config.config_source()), tls_config_(tls_config),
      cert_contexts_(factory_context_.threadLocal()) {
  cert_contexts_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalCerts>(); });
  for (const auto& name : config.prefetch_names()) {
    addCertificateConfig(name, nullptr, factory_context.initManager());
  }
}

void SecretManager::addCertificateConfig(absl::string_view secret_name, HandleSharedPtr handle,
                                         OptRef<Init::Manager> init_manager) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  CacheEntry& entry = cache_[secret_name];
  if (handle) {
    if (entry.cert_context_) {
      handle->notify(entry.cert_context_);
    } else {
      entry.callbacks_.push_back(handle);
    }
  }

  // Should be last to trigger the callback since constructor can fire the update event.
  if (entry.cert_config_ == nullptr) {
    stats_.certificate_added_.inc();
    entry.cert_config_ = std::make_unique<AsyncContextConfig>(
        secret_name, factory_context_, config_source_, init_manager,
        [this](absl::string_view secret_name, const Ssl::TlsCertificateConfig& cert_config)
            -> absl::Status { return updateCertificate(secret_name, cert_config); });
  }
}

absl::Status SecretManager::updateCertificate(absl::string_view secret_name,
                                              const Ssl::TlsCertificateConfig& cert_config) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  absl::Status creation_status = absl::OkStatus();
  auto cert_context = std::make_shared<AsyncContext>(*stats_scope_, factory_context_, tls_config_,
                                                     cert_config, creation_status);
  RETURN_IF_NOT_OK(creation_status);

  // Update the future lookups and notify pending callbacks.
  setContext(secret_name, cert_context);
  CacheEntry& entry = cache_[secret_name];
  entry.cert_context_ = cert_context;
  for (auto fetch_handle : entry.callbacks_) {
    if (auto handle = fetch_handle.lock(); handle) {
      handle->notify(cert_context);
    }
  }
  entry.callbacks_.clear();
  return absl::OkStatus();
}

HandleSharedPtr SecretManager::fetchCertificate(absl::string_view secret_name,
                                                Ssl::CertificateSelectionCallbackPtr&& cb) {
  HandleSharedPtr handle = std::make_shared<Handle>(std::move(cb));
  factory_context_.mainThreadDispatcher().post(
      [that = shared_from_this(), name = std::string(secret_name), handle]() mutable {
        that->addCertificateConfig(name, handle, {});
      });
  return handle;
}

void SecretManager::setContext(absl::string_view secret_name, AsyncContextConstSharedPtr cert_ctx) {
  cert_contexts_.runOnAllThreads([name = std::string(secret_name),
                                  cert_ctx = std::move(cert_ctx)](OptRef<ThreadLocalCerts> certs) {
    certs->ctx_by_name_[name] = cert_ctx;
  });
}

absl::optional<AsyncContextConstSharedPtr>
SecretManager::getContext(absl::string_view secret_name) const {
  OptRef<ThreadLocalCerts> current = cert_contexts_.get();
  if (current) {
    const auto it = current->ctx_by_name_.find(secret_name);
    if (it != current->ctx_by_name_.end()) {
      return it->second;
    };
  }
  return {};
}

Ssl::SelectionResult AsyncSelector::selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                                     Ssl::CertificateSelectionCallbackPtr cb) {
  const std::string name = mapper_(ssl_client_hello);
  auto current_context = secret_manager_->getContext(name);
  if (current_context) {
    ENVOY_LOG(trace, "Using an existing certificate '{}'", name);
    const Ssl::TlsContext* tls_context = &current_context.value()->tlsContext();
    auto handle = std::make_shared<Handle>(*std::move(current_context));
    return Ssl::SelectionResult{
        .status = Ssl::SelectionResult::SelectionStatus::Success,
        .selected_ctx = tls_context,
        .handle = std::move(handle),
    };
  }
  ENVOY_LOG(trace, "Requesting a certificate '{}'", name);
  return Ssl::SelectionResult{
      .status = Ssl::SelectionResult::SelectionStatus::Pending,
      .handle = secret_manager_->fetchCertificate(name, std::move(cb)),
  };
}

absl::StatusOr<Ssl::TlsCertificateSelectorFactory>
OnDemandTlsCertificateSelectorFactory::createTlsCertificateSelectorFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ServerContextConfig& tls_config, bool for_quic) {
  if (for_quic) {
    return absl::InvalidArgumentError("Does not support QUIC listeners.");
  }
  const ConfigProto& config = MessageUtil::downcastAndValidate<const ConfigProto&>(
      proto_config, factory_context.messageValidationVisitor());
  Ssl::TlsCertificateMapperConfigFactory& mapper_config =
      Config::Utility::getAndCheckFactory<Ssl::TlsCertificateMapperConfigFactory>(
          config.secret_name());
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      config.secret_name().typed_config(), factory_context.messageValidationVisitor(),
      mapper_config);
  auto mapper_factory = mapper_config.createTlsCertificateMapperFactory(*message, factory_context);
  RETURN_IF_NOT_OK(mapper_factory.status());
  // Doing this last since it can kick-start SDS fetches.
  auto secret_manager = std::make_shared<SecretManager>(config, factory_context, tls_config);
  return [mapper = mapper_factory.value(),
          secret_manager](Ssl::TlsCertificateSelectorContext&) mutable {
    return std::make_unique<AsyncSelector>(mapper(), secret_manager);
  };
}

REGISTER_FACTORY(OnDemandTlsCertificateSelectorFactory, Ssl::TlsCertificateSelectorConfigFactory);

absl::StatusOr<Ssl::TlsCertificateMapperFactory>
StaticNameMapperFactory::createTlsCertificateMapperFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context) {
  const StaticNameConfigProto& config =
      MessageUtil::downcastAndValidate<const StaticNameConfigProto&>(
          proto_config, factory_context.messageValidationVisitor());
  return [name = config.name()]() { return [=](const SSL_CLIENT_HELLO&) { return name; }; };
}

REGISTER_FACTORY(StaticNameMapperFactory, Ssl::TlsCertificateMapperConfigFactory);

} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Extensions
} // namespace Envoy

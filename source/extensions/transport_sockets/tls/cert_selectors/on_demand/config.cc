#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/config.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tls/context_impl.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace OnDemand {

AsyncContextConfig::AsyncContextConfig(absl::string_view cert_name,
                                       Server::Configuration::ServerFactoryContext& factory_context,
                                       const envoy::config::core::v3::ConfigSource& config_source,
                                       OptRef<Init::Manager> init_manager, UpdateCb update_cb,
                                       RemoveCb remove_cb)
    : factory_context_(factory_context), cert_name_(cert_name),
      cert_provider_(factory_context_.secretManager().findOrCreateTlsCertificateProvider(
          config_source, cert_name_, factory_context_, init_manager, false)),
      update_cb_(update_cb),
      update_cb_handle_(cert_provider_->addUpdateCallback([this]() { return loadCert(); })),
      remove_cb_(remove_cb), remove_cb_handle_(cert_provider_->addRemoveCallback(
                                 [this]() { return remove_cb_(cert_name_); })) {}

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

const Ssl::TlsContext& ServerAsyncContext::tlsContext() const { return tls_contexts_[0]; }
const Ssl::TlsContext& ClientAsyncContext::tlsContext() const { return tls_contexts_[0]; }

void Handle::notify(AsyncContextConstSharedPtr cert_ctx) {
  ASSERT(cb_);
  bool staple = false;
  if (cert_ctx) {
    active_context_ = cert_ctx;
    staple =
        (ocspStapleAction(active_context_->tlsContext(), client_ocsp_capable_,
                          active_context_->ocspStaplePolicy()) == Ssl::OcspStapleAction::Staple);
  }
  Event::Dispatcher& dispatcher = cb_->dispatcher();
  // TODO: This could benefit from batching events by the dispatcher in the outer loop.
  dispatcher.post([cb = std::move(cb_), cert_ctx, staple] {
    cb->onCertificateSelectionResult(
        makeOptRefFromPtr(cert_ctx ? &cert_ctx->tlsContext() : nullptr), staple);
  });
  cb_ = nullptr;
}

CertSelectionStatsSharedPtr generateCertSelectionStats(Stats::Scope& store) {
  return std::make_shared<CertSelectionStats>(CertSelectionStats{
      ALL_CERT_SELECTION_STATS(POOL_COUNTER(store), POOL_GAUGE(store), POOL_HISTOGRAM(store))});
}

SecretManager::SecretManager(const ConfigProto& config,
                             Server::Configuration::GenericFactoryContext& factory_context,
                             AsyncContextFactory&& context_factory)
    : stats_scope_(factory_context.scope().createScope("on_demand_secret.")),
      stats_(generateCertSelectionStats(*stats_scope_)),
      factory_context_(factory_context.serverFactoryContext()),
      config_source_(config.config_source()), context_factory_(std::move(context_factory)),
      cert_contexts_(factory_context_.threadLocal()) {
  cert_contexts_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalCerts>(); });
  for (const auto& name : config.prefetch_secret_names()) {
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

  // Should be last to trigger the callback since constructor can fire the update event for an
  // existing SDS subscription.
  if (entry.cert_config_ == nullptr) {
    entry.cert_config_ = std::make_unique<AsyncContextConfig>(
        secret_name, factory_context_, config_source_, init_manager,
        [this](absl::string_view secret_name, const Ssl::TlsCertificateConfig& cert_config)
            -> absl::Status { return updateCertificate(secret_name, cert_config); },
        [this](absl::string_view secret_name) -> absl::Status {
          return removeCertificateConfig(secret_name);
        });
    stats_->cert_requested_.inc();
    stats_->cert_active_.inc();
  }
}

absl::Status SecretManager::updateCertificate(absl::string_view secret_name,
                                              const Ssl::TlsCertificateConfig& cert_config) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  absl::Status creation_status = absl::OkStatus();
  auto cert_context =
      context_factory_(*stats_scope_, factory_context_, cert_config, creation_status);
  RETURN_IF_NOT_OK(creation_status);

  // Update the future lookups and notify pending callbacks.
  setContext(secret_name, cert_context);
  CacheEntry& entry = cache_[secret_name];
  entry.cert_context_ = cert_context;
  size_t notify_count = 0;
  for (auto fetch_handle : entry.callbacks_) {
    if (auto handle = fetch_handle.lock(); handle) {
      handle->notify(cert_context);
      notify_count++;
    }
  }
  ENVOY_LOG(trace, "Notified {} pending connections about certificate '{}', out of queued {}",
            notify_count, secret_name, entry.callbacks_.size());
  entry.callbacks_.clear();
  return absl::OkStatus();
}

absl::Status SecretManager::updateAll() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  for (auto& [secret_name, entry] : cache_) {
    const auto& cert_config = entry.cert_config_->certConfig();
    // Refresh only if there is a certificate present and skip notifying.
    if (cert_config) {
      absl::Status creation_status = absl::OkStatus();
      entry.cert_context_ =
          context_factory_(*stats_scope_, factory_context_, *cert_config, creation_status);
      setContext(secret_name, entry.cert_context_);
      RETURN_IF_NOT_OK(creation_status);
    }
  }
  return absl::OkStatus();
}

absl::Status SecretManager::removeCertificateConfig(absl::string_view secret_name) {
  // We cannot remove the subscription caller directly because this is called during a callback
  // which continues later. Instead, we post to the main as a completion.
  factory_context_.mainThreadDispatcher().post(
      [weak_this = std::weak_ptr<SecretManager>(shared_from_this()),
       name = std::string(secret_name)] {
        if (auto that = weak_this.lock(); that) {
          that->doRemoveCertificateConfig(name);
        }
      });
  return absl::OkStatus();
}

void SecretManager::doRemoveCertificateConfig(absl::string_view secret_name) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  auto it = cache_.find(secret_name);
  if (it == cache_.end()) {
    return;
  }
  size_t notify_count = 0;
  for (auto fetch_handle : it->second.callbacks_) {
    if (auto handle = fetch_handle.lock(); handle) {
      handle->notify(nullptr);
      notify_count++;
    }
  }
  cache_.erase(it);
  setContext(secret_name, nullptr);
  stats_->cert_active_.dec();
  ENVOY_LOG(trace, "Removed certificate subscription for '{}', notified {} pending connections",
            secret_name, notify_count);
}

HandleSharedPtr SecretManager::fetchCertificate(absl::string_view secret_name,
                                                Ssl::CertificateSelectionCallbackPtr&& cb,
                                                bool client_ocsp_capable) {
  HandleSharedPtr handle = std::make_shared<Handle>(std::move(cb), client_ocsp_capable);
  // The manager might need to be destroyed after posting from a worker because
  // the filter chain is being removed. Therefore, use a weak_ptr and ignore
  // the request to fetch a secret. Handle can also be destroyed because the
  // underlying connection is reset, and handshake is cancelled.
  factory_context_.mainThreadDispatcher().post(
      [weak_this = std::weak_ptr<SecretManager>(shared_from_this()),
       name = std::string(secret_name), weak_handle = std::weak_ptr<Handle>(handle)]() mutable {
        auto that = weak_this.lock();
        auto handle = weak_handle.lock();
        if (that && handle) {
          that->addCertificateConfig(name, handle, {});
        }
      });
  return handle;
}

void SecretManager::setContext(absl::string_view secret_name, AsyncContextConstSharedPtr cert_ctx) {
  cert_contexts_.runOnAllThreads(
      [name = std::string(secret_name),
       cert_ctx = std::move(cert_ctx)](OptRef<ThreadLocalCerts> certs) {
        if (cert_ctx) {
          certs->ctx_by_name_[name] = cert_ctx;
        } else {
          certs->ctx_by_name_.erase(name);
        }
      },
      [stats_scope = stats_scope_, stats = stats_] { stats->cert_updated_.inc(); });
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

Ssl::SelectionResult
BaseAsyncSelector::doSelectTlsContext(const std::string& name, const bool client_ocsp_capable,
                                      Ssl::CertificateSelectionCallbackPtr cb) {
  auto current_context = secret_manager_->getContext(name);
  if (current_context) {
    ENVOY_LOG(trace, "Using an existing certificate '{}'", name);
    const Ssl::TlsContext* tls_context = &current_context.value()->tlsContext();
    const auto staple_action = ocspStapleAction(*tls_context, client_ocsp_capable,
                                                current_context.value()->ocspStaplePolicy());
    auto handle = std::make_shared<Handle>(*std::move(current_context));
    return Ssl::SelectionResult{
        .status = Ssl::SelectionResult::SelectionStatus::Success,
        .selected_ctx = tls_context,
        .staple = (staple_action == Ssl::OcspStapleAction::Staple),
        .handle = std::move(handle),
    };
  }
  ENVOY_LOG(trace, "Requesting a certificate '{}'", name);
  return Ssl::SelectionResult{
      .status = Ssl::SelectionResult::SelectionStatus::Pending,
      .handle = secret_manager_->fetchCertificate(name, std::move(cb), client_ocsp_capable),
  };
}

Ssl::SelectionResult AsyncSelector::selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                                     Ssl::CertificateSelectionCallbackPtr cb) {
  const std::string name = mapper_->deriveFromClientHello(ssl_client_hello);
  const bool client_ocsp_capable = isClientOcspCapable(ssl_client_hello);
  return doSelectTlsContext(name, client_ocsp_capable, std::move(cb));
}

Ssl::SelectionResult UpstreamAsyncSelector::selectTlsContext(
    const SSL& ssl, const Network::TransportSocketOptionsConstSharedPtr& options,
    Ssl::CertificateSelectionCallbackPtr cb) {
  const std::string name = mapper_->deriveFromServerHello(ssl, options);
  return doSelectTlsContext(name, false, std::move(cb));
}

Ssl::TlsCertificateSelectorPtr
OnDemandTlsCertificateSelectorFactory::create(Ssl::TlsCertificateSelectorContext&) {
  return std::make_unique<AsyncSelector>(mapper_factory_(), secret_manager_);
}

Ssl::UpstreamTlsCertificateSelectorPtr
UpstreamOnDemandTlsCertificateSelectorFactory::createUpstreamTlsCertificateSelector(
    Ssl::TlsCertificateSelectorContext&) {
  return std::make_unique<UpstreamAsyncSelector>(mapper_factory_(), secret_manager_);
}

absl::Status BaseCertificateSelectorFactory::onConfigUpdate() {
  return secret_manager_->updateAll();
}

namespace {
template <typename MapperFactory, typename SelectorFactory>
absl::StatusOr<std::unique_ptr<SelectorFactory>>
createCertificateSelectorFactory(const Protobuf::Message& proto_config,
                                 Server::Configuration::GenericFactoryContext& factory_context,
                                 AsyncContextFactory&& context_factory) {
  const ConfigProto& config = MessageUtil::downcastAndValidate<const ConfigProto&>(
      proto_config, factory_context.messageValidationVisitor());
  MapperFactory& mapper_config =
      Config::Utility::getAndCheckFactory<MapperFactory>(config.certificate_mapper());
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      config.certificate_mapper().typed_config(), factory_context.messageValidationVisitor(),
      mapper_config);
  auto mapper_factory = mapper_config.createTlsCertificateMapperFactory(*message, factory_context);
  RETURN_IF_NOT_OK(mapper_factory.status());
  // Doing this last since it can kick-start SDS fetches.
  // Envoy ensures that per-worker TLS sockets are destroyed before the filter
  // chain holding the TLS socket factory using a completion. This means the
  // TLS context config in the lambda will outlive each AsyncSelector, and it
  // is safe to refer to TLS context config by reference.
  auto secret_manager =
      std::make_shared<SecretManager>(config, factory_context, std::move(context_factory));
  return std::make_unique<SelectorFactory>(*std::move(mapper_factory), std::move(secret_manager));
}
} // namespace

absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
OnDemandTlsCertificateSelectorConfigFactory::createTlsCertificateSelectorFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ServerContextConfig& tls_config, bool for_quic) {
  if (for_quic) {
    return absl::InvalidArgumentError("Does not support QUIC listeners.");
  }
  // Session ID is currently generated from server names and the included TLS
  // certificates in the parent TLS context. It would not be safe to allow
  // resuming with this ID for on-demand TLS certificates which are not present
  // in the parent TLS context.
  if (!tls_config.disableStatelessSessionResumption() ||
      !tls_config.disableStatefulSessionResumption()) {
    return absl::InvalidArgumentError(
        "On demand certificates are not integrated with session resumption support.");
  }
  return createCertificateSelectorFactory<Ssl::TlsCertificateMapperConfigFactory,
                                          OnDemandTlsCertificateSelectorFactory>(
      proto_config, factory_context,
      [&tls_config](Stats::Scope& scope,
                    Server::Configuration::ServerFactoryContext& server_factory_context,
                    const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status) {
        return std::make_shared<ServerAsyncContext>(scope, server_factory_context, tls_config,
                                                    cert_config, creation_status);
      });
}

absl::StatusOr<Ssl::UpstreamTlsCertificateSelectorFactoryPtr>
UpstreamOnDemandTlsCertificateSelectorConfigFactory::createUpstreamTlsCertificateSelectorFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ClientContextConfig& tls_config) {
  return createCertificateSelectorFactory<Ssl::UpstreamTlsCertificateMapperConfigFactory,
                                          UpstreamOnDemandTlsCertificateSelectorFactory>(
      proto_config, factory_context,
      [&tls_config](Stats::Scope& scope,
                    Server::Configuration::ServerFactoryContext& server_factory_context,
                    const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status) {
        return std::make_shared<ClientAsyncContext>(scope, server_factory_context, tls_config,
                                                    cert_config, creation_status);
      });
}

REGISTER_FACTORY(OnDemandTlsCertificateSelectorConfigFactory,
                 Ssl::TlsCertificateSelectorConfigFactory);

REGISTER_FACTORY(UpstreamOnDemandTlsCertificateSelectorConfigFactory,
                 Ssl::UpstreamTlsCertificateSelectorConfigFactory);

} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

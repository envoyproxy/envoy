#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/config.h"

#include "source/common/config/utility.h"
#include "source/common/common/callback_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/certificate_providers/local/local_certificate_provider.h"
#include "source/common/ssl/tls_certificate_config_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/server/generic_factory_context.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "envoy/filesystem/filesystem.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace OnDemand {

namespace {

constexpr uint32_t DefaultLocalCertTtlDays = 30;

bool isValidRsaKeyBits(uint32_t bits) { return bits >= 2048 && bits <= 8192 && bits % 256 == 0; }

std::string localSignerProviderName(const LocalSignerProto& config) {
  return absl::StrCat("on_demand_local_signer_", MessageUtil::hash(config));
}
} // namespace

AsyncContextConfig::AsyncContextConfig(absl::string_view cert_name,
                                       Server::Configuration::ServerFactoryContext& factory_context,
                                       const envoy::config::core::v3::ConfigSource& config_source,
                                       OptRef<Init::Manager> init_manager, UpdateCb update_cb,
                                       RemoveCb remove_cb)
    : AsyncContextConfig(
          cert_name, factory_context,
          factory_context.secretManager().findOrCreateTlsCertificateProvider(
              config_source, std::string(cert_name), factory_context, init_manager, false),
          update_cb, remove_cb) {}

AsyncContextConfig::AsyncContextConfig(absl::string_view cert_name,
                                       Server::Configuration::ServerFactoryContext& factory_context,
                                       absl::string_view provider_name,
                                       OptRef<Init::Manager> init_manager, UpdateCb update_cb,
                                       RemoveCb remove_cb)
    : AsyncContextConfig(
          cert_name, factory_context,
          factory_context.secretManager().findOrCreateTlsCertificateProvider(
              std::string(provider_name), std::string(cert_name), factory_context,
              init_manager),
          update_cb, remove_cb) {}

AsyncContextConfig::AsyncContextConfig(
    absl::string_view cert_name, Server::Configuration::ServerFactoryContext& factory_context,
    Secret::TlsCertificateConfigProviderSharedPtr cert_provider, UpdateCb update_cb,
    RemoveCb remove_cb)
    : factory_context_(factory_context), cert_name_(cert_name),
      cert_provider_(std::move(cert_provider)),
      update_cb_(update_cb),
      update_cb_handle_(cert_provider_ ? cert_provider_->addUpdateCallback(
                                            [this]() { return loadCert(); })
                                       : nullptr),
      remove_cb_(remove_cb),
      remove_cb_handle_(cert_provider_ ? cert_provider_->addRemoveCallback(
                                            [this]() { return remove_cb_(cert_name_); })
                                       : nullptr) {}

absl::Status AsyncContextConfig::loadCert() {
  // Called on main, possibly during the constructor.
  if (cert_provider_ == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("certificate provider not found for certificate '", cert_name_, "'"));
  }
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
      local_signer_enabled_(config.has_local_signer()),
      local_signer_config_(config.has_local_signer() ? config.local_signer()
                                                     : LocalSignerProto()),
      certificate_provider_enabled_(config.has_local_signer() ||
                                    !config.certificate_provider_name().empty()),
      certificate_provider_name_(config.has_local_signer()
                                     ? localSignerProviderName(local_signer_config_)
                                     : config.certificate_provider_name()),
      cert_contexts_(factory_context_.threadLocal()) {
  cert_contexts_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalCerts>(); });
  if (local_signer_enabled_) {
    const uint32_t cert_ttl_days = local_signer_config_.cert_ttl_days() > 0
                                       ? local_signer_config_.cert_ttl_days()
                                       : DefaultLocalCertTtlDays;
    const absl::Status status = factory_context_.secretManager().registerTlsCertificateProvider(
        certificate_provider_name_,
        std::make_shared<
            ::Envoy::Extensions::CertificateProviders::Local::LocalNamedTlsCertificateProvider>(
            local_signer_config_));
    if (!status.ok() && status.code() != absl::StatusCode::kAlreadyExists) {
      throw EnvoyException(
          absl::StrCat("failed to register local signer certificate provider '",
                       certificate_provider_name_, "': ", status.message()));
    }
    ENVOY_LOG(info, "on-demand selector using local signer provider '{}' (cert_ttl_days={})",
              certificate_provider_name_, cert_ttl_days);
  } else if (certificate_provider_enabled_) {
    ENVOY_LOG(info, "on-demand selector using certificate provider '{}'",
              certificate_provider_name_);
  }
  for (const auto& name : config.prefetch_secret_names()) {
    const OptRef<Init::Manager> init_manager =
        certificate_provider_enabled_ ? OptRef<Init::Manager>() : factory_context.initManager();
    addCertificateConfig(name, nullptr, init_manager);
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
    stats_->cert_requested_.inc();
    stats_->cert_active_.inc();
    if (certificate_provider_enabled_) {
      entry.cert_config_ = std::make_unique<AsyncContextConfig>(
          secret_name, factory_context_, certificate_provider_name_, init_manager,
          [this](absl::string_view secret_name, const Ssl::TlsCertificateConfig& cert_config)
              -> absl::Status { return updateCertificate(secret_name, cert_config); },
          [this](absl::string_view secret_name) -> absl::Status {
            return removeCertificateConfig(secret_name);
          });
      if (!entry.cert_config_->hasProvider()) {
        ENVOY_LOG(error,
                  "failed to resolve certificate provider '{}', certificate '{}'",
                  certificate_provider_name_, secret_name);
        for (auto fetch_handle : entry.callbacks_) {
          if (auto cb_handle = fetch_handle.lock(); cb_handle) {
            cb_handle->notify(nullptr);
          }
        }
        entry.callbacks_.clear();
        cache_.erase(std::string(secret_name));
        stats_->cert_active_.dec();
      }
      return;
    }
    entry.cert_config_ = std::make_unique<AsyncContextConfig>(
        secret_name, factory_context_, config_source_, init_manager,
        [this](absl::string_view secret_name, const Ssl::TlsCertificateConfig& cert_config)
            -> absl::Status { return updateCertificate(secret_name, cert_config); },
        [this](absl::string_view secret_name) -> absl::Status {
          return removeCertificateConfig(secret_name);
        });
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
  if (local_signer_enabled_) {
    return ::Envoy::Extensions::CertificateProviders::Local::refreshLocalSignerCertificateProviders(
        local_signer_config_);
  }
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
  const bool has_config_source = config.has_config_source();
  const bool has_local_signer = config.has_local_signer();
  const bool has_certificate_provider_name = !config.certificate_provider_name().empty();
  if (!has_config_source && !has_local_signer && !has_certificate_provider_name) {
    return absl::InvalidArgumentError(
        "one of config_source, local_signer, or certificate_provider_name must be configured");
  }
  if (has_local_signer && has_certificate_provider_name) {
    return absl::InvalidArgumentError(
        "local_signer and certificate_provider_name are mutually exclusive");
  }
  if (config.has_local_signer()) {
    if (config.local_signer().ca_cert_path().empty() || config.local_signer().ca_key_path().empty()) {
      return absl::InvalidArgumentError("local_signer requires both ca_cert_path and ca_key_path");
    }
    const auto key_type = config.local_signer().key_type();
    if (key_type != LocalSignerProto::KEY_TYPE_UNSPECIFIED &&
        key_type != LocalSignerProto::KEY_TYPE_RSA &&
        key_type != LocalSignerProto::KEY_TYPE_ECDSA) {
      return absl::InvalidArgumentError("unsupported local_signer.key_type");
    }
    if (config.local_signer().rsa_key_bits() > 0) {
      const uint32_t bits = config.local_signer().rsa_key_bits();
      if (!isValidRsaKeyBits(bits)) {
        return absl::InvalidArgumentError(
            "local_signer.rsa_key_bits must be a multiple of 256 in [2048, 8192]");
      }
    }
    const auto ecdsa_curve = config.local_signer().ecdsa_curve();
    if (ecdsa_curve != LocalSignerProto::ECDSA_CURVE_UNSPECIFIED &&
        ecdsa_curve != LocalSignerProto::ECDSA_CURVE_P256 &&
        ecdsa_curve != LocalSignerProto::ECDSA_CURVE_P384) {
      return absl::InvalidArgumentError("unsupported local_signer.ecdsa_curve");
    }
    const auto signature_hash = config.local_signer().signature_hash();
    if (signature_hash != LocalSignerProto::SIGNATURE_HASH_UNSPECIFIED &&
        signature_hash != LocalSignerProto::SIGNATURE_HASH_SHA256 &&
        signature_hash != LocalSignerProto::SIGNATURE_HASH_SHA384 &&
        signature_hash != LocalSignerProto::SIGNATURE_HASH_SHA512) {
      return absl::InvalidArgumentError("unsupported local_signer.signature_hash");
    }
    const auto hostname_validation = config.local_signer().hostname_validation();
    if (hostname_validation != LocalSignerProto::HOSTNAME_VALIDATION_UNSPECIFIED &&
        hostname_validation != LocalSignerProto::HOSTNAME_VALIDATION_PERMISSIVE &&
        hostname_validation != LocalSignerProto::HOSTNAME_VALIDATION_STRICT) {
      return absl::InvalidArgumentError("unsupported local_signer.hostname_validation");
    }
    const auto ca_reload_failure_policy = config.local_signer().ca_reload_failure_policy();
    if (ca_reload_failure_policy !=
            LocalSignerProto::CA_RELOAD_FAILURE_POLICY_UNSPECIFIED &&
        ca_reload_failure_policy !=
            LocalSignerProto::CA_RELOAD_FAILURE_POLICY_FAIL_CLOSED &&
        ca_reload_failure_policy !=
            LocalSignerProto::CA_RELOAD_FAILURE_POLICY_FAIL_OPEN) {
      return absl::InvalidArgumentError("unsupported local_signer.ca_reload_failure_policy");
    }
    for (const auto& dns_san : config.local_signer().additional_dns_sans()) {
      if (dns_san.empty()) {
        return absl::InvalidArgumentError("local_signer.additional_dns_sans cannot contain empty");
      }
    }
    for (const auto key_usage : config.local_signer().key_usages()) {
      if (key_usage != LocalSignerProto::KEY_USAGE_UNSPECIFIED &&
          key_usage != LocalSignerProto::KEY_USAGE_DIGITAL_SIGNATURE &&
          key_usage != LocalSignerProto::KEY_USAGE_CONTENT_COMMITMENT &&
          key_usage != LocalSignerProto::KEY_USAGE_KEY_ENCIPHERMENT &&
          key_usage != LocalSignerProto::KEY_USAGE_DATA_ENCIPHERMENT &&
          key_usage != LocalSignerProto::KEY_USAGE_KEY_AGREEMENT &&
          key_usage != LocalSignerProto::KEY_USAGE_KEY_CERT_SIGN &&
          key_usage != LocalSignerProto::KEY_USAGE_CRL_SIGN) {
        return absl::InvalidArgumentError("unsupported local_signer.key_usages value");
      }
    }
    for (const auto extended_key_usage : config.local_signer().extended_key_usages()) {
      if (extended_key_usage != LocalSignerProto::EXTENDED_KEY_USAGE_UNSPECIFIED &&
          extended_key_usage != LocalSignerProto::EXTENDED_KEY_USAGE_SERVER_AUTH &&
          extended_key_usage != LocalSignerProto::EXTENDED_KEY_USAGE_CLIENT_AUTH &&
          extended_key_usage != LocalSignerProto::EXTENDED_KEY_USAGE_CODE_SIGNING &&
          extended_key_usage != LocalSignerProto::EXTENDED_KEY_USAGE_EMAIL_PROTECTION &&
          extended_key_usage != LocalSignerProto::EXTENDED_KEY_USAGE_TIME_STAMPING &&
          extended_key_usage != LocalSignerProto::EXTENDED_KEY_USAGE_OCSP_SIGNING) {
        return absl::InvalidArgumentError("unsupported local_signer.extended_key_usages value");
      }
    }
  }
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

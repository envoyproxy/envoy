#pragma once

#include <functional>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/api/v2/core/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/init/init.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_callbacks.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/callback_impl.h"
#include "common/common/cleanup.h"
#include "common/ssl/certificate_validation_context_config_impl.h"
#include "common/ssl/tls_certificate_config_impl.h"
#include "common/ssl/trusted_ca_config_impl.h"

namespace Envoy {
namespace Secret {

/**
 * SDS API implementation that fetches secrets from SDS server via Subscription.
 */
class SdsApi : public Init::Target,
               public Config::SubscriptionCallbacks<envoy::api::v2::auth::Secret> {
public:
  SdsApi(const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
         Runtime::RandomGenerator& random, Stats::Store& stats,
         Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
         const envoy::api::v2::core::ConfigSource& sds_config, const std::string& sds_config_name,
         std::function<void()> destructor_cb);

  // Init::Target
  void initialize(std::function<void()> callback) override;

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources, const std::string& version_info) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::auth::Secret>(resource).name();
  }

protected:
  // Checks secret and secret hash. Returns true if secret and hash are okay to use.
  virtual bool checkSecret(const envoy::api::v2::auth::Secret&, const uint64_t hash);
  // Creates new secret.
  virtual void setSecret(const envoy::api::v2::auth::Secret&) PURE;
  Common::CallbackManager<> update_callback_manager_;

private:
  void runInitializeCallbackIfAny();

  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;
  Runtime::RandomGenerator& random_;
  Stats::Store& stats_;
  Upstream::ClusterManager& cluster_manager_;

  const envoy::api::v2::core::ConfigSource sds_config_;
  std::unique_ptr<Config::Subscription<envoy::api::v2::auth::Secret>> subscription_;
  std::function<void()> initialize_callback_;
  const std::string sds_config_name_;

  uint64_t secret_hash_;
  Cleanup clean_up_;
};

class TlsCertificateSdsApi;
class CertificateValidationContextSdsApi;
class TrustedCaSdsApi;
typedef std::shared_ptr<TlsCertificateSdsApi> TlsCertificateSdsApiSharedPtr;
typedef std::shared_ptr<CertificateValidationContextSdsApi>
    CertificateValidationContextSdsApiSharedPtr;
typedef std::shared_ptr<TrustedCaSdsApi> TrustedCaSdsApiSharedPtr;    

/**
 * TlsCertificateSdsApi implementation maintains and updates dynamic TLS certificate secrets.
 */
class TlsCertificateSdsApi : public SdsApi, public TlsCertificateConfigProvider {
public:
  static TlsCertificateSdsApiSharedPtr
  create(Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
         const envoy::api::v2::core::ConfigSource& sds_config, const std::string& sds_config_name,
         std::function<void()> destructor_cb) {
    return std::make_shared<TlsCertificateSdsApi>(
        secret_provider_context.localInfo(), secret_provider_context.dispatcher(),
        secret_provider_context.random(), secret_provider_context.stats(),
        secret_provider_context.clusterManager(), *secret_provider_context.initManager(),
        sds_config, sds_config_name, destructor_cb);
  }

  TlsCertificateSdsApi(const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
                       Runtime::RandomGenerator& random, Stats::Store& stats,
                       Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
                       const envoy::api::v2::core::ConfigSource& sds_config,
                       const std::string& sds_config_name, std::function<void()> destructor_cb)
      : SdsApi(local_info, dispatcher, random, stats, cluster_manager, init_manager, sds_config,
               sds_config_name, destructor_cb) {}

  // SecretProvider
  const Ssl::TlsCertificateConfig* secret() const override {
    return tls_certificate_secrets_.get();
  }
  Common::CallbackHandle* addUpdateCallback(std::function<void()> callback) override {
    return update_callback_manager_.add(callback);
  }

protected:
  void setSecret(const envoy::api::v2::auth::Secret& secret) override {
    tls_certificate_secrets_ =
        std::make_unique<Ssl::TlsCertificateConfigImpl>(secret.tls_certificate());
  }

private:
  Ssl::TlsCertificateConfigPtr tls_certificate_secrets_;
};

/**
 * CertificateValidationContextSdsApi implementation maintains and updates dynamic certificate
 * validation context secrets.
 */
class CertificateValidationContextSdsApi : public SdsApi,
                                           public CertificateValidationContextConfigProvider {
public:
  static CertificateValidationContextSdsApiSharedPtr
  create(Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
         const envoy::api::v2::core::ConfigSource& sds_config, const std::string& sds_config_name,
         std::function<void()> destructor_cb) {
    return std::make_shared<CertificateValidationContextSdsApi>(
        secret_provider_context.localInfo(), secret_provider_context.dispatcher(),
        secret_provider_context.random(), secret_provider_context.stats(),
        secret_provider_context.clusterManager(), *secret_provider_context.initManager(),
        sds_config, sds_config_name, destructor_cb);
  }
  CertificateValidationContextSdsApi(const LocalInfo::LocalInfo& local_info,
                                     Event::Dispatcher& dispatcher,
                                     Runtime::RandomGenerator& random, Stats::Store& stats,
                                     Upstream::ClusterManager& cluster_manager,
                                     Init::Manager& init_manager,
                                     const envoy::api::v2::core::ConfigSource& sds_config,
                                     std::string sds_config_name,
                                     std::function<void()> destructor_cb)
      : SdsApi(local_info, dispatcher, random, stats, cluster_manager, init_manager, sds_config,
               sds_config_name, destructor_cb) {}

  // SecretProvider
  const Ssl::CertificateValidationContextConfig* secret() const override {
    return certificate_validation_context_secrets_.get();
  }
  Common::CallbackHandle* addUpdateCallback(std::function<void()> callback) override {
    return update_callback_manager_.add(callback);
  }

protected:
  bool checkSecret(const envoy::api::v2::auth::Secret& secret, const uint64_t hash) override {
    // Dynamic CertificateValidationContext should not have SDS config for trusted ca.
    if (secret.validation_context().has_trusted_ca_sds_secret_config()) {
      return false;
    }
    return SdsApi::checkSecret(secret, hash);
  }

  void setSecret(const envoy::api::v2::auth::Secret& secret) override {
    certificate_validation_context_secrets_ =
        std::make_unique<Ssl::CertificateValidationContextConfigImpl>(secret.validation_context());
  }

private:
  Ssl::CertificateValidationContextConfigPtr certificate_validation_context_secrets_;
};

/**
 * TrustedCaSdsApi implementation maintains and updates dynamic certificate
 * validation context secrets.
 */
class TrustedCaSdsApi : public SdsApi, public TrustedCaConfigProvider {
public:
  static TrustedCaSdsApiSharedPtr
  create(Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
         const envoy::api::v2::core::ConfigSource& sds_config, const std::string& sds_config_name,
         std::function<void()> destructor_cb) {
    return std::make_shared<TrustedCaSdsApi>(
        secret_provider_context.localInfo(), secret_provider_context.dispatcher(),
        secret_provider_context.random(), secret_provider_context.stats(),
        secret_provider_context.clusterManager(), *secret_provider_context.initManager(),
        sds_config, sds_config_name, destructor_cb);
  }
  TrustedCaSdsApi(const LocalInfo::LocalInfo& local_info,
                  Event::Dispatcher& dispatcher,
                  Runtime::RandomGenerator& random, Stats::Store& stats,
                  Upstream::ClusterManager& cluster_manager,
                  Init::Manager& init_manager,
                  const envoy::api::v2::core::ConfigSource& sds_config,
                  std::string sds_config_name,
                  std::function<void()> destructor_cb)
      : SdsApi(local_info, dispatcher, random, stats, cluster_manager, init_manager, sds_config,
               sds_config_name, destructor_cb) {}

  // SecretProvider
  const Ssl::TrustedCaConfig* secret() const override {
    return trusted_ca_secrets_.get();
  }
  Common::CallbackHandle* addUpdateCallback(std::function<void()> callback) override {
    return update_callback_manager_.add(callback);
  }

protected:
  void setSecret(const envoy::api::v2::auth::Secret& secret) override {
    trusted_ca_secrets_ = 
      std::make_unique<Ssl::TrustedCaConfigImpl>(secret.trusted_ca().trusted_ca());
  }

private:
  Ssl::TrustedCaConfigPtr trusted_ca_secrets_;
};

} // namespace Secret
} // namespace Envoy

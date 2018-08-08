#pragma once

#include <functional>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/api/v2/core/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/init/init.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/dynamic_secret_provider.h"
#include "envoy/secret/secret_callbacks.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Secret {

/**
 * SDS API implementation that fetches secrets from SDS server via Subscription.
 */
class SdsApi : public Init::Target,
               public DynamicTlsCertificateSecretProvider,
               public Config::SubscriptionCallbacks<envoy::api::v2::auth::Secret> {
public:
  SdsApi(const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
         Runtime::RandomGenerator& random, Stats::Store& stats,
         Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
         const envoy::api::v2::core::ConfigSource& sds_config, std::string sds_config_name,
         std::function<void()> unregister_secret_provider);

  ~SdsApi() override;

  // Init::Target
  void initialize(std::function<void()> callback) override;

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources, const std::string& version_info) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::auth::Secret>(resource).name();
  }

  // DynamicTlsCertificateSecretProvider
  const Ssl::TlsCertificateConfig* secret() const override {
    return tls_certificate_secrets_.get();
  }

  void addUpdateCallback(SecretCallbacks& callback) override {
    update_callbacks_.push_back(&callback);
  }
  void removeUpdateCallback(SecretCallbacks& callback) override {
    update_callbacks_.remove(&callback);
  }

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
  std::function<void()> unregister_secret_provider_cb_;
  Ssl::TlsCertificateConfigPtr tls_certificate_secrets_;
  std::list<SecretCallbacks*> update_callbacks_;
};

typedef std::unique_ptr<SdsApi> SdsApiPtr;

} // namespace Secret
} // namespace Envoy
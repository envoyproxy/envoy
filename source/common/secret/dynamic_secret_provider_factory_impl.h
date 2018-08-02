#pragma once

#include <functional>

#include "envoy/init/init.h"
#include "envoy/secret/dynamic_secret_provider.h"
#include "envoy/secret/dynamic_secret_provider_factory.h"
#include "envoy/secret/secret_manager.h"

#include "common/secret/sds_api.h"

namespace Envoy {
namespace Secret {

class DynamicTlsCertificateSecretProviderFactoryContextImpl
    : public DynamicTlsCertificateSecretProviderFactoryContext {
public:
  DynamicTlsCertificateSecretProviderFactoryContextImpl(const LocalInfo::LocalInfo& local_info,
                                                        Event::Dispatcher& dispatcher,
                                                        Runtime::RandomGenerator& random,
                                                        Stats::Store& stats,
                                                        Upstream::ClusterManager& cluster_manager)
      : local_info_(local_info), dispatcher_(dispatcher), random_(random), stats_(stats),
        cluster_manager_(cluster_manager),
        secret_manager_(cluster_manager.clusterManagerFactory().secretManager()) {}

  const LocalInfo::LocalInfo& local_info() override { return local_info_; }

  Event::Dispatcher& dispatcher() override { return dispatcher_; }

  Runtime::RandomGenerator& random() override { return random_; }

  Stats::Store& stats() override { return stats_; }

  Upstream::ClusterManager& cluster_manager() override { return cluster_manager_; }

private:
  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;
  Runtime::RandomGenerator& random_;
  Stats::Store& stats_;
  Upstream::ClusterManager& cluster_manager_;
  Secret::SecretManager& secret_manager_;
};

class DynamicTlsCertificateSecretProviderFactoryImpl
    : public DynamicTlsCertificateSecretProviderFactory {
public:
  DynamicTlsCertificateSecretProviderFactoryImpl(
      DynamicTlsCertificateSecretProviderFactoryContext& context, Init::Manager& init_manager)
      : context_(context), init_manager_(init_manager) {}

  DynamicTlsCertificateSecretProviderSharedPtr
  findOrCreate(const envoy::api::v2::core::ConfigSource& sds_config,
               std::string sds_config_name) override {
    Secret::SecretManager& secret_manager =
        context_.cluster_manager().clusterManagerFactory().secretManager();
    auto secret_provider =
        secret_manager.findDynamicTlsCertificateSecretProvider(sds_config, sds_config_name);
    if (!secret_provider) {
      secret_provider = std::make_shared<Secret::SdsApi>(
          context_.local_info(), context_.dispatcher(), context_.random(), context_.stats(),
          context_.cluster_manager(), init_manager_, sds_config, sds_config_name);
      secret_manager.setDynamicTlsCertificateSecretProvider(sds_config, sds_config_name,
                                                            secret_provider);
    }
    return secret_provider;
  }

private:
  DynamicTlsCertificateSecretProviderFactoryContext& context_;
  Init::Manager& init_manager_;
};

} // namespace Secret
} // namespace Envoy
#pragma once

#include "envoy/config/xds_manager.h"

#include "source/common/common/thread.h"
#include "source/common/config/subscription_factory_impl.h"

namespace Envoy {
namespace Config {

class XdsManagerImpl : public XdsManager {
public:
  XdsManagerImpl(Event::Dispatcher& main_thread_dispatcher, Api::Api& api,
                 const LocalInfo::LocalInfo& local_info,
                 ProtobufMessage::ValidationContext& validation_context, Server::Instance& server)
      : server_(server), main_thread_dispatcher_(main_thread_dispatcher), api_(api),
        local_info_(local_info), validation_context_(validation_context) {}

  // Config::ConfigSourceProvider
  absl::Status initialize(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                          Upstream::ClusterManager* cm) override;
  void shutdown() override {}
  absl::Status
  setAdsConfigSource(const envoy::config::core::v3::ApiConfigSource& config_source) override;
  OptRef<Config::XdsConfigTracker> xdsConfigTracker() override {
    return makeOptRefFromPtr<Config::XdsConfigTracker>(xds_config_tracker_.get());
  }

  XdsResourcesDelegateOptRef xdsResourcesDelegate() override {
    return makeOptRefFromPtr<Config::XdsResourcesDelegate>(xds_resources_delegate_.get());
  }

  SubscriptionFactory& subscriptionFactory() override { return *subscription_factory_; }

private:
  // Validates (syntactically) the config_source by doing the PGV validation.
  absl::Status validateAdsConfig(const envoy::config::core::v3::ApiConfigSource& config_source);

  Server::Instance& server_;
  Event::Dispatcher& main_thread_dispatcher_;
  Api::Api& api_;
  const LocalInfo::LocalInfo& local_info_;
  ProtobufMessage::ValidationContext& validation_context_;
  XdsResourcesDelegatePtr xds_resources_delegate_;
  XdsConfigTrackerPtr xds_config_tracker_;
  std::unique_ptr<SubscriptionFactoryImpl> subscription_factory_;
  // The cm_ will only be valid after the cluster-manager is initialized.
  // Note that this implies that the xDS-manager must be shut down properly
  // prior to the cluster-manager deletion.
  Upstream::ClusterManager* cm_;
};

} // namespace Config
} // namespace Envoy

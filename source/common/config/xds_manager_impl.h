#pragma once

#include "envoy/config/xds_manager.h"

#include "source/common/common/thread.h"

namespace Envoy {
namespace Config {

class XdsManagerImpl : public XdsManager {
public:
  XdsManagerImpl(Event::Dispatcher& main_thread_dispatcher, Api::Api& api,
                 ProtobufMessage::ValidationContext& validation_context)
      : main_thread_dispatcher_(main_thread_dispatcher), api_(api),
        validation_context_(validation_context) {}

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

private:
  // Validates (syntactically) the config_source by doing the PGV validation.
  absl::Status validateAdsConfig(const envoy::config::core::v3::ApiConfigSource& config_source);

  Event::Dispatcher& main_thread_dispatcher_;
  Api::Api& api_;
  ProtobufMessage::ValidationContext& validation_context_;
  XdsResourcesDelegatePtr xds_resources_delegate_;
  Config::XdsConfigTrackerPtr xds_config_tracker_;
  // The cm_ will only be valid after the cluster-manager is initialized.
  // Note that this implies that the xDS-manager must be shut down properly
  // prior to the cluster-manager deletion.
  Upstream::ClusterManager* cm_;
};

} // namespace Config
} // namespace Envoy

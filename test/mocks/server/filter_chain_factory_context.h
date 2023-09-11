#pragma once

#include "envoy/server/filter_config.h"

#include "factory_context.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockFilterChainFactoryContext : public MockFactoryContext, public FilterChainFactoryContext {
public:
  MockFilterChainFactoryContext();
  ~MockFilterChainFactoryContext() override;

  void createDynamicFilterConfigProvider(
      const envoy::config::core::v3::ExtensionConfigSource& config_source,
      const std::string& filter_config_name,
      Server::Configuration::ServerFactoryContext& server_context,
      Server::Configuration::FactoryContext& factory_context,
      Upstream::ClusterManager& cluster_manager, bool last_filter_in_filter_chain,
      const std::string& filter_chain_type,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher) override {}

  OptRef<Http::FilterFactoryCb>
  dynamicProviderConfig(const std::string& filter_config_name) override {
    return absl::nullopt;
  }
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy

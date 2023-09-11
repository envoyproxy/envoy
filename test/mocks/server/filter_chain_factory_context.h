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

  void createDynamicFilterConfigProvider(const envoy::config::core::v3::ExtensionConfigSource&,
                                         const std::string&,
                                         Server::Configuration::ServerFactoryContext&,
                                         Server::Configuration::FactoryContext&,
                                         Upstream::ClusterManager&, bool, const std::string&,
                                         const Network::ListenerFilterMatcherSharedPtr&) override {}

  OptRef<Http::FilterFactoryCb> dynamicProviderConfig(const std::string&) override {
    return absl::nullopt;
  }
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy

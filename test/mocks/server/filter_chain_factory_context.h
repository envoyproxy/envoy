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

  HttpExtensionConfigProvider
  createDynamicFilterConfigProvider(const envoy::config::core::v3::ExtensionConfigSource&,
                                    const std::string&, bool, const std::string&,
                                    const Network::ListenerFilterMatcherSharedPtr&) override {
    return nullptr;
  }
  Configuration::DownstreamFilterConfigProviderManagerPtr
  downstreamFilterConfigProviderManager() override {
    return nullptr;
  }
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy

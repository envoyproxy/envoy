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

  HttpExtensionConfigProviderSharedPtr
  createDynamicFilterConfigProvider(const envoy::config::core::v3::ExtensionConfigSource&,
                                    const std::string&, bool) override {
    return nullptr;
  }
  Configuration::DownstreamFilterConfigProviderManagerSharedPtr
  downstreamFilterConfigProviderManager() override {
    return nullptr;
  }
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy

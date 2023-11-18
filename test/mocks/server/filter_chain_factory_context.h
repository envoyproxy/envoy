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
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy

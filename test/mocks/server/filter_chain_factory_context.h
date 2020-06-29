#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

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
}

}

}

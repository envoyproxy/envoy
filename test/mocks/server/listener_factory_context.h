#pragma once

#include "envoy/server/listener_manager.h"

#include "factory_context.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockListenerFactoryContext : public MockFactoryContext, public ListenerFactoryContext {
public:
  MockListenerFactoryContext();
  ~MockListenerFactoryContext() override;

  const Network::ListenerConfig& listenerConfig() const override { return listener_config_; }
  MOCK_METHOD(const Network::ListenerConfig&, listenerConfig_, (), (const));

  Network::MockListenerConfig listener_config_;
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy

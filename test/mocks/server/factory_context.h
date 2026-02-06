#pragma once

#include "envoy/server/configuration.h"

#include "source/common/router/context_impl.h"
#include "source/common/tls/context_manager_impl.h"

#include "test/mocks/server/admin.h"
#include "test/mocks/server/drain_manager.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/overload_manager.h"
#include "test/mocks/server/server_lifecycle_notifier.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockFactoryContext : public virtual ListenerFactoryContext {
public:
  MockFactoryContext();
  ~MockFactoryContext() override;

  // Server::Configuration::GenericFactoryContext
  MOCK_METHOD(ServerFactoryContext&, serverFactoryContext, ());
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(Stats::Scope&, scope, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());

  // Server::Configuration::FactoryContext
  MOCK_METHOD(const Network::DrainDecision&, drainDecision, ());
  MOCK_METHOD(Stats::Scope&, listenerScope, ());
  MOCK_METHOD(const Network::ListenerInfo&, listenerInfo, (), (const));

  testing::NiceMock<MockServerFactoryContext> server_factory_context_;
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> store_;
  Stats::Scope& scope_{*store_.rootScope()};
  Stats::IsolatedStoreImpl listener_store_;
  Stats::Scope& listener_scope_{*listener_store_.rootScope()};
  testing::NiceMock<MockDrainManager> drain_manager_;
};

class MockUpstreamFactoryContext : public UpstreamFactoryContext {
public:
  MockUpstreamFactoryContext();

  MOCK_METHOD(ServerFactoryContext&, serverFactoryContext, ());
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(Stats::Scope&, scope, ());
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<MockServerFactoryContext> server_factory_context_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> store_;
  Stats::Scope& scope_{*store_.rootScope()};
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy

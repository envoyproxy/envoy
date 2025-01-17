#pragma once

#include "envoy/server/configuration.h"

#include "source/common/router/context_impl.h"
#include "source/common/tls/context_manager_impl.h"

#include "admin.h"
#include "drain_manager.h"
#include "gmock/gmock.h"
#include "instance.h"
#include "overload_manager.h"
#include "server_lifecycle_notifier.h"
#include "transport_socket_factory_context.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockFactoryContext : public virtual ListenerFactoryContext {
public:
  MockFactoryContext();
  ~MockFactoryContext() override;

  // Server::Configuration::GenericFactoryContext
  MOCK_METHOD(ServerFactoryContext&, serverFactoryContext, (), (const));
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(Stats::Scope&, scope, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, (), (const));

  // Server::Configuration::FactoryContext
  MOCK_METHOD(TransportSocketFactoryContext&, getTransportSocketFactoryContext, (), (const));
  MOCK_METHOD(const Network::DrainDecision&, drainDecision, ());
  MOCK_METHOD(Stats::Scope&, listenerScope, ());
  MOCK_METHOD(const Network::ListenerInfo&, listenerInfo, (), (const));

  testing::NiceMock<MockServerFactoryContext> server_factory_context_;
  testing::NiceMock<MockTransportSocketFactoryContext> transport_socket_factory_context_;
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

  MOCK_METHOD(ServerFactoryContext&, serverFactoryContext, (), (const));
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

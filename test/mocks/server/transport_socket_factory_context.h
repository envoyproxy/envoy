#pragma once

#include "envoy/server/transport_socket_config.h"

#include "source/common/secret/secret_manager_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/server/options.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class MockTransportSocketFactoryContext : public TransportSocketFactoryContext {
public:
  MockTransportSocketFactoryContext();
  ~MockTransportSocketFactoryContext() override;

  Secret::SecretManager& secretManager() override { return *(secret_manager_); }

  MOCK_METHOD(ServerFactoryContext&, serverFactoryContext, ());
  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());
  MOCK_METHOD(Ssl::ContextManager&, sslContextManager, ());
  MOCK_METHOD(Stats::Scope&, statsScope, ());
  MOCK_METHOD(Init::Manager&, initManager, ());

  testing::NiceMock<StatelessMockServerFactoryContext> server_context_;
  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Api::MockApi> api_;
  testing::NiceMock<MockConfigTracker> config_tracker_;
  testing::NiceMock<Ssl::MockContextManager> context_manager_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> store_;
  testing::NiceMock<Server::MockOptions> options_;
  std::unique_ptr<Secret::SecretManager> secret_manager_;
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  testing::NiceMock<Envoy::Server::MockServerLifecycleNotifier> lifecycle_notifier_;
  Singleton::ManagerImpl singleton_manager_;
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy

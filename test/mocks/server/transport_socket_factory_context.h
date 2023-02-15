#pragma once

#include "envoy/server/transport_socket_config.h"

#include "source/common/secret/secret_manager_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/server/options.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "config_tracker.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockTransportSocketFactoryContext : public TransportSocketFactoryContext {
public:
  MockTransportSocketFactoryContext();
  ~MockTransportSocketFactoryContext() override;

  Secret::SecretManager& secretManager() override { return *(secret_manager_); }

  MOCK_METHOD(OptRef<Server::Admin>, admin, ());
  MOCK_METHOD(Ssl::ContextManager&, sslContextManager, ());
  MOCK_METHOD(Stats::Scope&, scope, ());
  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, ());
  MOCK_METHOD(const LocalInfo::LocalInfo&, localInfo, (), (const));
  MOCK_METHOD(Event::Dispatcher&, mainThreadDispatcher, ());
  MOCK_METHOD(const Server::Options&, options, ());
  MOCK_METHOD(Envoy::Random::RandomGenerator&, random, ());
  MOCK_METHOD(Stats::Store&, stats, ());
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(Singleton::Manager&, singletonManager, ());
  MOCK_METHOD(ThreadLocal::SlotAllocator&, threadLocal, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());
  MOCK_METHOD(Api::Api&, api, ());
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, ());

  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Api::MockApi> api_;
  testing::NiceMock<MockConfigTracker> config_tracker_;
  testing::NiceMock<Ssl::MockContextManager> context_manager_;
  testing::NiceMock<Stats::MockStore> store_;
  testing::NiceMock<Server::MockOptions> options_;
  std::unique_ptr<Secret::SecretManager> secret_manager_;
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  Singleton::ManagerImpl singleton_manager_;
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy

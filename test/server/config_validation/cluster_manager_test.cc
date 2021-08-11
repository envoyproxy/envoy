#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/api/api_impl.h"
#include "source/common/grpc/context_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/singleton/manager_impl.h"
#include "source/extensions/transport_sockets/tls/context_manager_impl.h"
#include "source/server/config_validation/cluster_manager.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/options.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {
namespace {

TEST(ValidationClusterManagerTest, MockedMethods) {
  Stats::IsolatedStoreImpl stats_store;
  Event::SimulatedTimeSystem time_system;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context;
  Api::ApiPtr api(Api::createApiForTest(stats_store, time_system));
  Server::MockOptions options;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<ThreadLocal::MockInstance> tls;
  NiceMock<Random::MockRandomGenerator> random;
  testing::NiceMock<Secret::MockSecretManager> secret_manager;
  auto dns_resolver = std::make_shared<NiceMock<Network::MockDnsResolver>>();
  Extensions::TransportSockets::Tls::ContextManagerImpl ssl_context_manager{api->timeSource()};
  NiceMock<Event::MockDispatcher> dispatcher;
  LocalInfo::MockLocalInfo local_info;
  NiceMock<Server::MockAdmin> admin;
  Http::ContextImpl http_context(stats_store.symbolTable());
  Grpc::ContextImpl grpc_context(stats_store.symbolTable());
  Router::ContextImpl router_context(stats_store.symbolTable());
  Quic::QuicStatNames quic_stat_names(stats_store.symbolTable());
  AccessLog::MockAccessLogManager log_manager;
  Singleton::ManagerImpl singleton_manager{Thread::threadFactoryForTest()};

  ValidationClusterManagerFactory factory(
      admin, runtime, stats_store, tls, dns_resolver, ssl_context_manager, dispatcher, local_info,
      secret_manager, validation_context, *api, http_context, grpc_context, router_context,
      log_manager, singleton_manager, options, quic_stat_names);

  const envoy::config::bootstrap::v3::Bootstrap bootstrap;
  ClusterManagerPtr cluster_manager = factory.clusterManagerFromProto(bootstrap);
  EXPECT_EQ(nullptr, cluster_manager->getThreadLocalCluster("cluster"));
}

} // namespace
} // namespace Upstream
} // namespace Envoy

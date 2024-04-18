#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/api/api_impl.h"
#include "source/common/grpc/context_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/tls/context_manager_impl.h"
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
#include "test/mocks/server/instance.h"
#include "test/mocks/server/options.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {
namespace {

TEST(ValidationClusterManagerTest, MockedMethods) {
  NiceMock<Server::MockInstance> server;

  Stats::TestUtil::TestStore& stats_store = server.server_factory_context_->store_;
  Event::GlobalTimeSystem& time_system = server.server_factory_context_->time_system_;
  Api::ApiPtr api(Api::createApiForTest(stats_store, time_system));
  ON_CALL(*server.server_factory_context_, api()).WillByDefault(testing::ReturnRef(*api));

  NiceMock<ThreadLocal::MockInstance>& tls = server.server_factory_context_->thread_local_;

  testing::NiceMock<Secret::MockSecretManager> secret_manager;
  auto dns_resolver = std::make_shared<NiceMock<Network::MockDnsResolver>>();
  Extensions::TransportSockets::Tls::ContextManagerImpl ssl_context_manager{
      *server.server_factory_context_};

  Http::ContextImpl http_context(stats_store.symbolTable());
  Quic::QuicStatNames quic_stat_names(stats_store.symbolTable());

  ValidationClusterManagerFactory factory(
      *server.server_factory_context_, stats_store, tls, http_context,
      [dns_resolver]() -> Network::DnsResolverSharedPtr { return dns_resolver; },
      ssl_context_manager, secret_manager, quic_stat_names, server);

  const envoy::config::bootstrap::v3::Bootstrap bootstrap;
  ClusterManagerPtr cluster_manager = factory.clusterManagerFromProto(bootstrap);
  EXPECT_EQ(nullptr, cluster_manager->getThreadLocalCluster("cluster"));
}

} // namespace
} // namespace Upstream
} // namespace Envoy

#include <chrono>
#include <memory>
#include <vector>

#include "envoy/config/cluster/redis/redis_cluster.pb.h"
#include "envoy/config/cluster/redis/redis_cluster.pb.validate.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"
#include "envoy/stats/scope.h"

#include "common/network/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/upstream/logical_dns_cluster.h"

#include "source/extensions/clusters/redis/twem_cluster.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/clusters/redis/mocks.h"
#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

namespace {
const std::string BasicConfig = R"EOF(
  name: name
  connect_timeout: 0.25s
  dns_lookup_family: V4_ONLY
  common_lb_config:
    consistent_hashing_lb_config:
      use_hostname_for_hashing: true
  lb_policy: CLUSTER_PROVIDED
  cluster_type:
    name: envoy.clusters.twem
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        cluster_refresh_rate: 4s
        cluster_refresh_timeout: 0.25s
  eds_cluster_config:
    eds_config:
      path: memcached.yaml
  )EOF";
}

class TwemClusterTest : public testing::Test {
public:
  TwemClusterTest() : api_(Api::createApiForTest(stats_store_, random_)) {}

  void setupFactoryFromV3Yaml(const std::string& yaml) {
    NiceMock<Upstream::MockClusterManager> cm;
    envoy::config::cluster::v3::Cluster cluster_config = Upstream::parseClusterFromV3Yaml(yaml);
    Envoy::Stats::ScopePtr scope = stats_store_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm, local_info_, dispatcher_, stats_store_,
      singleton_manager_, tls_, validation_visitor_, *api_);

    envoy::config::cluster::redis::RedisClusterConfig config;
    Config::Utility::translateOpaqueConfig(cluster_config.cluster_type().typed_config(),
          ProtobufWkt::Struct::default_instance(), validation_visitor_, config);

    NiceMock<AccessLog::MockAccessLogManager> log_manager;
    NiceMock<Upstream::Outlier::EventLoggerSharedPtr> outlier_event_logger;
    NiceMock<Envoy::Api::MockApi> api;
    Upstream::ClusterFactoryContextImpl cluster_factory_context(
          cm, stats_store_, tls_, std::move(dns_resolver_), ssl_context_manager_, runtime_,
          dispatcher_, log_manager, local_info_, admin_, singleton_manager_,
          std::move(outlier_event_logger), false, validation_visitor_, api);

    TwemClusterFactory factory = TwemClusterFactory();
    factory.createClusterWithConfig(cluster_config, config, cluster_factory_context,
                                     factory_context, std::move(scope));
  }

  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;

  Stats::TestUtil::TestStore stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{new NiceMock<Network::MockDnsResolver>};
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  Api::ApiPtr api_;
};

TEST_F(TwemClusterTest, FactoryInitTwemClusterTypeSuccess) {
  setupFactoryFromV3Yaml(BasicConfig);
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

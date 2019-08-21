#include "common/singleton/manager_impl.h"

#include "extensions/clusters/aggregate/cluster.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"

using testing::AtLeast;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SizeIs;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

class ClusterTest : public testing::Test {
public:
  void initialize(const std::string& yaml_config) {
    envoy::api::v2::Cluster cluster_config = Upstream::parseClusterFromV2Yaml(yaml_config);
    envoy::config::cluster::aggregate::ClusterConfig config;
    Config::Utility::translateOpaqueConfig(cluster_config.cluster_type().typed_config(),
                                           ProtobufWkt::Struct::default_instance(),
                                           ProtobufMessage::getStrictValidationVisitor(), config);
    Stats::ScopePtr scope = stats_store_.createScope("cluster.name.");
    Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_store_,
        singleton_manager_, tls_, validation_visitor_, *api_);

    cluster_ = std::make_shared<Cluster>(cluster_config, config, runtime_, factory_context,
                                         std::move(scope), false);
    thread_aware_lb_ = std::make_unique<AggregateThreadAwareLoadBalancer>(*cluster_, cm_, random_);
    lb_factory_ = thread_aware_lb_->factory();
    refreshLb();
  }

  void refreshLb() { lb_ = lb_factory_->create(); }

  Stats::IsolatedStoreImpl stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_{Api::createApiForTest(stats_store_)};
  std::shared_ptr<Cluster> cluster_;
  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
  Upstream::LoadBalancerFactorySharedPtr lb_factory_;
  Upstream::LoadBalancerPtr lb_;

  const std::string default_yaml_config_ = R"EOF(
    name: aggregate_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.aggregate
      typed_config:
        "@type": type.googleapis.com/envoy.config.cluster.aggregate.ClusterConfig
        lb_clusters:
        - priority: 0
          cluster_name: proxy1
        - priority: 0
          cluster_name: proxy2
        - priority: 1
          cluster_name: proxy3
)EOF";
};

// Basic flow of the cluster including adding hosts and removing them.
TEST_F(ClusterTest, BasicFlow) {
  initialize(default_yaml_config_);
  auto cluster_per_priority = cluster_->getClustersPerPriority();
  EXPECT_EQ(cluster_per_priority.size(), 2);
  EXPECT_EQ(cluster_per_priority[0].size(), 2);
  EXPECT_EQ(cluster_per_priority[1].size(), 1);
}

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

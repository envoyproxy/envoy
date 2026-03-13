#include "envoy/extensions/clusters/mcp_multicluster/v3/cluster.pb.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/mcp_multicluster/cluster.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/http/conn_pool.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/load_balancer.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace McpMulticluster {

class McpMulticlusterTest : public testing::Test {
public:
  McpMulticlusterTest() = default;

  void initialize(const std::string& yaml_config) {
    cluster_config_ = Upstream::parseClusterFromV3Yaml(yaml_config);
    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               false);

    absl::StatusOr<std::pair<Upstream::ClusterSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
        result_status = factory_.create(cluster_config_, factory_context);
    ASSERT_OK(result_status);
    std::pair<Upstream::ClusterSharedPtr, Upstream::ThreadAwareLoadBalancerPtr> result(
        std::move(result_status).value());
    cluster_ = std::move(result.first);
    thread_aware_lb_ = std::move(result.second);
  }

  envoy::config::cluster::v3::Cluster cluster_config_;
  ClusterFactory factory_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Upstream::ClusterSharedPtr cluster_;
  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
};

// Test basic cluster creation.
TEST_F(McpMulticlusterTest, BasicCreation) {
  const std::string yaml = R"EOF(
name: mcp_multicluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.mcp_multicluster
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.mcp_multicluster.v3.ClusterConfig
    servers:
    - name: primary
      mcp_cluster:
        cluster: primary
    - name: secondary
      mcp_cluster:
        cluster: secondary
)EOF";

  initialize(yaml);
  const envoy::config::core::v3::Metadata& metadata = cluster_->info()->metadata();

  EXPECT_TRUE(metadata.typed_filter_metadata().contains("envoy.clusters.mcp_multicluster"));
  envoy::extensions::clusters::mcp_multicluster::v3::ClusterConfig mcp_multicluster_config;
  metadata.typed_filter_metadata()
      .at("envoy.clusters.mcp_multicluster")
      .UnpackTo(&mcp_multicluster_config);
  EXPECT_EQ("primary", mcp_multicluster_config.servers()[0].name());
  EXPECT_EQ("secondary", mcp_multicluster_config.servers()[1].name());
  EXPECT_EQ(Upstream::Cluster::InitializePhase::Secondary, cluster_->initializePhase());
}

} // namespace McpMulticluster
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

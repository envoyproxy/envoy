// Tests for the eager_preconnect_floor preconnect feature.

#include "test/common/upstream/cluster_manager_impl_test_common.h"
#include "test/mocks/upstream/load_balancer_context.h"

namespace Envoy {
namespace Upstream {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnNew;

class EagerPreconnectFloorTest : public ClusterManagerImplTest {
public:
  void createWithMinConnections(const std::string& yaml) {
    // Floor maintenance may allocate conn pools during initial host setup; default-mock allocator.
    ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
        .WillByDefault(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());
    create(parseBootstrapFromV3Yaml(yaml));
  }
};

TEST_F(EagerPreconnectFloorTest, AllFieldsDefaulted) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";

  createWithMinConnections(yaml);

  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  EXPECT_EQ(0, cluster->info()->eagerPreconnectFloor());
  EXPECT_EQ(3, cluster->info()->eagerPreconnectFloorFailureThreshold());

  factory_.tls_.shutdownThread();
}

TEST_F(EagerPreconnectFloorTest, AllFieldsConfigured) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
      preconnect_policy:
        eager_preconnect_floor:
          value: 3
        eager_preconnect_floor_failure_threshold:
          value: 5
  )EOF";

  createWithMinConnections(yaml);

  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  EXPECT_EQ(3, cluster->info()->eagerPreconnectFloor());
  EXPECT_EQ(5, cluster->info()->eagerPreconnectFloorFailureThreshold());

  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, EagerPreconnectFloorIncompatibleWithPoolPerDownstreamConnection) {
  // The eager preconnect floor warms connections ahead of requests, but with
  // connection_pool_per_downstream_connection there is no shared pool to warm. Reject the combo.
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      connection_pool_per_downstream_connection: true
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
      "eager_preconnect_floor is incompatible with connection_pool_per_downstream_connection");
}

} // namespace
} // namespace Upstream
} // namespace Envoy

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/metrics/v3/stats.pb.h"

#include "source/common/upstream/prod_cluster_info_factory.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {
namespace {

class ProdClusterInfoFactoryTest : public testing::Test {
protected:
  ProdClusterInfoFactoryTest() { ON_CALL(server_context_, api()).WillByDefault(ReturnRef(*api_)); }

  ClusterInfoConstSharedPtr createClusterInfo(const envoy::config::cluster::v3::Cluster& cluster) {
    return factory_.createClusterInfo({server_context_, cluster, bind_config_,
                                       server_context_.store_, server_context_.ssl_context_manager_,
                                       false, server_context_.thread_local_});
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<Random::MockRandomGenerator> random_;
  Api::ApiPtr api_ = Api::createApiForTest(server_context_.store_, random_);
  envoy::config::core::v3::BindConfig bind_config_;
  ProdClusterInfoFactory factory_;
};

// alt_stat_name is preferred over name for stats scope generation.
TEST_F(ProdClusterInfoFactoryTest, AltStatNamePreferred) {
  const std::string yaml = R"EOF(
    name: my_cluster
    alt_stat_name: my_alt_cluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";

  auto info = createClusterInfo(parseClusterFromV3Yaml(yaml));
  ASSERT_NE(nullptr, info);
  info->trafficStats();

  // Stats scope should be generated with alt_stat_name, not name.
  EXPECT_EQ(info->statsScope().symbolTable().toString(info->statsScope().prefix()),
            "cluster.my_alt_cluster");
}

// Verify that a cluster without a stats matcher in metadata creates all stats normally.
TEST_F(ProdClusterInfoFactoryTest, NoMetadataStatsMatcher) {
  const std::string yaml = R"EOF(
    name: my_cluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";

  auto info = createClusterInfo(parseClusterFromV3Yaml(yaml));
  ASSERT_NE(nullptr, info);
  info->trafficStats();

  EXPECT_EQ(info->statsScope().symbolTable().toString(info->statsScope().prefix()),
            "cluster.my_cluster");

  // Without a scope matcher, stats of any name are accepted.
  EXPECT_NE("", info->statsScope().counterFromString("upstream_cx_total").name());
  EXPECT_NE("", info->statsScope().counterFromString("upstream_rq_total").name());
}

// Verify that a cluster with typed_filter_metadata["envoy.stats_matcher"] applies an inclusion
// list: only stats matching the prefix are created; all others are rejected.
TEST_F(ProdClusterInfoFactoryTest, MetadataStatsMatcherInclusionList) {
  const std::string yaml = R"EOF(
    name: my_cluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    metadata:
      typed_filter_metadata:
        envoy.stats_matcher:
          "@type": type.googleapis.com/envoy.config.metrics.v3.StatsMatcher
          inclusion_list:
            patterns:
              - prefix: "cluster.my_cluster.upstream_cx"
    load_assignment:
      endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";

  auto info = createClusterInfo(parseClusterFromV3Yaml(yaml));
  ASSERT_NE(nullptr, info);
  info->trafficStats();

  // "cluster.my_cluster.upstream_cx_total" starts with the inclusion prefix — accepted.
  EXPECT_NE("", info->statsScope().counterFromString("upstream_cx_total").name());

  // "cluster.my_cluster.upstream_rq_total" does not match the prefix — rejected.
  EXPECT_EQ("", info->statsScope().counterFromString("upstream_rq_total").name());
}

// Verify that a cluster with an exclusion list rejects only the listed stats.
TEST_F(ProdClusterInfoFactoryTest, MetadataStatsMatcherExclusionList) {
  const std::string yaml = R"EOF(
    name: my_cluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    metadata:
      typed_filter_metadata:
        envoy.stats_matcher:
          "@type": type.googleapis.com/envoy.config.metrics.v3.StatsMatcher
          exclusion_list:
            patterns:
              - prefix: "cluster.my_cluster.upstream_rq"
    load_assignment:
      endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";

  auto info = createClusterInfo(parseClusterFromV3Yaml(yaml));
  ASSERT_NE(nullptr, info);
  info->trafficStats();

  // "cluster.my_cluster.upstream_cx_total" does not match the exclusion prefix — accepted.
  EXPECT_NE("", info->statsScope().counterFromString("upstream_cx_total").name());

  // "cluster.my_cluster.upstream_rq_total" matches the exclusion prefix — rejected.
  EXPECT_EQ("", info->statsScope().counterFromString("upstream_rq_total").name());
}

} // namespace
} // namespace Upstream
} // namespace Envoy

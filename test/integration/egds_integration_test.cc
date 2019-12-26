#include <random>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/type/v3/http.pb.h"

#include "common/runtime/runtime_impl.h"
#include "common/upstream/load_balancer_impl.h"

#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Integration test for EGDS features. EGDS is consumed via filesystem
// subscription.
class EgdsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                            public HttpIntegrationTest {
public:
  EgdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()),
        codec_client_type_(envoy::type::v3::HTTP1),
        endpoint_group_names_({"group1", "group2", "group3", "group4"}) {}

  void addShardEgds(absl::string_view group_name, uint32_t total_endpoints,
                    uint32_t healthy_endpoints, uint32_t degraded_endpoints,
                    uint32_t& current_upstream_index,
                    std::vector<envoy::config::endpoint::v3::EndpointGroup>& output_endpoint_groups,
                    bool remaining_unhealthy = true) {
    ASSERT(total_endpoints >= healthy_endpoints + degraded_endpoints);
    envoy::config::endpoint::v3::EndpointGroup endpoint_group;
    endpoint_group.set_name(group_name.data());
    auto* locality_lb_endpoints = endpoint_group.add_endpoints();
    for (uint32_t i = 0; i < total_endpoints; ++i) {
      ASSERT(current_upstream_index <= 4);
      auto* endpoint = locality_lb_endpoints->add_lb_endpoints();
      setUpstreamAddress(current_upstream_index++, *endpoint);
      // First N endpoints are degraded, next M are healthy and the remaining endpoints are
      // unhealthy or unknown depending on remaining_unhealthy.
      if (i < degraded_endpoints) {
        endpoint->set_health_status(::envoy::config::core::v3::HealthStatus::DEGRADED);
      } else if (i >= healthy_endpoints + degraded_endpoints) {
        endpoint->set_health_status(remaining_unhealthy
                                        ? ::envoy::config::core::v3::HealthStatus::UNHEALTHY
                                        : ::envoy::config::core::v3::HealthStatus::UNKNOWN);
      }
    }

    output_endpoint_groups.emplace_back(std::move(endpoint_group));
  }

  void
  addEmptyShardEgds(std::vector<envoy::config::endpoint::v3::EndpointGroup>& output_endpoint_groups,
                    bool remaining_unhealthy = true) {
    uint32_t current_upstream_index = 0;
    for (const auto& name : endpoint_group_names_) {
      addShardEgds(name, 0, 0, 0, current_upstream_index, output_endpoint_groups,
                   remaining_unhealthy);
    }
  }

  inline bool canShard(uint32_t total_endpoints, uint32_t group_count) {
    return total_endpoints >= group_count;
  }

  void setEndpoints(uint32_t total_endpoints, uint32_t healthy_endpoints,
                    uint32_t degraded_endpoints, bool remaining_unhealthy = true,
                    bool await_update = true) {
    std::vector<envoy::config::endpoint::v3::EndpointGroup> endpoint_groups;
    uint32_t current_upstream_index = 0;
    const uint32_t group_names_size = endpoint_group_names_.size();
    if (0 == total_endpoints) {
      ASSERT(0 == healthy_endpoints && 0 == degraded_endpoints);
      addEmptyShardEgds(endpoint_groups, remaining_unhealthy);
    } else {
      if (canShard(total_endpoints, group_names_size)) {
        uint32_t shard_total_endpoints = total_endpoints / group_names_size;
        uint32_t shard_healthy_endpoints = healthy_endpoints / group_names_size;
        uint32_t shard_degraded_endpoints = degraded_endpoints / group_names_size;

        for (uint32_t i = 0; i < group_names_size; i++) {
          // Place the remaining endpoints into the last shard.
          if (i == group_names_size - 1) {
            shard_total_endpoints = total_endpoints - shard_total_endpoints * i;
            shard_healthy_endpoints = healthy_endpoints - shard_healthy_endpoints * i;
            shard_degraded_endpoints = degraded_endpoints - shard_degraded_endpoints * i;
          }
          addShardEgds(endpoint_group_names_.at(i), shard_total_endpoints, shard_healthy_endpoints,
                       shard_degraded_endpoints, current_upstream_index, endpoint_groups,
                       remaining_unhealthy);
        }
      } else {
        ENVOY_LOG(info, "the number of endpoints is less than the number of groups, and only the "
                        "first group one is set");
        addShardEgds(endpoint_group_names_.at(0), total_endpoints, healthy_endpoints,
                     degraded_endpoints, current_upstream_index, endpoint_groups,
                     remaining_unhealthy);
      }
    }

    if (await_update) {
      egds_helper_.setEgdsAndWait(endpoint_groups, *test_server_);
    } else {
      egds_helper_.setEgds(endpoint_groups);
    }
  }

  void setEds(absl::optional<uint32_t> overprovisioning_factor = absl::nullopt,
              bool await_update = true) {
    envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
    cluster_load_assignment.set_cluster_name("cluster_0");
    if (overprovisioning_factor.has_value()) {
      cluster_load_assignment.mutable_policy()->mutable_overprovisioning_factor()->set_value(
          overprovisioning_factor.value());
    }

    for (const auto& name : endpoint_group_names_) {
      auto* group = cluster_load_assignment.add_endpoint_groups();
      group->set_endpoint_group_name(name);
      group->mutable_config_source()->set_path(egds_helper_.egds_path());
    }

    if (await_update) {
      eds_helper_.setEdsAndWait({cluster_load_assignment}, *test_server_);
    } else {
      eds_helper_.setEds({cluster_load_assignment});
    }
  }

  void initializeTest(bool http_active_hc) {
    setUpstreamCount(4);
    if (codec_client_type_ == envoy::type::v3::HTTP2) {
      setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    }
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Switch predefined cluster_0 to CDS filesystem sourcing.
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_path(cds_helper_.cds_path());
      bootstrap.mutable_static_resources()->clear_clusters();
    });

    // Set validate_clusters to false to allow us to reference a CDS cluster.
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) { hcm.mutable_route_config()->mutable_validate_clusters()->set_value(false); });

    cluster_.mutable_connect_timeout()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    cluster_.set_name("cluster_0");
    // cluster_.mutable_hosts()->Clear();
    cluster_.set_type(envoy::config::cluster::v3::Cluster::EDS);
    auto* eds_cluster_config = cluster_.mutable_eds_cluster_config();
    eds_cluster_config->mutable_eds_config()->set_path(eds_helper_.eds_path());
    if (http_active_hc) {
      auto* health_check = cluster_.add_health_checks();
      health_check->mutable_timeout()->set_seconds(30);
      // TODO(mattklein123): Consider using simulated time here.
      health_check->mutable_interval()->CopyFrom(
          Protobuf::util::TimeUtil::MillisecondsToDuration(100));
      health_check->mutable_no_traffic_interval()->CopyFrom(
          Protobuf::util::TimeUtil::MillisecondsToDuration(100));
      health_check->mutable_unhealthy_threshold()->set_value(1);
      health_check->mutable_healthy_threshold()->set_value(1);
      health_check->mutable_http_health_check()->set_path("/healthcheck");
      health_check->mutable_http_health_check()->set_codec_client_type(codec_client_type_);
    }

    setEndpoints(0, 0, 0, false, false);
    setEds(absl::nullopt, false);
    cds_helper_.setCds({cluster_});
    initialize();

    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  }

  envoy::type::v3::CodecClientType codec_client_type_{};
  std::vector<std::string> endpoint_group_names_;
  EdsHelper eds_helper_;
  CdsHelper cds_helper_;
  EgdsHelper egds_helper_;
  envoy::config::cluster::v3::Cluster cluster_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, EgdsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Verifies that a new cluster can we warmed when using HTTP/2 health checking. Regression test
// of the issue detailed in issue #6951.
TEST_P(EgdsIntegrationTest, Http2HcClusterRewarming) {
  codec_client_type_ = envoy::type::v3::HTTP2;
  initializeTest(true);
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  fake_upstreams_[1]->set_allow_unexpected_disconnects(true);
  fake_upstreams_[2]->set_allow_unexpected_disconnects(true);
  fake_upstreams_[3]->set_allow_unexpected_disconnects(true);
  uint32_t total_endpoints = 4;
  setEndpoints(total_endpoints, 0, 0, false);
  EXPECT_EQ(total_endpoints, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy. This should warm the initial cluster.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(total_endpoints, test_server_->gauge("cluster.cluster_0.membership_total")->value());

  // Trigger a CDS update. This should cause a new cluster to require warming, blocked on the host
  // being health checked.
  cluster_.mutable_circuit_breakers()->add_thresholds()->mutable_max_connections()->set_value(100);
  cds_helper_.setCds({cluster_});
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster_manager.warming_clusters")->value());

  // We need to do a bunch of work to get a hold of second hc connection.
  FakeHttpConnectionPtr fake_upstream_connection;
  auto result = fake_upstreams_[0]->waitForHttpConnection(
      *dispatcher_, fake_upstream_connection, TestUtility::DefaultTimeout, max_request_headers_kb_);
  RELEASE_ASSERT(result, result.message());

  FakeStreamPtr upstream_request;
  result = fake_upstream_connection->waitForNewStream(*dispatcher_, upstream_request);
  RELEASE_ASSERT(result, result.message());
  // Wait for the stream to be completely received.
  result = upstream_request->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  // Respond with a health check. This will cause the previous cluster to be destroyed inline as
  // part of processing the response.
  upstream_request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, true);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  EXPECT_EQ(0, test_server_->gauge("cluster_manager.warming_clusters")->value());

  // Since the second connection is not managed by the integration test base we need to close it
  // ourselves.
  result = fake_upstream_connection->close();
  RELEASE_ASSERT(result, result.message());
  result = fake_upstream_connection->waitForDisconnect();
  RELEASE_ASSERT(result, result.message());
  fake_upstream_connection.reset();
}

// Verify that a host stabilized via active health checking which is first removed from EGDS and
// then fails health checking is removed.
TEST_P(EgdsIntegrationTest, RemoveAfterHcFail) {
  initializeTest(true);
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  fake_upstreams_[1]->set_allow_unexpected_disconnects(true);
  fake_upstreams_[2]->set_allow_unexpected_disconnects(true);
  fake_upstreams_[3]->set_allow_unexpected_disconnects(true);
  uint32_t total_endpoints = 4;
  setEndpoints(total_endpoints, 0, 0, false);
  EXPECT_EQ(total_endpoints, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(total_endpoints, test_server_->gauge("cluster.cluster_0.membership_total")->value());

  // Clear out the host and verify the host is still healthy.
  setEndpoints(0, 0, 0);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Fail HC and verify the host is gone.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestHeaderMapImpl{{":status", "503"}, {"connection", "close"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 0);
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_total")->value());
}

// Verifies that endpoints are ignored until health checked when configured to.
TEST_P(EgdsIntegrationTest, EndpointWarmingSuccessfulHc) {
  cluster_.mutable_common_lb_config()->set_ignore_new_hosts_until_first_hc(true);

  // Endpoints are initially excluded.
  initializeTest(true);
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  fake_upstreams_[1]->set_allow_unexpected_disconnects(true);
  fake_upstreams_[2]->set_allow_unexpected_disconnects(true);
  fake_upstreams_[3]->set_allow_unexpected_disconnects(true);
  uint32_t total_endpoints = 4;
  setEndpoints(total_endpoints, 0, 0, false);

  EXPECT_EQ(total_endpoints, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(total_endpoints, test_server_->gauge("cluster.cluster_0.membership_excluded")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy and that it is no longer being excluded.
  // The other endpoint should still be excluded.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_excluded", 0);
  EXPECT_EQ(total_endpoints, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Verifies that endpoints are ignored until health checked when configured to when the first
// health check fails.
TEST_P(EgdsIntegrationTest, EndpointWarmingFailedHc) {
  cluster_.mutable_common_lb_config()->set_ignore_new_hosts_until_first_hc(true);

  // Endpoints are initially excluded.
  initializeTest(true);
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  fake_upstreams_[1]->set_allow_unexpected_disconnects(true);
  fake_upstreams_[2]->set_allow_unexpected_disconnects(true);
  fake_upstreams_[3]->set_allow_unexpected_disconnects(true);
  uint32_t total_endpoints = 4;
  setEndpoints(total_endpoints, 0, 0, false);

  EXPECT_EQ(total_endpoints, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(total_endpoints, test_server_->gauge("cluster.cluster_0.membership_excluded")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy and that it is no longer being excluded.
  // The other endpoint should still be excluded.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_excluded", 0);
  EXPECT_EQ(total_endpoints, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Validate that health status updates are consumed from EGDS.
TEST_P(EgdsIntegrationTest, HealthUpdate) {
  initializeTest(false);
  // Initial state, no cluster members.
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // 2/2 healthy endpoints.
  setEndpoints(2, 2, 0);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Drop to 0/2 healthy endpoints.
  setEndpoints(2, 0, 0);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Increase to 1/2 healthy endpoints.
  setEndpoints(2, 1, 0);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Add host and modify health to 2/3 healthy endpoints.
  setEndpoints(3, 2, 0);
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Modify health to 2/3 healthy and 1/3 degraded.
  setEndpoints(3, 2, 1);
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_degraded")->value());
}

// Validate that overprovisioning_factor update are picked up by Envoy.
TEST_P(EgdsIntegrationTest, OverprovisioningFactorUpdate) {
  initializeTest(false);
  // Default overprovisioning factor.
  setEndpoints(4, 4, 0);
  auto get_and_compare = [this](const uint32_t expected_factor) {
    const auto& cluster_map = test_server_->server().clusterManager().clusters();
    EXPECT_EQ(1, cluster_map.size());
    EXPECT_EQ(1, cluster_map.count("cluster_0"));
    const auto& cluster_ref = cluster_map.find("cluster_0")->second;
    const auto& hostset_per_priority = cluster_ref.get().prioritySet().hostSetsPerPriority();
    EXPECT_EQ(1, hostset_per_priority.size());
    const Envoy::Upstream::HostSetPtr& host_set = hostset_per_priority[0];
    EXPECT_EQ(expected_factor, host_set->overprovisioningFactor());
  };
  get_and_compare(Envoy::Upstream::kDefaultOverProvisioningFactor);

  // Use new overprovisioning factor 200.
  setEndpoints(4, 4, 0);
  setEds(200);
  get_and_compare(200);
}

TEST_P(EgdsIntegrationTest, StatsReadyFilter) {
  config_helper_.addFilter("name: eds-ready-filter");
  initializeTest(false);

  // Initial state: no healthy endpoints
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/cluster1", "", downstream_protocol_, version_, "foo.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().Status()->value().getStringView());
  EXPECT_EQ("EDS not ready", response->body());

  cleanupUpstreamAndDownstream();

  // 2/2 healthy endpoints.
  setEndpoints(2, 2, 0);
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  response = IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/cluster1", "",
                                                downstream_protocol_, version_, "foo.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("EDS is ready", response->body());

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy

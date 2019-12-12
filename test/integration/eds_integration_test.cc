#include "envoy/api/v2/eds.pb.h"

#include "common/upstream/load_balancer_impl.h"

#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Integration test for EDS features. EDS is consumed via filesystem
// subscription.
class EdsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                           public HttpIntegrationTest {
public:
  EdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()),
        codec_client_type_(envoy::type::HTTP1) {}

  // We need to supply the endpoints via EDS to provide health status. Use a
  // filesystem delivery to simplify test mechanics.
  void setEndpoints(uint32_t total_endpoints, uint32_t healthy_endpoints,
                    uint32_t degraded_endpoints, bool remaining_unhealthy = true,
                    absl::optional<uint32_t> overprovisioning_factor = absl::nullopt,
                    bool await_update = true) {
    ASSERT(total_endpoints >= healthy_endpoints + degraded_endpoints);
    envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
    cluster_load_assignment.set_cluster_name("cluster_0");
    if (overprovisioning_factor.has_value()) {
      cluster_load_assignment.mutable_policy()->mutable_overprovisioning_factor()->set_value(
          overprovisioning_factor.value());
    }
    auto* locality_lb_endpoints = cluster_load_assignment.add_endpoints();

    for (uint32_t i = 0; i < total_endpoints; ++i) {
      auto* endpoint = locality_lb_endpoints->add_lb_endpoints();
      setUpstreamAddress(i, *endpoint);
      // First N endpoints are degraded, next M are healthy and the remaining endpoints are
      // unhealthy or unknown depending on remaining_unhealthy.
      if (i < degraded_endpoints) {
        endpoint->set_health_status(envoy::api::v2::core::HealthStatus::DEGRADED);
      } else if (i >= healthy_endpoints + degraded_endpoints) {
        endpoint->set_health_status(remaining_unhealthy
                                        ? envoy::api::v2::core::HealthStatus::UNHEALTHY
                                        : envoy::api::v2::core::HealthStatus::UNKNOWN);
      }
    }

    if (await_update) {
      eds_helper_.setEdsAndWait({cluster_load_assignment}, *test_server_);
    } else {
      eds_helper_.setEds({cluster_load_assignment});
    }
  }

  void initializeTest(bool http_active_hc) {
    setUpstreamCount(4);
    if (codec_client_type_ == envoy::type::HTTP2) {
      setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    }
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      // Switch predefined cluster_0 to CDS filesystem sourcing.
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_path(cds_helper_.cds_path());
      bootstrap.mutable_static_resources()->clear_clusters();
    });

    // Set validate_clusters to false to allow us to reference a CDS cluster.
    config_helper_.addConfigModifier(
        [](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
               hcm) { hcm.mutable_route_config()->mutable_validate_clusters()->set_value(false); });

    cluster_.mutable_connect_timeout()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    cluster_.set_name("cluster_0");
    cluster_.mutable_hosts()->Clear();
    cluster_.set_type(envoy::api::v2::Cluster::EDS);
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
    setEndpoints(0, 0, 0, true, absl::nullopt, false);
    cds_helper_.setCds({cluster_});
    initialize();
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  }

  envoy::type::CodecClientType codec_client_type_{};
  EdsHelper eds_helper_;
  CdsHelper cds_helper_;
  envoy::api::v2::Cluster cluster_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, EdsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Verifies that a new cluster can we warmed when using HTTP/2 health checking. Regression test
// of the issue detailed in issue #6951.
TEST_P(EdsIntegrationTest, Http2HcClusterRewarming) {
  codec_client_type_ = envoy::type::HTTP2;
  initializeTest(true);
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  setEndpoints(1, 0, 0, false);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy. This should warm the initial cluster.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());

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

// Verify that a host stabilized via active health checking which is first removed from EDS and
// then fails health checking is removed.
TEST_P(EdsIntegrationTest, RemoveAfterHcFail) {
  initializeTest(true);
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  setEndpoints(1, 0, 0, false);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());

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
TEST_P(EdsIntegrationTest, EndpointWarmingSuccessfulHc) {
  cluster_.mutable_common_lb_config()->set_ignore_new_hosts_until_first_hc(true);

  // Endpoints are initially excluded.
  initializeTest(true);
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  setEndpoints(1, 0, 0, false);

  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_excluded")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy and that it is no longer being excluded.
  // The other endpoint should still be excluded.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_excluded", 0);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Verifies that endpoints are ignored until health checked when configured to when the first
// health check fails.
TEST_P(EdsIntegrationTest, EndpointWarmingFailedHc) {
  cluster_.mutable_common_lb_config()->set_ignore_new_hosts_until_first_hc(true);

  // Endpoints are initially excluded.
  initializeTest(true);
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  setEndpoints(1, 0, 0, false);

  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_excluded")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy and that it is no longer being excluded.
  // The other endpoint should still be excluded.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_excluded", 0);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Validate that health status updates are consumed from EDS.
TEST_P(EdsIntegrationTest, HealthUpdate) {
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
TEST_P(EdsIntegrationTest, OverprovisioningFactorUpdate) {
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
  setEndpoints(4, 4, 0, true, 200);
  get_and_compare(200);
}

// Verifies that EDS update only triggers member update callbacks once per update.
TEST_P(EdsIntegrationTest, BatchMemberUpdateCb) {
  initializeTest(false);

  uint32_t member_update_count{};

  auto& priority_set = test_server_->server()
                           .clusterManager()
                           .clusters()
                           .find("cluster_0")
                           ->second.get()
                           .prioritySet();

  // Keep track of how many times we're seeing a member update callback.
  priority_set.addMemberUpdateCb([&](const auto& hosts_added, const auto&) {
    // We should see both hosts present in the member update callback.
    EXPECT_EQ(2, hosts_added.size());
    member_update_count++;
  });

  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("cluster_0");

  {
    auto* locality_lb_endpoints = cluster_load_assignment.add_endpoints();

    auto* endpoint = locality_lb_endpoints->add_lb_endpoints();
    setUpstreamAddress(0, *endpoint);
  }

  auto* locality_lb_endpoints = cluster_load_assignment.add_endpoints();
  locality_lb_endpoints->set_priority(1);

  auto* endpoint = locality_lb_endpoints->add_lb_endpoints();
  setUpstreamAddress(1, *endpoint);

  eds_helper_.setEdsAndWait({cluster_load_assignment}, *test_server_);

  EXPECT_EQ(1, member_update_count);
}

TEST_P(EdsIntegrationTest, StatsReadyFilter) {
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

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/type/v3/http.pb.h"

#include "source/common/upstream/load_balancer_impl.h"

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
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()),
        codec_client_type_(envoy::type::v3::HTTP1) {}

  // We need to supply the endpoints via EDS to provide health status. Use a
  // filesystem delivery to simplify test mechanics.
  void setEndpointsInPriorities(uint32_t first_priority, uint32_t second_priority,
                                bool await_update = true) {
    envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
    cluster_load_assignment.set_cluster_name("cluster_0");

    {
      for (uint32_t i = 0; i < first_priority; ++i) {
        auto* locality_lb_endpoints = cluster_load_assignment.add_endpoints();
        auto* endpoint = locality_lb_endpoints->add_lb_endpoints();
        setUpstreamAddress(i, *endpoint);
      }
    }

    {
      for (uint32_t i = first_priority; i < first_priority + second_priority; ++i) {
        auto* locality_lb_endpoints = cluster_load_assignment.add_endpoints();
        locality_lb_endpoints->set_priority(1);
        auto* endpoint = locality_lb_endpoints->add_lb_endpoints();
        setUpstreamAddress(i, *endpoint);
      }
    }

    if (await_update) {
      eds_helper_.setEdsAndWait({cluster_load_assignment}, *test_server_);
    } else {
      eds_helper_.setEds({cluster_load_assignment});
    }
  }

  struct EndpointSettingOptions {
    uint32_t total_endpoints = 1;
    uint32_t healthy_endpoints = 0;
    uint32_t degraded_endpoints = 0;
    uint32_t disable_active_hc_endpoints = 0;
    absl::optional<uint32_t> overprovisioning_factor = absl::nullopt;
  };

  // We need to supply the endpoints via EDS to provide health status. Use a
  // filesystem delivery to simplify test mechanics.
  void setEndpoints(const EndpointSettingOptions& endpoint_setting, bool remaining_unhealthy = true,
                    bool await_update = true) {
    ASSERT(endpoint_setting.total_endpoints >=
           endpoint_setting.healthy_endpoints + endpoint_setting.degraded_endpoints);
    ASSERT(endpoint_setting.total_endpoints >= endpoint_setting.disable_active_hc_endpoints);
    envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
    cluster_load_assignment.set_cluster_name("cluster_0");
    if (endpoint_setting.overprovisioning_factor.has_value()) {
      cluster_load_assignment.mutable_policy()->mutable_overprovisioning_factor()->set_value(
          endpoint_setting.overprovisioning_factor.value());
    }
    auto* locality_lb_endpoints = cluster_load_assignment.add_endpoints();

    for (uint32_t i = 0; i < endpoint_setting.total_endpoints; ++i) {
      auto* endpoint = locality_lb_endpoints->add_lb_endpoints();
      setUpstreamAddress(i, *endpoint);
      // First N endpoints are degraded, next M are healthy and the remaining endpoints are
      // unhealthy or unknown depending on remaining_unhealthy.
      if (i < endpoint_setting.degraded_endpoints) {
        endpoint->set_health_status(envoy::config::core::v3::DEGRADED);
      } else if (i >= endpoint_setting.healthy_endpoints + endpoint_setting.degraded_endpoints) {
        endpoint->set_health_status(remaining_unhealthy ? envoy::config::core::v3::UNHEALTHY
                                                        : envoy::config::core::v3::UNKNOWN);
      }
      if (i < endpoint_setting.disable_active_hc_endpoints) {
        endpoint->mutable_endpoint()
            ->mutable_health_check_config()
            ->set_disable_active_health_check(true);
      }
    }

    if (await_update) {
      eds_helper_.setEdsAndWait({cluster_load_assignment}, *test_server_);
    } else {
      eds_helper_.setEds({cluster_load_assignment});
    }
  }

  void initializeTest(
      bool http_active_hc,
      std::function<void(envoy::config::cluster::v3::Cluster& cluster)> cluster_modifier) {
    setUpstreamCount(4);
    if (codec_client_type_ == envoy::type::v3::HTTP2) {
      setUpstreamProtocol(Http::CodecType::HTTP2);
    }
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Switch predefined cluster_0 to CDS filesystem sourcing.
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_resource_api_version(
          envoy::config::core::v3::ApiVersion::V3);
      bootstrap.mutable_dynamic_resources()
          ->mutable_cds_config()
          ->mutable_path_config_source()
          ->set_path(cds_helper_.cds_path());
      bootstrap.mutable_static_resources()->clear_clusters();
    });

    // Set validate_clusters to false to allow us to reference a CDS cluster.
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) { hcm.mutable_route_config()->mutable_validate_clusters()->set_value(false); });

    cluster_.mutable_connect_timeout()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    cluster_.set_name("cluster_0");
    cluster_.set_type(envoy::config::cluster::v3::Cluster::EDS);
    auto* eds_cluster_config = cluster_.mutable_eds_cluster_config();
    eds_cluster_config->mutable_eds_config()->set_resource_api_version(
        envoy::config::core::v3::ApiVersion::V3);
    eds_cluster_config->mutable_eds_config()->mutable_path_config_source()->set_path(
        eds_helper_.eds_path());
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
    setEndpoints({/*total_endpoints=*/0}, true, false);

    if (cluster_modifier != nullptr) {
      cluster_modifier(cluster_);
    }
    cds_helper_.setCds({cluster_});
    initialize();
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  }

  void initializeTest(bool http_active_hc) { initializeTest(http_active_hc, nullptr); }

  envoy::type::v3::CodecClientType codec_client_type_{};
  EdsHelper eds_helper_;
  CdsHelper cds_helper_;
  envoy::config::cluster::v3::Cluster cluster_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, EdsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Validates that endpoints can be added and then moved to other priorities without causing crashes.
// Primarily as a regression test for https://github.com/envoyproxy/envoy/issues/8764
TEST_P(EdsIntegrationTest, Http2UpdatePriorities) {
  codec_client_type_ = envoy::type::v3::HTTP2;
  initializeTest(true);

  setEndpointsInPriorities(2, 2);

  setEndpointsInPriorities(4, 0);

  setEndpointsInPriorities(0, 4);
}

// Verifies that a new cluster can we warmed when using HTTP/2 health checking. Regression test
// of the issue detailed in issue #6951.
TEST_P(EdsIntegrationTest, Http2HcClusterRewarming) {
  codec_client_type_ = envoy::type::v3::HTTP2;
  initializeTest(true);

  // There is 1 total endpoint.
  setEndpoints(EndpointSettingOptions(), false);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy. This should warm the initial cluster.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
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
  auto result = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection);
  RELEASE_ASSERT(result, result.message());

  FakeStreamPtr upstream_request;
  result = fake_upstream_connection->waitForNewStream(*dispatcher_, upstream_request);
  RELEASE_ASSERT(result, result.message());
  // Wait for the stream to be completely received.
  result = upstream_request->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  // Respond with a health check. This will cause the previous cluster to be destroyed inline as
  // part of processing the response.
  upstream_request->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
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

TEST_P(EdsIntegrationTest, EndpointDisableActiveHCFlag) {
  initializeTest(true);
  EndpointSettingOptions options;

  // Total 1 endpoints with all active health check enabled.
  setEndpoints(options, false);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Disable Active Healthy Check for the host. EDS Unknown is considered as healthy
  options.disable_active_hc_endpoints = 1;
  setEndpoints(options, false);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Set the host as unhealthy through EDS.
  setEndpoints(options, true);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Set host as healthy through EDS.
  options.healthy_endpoints = 1;
  setEndpoints(options, false);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Clear out the host and verify the host is gone.
  setEndpoints({/*total_endpoints=*/0});
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_total")->value());
}

// Verify that a host stabilized via active health checking which is first removed from EDS and
// then fails health checking is removed.
TEST_P(EdsIntegrationTest, RemoveAfterHcFail) {
  initializeTest(true);
  EndpointSettingOptions options;
  setEndpoints(options, false);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());

  // Clear out the host and verify the host is still healthy.
  options.total_endpoints = 0;
  setEndpoints(options);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Fail HC and verify the host is gone.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "503"}, {"connection", "close"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 0);
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_total")->value());
}

// Verifies that cluster warming proceeds even if a host is deleted before health checks complete.
// This is a regression test for https://github.com/envoyproxy/envoy/issues/17836.
TEST_P(EdsIntegrationTest, FinishWarmingIgnoreHealthCheck) {
  codec_client_type_ = envoy::type::v3::HTTP2;
  initializeTest(true, [](envoy::config::cluster::v3::Cluster& cluster) {
    cluster.set_ignore_health_on_host_removal(true);
  });
  EndpointSettingOptions options;
  setEndpoints(options, false);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster_manager.warming_clusters")->value());

  // Trigger a CDS update. This should cause a new cluster to require warming, blocked on the host
  // being health checked.
  cluster_.mutable_circuit_breakers()->add_thresholds()->mutable_max_connections()->set_value(100);
  cds_helper_.setCds({cluster_});
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);

  // Clear out the host before the health check finishes (regardless of success/error/timeout) and
  // ensure that warming_clusters goes to 0 to avoid a permanent warming state.
  options.total_endpoints = 0;
  setEndpoints(options, true, false);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
}

// Verifies that endpoints are ignored until health checked when configured to.
TEST_P(EdsIntegrationTest, EndpointWarmingSuccessfulHc) {
  cluster_.mutable_common_lb_config()->set_ignore_new_hosts_until_first_hc(true);

  // Endpoints are initially excluded.
  initializeTest(true);
  setEndpoints(EndpointSettingOptions(), false);

  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_excluded")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy and that it is no longer being excluded.
  // The other endpoint should still be excluded.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
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
  setEndpoints(EndpointSettingOptions(), false);

  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_excluded")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy and that it is no longer being excluded.
  // The other endpoint should still be excluded.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_excluded", 0);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Validate that health status updates are consumed from EDS.
TEST_P(EdsIntegrationTest, HealthUpdate) {
  initializeTest(false);
  EndpointSettingOptions options;
  // Initial state, no cluster members.
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // 2/2 healthy endpoints.
  options.total_endpoints = 2;
  options.healthy_endpoints = 2;
  setEndpoints(options);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Drop to 0/2 healthy endpoints.
  options.healthy_endpoints = 0;
  setEndpoints(options);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Increase to 1/2 healthy endpoints.
  options.healthy_endpoints = 1;
  setEndpoints(options);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Add host and modify health to 2/3 healthy endpoints.
  options.total_endpoints = 3;
  options.healthy_endpoints = 2;
  setEndpoints(options);
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Modify health to 2/3 healthy and 1/3 degraded.
  options.degraded_endpoints = 1;
  setEndpoints(options);
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_degraded")->value());
}

// Validate that overprovisioning_factor update are picked up by Envoy.
TEST_P(EdsIntegrationTest, OverprovisioningFactorUpdate) {
  initializeTest(false);
  // Default overprovisioning factor.
  EndpointSettingOptions options;
  options.total_endpoints = 4;
  options.healthy_endpoints = 4;
  setEndpoints(options);
  auto get_and_compare = [this](const uint32_t expected_factor) {
    const auto& cluster_map = test_server_->server().clusterManager().clusters();
    EXPECT_EQ(1, cluster_map.active_clusters_.size());
    EXPECT_EQ(1, cluster_map.active_clusters_.count("cluster_0"));
    const auto& cluster_ref = cluster_map.active_clusters_.find("cluster_0")->second;
    const auto& hostset_per_priority = cluster_ref.get().prioritySet().hostSetsPerPriority();
    EXPECT_EQ(1, hostset_per_priority.size());
    const Envoy::Upstream::HostSetPtr& host_set = hostset_per_priority[0];
    EXPECT_EQ(expected_factor, host_set->overprovisioningFactor());
  };
  get_and_compare(Envoy::Upstream::kDefaultOverProvisioningFactor);

  // Use new overprovisioning factor 200.
  options.overprovisioning_factor = 200;
  setEndpoints(options);
  get_and_compare(200);
}

// Verifies that EDS update only triggers member update callbacks once per update.
TEST_P(EdsIntegrationTest, BatchMemberUpdateCb) {
  initializeTest(false);

  uint32_t member_update_count{};

  auto& priority_set = test_server_->server()
                           .clusterManager()
                           .clusters()
                           .active_clusters_.find("cluster_0")
                           ->second.get()
                           .prioritySet();

  // Keep track of how many times we're seeing a member update callback.
  auto member_update_cb = priority_set.addMemberUpdateCb([&](const auto& hosts_added, const auto&) {
    // We should see both hosts present in the member update callback.
    EXPECT_EQ(2, hosts_added.size());
    member_update_count++;
  });

  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
  config_helper_.prependFilter("name: eds-ready-filter");
  initializeTest(false);

  // Initial state: no healthy endpoints
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/cluster1", "", downstream_protocol_, version_, "foo.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
  EXPECT_EQ("EDS not ready", response->body());

  cleanupUpstreamAndDownstream();

  // 2/2 healthy endpoints.
  setEndpoints({/*total_endpoint*/ 2, /*healthy_endpoints*/ 2});
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  response = IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/cluster1", "",
                                                downstream_protocol_, version_, "foo.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("EDS is ready", response->body());

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy

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

  // We need to supply the endpoints via EDS to provide health status. Use a
  // filesystem delivery to simplify test mechanics.
  void setEndpoints(uint32_t total_endpoints, uint32_t healthy_endpoints,
                    uint32_t degraded_endpoints, bool remaining_unhealthy = true,
                    absl::optional<uint32_t> overprovisioning_factor = absl::nullopt,
                    bool await_update = true) {
    ASSERT(total_endpoints >= healthy_endpoints + degraded_endpoints);
    envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
        endpoint->set_health_status(envoy::config::core::v3::DEGRADED);
      } else if (i >= healthy_endpoints + degraded_endpoints) {
        endpoint->set_health_status(remaining_unhealthy ? envoy::config::core::v3::UNHEALTHY
                                                        : envoy::config::core::v3::UNKNOWN);
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
    setEndpoints(0, 0, 0, true, absl::nullopt, false);

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
  setEndpoints(1, 0, 0, false);
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

// Verify that a host stabilized via active health checking which is first removed from EDS and
// then fails health checking is removed.
TEST_P(EdsIntegrationTest, RemoveAfterHcFail) {
  initializeTest(true);
  setEndpoints(1, 0, 0, false);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());

  // Clear out the host and verify the host is still healthy.
  setEndpoints(0, 0, 0);
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
  setEndpoints(1, 0, 0, false);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster_manager.warming_clusters")->value());

  // Trigger a CDS update. This should cause a new cluster to require warming, blocked on the
  // host being health checked.
  cluster_.mutable_circuit_breakers()->add_thresholds()->mutable_max_connections()->set_value(100);
  cds_helper_.setCds({cluster_});
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);

  // Clear out the host before the health check finishes (regardless of success/error/timeout)
  // and ensure that warming_clusters goes to 0 to avoid a permanent warming state.
  setEndpoints(0, 0, 0, true, absl::nullopt, false);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
}

// Verifies that endpoints are ignored until health checked when configured to.
TEST_P(EdsIntegrationTest, EndpointWarmingSuccessfulHc) {
  cluster_.mutable_common_lb_config()->set_ignore_new_hosts_until_first_hc(true);

  // Endpoints are initially excluded.
  initializeTest(true);
  setEndpoints(1, 0, 0, false);

  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_excluded")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy and that it is no longer being
  // excluded. The other endpoint should still be excluded.
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
  setEndpoints(1, 0, 0, false);

  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_excluded")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy and that it is no longer being
  // excluded. The other endpoint should still be excluded.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
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
  setEndpoints(2, 2, 0);
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  response = IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/cluster1", "",
                                                downstream_protocol_, version_, "foo.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("EDS is ready", response->body());

  cleanupUpstreamAndDownstream();
}

class EdsOverGrpcIntegrationTest : public Grpc::EdsModeDeltaSotwIntegrationParamTest,
                                   public HttpIntegrationTest {
protected:
  struct FakeUpstreamInfo {
    FakeHttpConnectionPtr connection_;
    FakeUpstream* upstream_{};
    absl::flat_hash_map<std::string, FakeStreamPtr> stream_by_resource_name_;
    static constexpr char default_stream_name[] = "default";
    // Used for cases where only a single stream is needed.
    FakeStreamPtr& defaultStream() { return stream_by_resource_name_[default_stream_name]; }
  };

  EdsOverGrpcIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion()),
        codec_client_type_(envoy::type::v3::HTTP2) {
    use_lds_ = true;
    test_skipped = false;
  }

  void TearDown() override {
    if (!test_skipped) {
      resetConnections();
      cleanUpXdsConnection();
    }
  }

  void resetConnections() {
    // First disconnect upstream connections to avoid FIN messages causing unexpected
    // disconnects on the fake servers.
    for (auto& host_upstream_info : hosts_upstreams_info_) {
      resetFakeUpstreamInfo(host_upstream_info);
    }
  }

  // A helper function to set the endpoints health status.
  void setEndpointsHealthStatus(
      const absl::flat_hash_set<uint32_t>& endpoints_idxs,
      envoy::config::core::v3::HealthStatus health_status, absl::string_view collection_prefix,
      absl::flat_hash_map<std::string, envoy::config::endpoint::v3::LbEndpoint>&
          updated_endpoints) {
    for (const auto endpoint_idx : endpoints_idxs) {
      const std::string endpoint_name = absl::StrCat(collection_prefix, "endpoint", endpoint_idx);
      envoy::config::endpoint::v3::LbEndpoint endpoint;
      // Shift fake_upstreams_ by 1 (due to EDS fake upstream).
      setUpstreamAddress(endpoint_idx + 1, endpoint);
      endpoint.set_health_status(health_status);
      updated_endpoints.emplace(endpoint_name, endpoint);
    }
  }

  void setEndpoints(uint32_t total_endpoints, uint32_t healthy_endpoints,
                    uint32_t degraded_endpoints, bool remaining_unhealthy = true,
                    absl::optional<uint32_t> overprovisioning_factor = absl::nullopt,
                    bool await_update = true) {
    envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
    cluster_load_assignment.set_cluster_name("cluster_0");
    if (overprovisioning_factor.has_value()) {
      cluster_load_assignment.mutable_policy()->mutable_overprovisioning_factor()->set_value(
          overprovisioning_factor.value());
    }
    auto* locality_lb_endpoints = cluster_load_assignment.add_endpoints();
    locality_lb_endpoints->set_priority(1);
    for (uint32_t i = 0; i < total_endpoints; ++i) {
      auto* endpoint = locality_lb_endpoints->add_lb_endpoints();
      // Skip EDS upstream.
      setUpstreamAddress(i + 1, *endpoint);
      // First N endpoints are degraded, next M are healthy and the remaining endpoints are
      // unhealthy or unknown depending on remaining_unhealthy.
      if (i < degraded_endpoints) {
        endpoint->set_health_status(envoy::config::core::v3::DEGRADED);
      } else if (i >= healthy_endpoints + degraded_endpoints) {
        endpoint->set_health_status(remaining_unhealthy ? envoy::config::core::v3::UNHEALTHY
                                                        : envoy::config::core::v3::UNKNOWN);
      }
    }
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::TypeUrl::get().ClusterLoadAssignment, {cluster_load_assignment},
        {cluster_load_assignment}, {}, std::to_string(eds_version_++));
    if (await_update) {
      // Receive EDS ack.
      EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment,
                                          std::to_string(eds_version_ - 1), {}, {}, {}, true));
    }
  }

  void createUpstreams() override {
    // Add EDS upstream.
    addFakeUpstream(Http::CodecType::HTTP2);
    HttpIntegrationTest::createUpstreams();
    hosts_upstreams_info_.reserve(fake_upstreams_count_);
    // Skip the first fake upstream as it is reserved for EDS.
    for (size_t i = 1; i < fake_upstreams_.size(); ++i) {
      FakeUpstreamInfo host_info;
      host_info.upstream_ = &(*fake_upstreams_[i]);
      hosts_upstreams_info_.emplace_back(std::move(host_info));
    }
  }

  void initializeTest(bool http_active_hc, uint32_t num_clusters_to_add,
                      bool ignore_new_hosts_until_first_hc) {
    setUpstreamCount(4);
    setUpstreamProtocol(Http::CodecType::HTTP2);

    config_helper_.addConfigModifier([this, http_active_hc, num_clusters_to_add,
                                      ignore_new_hosts_until_first_hc](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add a static EDS cluster.
      auto* eds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      eds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      eds_cluster->set_name("eds_cluster");
      eds_cluster->mutable_load_assignment()->set_cluster_name("eds_cluster");
      ConfigHelper::setHttp2(*eds_cluster);
      // Remove the static cluster (cluster_0) and set up CDS.
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_resource_api_version(
          envoy::config::core::v3::ApiVersion::V3);
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_path(cds_helper_.cds_path());
      bootstrap.mutable_static_resources()->mutable_clusters()->erase(
          bootstrap.mutable_static_resources()->mutable_clusters()->begin());
      for (uint32_t i = 0; i < num_clusters_to_add; ++i) {
        auto cluster_name = "cluster_" + std::to_string(i);
        auto cluster_to_add =
            buildCluster(cluster_name, http_active_hc, ignore_new_hosts_until_first_hc);
        envoy::config::endpoint::v3::ClusterLoadAssignment cla_to_add;
        cla_to_add.set_cluster_name(cluster_name);
        clas_to_add_.push_back(cla_to_add);
        clusters_to_add_.push_back(cluster_to_add);
        resource_names_.push_back(cluster_name);
      }
      cds_helper_.setCds(clusters_to_add_);
    });
    // Set validate_clusters to false to allow us to reference a CDS cluster.
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) { hcm.mutable_route_config()->mutable_validate_clusters()->set_value(false); });
    defer_listener_finalization_ = true;
    HttpIntegrationTest::initialize();
    // Add the assignment and localities.
    cluster_load_assignment_.set_cluster_name("cluster_0");
    acceptXdsConnection();
    for (uint32_t i = 0; i < num_clusters_to_add; ++i) {
      EXPECT_TRUE(compareDiscoveryRequest(
          Config::TypeUrl::get().ClusterLoadAssignment, "",
          {resource_names_.begin(), resource_names_.begin() + i + 1}, {}, {}, true));
    }
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::TypeUrl::get().ClusterLoadAssignment, clas_to_add_, clas_to_add_, {},
        std::to_string(eds_version_));
    // Receive EDS ack.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment,
                                        std::to_string(eds_version_), {}, {}, {}, true));
    eds_version_++;
    // Wait for our statically specified listener to become ready, and register its port in the
    // test framework's downstream listener port map.
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  }

  envoy::config::cluster::v3::Cluster buildCluster(const std::string& name, bool http_active_hc,
                                                   bool ignore_new_hosts_until_first_hc) {
    envoy::config::cluster::v3::Cluster cluster;
    if (ignore_new_hosts_until_first_hc) {
      cluster.mutable_common_lb_config()->set_ignore_new_hosts_until_first_hc(true);
    }
    cluster.set_name(name);
    cluster.set_type(envoy::config::cluster::v3::Cluster::EDS);
    cluster.mutable_connect_timeout()->CopyFrom(Protobuf::util::TimeUtil::SecondsToDuration(5));
    auto* eds_cluster_config = cluster.mutable_eds_cluster_config();
    eds_cluster_config->mutable_eds_config()->set_resource_api_version(
        envoy::config::core::v3::ApiVersion::V3);
    if (edsUpdateMode() == Grpc::EdsUpdateMode::Multiplexed) {
      eds_cluster_config->set_multiplex_eds(true);
    }
    auto* api_config_source = eds_cluster_config->mutable_eds_config()->mutable_api_config_source();
    api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* grpc_service = api_config_source->add_grpc_services();
    setGrpcService(*grpc_service, "eds_cluster", fake_upstreams_[0]->localAddress());
    if (http_active_hc) {
      auto* health_check = cluster.add_health_checks();
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
    return cluster;
  }

  void acceptXdsConnection() {
    AssertionResult result = // xds_connection_ is filled with the new FakeHttpConnection.
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }

  void resetFakeUpstreamInfo(FakeUpstreamInfo& upstream_info) {
    if (upstream_info.connection_ == nullptr || upstream_info.upstream_ == nullptr) {
      upstream_info.upstream_ = nullptr;
      return;
    }
    AssertionResult result = upstream_info.connection_->close();
    RELEASE_ASSERT(result, result.message());
    result = upstream_info.connection_->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
    upstream_info.connection_.reset();
    upstream_info.upstream_ = nullptr;
  }

  void waitForHealthCheck(uint32_t upstream_info_idx) {
    auto& host_info = hosts_upstreams_info_[upstream_info_idx];
    if (host_info.connection_ == nullptr) {
      ASSERT_TRUE(host_info.upstream_->waitForHttpConnection(*dispatcher_, host_info.connection_));
    }
    ASSERT_TRUE(host_info.connection_->waitForNewStream(*dispatcher_, host_info.defaultStream()));
    ASSERT_TRUE(host_info.defaultStream()->waitForEndStream(*dispatcher_));

    EXPECT_EQ(host_info.defaultStream()->headers().getPathValue(), "/healthcheck");
    EXPECT_EQ(host_info.defaultStream()->headers().getMethodValue(), "GET");
  }

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

    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::TypeUrl::get().ClusterLoadAssignment, {cluster_load_assignment},
        {cluster_load_assignment}, {}, std::to_string(eds_version_++));

    if (await_update) {
      // Receive EDS ack.
      EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment,
                                          std::to_string(eds_version_ - 1), {}, {}, {}, true));
    }
  }

  void initializeTest(bool http_active_hc) { initializeTest(http_active_hc, 1, false); }

  envoy::type::v3::CodecClientType codec_client_type_{};
  CdsHelper cds_helper_;
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment_;
  std::vector<envoy::config::cluster::v3::Cluster> clusters_to_add_;
  std::vector<envoy::config::endpoint::v3::ClusterLoadAssignment> clas_to_add_;
  std::vector<std::string> resource_names_;
  std::vector<FakeUpstreamInfo> hosts_upstreams_info_;
  uint32_t eds_version_{};
  bool test_skipped{false};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeSotwOrDeltaEdsMode, EdsOverGrpcIntegrationTest,
                         EDS_MODE_DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// Validates that endpoints can be added and then moved to other priorities without causing crashes.
// Primarily as a regression test for https://github.com/envoyproxy/envoy/issues/8764
TEST_P(EdsOverGrpcIntegrationTest, Http2UpdatePriorities) {
  initializeTest(true);
  setEndpointsInPriorities(2, 2);
  setEndpointsInPriorities(4, 0);
  setEndpointsInPriorities(0, 4);
}

// Verify that a host stabilized via active health checking which is first removed from EDS and
// then fails health checking is removed.
TEST_P(EdsOverGrpcIntegrationTest, RemoveAfterHcFail) {
  initializeTest(true);
  setEndpoints(1, 0, 0, false);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Wait for the first HC and verify the host is healthy.
  waitForHealthCheck(0);
  hosts_upstreams_info_[0].defaultStream()->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  // Clear out the host and verify the host is still healthy.
  setEndpoints(0, 0, 0);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Fail HC and verify the host is gone.
  waitForHealthCheck(0);
  hosts_upstreams_info_[0].defaultStream()->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "503"}, {"connection", "close"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 0);
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_total")->value());
}

// Verifies that endpoints are ignored until health checked when configured to.
TEST_P(EdsOverGrpcIntegrationTest, EndpointWarmingSuccessfulHc) {
  // Endpoints are initially excluded.
  initializeTest(true, 1, true);
  setEndpoints(1, 0, 0, false);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_excluded")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Wait for the first HC and verify the host is healthy and that it is no longer being
  // excluded.
  // The other endpoint should still be excluded.
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_excluded", 0);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Verifies that endpoints are ignored until health checked when configured to when the first
// health check fails.
TEST_P(EdsOverGrpcIntegrationTest, EndpointWarmingFailedHc) {
  // Endpoints are initially excluded.
  initializeTest(true, 1, true);
  setEndpoints(1, 0, 0, false);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_excluded")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy and that it is no longer being
  // excluded.
  // The other endpoint should still be excluded.
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_excluded", 0);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Validate that health status updates are consumed from EDS.
TEST_P(EdsOverGrpcIntegrationTest, HealthUpdate) {
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
TEST_P(EdsOverGrpcIntegrationTest, OverprovisioningFactorUpdate) {
  initializeTest(false);
  // Default overprovisioning factor.
  setEndpoints(4, 4, 0);
  auto get_and_compare = [this](const uint32_t expected_factor) {
    const auto& cluster_map = test_server_->server().clusterManager().clusters();
    EXPECT_EQ(2, cluster_map.active_clusters_.size());
    EXPECT_EQ(1, cluster_map.active_clusters_.count("cluster_0"));
    const auto& cluster_ref = cluster_map.active_clusters_.find("cluster_0")->second;
    const auto& hostset_per_priority = cluster_ref.get().prioritySet().hostSetsPerPriority();
    EXPECT_EQ(2, hostset_per_priority.size());
    const Envoy::Upstream::HostSetPtr& host_set = hostset_per_priority[0];
    EXPECT_EQ(expected_factor, host_set->overprovisioningFactor());
  };
  get_and_compare(Envoy::Upstream::kDefaultOverProvisioningFactor);
  // Use new overprovisioning factor 200.
  setEndpoints(4, 4, 0, true, 200);
  get_and_compare(200);
}

// Verifies that EDS update only triggers member update callbacks once per update.
TEST_P(EdsOverGrpcIntegrationTest, BatchMemberUpdateCb) {
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
  setEndpoints(2, 2, 0);
  EXPECT_EQ(1, member_update_count);
}

TEST_P(EdsOverGrpcIntegrationTest, StatsReadyFilter) {
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
  setEndpoints(2, 2, 0);
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  response = IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/cluster1", "",
                                                downstream_protocol_, version_, "foo.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("EDS is ready", response->body());
  cleanupUpstreamAndDownstream();
}

TEST_P(EdsOverGrpcIntegrationTest, ReuseMuxAndStreamForMultipleClusters) {
  if (edsUpdateMode() == Grpc::EdsUpdateMode::Multiplexed) {
    initializeTest(false, 3, false);
    EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_total")->value());
    EXPECT_EQ(0, test_server_->gauge("cluster.cluster_1.membership_total")->value());
    EXPECT_EQ(0, test_server_->gauge("cluster.cluster_2.membership_total")->value());
    switch (clientType()) {
    case Grpc::ClientType::EnvoyGrpc:
      // As EDS uses HTTP2, number of streams created by Envoy for EDS cluster equals to number
      // of requests.
      EXPECT_EQ(1UL, test_server_->counter("cluster.eds_cluster.upstream_rq_total")->value());
      break;
    case Grpc::ClientType::GoogleGrpc:
      // One EDS mux/stream is created and reused for all 3 clusters when initializing first EDS
      // cluster (cluster_0). As a consequence, only one Google async grpc client and one
      // corresponding set of client stats should be created.
      EXPECT_EQ(1UL,
                test_server_->counter("cluster.cluster_0.grpc.eds_cluster.streams_total")->value());
      EXPECT_EQ(TestUtility::findCounter(test_server_->statStore(),
                                         "cluster.cluster_1.grpc.eds_cluster.streams_total"),
                nullptr);
      EXPECT_EQ(TestUtility::findCounter(test_server_->statStore(),
                                         "cluster.cluster_2.grpc.eds_cluster.streams_total"),
                nullptr);
      break;
    default:
      PANIC("reached unexpected code");
    }
  } else {
    test_skipped = true;
  }
}

} // namespace
} // namespace Envoy

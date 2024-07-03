#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/type/v3/http.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Integration test for LEDS features. CDS is consumed vi filesystem subscription,
// and EDS and LEDS are consumed via using the delta-xDS gRPC protocol.
class LedsIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
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

  LedsIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion()),
        codec_client_type_(envoy::type::v3::HTTP1) {
    use_lds_ = false;
    create_xds_upstream_ = false;
    // LEDS is only supported by delta-xDS.
    sotw_or_delta_ = Grpc::SotwOrDelta::Delta;
  }

  ~LedsIntegrationTest() override {
    // First disconnect upstream connections to avoid FIN messages causing unexpected
    // disconnects on the fake servers.
    for (auto& host_upstream_info : hosts_upstreams_info_) {
      resetFakeUpstreamInfo(host_upstream_info);
    }

    resetFakeUpstreamInfo(leds_upstream_info_);
    resetFakeUpstreamInfo(eds_upstream_info_);
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
      // Shift fake_upstreams_ by 2 (due to EDS and LEDS fake upstreams).
      setUpstreamAddress(endpoint_idx + 2, endpoint);
      endpoint.set_health_status(health_status);
      updated_endpoints.emplace(endpoint_name, endpoint);
    }
  }

  // Sets endpoints in a specific locality (using the LEDS helper).
  // We need to supply the endpoints via LEDS to provide health status. Use a
  // filesystem delivery to simplify test mechanics.
  void setEndpoints(const absl::flat_hash_set<uint32_t>& healthy_endpoints_idxs,
                    const absl::flat_hash_set<uint32_t>& degraded_endpoints_idxs,
                    const absl::flat_hash_set<uint32_t>& unhealthy_endpoints_idxs,
                    const absl::flat_hash_set<uint32_t>& unknown_endpoints_idxs,
                    const absl::flat_hash_set<uint32_t>& removed_endpoints_idxs,
                    uint32_t locality_idx = 0, bool await_update = true) {
    const auto& collection_prefix = localities_prefixes_[locality_idx];
    absl::flat_hash_map<std::string, envoy::config::endpoint::v3::LbEndpoint> updated_endpoints;
    std::vector<std::string> removed_endpoints;
    setEndpointsHealthStatus(healthy_endpoints_idxs, envoy::config::core::v3::HEALTHY,
                             collection_prefix, updated_endpoints);
    setEndpointsHealthStatus(degraded_endpoints_idxs, envoy::config::core::v3::DEGRADED,
                             collection_prefix, updated_endpoints);
    setEndpointsHealthStatus(unhealthy_endpoints_idxs, envoy::config::core::v3::UNHEALTHY,
                             collection_prefix, updated_endpoints);
    setEndpointsHealthStatus(unknown_endpoints_idxs, envoy::config::core::v3::UNKNOWN,
                             collection_prefix, updated_endpoints);

    for (const auto removed_endpoint_idx : removed_endpoints_idxs) {
      const std::string endpoint_name =
          absl::StrCat(collection_prefix, "endpoint", removed_endpoint_idx);
      removed_endpoints.emplace_back(endpoint_name);
    }

    sendDeltaLedsResponse(updated_endpoints, removed_endpoints, "7", locality_idx);

    if (await_update) {
      // Receive LEDS ack.
      EXPECT_TRUE(compareDeltaDiscoveryRequest(
          Config::TypeUrl::get().LbEndpoint, {}, {},
          leds_upstream_info_.stream_by_resource_name_[localities_prefixes_[locality_idx]].get()));
    }
  }

  // Sends an LEDS response to a specific locality, containing the updated
  // endpoints map (resource name to endpoint data), and the list of resource
  // names to remove.
  void sendDeltaLedsResponse(
      const absl::flat_hash_map<std::string, envoy::config::endpoint::v3::LbEndpoint>&
          to_update_map,
      const std::vector<std::string>& to_delete_list, const std::string& version,
      uint32_t locality_idx) {
    auto& locality_stream =
        leds_upstream_info_.stream_by_resource_name_[localities_prefixes_[locality_idx]];
    ASSERT(locality_stream != nullptr);
    envoy::service::discovery::v3::DeltaDiscoveryResponse response;
    response.set_system_version_info(version);
    response.set_type_url(Config::TypeUrl::get().LbEndpoint);

    for (const auto& endpoint_name : to_delete_list) {
      *response.add_removed_resources() = endpoint_name;
    }
    for (const auto& [resource_name, lb_endpoint] : to_update_map) {
      auto* resource = response.add_resources();
      resource->set_name(resource_name);
      resource->set_version(version);
      resource->mutable_resource()->PackFrom(lb_endpoint);
    }
    locality_stream->sendGrpcMessage(response);
  }

  void createUpstreams() override {
    // Add the EDS upstream.
    eds_upstream_info_.upstream_ = &addFakeUpstream(FakeHttpConnection::Type::HTTP2);
    // Add the LEDS upstream.
    leds_upstream_info_.upstream_ = &addFakeUpstream(FakeHttpConnection::Type::HTTP2);

    // Create backends and initialize their wrapper.
    HttpIntegrationTest::createUpstreams();
    // Store all hosts upstreams info in a single place so it would be easily
    // accessible.
    ASSERT(fake_upstreams_.size() == fake_upstreams_count_ + 2);
    hosts_upstreams_info_.reserve(fake_upstreams_count_);
    // Skip the first 2 fake upstreams as they are reserved for EDS and LEDS.
    for (size_t i = 2; i < fake_upstreams_.size(); ++i) {
      FakeUpstreamInfo host_info;
      host_info.upstream_ = &(*fake_upstreams_[i]);
      hosts_upstreams_info_.emplace_back(std::move(host_info));
    }
  }

  // Initialize a gRPC stream of an upstream server.
  void initializeStream(FakeUpstreamInfo& upstream_info,
                        const std::string& resource_name = FakeUpstreamInfo::default_stream_name) {
    if (!upstream_info.connection_) {
      auto result =
          upstream_info.upstream_->waitForHttpConnection(*dispatcher_, upstream_info.connection_);
      RELEASE_ASSERT(result, result.message());
    }
    if (!upstream_info.stream_by_resource_name_.try_emplace(resource_name, nullptr).second) {
      RELEASE_ASSERT(false,
                     fmt::format("stream with resource name '{}' already exists!", resource_name));
    }

    auto result = upstream_info.connection_->waitForNewStream(
        *dispatcher_, upstream_info.stream_by_resource_name_[resource_name]);
    RELEASE_ASSERT(result, result.message());
    upstream_info.stream_by_resource_name_[resource_name]->startGrpcStream();
  }

  // A specific function to initialize LEDS streams. This was introduced to
  // handle the non-deterministic requests order when more than one locality is
  // used. This method first establishes the gRPC stream, fetches the first
  // request and reads its requested resource name, and then assigns the stream
  // to the internal data-structure.
  void initializeAllLedsStreams() {
    // Create a set of localities that are expected.
    absl::flat_hash_set<std::string> expected_localities_prefixes(localities_prefixes_.begin(),
                                                                  localities_prefixes_.end());

    if (!leds_upstream_info_.connection_) {
      auto result = leds_upstream_info_.upstream_->waitForHttpConnection(
          *dispatcher_, leds_upstream_info_.connection_);
      RELEASE_ASSERT(result, result.message());
    }

    // Wait for the exact number of streams.
    for (uint32_t i = 0; i < localities_prefixes_.size(); ++i) {
      // Create the stream for the LEDS collection and fetch the name from the
      // contents, then validate that this is an expected collection
      FakeStreamPtr temp_stream;
      envoy::service::discovery::v3::DeltaDiscoveryRequest request;
      auto result = leds_upstream_info_.connection_->waitForNewStream(*dispatcher_, temp_stream);
      RELEASE_ASSERT(result, result.message());
      temp_stream->startGrpcStream();
      RELEASE_ASSERT(temp_stream->waitForGrpcMessage(*dispatcher_, request),
                     "LEDS message did not arrive as expected");
      RELEASE_ASSERT(request.resource_names_subscribe().size() == 1,
                     "Each LEDS request in this test must have a single resource");
      // Remove the "*" from the collection name to match against the set
      // contents.
      const auto request_collection_name = *request.resource_names_subscribe().begin();
      const auto pos = request_collection_name.find_last_of('*');
      ASSERT(pos != std::string::npos);
      const auto request_collection_prefix = request_collection_name.substr(0, pos);
      auto set_it = expected_localities_prefixes.find(request_collection_prefix);
      ASSERT(set_it != expected_localities_prefixes.end());
      // Associate the stream with the locality prefix.
      leds_upstream_info_.stream_by_resource_name_[*set_it] = std::move(temp_stream);
      // Remove the locality prefix from the expected set.
      expected_localities_prefixes.erase(set_it);
    }
  }

  void initializeTest(bool http_active_hc, uint32_t localities_num = 1) {
    // Set up a single upstream host for the LEDS cluster using HTTP2 (gRPC).
    setUpstreamCount(4);

    config_helper_.addConfigModifier([this, http_active_hc](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add a static EDS cluster.
      auto* eds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      eds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      eds_cluster->set_name("eds_cluster");
      eds_cluster->mutable_load_assignment()->set_cluster_name("eds_cluster");
      ConfigHelper::setHttp2(*eds_cluster);

      // Add a static LEDS cluster.
      auto* leds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      leds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      leds_cluster->set_name("leds_cluster");
      leds_cluster->mutable_load_assignment()->set_cluster_name("leds_cluster");
      ConfigHelper::setHttp2(*leds_cluster);

      // Remove the static cluster (cluster_0) and set up CDS.
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_resource_api_version(
          envoy::config::core::v3::ApiVersion::V3);
      bootstrap.mutable_dynamic_resources()
          ->mutable_cds_config()
          ->mutable_path_config_source()
          ->set_path(cds_helper_.cdsPath());
      bootstrap.mutable_static_resources()->mutable_clusters()->erase(
          bootstrap.mutable_static_resources()->mutable_clusters()->begin());

      // Set the default static cluster to use EDS.
      auto& cluster_0 = cluster_;
      cluster_0.set_name("cluster_0");
      cluster_0.set_type(envoy::config::cluster::v3::Cluster::EDS);
      cluster_0.mutable_connect_timeout()->CopyFrom(Protobuf::util::TimeUtil::SecondsToDuration(5));
      auto* eds_cluster_config = cluster_0.mutable_eds_cluster_config();
      eds_cluster_config->mutable_eds_config()->set_resource_api_version(
          envoy::config::core::v3::ApiVersion::V3);
      auto* api_config_source =
          eds_cluster_config->mutable_eds_config()->mutable_api_config_source();
      api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
      api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
      auto* grpc_service = api_config_source->add_grpc_services();
      setGrpcService(*grpc_service, "eds_cluster", eds_upstream_info_.upstream_->localAddress());
      if (http_active_hc) {
        auto* health_check = cluster_0.add_health_checks();
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
      // Set the cluster using CDS.
      cds_helper_.setCds({cluster_});
    });

    // Set validate_clusters to false to allow us to reference a CDS cluster.
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) { hcm.mutable_route_config()->mutable_validate_clusters()->set_value(false); });

    defer_listener_finalization_ = true;
    initialize();

    EXPECT_EQ(1, test_server_->gauge("cluster_manager.warming_clusters")->value());
    EXPECT_EQ(2, test_server_->gauge("cluster_manager.active_clusters")->value());

    // Create the EDS connection and stream.
    initializeStream(eds_upstream_info_);
    // Add the assignment and localities.
    cluster_load_assignment_.set_cluster_name("cluster_0");
    localities_prefixes_.reserve(localities_num);
    for (uint32_t locality_idx = 0; locality_idx < localities_num; ++locality_idx) {
      // Setup per locality LEDS config over gRPC.
      auto* locality_lb_endpoints = cluster_load_assignment_.add_endpoints();
      locality_lb_endpoints->set_priority(locality_idx);
      auto* leds_locality_config = locality_lb_endpoints->mutable_leds_cluster_locality_config();
      auto* leds_config = leds_locality_config->mutable_leds_config();

      leds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      auto* api_config_source = leds_config->mutable_api_config_source();
      api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
      api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
      auto* grpc_service = api_config_source->add_grpc_services();
      setGrpcService(*grpc_service, "leds_cluster", leds_upstream_info_.upstream_->localAddress());

      const std::string locality_endpoints_prefix = fmt::format(
          "xdstp://test/envoy.config.endpoint.v3.LbEndpoint/cluster0/locality{}/", locality_idx);
      localities_prefixes_.push_back(locality_endpoints_prefix);
      const std::string locality_endpoints_collection_name =
          absl::StrCat(locality_endpoints_prefix, "*");
      leds_locality_config->set_leds_collection_name(locality_endpoints_collection_name);
    }

    EXPECT_EQ(1, test_server_->gauge("cluster_manager.warming_clusters")->value());
    EXPECT_EQ(2, test_server_->gauge("cluster_manager.active_clusters")->value());

    // Do the initial compareDiscoveryRequest / sendDiscoveryResponse for cluster_'s localities
    // (ClusterLoadAssignment).
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment,
                                             {"cluster_0"}, {},
                                             eds_upstream_info_.defaultStream().get()));
    sendDeltaDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::TypeUrl::get().ClusterLoadAssignment, {cluster_load_assignment_}, {}, "2",
        eds_upstream_info_.defaultStream().get());

    // Receive EDS ack.
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, {}, {},
                                             eds_upstream_info_.defaultStream().get()));

    EXPECT_EQ(1, test_server_->gauge("cluster_manager.warming_clusters")->value());
    EXPECT_EQ(2, test_server_->gauge("cluster_manager.active_clusters")->value());

    // Create the LEDS connection and stream(s).
    // Wait for all the LEDS streams to be established. Note that if more
    // than one locality has issued a LEDS request, the order of the requests
    // can be non-deterministic (e.g., the request for "locality1" might be
    // received before the request for "locality0"). Therefore we first wait
    // for all the streams to be established, and only then verify that all the
    // requests arrived as expected.
    initializeAllLedsStreams();

    EXPECT_EQ(1, test_server_->gauge("cluster_manager.warming_clusters")->value());
    EXPECT_EQ(2, test_server_->gauge("cluster_manager.active_clusters")->value());
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

  envoy::type::v3::CodecClientType codec_client_type_{};
  CdsHelper cds_helper_;
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment_;
  envoy::config::cluster::v3::Cluster cluster_;
  std::vector<std::string> localities_prefixes_;
  std::vector<FakeUpstreamInfo> hosts_upstreams_info_;
  FakeUpstreamInfo eds_upstream_info_;
  FakeUpstreamInfo leds_upstream_info_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, LedsIntegrationTest, GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

// Validates basic LEDS request response behavior.
TEST_P(LedsIntegrationTest, BasicLeds) {
  initializeTest(true);

  // Send an endpoint update with an unknown state using LEDS.
  setEndpoints({}, {}, {}, {0}, {});

  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 1);
  // Waiting for the endpoint to become Healthy to end warming and move the cluster to active.
  EXPECT_EQ(1, test_server_->gauge("cluster_manager.warming_clusters")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster_manager.active_clusters")->value());

  // There should be a single backend in the cluster, and not yet healthy.
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first health-check and verify the host is healthy. This should warm the initial
  // cluster.
  waitForHealthCheck(0);
  hosts_upstreams_info_[0].defaultStream()->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());

  // Wait for our statically specified listener to become ready, and register its port in the
  // test framework's downstream listener port map.
  test_server_->waitUntilListenersReady();
  registerTestServerPorts({"http"});

  // The endpoint sent a valid health-check so the cluster should be active.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeEq("cluster_manager.active_clusters", 3);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Validates adding endpoints using LEDS.
TEST_P(LedsIntegrationTest, LedsAdd) {
  initializeTest(true);

  // Send an endpoint update with an unknown state using LEDS.
  setEndpoints({}, {}, {}, {0}, {});

  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 1);
  // Waiting for the endpoint to become Healthy to end warming and move the cluster to active.
  EXPECT_EQ(1, test_server_->gauge("cluster_manager.warming_clusters")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster_manager.active_clusters")->value());

  // There should be a single backend in the cluster, and not yet healthy.
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first health-check and verify the host is healthy. This should warm the initial
  // cluster.
  waitForHealthCheck(0);
  hosts_upstreams_info_[0].defaultStream()->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());

  // Wait for our statically specified listener to become ready, and register its port in the
  // test framework's downstream listener port map.
  test_server_->waitUntilListenersReady();
  registerTestServerPorts({"http"});

  // The cluster should have now a single healthy host.
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Add 2 more endpoints using LEDS in unknown state.
  setEndpoints({}, {}, {}, {1, 2}, {});

  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 2);

  // There should be additional 2 backends in the cluster, and only one healthy.
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Send health-check responses back from the new hosts.
  for (int i = 1; i < 3; ++i) {
    waitForHealthCheck(i);
    hosts_upstreams_info_[i].defaultStream()->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  }

  // Verify that Envoy observes the healthy endpoints.
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 3);
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Verify that updating the same endpoint doesn't change anything.
TEST_P(LedsIntegrationTest, LedsUpdateSameEndpoint) {
  initializeTest(true);

  // Send an endpoint update with an unknown state using LEDS.
  setEndpoints({}, {}, {}, {0}, {});

  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 1);
  // Waiting for the endpoint to become Healthy to end warming and move the cluster to active.
  EXPECT_EQ(1, test_server_->gauge("cluster_manager.warming_clusters")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster_manager.active_clusters")->value());

  // There should be a single backend in the cluster, and not yet healthy.
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first health-check and verify the host is healthy. This should warm the initial
  // cluster.
  waitForHealthCheck(0);
  hosts_upstreams_info_[0].defaultStream()->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());

  // Wait for our statically specified listener to become ready, and register its port in the
  // test framework's downstream listener port map.
  test_server_->waitUntilListenersReady();
  registerTestServerPorts({"http"});

  // The cluster should have now a single healthy host.
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // "Update" the endpoint by sending the same state. The endpoint should still
  // be healthy, as the active health check cleared it.
  setEndpoints({}, {}, {}, {0}, {});

  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 2);

  // There should be additional 2 backends in the cluster, and only one healthy.
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Verify that Envoy observes the healthy endpoint.
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Verify endpoint removal using LEDS.
TEST_P(LedsIntegrationTest, EndpointRemoval) {
  // Set health-checking to false, so Envoy will remove the endpoint, although it
  // is still healthy.
  initializeTest(false);

  // Send 2 endpoints update with an unknown state using LEDS.
  setEndpoints({}, {}, {}, {0, 1}, {});

  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 1);
  // Waiting for the endpoint to become Healthy to end warming and move the cluster to active.
  EXPECT_EQ(3, test_server_->gauge("cluster_manager.active_clusters")->value());

  // There should be a single backend in the cluster, and not yet healthy.
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());

  // Wait for our statically specified listener to become ready, and register its port in the
  // test framework's downstream listener port map.
  test_server_->waitUntilListenersReady();
  registerTestServerPorts({"http"});

  // Remove one of the endpoints.
  setEndpoints({}, {}, {}, {}, {0});
  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 2);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Verify that a config removing an unknown endpoint is a no-op (similar to CDS).
TEST_P(LedsIntegrationTest, UnknownEndpointRemoval) {
  initializeTest(true);

  // Send 2 endpoints update with an unknown state using LEDS.
  setEndpoints({}, {}, {}, {0}, {});

  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 1);
  // Waiting for the endpoint to become Healthy to end warming and move the cluster to active.
  EXPECT_EQ(1, test_server_->gauge("cluster_manager.warming_clusters")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster_manager.active_clusters")->value());

  // There should be a single backend in the cluster, and not yet healthy.
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first health-check and verify the host is healthy. This should warm the initial
  // cluster.
  waitForHealthCheck(0);
  hosts_upstreams_info_[0].defaultStream()->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());

  // Wait for our statically specified listener to become ready, and register its port in the
  // test framework's downstream listener port map.
  test_server_->waitUntilListenersReady();
  registerTestServerPorts({"http"});

  // The endpoints sent valid health-checks so the cluster should be active.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeEq("cluster_manager.active_clusters", 3);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Remove one of the endpoints.
  setEndpoints({}, {}, {}, {}, {2});
  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 2);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.leds.update_rejected")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Validates that endpoints can be added and then moved to other localities without causing crashes
// (Primarily as a regression test for https://github.com/envoyproxy/envoy/issues/8764).
TEST_P(LedsIntegrationTest, MoveEndpointsBetweenLocalities) {
  // Create 2 localities in the cluster, no health-check as part of this test.
  initializeTest(false, 2);

  EXPECT_EQ(1, test_server_->gauge("cluster_manager.warming_clusters")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster_manager.active_clusters")->value());

  // Send an endpoint update using LEDS for locality 0.
  setEndpoints({}, {}, {}, {0}, {}, 0);

  // The update only updates the first locality, but the cluster should still be
  // in warmed state.
  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 1);
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster_manager.warming_clusters")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster_manager.active_clusters")->value());

  // Send an endpoint update using LEDS for locality 1.
  setEndpoints({}, {}, {}, {1, 2}, {}, 1);

  // All localities should have endpoints so the cluster warm-up should be over.
  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 2);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 3);

  // There should be a single backend in the cluster, all healthy as there isn't
  // active health-check.
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for our statically specified listener to become ready, and register its port in the
  // test framework's downstream listener port map.
  test_server_->waitUntilListenersReady();
  registerTestServerPorts({"http"});

  // Move one endpoint from locality1 to locality0.
  setEndpoints({}, {}, {}, {0, 2}, {}, 0);
  setEndpoints({}, {}, {}, {}, {2}, 1);

  // Wait for the additional 2 LEDS updates.
  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 4);

  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Move one endpoint from locality0 to locality1, and remove the other endpoint.
  setEndpoints({}, {}, {}, {}, {2}, 0);
  setEndpoints({}, {}, {}, {0}, {}, 1);

  // Wait for the additional 2 LEDS updates.
  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 6);

  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Verify that an endpoint can be in 2 localities at the same time.
TEST_P(LedsIntegrationTest, LocalitiesShareEndpoint) {
  // Create 2 localities in the cluster, no health-check as part of this test.
  initializeTest(false, 2);

  EXPECT_EQ(1, test_server_->gauge("cluster_manager.warming_clusters")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster_manager.active_clusters")->value());

  // Send an endpoint update using LEDS for locality 0.
  setEndpoints({}, {}, {}, {0}, {}, 0);

  // The update only updates the first locality, but the cluster should still be
  // in warmed state.
  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 1);
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster_manager.warming_clusters")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster_manager.active_clusters")->value());

  // Send an endpoint update using LEDS for locality 1 with a different endpoint.
  setEndpoints({}, {}, {}, {1}, {}, 1);

  // All localities should have endpoints so the cluster warm-up should be over.
  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 2);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 3);

  // There should be 2 hosts in the cluster, all healthy as there isn't active health-check.
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for our statically specified listener to become ready, and register its port in the
  // test framework's downstream listener port map.
  test_server_->waitUntilListenersReady();
  registerTestServerPorts({"http"});

  // Send a LEDS update to locality 1 with the same endpoint that is in locality 0.
  setEndpoints({}, {}, {}, {0}, {}, 1);
  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 3);

  // There should be 2 hosts in the cluster, all healthy as there isn't active health-check.
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Remove the endpoint from one locality.
  setEndpoints({}, {}, {}, {}, {0}, 0);

  // Wait for the additional LEDS update.
  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 4);

  // There are 2 endpoints left in locality 1.
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Verify that a host stabilized via active health checking which is first removed from LEDS and
// then fails health checking is removed.
TEST_P(LedsIntegrationTest, RemoveAfterHcFail) {
  initializeTest(true);
  setEndpoints({}, {}, {}, {0}, {});
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.leds.update_success")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Wait for the first HC and verify the host is healthy.
  waitForHealthCheck(0);
  hosts_upstreams_info_[0].defaultStream()->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());

  // Clear out the host and verify the host is still healthy.
  setEndpoints({}, {}, {}, {}, {0});

  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.leds.update_success")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());

  // Fail HC and verify the host is gone.
  waitForHealthCheck(0);
  hosts_upstreams_info_[0].defaultStream()->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "503"}, {"connection", "close"}}, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_healthy", 0);
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_total")->value());
}

// Validate that health status updates are consumed from LEDS.
TEST_P(LedsIntegrationTest, HealthUpdate) {
  initializeTest(false);
  // Initial state, no cluster members.
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // 2 healthy endpoints.
  setEndpoints({0, 1}, {}, {}, {}, {});
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Drop to 0/2 healthy endpoints (2 unknown health state).
  setEndpoints({}, {}, {0, 1}, {}, {});
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Increase to 1/2 healthy endpoints (host 1 will remain unhealthy).
  setEndpoints({0}, {}, {1}, {}, {});
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Add host and modify healthy to 2/3 healthy endpoints.
  setEndpoints({2}, {}, {1}, {}, {});
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Modify healthy to 2/3 healthy and 1/3 degraded.
  setEndpoints({}, {1}, {}, {}, {});
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_degraded")->value());
}

// Validates that in a LEDS response that contains 2 endpoints with the same
// address, only the first will be used.
TEST_P(LedsIntegrationTest, LedsSameAddressEndpoints) {
  initializeTest(false);

  // Send a response with 2 endpoints with a different resource name but that
  // map to the same address.
  const auto& collection_prefix = localities_prefixes_[0];
  absl::flat_hash_map<std::string, envoy::config::endpoint::v3::LbEndpoint> updated_endpoints;
  std::vector<std::string> removed_endpoints;

  const std::vector<std::string> endpoints_names{
      absl::StrCat(collection_prefix, "endpoint0"),
      absl::StrCat(collection_prefix, "endpoint1"),
  };

  for (const auto& endpoint_name : endpoints_names) {
    envoy::config::endpoint::v3::LbEndpoint endpoint;
    // Shift fake_upstreams_ by 2 (due to EDS and LEDS fake upstreams).
    setUpstreamAddress(2, endpoint);
    endpoint.set_health_status(envoy::config::core::v3::HEALTHY);
    updated_endpoints.emplace(endpoint_name, endpoint);
  }

  sendDeltaLedsResponse(updated_endpoints, removed_endpoints, "7", 0);

  // Await for update (LEDS Ack).
  EXPECT_TRUE(compareDeltaDiscoveryRequest(
      Config::TypeUrl::get().LbEndpoint, {}, {},
      leds_upstream_info_.stream_by_resource_name_[localities_prefixes_[0]].get()));

  // Verify that the update is successful.
  test_server_->waitForCounterEq("cluster.cluster_0.leds.update_success", 1);

  // Wait for our statically specified listener to become ready, and register its port in the
  // test framework's downstream listener port map.
  test_server_->waitUntilListenersReady();
  registerTestServerPorts({"http"});

  // Verify that only one endpoint was processed.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeEq("cluster_manager.active_clusters", 3);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

} // namespace
} // namespace Envoy

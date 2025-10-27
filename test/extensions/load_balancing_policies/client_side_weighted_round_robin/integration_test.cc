#include <chrono>
#include <cstdint>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/load_balancing_policies/round_robin/config.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace ClientSideWeightedRoundRobin {
namespace {

void configureClusterLoadBalancingPolicy(envoy::config::cluster::v3::Cluster& cluster) {
  auto* policy = cluster.mutable_load_balancing_policy();

  // Configure LB policy with short blackout period, long expiration period,
  // and short update period.
  const std::string policy_yaml = R"EOF(
      policies:
      - typed_extension_config:
          name: envoy.load_balancing_policies.client_side_weighted_round_robin
          typed_config:
              "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin
              blackout_period:
                  seconds: 1
              weight_expiration_period:
                  seconds: 180
              weight_update_period:
                  seconds: 1
      )EOF";

  TestUtility::loadFromYaml(policy_yaml, *policy);
}

Http::TestResponseHeaderMapImpl
responseHeadersWithLoadReport(int backend_index, double application_utilization, double qps) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_application_utilization(application_utilization);
  orca_load_report.mutable_named_metrics()->insert({"backend_index", backend_index});
  orca_load_report.set_rps_fractional(qps);
  std::string proto_string = TestUtility::getProtobufBinaryStringFromMessage(orca_load_report);
  std::string orca_load_report_header_bin =
      Envoy::Base64::encode(proto_string.c_str(), proto_string.length());
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_headers.addCopy("endpoint-load-metrics-bin", orca_load_report_header_bin);
  return response_headers;
}

class ClientSideWeightedRoundRobinIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, int>>,
      public HttpIntegrationTest {
public:
  ClientSideWeightedRoundRobinIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, std::get<0>(GetParam())) {
    qps_multiplier_ = std::get<1>(GetParam());
    // Create 3 different upstream server for stateful session test.
    setUpstreamCount(3);
  }

  void initializeConfig() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
      ASSERT(cluster_0->name() == "cluster_0");
      auto* endpoint = cluster_0->mutable_load_assignment()->mutable_endpoints()->Mutable(0);

      constexpr absl::string_view endpoints_yaml = R"EOF(
          lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: 0
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: 0
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: 0
          )EOF";

      const std::string local_address =
          Network::Test::getLoopbackAddressString(std::get<0>(GetParam()));
      TestUtility::loadFromYaml(
          fmt::format(endpoints_yaml, local_address, local_address, local_address), *endpoint);

      configureClusterLoadBalancingPolicy(*cluster_0);
    });

    HttpIntegrationTest::initialize();
  }

  Http::TestResponseHeaderMapImpl
  responseHeadersWithLoadReport(int backend_index, double application_utilization, double qps) {
    xds::data::orca::v3::OrcaLoadReport orca_load_report;
    orca_load_report.set_application_utilization(application_utilization);
    orca_load_report.mutable_named_metrics()->insert({"backend_index", backend_index});
    orca_load_report.set_rps_fractional(qps);
    std::string proto_string = TestUtility::getProtobufBinaryStringFromMessage(orca_load_report);
    std::string orca_load_report_header_bin =
        Envoy::Base64::encode(proto_string.c_str(), proto_string.length());
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    response_headers.addCopy("endpoint-load-metrics-bin", orca_load_report_header_bin);
    return response_headers;
  }

  void sendRequestsAndTrackUpstreamUsage(uint64_t number_of_requests,
                                         std::vector<uint64_t>& upstream_usage) {
    // Expected number of upstreams.
    upstream_usage.resize(3);
    ENVOY_LOG(trace, "Start sending {} requests.", number_of_requests);

    for (uint64_t i = 0; i < number_of_requests; i++) {
      ENVOY_LOG(trace, "Before request {}.", i);

      codec_client_ = makeHttpConnection(lookupPort("http"));

      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2});
      ASSERT(upstream_index.has_value());
      upstream_usage[upstream_index.value()]++;

      // All hosts report the same utilization, but different QPS, so their
      // weights will be different.
      upstream_request_->encodeHeaders(
          responseHeadersWithLoadReport(upstream_index.value(), 0.5,
                                        qps_multiplier_ * (upstream_index.value() + 1)),
          true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      cleanupUpstreamAndDownstream();
      ENVOY_LOG(trace, "After request {}.", i);
    }
  }

  void runNormalLoadBalancing() {
    std::vector<uint64_t> indexs;

    // Initial requests use round robin because client-side reported weights
    // are ignored during 1s blackout period.
    std::vector<uint64_t> initial_usage;
    sendRequestsAndTrackUpstreamUsage(50, initial_usage);

    ENVOY_LOG(trace, "initial_usage {}", initial_usage);

    // Wait longer than blackout period to ensure that client side weights are
    // applied.
    timeSystem().advanceTimeWait(std::chrono::seconds(2));

    // Send more requests expecting weights to be applied, so upstream hosts are
    // used proportionally to their weights.
    std::vector<uint64_t> weighted_usage;
    sendRequestsAndTrackUpstreamUsage(100, weighted_usage);
    ENVOY_LOG(trace, "weighted_usage {}", weighted_usage);
    EXPECT_LT(weighted_usage[0], weighted_usage[1]);
    EXPECT_LT(weighted_usage[1], weighted_usage[2]);
  }

  int qps_multiplier_ = 1;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, ClientSideWeightedRoundRobinIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn<int>({1, 100, 10000})));

TEST_P(ClientSideWeightedRoundRobinIntegrationTest, NormalLoadBalancing) {
  initializeConfig();
  runNormalLoadBalancing();
}

// Tests to verify the behavior of load balancing policy when cluster is added,
// removed, and added again.
class ClientSideWeightedRoundRobinXdsIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public HttpIntegrationTest {
public:
  ClientSideWeightedRoundRobinXdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, std::get<0>(GetParam()), config()),
        deferred_cluster_creation_(std::get<1>(GetParam())) {
    use_lds_ = false;
  }

  void TearDown() override { cleanUpXdsConnection(); }

  void initialize() override {
    use_lds_ = false;
    setUpstreamCount(2);                         // the CDS cluster
    setUpstreamProtocol(Http::CodecType::HTTP2); // CDS uses gRPC uses HTTP2.

    defer_listener_finalization_ = true;
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_cluster_manager()->set_enable_deferred_cluster_creation(
          deferred_cluster_creation_);
    });
    HttpIntegrationTest::initialize();

    addFakeUpstream(Http::CodecType::HTTP2);
    addFakeUpstream(Http::CodecType::HTTP2);
    cluster1_ = ConfigHelper::buildStaticCluster(
        FirstClusterName, fake_upstreams_[FirstUpstreamIndex]->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(version_));
    configureClusterLoadBalancingPolicy(cluster1_);

    cluster2_ = ConfigHelper::buildStaticCluster(
        SecondClusterName, fake_upstreams_[SecondUpstreamIndex]->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(version_));
    configureClusterLoadBalancingPolicy(cluster2_);

    // Let Envoy establish its connection to the CDS server.
    acceptXdsConnection();

    // Do the initial compareDiscoveryRequest / sendDiscoveryResponse for
    // cluster_1.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                               {cluster1_}, {cluster1_}, {}, "55");

    test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);

    // Wait for our statically specified listener to become ready, and register
    // its port in the test framework's downstream listener port map.
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  void acceptXdsConnection() {
    // xds_connection_ is filled with the new FakeHttpConnection.
    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }

  const char* FirstClusterName = "cluster_1";
  const char* SecondClusterName = "cluster_2";
  // Index in fake_upstreams_
  const int FirstUpstreamIndex = 2;
  const int SecondUpstreamIndex = 3;

  const std::string& config() {
    CONSTRUCT_ON_FIRST_USE(std::string, fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
dynamic_resources:
  cds_config:
    api_config_source:
      api_type: GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: my_cds_cluster
      set_node_on_first_message_only: true
static_resources:
  clusters:
  - name: my_cds_cluster
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http2_protocol_options: {{}}
    load_assignment:
      cluster_name: my_cds_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
  listeners:
  - name: http
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: http
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: config_test
          http_filters:
            name: envoy.filters.http.router
          codec_type: HTTP1
          route_config:
            name: route_config_0
            validate_clusters: false
            virtual_hosts:
              name: integration
              routes:
              - route:
                  cluster: cluster_1
                match:
                  prefix: "/cluster1"
              - route:
                  cluster: cluster_2
                match:
                  prefix: "/cluster2"
              domains: "*"
)EOF",
                                                    Platform::null_device_path));
  }

  const bool deferred_cluster_creation_;
  envoy::config::cluster::v3::Cluster cluster1_;
  envoy::config::cluster::v3::Cluster cluster2_;
};

TEST_P(ClientSideWeightedRoundRobinXdsIntegrationTest, ClusterUpDownUp) {
  // Calls our initialize(), which includes establishing a listener, route, and
  // cluster.
  testRouterHeaderOnlyRequestAndResponse(nullptr, FirstUpstreamIndex, "/cluster1");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Tell Envoy that cluster_1 is gone.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "55", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster, {},
                                                             {}, {FirstClusterName}, "42");
  // We can continue the test once we're sure that Envoy's ClusterManager has
  // made use of the DiscoveryResponse that says cluster_1 is gone.
  test_server_->waitForCounterGe("cluster_manager.cluster_removed", 1);

  // Now that cluster_1 is gone, the listener (with its routing to cluster_1)
  // should 503.
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/cluster1", "", downstream_protocol_, version_, "foo.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Tell Envoy that cluster_1 is back.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "42", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                             {cluster1_}, {cluster1_}, {}, "413");

  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);
  testRouterHeaderOnlyRequestAndResponse(nullptr, FirstUpstreamIndex, "/cluster1");

  cleanupUpstreamAndDownstream();

  // runNormalLoadBalancing();
}

// Tests adding a cluster, adding another, then removing and adding back the first.
TEST_P(ClientSideWeightedRoundRobinXdsIntegrationTest, TwoClusters) {
  // Calls our initialize(), which includes establishing a listener, route, and
  // cluster.
  testRouterHeaderOnlyRequestAndResponse(nullptr, FirstUpstreamIndex, "/cluster1");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Tell Envoy that cluster_2 is here.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "55", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {cluster1_, cluster2_}, {cluster2_}, {}, "42");
  // Wait for the cluster to be active (two upstream clusters plus the CDS
  // cluster).
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 3);

  // A request for the second cluster should be fine.
  testRouterHeaderOnlyRequestAndResponse(nullptr, SecondUpstreamIndex, "/cluster2");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Tell Envoy that cluster_1 is gone.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "42", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {cluster2_}, {}, {FirstClusterName}, "43");
  // We can continue the test once we're sure that Envoy's ClusterManager has
  // made use of the DiscoveryResponse that says cluster_1 is gone.
  test_server_->waitForCounterGe("cluster_manager.cluster_removed", 1);

  testRouterHeaderOnlyRequestAndResponse(nullptr, SecondUpstreamIndex, "/cluster2");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Tell Envoy that cluster_1 is back.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "43", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {cluster1_, cluster2_}, {cluster1_}, {}, "413");
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 3);
  testRouterHeaderOnlyRequestAndResponse(nullptr, FirstUpstreamIndex, "/cluster1");
  cleanupUpstreamAndDownstream();
}

INSTANTIATE_TEST_SUITE_P(
    IpVersions, ClientSideWeightedRoundRobinXdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()));

// Tests to verify the behavior of load balancing policy when endpoints are
// updated.
class ClientSideWeightedRoundRobinEdsIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  ClientSideWeightedRoundRobinEdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam(), config()) {
    use_lds_ = false;
  }

  void TearDown() override { cleanUpXdsConnection(); }

  void initialize() override {
    use_lds_ = false;
    setUpstreamCount(2);                         // the endpoints of the CDS
                                                 // cluster
    setUpstreamProtocol(Http::CodecType::HTTP2); // CDS uses gRPC uses HTTP2.

    defer_listener_finalization_ = true;
    HttpIntegrationTest::initialize();

    addFakeUpstream(Http::CodecType::HTTP2);
    addFakeUpstream(Http::CodecType::HTTP2);
    addFakeUpstream(Http::CodecType::HTTP2);
    addFakeUpstream(Http::CodecType::HTTP2);
    // Create an EDS cluster.
    cluster1_ = ConfigHelper::buildCluster(FirstClusterName);
    configureClusterLoadBalancingPolicy(cluster1_);
    cluster1_.mutable_common_lb_config()->mutable_update_merge_window()->set_seconds(0);

    // Let Envoy establish its connection to the CDS server.
    acceptXdsConnection();

    // Do the initial compareDiscoveryRequest / sendDiscoveryResponse for
    // cluster1.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                               {cluster1_}, {cluster1_}, {}, "55");

    // Wait for EDS request.
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
    test_server_->waitForGaugeEq("cluster.cluster_1.warming_state", 1);
    EXPECT_TRUE(compareDiscoveryRequest(
        Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(), "",
        {FirstClusterName}, {FirstClusterName}, {}));

    // Send EDS response for cluster1 that contains 2 localities with 2
    // endpoints each. First locality includes fake_upstreams_[1] and
    // fake_upstreams_[2].
    cluster1_endpoints_ = ConfigHelper::buildClusterLoadAssignment(
        FirstClusterName, Network::Test::getLoopbackAddressString(version_),
        fake_upstreams_[1]->localAddress()->ip()->port());
    cluster1_endpoints_.mutable_endpoints(0)->set_priority(0);
    cluster1_endpoints_.mutable_endpoints(0)->mutable_locality()->set_sub_zone("zone_0");
    auto* address = cluster1_endpoints_.mutable_endpoints(0)
                        ->add_lb_endpoints()
                        ->mutable_endpoint()
                        ->mutable_address()
                        ->mutable_socket_address();
    address->set_address(Network::Test::getLoopbackAddressString(version_));
    address->set_port_value(fake_upstreams_[2]->localAddress()->ip()->port());

    // Second locality includes fake_upstreams_[3] and fake_upstreams_[4]
    auto temp_endpoints = ConfigHelper::buildClusterLoadAssignment(
        FirstClusterName, Network::Test::getLoopbackAddressString(version_),
        fake_upstreams_[3]->localAddress()->ip()->port());
    temp_endpoints.mutable_endpoints(0)->set_priority(0);
    temp_endpoints.mutable_endpoints(0)->mutable_locality()->set_sub_zone("zone_1");
    auto* address4 = temp_endpoints.mutable_endpoints(0)
                         ->add_lb_endpoints()
                         ->mutable_endpoint()
                         ->mutable_address()
                         ->mutable_socket_address();
    address4->set_address(Network::Test::getLoopbackAddressString(version_));
    address4->set_port_value(fake_upstreams_[4]->localAddress()->ip()->port());

    cluster1_endpoints_.mutable_endpoints()->Add()->MergeFrom(temp_endpoints.endpoints(0));

    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(),
        {cluster1_endpoints_}, {cluster1_endpoints_}, {}, "1");

    // A CDS and EDS ack.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "1", {}, {}, {}));
    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().ClusterLoadAssignment, "1",
                                        {FirstClusterName}, {}, {}));

    // Cluster should become active.
    test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);

    // Wait for our statically specified listener to become ready, and register
    // its port in the test framework's downstream listener port map.
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  void acceptXdsConnection() {
    // xds_connection_ is filled with the new FakeHttpConnection.
    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }

  void sendRequestsAndTrackUpstreamUsage(uint64_t first_upstream_index,
                                         const std::vector<uint64_t>& upstream_qps,
                                         uint64_t number_of_requests,
                                         std::vector<uint64_t>& upstream_usage) {
    auto number_of_upstreams = upstream_qps.size();
    std::vector<uint64_t> upstream_indices;
    for (uint64_t i = 0; i < number_of_upstreams; ++i) {
      upstream_indices.push_back(first_upstream_index + i);
    }
    // Expected number of upstreams.
    upstream_usage.resize(number_of_upstreams);
    ENVOY_LOG(trace, "Start sending {} requests.", number_of_requests);

    for (uint64_t i = 0; i < number_of_requests; i++) {
      ENVOY_LOG(trace, "Before request {}.", i);

      codec_client_ = makeHttpConnection(lookupPort("http"));

      Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                     {":path", "/cluster1"},
                                                     {":scheme", "http"},
                                                     {":authority", "example.com"}};

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest(upstream_indices);
      ASSERT(upstream_index.has_value());
      upstream_usage[upstream_index.value()]++;

      // All hosts report the same utilization, but different QPS, so their
      // weights will be different.
      upstream_request_->encodeHeaders(
          responseHeadersWithLoadReport(upstream_index.value(), 0.5,
                                        upstream_qps[upstream_index.value()]),
          true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      cleanupUpstreamAndDownstream();
      ENVOY_LOG(trace, "After request {}.", i);
    }
  }

  const char* FirstClusterName = "cluster_1";
  // Index in fake_upstreams_
  const int FirstUpstreamIndex = 1;

  const std::string& config() {
    CONSTRUCT_ON_FIRST_USE(std::string, fmt::format(R"EOF(
    admin:
      access_log:
      - name: envoy.access_loggers.file
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
          path: "{}"
      address:
        socket_address:
          address: 127.0.0.1
          port_value: 0
    dynamic_resources:
      cds_config:
        ads: {{}}
      ads_config:
        api_type: GRPC
        grpc_services:
          envoy_grpc:
            cluster_name: my_cds_cluster
        set_node_on_first_message_only: true
    static_resources:
      clusters:
      - name: my_cds_cluster
        typed_extension_protocol_options:
          envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
            "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
            explicit_http_config:
              http2_protocol_options: {{}}
        load_assignment:
          cluster_name: my_cds_cluster
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
      listeners:
      - name: http
        address:
          socket_address:
            address: 127.0.0.1
            port_value: 0
        filter_chains:
          filters:
            name: http
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
              stat_prefix: config_test
              http_filters:
                name: envoy.filters.http.router
              codec_type: HTTP1
              route_config:
                name: route_config_0
                validate_clusters: false
                virtual_hosts:
                  name: integration
                  routes:
                  - route:
                      cluster: cluster_1
                    match:
                      prefix: "/cluster1"
                  domains: "*"
    )EOF",
                                                    Platform::null_device_path));
  }

  bool locality_weighted_lb_enabled_;
  envoy::config::cluster::v3::Cluster cluster1_;
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster1_endpoints_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClientSideWeightedRoundRobinEdsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ClientSideWeightedRoundRobinEdsIntegrationTest, UpdateLocalityPriority) {
  initialize();
  for (uint32_t i = 0; i < 10; ++i) {
    bool use_single_locality = (i % 2 != 0);
    cluster1_endpoints_.mutable_endpoints(0)->mutable_load_balancing_weight()->set_value(i + 1);
    cluster1_endpoints_.mutable_endpoints(1)->mutable_load_balancing_weight()->set_value(i * 3 + 1);
    cluster1_endpoints_.mutable_endpoints(1)->set_priority(use_single_locality ? 1 : 0);

    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(),
        {cluster1_endpoints_}, {}, {"cluster_1"}, "2");

    // Wait for the EDS ack.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().ClusterLoadAssignment, "2",
                                        {FirstClusterName}, {}, {}));
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(),
        {cluster1_endpoints_}, {}, {"cluster_1"}, "2");

    // Upstream QPS for ORCA load reports. All hosts report the same QPS.
    const std::vector<uint64_t> upstream_qps = {100, 100, 100, 100};
    // Send 100 requests to cluster1 so host weights are updated.
    std::vector<uint64_t> initial_usage;
    sendRequestsAndTrackUpstreamUsage(FirstUpstreamIndex, upstream_qps, 10, initial_usage);
    ENVOY_LOG(trace, "initial_usage {}", initial_usage);

    EXPECT_EQ(i * 2 + 1, test_server_->counter("cluster.cluster_1.membership_change")->value());

    // Send another 100 requests to cluster1, expecting weights to be used.
    std::vector<uint64_t> upstream_usage;
    sendRequestsAndTrackUpstreamUsage(FirstUpstreamIndex, upstream_qps, 100, upstream_usage);
    ENVOY_LOG(trace, "upstream_usage {}", upstream_usage);
    // Expect the usage of first locality to be non-zero.
    EXPECT_GT(upstream_usage[0], 0);
    EXPECT_GT(upstream_usage[1], 0);
    // Expect the usage of second locality to be non-zero if the priority is
    // not set to 1.
    if (use_single_locality) {
      EXPECT_EQ(upstream_usage[2], 0);
      EXPECT_EQ(upstream_usage[3], 0);
    } else {
      EXPECT_GT(upstream_usage[2], 0);
      EXPECT_GT(upstream_usage[3], 0);
    }
  }
}

TEST_P(ClientSideWeightedRoundRobinEdsIntegrationTest, AddRemoveLocality) {
  initialize();
  for (uint32_t i = 0; i < 10; ++i) {
    bool use_single_locality = (i % 2 != 0);
    cluster1_endpoints_.mutable_endpoints(0)->mutable_load_balancing_weight()->set_value(i + 1);
    cluster1_endpoints_.mutable_endpoints(1)->mutable_load_balancing_weight()->set_value(i * 3 + 1);

    envoy::config::endpoint::v3::ClusterLoadAssignment current_endpoints;
    current_endpoints.CopyFrom(cluster1_endpoints_);
    if (use_single_locality) {
      current_endpoints.mutable_endpoints()->DeleteSubrange(1, 1);
    }

    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(),
        {current_endpoints}, {}, {"cluster_1"}, "2");

    // Wait for the EDS ack.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().ClusterLoadAssignment, "2",
                                        {FirstClusterName}, {}, {}));
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(),
        {current_endpoints}, {}, {"cluster_1"}, "2");

    // Upstream QPS for ORCA load reports. All hosts report the same QPS.
    const std::vector<uint64_t> upstream_qps = {100, 100, 100, 100};
    // Send 100 requests to cluster1 so host weights are updated.
    std::vector<uint64_t> initial_usage;
    sendRequestsAndTrackUpstreamUsage(FirstUpstreamIndex, upstream_qps, 10, initial_usage);
    ENVOY_LOG(trace, "initial_usage {}", initial_usage);

    EXPECT_EQ(i + 1, test_server_->counter("cluster.cluster_1.membership_change")->value());

    // Send another 100 requests to cluster1, expecting weights to be used.
    std::vector<uint64_t> upstream_usage;
    sendRequestsAndTrackUpstreamUsage(FirstUpstreamIndex, upstream_qps, 100, upstream_usage);
    ENVOY_LOG(trace, "upstream_usage {}", upstream_usage);
    // Expect the usage of first locality to be non-zero.
    EXPECT_GT(upstream_usage[0], 0);
    EXPECT_GT(upstream_usage[1], 0);
    // Expect the usage of second locality to be non-zero if the priority is
    // not set to 1.
    if (use_single_locality) {
      EXPECT_EQ(upstream_usage[2], 0);
      EXPECT_EQ(upstream_usage[3], 0);
    } else {
      EXPECT_GT(upstream_usage[2], 0);
      EXPECT_GT(upstream_usage[3], 0);
    }
  }
}

} // namespace
} // namespace ClientSideWeightedRoundRobin
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy

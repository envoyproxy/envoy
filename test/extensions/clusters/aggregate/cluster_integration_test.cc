#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/stats/scope.h"

#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

const char FirstClusterName[] = "cluster_1";
const char SecondClusterName[] = "cluster_2";
// Index in fake_upstreams_
const int FirstUpstreamIndex = 2;
const int SecondUpstreamIndex = 3;

struct CircuitBreakerLimits {
  uint32_t max_connections = 1024;
  uint32_t max_requests = 1024;
  uint32_t max_pending_requests = 1024;
  uint32_t max_retries = 3;
  uint32_t max_connection_pools = std::numeric_limits<uint32_t>::max();

  CircuitBreakerLimits& withMaxConnections(uint32_t max_connections) {
    this->max_connections = max_connections;
    return *this;
  }

  CircuitBreakerLimits& withMaxRequests(uint32_t max_requests) {
    this->max_requests = max_requests;
    return *this;
  }

  CircuitBreakerLimits& withMaxPendingRequests(uint32_t max_pending_requests) {
    this->max_pending_requests = max_pending_requests;
    return *this;
  }

  CircuitBreakerLimits& withMaxRetries(uint32_t max_retries) {
    this->max_retries = max_retries;
    return *this;
  }
};

void setCircuitBreakerLimits(envoy::config::cluster::v3::Cluster& cluster,
                             const CircuitBreakerLimits& limits) {
  auto* cluster_circuit_breakers = cluster.mutable_circuit_breakers();

  auto* cluster_circuit_breakers_threshold_default = cluster_circuit_breakers->add_thresholds();
  cluster_circuit_breakers_threshold_default->set_priority(
      envoy::config::core::v3::RoutingPriority::DEFAULT);

  cluster_circuit_breakers_threshold_default->mutable_max_connections()->set_value(
      limits.max_connections);
  cluster_circuit_breakers_threshold_default->mutable_max_pending_requests()->set_value(
      limits.max_pending_requests);
  cluster_circuit_breakers_threshold_default->mutable_max_requests()->set_value(
      limits.max_requests);
  cluster_circuit_breakers_threshold_default->mutable_max_retries()->set_value(limits.max_retries);
  cluster_circuit_breakers_threshold_default->mutable_max_connection_pools()->set_value(
      limits.max_connection_pools);
  cluster_circuit_breakers_threshold_default->set_track_remaining(true);
};

void setMaxConcurrentStreams(envoy::config::cluster::v3::Cluster& cluster,
                             uint32_t max_concurrent_streams) {
  envoy::extensions::upstreams::http::v3::HttpProtocolOptions http_protocol_options;
  http_protocol_options.mutable_explicit_http_config()
      ->mutable_http2_protocol_options()
      ->mutable_max_concurrent_streams()
      ->set_value(max_concurrent_streams);
  (*cluster.mutable_typed_extension_protocol_options())
      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
          .PackFrom(http_protocol_options);
};

void reduceAggregateClustersListToOneCluster(
    envoy::config::cluster::v3::Cluster& aggregate_cluster) {
  auto* aggregate_cluster_type = aggregate_cluster.mutable_cluster_type();
  auto* aggregate_cluster_typed_config = aggregate_cluster_type->mutable_typed_config();
  envoy::extensions::clusters::aggregate::v3::ClusterConfig new_aggregate_cluster_typed_config;
  aggregate_cluster_typed_config->UnpackTo(&new_aggregate_cluster_typed_config);
  new_aggregate_cluster_typed_config.clear_clusters();
  new_aggregate_cluster_typed_config.add_clusters("cluster_1");
  aggregate_cluster_typed_config->PackFrom(new_aggregate_cluster_typed_config);
};

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
  - name: aggregate_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.aggregate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate.v3.ClusterConfig
        clusters:
        - cluster_1
        - cluster_2
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
              - route:
                  cluster: aggregate_cluster
                  retry_policy:
                    retry_priority:
                      name: envoy.retry_priorities.previous_priorities
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.retry.priority.previous_priorities.v3.PreviousPrioritiesConfig
                        update_frequency: 1
                match:
                  prefix: "/aggregatecluster"
              domains: "*"
)EOF",
                                                  Platform::null_device_path));
}

class AggregateIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public HttpIntegrationTest {
public:
  AggregateIntegrationTest()
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
    cluster2_ = ConfigHelper::buildStaticCluster(
        SecondClusterName, fake_upstreams_[SecondUpstreamIndex]->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(version_));

    // Let Envoy establish its connection to the CDS server.
    acceptXdsConnection();

    // Do the initial compareDiscoveryRequest / sendDiscoveryResponse for cluster_1.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                               {cluster1_}, {cluster1_}, {}, "55");

    test_server_->waitForGaugeGe("cluster_manager.active_clusters", 3);

    // Wait for our statically specified listener to become ready, and register its port in the
    // test framework's downstream listener port map.
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  void acceptXdsConnection() {
    AssertionResult result = // xds_connection_ is filled with the new FakeHttpConnection.
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }

  const bool deferred_cluster_creation_;
  envoy::config::cluster::v3::Cluster cluster1_;
  envoy::config::cluster::v3::Cluster cluster2_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, AggregateIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()));

TEST_P(AggregateIntegrationTest, ClusterUpDownUp) {
  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  testRouterHeaderOnlyRequestAndResponse(nullptr, FirstUpstreamIndex, "/aggregatecluster");

  // Tell Envoy that cluster_1 is gone.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "55", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster, {},
                                                             {}, {FirstClusterName}, "42");
  // We can continue the test once we're sure that Envoy's ClusterManager has made use of
  // the DiscoveryResponse that says cluster_1 is gone.
  test_server_->waitForCounterGe("cluster_manager.cluster_removed", 1);

  // Now that cluster_1 is gone, the listener (with its routing to cluster_1) should 503.
  BufferingStreamDecoderPtr response =
      IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/aggregatecluster", "",
                                         downstream_protocol_, version_, "foo.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Tell Envoy that cluster_1 is back.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "42", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                             {cluster1_}, {cluster1_}, {}, "413");

  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 3);
  testRouterHeaderOnlyRequestAndResponse(nullptr, FirstUpstreamIndex, "/aggregatecluster");

  cleanupUpstreamAndDownstream();
}

// Tests adding a cluster, adding another, then removing the first.
TEST_P(AggregateIntegrationTest, TwoClusters) {
  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  testRouterHeaderOnlyRequestAndResponse(nullptr, FirstUpstreamIndex, "/aggregatecluster");

  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Tell Envoy that cluster_2 is here.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "55", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {cluster1_, cluster2_}, {cluster2_}, {}, "42");
  // The '4' includes the fake CDS server and aggregate cluster.
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 4);

  // A request for aggregate cluster should be fine.
  testRouterHeaderOnlyRequestAndResponse(nullptr, FirstUpstreamIndex, "/aggregatecluster");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Tell Envoy that cluster_1 is gone.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "42", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {cluster2_}, {}, {FirstClusterName}, "43");
  // We can continue the test once we're sure that Envoy's ClusterManager has made use of
  // the DiscoveryResponse that says cluster_1 is gone.
  test_server_->waitForCounterGe("cluster_manager.cluster_removed", 1);

  testRouterHeaderOnlyRequestAndResponse(nullptr, SecondUpstreamIndex, "/aggregatecluster");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Tell Envoy that cluster_1 is back.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "43", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {cluster1_, cluster2_}, {cluster1_}, {}, "413");

  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 4);
  testRouterHeaderOnlyRequestAndResponse(nullptr, FirstUpstreamIndex, "/aggregatecluster");

  cleanupUpstreamAndDownstream();
}

// Test that the PreviousPriorities retry predicate works as expected. It is configured
// in this test to exclude a priority after a single failure, so the first failure
// on cluster_1 results in the retry going to cluster_2.
TEST_P(AggregateIntegrationTest, PreviousPrioritiesRetryPredicate) {
  initialize();

  // Tell Envoy that cluster_2 is here.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {cluster1_, cluster2_}, {cluster2_}, {}, "42");
  // The '4' includes the fake CDS server and aggregate cluster.
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 4);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/aggregatecluster"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024);
  waitForNextUpstreamRequest(FirstUpstreamIndex);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);

  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  fake_upstream_connection_.reset();

  waitForNextUpstreamRequest(SecondUpstreamIndex);
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  cleanupUpstreamAndDownstream();
}

// Tests the max_connections circuit breaker behaviour on the aggregate cluster and its underlying
// cluster1. When connections reach the configured limit, only the underlying cluster1's circuit
// breaker opens, the aggregate cluster's circuit breaker is completely unaffected. When cluster1's
// circuit breaker opens, new connections are rejected, and the overflow counter increases. After
// closing the connection, cluster1's circuit breaker closes again.
TEST_P(AggregateIntegrationTest, CircuitBreakerTestMaxConnections) {
  setDownstreamProtocol(Http::CodecType::HTTP2);

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* aggregate_cluster = static_resources->mutable_clusters(1);

    reduceAggregateClustersListToOneCluster(*aggregate_cluster);
    setCircuitBreakerLimits(*aggregate_cluster, CircuitBreakerLimits{}.withMaxConnections(1));
    setMaxConcurrentStreams(*aggregate_cluster, 1);
  });

  initialize();

  setCircuitBreakerLimits(cluster1_, CircuitBreakerLimits{}.withMaxConnections(1));
  setMaxConcurrentStreams(cluster1_, 1);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "55", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                             {cluster1_}, {cluster1_}, {}, "56");
  test_server_->waitForGaugeEq("cluster_manager.active_clusters", 3);

  // initial circuit breaker states:
  // the aggregate cluster circuit breaker is closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.cx_open", 0);
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.remaining_cx",
                               1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_cx_overflow", 0);
  // the cluster1 circuit breaker is closed
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.cx_open", 0);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_cx", 1);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_cx_overflow", 0);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // send a first request to the aggregate cluster
  auto aggregate_cluster_response1 = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/aggregatecluster"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});

  // wait for the first request to arrive at cluster1
  waitForNextUpstreamRequest(FirstUpstreamIndex);

  // after the first request arrives at cluster1 (there is now a single active upstream connection)
  // the aggregate cluster circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.cx_open", 0);
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.remaining_cx",
                               1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_cx_overflow", 0);
  // the cluster1 circuit breaker opens
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.cx_open", 1);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_cx", 0);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_cx_overflow", 0);

  // send a second request to the aggregate cluster
  auto aggregate_cluster_response2 = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/aggregatecluster"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});

  // the second request is rejected because the cluster1 circuit breaker is open
  // the aggregate cluster circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.cx_open", 0);
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.remaining_cx",
                               1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_cx_overflow", 0);
  // the cluster1 circuit breaker remains open and overflows
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.cx_open", 1);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_cx", 0);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_cx_overflow", 1);

  // the requests to the aggregate cluster route affect the cluster1 circuit breaker state
  // send a third request directly to cluster1 to confirm this
  auto cluster1_response1 = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/cluster1"}, {":scheme", "http"}, {":authority", "host"}});

  // the third request is rejected because the cluster1 circuit breaker is already open
  // the aggregate cluster circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.cx_open", 0);
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.remaining_cx",
                               1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_cx_overflow", 0);
  // the cluster1 circuit breaker remains open and overflows again
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.cx_open", 1);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_cx", 0);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_cx_overflow", 2);

  // respond to the first request to the aggregate cluster
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // the first request completes successfully
  ASSERT_TRUE(aggregate_cluster_response1->waitForEndStream());
  EXPECT_EQ("200", aggregate_cluster_response1->headers().getStatusValue());

  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  // after completing the first request and closing the connection
  // the aggregate cluster circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.cx_open", 0);
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.remaining_cx",
                               1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_cx_overflow", 0);
  // the cluster1 circuit breaker closes
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.cx_open", 0);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_cx", 1);
  // the overflow may be greater than 2 because after completing the first request
  // the queued pending requests will attempt to reuse the connection
  test_server_->waitForCounterGe("cluster.cluster_1.upstream_cx_overflow", 2);

  cleanupUpstreamAndDownstream();
}

// Tests the max_requests circuit breaker behaviour on the aggregate cluster and its underlying
// cluster1. When requests reach the configured limit, only the underlying cluster1's circuit
// breaker opens, the aggregate cluster's circuit breaker is completely unaffected. When
// cluster1's circuit breaker opens, new requests are rejected, and the overflow counter increases.
// After completing the request, cluster1's circuit breaker closes again.
TEST_P(AggregateIntegrationTest, CircuitBreakerTestMaxRequests) {
  setDownstreamProtocol(Http::CodecType::HTTP2);

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* aggregate_cluster = static_resources->mutable_clusters(1);

    reduceAggregateClustersListToOneCluster(*aggregate_cluster);
    setCircuitBreakerLimits(*aggregate_cluster, CircuitBreakerLimits{}.withMaxRequests(1));
  });

  initialize();

  setCircuitBreakerLimits(cluster1_, CircuitBreakerLimits{}.withMaxRequests(1));

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "55", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                             {cluster1_}, {cluster1_}, {}, "56");
  test_server_->waitForGaugeEq("cluster_manager.active_clusters", 3);

  // initial circuit breaker states:
  // the aggregate cluster circuit breaker is closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_open", 0);
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.remaining_rq",
                               1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_pending_overflow", 0);
  // the cluster1 circuit breaker is closed
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_open", 0);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_rq", 1);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_pending_overflow", 0);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // send a first request to the aggregate cluster
  auto aggregate_cluster_response1 = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/aggregatecluster"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});

  // wait for the first request to arrive at cluster1
  waitForNextUpstreamRequest(FirstUpstreamIndex);

  // after the first request arrives at cluster1 (there is now a single active upstream request)
  // the aggregate cluster circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_open", 0);
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.remaining_rq",
                               1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_pending_overflow", 0);
  // the cluster1 circuit breaker opens
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_open", 1);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_rq", 0);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_pending_overflow", 0);

  // send a second request to the aggregate cluster
  auto aggregate_cluster_response2 = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/aggregatecluster"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});

  ASSERT_TRUE(aggregate_cluster_response2->waitForEndStream());
  // the second request to the aggregate cluster is rejected
  EXPECT_EQ("503", aggregate_cluster_response2->headers().getStatusValue());

  // the aggregate cluster circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_open", 0);
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.remaining_rq",
                               1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_pending_overflow", 0);
  // the cluster1 circuit breaker remains open and overflows
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_open", 1);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_rq", 0);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_pending_overflow", 1);

  // the requests to the aggregate cluster route affect the cluster1 circuit breaker state
  // send a third request directly to cluster1 to confirm this
  auto cluster1_response1 = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/cluster1"}, {":scheme", "http"}, {":authority", "host"}});

  ASSERT_TRUE(cluster1_response1->waitForEndStream());
  // the third request to cluster1 is rejected
  EXPECT_EQ("503", cluster1_response1->headers().getStatusValue());

  // the aggregate cluster circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_open", 0);
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.remaining_rq",
                               1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_pending_overflow", 0);
  // the cluster1 circuit breaker remains open and overflows again
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_open", 1);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_rq", 0);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_pending_overflow", 2);

  // respond to the first request to the aggregate cluster
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  // the first request completes successfully
  ASSERT_TRUE(aggregate_cluster_response1->waitForEndStream());
  EXPECT_EQ("200", aggregate_cluster_response1->headers().getStatusValue());

  // the aggregate cluster circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_open", 0);
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.remaining_rq",
                               1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_pending_overflow", 0);
  // the cluster1 circuit breaker closes
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_open", 0);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_rq", 1);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_pending_overflow", 2);

  cleanupUpstreamAndDownstream();
}

// Tests the max_pending_requests circuit breaker behaviour on the aggregate cluster and its
// underlying cluster1. When pending requests reach the configured limit, only the underlying
// cluster1's circuit breaker opens, the aggregate cluster's circuit breaker is completely
// unaffected. When cluster1's circuit breaker opens, additional pending requests are rejected, and
// the overflow counter increases. After processing the pending requests, cluster1's circuit breaker
// closes again.
TEST_P(AggregateIntegrationTest, CircuitBreakerTestMaxPendingRequests) {
  setDownstreamProtocol(Http::CodecType::HTTP2);

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* aggregate_cluster = static_resources->mutable_clusters(1);

    reduceAggregateClustersListToOneCluster(*aggregate_cluster);
    setCircuitBreakerLimits(*aggregate_cluster,
                            CircuitBreakerLimits{}.withMaxConnections(1).withMaxPendingRequests(1));
    setMaxConcurrentStreams(*aggregate_cluster, 1);
  });

  initialize();

  setCircuitBreakerLimits(cluster1_,
                          CircuitBreakerLimits{}.withMaxConnections(1).withMaxPendingRequests(1));
  setMaxConcurrentStreams(cluster1_, 1);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "55", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                             {cluster1_}, {cluster1_}, {}, "56");
  test_server_->waitForGaugeEq("cluster_manager.active_clusters", 3);

  // initial circuit breaker states:
  // the aggregate cluster circuit breaker is closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_pending_open",
                               0);
  test_server_->waitForGaugeEq(
      "cluster.aggregate_cluster.circuit_breakers.default.remaining_pending", 1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_pending_overflow", 0);
  // the cluster1 circuit breaker is closed
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_pending_open", 0);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_pending", 1);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_pending_overflow", 0);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // send a first request to the aggregate cluster
  auto aggregate_cluster_response1 = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/aggregatecluster"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});

  // wait for the first request to arrive at cluster1
  waitForNextUpstreamRequest(FirstUpstreamIndex);

  // send a second request to the aggregate cluster (this is the first pending request)
  auto aggregate_cluster_response2 = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/aggregatecluster"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});

  // the aggregate cluster circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_pending_open",
                               0);
  test_server_->waitForGaugeEq(
      "cluster.aggregate_cluster.circuit_breakers.default.remaining_pending", 1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_pending_overflow", 0);
  // the cluster1 circuit breaker opens
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_pending_open", 1);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_pending", 0);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_pending_overflow", 0);

  // send a third request to the aggregate cluster (this is the second pending request)
  auto aggregate_cluster_response3 = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/aggregatecluster"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});

  // the third request fails immediately because the cluster1 circuit breaker is open
  ASSERT_TRUE(aggregate_cluster_response3->waitForEndStream());
  EXPECT_EQ("503", aggregate_cluster_response3->headers().getStatusValue());

  // the aggregate cluster circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_pending_open",
                               0);
  test_server_->waitForGaugeEq(
      "cluster.aggregate_cluster.circuit_breakers.default.remaining_pending", 1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_pending_overflow", 0);
  // the cluster1 circuit breaker remains open and overflows
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_pending_open", 1);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_pending", 0);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_pending_overflow", 1);

  // the pending requests to the aggregate cluster route affect the cluster1 circuit breaker state
  // send a fourth request directly to cluster1 to confirm this
  auto cluster1_response1 = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/cluster1"}, {":scheme", "http"}, {":authority", "host"}});

  // the fourth request fails immediately because the cluster1 circuit breaker is open
  ASSERT_TRUE(cluster1_response1->waitForEndStream());
  EXPECT_EQ("503", cluster1_response1->headers().getStatusValue());

  // the aggregate cluster circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_pending_open",
                               0);
  test_server_->waitForGaugeEq(
      "cluster.aggregate_cluster.circuit_breakers.default.remaining_pending", 1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_pending_overflow", 0);
  // the cluster1 circuit breaker remains open and overflows again
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_pending_open", 1);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_pending", 0);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_pending_overflow", 2);

  // respond to the first request to the aggregate cluster
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(aggregate_cluster_response1->waitForEndStream());
  EXPECT_EQ("200", aggregate_cluster_response1->headers().getStatusValue());

  // wait for the second request to arrive at cluster1
  waitForNextUpstreamRequest(FirstUpstreamIndex);

  // respond to the second request to the aggregate cluster
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(aggregate_cluster_response2->waitForEndStream());
  EXPECT_EQ("200", aggregate_cluster_response2->headers().getStatusValue());

  // the aggregate cluster circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_pending_open",
                               0);
  test_server_->waitForGaugeEq(
      "cluster.aggregate_cluster.circuit_breakers.default.remaining_pending", 1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_pending_overflow", 0);
  // the cluster1 circuit breaker closes
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_pending_open", 0);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_pending", 1);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_pending_overflow", 2);

  cleanupUpstreamAndDownstream();
}

// Tests the max_retries circuit breaker behaviour on the aggregate cluster and its underlying
// cluster1. Unlike the other circuit breakers, the aggregate cluster's retry circuit breaker opens
// when retries exceed its configured limit. cluster1's retry circuit breaker opens independently
// when direct requests to cluster1 exceed its configured limit. When either circuit breaker opens,
// additional retries to that cluster are prevented, and overflow counters increase. The two circuit
// breakers operate independently.
TEST_P(AggregateIntegrationTest, CircuitBreakerMaxRetriesTest) {
  setDownstreamProtocol(Http::CodecType::HTTP2);

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* listener = static_resources->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
        http_connection_manager;
    filter->mutable_typed_config()->UnpackTo(&http_connection_manager);
    auto* virtual_host = http_connection_manager.mutable_route_config()->mutable_virtual_hosts(0);
    auto* aggregate_cluster_route = virtual_host->mutable_routes(2);
    auto* cluster1_route = virtual_host->mutable_routes(0);
    aggregate_cluster_route->mutable_route()->mutable_retry_policy()->clear_retry_priority();
    // adjust the retry policy on both the /aggregatecluster and /cluster1 routes
    aggregate_cluster_route->mutable_route()->mutable_retry_policy()->mutable_retry_on()->assign(
        "5xx");
    cluster1_route->mutable_route()->mutable_retry_policy()->mutable_retry_on()->assign("5xx");
    filter->mutable_typed_config()->PackFrom(http_connection_manager);

    auto* aggregate_cluster = static_resources->mutable_clusters(1);
    reduceAggregateClustersListToOneCluster(*aggregate_cluster);
    setCircuitBreakerLimits(*aggregate_cluster, CircuitBreakerLimits{}.withMaxRetries(1));
  });

  initialize();

  setCircuitBreakerLimits(cluster1_, CircuitBreakerLimits{}.withMaxRetries(1));

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "55", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                             {cluster1_}, {cluster1_}, {}, "56");
  test_server_->waitForGaugeEq("cluster_manager.active_clusters", 3);

  // initial circuit breaker states:
  // the aggregate cluster circuit breaker is closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_retry_open",
                               0);
  test_server_->waitForGaugeEq(
      "cluster.aggregate_cluster.circuit_breakers.default.remaining_retries", 1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_retry_overflow", 0);
  // the cluster1 circuit breaker is closed
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_retry_open", 0);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_retries", 1);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_retry_overflow", 0);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // send a first request to the aggregate cluster
  auto aggregate_cluster_response1 = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/aggregatecluster"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});

  // wait for the first request to arrive at cluster1
  waitForNextUpstreamRequest(FirstUpstreamIndex);
  // respond to the first request with a 503 to trigger a retry
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  // wait for the first request retry to arrive at cluster1
  waitForNextUpstreamRequest(FirstUpstreamIndex);
  auto first_request_retry = std::move(upstream_request_);

  // the aggregate_cluster the circuit breaker opens
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_retry_open",
                               1);
  test_server_->waitForGaugeEq(
      "cluster.aggregate_cluster.circuit_breakers.default.remaining_retries", 0);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_retry_overflow", 0);
  // the cluster1 circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_retry_open", 0);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_retries", 1);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_retry_overflow", 0);

  // send a second request to the aggregate cluster
  auto aggregate_cluster_response2 = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/aggregatecluster"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});

  // wait for the second request to arrive at cluster1
  waitForNextUpstreamRequest(FirstUpstreamIndex);
  // respond to the second request with a 503 to trigger a retry
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  // the aggregate_cluster the circuit breaker remains open and overflows
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_retry_open",
                               1);
  test_server_->waitForGaugeEq(
      "cluster.aggregate_cluster.circuit_breakers.default.remaining_retries", 0);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_retry_overflow", 1);
  // the cluster1 circuit breaker remains closed
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_retry_open", 0);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_retries", 1);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_retry_overflow", 0);

  // send a third request directly to cluster1
  auto cluster1_response1 = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/cluster1"}, {":scheme", "http"}, {":authority", "host"}});

  // wait for the third request to arrive at cluster1
  waitForNextUpstreamRequest(FirstUpstreamIndex);
  // respond to the third request with a 503 to trigger a retry
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  // wait for the third request retry to arrive at cluster1
  waitForNextUpstreamRequest(FirstUpstreamIndex);
  auto third_request_retry = std::move(upstream_request_);

  // the aggregate_cluster the circuit breaker remains open
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_retry_open",
                               1);
  test_server_->waitForGaugeEq(
      "cluster.aggregate_cluster.circuit_breakers.default.remaining_retries", 0);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_retry_overflow", 1);
  // the cluster1 circuit breaker opens
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_retry_open", 1);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_retries", 0);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_retry_overflow", 0);

  // send a fourth request directly to cluster1
  auto cluster1_response2 = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/cluster1"}, {":scheme", "http"}, {":authority", "host"}});

  // wait for the fourth request to arrive at cluster1
  waitForNextUpstreamRequest(FirstUpstreamIndex);
  // respond to the fourth request with a 503 to trigger a retry
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  // the aggregate_cluster the circuit breaker remains open
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_retry_open",
                               1);
  test_server_->waitForGaugeEq(
      "cluster.aggregate_cluster.circuit_breakers.default.remaining_retries", 0);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_retry_overflow", 1);
  // the cluster1 circuit breaker remains open and overflows
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_retry_open", 1);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_retries", 0);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_retry_overflow", 1);

  // respond to the third request to cluster1
  third_request_retry->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(cluster1_response1->waitForEndStream());
  EXPECT_EQ("200", cluster1_response1->headers().getStatusValue());
  // handle the response to the fourth request to cluster1
  ASSERT_TRUE(cluster1_response2->waitForEndStream());
  EXPECT_EQ("503", cluster1_response2->headers().getStatusValue());
  // respond to the first request to the aggregate cluster
  first_request_retry->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(aggregate_cluster_response1->waitForEndStream());
  EXPECT_EQ("200", aggregate_cluster_response1->headers().getStatusValue());
  // handle the response to the second request to the aggregate cluster
  ASSERT_TRUE(aggregate_cluster_response2->waitForEndStream());
  EXPECT_EQ("503", aggregate_cluster_response2->headers().getStatusValue());

  // the aggregate cluster circuit breaker is closed
  test_server_->waitForGaugeEq("cluster.aggregate_cluster.circuit_breakers.default.rq_retry_open",
                               0);
  test_server_->waitForGaugeEq(
      "cluster.aggregate_cluster.circuit_breakers.default.remaining_retries", 1);
  test_server_->waitForCounterEq("cluster.aggregate_cluster.upstream_rq_retry_overflow", 1);
  // the cluster1 circuit breaker is closed
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.rq_retry_open", 0);
  test_server_->waitForGaugeEq("cluster.cluster_1.circuit_breakers.default.remaining_retries", 1);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_retry_overflow", 1);

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy

#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class LocalRateLimitFilterIntegrationTest : public Event::TestUsingSimulatedTime,
                                            public HttpProtocolIntegrationTest {
protected:
  void initializeFilter(const std::string& filter_config) {
    config_helper_.prependFilter(filter_config);
    initialize();
  }

  void initializeFilterWithRds(const std::string& filter_config,
                               const std::string& route_config_name,
                               const std::string& initial_route_config) {
    // Set this flag to true to create fake upstream for xds_cluster.
    create_xds_upstream_ = true;
    // Create static XDS cluster.
    createXdsCluster();

    config_helper_.prependFilter(filter_config);

    // Set RDS config source.
    config_helper_.addConfigModifier(
        [route_config_name](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_rds()->set_route_config_name(route_config_name);
          hcm.mutable_rds()->mutable_config_source()->set_resource_api_version(
              envoy::config::core::v3::ApiVersion::V3);
          envoy::config::core::v3::ApiConfigSource* rds_api_config_source =
              hcm.mutable_rds()->mutable_config_source()->mutable_api_config_source();
          rds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
          rds_api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
          envoy::config::core::v3::GrpcService* grpc_service =
              rds_api_config_source->add_grpc_services();
          grpc_service->mutable_envoy_grpc()->set_cluster_name("xds_cluster");
        });

    on_server_init_function_ = [&]() {
      AssertionResult result =
          fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, xds_connection_);
      RELEASE_ASSERT(result, result.message());
      result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
      RELEASE_ASSERT(result, result.message());
      xds_stream_->startGrpcStream();

      EXPECT_TRUE(compareSotwDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                              {route_config_name}, true));
      sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
          Config::TypeUrl::get().RouteConfiguration,
          {TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(
              initial_route_config)},
          "1");
    };
    initialize();
    registerTestServerPorts({"http"});
  }

  void initializeFilterWithLocalCluster(const std::string& filter_config,
                                        const std::string& initial_local_cluster_endpoints) {
    config_helper_.prependFilter(filter_config);

    // Set this flag to true to create fake upstream for xds_cluster.
    create_xds_upstream_ = true;
    // Create static XDS cluster.
    createXdsCluster();

    // Create local cluster.
    createLocalCluster();

    on_server_init_function_ = [&]() {
      AssertionResult result =
          fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, xds_connection_);
      RELEASE_ASSERT(result, result.message());
      result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
      RELEASE_ASSERT(result, result.message());
      xds_stream_->startGrpcStream();

      EXPECT_TRUE(compareSotwDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                              {"local_cluster"}, true));
      sendSotwDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
          Config::TypeUrl::get().ClusterLoadAssignment,
          {TestUtility::parseYaml<envoy::config::endpoint::v3::ClusterLoadAssignment>(
              initial_local_cluster_endpoints)},
          "1");
    };
    initialize();
  }

  void createXdsCluster() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      xds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      xds_cluster->set_name("xds_cluster");
      ConfigHelper::setHttp2(*xds_cluster);
    });
  }

  void createLocalCluster() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Set local cluster name to "local_cluster".
      bootstrap.mutable_cluster_manager()->set_local_cluster_name("local_cluster");

      // Create local cluster.
      auto* local_cluster = bootstrap.mutable_static_resources()->add_clusters();
      local_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      local_cluster->set_name("local_cluster");
      local_cluster->clear_load_assignment();

      // This should be EDS cluster to load endpoints dynamically.
      local_cluster->set_type(::envoy::config::cluster::v3::Cluster::EDS);
      local_cluster->mutable_eds_cluster_config()->set_service_name("local_cluster");
      local_cluster->mutable_eds_cluster_config()->mutable_eds_config()->set_resource_api_version(
          envoy::config::core::v3::ApiVersion::V3);
      envoy::config::core::v3::ApiConfigSource* eds_api_config_source =
          local_cluster->mutable_eds_cluster_config()
              ->mutable_eds_config()
              ->mutable_api_config_source();
      eds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      eds_api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
      envoy::config::core::v3::GrpcService* grpc_service =
          eds_api_config_source->add_grpc_services();
      grpc_service->mutable_envoy_grpc()->set_cluster_name("xds_cluster");
    });
  }

  void cleanUpXdsConnection() {
    if (xds_connection_ != nullptr) {
      AssertionResult result = xds_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = xds_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      xds_connection_ = nullptr;
    }
  }

  static constexpr absl::string_view filter_config_ =
      R"EOF(
name: envoy.filters.http.local_ratelimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
  stat_prefix: http_local_rate_limiter
  token_bucket:
    max_tokens: 1
    tokens_per_fill: 1
    fill_interval: 1000s
  filter_enabled:
    runtime_key: local_rate_limit_enabled
    default_value:
      numerator: 100
      denominator: HUNDRED
  filter_enforced:
    runtime_key: local_rate_limit_enforced
    default_value:
      numerator: 100
      denominator: HUNDRED
  response_headers_to_add:
    - append_action: OVERWRITE_IF_EXISTS_OR_ADD
      header:
        key: x-local-rate-limit
        value: 'true'
  local_rate_limit_per_downstream_connection: {}
)EOF";

  const std::string filter_config_with_local_cluster_rate_limit_ =
      R"EOF(
name: envoy.filters.http.local_ratelimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
  stat_prefix: http_local_rate_limiter
  token_bucket:
    max_tokens: 1
    tokens_per_fill: 1
    fill_interval: 1000s
  filter_enabled:
    runtime_key: local_rate_limit_enabled
    default_value:
      numerator: 100
      denominator: HUNDRED
  filter_enforced:
    runtime_key: local_rate_limit_enforced
    default_value:
      numerator: 100
      denominator: HUNDRED
  response_headers_to_add:
    - append_action: OVERWRITE_IF_EXISTS_OR_ADD
      header:
        key: x-local-rate-limit
        value: 'true'
  local_cluster_rate_limit: {}
)EOF";

  const std::string initial_local_cluster_endpoints_ = R"EOF(
cluster_name: local_cluster
endpoints:
- lb_endpoints:
  - endpoint:
      address:
        socket_address:
          address: 127.0.0.1
          port_value: 80
)EOF";

  const std::string update_local_cluster_endpoints_ = R"EOF(
cluster_name: local_cluster
endpoints:
- lb_endpoints:
  - endpoint:
      address:
        socket_address:
          address: 127.0.0.1
          port_value: 80
  - endpoint:
      address:
        socket_address:
          address: 127.0.0.1
          port_value: 81
)EOF";

  const std::string initial_route_config_ = R"EOF(
name: basic_routes
virtual_hosts:
- name: rds_vhost_1
  domains: ["*"]
  routes:
  - match:
      prefix: "/"
    route:
      cluster: cluster_0
    typed_per_filter_config:
      envoy.filters.http.local_ratelimit:
        "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
        stat_prefix: http_local_rate_limiter
        token_bucket:
          max_tokens: 1
          tokens_per_fill: 1
          fill_interval: 1000s
        filter_enabled:
          runtime_key: local_rate_limit_enabled
          default_value:
            numerator: 100
            denominator: HUNDRED
        filter_enforced:
          runtime_key: local_rate_limit_enforced
          default_value:
            numerator: 100
            denominator: HUNDRED
        response_headers_to_add:
          - append_action: OVERWRITE_IF_EXISTS_OR_ADD
            header:
              key: x-local-rate-limit
              value: 'true'
        local_rate_limit_per_downstream_connection: true
)EOF";

  const std::string update_route_config_ = R"EOF(
name: basic_routes
virtual_hosts:
- name: rds_vhost_1
  domains: ["*"]
  routes:
  - match:
      prefix: "/"
    route:
      cluster: cluster_0
    typed_per_filter_config:
      envoy.filters.http.local_ratelimit:
        "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
        stat_prefix: http_local_rate_limiter
        token_bucket:
          max_tokens: 1
          tokens_per_fill: 1
          fill_interval: 1000s
        filter_enabled:
          runtime_key: local_rate_limit_enabled
          default_value:
            numerator: 100
            denominator: HUNDRED
        filter_enforced:
          runtime_key: local_rate_limit_enforced
          default_value:
            numerator: 100
            denominator: HUNDRED
        response_headers_to_add:
          - header:
              key: x-local-rate-limit
              value: 'true'
        local_rate_limit_per_downstream_connection: true
)EOF";
};

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, LocalRateLimitFilterIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(LocalRateLimitFilterIntegrationTest, DenyRequestPerProcess) {
  initializeFilter(fmt::format(filter_config_, "false"));

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 0);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, 1);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, response->body().size());

  cleanupUpstreamAndDownstream();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  response = codec_client_->makeRequestWithBody(default_request_headers_, 0);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("429", response->headers().getStatusValue());
  EXPECT_EQ(18, response->body().size());
}

TEST_P(LocalRateLimitFilterIntegrationTest, DenyRequestWithinSameConnection) {
  initializeFilter(fmt::format(filter_config_, "true"));

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 0);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, 1);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, response->body().size());

  response = codec_client_->makeRequestWithBody(default_request_headers_, 0);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("429", response->headers().getStatusValue());
  EXPECT_EQ(18, response->body().size());
}

TEST_P(LocalRateLimitFilterIntegrationTest, PermitRequestAcrossDifferentConnections) {
  initializeFilter(fmt::format(filter_config_, "true"));

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 0);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, 1);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, response->body().size());

  cleanupUpstreamAndDownstream();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  response = codec_client_->makeRequestWithBody(default_request_headers_, 0);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, 1);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, response->body().size());
}

TEST_P(LocalRateLimitFilterIntegrationTest, BasicTestPerRouteAndRds) {
  initializeFilterWithRds(fmt::format(filter_config_, true), "basic_routes", initial_route_config_);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 0);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, 1);

  // Update route config by RDS when request is sending. Test whether RDS can work normally.
  sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration,
      {TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(update_route_config_)},
      "2");
  test_server_->waitForCounterGe("http.config_test.rds.basic_routes.update_success", 2);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, response->body().size());

  cleanupUpstreamAndDownstream();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  response = codec_client_->makeRequestWithBody(default_request_headers_, 0);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, 1);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, response->body().size());

  cleanupUpstreamAndDownstream();

  cleanUpXdsConnection();
}

TEST_P(LocalRateLimitFilterIntegrationTest, TestLocalClusterRateLimit) {
  initializeFilterWithLocalCluster(filter_config_with_local_cluster_rate_limit_,
                                   initial_local_cluster_endpoints_);

  auto share_provider_manager =
      test_server_->server()
          .singletonManager()
          .getTyped<Extensions::Filters::Common::LocalRateLimit::ShareProviderManager>(
              "local_ratelimit_share_provider_manager_singleton");
  ASSERT(share_provider_manager != nullptr);
  auto share_provider = share_provider_manager->getShareProvider({});

  test_server_->waitForGaugeEq("cluster.local_cluster.membership_total", 1);
  simTime().advanceTimeWait(std::chrono::milliseconds(1));

  EXPECT_EQ(1.0, share_provider->getTokensShareFactor());

  sendSotwDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment,
      {TestUtility::parseYaml<envoy::config::endpoint::v3::ClusterLoadAssignment>(
          update_local_cluster_endpoints_)},
      "2");

  test_server_->waitForGaugeEq("cluster.local_cluster.membership_total", 2);
  simTime().advanceTimeWait(std::chrono::milliseconds(1));

  EXPECT_EQ(0.5, share_provider->getTokensShareFactor());

  cleanUpXdsConnection();
}

} // namespace
} // namespace Envoy

#include "envoy/extensions/router/cluster_specifiers/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {
namespace {

using DynamicModuleClusterSpecifierProto = envoy::extensions::router::cluster_specifiers::
    dynamic_modules::v3::DynamicModuleClusterSpecifier;

// Reports the subset the upstream host belongs to, so that a test can tell which endpoint of the
// subset load balancing cluster served the request.
constexpr char HostVersionHeader[] = "x-host-version";

class DynamicModuleClusterSpecifierIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleClusterSpecifierIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    autonomous_upstream_ = true;
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
  }

  void setupTest(bool refresh_filter = false) {
    if (refresh_filter) {
      config_helper_.prependFilter(R"EOF(
        name: refresh-route-cluster
        typed_config:
          "@type": type.googleapis.com/test.integration.filters.RefreshRouteClusterConfig
      )EOF");
    }

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          const std::string specifier_yaml = R"EOF(
dynamic_module_config:
  name: cluster_specifier_integration_test
specifier_name: test_cluster_specifier
route_action_overrides:
  canary:
    retry_policy:
      retry_on: 5xx
      num_retries: 3
    metadata_match:
      filter_metadata:
        envoy.lb:
          version: canary
    request_mirror_policies:
    - cluster: mirror-cluster
  stable:
    metadata_match:
      filter_metadata:
        envoy.lb:
          version: stable
  cross-cluster:
    retry_policy:
      retry_on: 5xx
      num_retries: 3
      refresh_cluster_on_retry: true
)EOF";
          DynamicModuleClusterSpecifierProto specifier_config;
          TestUtility::loadFromYaml(specifier_yaml, specifier_config);

          auto* route_action = hcm.mutable_route_config()
                                   ->mutable_virtual_hosts(0)
                                   ->mutable_routes(0)
                                   ->mutable_route();
          route_action->clear_cluster();
          auto* plugin =
              route_action->mutable_inline_cluster_specifier_plugin()->mutable_extension();
          plugin->set_name("dynamic-module-specifier");
          std::ignore = plugin->mutable_typed_config()->PackFrom(specifier_config);
        });
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_static_resources()->mutable_clusters(0)->set_name("prod");
    });

    HttpIntegrationTest::initialize();
  }

  // Turns the upstream cluster into a subset load balancing cluster with one endpoint per version,
  // and reports the version of the endpoint that served the request. NO_FALLBACK makes a request
  // without matching criteria fail, so the criteria the module selected are what drives the choice.
  void configureSubsetLoadBalancing() {
    setUpstreamCount(2);
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      TestUtility::loadFromYaml(R"EOF(
fallback_policy: NO_FALLBACK
subset_selectors:
- keys:
  - version
)EOF",
                                *cluster->mutable_lb_subset_config());

      auto* locality = cluster->mutable_load_assignment()->mutable_endpoints(0);
      auto* stable_endpoint = locality->mutable_lb_endpoints(0);
      // The second endpoint reuses the same loopback address with port 0 so that setPorts() assigns
      // it the next fake upstream. Copy the address before setting metadata.
      auto* canary_endpoint = locality->add_lb_endpoints();
      canary_endpoint->mutable_endpoint()->mutable_address()->CopyFrom(
          stable_endpoint->endpoint().address());
      TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.lb:
    version: stable
)EOF",
                                *stable_endpoint->mutable_metadata());
      TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.lb:
    version: canary
)EOF",
                                *canary_endpoint->mutable_metadata());
    });
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* header = hcm.mutable_route_config()
                             ->mutable_virtual_hosts(0)
                             ->add_response_headers_to_add()
                             ->mutable_header();
          header->set_key(HostVersionHeader);
          header->set_value("%UPSTREAM_METADATA(envoy.lb:version)%");
        });
  }

  // Adds a copy of the upstream cluster under a second name, so that a test can tell which of two
  // clusters served a request rather than relying on the only cluster in the configuration.
  void addClusterCopy(const std::string& name) {
    setUpstreamCount(2);
    config_helper_.addConfigModifier([name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->MergeFrom(bootstrap.static_resources().clusters(0));
      cluster->set_name(name);
    });
  }

  // Closes the high priority circuit breaker of the upstream cluster and leaves the default one
  // open, so that the priority the module selected decides whether a request is admitted.
  void configureClosedHighPriorityCircuitBreaker() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* threshold = bootstrap.mutable_static_resources()
                            ->mutable_clusters(0)
                            ->mutable_circuit_breakers()
                            ->add_thresholds();
      threshold->set_priority(envoy::config::core::v3::HIGH);
      threshold->mutable_max_requests()->set_value(0);
    });
  }

  Http::TestRequestHeaderMapImpl
  requestHeaders(const std::vector<std::pair<std::string, std::string>>& extra_headers = {}) {
    Http::TestRequestHeaderMapImpl headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};
    for (const auto& [key, value] : extra_headers) {
      headers.setCopy(Http::LowerCaseString(key), value);
    }
    return headers;
  }

  std::string hostVersion(const Http::ResponseHeaderMap& headers) {
    const auto values = headers.get(Http::LowerCaseString(HostVersionHeader));
    return values.empty() ? "" : std::string(values[0]->value().getStringView());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleClusterSpecifierIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// The module selects the upstream cluster from the request header.
TEST_P(DynamicModuleClusterSpecifierIntegrationTest, SelectsCluster) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"env", "prod"}}));
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Setting every property the module can select still routes the request.
TEST_P(DynamicModuleClusterSpecifierIntegrationTest, SelectsClusterWithRouteActionOverride) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeHeaderOnlyRequest(requestHeaders({{"env", "prod"},
                                                           {"x-override", "canary"},
                                                           {"x-timeout-ms", "5000"},
                                                           {"x-idle-timeout-ms", "5000"},
                                                           {"x-buffer-limit", "9999"},
                                                           {"x-priority", "high"}}));
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// The priority the module selected reaches the router, so the request is charged against the high
// priority circuit breaker rather than the default one.
TEST_P(DynamicModuleClusterSpecifierIntegrationTest, SelectedPriorityIsUsed) {
  configureClosedHighPriorityCircuitBreaker();
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"env", "prod"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, test_server_->counter("cluster.prod.upstream_rq_active_overflow")->value());

  response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"env", "prod"}, {"x-priority", "high"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.prod.upstream_rq_active_overflow")->value());
}

// A cluster the module selects that does not exist fails the request rather than crashing.
TEST_P(DynamicModuleClusterSpecifierIntegrationTest, UnknownClusterFailsRequest) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"env", "nonexistent"}}));
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  // The request must fail because the selected cluster is missing, not for some earlier reason.
  EXPECT_EQ(1, test_server_->counter("http.config_test.no_cluster")->value());
}

// Without a decision from the module the matched route has no cluster, so the request fails.
TEST_P(DynamicModuleClusterSpecifierIntegrationTest, NoDecisionFailsRequest) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders());
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  // A route with no cluster resolves to an empty cluster name rather than falling back to a route.
  EXPECT_EQ(1, test_server_->counter("http.config_test.no_cluster")->value());
}

// A filter refreshing the route cluster re-enters the module, and the new decision is used for the
// upstream request.
TEST_P(DynamicModuleClusterSpecifierIntegrationTest, RefreshRouteCluster) {
  setupTest(true);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The initial selection has no env header so the module reports no decision and the matched route
  // has no cluster. The filter then sets 'env: prod' and refreshes, which routes to the prod
  // cluster.
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders());
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  EXPECT_EQ("", response->headers()
                    .get(Http::LowerCaseString("initial-cluster"))[0]
                    ->value()
                    .getStringView());
  EXPECT_EQ("false", response->headers()
                         .get(Http::LowerCaseString("has-initial-cluster-info"))[0]
                         ->value()
                         .getStringView());
  EXPECT_EQ("prod", response->headers()
                        .get(Http::LowerCaseString("refreshed-cluster"))[0]
                        ->value()
                        .getStringView());
  EXPECT_EQ("true", response->headers()
                        .get(Http::LowerCaseString("has-refreshed-cluster-info"))[0]
                        ->value()
                        .getStringView());
}

// The metadata match criteria of the selected override reach the subset load balancer, so the
// override decides which endpoint of the cluster serves the request.
TEST_P(DynamicModuleClusterSpecifierIntegrationTest, OverrideSelectsLoadBalancingSubset) {
  configureSubsetLoadBalancing();
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"env", "prod"}, {"x-override", "canary"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("canary", hostVersion(response->headers()));

  response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"env", "prod"}, {"x-override", "stable"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("stable", hostVersion(response->headers()));
}

// Criteria selected on a refresh reach the subset load balancer, since the upstream attempt reads
// them after the refresh rather than when the route was first resolved.
TEST_P(DynamicModuleClusterSpecifierIntegrationTest, RefreshSelectsLoadBalancingSubset) {
  configureSubsetLoadBalancing();
  setupTest(true);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The request carries no env header, so the module reaches no decision until the filter injects
  // one and refreshes.
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"x-override", "canary"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("canary", hostVersion(response->headers()));
}

// Without an override the criteria of the matched route are in effect, and the matched route has
// none, so the NO_FALLBACK subset load balancer finds no host.
TEST_P(DynamicModuleClusterSpecifierIntegrationTest, NoOverrideFindsNoLoadBalancingSubset) {
  configureSubsetLoadBalancing();
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"env", "prod"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(0, test_server_->counter("cluster.prod.upstream_rq_total")->value());
}

// The request mirroring policies of the selected override reach the router, so the mirror cluster
// receives a copy of the request.
TEST_P(DynamicModuleClusterSpecifierIntegrationTest, OverrideMirrorsRequest) {
  addClusterCopy("mirror-cluster");
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"env", "prod"}, {"x-override", "canary"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.mirror-cluster.upstream_rq_total", testing::Eq(1));
}

// Without an override the mirroring policies of the matched route are in effect, and the matched
// route has none.
TEST_P(DynamicModuleClusterSpecifierIntegrationTest, NoOverrideDoesNotMirrorRequest) {
  addClusterCopy("mirror-cluster");
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"env", "prod"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.prod.upstream_rq_total", testing::Eq(1));
  EXPECT_EQ(0, test_server_->counter("cluster.mirror-cluster.upstream_rq_total")->value());
}

// Tests that drive the upstream response so that the properties the module selected are observable.
class DynamicModuleClusterSpecifierUpstreamIntegrationTest
    : public DynamicModuleClusterSpecifierIntegrationTest {
public:
  DynamicModuleClusterSpecifierUpstreamIntegrationTest() { autonomous_upstream_ = false; }

  // Fails the pending upstream request and drops the connection, so that a retry arrives on a fresh
  // connection rather than depending on how the pool reuses this one.
  void failUpstreamRequest() {
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    fake_upstream_connection_.reset();
  }

  // Waits only for the upstream request headers, for the tests that leave the downstream request
  // open so that waitForNextUpstreamRequest() would never see the end of the stream.
  void waitForUpstreamRequestHeaders() {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  }

  // Timeouts are given room to survive a loaded or instrumented test machine.
  static std::string timeoutMs() { return absl::StrCat(300 * TIMEOUT_FACTOR); }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleClusterSpecifierUpstreamIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// The retry policy of the selected override reaches the router, so a failed attempt is retried even
// though the matched route configures no retries.
TEST_P(DynamicModuleClusterSpecifierUpstreamIntegrationTest, OverrideRetriesRequest) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"env", "prod"}, {"x-override", "canary"}}));
  failUpstreamRequest();
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.prod.upstream_rq_retry")->value());
}

// A retry whose policy refreshes the cluster runs the module again, and the cluster it selects for
// that attempt is the one the retry reaches.
TEST_P(DynamicModuleClusterSpecifierUpstreamIntegrationTest, RetryReSelectsCluster) {
  addClusterCopy("staging");
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders(
      {{"env", "prod"}, {"x-override", "cross-cluster"}, {"x-retry-cluster", "staging"}}));

  // The first attempt reaches the cluster named after the env header, and fails.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  // The retry re-enters the module, which selects the second cluster for this attempt.
  FakeHttpConnectionPtr staging_connection;
  FakeStreamPtr staging_request;
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, staging_connection));
  ASSERT_TRUE(staging_connection->waitForNewStream(*dispatcher_, staging_request));
  ASSERT_TRUE(staging_request->waitForEndStream(*dispatcher_));
  staging_request->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.prod.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.staging.upstream_rq_total")->value());
  ASSERT_TRUE(staging_connection->close());
}

// Without an override the retry policy of the matched route is in effect, and the matched route
// configures no retries, so the failed response reaches the client.
TEST_P(DynamicModuleClusterSpecifierUpstreamIntegrationTest, NoOverrideDoesNotRetryRequest) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"env", "prod"}}));
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(0, test_server_->counter("cluster.prod.upstream_rq_retry")->value());
}

// The timeout the module selected reaches the router, so an upstream that never responds ends the
// request rather than the matched route timeout, which is far longer.
TEST_P(DynamicModuleClusterSpecifierUpstreamIntegrationTest, SelectedTimeoutIsUsed) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"env", "prod"}, {"x-timeout-ms", timeoutMs()}}));
  waitForNextUpstreamRequest();
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("504", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.prod.upstream_rq_timeout")->value());
}

// The stream idle timeout the module selected reaches the connection manager, so a request the
// downstream never completes is timed out.
TEST_P(DynamicModuleClusterSpecifierUpstreamIntegrationTest, SelectedIdleTimeoutIsUsed) {
  config_helper_.disableDelayClose();
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The request is left without end stream and the upstream does not respond, so the stream is idle
  // until the timeout fires.
  auto encoder_decoder = codec_client_->startRequest(
      requestHeaders({{"env", "prod"}, {"x-idle-timeout-ms", timeoutMs()}}));
  auto response = std::move(encoder_decoder.second);
  waitForUpstreamRequestHeaders();

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_EQ("408", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_idle_timeout")->value());
}

// The request body buffer limit the module selected reaches the router, so a body larger than the
// limit makes the router give up on buffering for retries.
TEST_P(DynamicModuleClusterSpecifierUpstreamIntegrationTest, SelectedRequestBodyBufferLimitIsUsed) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The canary override enables retries, which is what makes the router buffer the request body.
  auto encoder_decoder = codec_client_->startRequest(
      requestHeaders({{"env", "prod"}, {"x-override", "canary"}, {"x-buffer-limit", "16"}}));
  auto& encoder = encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  waitForUpstreamRequestHeaders();

  Buffer::OwnedImpl body(std::string(64, 'a'));
  encoder.encodeData(body, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.prod.retry_or_shadow_abandoned")->value());
}

} // namespace
} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy

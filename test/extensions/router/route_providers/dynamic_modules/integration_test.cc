#include <tuple>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/router/route_providers/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace RouteProviders {
namespace DynamicModules {
namespace {

using DynamicModuleRouteProviderProto =
    envoy::extensions::router::route_providers::dynamic_modules::v3::DynamicModuleRouteProvider;

// Reports the version of the upstream host that served the request, so that a subset load balancing
// test can tell which endpoint the metadata match criteria the module selected chose.
constexpr char HostVersionHeader[] = "x-host-version";

class DynamicModuleRouteProviderIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleRouteProviderIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    autonomous_upstream_ = true;
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
  }

  void setupTest() {
    // A subset test adds a second endpoint to cluster_0 and a mirror test adds a third cluster, so
    // the upstream count follows what the enabled feature needs.
    setUpstreamCount(subset_lb_ || mirror_override_ ? 3 : 2);
    // Rename the static cluster to cluster_0 and add a copy cluster_1, so that a test can tell
    // which route template served the request from the per-cluster request counters.
    config_helper_.addConfigModifier([subset = subset_lb_, mirror = mirror_override_](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* clusters = bootstrap.mutable_static_resources()->mutable_clusters();
      clusters->Mutable(0)->set_name("cluster_0");
      auto* cluster_1 = clusters->Add();
      cluster_1->MergeFrom(clusters->Get(0));
      cluster_1->set_name("cluster_1");
      if (mirror) {
        auto* mirror_cluster = clusters->Add();
        mirror_cluster->MergeFrom(clusters->Get(0));
        mirror_cluster->set_name("mirror-cluster");
      }
      // Applied after the copies so cluster_1 and the mirror cluster stay single endpoint
      // clusters rather than inheriting the subset endpoints.
      if (subset) {
        applySubsetLoadBalancing(*clusters->Mutable(0));
      }
    });
    config_helper_.addConfigModifier(
        [subset = subset_lb_, mirror = mirror_override_, per_route = per_route_config_](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
          virtual_host->clear_routes();
          // A subset test reports the version of the selected endpoint, so the metadata match
          // criteria the module selected are observable end to end.
          if (subset) {
            auto* header = virtual_host->add_response_headers_to_add()->mutable_header();
            header->set_key(HostVersionHeader);
            header->set_value("%UPSTREAM_METADATA(envoy.lb:version)%");
          }
          auto* route_provider = virtual_host->mutable_route_provider();

          // The first template carries a request header the module can remove, so that a test can
          // tell the module removed it apart from it never having been added.
          TestUtility::loadFromYaml(R"EOF(
match:
  prefix: "/"
route:
  cluster: cluster_0
request_headers_to_add:
- header:
    key: x-remove-me
    value: present
)EOF",
                                    *route_provider->add_route_templates());
          TestUtility::loadFromYaml(R"EOF(
match:
  prefix: "/"
route:
  cluster: cluster_1
)EOF",
                                    *route_provider->add_route_templates());

          DynamicModuleRouteProviderProto provider_config;
          TestUtility::loadFromYaml(R"EOF(
dynamic_module_config:
  name: route_provider_integration_test
provider_name: test_route_provider
route_action_overrides:
  canary:
    retry_policy:
      retry_on: 5xx
      num_retries: 3
    metadata_match:
      filter_metadata:
        envoy.lb:
          version: canary
  stable:
    metadata_match:
      filter_metadata:
        envoy.lb:
          version: stable
)EOF",
                                    provider_config);
          // A statically named mirror cluster has to exist when the route configuration is
          // validated, so the mirror policy is only declared when the mirror cluster is present.
          if (mirror) {
            (*provider_config.mutable_route_action_overrides())["canary"]
                .add_request_mirror_policies()
                ->set_cluster("mirror-cluster");
          }
          if (per_route) {
            TestUtility::loadFromYaml(
                R"EOF(
typed_per_filter_config:
  envoy.filters.http.header_mutation:
    "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutationPerRoute
    mutations:
      response_mutations:
      - append:
          header:
            key: x-per-route-applied
            value: "yes"
          append_action: OVERWRITE_IF_EXISTS_OR_ADD
)EOF",
                (*provider_config.mutable_per_route_config_overrides())["mutate"]);
          }
          auto* resolver = route_provider->mutable_resolver();
          resolver->set_name("envoy.router.route_provider.dynamic_modules");
          std::ignore = resolver->mutable_typed_config()->PackFrom(provider_config);
        });

    HttpIntegrationTest::initialize();
  }

  // Enables a subset load balancing cluster_0 with a stable and a canary endpoint, so that a test
  // can assert the metadata match criteria the module selected decide which endpoint serves the
  // request.
  void configureSubsetLoadBalancing() { subset_lb_ = true; }

  // Declares the request mirroring policy of the canary override together with the mirror cluster
  // it names, since the two cannot be configured independently.
  void configureMirrorOverride() { mirror_override_ = true; }

  // Adds a header_mutation filter and declares a per-route configuration override that adds a
  // response header through it, so the per-filter configuration the module selects is observable
  // end to end.
  void configurePerRouteConfigOverride() {
    per_route_config_ = true;
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.header_mutation
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
)EOF");
  }

  // Turns a cluster into a subset load balancing cluster with one endpoint per version and reports
  // the version through the endpoint metadata. NO_FALLBACK makes a request without matching
  // criteria find no host, so the criteria the module selected are what drives the choice.
  static void applySubsetLoadBalancing(envoy::config::cluster::v3::Cluster& cluster) {
    TestUtility::loadFromYaml(R"EOF(
fallback_policy: NO_FALLBACK
subset_selectors:
- keys:
  - version
)EOF",
                              *cluster.mutable_lb_subset_config());
    auto* locality = cluster.mutable_load_assignment()->mutable_endpoints(0);
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
  }

  // Closes the high priority circuit breaker of the first cluster and leaves the default one open,
  // so that the priority the module selected decides whether a request is admitted.
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

  std::string headerValue(const Http::ResponseHeaderMap& headers, absl::string_view name) {
    const auto values = headers.get(Http::LowerCaseString(name));
    return values.empty() ? "" : std::string(values[0]->value().getStringView());
  }

  std::string hostVersion(const Http::ResponseHeaderMap& headers) {
    return headerValue(headers, HostVersionHeader);
  }

  bool subset_lb_{false};
  bool mirror_override_{false};
  bool per_route_config_{false};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleRouteProviderIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// The index the module selects picks the route template, so a request is routed to the cluster of
// the selected template.
TEST_P(DynamicModuleRouteProviderIntegrationTest, SelectsTemplateByIndex) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"route-index", "0"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.cluster_0.upstream_rq_total", testing::Eq(1));

  response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"route-index", "1"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.cluster_1.upstream_rq_total", testing::Eq(1));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
}

// When the module selects no route Envoy resolves no route, so the request gets a 404.
TEST_P(DynamicModuleRouteProviderIntegrationTest, NoRouteWhenModuleSelectsNone) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"x-no-route", "true"}}));
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
}

// An out of range index makes the module select no route, so the request gets a 404.
TEST_P(DynamicModuleRouteProviderIntegrationTest, OutOfRangeIndexNoRoute) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"route-index", "9"}}));
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
}

// The cluster the module selects replaces the cluster of the selected route template, so the
// request reaches the overridden cluster rather than the one the template names.
TEST_P(DynamicModuleRouteProviderIntegrationTest, ClusterOverride) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-cluster", "cluster_1"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.cluster_1.upstream_rq_total", testing::Eq(1));
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
}

// The priority the module selected reaches the router, so the request is charged against the high
// priority circuit breaker rather than the default one.
TEST_P(DynamicModuleRouteProviderIntegrationTest, SelectedPriorityIsUsed) {
  configureClosedHighPriorityCircuitBreaker();
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"route-index", "0"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_rq_active_overflow")->value());

  response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-priority", "high"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_active_overflow")->value());
}

// The route metadata the module set is layered onto the selected template, so the METADATA(ROUTE)
// formatter that a response header uses observes every metadata entry end to end.
TEST_P(DynamicModuleRouteProviderIntegrationTest, RouteMetadataVisibleToFormatter) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
        const auto add_header = [virtual_host](absl::string_view name, absl::string_view key) {
          auto* header = virtual_host->add_response_headers_to_add()->mutable_header();
          header->set_key(std::string(name));
          header->set_value(absl::StrCat("%METADATA(ROUTE:envoy.test.route:", key, ")%"));
        };
        add_header("x-route-metadata-string", "string_key");
        add_header("x-route-metadata-number", "number_key");
        add_header("x-route-metadata-bool", "bool_key");
        add_header("x-route-metadata-struct", "struct_key");
      });
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeHeaderOnlyRequest(requestHeaders({{"route-index", "0"},
                                                           {"x-route-meta-string", "shard-a"},
                                                           {"x-route-meta-number", "0.5"},
                                                           {"x-route-meta-bool", "true"},
                                                           {"x-route-meta-struct", "1"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("shard-a", headerValue(response->headers(), "x-route-metadata-string"));
  EXPECT_EQ("0.5", headerValue(response->headers(), "x-route-metadata-number"));
  EXPECT_EQ("true", headerValue(response->headers(), "x-route-metadata-bool"));
  EXPECT_EQ("struct-val", headerValue(response->headers(), "x-route-metadata-struct"));
}

// The response header mutation the module recorded reaches the downstream response.
TEST_P(DynamicModuleRouteProviderIntegrationTest, ResponseHeaderMutation) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-resp-header-set", "world"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("world", headerValue(response->headers(), "x-module-resp"));
}

// A direct response the module resolved the request to is honored, status and body included.
TEST_P(DynamicModuleRouteProviderIntegrationTest, DirectResponse) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-direct-response", "201"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("201", response->headers().getStatusValue());
  EXPECT_EQ("direct-response-body", response->body());
}

// A redirect the module resolved the request to is honored, status and Location included.
TEST_P(DynamicModuleRouteProviderIntegrationTest, Redirect) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-redirect", "302"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("302", response->headers().getStatusValue());
  EXPECT_EQ("http://redirect.example.com/new", headerValue(response->headers(), "location"));
}

// Every request state read accessor the context exposes is reachable: the module echoes the value
// it read into the x-echo-result response header, so a test can assert what the module read across
// the ABI boundary.
TEST_P(DynamicModuleRouteProviderIntegrationTest, ReaderAccessorsEchoed) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  const auto echoed = [this](absl::string_view accessor,
                             const std::vector<std::pair<std::string, std::string>>& extra = {}) {
    std::vector<std::pair<std::string, std::string>> headers{{"route-index", "0"},
                                                             {"x-echo", std::string(accessor)}};
    headers.insert(headers.end(), extra.begin(), extra.end());
    auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders(headers));
    EXPECT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
    return headerValue(response->headers(), "x-echo-result");
  };

  // The protocol string, the health check flag and the template count are fixed for this setup.
  EXPECT_EQ("HTTP/1.1", echoed("attribute-string"));
  // ResponseCodeDetails is unset while the route is selected, so the string accessor reports
  // absent, which exercises the unset branch of the get_attribute_string callback across the ABI
  // boundary.
  EXPECT_EQ("absent", echoed("attribute-string-unset"));
  EXPECT_EQ("false", echoed("attribute-bool"));
  EXPECT_EQ("2", echoed("template-count"));

  // The single and bulk header accessors read the value of the x-multi header the request carries.
  EXPECT_EQ("multi-value", echoed("header-value", {{"x-multi", "multi-value"}}));
  EXPECT_EQ("multi-value", echoed("header-bulk", {{"x-multi", "multi-value"}}));

  // UpstreamRequestAttemptCount always resolves through the integer accessor, defaulting to zero
  // before an upstream attempt is made, so the read reports a numeric string rather than absent.
  EXPECT_NE("absent", echoed("attribute-int"));

  // The header count and the random value are not fixed, so only their numeric shape is asserted.
  uint64_t parsed = 0;
  EXPECT_TRUE(absl::SimpleAtoi(echoed("header-count"), &parsed));
  EXPECT_GT(parsed, 0);
  EXPECT_TRUE(absl::SimpleAtoi(echoed("random-value"), &parsed));
}

// The indexed and total value accessors read a specific value and the count of a multi-value
// header, so a request that carries two x-multi values lets a test assert what the module read at
// each index across the ABI boundary.
TEST_P(DynamicModuleRouteProviderIntegrationTest, ReaderAccessorsMultiValue) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // requestHeaders() sets the first x-multi value with setCopy, so the second value is added
  // afterwards to give the header two values for the indexed accessors to read.
  const auto echoedMultiValue = [this](absl::string_view accessor) {
    auto headers =
        requestHeaders({{"route-index", "0"}, {"x-echo", std::string(accessor)}, {"x-multi", "a"}});
    headers.addCopy(Http::LowerCaseString("x-multi"), "b");
    auto response = codec_client_->makeHeaderOnlyRequest(headers);
    EXPECT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
    return headerValue(response->headers(), "x-echo-result");
  };

  EXPECT_EQ("a", echoedMultiValue("header-value"));
  EXPECT_EQ("b", echoedMultiValue("header-value-index"));
  EXPECT_EQ("2", echoedMultiValue("header-value-total"));
}

// The metadata match criteria of the selected override reach the subset load balancer, so the
// override decides which endpoint of the cluster serves the request.
TEST_P(DynamicModuleRouteProviderIntegrationTest, OverrideSelectsLoadBalancingSubset) {
  configureSubsetLoadBalancing();
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-override", "canary"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("canary", hostVersion(response->headers()));

  response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-override", "stable"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("stable", hostVersion(response->headers()));
}

// Without an override the criteria of the selected route template are in effect, and it carries
// none, so the NO_FALLBACK subset load balancer finds no host.
TEST_P(DynamicModuleRouteProviderIntegrationTest, NoOverrideFindsNoLoadBalancingSubset) {
  configureSubsetLoadBalancing();
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"route-index", "0"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
}

// The request mirroring policies of the selected override reach the router, so the mirror cluster
// receives a copy of the request.
TEST_P(DynamicModuleRouteProviderIntegrationTest, OverrideMirrorsRequest) {
  configureMirrorOverride();
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-override", "canary"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.mirror-cluster.upstream_rq_total", testing::Eq(1));
}

// The per-route configuration override the module selects is overlaid onto the selected route
// template, so a header_mutation filter that reads its per-route configuration through the route
// observes the override end to end. Without an override the template carries none, so the filter
// adds nothing.
TEST_P(DynamicModuleRouteProviderIntegrationTest, PerRouteConfigOverrideAppliesFilterConfig) {
  configurePerRouteConfigOverride();
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-per-route-config", "mutate"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("yes", headerValue(response->headers(), "x-per-route-applied"));

  response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"route-index", "0"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("", headerValue(response->headers(), "x-per-route-applied"));
}

// An unknown per-route configuration override name is a no-op, so the request routes normally and
// the filter adds nothing.
TEST_P(DynamicModuleRouteProviderIntegrationTest, UnknownPerRouteConfigOverrideIgnored) {
  configurePerRouteConfigOverride();
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-per-route-config", "does-not-exist"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("", headerValue(response->headers(), "x-per-route-applied"));
}

// Typed route metadata the module set is layered onto the selected template, so a request that
// carries a type URL a factory would accept still routes as usual.
TEST_P(DynamicModuleRouteProviderIntegrationTest, TypedRouteMetadata) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-route-typed-meta", "t/x"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.cluster_0.upstream_rq_total", testing::Eq(1));
}

// A route metadata buffer that does not parse leaves the metadata unchanged, so the request routes
// normally instead of failing.
TEST_P(DynamicModuleRouteProviderIntegrationTest, MalformedRouteMetadataIgnored) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders(
      {{"route-index", "0"}, {"x-route-meta-bad-struct", "1"}, {"x-route-meta-bad-typed", "1"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.cluster_0.upstream_rq_total", testing::Eq(1));
}

// A direct response code outside [200, 600) and a redirect code that is not a 3xx are both rejected
// by the setters, so no terminal response is recorded and the request routes normally instead.
TEST_P(DynamicModuleRouteProviderIntegrationTest, InvalidDirectResponseAndRedirectCodesIgnored) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-direct-response", "99"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.cluster_0.upstream_rq_total", testing::Eq(1));

  response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-redirect", "200"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.cluster_0.upstream_rq_total", testing::Eq(2));
}

// When the module reports a decision without selecting a route Envoy resolves no route, so the
// request gets a 404.
TEST_P(DynamicModuleRouteProviderIntegrationTest, DecisionWithoutSelectingRouteResolvesNoRoute) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeHeaderOnlyRequest(requestHeaders({{"x-decide-no-select", "true"}}));
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
}

// A scalar the module sets to the largest value the SDK type can express is clamped by the
// saturating conversion at the ABI boundary and the toMilliseconds clamp, so the request still
// routes rather than the timeout overflowing.
TEST_P(DynamicModuleRouteProviderIntegrationTest, SaturatingTimeoutValueAccepted) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-timeout-ms", "max"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.cluster_0.upstream_rq_total", testing::Eq(1));
}

// Tests that drive the upstream response so that the request the module shaped is observable at the
// upstream.
class DynamicModuleRouteProviderUpstreamIntegrationTest
    : public DynamicModuleRouteProviderIntegrationTest {
public:
  DynamicModuleRouteProviderUpstreamIntegrationTest() { autonomous_upstream_ = false; }

  // Fails the pending upstream request with a 503.
  void failUpstreamRequest() {
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
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

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleRouteProviderUpstreamIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// The request header mutations the module recorded reach the upstream: an appended header is
// present and a removed one is gone, even though the route template adds it.
TEST_P(DynamicModuleRouteProviderUpstreamIntegrationTest, RequestHeaderMutationSeenUpstream) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders(
      {{"route-index", "0"}, {"x-req-header-add", "hello"}, {"x-req-header-remove", "true"}}));
  waitForNextUpstreamRequest();
  const auto module_req = upstream_request_->headers().get(Http::LowerCaseString("x-module-req"));
  ASSERT_FALSE(module_req.empty());
  EXPECT_EQ("hello", module_req[0]->value().getStringView());
  EXPECT_TRUE(upstream_request_->headers().get(Http::LowerCaseString("x-remove-me")).empty());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// The path the module set replaces the request path the upstream receives.
TEST_P(DynamicModuleRouteProviderUpstreamIntegrationTest, PathRewrite) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-set-path", "/rewritten"}}));
  waitForNextUpstreamRequest();
  EXPECT_EQ("/rewritten", upstream_request_->headers().getPathValue());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// The retry policy of the selected override reaches the router, so a failed attempt is retried even
// though the selected route template configures no retries.
TEST_P(DynamicModuleRouteProviderUpstreamIntegrationTest, OverrideRetriesRequest) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-override", "canary"}}));
  failUpstreamRequest();
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.cluster_0.upstream_rq_retry", testing::Eq(1));
}

// The timeout the module selected reaches the router, so an upstream that never responds ends the
// request rather than the template timeout, which is far longer.
TEST_P(DynamicModuleRouteProviderUpstreamIntegrationTest, SelectedTimeoutIsUsed) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-timeout-ms", timeoutMs()}}));
  waitForNextUpstreamRequest();
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("504", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.cluster_0.upstream_rq_timeout", testing::Eq(1));
}

// The stream idle timeout the module selected reaches the connection manager, so a request the
// downstream never completes is timed out rather than the template idle timeout, which is far
// longer.
TEST_P(DynamicModuleRouteProviderUpstreamIntegrationTest, SelectedIdleTimeoutIsUsed) {
  config_helper_.disableDelayClose();
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The request is left without end stream and the upstream does not respond, so the stream is idle
  // until the timeout fires.
  auto encoder_decoder = codec_client_->startRequest(
      requestHeaders({{"route-index", "0"}, {"x-idle-timeout-ms", timeoutMs()}}));
  auto response = std::move(encoder_decoder.second);
  waitForUpstreamRequestHeaders();

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_EQ("408", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_idle_timeout")->value());
}

// The maximum stream duration the module selected reaches the connection manager, so a request the
// upstream never answers is ended once the duration elapses. The whole downstream request was
// received first, so the reply is a gateway timeout.
TEST_P(DynamicModuleRouteProviderUpstreamIntegrationTest, SelectedMaxStreamDurationIsUsed) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"route-index", "0"}, {"x-max-stream-duration-ms", timeoutMs()}}));
  waitForNextUpstreamRequest();
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("504", response->headers().getStatusValue());
  test_server_->waitForCounter("http.config_test.downstream_rq_max_duration_reached",
                               testing::Eq(1));
}

// The request body buffer limit the module selected reaches the router, so a body larger than the
// limit makes the router give up on buffering for retries.
TEST_P(DynamicModuleRouteProviderUpstreamIntegrationTest, SelectedRequestBodyBufferLimitIsUsed) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The canary override enables retries, which is what makes the router buffer the request body.
  auto encoder_decoder = codec_client_->startRequest(
      requestHeaders({{"route-index", "0"}, {"x-override", "canary"}, {"x-buffer-limit", "16"}}));
  auto& encoder = encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  waitForUpstreamRequestHeaders();

  Buffer::OwnedImpl body(std::string(64, 'a'));
  encoder.encodeData(body, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.retry_or_shadow_abandoned")->value());
}

} // namespace
} // namespace DynamicModules
} // namespace RouteProviders
} // namespace Router
} // namespace Extensions
} // namespace Envoy

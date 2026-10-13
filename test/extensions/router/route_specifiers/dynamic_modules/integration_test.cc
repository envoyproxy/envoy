#include "envoy/extensions/router/route_specifiers/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace RouteSpecifiers {
namespace DynamicModules {
namespace {

using DynamicModuleRouteSpecifierProto =
    envoy::extensions::router::route_specifiers::dynamic_modules::v3::DynamicModuleRouteSpecifier;

class DynamicModuleRouteSpecifierIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleRouteSpecifierIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    autonomous_upstream_ = true;
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
  }

  // Configures the virtual host to run the route specifier. The extra configuration is appended to
  // the specifier configuration so that a test can add what it needs. A second specifier is
  // appended to the chain only when a test asks for it, since it decides the request too.
  void setupTest(const std::string& extra_specifier_yaml = "", bool chain_second_specifier = false,
                 bool at_route_config_level = false) {
    config_helper_.addConfigModifier(
        [extra_specifier_yaml, chain_second_specifier, at_route_config_level](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          const std::string specifier_yaml = R"EOF(
dynamic_module_config:
  name: route_specifier_integration_test
  do_not_close: true
specifier_name: test_route_specifier
stat_prefix: test
failure_policy: PASS_THROUGH
route_templates:
- template_id: canary
  route:
    match: {prefix: "/"}
    route:
      cluster: canary
      timeout: 7s
- template_id: direct
  route:
    match: {prefix: "/"}
    direct_response:
      status: 204
- template_id: redirect
  route:
    match: {prefix: "/"}
    redirect:
      host_redirect: redirected.example.com
- template_id: unmatched
  route:
    match: {prefix: "/never"}
    route: {cluster: canary}
- template_id: hedged
  route:
    match: {prefix: "/"}
    route:
      cluster: canary
      hedge_policy: {hedge_on_per_try_timeout: true}
- template_id: redirect_found
  route:
    match: {prefix: "/"}
    redirect:
      host_redirect: redirected.example.com
      response_code: FOUND
- template_id: direct_body
  route:
    name: template_direct
    match: {prefix: "/"}
    direct_response:
      status: 200
      body: {inline_string: "template"}
- template_id: full
  route:
    name: full_route
    match: {prefix: "/"}
    request_body_buffer_limit: 4096
    metadata:
      filter_metadata:
        envoy.test.route: {key: value, number: 42}
    route:
      cluster: canary
      timeout: 7s
      idle_timeout: 8s
      max_stream_duration: {max_stream_duration: 9s}
      priority: HIGH
      cluster_not_found_response_code: NOT_FOUND
      metadata_match:
        filter_metadata:
          envoy.lb: {version: canary}
      hash_policy:
      - header: {header_name: x-hash}
      request_mirror_policies:
      - cluster: canary
      rate_limits:
      - actions: [{destination_cluster: {}}]
- template_id: traced
  route:
    match: {prefix: "/"}
    tracing:
      client_sampling: {numerator: 10}
    route:
      cluster: canary
- template_id: filter_off
  route:
    match: {prefix: "/"}
    typed_per_filter_config:
      envoy.test.disabled:
        "@type": type.googleapis.com/envoy.config.route.v3.FilterConfig
        is_optional: true
        disabled: true
    route:
      cluster: canary
route_overrides:
- override_id: traced
  tracing:
    client_sampling: {numerator: 10}
- override_id: tagged
  metadata:
    filter_metadata:
      envoy.test.override: {group: canary}
)EOF" + extra_specifier_yaml;
          DynamicModuleRouteSpecifierProto specifier_config;
          TestUtility::loadFromYaml(specifier_yaml, specifier_config);

          auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
          // A specifier at the route configuration level also runs when matching resolves no route,
          // which is how a null input route is exercised. Templates need a route builder that the
          // route configuration level does not provide, so they are dropped for that placement.
          if (at_route_config_level) {
            specifier_config.clear_route_templates();
          }
          auto* specifier = at_route_config_level
                                ? hcm.mutable_route_config()->add_route_specifiers()
                                : virtual_host->add_route_specifiers();
          specifier->set_name("envoy.router.route_specifiers.dynamic_modules");
          std::ignore = specifier->mutable_typed_config()->PackFrom(specifier_config);

          if (!chain_second_specifier) {
            return;
          }
          DynamicModuleRouteSpecifierProto second_config = specifier_config;
          second_config.set_stat_prefix("second");
          auto* second = virtual_host->add_route_specifiers();
          second->set_name("envoy.router.route_specifiers.dynamic_modules");
          std::ignore = second->mutable_typed_config()->PackFrom(second_config);
        });
    // Extra routes so a test can match a redirect or a direct response by path.
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
          // Match only the authority the requests use, so a request to another authority resolves
          // no route, which is how OverrideWithoutRoute exercises a decision without a route.
          virtual_host->clear_domains();
          virtual_host->add_domains("example.com");
          auto* redirect = virtual_host->add_routes();
          TestUtility::loadFromYaml(R"EOF(
match: {prefix: "/redirect"}
redirect: {host_redirect: original.example.com}
)EOF",
                                    *redirect);
          auto* direct = virtual_host->add_routes();
          TestUtility::loadFromYaml(R"EOF(
name: matched_direct
match: {prefix: "/direct"}
direct_response:
  status: 200
  body: {inline_string: "route"}
)EOF",
                                    *direct);
          // Envoy matches routes in order, so the catch all route moves to the end.
          auto* routes = virtual_host->mutable_routes();
          routes->SwapElements(0, 1);
          routes->SwapElements(1, 2);
        });
    // The template clusters must exist, since a template names one that no route does.
    addCanaryCluster();
    setUpstreamCount(2);
    HttpIntegrationTest::initialize();
  }

  // Adds a second static cluster named canary, which the templates and the module may route to.
  void addCanaryCluster() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->MergeFrom(bootstrap.static_resources().clusters(0));
      cluster->set_name("canary");
    });
  }

  // Configures the virtual host to run the shadow example module, which routes to intended_cluster
  // by shadowing the decision in a dry run or by applying it in a wet run.
  void setupShadowExample(bool dry_run, absl::string_view intended_cluster) {
    const std::string mode = dry_run ? "dry-run" : "wet-run";
    config_helper_.addConfigModifier(
        [mode, intended_cluster = std::string(intended_cluster)](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          const std::string specifier_yaml = absl::StrCat(R"EOF(
dynamic_module_config:
  name: route_specifier_shadow
  do_not_close: true
specifier_name: shadow_example
stat_prefix: test
failure_policy: PASS_THROUGH
specifier_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: ")EOF",
                                                          mode, " ", intended_cluster, "\"\n");
          DynamicModuleRouteSpecifierProto specifier_config;
          TestUtility::loadFromYaml(specifier_yaml, specifier_config);
          auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
          auto* specifier = virtual_host->add_route_specifiers();
          specifier->set_name("envoy.router.route_specifiers.dynamic_modules");
          std::ignore = specifier->mutable_typed_config()->PackFrom(specifier_config);
        });
    addCanaryCluster();
    setUpstreamCount(2);
    HttpIntegrationTest::initialize();
  }

  // Configures the catch all route with a route level specifier and adds a more specific route
  // after it, so a test can see a StopIterationAndSkipRoute status drop the catch all route and
  // let matching carry on to the specific route.
  void setupSkipRouteTest(const std::string& failure_policy = "PASS_THROUGH") {
    config_helper_.addConfigModifier(
        [failure_policy](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          const std::string specifier_yaml = absl::StrCat(R"EOF(
dynamic_module_config:
  name: route_specifier_integration_test
  do_not_close: true
specifier_name: test_route_specifier
stat_prefix: test
failure_policy: )EOF",
                                                          failure_policy, "\n");
          DynamicModuleRouteSpecifierProto specifier_config;
          TestUtility::loadFromYaml(specifier_yaml, specifier_config);

          auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
          virtual_host->clear_domains();
          virtual_host->add_domains("example.com");

          auto* catch_all = virtual_host->mutable_routes(0);
          catch_all->set_name("catch_all");
          auto* specifier = catch_all->add_route_specifiers();
          specifier->set_name("envoy.router.route_specifiers.dynamic_modules");
          std::ignore = specifier->mutable_typed_config()->PackFrom(specifier_config);

          auto* specific = virtual_host->add_routes();
          TestUtility::loadFromYaml(R"EOF(
name: specific
match: {prefix: "/specific"}
route: {cluster: canary}
)EOF",
                                    *specific);
        });
    addCanaryCluster();
    setUpstreamCount(2);
    HttpIntegrationTest::initialize();
  }

  Http::TestRequestHeaderMapImpl
  requestHeaders(const std::vector<std::pair<std::string, std::string>>& extra_headers = {}) {
    Http::TestRequestHeaderMapImpl headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};
    for (const auto& [key, value] : extra_headers) {
      // A pseudo header replaces the default, while a normal header is appended so that a test can
      // send the same key twice.
      if (absl::StartsWith(key, ":")) {
        headers.setCopy(Http::LowerCaseString(key), value);
      } else {
        headers.addCopy(Http::LowerCaseString(key), value);
      }
    }
    return headers;
  }

  std::string header(const Http::ResponseHeaderMap& headers, absl::string_view key) {
    const auto values = headers.get(Http::LowerCaseString(key));
    return values.empty() ? "" : std::string(values[0]->value().getStringView());
  }

  // The value of a counter, or zero when it was never created. Cluster response code counters are
  // created on the first matching response, so a cluster that served none has no counter.
  uint64_t counterValue(const std::string& name) {
    const auto counter = test_server_->counter(name);
    return counter == nullptr ? 0 : counter->value();
  }

  IntegrationStreamDecoderPtr
  sendRequest(const std::vector<std::pair<std::string, std::string>>& extra_headers) {
    auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders(extra_headers));
    EXPECT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    return response;
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleRouteSpecifierIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Without a decision from the module the default Unspecified decision is in effect, and with
// nothing recorded it leaves the route that matching resolved unchanged.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, DefaultDecisionPassesThrough) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.decision_has_override",
                               testing::Ge(1));
}

// The PassThrough decision keeps the route that matching resolved and ignores whatever the
// module recorded.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, PassThrough) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "pass-through"}, {"x-cluster", "canary"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.decision_pass_through",
                               testing::Ge(1));
  test_server_->waitForCounter("cluster.cluster_0.upstream_rq_200", testing::Ge(1));
  EXPECT_EQ(0, counterValue("cluster.canary.upstream_rq_200"));
}

// The module defines metrics at configuration time and records them on each decision.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, RecordsDecisionMetrics) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-template", "canary"}});
  EXPECT_EQ("200", response->headers().getStatusValue());

  test_server_->waitForCounter("dynamicmodulescustom.decisions_total", testing::Ge(1));
  test_server_->waitForCounter("dynamicmodulescustom.decisions_by_template.template.canary",
                               testing::Ge(1));
}

// A selected template is evaluated against the request like a configured route, so its action
// decides where the request goes.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, SelectsTemplate) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-template", "canary"}, {"x-echo", "route-cluster-name"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("canary", header(response->headers(), "x-echo-result"));
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.decision_has_template",
                               testing::Ge(1));
}

// A module registers a route template from a serialized Route during configuration and then selects
// it like a declared template, which is how a module produces routes from its own configuration.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, SelectsRegisteredTemplate) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-template", "registered"}, {"x-echo", "route-cluster-name"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("canary", header(response->headers(), "x-echo-result"));
  test_server_->waitForCounter("cluster.canary.upstream_rq_200", testing::Ge(1));
}

// A template whose action answers the request directly is served without an upstream.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, SelectsDirectResponseTemplate) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-template", "direct"}});
  EXPECT_EQ("204", response->headers().getStatusValue());
}

// A template whose action redirects answers with the location it builds.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, SelectsRedirectTemplate) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-template", "redirect"}});
  EXPECT_EQ("301", response->headers().getStatusValue());
  EXPECT_EQ("http://redirected.example.com/", header(response->headers(), "location"));
}

// A template whose match does not hold for the request cannot be used, so the failure policy
// applies. PASS_THROUGH keeps the route that matching resolved.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, TemplateMatchFailedPassesThrough) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-template", "unmatched"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter(
      "dynamicmodulescustom.route_specifier.test.failure_template_match_failed", testing::Ge(1));
}

// A selection that names a template that is not declared cannot be honored, so the failure
// policy applies rather than silently falling back to the resolved route.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, TemplateNotSelectedPassesThrough) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-template", "unknown"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter(
      "dynamicmodulescustom.route_specifier.test.failure_template_not_selected", testing::Ge(1));
}

// A module that reports an error is handled by the failure policy rather than by the decision.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ModuleErrorPassesThrough) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "error"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.failure_module_error",
                               testing::Ge(1));
}

// NO_ROUTE makes a failure drop the route rather than fall back to the route table.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ModuleErrorFailsClosed) {
  setupTest(R"EOF(
failure_policy: NO_ROUTE
)EOF");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "error"}});
  EXPECT_EQ("404", response->headers().getStatusValue());
}

// A module can drop the route of a request outright.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, NoRoute) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "no-route"}});
  EXPECT_EQ("404", response->headers().getStatusValue());
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.decision_no_route",
                               testing::Ge(1));
}

// An override replaces the cluster of the route that matching resolved, so the request is served
// by the cluster the module named.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, OverridesCluster) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest(
      {{"x-decision", "unspecified"}, {"x-cluster", "canary"}, {"x-echo", "route-cluster-name"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  // The echo reads the route matching resolved, which is the one the override replaces.
  EXPECT_EQ("cluster_0", header(response->headers(), "x-echo-result"));
  test_server_->waitForCounter("cluster.canary.upstream_rq_200", testing::Ge(1));
  EXPECT_EQ(0, counterValue("cluster.cluster_0.upstream_rq_200"));
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.decision_has_override",
                               testing::Ge(1));
}

// Route entry properties cannot be applied to a route that answers the request directly.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, OverrideOnDirectResponse) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-template", "direct"}, {"x-cluster", "canary"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter(
      "dynamicmodulescustom.route_specifier.test.failure_override_on_non_route_entry",
      testing::Ge(1));
}

// The route timeout a module records reaches the upstream request.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, OverridesTimeout) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "unspecified"}, {"x-timeout-ms", "3000"}});
  EXPECT_EQ("200", response->headers().getStatusValue());

  const auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_NE(nullptr, upstream_headers);
  EXPECT_EQ("3000", upstream_headers->get_("x-envoy-expected-rq-timeout-ms"));
}

// The status code a module records for a missing cluster is the one the request fails with.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, OverridesClusterNotFoundResponseCode) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest(
      {{"x-decision", "unspecified"}, {"x-cluster", "missing"}, {"x-not-found-code", "502"}});
  EXPECT_EQ("502", response->headers().getStatusValue());
}

// An override of a request that matching resolved no route for cannot be honored.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, OverrideWithoutRoute) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "nomatch.example.com"},
                                     {"x-decision", "unspecified"},
                                     {"x-cluster", "canary"}});
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
}

// The header mutations a module records are applied to the request sent upstream and to the
// response.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, AppliesHeaderMutations) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "unspecified"},
                               {"x-add-response-header", "x-added=value"},
                               {"x-add-request-header", "x-upstream=value"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("value", header(response->headers(), "x-added"));

  const auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_NE(nullptr, upstream_headers);
  EXPECT_EQ("value", upstream_headers->get_("x-upstream"));
}

// A recorded path and authority replace those of the request sent upstream.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, RewritesPathAndHost) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "unspecified"},
                               {"x-set-path", "/rewritten"},
                               {"x-set-host", "upstream.local"}});
  EXPECT_EQ("200", response->headers().getStatusValue());

  const auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_NE(nullptr, upstream_headers);
  EXPECT_EQ("/rewritten", upstream_headers->getPathValue());
  EXPECT_EQ("upstream.local", upstream_headers->getHostValue());
}

// A recorded route name is the one the %ROUTE_NAME% access log command operator reports, so a
// module built route carries an identity of its own end to end.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, SetRouteNameSurfacesInAccessLog) {
  useAccessLog("%ROUTE_NAME%");
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "unspecified"},
                               {"x-cluster", "canary"},
                               {"x-set-route-name", "module_route"}});
  EXPECT_EQ("200", response->headers().getStatusValue());

  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_NE(std::string::npos, log.find("module_route"));
}

// A recorded prefix rewrite replaces the matched prefix of the path sent upstream, keeping the
// query string.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, PrefixRewriteRewritesUpstreamPath) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{":path", "/api/foo?q=1"},
                               {"x-decision", "unspecified"},
                               {"x-prefix-rewrite", "/api=/internal"}});
  EXPECT_EQ("200", response->headers().getStatusValue());

  const auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_NE(nullptr, upstream_headers);
  EXPECT_EQ("/internal/foo?q=1", upstream_headers->getPathValue());
}

// A regex rewrite carried by a selected route override rewrites the path sent upstream when the
// decision resolves, keeping the query string the same way a prefix rewrite does.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, RouteOverrideRegexRewritesUpstreamPath) {
  setupTest(R"EOF(
route_overrides:
- override_id: regex
  regex_rewrite:
    pattern: {regex: "^/api/(.*)$"}
    substitution: '/internal/\1'
)EOF");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest(
      {{":path", "/api/foo?q=1"}, {"x-decision", "unspecified"}, {"x-override", "regex"}});
  EXPECT_EQ("200", response->headers().getStatusValue());

  const auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_NE(nullptr, upstream_headers);
  EXPECT_EQ("/internal/foo?q=1", upstream_headers->getPathValue());
}

// A route level override such as tracing is accepted on a direct response template, because it is
// not a route entry property and so does not trip the route entry override check.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, RouteLevelOverrideOnDirectResponse) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-template", "direct_body"}, {"x-override", "traced"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("template", response->body());
  EXPECT_EQ(0, counterValue("dynamicmodulescustom.route_specifier.test."
                            "failure_override_on_non_route_entry"));
}

// A module reads the request, the stream info and the route through the context, which is how it
// reaches a decision.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ReadsRequestAndRouteState) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  struct TestCase {
    std::string accessor;
    std::string expected;
  };
  const TestCase test_cases[] = {
      {"header-count", ""},
      {"header-value", "first"},
      {"header-value-index", "second"},
      {"header-value-total", "2"},
      {"header-bulk", "first"},
      {"attribute-string", "HTTP/1.1"},
      // Request size is the bytes received so far, which is not a fixed value, so only its presence
      // is checked.
      {"attribute-int", ""},
      {"attribute-bool", "false"},
      {"route-kind", "RouteEntry"},
      {"route-bulk", "RouteEntry//integration/cluster_0"},
      {"virtual-host-name", "integration"},
      {"route-cluster-name", "cluster_0"},
      // The resolved route has no envoy.test.route metadata, so these reads are absent.
      {"route-metadata", "absent"},
      {"route-metadata-number", "absent"},
      {"cluster-host-count", "1/1/0"},
      {"random-value", ""},
      // No filter sets these, so the reads return the absent marker.
      {"dynamic-metadata", "absent"},
      {"dynamic-metadata-number", "absent"},
      {"dynamic-metadata-bool", "absent"},
      {"filter-state", "absent"},
      // No template was selected, so the selected template id is absent.
      {"selected-template", "absent"},
      {"template-ids",
       "canary,direct,redirect,unmatched,hedged,redirect_found,direct_body,full,traced,filter_off,"
       "registered,registered_direct"},
  };
  for (const auto& test_case : test_cases) {
    auto response = sendRequest({{"x-decision", "unspecified"},
                                 {"x-echo", test_case.accessor},
                                 {"x-query-cluster", "cluster_0"},
                                 {"x-multi", "first"},
                                 {"x-multi", "second"}});
    EXPECT_EQ("200", response->headers().getStatusValue());
    if (!test_case.expected.empty()) {
      EXPECT_EQ(test_case.expected, header(response->headers(), "x-echo-result"))
          << test_case.accessor;
    } else {
      // These accessors return a value that is not fixed, but it must still be a real read rather
      // than the absent marker.
      const std::string result = header(response->headers(), "x-echo-result");
      EXPECT_FALSE(result.empty()) << test_case.accessor;
      EXPECT_NE("absent", result) << test_case.accessor;
    }
  }
}

// The bulk snapshot returns every free-to-read property of the route in one call, and after a
// template is selected it reflects that template, so a module reads the route it produces without a
// call per property.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ReadsExpandedRouteSnapshot) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  struct TestCase {
    std::string accessor;
    std::string expected;
  };
  const TestCase test_cases[] = {
      {"route-bulk", "RouteEntry/full_route/integration/canary"},
      {"route-name", "full_route"},
      {"route-cluster-name", "canary"},
      {"route-timeout", "7000"},
      {"route-idle-timeout", "8000"},
      {"route-max-stream-duration", "9000"},
      {"route-priority", "high"},
      {"route-buffer-limit", "4096"},
      {"route-not-found-code", "404"},
      {"route-flags", "meta=true,mm=true,hash=true,rl=true,mirror=1"},
      {"route-metadata", "value"},
      {"route-metadata-number", "42"},
      {"selected-template", "full"},
  };
  for (const auto& test_case : test_cases) {
    auto response = sendRequest({{"x-template", "full"}, {"x-echo", test_case.accessor}});
    EXPECT_EQ("200", response->headers().getStatusValue()) << test_case.accessor;
    EXPECT_EQ(test_case.expected, header(response->headers(), "x-echo-result"))
        << test_case.accessor;
  }
}

// The redirect location of a direct response route is read through the getter without changing the
// routing of the request.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ReadsInputRouteRedirectLocation) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{":path", "/redirect"}, {"x-echo", "route-redirect-location"}}));
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("301", response->headers().getStatusValue());
  EXPECT_EQ("http://original.example.com/redirect", header(response->headers(), "location"));
}

// When no route resolves, the route getters report the route as absent rather than reading a null
// route, and the module still reaches a decision.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ReadsRouteStateWithoutRoute) {
  // The route configuration level specifier runs even for an authority that matches no virtual
  // host.
  setupTest("", false, true);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  for (const std::string accessor :
       {"route-bulk", "route-kind", "route-name", "virtual-host-name", "route-cluster-name",
        "route-timeout", "route-response-code", "route-redirect-location", "route-metadata",
        "route-metadata-number"}) {
    auto response =
        codec_client_->makeHeaderOnlyRequest(requestHeaders({{":authority", "nomatch.example.com"},
                                                             {"x-decision", "unspecified"},
                                                             {"x-echo", accessor}}));
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("404", response->headers().getStatusValue()) << accessor;
  }
  test_server_->waitForCounter(
      "dynamicmodulescustom.route_specifier.test.failure_override_without_route", testing::Ge(1));
}

// The route getters read a direct response input route, including its response code.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ReadsDirectResponseInputRoute) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  for (const std::string accessor : {"route-bulk", "route-kind", "route-response-code"}) {
    auto response = codec_client_->makeHeaderOnlyRequest(
        requestHeaders({{":path", "/direct"}, {"x-echo", accessor}}));
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue()) << accessor;
  }
}

// The route metadata setters record numeric, boolean and typed metadata on the produced route, and
// an `unparsable` typed value is rejected.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, SetsRouteMetadata) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "unspecified"},
                               {"x-route-meta-number", "42"},
                               {"x-route-meta-bool", "true"},
                               {"x-route-typed-meta", "accept"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  response = sendRequest({{"x-decision", "unspecified"}, {"x-route-typed-meta", "unparsable"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// The module records a gauge and a histogram, each in scalar and labeled form.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, RecordsGaugeAndHistogram) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-metric-op", "record"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  // set(10) then increase(5) then decrease(3) leaves 12.
  test_server_->waitForGauge("dynamicmodulescustom.in_flight", testing::Eq(12));
  test_server_->waitForGauge("dynamicmodulescustom.in_flight_by_template.template.none",
                             testing::Eq(12));
  test_server_->waitUntilHistogramHasSamples("dynamicmodulescustom.decision_micros");
  test_server_->waitUntilHistogramHasSamples(
      "dynamicmodulescustom.decision_micros_by_template.template.none");
}

// Metric operations with unknown ids, the wrong label count, or after configuration are rejected
// without failing the request.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, MetricErrorsAreRejected) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-metric-op", "errors"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  // Defining a metric after configuration is frozen is rejected, so no late metric is created.
  EXPECT_EQ(nullptr, test_server_->counter("dynamicmodulescustom.late"));
  EXPECT_EQ(nullptr, test_server_->gauge("dynamicmodulescustom.late"));
  EXPECT_EQ(nullptr, test_server_->histogram("dynamicmodulescustom.late"));
}

// The cluster host count getter reports absent for an unknown cluster or an out-of-range priority.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ClusterHostCountMisses) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "unspecified"},
                               {"x-echo", "cluster-host-count"},
                               {"x-query-cluster", "missing"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("absent", header(response->headers(), "x-echo-result"));
  response = sendRequest({{"x-decision", "unspecified"},
                          {"x-echo", "cluster-host-count"},
                          {"x-query-cluster", "cluster_0"},
                          {"x-query-priority", "99"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("absent", header(response->headers(), "x-echo-result"));
}

// Invalid setter inputs are each rejected, and the request is still served by the resolved route.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, RejectsInvalidSetterInputs) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The empty cluster and filter names and the unknown override are rejected, which the specifier
  // records at debug level.
  IntegrationStreamDecoderPtr response;
  EXPECT_LOG_CONTAINS_ALL_OF(
      (Envoy::ExpectedLogMessages{
          {"debug", "dynamic module route specifier rejected cluster ''"},
          {"debug", "dynamic module route specifier rejected filter ''"},
          {"debug", "dynamic module route specifier selected unknown route override 'unknown'"}}),
      {
        response = sendRequest({{"x-decision", "unspecified"},
                                {"x-cluster", ""},
                                {"x-filter-disabled", ""},
                                {"x-set-path", "no-leading-slash"},
                                {"x-set-host", "invalid host"},
                                {"x-add-request-header", ":method=CONNECT"},
                                {"x-remove-request-header", ":path"},
                                {"x-not-found-code", "max"},
                                {"x-override", "unknown"}});
      });
  EXPECT_EQ("200", response->headers().getStatusValue());
  // Every setter input was rejected, so the request reaches the upstream with its path and host
  // unchanged.
  const auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_NE(nullptr, upstream_headers);
  EXPECT_EQ("/", upstream_headers->getPathValue());
  EXPECT_EQ("example.com", upstream_headers->getHostValue());
}

// A module validates its configuration against the templates Envoy declares, which reads the
// template kind at configuration time.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ValidatesModuleConfigTemplate) {
  setupTest(R"EOF(
specifier_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: canary
)EOF");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({});
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// A request outside the runtime fraction is passed through untouched.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, RuntimeFractionSkips) {
  setupTest(R"EOF(
runtime_fraction:
  default_value:
    numerator: 0
    denominator: HUNDRED
)EOF");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "no-route"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.runtime_skipped",
                               testing::Ge(1));
}

// A filter that clears the route cache makes Envoy resolve the route again, which re-enters the
// module so that a decision taken from state an earlier filter produced takes effect.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ReEntersModuleOnRouteCacheClear) {
  config_helper_.prependFilter(R"EOF(
name: clear-route-cache
typed_config:
  "@type": type.googleapis.com/test.integration.filters.ClearRouteCacheFilterConfig
)EOF");
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "unspecified"}, {"x-cluster", "canary"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  // The module ran once while the route was first resolved and again for the refresh.
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.decision_has_override",
                               testing::Ge(2));
  test_server_->waitForCounter("cluster.canary.upstream_rq_200", testing::Ge(1));
}

// When the route is recomputed the module reads the route the connection manager last installed.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ReadsPreviousRouteOnRecompute) {
  config_helper_.prependFilter(R"EOF(
name: clear-route-cache
typed_config:
  "@type": type.googleapis.com/test.integration.filters.ClearRouteCacheFilterConfig
)EOF");
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The first resolution overrides to canary and installs that route. The cache clear recomputes,
  // and the echo of the second resolution reports the cluster of the previous route.
  auto response = sendRequest({{"x-decision", "unspecified"},
                               {"x-cluster", "canary"},
                               {"x-echo", "previous-route-cluster"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("canary", header(response->headers(), "x-echo-result"));
}

// When the route is recomputed the module reads the string metadata of the route the connection
// manager last installed, which is how a module keeps a stream on a marker it wrote into a route it
// built earlier.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ReadsPreviousRouteMetadataOnRecompute) {
  config_helper_.prependFilter(R"EOF(
name: clear-route-cache
typed_config:
  "@type": type.googleapis.com/test.integration.filters.ClearRouteCacheFilterConfig
)EOF");
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The first resolution selects the full template, whose route carries envoy.test.route metadata,
  // and installs it. The cache clear recomputes, and the echo of the second resolution reports the
  // string value read from the previous route.
  auto response = sendRequest({{"x-template", "full"}, {"x-echo", "previous-route-metadata"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("value", header(response->headers(), "x-echo-result"));

  // A previous route that carries no envoy.test.route metadata has no string value under the key,
  // so the read reports the absent marker.
  response = sendRequest({{"x-decision", "unspecified"},
                          {"x-cluster", "canary"},
                          {"x-echo", "previous-route-metadata"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("absent", header(response->headers(), "x-echo-result"));
}

// ReusePrevious without a previous route is rejected and the failure policy passes it through.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ReusePreviousWithoutPreviousRoute) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "reuse-previous"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.reuse_previous_rejected",
                               testing::Ge(1));
}

// After a route is installed, ReusePrevious keeps it on a recompute without building a new route.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ReusePreviousKeepsInstalledRoute) {
  config_helper_.prependFilter(R"EOF(
name: clear-route-cache
typed_config:
  "@type": type.googleapis.com/test.integration.filters.ClearRouteCacheFilterConfig
)EOF");
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The first resolution has no previous route and passes through to the static route, which is
  // installed. The cache clear recomputes, and the second resolution reuses that route.
  auto response = sendRequest({{"x-decision", "reuse-previous"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.decision_reuse_previous",
                               testing::Ge(2));
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.reuse_previous_rejected",
                               testing::Ge(1));
}

// A route built with user data is wrapped even with no other override, and the route destroy hook
// fires when it is destroyed at stream end.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, RouteUserDataFiresDestroyHook) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-template", "canary"}, {"x-user-data", "42"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.route_destroy",
                               testing::Ge(1));
}

// A decision that stops the chain skips the specifiers configured after it, which the second
// specifier of the chain reports through its own statistics.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, StopsChain) {
  setupTest("", /*chain_second_specifier=*/true);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "unspecified"}, {"x-cluster", "canary"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.second.decision_has_override",
                               testing::Ge(1));

  response = sendRequest(
      {{"x-decision", "unspecified"}, {"x-cluster", "canary"}, {"x-stop-chain", "true"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  // The second specifier did not run again, so its counter did not move.
  EXPECT_EQ(
      1, test_server_->counter("dynamicmodulescustom.route_specifier.second.decision_has_override")
             ->value());
}

// The returned status alone decides whether the specifiers after this one run, for a template
// decision exactly like for any other.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ContinuesChain) {
  setupTest("", /*chain_second_specifier=*/true);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Stopping the chain keeps the second specifier from running. The echo confirms the first
  // specifier really selected the template rather than passing through.
  auto baseline = sendRequest(
      {{"x-template", "canary"}, {"x-stop-chain", "true"}, {"x-echo", "selected-template"}});
  EXPECT_EQ("canary", header(baseline->headers(), "x-echo-result"));
  EXPECT_EQ(0, counterValue("dynamicmodulescustom.route_specifier.second.decision_has_template"));

  // Continuing the chain is the only difference, so the second specifier running is attributable
  // to it.
  auto response = sendRequest({{"x-template", "canary"}, {"x-continue-chain", "true"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.second.decision_has_template",
                               testing::Ge(1));
}

// Each append action combines an added header with one of the same name in its own way.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, AppliesEachHeaderAppendAction) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  struct TestCase {
    std::string action;
    std::string expected;
  };
  const TestCase test_cases[] = {
      {"append", "original,added"},
      {"add-if-absent", "original"},
      {"overwrite", "added"},
      {"overwrite-if-exists", "added"},
  };
  for (const auto& test_case : test_cases) {
    auto response = sendRequest({{"x-decision", "unspecified"},
                                 {"x-append-action", test_case.action},
                                 {"x-existing", "original"},
                                 {"x-add-request-header", "x-existing=added"}});
    EXPECT_EQ("200", response->headers().getStatusValue());
    const auto upstream_headers =
        reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
    ASSERT_NE(nullptr, upstream_headers);
    EXPECT_EQ(test_case.expected, upstream_headers->get_("x-existing")) << test_case.action;
  }

  // Without a header of the same name every action adds the header.
  auto response = sendRequest({{"x-decision", "unspecified"},
                               {"x-append-action", "overwrite-if-exists"},
                               {"x-add-request-header", "x-absent=added"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  const auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_NE(nullptr, upstream_headers);
  EXPECT_EQ("", upstream_headers->get_("x-absent"));

  // Without a header of the same name add-if-absent adds it, which is the branch that overwrites
  // nothing.
  response = sendRequest({{"x-decision", "unspecified"},
                          {"x-append-action", "add-if-absent"},
                          {"x-add-request-header", "x-absent-add=added"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  const auto add_if_absent_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_NE(nullptr, add_if_absent_headers);
  EXPECT_EQ("added", add_if_absent_headers->get_("x-absent-add"));
}

// The header removals a module records are applied to the request sent upstream and to the
// response.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, RemovesHeaders) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-decision", "unspecified"},
                               {"x-drop-me", "value"},
                               {"x-remove-request-header", "x-drop-me"},
                               {"x-add-response-header", "x-drop-response=value"},
                               {"x-remove-response-header", "x-drop-response"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("", header(response->headers(), "x-drop-response"));

  const auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_NE(nullptr, upstream_headers);
  EXPECT_EQ("", upstream_headers->get_("x-drop-me"));
}

// The example module shadows its routing in a dry run, counting a match when the cluster it would
// route to is the cluster the matched route already names, and leaving routing unchanged.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ShadowExampleDryRunCountsMatch) {
  setupShadowExample(/*dry_run=*/true, "cluster_0");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("dynamicmodulescustom.shadow_match", testing::Ge(1));
  // Routing is unchanged, so the request reaches the cluster the matched route names.
  test_server_->waitForCounter("cluster.cluster_0.upstream_rq_200", testing::Ge(1));
  EXPECT_EQ(0, counterValue("dynamicmodulescustom.shadow_mismatch"));
}

// A dry run counts a mismatch when the cluster the module would route to differs from the matched
// route, and still leaves routing unchanged.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ShadowExampleDryRunCountsMismatch) {
  setupShadowExample(/*dry_run=*/true, "canary");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("dynamicmodulescustom.shadow_mismatch", testing::Ge(1));
  // The dry run does not apply the decision, so the request still reaches the matched cluster.
  test_server_->waitForCounter("cluster.cluster_0.upstream_rq_200", testing::Ge(1));
  EXPECT_EQ(0, counterValue("cluster.canary.upstream_rq_200"));
}

// A wet run applies the decision, so the request reaches the cluster the module chose rather than
// the one the matched route names.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ShadowExampleWetRunApplies) {
  setupShadowExample(/*dry_run=*/false, "canary");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.canary.upstream_rq_200", testing::Ge(1));
  EXPECT_EQ(0, counterValue("cluster.cluster_0.upstream_rq_200"));
  // A wet run applies the decision instead of counting, so the dry run counters are never created.
  EXPECT_EQ(0, counterValue("dynamicmodulescustom.shadow_match"));
  EXPECT_EQ(0, counterValue("dynamicmodulescustom.shadow_mismatch"));
}

// A StopIterationAndSkipRoute status drops the catch all route and lets matching carry on to a
// more specific route.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, SkipRouteSkipsCatchAllRoute) {
  setupSkipRouteTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Without a decision the catch all route stays in effect, so the request reaches its cluster.
  auto baseline = sendRequest({{":path", "/specific"}});
  EXPECT_EQ("200", baseline->headers().getStatusValue());
  test_server_->waitForCounter("cluster.cluster_0.upstream_rq_200", testing::Ge(1));

  // The skip drops the catch all route, so matching reaches the specific route.
  auto response = sendRequest({{":path", "/specific"}, {"x-skip-route", "true"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.canary.upstream_rq_200", testing::Ge(1));
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.route_skipped",
                               testing::Ge(1));
}

// A StopIterationAndSkipRoute status on a request that matches no other route yields no route,
// so the request gets a 404.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, SkipRouteOnTheLastRouteReturns404) {
  setupSkipRouteTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The specific route does not match "/", so the catch all route is the last one to try.
  auto response = sendRequest({{"x-skip-route", "true"}});
  EXPECT_EQ("404", response->headers().getStatusValue());
}

// With the CONTINUE_MATCHING failure policy a module error drops the catch all route and lets
// matching carry on to a more specific route.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, ContinueMatchingFailurePolicyContinuesOnError) {
  setupSkipRouteTest("CONTINUE_MATCHING");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{":path", "/specific"}, {"x-decision", "error"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.canary.upstream_rq_200", testing::Ge(1));
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.failure_module_error",
                               testing::Ge(1));
}

// Unsetting the selected template reverts the base of the final route to the route that matching
// resolved, with the recorded overrides still applied, and the input route getters revert too.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, UnsetTemplateRevertsToTheResolvedRoute) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-template", "canary"},
                               {"x-unset-template", "canary"},
                               {"x-cluster", "canary"},
                               {"x-echo", "route-cluster-name"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  // The echo reads the route the getters reflect, which reverted to the route matching resolved.
  EXPECT_EQ("cluster_0", header(response->headers(), "x-echo-result"));
  // The recorded cluster override survived the unset and was applied to that route.
  test_server_->waitForCounter("cluster.canary.upstream_rq_200", testing::Ge(1));
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.decision_has_override",
                               testing::Ge(1));
}

// A selection attempt that failed cannot be unset: only the selected template can. The failure
// policy still applies, so a typo'd identifier never falls back silently.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, UnsetTemplateDoesNotForgetAFailedSelection) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest({{"x-template", "unknown"}, {"x-unset-template", "unknown"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter(
      "dynamicmodulescustom.route_specifier.test.failure_template_not_selected", testing::Ge(1));
}

// Unsetting with an identifier that is not the selected one changes nothing, so the template
// stays in effect.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, UnsetTemplateWithAnotherIdLeavesTheSelection) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest(
      {{"x-template", "canary"}, {"x-unset-template", "other"}, {"x-echo", "route-cluster-name"}});
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("canary", header(response->headers(), "x-echo-result"));
  test_server_->waitForCounter("dynamicmodulescustom.route_specifier.test.decision_has_template",
                               testing::Ge(1));
}

// Unsetting the selected route override reverts it, so nothing of the override applies to the
// final route.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest, UnsetRouteOverrideRevertsTheSelection) {
  setupTest(R"EOF(
route_overrides:
- override_id: regex
  regex_rewrite:
    pattern: {regex: "^/api/(.*)$"}
    substitution: '/internal/\1'
)EOF");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest(
      {{":path", "/api/foo?q=1"}, {"x-override", "regex"}, {"x-unset-override", "regex"}});
  EXPECT_EQ("200", response->headers().getStatusValue());

  const auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_NE(nullptr, upstream_headers);
  EXPECT_EQ("/api/foo?q=1", upstream_headers->getPathValue());
}

// Unsetting an override with an identifier that is not the selected one changes nothing, so the
// selected override stays in effect.
TEST_P(DynamicModuleRouteSpecifierIntegrationTest,
       UnsetRouteOverrideWithAnotherIdLeavesTheSelection) {
  setupTest(R"EOF(
route_overrides:
- override_id: regex
  regex_rewrite:
    pattern: {regex: "^/api/(.*)$"}
    substitution: '/internal/\1'
)EOF");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = sendRequest(
      {{":path", "/api/foo?q=1"}, {"x-override", "regex"}, {"x-unset-override", "other"}});
  EXPECT_EQ("200", response->headers().getStatusValue());

  const auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_NE(nullptr, upstream_headers);
  EXPECT_EQ("/internal/foo?q=1", upstream_headers->getPathValue());
}

} // namespace
} // namespace DynamicModules
} // namespace RouteSpecifiers
} // namespace Extensions
} // namespace Envoy

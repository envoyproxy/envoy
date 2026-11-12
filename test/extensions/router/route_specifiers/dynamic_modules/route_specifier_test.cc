#include <memory>
#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/extensions/router/route_specifiers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"
#include "source/common/config/metadata.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/route_specifier_impl.h"
#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/router/route_specifiers/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/thread_factory_for_test.h"
#include "test/test_common/utility.h"

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_replace.h"
#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace RouteSpecifiers {
namespace DynamicModules {

class DeclaredVirtualClusterRegistryPeer {
public:
  // The number of entries that the registry the virtual clusters came from keeps.
  static size_t entryCount(const DeclaredVirtualClusterSet& clusters) {
    return clusters.registry_->declarations_.size();
  }
  // Files the virtual clusters under the entry of the declaration, like a colliding fingerprint.
  static void collide(const DeclaredVirtualClusterSetConstSharedPtr& clusters,
                      const VirtualClusterOverrideProto& declaration) {
    clusters->registry_->declarations_[DeclaredVirtualClusterRegistry::keyOf(declaration)]
        .push_back(clusters);
  }
  // The number of declarations, released ones included, that the registry keeps entries for.
  static size_t declarationCount(const DeclaredVirtualClusterSet& clusters) {
    size_t count = 0;
    for (const auto& entry : clusters.registry_->declarations_) {
      count += entry.second.size();
    }
    return count;
  }
};

namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::NiceMock;

// Builds a route configuration whose only virtual host runs the given route specifier
// configuration, so that templates are built through the same path a real configuration uses.
std::string routeConfigYaml(absl::string_view specifier_yaml) {
  return fmt::format(R"EOF(
name: test_route_config
virtual_hosts:
- name: test_vhost
  domains: ["*"]
  routes:
  - match: {{prefix: "/"}}
    route: {{cluster: matched_cluster}}
  route_specifiers:
  - name: envoy.router.route_specifiers.dynamic_modules
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier
{}
)EOF",
                     specifier_yaml);
}

// Builds a route configuration whose first route is a catch all carrying the given route level
// specifier, followed by a specific route, so a test can see matching carry on past the catch all.
std::string catchAllRouteConfigYaml(absl::string_view module_name,
                                    absl::string_view failure_policy) {
  return fmt::format(R"EOF(
name: test_route_config
virtual_hosts:
- name: test_vhost
  domains: ["*"]
  routes:
  - match: {{prefix: "/"}}
    route: {{cluster: catch_all_cluster}}
    route_specifiers:
    - name: envoy.router.route_specifiers.dynamic_modules
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier
        dynamic_module_config:
          name: {}
          do_not_close: true
        specifier_name: test_route_specifier
        stat_prefix: test
        failure_policy: {}
  - match: {{prefix: "/specific"}}
    route: {{cluster: specific_cluster}}
)EOF",
                     module_name, failure_policy);
}

// Indents a specifier configuration so that it nests under `typed_config`.
std::string specifierYaml(absl::string_view module_name, absl::string_view body) {
  return fmt::format(R"EOF(      dynamic_module_config:
        name: {}
        do_not_close: true
      specifier_name: test_route_specifier
      stat_prefix: test
{})EOF",
                     module_name, body);
}

// Request headers with everything route matching needs, so that only the specifier under test can
// change the outcome.
Http::TestRequestHeaderMapImpl requestHeaders(absl::string_view path = "/") {
  return Http::TestRequestHeaderMapImpl{{":authority", "host"},
                                        {":path", std::string(path)},
                                        {":method", "GET"},
                                        {":scheme", "http"},
                                        {"x-forwarded-proto", "http"}};
}

class DynamicModuleRouteSpecifierTest : public testing::Test {
public:
  DynamicModuleRouteSpecifierTest() {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
    ON_CALL(context_.api_, customStatNamespaces())
        .WillByDefault(testing::ReturnRef(custom_stat_namespaces_));
  }

  absl::StatusOr<std::shared_ptr<Envoy::Router::ConfigImpl>>
  loadConfig(absl::string_view specifier_yaml) {
    return loadRaw(routeConfigYaml(specifier_yaml));
  }

  absl::StatusOr<std::shared_ptr<Envoy::Router::ConfigImpl>> loadRaw(absl::string_view yaml) {
    envoy::config::route::v3::RouteConfiguration proto_config;
    TestUtility::loadFromYaml(std::string(yaml), proto_config);
    return Envoy::Router::ConfigImpl::create(proto_config, context_, creation_status_visitor_,
                                             init_manager_, false);
  }

  uint64_t counterValue(absl::string_view name) {
    return context_.store_.counter(absl::StrCat("dynamicmodulescustom.route_specifier.test.", name))
        .value();
  }

  // Finalizes the request headers through the route entry, after checking that the entry reports
  // the path it finalizes through currentUrlPathAfterRewrite.
  Http::TestRequestHeaderMapImpl finalize(const Envoy::Router::RouteEntry& entry,
                                          Http::TestRequestHeaderMapImpl headers,
                                          bool insert_envoy_original_path = true) {
    Formatter::Context formatter_context(&headers);
    const std::string expected_path =
        entry.currentUrlPathAfterRewrite(headers, formatter_context, stream_info_);
    entry.finalizeRequestHeaders(headers, formatter_context, stream_info_,
                                 insert_envoy_original_path);
    EXPECT_EQ(expected_path, headers.getPathValue());
    return headers;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Init::MockManager> init_manager_;
  ProtobufMessage::NullValidationVisitorImpl creation_status_visitor_;
  Stats::CustomStatNamespacesImpl custom_stat_namespaces_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  DynamicModuleRouteSpecifierFactory factory_;
};

TEST_F(DynamicModuleRouteSpecifierTest, FactoryName) {
  EXPECT_EQ("envoy.router.route_specifiers.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleRouteSpecifierTest, ModuleNotFound) {
  const auto config =
      loadConfig(specifierYaml("nonexistent_module", "      failure_policy: PASS_THROUGH\n"));
  EXPECT_THAT(config.status(), HasStatusMessage(HasSubstr("Failed to load dynamic module")));
}

TEST_F(DynamicModuleRouteSpecifierTest, MissingConfigNew) {
  const auto config = loadConfig(
      specifierYaml("route_specifier_missing_config_new", "      failure_policy: PASS_THROUGH\n"));
  EXPECT_THAT(config.status(),
              HasStatusMessage(HasSubstr("envoy_dynamic_module_on_route_specifier_config_new")));
}

TEST_F(DynamicModuleRouteSpecifierTest, MissingConfigDestroy) {
  const auto config = loadConfig(specifierYaml("route_specifier_missing_config_destroy",
                                               "      failure_policy: PASS_THROUGH\n"));
  EXPECT_THAT(config.status(), HasStatusMessage(HasSubstr(
                                   "envoy_dynamic_module_on_route_specifier_config_destroy")));
}

TEST_F(DynamicModuleRouteSpecifierTest, MissingOnRoute) {
  const auto config = loadConfig(
      specifierYaml("route_specifier_missing_on_route", "      failure_policy: PASS_THROUGH\n"));
  EXPECT_THAT(config.status(),
              HasStatusMessage(HasSubstr("envoy_dynamic_module_on_route_specifier_on_route")));
}

TEST_F(DynamicModuleRouteSpecifierTest, ConfigNewFail) {
  const auto config = loadConfig(
      specifierYaml("route_specifier_config_new_fail", "      failure_policy: PASS_THROUGH\n"));
  EXPECT_THAT(config.status(), HasStatusMessage(HasSubstr("Failed to initialize dynamic module")));
}

TEST_F(DynamicModuleRouteSpecifierTest, FailurePolicyRequired) {
  const auto config = loadConfig(specifierYaml("route_specifier_no_op", ""));
  EXPECT_THAT(config.status(), HasStatusMessage(HasSubstr("failure_policy must be set")));
}

TEST_F(DynamicModuleRouteSpecifierTest, DoNotCloseRequired) {
  const auto config = loadConfig(R"EOF(      dynamic_module_config:
        name: route_specifier_no_op
        do_not_close: false
      specifier_name: test_route_specifier
      stat_prefix: test
      failure_policy: PASS_THROUGH
)EOF");
  EXPECT_THAT(config.status(),
              HasStatusMessage(HasSubstr(
                  "dynamic_module_config.do_not_close must be true for a dynamic module route "
                  "specifier")));
}

TEST_F(DynamicModuleRouteSpecifierTest, DuplicateTemplateId) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_templates:
      - template_id: canary
        route:
          match: {prefix: "/"}
          route: {cluster: canary_cluster}
      - template_id: canary
        route:
          match: {prefix: "/"}
          route: {cluster: other_cluster}
)EOF"));
  EXPECT_THAT(config.status(), HasStatusMessage(HasSubstr("duplicate route template id 'canary'")));
}

// A repeated route override list can carry the same override_id twice, which is rejected at
// configuration load just like a duplicate template id.
TEST_F(DynamicModuleRouteSpecifierTest, DuplicateRouteOverrideId) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_overrides:
      - override_id: slow
        retry_policy: {retry_on: "5xx", num_retries: 3}
      - override_id: slow
        hedge_policy: {hedge_on_per_try_timeout: true}
)EOF"));
  EXPECT_THAT(config.status(), HasStatusMessage(HasSubstr("duplicate route override id 'slow'")));
}

TEST_F(DynamicModuleRouteSpecifierTest, TemplateWithRouteSpecifiers) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_templates:
      - template_id: canary
        route:
          match: {prefix: "/"}
          route: {cluster: canary_cluster}
          route_specifiers:
          - name: envoy.router.route_specifiers.dynamic_modules
            typed_config:
              "@type": type.googleapis.com/google.protobuf.Struct
)EOF"));
  EXPECT_THAT(config.status(),
              HasStatusMessage(HasSubstr("route_specifiers are not supported on a built route")));
}

TEST_F(DynamicModuleRouteSpecifierTest, InvalidTemplate) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      validate_clusters: true
      route_templates:
      - template_id: canary
        route:
          match: {prefix: "/"}
          route: {cluster: unknown_cluster}
)EOF"));
  EXPECT_THAT(config.status(), HasStatusMessage(HasSubstr("route template 'canary'")));
}

TEST_F(DynamicModuleRouteSpecifierTest, EmptyRouteOverride) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_overrides:
      - override_id: canary
)EOF"));
  EXPECT_THAT(config.status(),
              HasStatusMessage(HasSubstr("Route override must replace at least one property")));
}

TEST_F(DynamicModuleRouteSpecifierTest, MetadataMatchWithoutLbEntry) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_overrides:
      - override_id: canary
        metadata_match:
          filter_metadata:
            envoy.other: {key: value}
)EOF"));
  EXPECT_THAT(config.status(),
              HasStatusMessage(HasSubstr("Route override must replace at least one property")));
}

// A route override that only replaces the hedge policy is valid, so a module can add hedging
// to the route it produces.
TEST_F(DynamicModuleRouteSpecifierTest, HedgePolicyRouteOverride) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_overrides:
      - override_id: hedged
        hedge_policy:
          hedge_on_per_try_timeout: true
)EOF"));
  EXPECT_TRUE(config.ok());
}

// A route override that only replaces the rate limits is valid, so a module can rate limit
// the route it produces.
TEST_F(DynamicModuleRouteSpecifierTest, RateLimitPolicyRouteOverride) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_overrides:
      - override_id: rated
        rate_limits:
        - actions: [{destination_cluster: {}}]
)EOF"));
  EXPECT_TRUE(config.ok());
}

// A route override that only replaces the CORS policy is valid, so a module can set CORS on
// the route it produces.
TEST_F(DynamicModuleRouteSpecifierTest, CorsPolicyRouteOverride) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_overrides:
      - override_id: corsed
        cors:
          allow_origin_string_match: [{exact: "example.com"}]
          allow_methods: "GET"
)EOF"));
  EXPECT_TRUE(config.ok());
}

// A CORS policy whose origin matcher cannot be built is rejected at configuration load.
TEST_F(DynamicModuleRouteSpecifierTest, CorsPolicyInvalidOriginMatcher) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_overrides:
      - override_id: corsed
        cors:
          allow_origin_string_match: [{safe_regex: {regex: "("}}]
)EOF"));
  EXPECT_THAT(config.status(), HasStatusMessage(HasSubstr("route override 'corsed'")));
}

// A rate limit whose descriptor value cannot be built is rejected at configuration load.
TEST_F(DynamicModuleRouteSpecifierTest, RateLimitPolicyInvalidDescriptor) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_overrides:
      - override_id: rated
        rate_limits:
        - actions: [{generic_key: {descriptor_value: "%"}}]
)EOF"));
  EXPECT_THAT(config.status(), HasStatusMessage(HasSubstr("route override 'rated'")));
}

TEST_F(DynamicModuleRouteSpecifierTest, ValidConfigWithTemplatesAndOverrides) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: NO_ROUTE
      route_templates:
      - template_id: canary
        route:
          match: {prefix: "/"}
          route: {cluster: canary_cluster}
      - template_id: redirect
        route:
          match: {prefix: "/"}
          redirect: {host_redirect: "example.com"}
      route_overrides:
      - override_id: slow
        retry_policy: {retry_on: "5xx", num_retries: 3}
)EOF"));
  ASSERT_TRUE(config.ok());

  // The no-op module passes through, so the matched route stays in effect.
  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("matched_cluster", route.route->routeEntry()->clusterName());
}

// A module built against a newer ABI could record a decision this build does not know, which is
// handled by the failure policy rather than trusted.
TEST_F(DynamicModuleRouteSpecifierTest, UnknownDecisionPassesThrough) {
  const auto config = loadConfig(
      specifierYaml("route_specifier_unknown_decision", "      failure_policy: PASS_THROUGH\n"));
  ASSERT_TRUE(config.ok());

  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("matched_cluster", route.route->routeEntry()->clusterName());
  EXPECT_EQ(
      1, context_.store_.counter("dynamicmodulescustom.route_specifier.test.failure_module_error")
             .value());
}

// NO_ROUTE drops the route rather than falling back to the route table.
TEST_F(DynamicModuleRouteSpecifierTest, UnknownDecisionFailsClosed) {
  const auto config = loadConfig(
      specifierYaml("route_specifier_unknown_decision", "      failure_policy: NO_ROUTE\n"));
  ASSERT_TRUE(config.ok());

  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  EXPECT_EQ(nullptr, route.route);
}

// A module built against a newer ABI could also return a status this build does not know, which
// is handled by the failure policy as well.
TEST_F(DynamicModuleRouteSpecifierTest, UnknownStatusPassesThrough) {
  const auto config = loadConfig(
      specifierYaml("route_specifier_unknown_status", "      failure_policy: PASS_THROUGH\n"));
  ASSERT_TRUE(config.ok());

  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("matched_cluster", route.route->routeEntry()->clusterName());
  EXPECT_EQ(
      1, context_.store_.counter("dynamicmodulescustom.route_specifier.test.failure_module_error")
             .value());
  // A status Envoy does not know is not a decision the module recorded.
  EXPECT_EQ(
      1,
      context_.store_.counter("dynamicmodulescustom.route_specifier.test.unknown_status").value());
  EXPECT_EQ(
      0,
      context_.store_.counter("dynamicmodulescustom.route_specifier.test.decision_error").value());
}

// A virtual host that configures no routes at all is routed entirely by the module, which is how a
// module owns the routing of a virtual host.
TEST_F(DynamicModuleRouteSpecifierTest, RoutesVirtualHostWithoutRoutes) {
  envoy::config::route::v3::RouteConfiguration proto_config;
  TestUtility::loadFromYaml(R"EOF(
name: test_route_config
virtual_hosts:
- name: test_vhost
  domains: ["*"]
  route_specifiers:
  - name: envoy.router.route_specifiers.dynamic_modules
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier
      dynamic_module_config:
        name: route_specifier_select_template
        do_not_close: true
      specifier_name: test_route_specifier
      stat_prefix: test
      failure_policy: NO_ROUTE
      route_templates:
      - template_id: only
        route:
          match: {prefix: "/"}
          route: {cluster: module_cluster}
)EOF",
                            proto_config);
  const auto config = Envoy::Router::ConfigImpl::create(
      proto_config, context_, creation_status_visitor_, init_manager_, false);
  ASSERT_TRUE(config.ok());

  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  ASSERT_NE(nullptr, route.route->routeEntry());
  EXPECT_EQ("module_cluster", route.route->routeEntry()->clusterName());
  EXPECT_EQ("test_vhost", route.route->virtualHost().name());
}

TEST_F(DynamicModuleRouteSpecifierTest, ConfigDestroyRunsOnTeardown) {
  using GetConfigDestroyCountFuncType = int (*)(void);
  auto module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("route_specifier_no_op", "c"),
      /*do_not_close=*/true);
  ASSERT_TRUE(module.ok());
  const auto destroy_count =
      module.value()->getFunctionPointer<GetConfigDestroyCountFuncType>("getConfigDestroyCount");
  ASSERT_TRUE(destroy_count.ok());
  const int before = destroy_count.value()();

  {
    const auto config =
        loadConfig(specifierYaml("route_specifier_no_op", "      failure_policy: PASS_THROUGH\n"));
    ASSERT_TRUE(config.ok());
  }
  EXPECT_EQ(before + 1, destroy_count.value()());
}

// A route override may carry a regex rewrite, which is compiled once when the specifier loads.
TEST_F(DynamicModuleRouteSpecifierTest, RegexRewriteOverrideLoads) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_overrides:
      - override_id: rewrite
        regex_rewrite:
          pattern: {regex: "^/api/(.*)$"}
          substitution: '/internal/\1'
)EOF"));
  EXPECT_TRUE(config.ok());
}

// A route override may carry tracing, built once when the specifier loads.
TEST_F(DynamicModuleRouteSpecifierTest, TracingOverrideLoads) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_overrides:
      - override_id: traced
        tracing:
          client_sampling: {numerator: 10}
          overall_sampling: {numerator: 50}
)EOF"));
  EXPECT_TRUE(config.ok());
}

// An invalid tracing operation formatter is rejected when the specifier loads rather than throwing.
TEST_F(DynamicModuleRouteSpecifierTest, InvalidTracingOperationRejected) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_overrides:
      - override_id: traced
        tracing:
          operation: "%INVALID_COMMAND_THAT_DOES_NOT_EXIST%"
)EOF"));
  EXPECT_FALSE(config.ok());
}

// A route override may carry metadata, which is a route level property valid on any route.
TEST_F(DynamicModuleRouteSpecifierTest, MetadataOverrideLoads) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      route_overrides:
      - override_id: tagged
        metadata:
          filter_metadata:
            envoy.test.override: {group: canary}
)EOF"));
  EXPECT_TRUE(config.ok());
}

// A shadow policy of a route override that names a cluster the cluster manager does not know
// is rejected when clusters are validated, since a statically named cluster can be checked at load.
TEST_F(DynamicModuleRouteSpecifierTest, ValidateClustersRejectsUnknownShadowCluster) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      failure_policy: PASS_THROUGH
      validate_clusters: true
      route_overrides:
      - override_id: mirrored
        request_mirror_policies:
        - cluster: unknown_cluster
)EOF"));
  EXPECT_THAT(config.status(),
              HasStatusMessage(HasSubstr("unknown shadow cluster 'unknown_cluster'")));
}

// Route templates need a route builder, which a specifier configured on a route configuration does
// not have, so a configuration that pairs the two is rejected.
TEST_F(DynamicModuleRouteSpecifierTest, TemplatesRequireRouteBuilder) {
  envoy::config::route::v3::RouteConfiguration proto_config;
  TestUtility::loadFromYaml(R"EOF(
name: test_route_config
virtual_hosts:
- name: test_vhost
  domains: ["*"]
  routes:
  - match: {prefix: "/"}
    route: {cluster: matched_cluster}
route_specifiers:
- name: envoy.router.route_specifiers.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier
    dynamic_module_config:
      name: route_specifier_no_op
      do_not_close: true
    specifier_name: test_route_specifier
    stat_prefix: test
    failure_policy: PASS_THROUGH
    route_templates:
    - template_id: canary
      route:
        match: {prefix: "/"}
        route: {cluster: canary_cluster}
)EOF",
                            proto_config);
  const auto config = Envoy::Router::ConfigImpl::create(
      proto_config, context_, creation_status_visitor_, init_manager_, false);
  EXPECT_THAT(config.status(), HasStatusMessage(HasSubstr(
                                   "route templates require a route specifier configured on a "
                                   "virtual host or a route")));
}

// With the runtime feature enabled a configured metrics namespace replaces the default, is
// registered as a custom stat namespace, and roots the statistics of the specifier.
TEST_F(DynamicModuleRouteSpecifierTest, RegistersCustomStatNamespace) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix", "true"}});
  const auto config = loadConfig(R"EOF(      dynamic_module_config:
        name: route_specifier_no_op
        do_not_close: true
        metrics_namespace: custom_metrics
      specifier_name: test_route_specifier
      stat_prefix: test
      failure_policy: PASS_THROUGH
)EOF");
  ASSERT_TRUE(config.ok());
  EXPECT_TRUE(custom_stat_namespaces_.registered("custom_metrics"));

  // The statistics of the specifier are rooted at the configured metrics namespace.
  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  // The no-op module records no decision, so the default Unspecified decision is in effect.
  EXPECT_EQ(
      1,
      context_.store_.counter("custom_metrics.route_specifier.test.decision_has_override").value());
}

// A decision that records route entry overrides produces a route entry wrapper. Its rewritten path,
// header transforms and typed metadata are read directly, the way the router and its consumers read
// them rather than through the finalize hooks.
TEST_F(DynamicModuleRouteSpecifierTest, RouteEntryWrapperAccessors) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_override", "      failure_policy: PASS_THROUGH\n"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  const auto* entry = route.route->routeEntry();
  ASSERT_NE(nullptr, entry);

  // The recorded route name, cluster, and path replace those of the route.
  EXPECT_EQ("module_route_entry", route.route->routeName());
  EXPECT_EQ("canary", entry->clusterName());
  auto headers = requestHeaders();
  Formatter::Context formatter_context(&headers);
  EXPECT_EQ("/rewritten",
            entry->currentUrlPathAfterRewrite(headers, formatter_context, stream_info_));

  // The recorded scalar route entry overrides replace those of the route.
  ASSERT_TRUE(entry->idleTimeout().has_value());
  EXPECT_EQ(std::chrono::milliseconds(8000), *entry->idleTimeout());
  ASSERT_TRUE(entry->maxStreamDuration().has_value());
  EXPECT_EQ(std::chrono::milliseconds(9000), *entry->maxStreamDuration());
  EXPECT_TRUE(entry->usingNewTimeouts());
  EXPECT_EQ(4096, entry->requestBodyBufferLimit());
  EXPECT_EQ(Envoy::Upstream::ResourcePriority::High, entry->priority());

  // Each recorded header mutation lands in the transform bucket of its append action, and the
  // removal in the remove list.
  const auto request_transforms = entry->requestHeaderTransforms(stream_info_, true);
  ASSERT_EQ(1, request_transforms.headers_to_append_or_add.size());
  EXPECT_EQ("x-append", request_transforms.headers_to_append_or_add[0].first.get());
  EXPECT_EQ("value", request_transforms.headers_to_append_or_add[0].second);
  ASSERT_EQ(1, request_transforms.headers_to_add_if_absent.size());
  EXPECT_EQ("x-add-if-absent", request_transforms.headers_to_add_if_absent[0].first.get());
  ASSERT_EQ(1, request_transforms.headers_to_overwrite_or_add.size());
  EXPECT_EQ("x-overwrite", request_transforms.headers_to_overwrite_or_add[0].first.get());
  ASSERT_EQ(1, request_transforms.headers_to_remove.size());
  EXPECT_EQ("x-remove", request_transforms.headers_to_remove[0].get());
  const auto response_transforms = entry->responseHeaderTransforms(stream_info_, true);
  ASSERT_EQ(1, response_transforms.headers_to_add_if_absent.size());
  EXPECT_EQ("x-added", response_transforms.headers_to_add_if_absent[0].first.get());

  // The recorded metadata is layered onto the route.
  const auto& filter_metadata = route.route->metadata().filter_metadata();
  ASSERT_TRUE(filter_metadata.contains("envoy.test.route"));
  EXPECT_EQ("value", filter_metadata.at("envoy.test.route").fields().at("key").string_value());
  // The typed accessor reads the same layered metadata, exercised for coverage since string
  // metadata has no typed representation to assert.
  static_cast<void>(route.route->typedMetadata());
}

// A decision that selects a declared route override applies its properties, so the route entry
// wrapper reads each one from the override rather than from the route.
TEST_F(DynamicModuleRouteSpecifierTest, RouteEntryWrapperRouteOverrideAccessors) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_override", R"EOF(      failure_policy: PASS_THROUGH
      specifier_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: select-override
      route_overrides:
      - override_id: applied
        retry_policy: {retry_on: "5xx", num_retries: 3}
        hedge_policy: {hedge_on_per_try_timeout: true}
        hash_policy:
        - header: {header_name: x-hash}
        metadata_match:
          filter_metadata:
            envoy.lb: {version: canary}
        rate_limits:
        - actions: [{generic_key: {descriptor_value: rated}}]
        cors:
          allow_origin_string_match: [{exact: "example.com"}]
        request_mirror_policies:
        - cluster: canary
)EOF"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  const auto* entry = route.route->routeEntry();
  ASSERT_NE(nullptr, entry);

  // Each property the override sets is read from the override rather than from the route.
  ASSERT_NE(nullptr, entry->retryPolicy());
  EXPECT_EQ(3, entry->retryPolicy()->numRetries());
  EXPECT_TRUE(entry->hedgePolicy().hedgeOnPerTryTimeout());
  EXPECT_NE(nullptr, entry->hashPolicy());
  EXPECT_NE(nullptr, entry->metadataMatchCriteria());
  EXPECT_FALSE(entry->rateLimitPolicy().empty());
  EXPECT_NE(nullptr, entry->corsPolicy());
  ASSERT_FALSE(entry->shadowPolicies().empty());
  EXPECT_EQ("canary", entry->shadowPolicies()[0]->cluster());
}

// A decision that records only route metadata produces a route wrapper, whose metadata accessors
// layer the recorded metadata onto the route.
TEST_F(DynamicModuleRouteSpecifierTest, RouteWrapperMetadataAccessors) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_override", R"EOF(      failure_policy: PASS_THROUGH
      specifier_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: route-only
)EOF"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  const auto& filter_metadata = route.route->metadata().filter_metadata();
  ASSERT_TRUE(filter_metadata.contains("envoy.test.route"));
  EXPECT_EQ("value", filter_metadata.at("envoy.test.route").fields().at("key").string_value());
  // The typed accessor reads the same layered metadata, exercised for coverage.
  static_cast<void>(route.route->typedMetadata());
}

// A decision that records only a route name produces a route wrapper whose name replaces that of
// the route, without recording any route entry override.
TEST_F(DynamicModuleRouteSpecifierTest, RouteWrapperRouteName) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_override", R"EOF(      failure_policy: PASS_THROUGH
      specifier_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: route-name
)EOF"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("module_route", route.route->routeName());
}

// A route wrapper that records no metadata delegates both metadata accessors to the route.
TEST_F(DynamicModuleRouteSpecifierTest, RouteWrapperMetadataFallback) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_override", R"EOF(      failure_policy: PASS_THROUGH
      specifier_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: filter-only
)EOF"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  // The recorded filter override marks the wrapper, confirming it is a route wrapper.
  EXPECT_TRUE(route.route->filterDisabled("envoy.test.disabled").value_or(false));
  // Only a filter override was recorded, so the metadata accessors and the route name fall back to
  // the route, which carries no envoy.test.route metadata and no name.
  EXPECT_FALSE(route.route->metadata().filter_metadata().contains("envoy.test.route"));
  EXPECT_EQ("", route.route->routeName());
  static_cast<void>(route.route->typedMetadata());
}

// A route entry wrapper that records no metadata delegates both metadata accessors to the route.
TEST_F(DynamicModuleRouteSpecifierTest, RouteEntryWrapperMetadataFallback) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_override", R"EOF(      failure_policy: PASS_THROUGH
      specifier_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: entry-no-meta
)EOF"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("canary", route.route->routeEntry()->clusterName());
  // Only a cluster override was recorded, so the metadata accessors and the route name fall back to
  // the route.
  EXPECT_FALSE(route.route->metadata().filter_metadata().contains("envoy.test.route"));
  EXPECT_EQ("", route.route->routeName());
  static_cast<void>(route.route->typedMetadata());
}

// A typed metadata factory that rejects the metadata recorded under its namespace, so the build of
// the route metadata pack fails the way a real factory would on input it cannot parse.
class RejectingRouteMetadataFactory : public Envoy::Router::HttpRouteTypedMetadataFactory {
public:
  std::string name() const override { return "envoy.test.route"; }
  std::unique_ptr<const Envoy::Config::TypedMetadata::Object>
  parse(const Protobuf::Struct&) const override {
    throw EnvoyException("route metadata rejected by the test factory");
  }
  std::unique_ptr<const Envoy::Config::TypedMetadata::Object>
  parse(const Protobuf::Any&) const override {
    return nullptr;
  }
};

// When a typed metadata factory rejects the metadata a module records, the decision cannot be
// honored, and the failure policy resolves it by keeping the matched route.
TEST_F(DynamicModuleRouteSpecifierTest, RouteMetadataRejectionFailsOpen) {
  RejectingRouteMetadataFactory factory;
  Registry::InjectFactory<Envoy::Router::HttpRouteTypedMetadataFactory> injection(factory);
  const auto config =
      loadConfig(specifierYaml("route_specifier_override", "      failure_policy: PASS_THROUGH\n"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("matched_cluster", route.route->routeEntry()->clusterName());
  EXPECT_EQ(
      1, context_.store_.counter("dynamicmodulescustom.route_specifier.test.failure_route_metadata")
             .value());
}

// A StopIterationAndSkipRoute status drops the matched route and lets matching carry on to the
// next route.
TEST_F(DynamicModuleRouteSpecifierTest, SkipRouteStatusSkipsToTheNextRoute) {
  const auto config =
      loadRaw(catchAllRouteConfigYaml("route_specifier_continue_matching", "PASS_THROUGH"));
  ASSERT_TRUE(config.ok());

  const auto route = config.value()->route(requestHeaders("/specific"), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  ASSERT_NE(nullptr, route.route->routeEntry());
  EXPECT_EQ("specific_cluster", route.route->routeEntry()->clusterName());
  EXPECT_EQ(
      1,
      context_.store_.counter("dynamicmodulescustom.route_specifier.test.route_skipped").value());
}

// A StopIterationAndSkipRoute status on the last route that matches yields no route, since there
// is nothing left to match.
TEST_F(DynamicModuleRouteSpecifierTest, SkipRouteStatusOnTheLastRouteYieldsNoRoute) {
  const auto config =
      loadRaw(catchAllRouteConfigYaml("route_specifier_continue_matching", "PASS_THROUGH"));
  ASSERT_TRUE(config.ok());

  // The specific route does not match this path, so the catch all is the last route to try.
  const auto route = config.value()->route(requestHeaders("/"), stream_info_, 0);
  EXPECT_EQ(nullptr, route.route);
}

// With the CONTINUE_MATCHING failure policy a module error drops the matched route and lets
// matching carry on to the next route.
TEST_F(DynamicModuleRouteSpecifierTest, ContinueMatchingFailurePolicyContinuesOnError) {
  const auto config =
      loadRaw(catchAllRouteConfigYaml("route_specifier_unknown_decision", "CONTINUE_MATCHING"));
  ASSERT_TRUE(config.ok());

  const auto route = config.value()->route(requestHeaders("/specific"), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  ASSERT_NE(nullptr, route.route->routeEntry());
  EXPECT_EQ("specific_cluster", route.route->routeEntry()->clusterName());
  EXPECT_EQ(
      1, context_.store_.counter("dynamicmodulescustom.route_specifier.test.failure_module_error")
             .value());
  // The failure path has its own counters, so the skip it produced is not counted as one the
  // module asked for.
  EXPECT_EQ(
      0,
      context_.store_.counter("dynamicmodulescustom.route_specifier.test.route_skipped").value());
}

// A selection that names a template that is not declared cannot be honored, so the failure
// policy applies rather than silently falling back to the resolved route.
TEST_F(DynamicModuleRouteSpecifierTest, TemplateNotSelectedPassesThrough) {
  const auto config = loadConfig(
      specifierYaml("route_specifier_select_template", "      failure_policy: PASS_THROUGH\n"));
  ASSERT_TRUE(config.ok());

  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("matched_cluster", route.route->routeEntry()->clusterName());
  EXPECT_EQ(1,
            context_.store_
                .counter("dynamicmodulescustom.route_specifier.test.failure_template_not_selected")
                .value());
}

// The metadata a selected override carries is layered under the metadata the module records
// itself: the module's value wins for the same key, the override contributes its other keys, and
// the typed metadata the override carries reaches the produced route.
TEST_F(DynamicModuleRouteSpecifierTest, RouteOverrideMetadataLayersUnderModuleMetadata) {
  const auto config =
      loadConfig(specifierYaml("route_specifier_override", R"EOF(      failure_policy: PASS_THROUGH
      specifier_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: select-override
      route_overrides:
      - override_id: applied
        metadata:
          filter_metadata:
            envoy.test.override: {group: override, extra: carried}
          typed_filter_metadata:
            envoy.test.typed:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: carried
)EOF"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders(), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);

  const auto& metadata = route.route->metadata();
  const auto& fields = metadata.filter_metadata().at("envoy.test.override").fields();
  // The module recorded group=module, which wins over the group=override the override carries.
  EXPECT_EQ("module", fields.at("group").string_value());
  EXPECT_EQ("carried", fields.at("extra").string_value());
  EXPECT_TRUE(metadata.typed_filter_metadata().contains("envoy.test.typed"));
}

// A route configuration whose only route has the given action, under the rewrite rule module in the
// given mode with a route override that carries a regex rewrite and one that carries none.
std::string rewriteRuleConfigYaml(absl::string_view mode,
                                  absl::string_view route_action = "{cluster: matched_cluster}",
                                  absl::string_view extra = "") {
  return fmt::format(R"EOF(
name: test_route_config
virtual_hosts:
- name: test_vhost
  domains: ["*"]
  routes:
  - match: {{prefix: "/"}}
    route: {}
  route_specifiers:
  - name: envoy.router.route_specifiers.dynamic_modules
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier
{}
)EOF",
                     route_action,
                     specifierYaml("route_specifier_rewrite_rule",
                                   fmt::format(R"EOF(      failure_policy: PASS_THROUGH
      specifier_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: {}
      route_overrides:
      - override_id: rewrite
        regex_rewrite:
          pattern: {{regex: "^/(.+)$"}}
          substitution: '/regex/\1'
      - override_id: plain
        retry_policy: {{num_retries: 1}}
{})EOF",
                                               mode, extra)));
}

// Whether the last call of the rewrite rule module was accepted.
std::string ruleAccepted(const Envoy::Router::Route& route) {
  return Envoy::Config::Metadata::metadataValue(&route.metadata(), "envoy.test.rule", "accepted")
      .string_value();
}

// Matches finalized request headers by their path and their x-envoy-original-path, which is empty
// when they carry none.
MATCHER_P2(HasPaths, path, original_path, "") {
  *result_listener << "with path " << arg.getPathValue() << " and original path "
                   << arg.getEnvoyOriginalPathValue();
  return arg.getPathValue() == path && arg.getEnvoyOriginalPathValue() == original_path;
}

// A rule applies to the path the request carries when its headers are finalized, so the route
// rewrites a request it was not built for the way a configured route does.
TEST_F(DynamicModuleRouteSpecifierTest, PrefixRewriteRuleAppliesToTheFinalizedPath) {
  const auto config = loadRaw(rewriteRuleConfigYaml("literal"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders("/api/v1?x=1"), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("true", ruleAccepted(*route.route));
  const auto* entry = route.route->routeEntry();
  ASSERT_NE(nullptr, entry);

  EXPECT_THAT(finalize(*entry, requestHeaders("/API/v2?y=2")),
              HasPaths("/internal/v2?y=2", "/API/v2?y=2"));
  EXPECT_THAT(finalize(*entry, requestHeaders("/api/v3"), false), HasPaths("/internal/v3", ""));
  EXPECT_EQ(0, counterValue("rewrite_not_applied"));
}

// A rule over the whole path replaces the path without its query string and fragment.
TEST_F(DynamicModuleRouteSpecifierTest, WholePathPrefixRewriteRuleKeepsTheQuery) {
  const auto config = loadRaw(rewriteRuleConfigYaml("whole-path"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders("/api/v1"), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("true", ruleAccepted(*route.route));
  EXPECT_THAT(finalize(*route.route->routeEntry(), requestHeaders("/anything/else?q=1#fragment")),
              HasPaths("/fixed?q=1#fragment", "/anything/else?q=1#fragment"));
}

// An invalid rule, or one that cannot apply to the current request, is rejected and records
// nothing.
class RejectedPrefixRewriteRuleTest : public DynamicModuleRouteSpecifierTest,
                                      public testing::WithParamInterface<std::string> {};

INSTANTIATE_TEST_SUITE_P(Modes, RejectedPrefixRewriteRuleTest,
                         testing::Values("not-prefix", "whole-path-with-matched",
                                         "empty-replacement", "invalid-replacement", "unknown-span",
                                         "too-long", "relative-replacement", "query-in-replacement",
                                         "fragment-in-replacement", "result-too-long"));

TEST_P(RejectedPrefixRewriteRuleTest, RecordsNothing) {
  const auto config = loadRaw(rewriteRuleConfigYaml(GetParam(), "{cluster: matched_cluster}",
                                                    "      max_rewritten_path_bytes: 16\n"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders("/api/v1"), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("false", ruleAccepted(*route.route));
  // Only the metadata was recorded, which needs no route entry wrapper.
  EXPECT_EQ(nullptr, dynamic_cast<const DynamicModuleRouteEntry*>(route.route->routeEntry()));
}

// A literal span ends before the query string and fragment, so a matched that reaches into them is
// rejected even though it is a prefix of the path the request carries.
TEST_F(DynamicModuleRouteSpecifierTest, LiteralSpanEndsBeforeTheQueryAndFragment) {
  for (const auto& [mode, path] : std::vector<std::pair<std::string, std::string>>{
           {"query-in-matched", "/api?x=1"}, {"fragment-in-matched", "/api#x"}}) {
    SCOPED_TRACE(mode);
    const auto config = loadRaw(rewriteRuleConfigYaml(mode, "{cluster: matched_cluster}"));
    ASSERT_TRUE(config.ok());
    const auto route = config.value()->route(requestHeaders(path), stream_info_, 0);
    ASSERT_NE(nullptr, route.route);
    EXPECT_EQ("false", ruleAccepted(*route.route));
  }
}

// A recorded path takes precedence over a rule whatever the call order, so the rule is then neither
// applied nor counted.
TEST_F(DynamicModuleRouteSpecifierTest, RecordedPathTakesPrecedenceOverPrefixRewriteRule) {
  for (const std::string mode : {"path-wins", "rule-then-path"}) {
    SCOPED_TRACE(mode);
    const auto config = loadRaw(rewriteRuleConfigYaml(mode));
    ASSERT_TRUE(config.ok());
    const auto route = config.value()->route(requestHeaders("/api/v1"), stream_info_, 0);
    ASSERT_NE(nullptr, route.route);
    EXPECT_EQ("true", ruleAccepted(*route.route));
    const auto* entry = route.route->routeEntry();
    ASSERT_NE(nullptr, entry);

    EXPECT_THAT(finalize(*entry, requestHeaders("/api/v2")), HasPaths("/literal", "/api/v2"));
    EXPECT_THAT(finalize(*entry, requestHeaders("/other")), HasPaths("/literal", "/other"));
  }
  EXPECT_EQ(0, counterValue("rewrite_not_applied"));
}

// A rule takes precedence over the regex rewrite of a selected route override whatever the call
// order. The regex rewrite applies to a path the rule does not when the request headers are
// finalized, and then records that path over the original path the request carries.
TEST_F(DynamicModuleRouteSpecifierTest, PrefixRewriteRuleTakesPrecedenceOverRouteOverrideRegex) {
  for (const std::string mode : {"rule-over-override", "rule-then-override"}) {
    SCOPED_TRACE(mode);
    const auto config = loadRaw(rewriteRuleConfigYaml(mode));
    ASSERT_TRUE(config.ok());
    const auto route = config.value()->route(requestHeaders("/api/v1"), stream_info_, 0);
    ASSERT_NE(nullptr, route.route);
    EXPECT_EQ("true", ruleAccepted(*route.route));
    const auto* entry = route.route->routeEntry();
    ASSERT_NE(nullptr, entry);

    EXPECT_THAT(finalize(*entry, requestHeaders("/api/v2")), HasPaths("/internal/v2", "/api/v2"));
    auto other = requestHeaders("/other?q=1");
    other.setEnvoyOriginalPath("/carried");
    EXPECT_THAT(finalize(*entry, std::move(other)), HasPaths("/regex/other?q=1", "/other?q=1"));
  }
  EXPECT_EQ(0, counterValue("rewrite_not_applied"));
}

// A rule applies to a path exactly when it produces one, which checking it does without building
// the path.
TEST(PrefixRewriteRuleTest, AppliesToAgreesWithApply) {
  const PrefixRewriteRule literal{"/api", "/internal"};
  const PrefixRewriteRule whole_path{std::nullopt, "/fixed"};
  const PrefixRewriteRule empty_replacement{"/api", ""};
  for (const PrefixRewriteRule* rule : {&literal, &whole_path, &empty_replacement}) {
    for (const absl::string_view path : {"/api/v1", "/API/v1?q=1#f", "/api", "/ap", "/other"}) {
      for (const size_t max_bytes : {0, 6, 9, 11, 12, 64}) {
        SCOPED_TRACE(absl::StrCat(path, " ", max_bytes));
        EXPECT_EQ(rule->apply(path, max_bytes).has_value(), rule->appliesTo(path, max_bytes));
      }
    }
  }
}

// A recorded path takes precedence over the regex rewrite of a selected route override.
TEST_F(DynamicModuleRouteSpecifierTest, RecordedPathTakesPrecedenceOverRouteOverrideRegex) {
  const auto config = loadRaw(rewriteRuleConfigYaml("path-over-override"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders("/api/v1"), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("true", ruleAccepted(*route.route));
  EXPECT_THAT(finalize(*route.route->routeEntry(), requestHeaders("/api/v2")),
              HasPaths("/literal", "/api/v2"));
}

// Without a rule the regex rewrite of a selected route override applies when the decision
// resolves, so the route carries the path it produced and keeps an original path the request
// carries.
TEST_F(DynamicModuleRouteSpecifierTest, RouteOverrideRegexRewriteAppliesWhenTheDecisionResolves) {
  const auto config = loadRaw(rewriteRuleConfigYaml("override-only"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders("/api/v1"), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("true", ruleAccepted(*route.route));
  const auto* entry = route.route->routeEntry();
  ASSERT_NE(nullptr, entry);

  auto headers = requestHeaders("/other?q=1");
  headers.setEnvoyOriginalPath("/carried");
  EXPECT_THAT(finalize(*entry, std::move(headers)), HasPaths("/regex/api/v1", "/carried"));
  EXPECT_EQ(0, counterValue("rewrite_not_applied"));
}

// A later rule replaces the rule of an earlier call.
TEST_F(DynamicModuleRouteSpecifierTest, LaterPrefixRewriteRuleReplacesTheEarlierOne) {
  const auto config = loadRaw(rewriteRuleConfigYaml("replaced"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders("/api/v1"), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("true", ruleAccepted(*route.route));
  EXPECT_THAT(finalize(*route.route->routeEntry(), requestHeaders("/api/v1")),
              HasPaths("/second/v1", "/api/v1"));
}

// A rejected rule records nothing, so the rule of an earlier call stays.
TEST_F(DynamicModuleRouteSpecifierTest, RejectedPrefixRewriteRuleKeepsTheEarlierOne) {
  const auto config = loadRaw(rewriteRuleConfigYaml("rejected-after-accepted"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders("/api/v1"), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("false", ruleAccepted(*route.route));
  EXPECT_THAT(finalize(*route.route->routeEntry(), requestHeaders("/api/v1")),
              HasPaths("/first/v1", "/api/v1"));
}

// A rule that does not apply when the request headers are finalized leaves the rewrite of the route
// in effect and is counted.
TEST_F(DynamicModuleRouteSpecifierTest, PrefixRewriteRuleThatDoesNotApplyLeavesTheRouteRewrite) {
  const auto config = loadRaw(
      rewriteRuleConfigYaml("literal", R"({cluster: matched_cluster, prefix_rewrite: "/route/"})"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders("/api/v1"), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("true", ruleAccepted(*route.route));
  const auto* entry = route.route->routeEntry();
  ASSERT_NE(nullptr, entry);

  EXPECT_THAT(finalize(*entry, requestHeaders("/other/x")), HasPaths("/route/other/x", "/other/x"));
  EXPECT_EQ(1, counterValue("rewrite_not_applied"));

  // Where the rule applies it replaces the rewrite of the route.
  EXPECT_THAT(finalize(*entry, requestHeaders("/api/v2")), HasPaths("/internal/v2", "/api/v2"));
  EXPECT_EQ(1, counterValue("rewrite_not_applied"));
}

// A rule that does not apply, followed by a regex rewrite that yields nothing, here one over the
// bound, leaves the rewrite of the route in effect and is counted.
TEST_F(DynamicModuleRouteSpecifierTest, RouteOverrideRegexRewriteThatYieldsNothingIsCounted) {
  const auto config = loadRaw(rewriteRuleConfigYaml(
      "rule-over-override", R"({cluster: matched_cluster, prefix_rewrite: "/route/"})",
      "      max_rewritten_path_bytes: 16\n"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders("/api/v1"), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("true", ruleAccepted(*route.route));
  const auto* entry = route.route->routeEntry();
  ASSERT_NE(nullptr, entry);

  EXPECT_THAT(finalize(*entry, requestHeaders("/a-path-past-the-bound")),
              HasPaths("/route/a-path-past-the-bound", "/a-path-past-the-bound"));
  EXPECT_EQ(1, counterValue("rewrite_not_applied"));
}

// A route override that carries no rewrite leaves the rewrite of the route in effect, uncounted.
TEST_F(DynamicModuleRouteSpecifierTest, RouteOverrideWithoutARewriteLeavesTheRouteRewrite) {
  const auto config = loadRaw(rewriteRuleConfigYaml(
      "no-rewrite", R"({cluster: matched_cluster, prefix_rewrite: "/route/"})"));
  ASSERT_TRUE(config.ok());
  const auto route = config.value()->route(requestHeaders("/api/v1"), stream_info_, 0);
  ASSERT_NE(nullptr, route.route);
  EXPECT_EQ("true", ruleAccepted(*route.route));
  const auto* entry = dynamic_cast<const DynamicModuleRouteEntry*>(route.route->routeEntry());
  ASSERT_NE(nullptr, entry);
  EXPECT_THAT(finalize(*entry, requestHeaders("/api/v1")), HasPaths("/route/api/v1", "/api/v1"));
  EXPECT_EQ(0, counterValue("rewrite_not_applied"));
}

constexpr int CacheDisabled =
    envoy_dynamic_module_type_route_specifier_built_route_lookup_result_Disabled;
constexpr int CacheMiss = envoy_dynamic_module_type_route_specifier_built_route_lookup_result_Miss;
constexpr int CacheHit = envoy_dynamic_module_type_route_specifier_built_route_lookup_result_Hit;
constexpr int CacheHitVerify =
    envoy_dynamic_module_type_route_specifier_built_route_lookup_result_HitVerify;

// A built_route_cache block keeping max_entries routes per worker, followed by the given fields.
std::string builtRouteCacheYaml(uint32_t max_entries, absl::string_view fields = "") {
  return fmt::format("      built_route_cache:\n        max_entries_per_worker: {}\n{}",
                     max_entries, fields);
}

// The built route cache module in the given mode with the given cache block and the route overrides
// and templates it selects.
std::string builtRouteCacheModuleYaml(absl::string_view mode, absl::string_view cache) {
  return specifierYaml("route_specifier_built_route_cache",
                       fmt::format(R"EOF(      failure_policy: PASS_THROUGH
      specifier_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: {}
{}      route_overrides:
      - override_id: override_1
        retry_policy: {{num_retries: 1}}
      - override_id: override_2
        retry_policy: {{num_retries: 2}}
      - override_id: with_metadata
        metadata:
          filter_metadata:
            envoy.test.cache: {{from: override}}
      - override_id: regex
        regex_rewrite:
          pattern: {{regex: "^/(.+)$"}}
          substitution: '/regex/\1'
      route_templates:
      - template_id: only
        route:
          match: {{prefix: "/"}}
          route: {{cluster: module_cluster}}
      - template_id: direct
        route:
          match: {{prefix: "/"}}
          direct_response: {{status: 204}}
      - template_id: weighted
        route:
          match: {{prefix: "/"}}
          route:
            weighted_clusters:
              clusters:
              - {{name: cluster_a, weight: 50}}
              - {{name: cluster_b, weight: 50}}
      - template_id: header
        route:
          match: {{prefix: "/"}}
          route: {{cluster_header: x-cluster}}
      - template_id: never
        route:
          match: {{prefix: "/never"}}
          route: {{cluster: module_cluster}}
)EOF",
                                   mode, cache));
}

class DynamicModuleBuiltRouteCacheTest : public DynamicModuleRouteSpecifierTest {
public:
  void SetUp() override {
    auto module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("route_specifier_built_route_cache", "c"),
        /*do_not_close=*/true);
    ASSERT_TRUE(module.ok());
    module_ = std::move(module.value());
    verificationRandom(1);
  }

  // Sets the random value that decides whether a hit is verified. A hit is verified when the value
  // is a multiple of verify_one_in.
  void verificationRandom(uint64_t value) {
    ON_CALL(context_.api_.random_, random()).WillByDefault(testing::Return(value));
  }

  // Reads one of the values the module exports for tests.
  int moduleValue(const std::string& getter) {
    auto function = module_->getFunctionPointer<int (*)(void)>(getter);
    EXPECT_TRUE(function.ok());
    return function.ok() ? function.value()() : -1;
  }
  int lastCacheResult() { return moduleValue("getLastCacheResult"); }

  // Resolves the route of a request for the path that carries the cache key.
  Envoy::Router::RouteConstSharedPtr resolve(Envoy::Router::ConfigImpl& config, uint64_t key,
                                             uint64_t random = 1,
                                             absl::string_view path = "/api/v1") {
    auto headers = requestHeaders(path);
    headers.addCopy(Http::LowerCaseString("x-key"), absl::StrCat(key));
    return config.route(headers, stream_info_, random).route;
  }

  uint64_t entries() {
    return context_.store_
        .gauge("dynamicmodulescustom.route_specifier.test.built_route_cache_entries",
               Stats::Gauge::ImportMode::NeverImport)
        .value();
  }

  Extensions::DynamicModules::DynamicModulePtr module_;
};

// A hit returns the route the miss built for the key, so the route is built once.
TEST_F(DynamicModuleBuiltRouteCacheTest, HitReturnsTheRouteTheMissBuilt) {
  const auto config = loadConfig(builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(16)));
  ASSERT_TRUE(config.ok());
  const auto first = resolve(*config.value(), 1);
  EXPECT_EQ(CacheMiss, lastCacheResult());
  const auto second = resolve(*config.value(), 1);
  EXPECT_EQ(CacheHit, lastCacheResult());
  ASSERT_NE(nullptr, first);
  EXPECT_EQ(first, second);
  EXPECT_EQ("cached_cluster", second->routeEntry()->clusterName());

  const auto other = resolve(*config.value(), 2);
  EXPECT_EQ(CacheMiss, lastCacheResult());
  EXPECT_NE(first, other);
  EXPECT_EQ(1, counterValue("built_route_cache_hit"));
  EXPECT_EQ(2, counterValue("built_route_cache_miss"));
  EXPECT_EQ(2, entries());
}

// A decision that fails is not cached.
TEST_F(DynamicModuleBuiltRouteCacheTest, FailedDecisionIsNotCached) {
  const auto config =
      loadConfig(builtRouteCacheModuleYaml("direct-entry", builtRouteCacheYaml(16)));
  ASSERT_TRUE(config.ok());
  resolve(*config.value(), 1);
  resolve(*config.value(), 1);
  EXPECT_EQ(CacheMiss, lastCacheResult());
  EXPECT_EQ(2, counterValue("failure_override_on_non_route_entry"));
  EXPECT_EQ(0, entries());
}

// A verified hit uses the route the module recorded, and evicts the cached route when it was
// recorded differently, so a later request builds the route again.
TEST_F(DynamicModuleBuiltRouteCacheTest, VerifiedHitEvictsARouteRecordedDifferently) {
  const auto config = loadConfig(builtRouteCacheModuleYaml(
      "inconsistent", builtRouteCacheYaml(16, "        verify_one_in: 2\n")));
  ASSERT_TRUE(config.ok());
  const auto first = resolve(*config.value(), 1);
  EXPECT_EQ(first, resolve(*config.value(), 1));
  EXPECT_EQ(CacheHit, lastCacheResult());

  verificationRandom(2);
  const auto second = resolve(*config.value(), 1);
  EXPECT_EQ(CacheHitVerify, lastCacheResult());
  ASSERT_NE(nullptr, second);
  EXPECT_EQ("module_cluster", first->routeEntry()->clusterName());
  EXPECT_EQ("cluster_b", second->routeEntry()->clusterName());
  EXPECT_EQ(1, counterValue("built_route_cache_verify"));
  EXPECT_EQ(1, counterValue("built_route_cache_verify_mismatch"));
  EXPECT_EQ(0, entries());

  resolve(*config.value(), 1);
  EXPECT_EQ(CacheMiss, lastCacheResult());
  EXPECT_EQ(1, entries());
}

// A verified hit whose recording fails differs from a template cached as it is, which it evicts.
TEST_F(DynamicModuleBuiltRouteCacheTest, VerifiedHitWhoseRecordingFailsEvictsTheRoute) {
  const auto config = loadConfig(builtRouteCacheModuleYaml(
      "failing-verify", builtRouteCacheYaml(16, "        verify_one_in: 2\n")));
  ASSERT_TRUE(config.ok());
  resolve(*config.value(), 1);
  EXPECT_EQ(1, entries());
  verificationRandom(2);
  resolve(*config.value(), 1);
  EXPECT_EQ(CacheHitVerify, lastCacheResult());
  EXPECT_EQ(1, counterValue("failure_override_on_non_route_entry"));
  EXPECT_EQ(1, counterValue("built_route_cache_verify_mismatch"));
  EXPECT_EQ(0, entries());
}

// A verified recording that depends on the request path could not be cached itself, so it evicts
// the cached route even when it records the same path for this request, and a later request for
// another path does not reuse that path.
TEST_F(DynamicModuleBuiltRouteCacheTest,
       VerifiedHitWhoseRecordingDependsOnTheRequestPathEvictsTheRoute) {
  const auto config = loadConfig(builtRouteCacheModuleYaml(
      "prefix-rewrite-when-verified", builtRouteCacheYaml(16, "        verify_one_in: 2\n")));
  ASSERT_TRUE(config.ok());
  resolve(*config.value(), 1);
  EXPECT_EQ(1, entries());
  verificationRandom(2);
  resolve(*config.value(), 1);
  EXPECT_EQ(CacheHitVerify, lastCacheResult());
  EXPECT_EQ(1, counterValue("built_route_cache_verify_mismatch"));
  EXPECT_EQ(0, entries());

  verificationRandom(1);
  resolve(*config.value(), 1, 1, "/api/v2");
  EXPECT_EQ(CacheMiss, lastCacheResult());
}

// A template cached as it is differs from a verified recording that carries only route overrides.
TEST_F(DynamicModuleBuiltRouteCacheTest, VerifiedHitFindsRouteOverridesMissingFromACachedTemplate) {
  const auto config = loadConfig(builtRouteCacheModuleYaml(
      "inconsistent-metadata", builtRouteCacheYaml(16, "        verify_one_in: 1\n")));
  ASSERT_TRUE(config.ok());
  const auto first = resolve(*config.value(), 1);
  const auto second = resolve(*config.value(), 1);
  EXPECT_EQ(CacheHitVerify, lastCacheResult());
  ASSERT_NE(nullptr, second);
  EXPECT_NE(first, second);
  EXPECT_TRUE(second->metadata().filter_metadata().contains("envoy.test.cache"));
  EXPECT_EQ(1, counterValue("built_route_cache_verify_mismatch"));
}

// A verified hit whose recording matches keeps the cached route, whether it carries route entry
// overrides, only route overrides, the metadata of a selected route override, or is the template
// as it is.
TEST_F(DynamicModuleBuiltRouteCacheTest, VerifiedHitKeepsARouteRecordedTheSame) {
  for (const std::string mode : {"cached", "route-only", "override-metadata", "bare"}) {
    SCOPED_TRACE(mode);
    const auto config = loadConfig(
        builtRouteCacheModuleYaml(mode, builtRouteCacheYaml(16, "        verify_one_in: 2\n")));
    ASSERT_TRUE(config.ok());
    const auto first = resolve(*config.value(), 1);
    ASSERT_NE(nullptr, first);
    verificationRandom(2);
    resolve(*config.value(), 1);
    EXPECT_EQ(CacheHitVerify, lastCacheResult());
    verificationRandom(1);
    EXPECT_EQ(first, resolve(*config.value(), 1));
    EXPECT_EQ(CacheHit, lastCacheResult());
  }
  EXPECT_EQ(4, counterValue("built_route_cache_verify"));
  EXPECT_EQ(0, counterValue("built_route_cache_verify_mismatch"));
}

// A verified hit uses a route of its own built from the recording, so the user data it records
// fires the destroy hook once that route goes, while the cached route stays.
TEST_F(DynamicModuleBuiltRouteCacheTest, VerifiedHitKeepsTheUserDataItRecordsOnItsOwnRoute) {
  const auto config = loadConfig(
      builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(16, "        verify_one_in: 1\n")));
  ASSERT_TRUE(config.ok());
  const auto first = resolve(*config.value(), 1);
  auto second = resolve(*config.value(), 1);
  EXPECT_EQ(CacheHitVerify, lastCacheResult());
  ASSERT_NE(nullptr, second);
  EXPECT_NE(first, second);
  EXPECT_EQ(0, counterValue("route_destroy"));
  second.reset();
  EXPECT_EQ(1, counterValue("route_destroy"));
  EXPECT_EQ(1, entries());
}

// A hit is verified with a chance of one in verify_one_in, which defaults to 1024, and zero
// verifies none.
TEST_F(DynamicModuleBuiltRouteCacheTest, VerificationIsSampled) {
  const auto sampled = loadConfig(
      builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(16, "        verify_one_in: 3\n")));
  ASSERT_TRUE(sampled.ok());
  resolve(*sampled.value(), 1);
  for (const uint64_t value : {1, 2, 3, 4, 6}) {
    verificationRandom(value);
    resolve(*sampled.value(), 1);
  }
  EXPECT_EQ(5, counterValue("built_route_cache_hit"));
  EXPECT_EQ(2, counterValue("built_route_cache_verify"));

  const auto defaulted = loadConfig(builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(16)));
  ASSERT_TRUE(defaulted.ok());
  resolve(*defaulted.value(), 1);
  for (const uint64_t value : {512, 1023}) {
    verificationRandom(value);
    resolve(*defaulted.value(), 1);
    EXPECT_EQ(CacheHit, lastCacheResult());
  }
  verificationRandom(1024);
  resolve(*defaulted.value(), 1);
  EXPECT_EQ(CacheHitVerify, lastCacheResult());

  const auto unverified = loadConfig(
      builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(16, "        verify_one_in: 0\n")));
  ASSERT_TRUE(unverified.ok());
  resolve(*unverified.value(), 1);
  verificationRandom(0);
  resolve(*unverified.value(), 1);
  EXPECT_EQ(CacheHit, lastCacheResult());
  EXPECT_EQ(3, counterValue("built_route_cache_verify"));
}

// The cache keys a route by the template as well, so two templates share no route for one key.
TEST_F(DynamicModuleBuiltRouteCacheTest, TemplateIsPartOfTheKey) {
  const auto config =
      loadConfig(builtRouteCacheModuleYaml("template-by-header", builtRouteCacheYaml(16)));
  ASSERT_TRUE(config.ok());
  const auto resolve_template = [&](absl::string_view template_id) {
    auto headers = requestHeaders("/api/v1");
    headers.addCopy(Http::LowerCaseString("x-template"), template_id);
    return config.value()->route(headers, stream_info_, 1).route;
  };
  const auto only = resolve_template("only");
  EXPECT_EQ(CacheMiss, lastCacheResult());
  const auto direct = resolve_template("direct");
  EXPECT_EQ(CacheMiss, lastCacheResult());
  ASSERT_NE(nullptr, only);
  EXPECT_NE(only, direct);
  EXPECT_EQ(2, entries());
  EXPECT_EQ(only, resolve_template("only"));
  EXPECT_EQ(CacheHit, lastCacheResult());
  EXPECT_EQ(direct, resolve_template("direct"));
  EXPECT_EQ(CacheHit, lastCacheResult());
}

// A hit ignores what the module records, so no route is built for it.
TEST_F(DynamicModuleBuiltRouteCacheTest, HitIgnoresTheRecording) {
  const auto config =
      loadConfig(builtRouteCacheModuleYaml("hit-then-record", builtRouteCacheYaml(16)));
  ASSERT_TRUE(config.ok());
  const auto first = resolve(*config.value(), 1);
  const auto second = resolve(*config.value(), 1);
  EXPECT_EQ(CacheHit, lastCacheResult());
  ASSERT_NE(nullptr, second);
  EXPECT_EQ(first, second);
  EXPECT_EQ("cached_cluster", second->routeEntry()->clusterName());
  EXPECT_EQ(0, counterValue("route_destroy"));
  // The module exports no user data discard hook, so the value the hit ignores is dropped.
  EXPECT_EQ(0, counterValue("user_data_discard"));
}

// A template whose cluster is picked per request builds a route specific to the request, so the
// cache is not in effect for it.
TEST_F(DynamicModuleBuiltRouteCacheTest, WeightedTemplateIsNotCached) {
  const auto config = loadConfig(builtRouteCacheModuleYaml("weighted", builtRouteCacheYaml(16)));
  ASSERT_TRUE(config.ok());
  const auto first = resolve(*config.value(), 1, 10);
  EXPECT_EQ(CacheDisabled, lastCacheResult());
  const auto second = resolve(*config.value(), 1, 60);
  ASSERT_NE(nullptr, first);
  ASSERT_NE(nullptr, second);
  EXPECT_EQ("cluster_a", first->routeEntry()->clusterName());
  EXPECT_EQ("cluster_b", second->routeEntry()->clusterName());
  EXPECT_EQ(2, counterValue("built_route_cache_disabled"));
  EXPECT_EQ(0, counterValue("built_route_cache_miss"));
  EXPECT_EQ(0, entries());
}

// A template whose cluster a request header names is not cached either, nor one whose match does
// not hold for the request.
TEST_F(DynamicModuleBuiltRouteCacheTest, ClusterHeaderTemplateIsNotCached) {
  const auto config =
      loadConfig(builtRouteCacheModuleYaml("template-by-header", builtRouteCacheYaml(16)));
  ASSERT_TRUE(config.ok());
  const auto resolve_template = [&](absl::string_view template_id) {
    auto headers = requestHeaders("/api/v1");
    headers.addCopy(Http::LowerCaseString("x-template"), template_id);
    headers.addCopy(Http::LowerCaseString("x-cluster"), "header_cluster");
    return config.value()->route(headers, stream_info_, 1).route;
  };
  const auto route = resolve_template("header");
  EXPECT_EQ(CacheDisabled, lastCacheResult());
  ASSERT_NE(nullptr, route);
  EXPECT_EQ("header_cluster", route->routeEntry()->clusterName());
  resolve_template("never");
  EXPECT_EQ(CacheDisabled, lastCacheResult());
  EXPECT_EQ(2, counterValue("built_route_cache_disabled"));
  EXPECT_EQ(0, entries());
}

// A full worker evicts a route the CLOCK hand finds unused since it last passed, sparing one used
// since then.
TEST_F(DynamicModuleBuiltRouteCacheTest, FullCacheEvictsARouteNotUsedRecently) {
  const auto config = loadConfig(builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(3)));
  ASSERT_TRUE(config.ok());
  for (uint64_t key = 1; key <= 4; ++key) {
    resolve(*config.value(), key);
  }
  // The hand cleared every route, evicted the first and stopped at the second.
  EXPECT_EQ(1, counterValue("built_route_cache_evict"));
  EXPECT_EQ(3, entries());
  resolve(*config.value(), 2);
  EXPECT_EQ(CacheHit, lastCacheResult());
  // The second route was used since the hand passed it, so the third is evicted instead.
  resolve(*config.value(), 5);
  EXPECT_EQ(2, counterValue("built_route_cache_evict"));
  resolve(*config.value(), 2);
  EXPECT_EQ(CacheHit, lastCacheResult());
  resolve(*config.value(), 3);
  EXPECT_EQ(CacheMiss, lastCacheResult());
  // The hand now points at the second route, unused since it last passed, so the sixth takes its
  // place.
  resolve(*config.value(), 6);
  resolve(*config.value(), 5);
  EXPECT_EQ(CacheHit, lastCacheResult());
  // Nothing else holds the evicted routes, so each fired the destroy hook.
  EXPECT_EQ(4, counterValue("built_route_cache_evict"));
  EXPECT_EQ(4, counterValue("route_destroy"));
}

// A worker sweeps the routes idle for longer than the entry age, also when it is idle, and runs
// the sweep only while it caches a route.
TEST_F(DynamicModuleBuiltRouteCacheTest, SweepEvictsIdleRoutes) {
  auto* timer = new NiceMock<Event::MockTimer>(&context_.thread_local_.dispatcher_);
  // Armed by the two inserts into an empty cache and by the nine sweeps that leave a route behind.
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(1000), _)).Times(11);
  const auto config = loadConfig(
      builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(16, "        max_idle_time: 4s\n")));
  ASSERT_TRUE(config.ok());
  EXPECT_FALSE(timer->enabled_);
  resolve(*config.value(), 1);
  EXPECT_TRUE(timer->enabled_);
  const auto second = resolve(*config.value(), 2);
  for (int i = 0; i < 3; ++i) {
    timer->invokeCallback();
  }
  // A use before the age passes keeps the route.
  resolve(*config.value(), 2);
  timer->invokeCallback();
  timer->invokeCallback();
  EXPECT_EQ(1, counterValue("built_route_cache_evict"));
  EXPECT_EQ(1, counterValue("route_destroy"));
  EXPECT_EQ(1, entries());
  // The second route moved into the slot the first left and is still found.
  EXPECT_EQ(second, resolve(*config.value(), 2));
  EXPECT_EQ(CacheHit, lastCacheResult());

  for (int i = 0; i < 5; ++i) {
    timer->invokeCallback();
  }
  EXPECT_EQ(0, entries());
  EXPECT_FALSE(timer->enabled_);
  resolve(*config.value(), 1);
  EXPECT_EQ(CacheMiss, lastCacheResult());
  EXPECT_TRUE(timer->enabled_);
}

// A sweep evicts every route that went idle, also several at once.
TEST_F(DynamicModuleBuiltRouteCacheTest, SweepEvictsEveryIdleRoute) {
  auto* timer = new NiceMock<Event::MockTimer>(&context_.thread_local_.dispatcher_);
  const auto config = loadConfig(
      builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(16, "        max_idle_time: 4s\n")));
  ASSERT_TRUE(config.ok());
  for (uint64_t key = 1; key <= 3; ++key) {
    resolve(*config.value(), key);
  }
  for (int i = 0; i < 4; ++i) {
    timer->invokeCallback();
  }
  EXPECT_EQ(3, entries());
  timer->invokeCallback();
  EXPECT_EQ(3, counterValue("built_route_cache_evict"));
  EXPECT_EQ(0, entries());
}

// The entry age defaults to 600 seconds, which a worker sweeps every quarter of.
TEST_F(DynamicModuleBuiltRouteCacheTest, DefaultEntryAgeIsSweptEveryQuarter) {
  auto* timer = new NiceMock<Event::MockTimer>(&context_.thread_local_.dispatcher_);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(150000), _));
  const auto config = loadConfig(builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(16)));
  ASSERT_TRUE(config.ok());
  resolve(*config.value(), 1);
}

// Destroying the route configuration releases the cached routes, each firing the destroy hook once,
// and destroys the specifier configuration.
TEST_F(DynamicModuleBuiltRouteCacheTest, TeardownReleasesCachedRoutes) {
  const int routes_before = moduleValue("getRouteDestroyCount");
  const int configs_before = moduleValue("getConfigDestroyCount");
  {
    const auto config = loadConfig(builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(16)));
    ASSERT_TRUE(config.ok());
    resolve(*config.value(), 1);
    resolve(*config.value(), 2);
    resolve(*config.value(), 1);
    EXPECT_EQ(0, counterValue("route_destroy"));
  }
  EXPECT_EQ(2, counterValue("route_destroy"));
  EXPECT_EQ(routes_before + 2, moduleValue("getRouteDestroyCount"));
  EXPECT_EQ(configs_before + 1, moduleValue("getConfigDestroyCount"));
  EXPECT_EQ(0, entries());
}

// A stream that holds a cached route when the configuration goes keeps it, and the destroy hook
// fires once the stream releases it.
TEST_F(DynamicModuleBuiltRouteCacheTest, StreamHoldingACachedRouteReleasesItLast) {
  const int routes_before = moduleValue("getRouteDestroyCount");
  Envoy::Router::RouteConstSharedPtr held;
  {
    const auto config = loadConfig(builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(16)));
    ASSERT_TRUE(config.ok());
    held = resolve(*config.value(), 1);
  }
  EXPECT_EQ(routes_before, moduleValue("getRouteDestroyCount"));
  held.reset();
  EXPECT_EQ(routes_before + 1, moduleValue("getRouteDestroyCount"));
}

// The last reference to a route configuration can drop on a worker, which destroys the slot of the
// built route cache there. The slot is removed on the main thread, which releases what each worker
// cached on that worker.
TEST_F(DynamicModuleBuiltRouteCacheTest, ConfigurationReleasedOnAWorkerReleasesItsCachedRoutes) {
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr main_dispatcher = api->allocateDispatcher("test_main_thread");
  Event::DispatcherPtr worker_dispatcher = api->allocateDispatcher("test_worker_thread");
  ThreadLocal::InstanceImpl tls;
  absl::Cleanup shutdown_tls = [&tls]() {
    tls.shutdownGlobalThreading();
    tls.shutdownThread();
  };
  tls.registerThread(*main_dispatcher, true);
  tls.registerThread(*worker_dispatcher, false);
  // Running the main dispatcher binds it to this thread, so a slot destroyed on the worker posts
  // its removal here.
  main_dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  ON_CALL(context_, threadLocal()).WillByDefault(testing::ReturnRef(tls));
  const int routes_before = moduleValue("getRouteDestroyCount");
  const int configs_before = moduleValue("getConfigDestroyCount");
  auto loaded = loadConfig(builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(16)));
  ASSERT_TRUE(loaded.ok());
  std::shared_ptr<Envoy::Router::ConfigImpl> config = std::move(loaded.value());

  // One worker thread for the whole test, since a thread that exits destroys what it holds in
  // thread local storage.
  Thread::ThreadPtr worker = Thread::threadFactoryForTest().createThread(
      [&]() { worker_dispatcher->run(Event::Dispatcher::RunType::RunUntilExit); });
  const auto on_worker = [&worker_dispatcher](const auto& work) {
    absl::Notification done;
    worker_dispatcher->post([&]() {
      work();
      done.Notify();
    });
    done.WaitForNotification();
  };
  on_worker([&]() {
    resolve(*config, 1);
    EXPECT_EQ(CacheMiss, lastCacheResult());
    config.reset();
  });
  EXPECT_EQ(routes_before, moduleValue("getRouteDestroyCount"));

  main_dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  on_worker([]() {});
  EXPECT_EQ(routes_before + 1, moduleValue("getRouteDestroyCount"));
  EXPECT_EQ(configs_before + 1, moduleValue("getConfigDestroyCount"));

  worker_dispatcher->exit();
  worker->join();
}

// A module that sets no key builds a route per request as it does without a cache.
TEST_F(DynamicModuleBuiltRouteCacheTest, DecisionWithoutAKeyIsNotCached) {
  const auto config = loadConfig(builtRouteCacheModuleYaml("no-key", builtRouteCacheYaml(16)));
  ASSERT_TRUE(config.ok());
  EXPECT_NE(resolve(*config.value(), 1), resolve(*config.value(), 1));
  EXPECT_EQ(0, counterValue("built_route_cache_miss"));
  EXPECT_EQ(0, entries());
}

// The cache is not in effect without a selected template, or without a configured cache.
TEST_F(DynamicModuleBuiltRouteCacheTest, CacheIsDisabledWithoutATemplateOrAConfiguration) {
  const auto without_template =
      loadConfig(builtRouteCacheModuleYaml("key-without-template", builtRouteCacheYaml(16)));
  ASSERT_TRUE(without_template.ok());
  const auto route = resolve(*without_template.value(), 1);
  EXPECT_EQ(CacheDisabled, lastCacheResult());
  ASSERT_NE(nullptr, route);
  EXPECT_EQ("uncached", route->routeEntry()->clusterName());

  const auto without_cache = loadConfig(builtRouteCacheModuleYaml("cached", ""));
  ASSERT_TRUE(without_cache.ok());
  const auto first = resolve(*without_cache.value(), 1);
  EXPECT_EQ(CacheDisabled, lastCacheResult());
  EXPECT_NE(first, resolve(*without_cache.value(), 1));
  EXPECT_EQ(3, counterValue("built_route_cache_disabled"));
}

// A module reads from its configuration hook whether the configuration caches built routes.
TEST_F(DynamicModuleBuiltRouteCacheTest, ConfigurationReportsWhetherItCachesBuiltRoutes) {
  ASSERT_TRUE(loadConfig(builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(16))).ok());
  EXPECT_EQ(1, moduleValue("getConfigHasBuiltRouteCache"));
  ASSERT_TRUE(loadConfig(builtRouteCacheModuleYaml("cached", "")).ok());
  EXPECT_EQ(0, moduleValue("getConfigHasBuiltRouteCache"));
}

// A module whose routes rely on the cache rejects a configuration without it.
TEST_F(DynamicModuleBuiltRouteCacheTest, ModuleRejectsAConfigurationWithoutTheCache) {
  EXPECT_TRUE(
      loadConfig(builtRouteCacheModuleYaml("requires-cache", builtRouteCacheYaml(16))).ok());
  EXPECT_THAT(loadConfig(builtRouteCacheModuleYaml("requires-cache", "")).status(),
              HasStatusMessage(HasSubstr("Failed to initialize dynamic module")));
}

// The cache is not in effect on a thread that thread local storage does not know, nor on a worker
// that has not set it up yet.
TEST_F(DynamicModuleBuiltRouteCacheTest, ThreadWithoutTheCacheResolvesWithoutIt) {
  context_.thread_local_.defer_data_ = true;
  const auto config = loadConfig(builtRouteCacheModuleYaml("cached", builtRouteCacheYaml(16)));
  ASSERT_TRUE(config.ok());
  resolve(*config.value(), 1);
  EXPECT_EQ(CacheDisabled, lastCacheResult());
  context_.thread_local_.call();
  context_.thread_local_.registered_ = false;
  resolve(*config.value(), 1);
  EXPECT_EQ(CacheDisabled, lastCacheResult());
  context_.thread_local_.registered_ = true;
  resolve(*config.value(), 1);
  EXPECT_EQ(CacheMiss, lastCacheResult());
  EXPECT_EQ(2, counterValue("built_route_cache_disabled"));
}

// Selecting the template again after the lookup discards it, so the route is not cached.
TEST_F(DynamicModuleBuiltRouteCacheTest, SelectingATemplateDiscardsTheLookup) {
  const auto config = loadConfig(builtRouteCacheModuleYaml("reselect", builtRouteCacheYaml(16)));
  ASSERT_TRUE(config.ok());
  resolve(*config.value(), 1);
  resolve(*config.value(), 1);
  EXPECT_EQ(CacheMiss, lastCacheResult());
  EXPECT_EQ(0, entries());
}

// Unsetting the template after the lookup discards it, so the regex rewrite of a route override
// selected for the route matching resolved applies when the decision resolves.
TEST_F(DynamicModuleBuiltRouteCacheTest, UnsettingTheTemplateDiscardsTheLookup) {
  const auto config =
      loadConfig(builtRouteCacheModuleYaml("unset-after-key", builtRouteCacheYaml(16)));
  ASSERT_TRUE(config.ok());
  const auto route = resolve(*config.value(), 1);
  ASSERT_NE(nullptr, route);
  EXPECT_THAT(finalize(*route->routeEntry(), requestHeaders("/api/v2")),
              HasPaths("/regex/api/v1", "/api/v2"));
  EXPECT_EQ(0, entries());
}

// A later key replaces the lookup, so the route is cached for the key of the last call.
TEST_F(DynamicModuleBuiltRouteCacheTest, LaterKeyReplacesTheLookup) {
  const auto config = loadConfig(builtRouteCacheModuleYaml("rekey", builtRouteCacheYaml(16)));
  ASSERT_TRUE(config.ok());
  const auto first = resolve(*config.value(), 1);
  EXPECT_EQ(CacheMiss, lastCacheResult());
  EXPECT_EQ(1, entries());
  EXPECT_EQ(first, resolve(*config.value(), 1));
  EXPECT_EQ(CacheHit, lastCacheResult());
  EXPECT_EQ(3, counterValue("built_route_cache_miss"));
}

// A recording that depends on the path of the current request is not cached. It does after
// set_prefix_rewrite, even once set_path replaces the path, after a rule replaced an accepted one,
// and after a rule was rejected for the path, while a rule that applies is cached and rewrites
// later requests.
TEST_F(DynamicModuleBuiltRouteCacheTest, RecordingThatDependsOnTheRequestPathIsNotCached) {
  for (const std::string mode : {"prefix-rewrite", "prefix-rewrite-then-path", "replaced-rule"}) {
    SCOPED_TRACE(mode);
    const auto config = loadConfig(builtRouteCacheModuleYaml(mode, builtRouteCacheYaml(16)));
    ASSERT_TRUE(config.ok());
    resolve(*config.value(), 1);
    resolve(*config.value(), 1);
    EXPECT_EQ(CacheMiss, lastCacheResult());
    EXPECT_EQ(0, entries());
  }

  const auto rule = loadConfig(builtRouteCacheModuleYaml("rule", builtRouteCacheYaml(16)));
  ASSERT_TRUE(rule.ok());
  const auto rejected = resolve(*rule.value(), 1, 1, "/other");
  ASSERT_NE(nullptr, rejected);
  EXPECT_EQ("fallback_cluster", rejected->routeEntry()->clusterName());
  EXPECT_EQ(0, entries());
  const auto cached = resolve(*rule.value(), 1, 1, "/api/v1");
  EXPECT_EQ(CacheMiss, lastCacheResult());
  EXPECT_EQ(1, entries());
  ASSERT_NE(nullptr, cached);
  EXPECT_EQ(cached, resolve(*rule.value(), 1, 1, "/api/v2"));
  EXPECT_EQ(CacheHit, lastCacheResult());
  EXPECT_THAT(finalize(*cached->routeEntry(), requestHeaders("/api/v2")),
              HasPaths("/internal/v2", "/api/v2"));
}

// A cached route that carries a rule serves only the paths the rule applies to, so a request for
// another path is a miss the module decides afresh, whose route does not replace the cached one.
TEST_F(DynamicModuleBuiltRouteCacheTest, CachedRuleServesOnlyThePathsItAppliesTo) {
  for (const std::string mode : {"rule", "rule-for-api"}) {
    SCOPED_TRACE(mode);
    const auto config = loadConfig(builtRouteCacheModuleYaml(mode, builtRouteCacheYaml(16)));
    ASSERT_TRUE(config.ok());
    const auto cached = resolve(*config.value(), 1, 1, "/api/v1");
    const auto other = resolve(*config.value(), 1, 1, "/other");
    EXPECT_EQ(CacheMiss, lastCacheResult());
    ASSERT_NE(nullptr, other);
    EXPECT_EQ("fallback_cluster", other->routeEntry()->clusterName());
    EXPECT_EQ(1, entries());
    EXPECT_EQ(cached, resolve(*config.value(), 1, 1, "/api/v2"));
    EXPECT_EQ(CacheHit, lastCacheResult());
  }
}

// For a decision the cache looks up, the regex rewrite of a selected route override applies when
// the request headers are finalized, so the cached route rewrites the path of each request.
TEST_F(DynamicModuleBuiltRouteCacheTest, CachedRouteOverrideRegexRewritesEachRequest) {
  const auto config =
      loadConfig(builtRouteCacheModuleYaml("override-regex", builtRouteCacheYaml(16)));
  ASSERT_TRUE(config.ok());
  const auto cached = resolve(*config.value(), 1);
  ASSERT_NE(nullptr, cached);
  EXPECT_EQ(cached, resolve(*config.value(), 1, 1, "/api/v2"));
  EXPECT_EQ(CacheHit, lastCacheResult());
  EXPECT_THAT(finalize(*cached->routeEntry(), requestHeaders("/api/v2")),
              HasPaths("/regex/api/v2", "/api/v2"));
}

// A specifier on a route configuration cannot build templates, so it rejects a built route cache.
TEST_F(DynamicModuleBuiltRouteCacheTest, BuiltRouteCacheRequiresARouteBuilder) {
  const auto config = loadRaw(fmt::format(R"EOF(
name: test_route_config
virtual_hosts:
- name: test_vhost
  domains: ["*"]
  routes:
  - match: {{prefix: "/"}}
    route: {{cluster: matched_cluster}}
route_specifiers:
- name: envoy.router.route_specifiers.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier
    dynamic_module_config:
      name: route_specifier_built_route_cache
      do_not_close: true
    specifier_name: test_route_specifier
    stat_prefix: test
    failure_policy: PASS_THROUGH
    built_route_cache:
      max_entries_per_worker: 16
)EOF"));
  EXPECT_THAT(config.status(),
              HasStatusMessage(HasSubstr("built_route_cache requires a route specifier configured "
                                         "on a virtual host or a route")));
}

// A decision the module records after a hit, or a status that skips the route, wins over the hit.
TEST_F(DynamicModuleBuiltRouteCacheTest, DecisionsAndSkipWinOverAHit) {
  struct TestCase {
    std::string mode;
    std::string counter;
    uint64_t increment;
    std::string cluster;
  };
  for (const TestCase& test_case : std::vector<TestCase>{
           {"hit-then-no-route", "decision_no_route", 1, ""},
           {"hit-then-pass-through", "decision_pass_through", 1, "matched_cluster"},
           {"hit-then-error", "decision_error", 1, "matched_cluster"},
           // Skipping the only route runs the chain once more with no route left.
           {"hit-then-skip", "route_skipped", 2, ""},
       }) {
    SCOPED_TRACE(test_case.mode);
    const auto config =
        loadConfig(builtRouteCacheModuleYaml(test_case.mode, builtRouteCacheYaml(16)));
    ASSERT_TRUE(config.ok());
    const auto cached = resolve(*config.value(), 1);
    ASSERT_NE(nullptr, cached);
    EXPECT_EQ("cached_cluster", cached->routeEntry()->clusterName());
    const uint64_t before = counterValue(test_case.counter);
    const auto route = resolve(*config.value(), 1);
    EXPECT_EQ(CacheHit, lastCacheResult());
    EXPECT_EQ(before + test_case.increment, counterValue(test_case.counter));
    if (test_case.cluster.empty()) {
      EXPECT_EQ(nullptr, route);
    } else {
      ASSERT_NE(nullptr, route);
      EXPECT_EQ(test_case.cluster, route->routeEntry()->clusterName());
    }
  }
}

// A verified hit whose recording differs in any one field evicts the cached route.
class VerifiedHitFieldTest : public DynamicModuleBuiltRouteCacheTest,
                             public testing::WithParamInterface<std::string> {};

INSTANTIATE_TEST_SUITE_P(Fields, VerifiedHitFieldTest,
                         testing::Values("cluster", "timeout", "idle-timeout",
                                         "max-stream-duration", "body-buffer-limit", "priority",
                                         "cluster-not-found", "override", "metadata",
                                         "filter-disabled", "route-name", "virtual-cluster", "path",
                                         "rule", "host", "request-header-add",
                                         "request-header-remove", "response-header-add",
                                         "response-header-remove", "user-data"));

TEST_P(VerifiedHitFieldTest, DifferenceEvictsTheRoute) {
  const auto config = loadConfig(builtRouteCacheModuleYaml(
      "vary", absl::StrCat(builtRouteCacheYaml(16, "        verify_one_in: 1\n"),
                           R"EOF(      virtual_cluster_override:
        virtual_host_name: test_vhost
        names: [vcluster_1, vcluster_2]
)EOF")));
  ASSERT_TRUE(config.ok());
  const auto request = [&](absl::string_view value) {
    auto headers = requestHeaders("/api/v1");
    headers.addCopy(Http::LowerCaseString("x-vary"), GetParam());
    headers.addCopy(Http::LowerCaseString("x-value"), value);
    return config.value()->route(headers, stream_info_, 0).route;
  };
  const auto first = request("1");
  EXPECT_EQ(CacheMiss, lastCacheResult());
  const auto second = request("2");
  EXPECT_EQ(CacheHitVerify, lastCacheResult());
  ASSERT_NE(nullptr, second);
  EXPECT_NE(first, second);
  EXPECT_EQ(1, counterValue("built_route_cache_verify_mismatch"));
  EXPECT_EQ(0, entries());
}

// The user data module with the templates its outcomes select and a built route cache that
// verifies a hit with a chance of one in verify_one_in. Its default of zero verifies none.
std::string userDataModuleYaml(uint32_t verify_one_in = 0) {
  const std::string cache =
      builtRouteCacheYaml(16, fmt::format("        verify_one_in: {}\n", verify_one_in));
  return specifierYaml("route_specifier_user_data",
                       fmt::format(R"EOF(      failure_policy: PASS_THROUGH
{}      route_templates:
      - template_id: only
        route:
          match: {{prefix: "/"}}
          route: {{cluster: module_cluster}}
      - template_id: never
        route:
          match: {{prefix: "/never"}}
          route: {{cluster: module_cluster}}
      - template_id: direct
        route:
          match: {{prefix: "/"}}
          direct_response: {{status: 204}}
)EOF",
                                   cache));
}

class DynamicModuleUserDataTest : public DynamicModuleRouteSpecifierTest {
public:
  void SetUp() override {
    auto module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("route_specifier_user_data", "c"),
        /*do_not_close=*/true);
    ASSERT_TRUE(module.ok());
    module_ = std::move(module.value());
    discards_before_ = moduleValue("getUserDataDiscardCount");
    destroys_before_ = moduleValue("getRouteDestroyCount");
  }

  int moduleValue(const std::string& getter) {
    auto function = module_->getFunctionPointer<int (*)(void)>(getter);
    EXPECT_TRUE(function.ok());
    return function.ok() ? function.value()() : -1;
  }
  // The values the module got back through each hook since the test started.
  int discards() { return moduleValue("getUserDataDiscardCount") - discards_before_; }
  int destroys() { return moduleValue("getRouteDestroyCount") - destroys_before_; }
  uint64_t lastDiscarded() {
    auto function = module_->getFunctionPointer<uint64_t (*)(void)>("getLastDiscardedUserData");
    EXPECT_TRUE(function.ok());
    return function.ok() ? function.value()() : 0;
  }

  // Resolves the route of a request for which the module records the user data, if any, and then
  // ends the decision the way the outcome names.
  Envoy::Router::RouteConstSharedPtr resolve(const Envoy::Router::ConfigImpl& config,
                                             absl::string_view outcome,
                                             std::optional<uint64_t> user_data,
                                             absl::string_view path = "/") {
    auto headers = requestHeaders(path);
    headers.addCopy(Http::LowerCaseString("x-outcome"), outcome);
    if (user_data.has_value()) {
      headers.addCopy(Http::LowerCaseString("x-user-data"), absl::StrCat(*user_data));
    }
    return config.route(headers, stream_info_, 0).route;
  }

  Extensions::DynamicModules::DynamicModulePtr module_;
  int discards_before_{0};
  int destroys_before_{0};
};

// A route built with the user data carries it, so only the route destroy hook receives it, once the
// route is released.
TEST_F(DynamicModuleUserDataTest, RouteThatCarriesTheUserDataFiresTheDestroyHook) {
  const auto config = loadConfig(userDataModuleYaml());
  ASSERT_TRUE(config.ok());
  int destroyed = 0;
  for (const absl::string_view outcome : {"template", "refine"}) {
    auto route = resolve(*config.value(), outcome, 7);
    ASSERT_NE(nullptr, route);
    EXPECT_EQ(destroyed, destroys());
    route.reset();
    EXPECT_EQ(++destroyed, destroys());
  }
  EXPECT_EQ(0, discards());
  EXPECT_EQ(0, counterValue("user_data_discard"));
}

// A decision that builds no route for the user data hands the value back through the user data
// discard hook. The counter names the path the decision took.
class UserDataDiscardTest
    : public DynamicModuleUserDataTest,
      public testing::WithParamInterface<std::pair<std::string, std::string>> {};

INSTANTIATE_TEST_SUITE_P(
    Outcomes, UserDataDiscardTest,
    testing::Values(std::make_pair("pass-through", "decision_pass_through"),
                    std::make_pair("no-route", "decision_no_route"),
                    std::make_pair("error", "decision_error"),
                    std::make_pair("reuse-previous", "reuse_previous_rejected"),
                    std::make_pair("unknown-decision", "decision_error"),
                    std::make_pair("unknown-status", "unknown_status"),
                    std::make_pair("missing-template", "failure_template_not_selected"),
                    std::make_pair("unmatched-template", "failure_template_match_failed"),
                    std::make_pair("direct-entry", "failure_override_on_non_route_entry")));

TEST_P(UserDataDiscardTest, HandsTheUserDataBack) {
  const auto config = loadConfig(userDataModuleYaml());
  ASSERT_TRUE(config.ok());
  resolve(*config.value(), GetParam().first, 42);
  EXPECT_EQ(1, counterValue(GetParam().second));
  EXPECT_EQ(1, discards());
  EXPECT_EQ(42, lastDiscarded());
  EXPECT_EQ(1, counterValue("user_data_discard"));
  EXPECT_EQ(0, destroys());
}

// A skipped route builds nothing for the user data, while matching carries on with the next route.
TEST_F(DynamicModuleUserDataTest, SkippedRouteHandsTheUserDataBack) {
  const auto config = loadRaw(catchAllRouteConfigYaml("route_specifier_user_data", "PASS_THROUGH"));
  ASSERT_TRUE(config.ok());
  const auto route = resolve(*config.value(), "skip", 42, "/specific");
  ASSERT_NE(nullptr, route);
  EXPECT_EQ("specific_cluster", route->routeEntry()->clusterName());
  EXPECT_EQ(1, counterValue("route_skipped"));
  EXPECT_EQ(1, discards());
  EXPECT_EQ(42, lastDiscarded());
}

// Reusing the previous route builds nothing for the user data recorded with the decision.
TEST_F(DynamicModuleUserDataTest, ReusedPreviousRouteHandsTheUserDataBack) {
  const auto config = loadConfig(userDataModuleYaml());
  ASSERT_TRUE(config.ok());
  stream_info_.route_ = resolve(*config.value(), "template", 1);
  EXPECT_EQ(stream_info_.route_, resolve(*config.value(), "reuse-previous", 42));
  EXPECT_EQ(1, counterValue("decision_reuse_previous"));
  EXPECT_EQ(1, discards());
  EXPECT_EQ(42, lastDiscarded());
}

// Metadata a typed metadata factory rejects fails the decision after the module returned, so no
// route carries the user data.
TEST_F(DynamicModuleUserDataTest, RejectedRouteMetadataHandsTheUserDataBack) {
  RejectingRouteMetadataFactory factory;
  Registry::InjectFactory<Envoy::Router::HttpRouteTypedMetadataFactory> injection(factory);
  const auto config = loadConfig(userDataModuleYaml());
  ASSERT_TRUE(config.ok());
  resolve(*config.value(), "rejected-metadata", 42);
  EXPECT_EQ(1, counterValue("failure_route_metadata"));
  EXPECT_EQ(1, discards());
  EXPECT_EQ(42, lastDiscarded());
}

// The user data needs a route to be recorded on, so it is handed back when no route matched.
TEST_F(DynamicModuleUserDataTest, UserDataWithoutARouteIsHandedBack) {
  const auto config = loadRaw(R"EOF(
name: test_route_config
virtual_hosts:
- name: test_vhost
  domains: ["*"]
  routes:
  - match: {prefix: "/api"}
    route: {cluster: matched_cluster}
  route_specifiers:
  - name: envoy.router.route_specifiers.dynamic_modules
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier
      dynamic_module_config:
        name: route_specifier_user_data
        do_not_close: true
      specifier_name: test_route_specifier
      stat_prefix: test
      failure_policy: PASS_THROUGH
)EOF");
  ASSERT_TRUE(config.ok());
  EXPECT_EQ(nullptr, resolve(*config.value(), "refine", 42, "/other"));
  EXPECT_EQ(1, counterValue("failure_override_without_route"));
  EXPECT_EQ(1, discards());
  EXPECT_EQ(42, lastDiscarded());
}

// A hit serves the route a miss built, so the user data the hit records is handed back while the
// cached route keeps the value of the miss.
TEST_F(DynamicModuleUserDataTest, HitHandsTheUserDataBack) {
  const auto config = loadConfig(userDataModuleYaml());
  ASSERT_TRUE(config.ok());
  const auto first = resolve(*config.value(), "cached", 1);
  EXPECT_EQ(0, discards());
  EXPECT_EQ(first, resolve(*config.value(), "cached", 2));
  EXPECT_EQ(1, counterValue("built_route_cache_hit"));
  EXPECT_EQ(1, discards());
  EXPECT_EQ(2, lastDiscarded());
  EXPECT_EQ(0, destroys());
}

// A later value replaces the one the module recorded before, so only the last one is handed back.
TEST_F(DynamicModuleUserDataTest, LastRecordedUserDataIsHandedBack) {
  const auto config = loadConfig(userDataModuleYaml());
  ASSERT_TRUE(config.ok());
  resolve(*config.value(), "recorded-twice", 42);
  EXPECT_EQ(1, discards());
  EXPECT_EQ(43, lastDiscarded());
}

// A verified hit uses the route its recording builds, which carries the user data, so nothing is
// handed back whether or not the recording matches the cached route.
TEST_F(DynamicModuleUserDataTest, VerifiedHitKeepsTheUserDataOnTheRouteItBuilds) {
  const auto config = loadConfig(userDataModuleYaml(1));
  ASSERT_TRUE(config.ok());
  const auto cached = resolve(*config.value(), "cached", 1);
  auto same = resolve(*config.value(), "cached", 1);
  ASSERT_NE(nullptr, same);
  EXPECT_NE(cached, same);
  EXPECT_EQ(0, counterValue("built_route_cache_verify_mismatch"));
  same.reset();
  EXPECT_EQ(1, destroys());

  auto different = resolve(*config.value(), "cached", 2);
  ASSERT_NE(nullptr, different);
  EXPECT_EQ(1, counterValue("built_route_cache_verify_mismatch"));
  different.reset();
  EXPECT_EQ(2, destroys());
  EXPECT_EQ(2, counterValue("built_route_cache_verify"));
  EXPECT_EQ(0, discards());
  EXPECT_EQ(0, counterValue("user_data_discard"));
}

// The specifier reads the clock before and after the module, and once more only for a decision that
// builds a route, fails or hands its user data back, or whose lookup in the built route cache
// misses or is verified.
TEST_F(DynamicModuleUserDataTest, SpecifierDurationEndsWithTheModuleUnlessTheDecisionDoesMore) {
  NiceMock<MockTimeSystem> time_system;
  ON_CALL(context_, timeSource()).WillByDefault(testing::ReturnRef(time_system));
  const auto config = loadConfig(userDataModuleYaml());
  ASSERT_TRUE(config.ok());
  const auto verified = loadConfig(userDataModuleYaml(1));
  ASSERT_TRUE(verified.ok());
  const auto histogram = [](absl::string_view name) {
    return testing::Property(&Stats::Metric::name,
                             absl::StrCat("dynamicmodulescustom.route_specifier.test.", name));
  };
  // The module takes 2 microseconds, and a decision that does more after it takes 5 more.
  const auto expect_durations = [&](const Envoy::Router::ConfigImpl& route_config,
                                    absl::string_view outcome, std::optional<uint64_t> user_data,
                                    bool does_more) {
    SCOPED_TRACE(absl::StrCat(outcome, user_data.has_value() ? " with user data" : ""));
    auto& reads = EXPECT_CALL(time_system, monotonicTime())
                      .WillOnce(testing::Return(MonotonicTime(std::chrono::microseconds(10))))
                      .WillOnce(testing::Return(MonotonicTime(std::chrono::microseconds(12))));
    // The last read follows everything the specifier does, the discard hook included.
    int discards_at_end = discards();
    if (does_more) {
      reads.WillOnce([&]() {
        discards_at_end = discards();
        return MonotonicTime(std::chrono::microseconds(17));
      });
    }
    EXPECT_CALL(context_.store_, deliverHistogramToSinks(histogram("on_route_duration"), 2));
    EXPECT_CALL(context_.store_,
                deliverHistogramToSinks(histogram("specifier_duration"), does_more ? 7 : 2));
    resolve(route_config, outcome, user_data);
    EXPECT_EQ(discards(), discards_at_end);
    testing::Mock::VerifyAndClearExpectations(&time_system);
    testing::Mock::VerifyAndClearExpectations(&context_.store_);
  };
  expect_durations(*config.value(), "pass-through", std::nullopt, false);
  expect_durations(*config.value(), "template", std::nullopt, false);
  expect_durations(*config.value(), "template", 1, true);
  expect_durations(*config.value(), "pass-through", 1, true);
  expect_durations(*config.value(), "error", std::nullopt, true);
  // The miss fills the cache and the hit serves what it cached.
  expect_durations(*config.value(), "cached", std::nullopt, true);
  expect_durations(*config.value(), "cached", std::nullopt, false);
  expect_durations(*verified.value(), "cached", std::nullopt, true);
  expect_durations(*verified.value(), "cached", std::nullopt, true);
}

// A request outside runtime_fraction never reaches the module, so the specifier reads no clock and
// records no duration for it, while a request inside it is timed.
TEST_F(DynamicModuleUserDataTest, OnlyARequestInsideTheRuntimeFractionIsTimed) {
  NiceMock<MockTimeSystem> time_system;
  ON_CALL(context_, timeSource()).WillByDefault(testing::ReturnRef(time_system));
  const auto config =
      loadConfig(specifierYaml("route_specifier_user_data", R"EOF(      failure_policy: PASS_THROUGH
      runtime_fraction:
        default_value: {numerator: 50}
        runtime_key: test.fraction
)EOF"));
  ASSERT_TRUE(config.ok());
  const auto expect_fraction = [this](bool inside) {
    EXPECT_CALL(
        context_.runtime_loader_.snapshot_,
        featureEnabled("test.fraction", testing::A<const envoy::type::v3::FractionalPercent&>(), 0))
        .WillOnce(testing::Return(inside));
  };

  expect_fraction(false);
  EXPECT_CALL(time_system, monotonicTime()).Times(0);
  EXPECT_CALL(context_.store_, deliverHistogramToSinks(_, _)).Times(0);
  resolve(*config.value(), "pass-through", std::nullopt);
  EXPECT_EQ(1, counterValue("runtime_skipped"));
  EXPECT_EQ(0, counterValue("decision_pass_through"));
  testing::Mock::VerifyAndClearExpectations(&time_system);
  testing::Mock::VerifyAndClearExpectations(&context_.store_);

  expect_fraction(true);
  EXPECT_CALL(time_system, monotonicTime()).Times(2);
  EXPECT_CALL(context_.store_, deliverHistogramToSinks(_, _)).Times(2);
  resolve(*config.value(), "pass-through", std::nullopt);
  EXPECT_EQ(1, counterValue("runtime_skipped"));
  EXPECT_EQ(1, counterValue("decision_pass_through"));
}

// The virtual clusters the virtual host of virtualClusterConfigYaml() matches requests against.
constexpr absl::string_view RidesAndTripsVirtualClusters = R"EOF(  virtual_clusters:
  - name: rides
    headers:
    - name: ":path"
      string_match: {prefix: "/rides"}
  - name: trips
    headers:
    - name: ":path"
      string_match: {prefix: "/trips"}
)EOF";

constexpr absl::string_view PreferSelection = "        prefer_selection: true\n";

// A virtual_cluster_override block that declares rides, trips, bikes and the catch all virtual
// cluster for the virtual host, followed by the given fields.
std::string virtualClusterOverrideYaml(absl::string_view fields = "",
                                       absl::string_view virtual_host_name = "test_vhost") {
  return fmt::format(R"EOF(      virtual_cluster_override:
        virtual_host_name: {}
        names: [rides, trips, bikes, other]
{})EOF",
                     virtual_host_name, fields);
}

// A route configuration whose virtual host has the given virtual clusters and a route for the
// prefix, and runs the virtual cluster module with the given specifier fields.
std::string
virtualClusterConfigYaml(absl::string_view specifier_fields,
                         absl::string_view virtual_clusters = RidesAndTripsVirtualClusters,
                         absl::string_view prefix = "/") {
  return fmt::format(R"EOF(
name: test_route_config
virtual_hosts:
- name: test_vhost
  domains: ["*"]
  routes:
  - match: {{prefix: "{}"}}
    route: {{cluster: matched_cluster}}
{}  route_specifiers:
  - name: envoy.router.route_specifiers.dynamic_modules
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier
      dynamic_module_config:
        name: route_specifier_virtual_cluster
        do_not_close: true
      specifier_name: test_route_specifier
      stat_prefix: test
      failure_policy: PASS_THROUGH
      route_templates:
      - template_id: direct
        route:
          match: {{prefix: "/"}}
          direct_response: {{status: 204}}
      - template_id: rerouted
        route:
          match: {{prefix: "/"}}
          route: {{cluster: rerouted_cluster}}
{})EOF",
                     prefix, virtual_clusters, specifier_fields);
}

class DynamicModuleVirtualClusterTest : public DynamicModuleRouteSpecifierTest {
public:
  void SetUp() override {
    auto module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("route_specifier_virtual_cluster", "c"),
        /*do_not_close=*/true);
    ASSERT_TRUE(module.ok());
    module_ = std::move(module.value());
    setComparisonRandomValue(1);
  }

  // Sets the random value that decides whether a preferred selection is compared with the virtual
  // host, which happens when the value is a multiple of verify_one_in.
  void setComparisonRandomValue(uint64_t value) {
    ON_CALL(context_.api_.random_, random()).WillByDefault(testing::Return(value));
  }

  // Reads one of the values the module exports for tests.
  int moduleValue(const std::string& getter) {
    auto function = module_->getFunctionPointer<int (*)(void)>(getter);
    EXPECT_TRUE(function.ok());
    return function.ok() ? function.value()() : -2;
  }
  int lastVirtualClusterResult() { return moduleValue("getLastVirtualClusterResult"); }

  // Resolves the route of a request for the path that carries the given headers.
  Envoy::Router::RouteConstSharedPtr
  resolve(const Envoy::Router::ConfigImpl& config, absl::string_view path,
          const std::vector<std::pair<std::string, std::string>>& extra_headers = {}) {
    auto headers = requestHeaders(path);
    for (const auto& [key, value] : extra_headers) {
      headers.addCopy(Http::LowerCaseString(key), value);
    }
    return config.route(headers, stream_info_, 0).route;
  }

  // The virtual cluster the router counts a request for the path in when it uses the route. Like
  // the lookup of the router, it counts the selection statistics and may draw a comparison sample.
  static const Envoy::Router::VirtualCluster* virtualClusterOf(const Envoy::Router::Route& route,
                                                               absl::string_view path) {
    return route.virtualHost().virtualCluster(requestHeaders(path));
  }

  // The name of that virtual cluster, unset for the catch all virtual cluster and for none. It
  // counts the selection statistics like virtualClusterOf.
  static std::optional<std::string> countedIn(const Envoy::Router::Route& route,
                                              absl::string_view path) {
    const Envoy::Router::VirtualCluster* cluster = virtualClusterOf(route, path);
    return cluster != nullptr ? cluster->name() : std::nullopt;
  }

  Extensions::DynamicModules::DynamicModulePtr module_;
};

// A preferred selection is the virtual cluster the router counts the request in. It reports the
// declared name and uses the statistics of the virtual cluster of that name on the virtual host,
// while the route keeps the virtual host it wraps for everything else.
TEST_F(DynamicModuleVirtualClusterTest, PreferredSelectionIsTheVirtualClusterOfTheRequest) {
  const auto config =
      loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection)));
  ASSERT_TRUE(config.ok());
  const auto route = resolve(*config.value(), "/trips", {{"x-virtual-cluster", "rides"}});
  ASSERT_NE(nullptr, route);
  EXPECT_EQ(1, lastVirtualClusterResult());
  const Envoy::Router::VirtualCluster* selected = virtualClusterOf(*route, "/trips");
  ASSERT_NE(nullptr, selected);
  EXPECT_EQ("rides", selected->name());
  EXPECT_EQ(1, counterValue("virtual_cluster_selected"));
  EXPECT_EQ(0, counterValue("virtual_cluster_agree") + counterValue("virtual_cluster_disagree"));

  const auto unselected = resolve(*config.value(), "/rides");
  ASSERT_NE(nullptr, unselected);
  const Envoy::Router::VirtualCluster* matched = virtualClusterOf(*unselected, "/rides");
  ASSERT_NE(nullptr, matched);
  EXPECT_NE(selected, matched);
  EXPECT_EQ(selected->statName(), matched->statName());
  EXPECT_EQ(&selected->stats().upstream_rq_total_, &matched->stats().upstream_rq_total_);

  EXPECT_NE(route->virtualHostSharedPtr().get(), &route->virtualHost());
  EXPECT_EQ(&unselected->virtualHost(), route->virtualHostSharedPtr().get());
  EXPECT_EQ("test_vhost", route->virtualHost().name());
}

// A declared virtual cluster that the virtual host does not configure names and tags its statistics
// the way the virtual host names and tags those of its own virtual clusters, from the moment the
// configuration loads. Only the virtual host creates rides and only the declaration creates bikes,
// so the expectations for bikes follow the virtual host.
TEST_F(DynamicModuleVirtualClusterTest, DeclaredVirtualClusterStatisticsMatchTheVirtualHost) {
  const auto config = loadRaw(virtualClusterConfigYaml(R"EOF(      virtual_cluster_override:
        virtual_host_name: test_vhost
        names: [bikes]
        prefer_selection: true
)EOF"));
  ASSERT_TRUE(config.ok());
  EXPECT_NE(nullptr, TestUtility::findCounter(context_.store_,
                                              "vhost.test_vhost.vcluster.bikes.upstream_rq_total"));
  const auto bikes_route = resolve(*config.value(), "/rides", {{"x-virtual-cluster", "bikes"}});
  const auto rides_route = resolve(*config.value(), "/rides");
  ASSERT_NE(nullptr, bikes_route);
  ASSERT_NE(nullptr, rides_route);
  const Envoy::Router::VirtualCluster* bikes = virtualClusterOf(*bikes_route, "/rides");
  const Envoy::Router::VirtualCluster* rides = virtualClusterOf(*rides_route, "/rides");
  ASSERT_NE(nullptr, bikes);
  ASSERT_NE(nullptr, rides);
  const Stats::Counter& bikes_total = bikes->stats().upstream_rq_total_;
  const Stats::Counter& rides_total = rides->stats().upstream_rq_total_;
  EXPECT_EQ("vhost.test_vhost.vcluster.rides.upstream_rq_total", rides_total.name());
  EXPECT_THAT(rides_total.tags(),
              testing::ElementsAre(Stats::Tag{"envoy.virtual_host", "test_vhost"},
                                   Stats::Tag{"envoy.virtual_cluster", "rides"}));
  const auto as_bikes = [](absl::string_view value) {
    return absl::StrReplaceAll(value, {{"rides", "bikes"}});
  };
  EXPECT_EQ(as_bikes(rides_total.name()), bikes_total.name());
  EXPECT_EQ(rides_total.tagExtractedName(), bikes_total.tagExtractedName());
  Stats::TagVector bikes_tags = rides_total.tags();
  for (Stats::Tag& tag : bikes_tags) {
    tag.value_ = as_bikes(tag.value_);
  }
  EXPECT_EQ(bikes_tags, bikes_total.tags());
}

// A preferred selection of none leaves the request to the virtual cluster the virtual host matches,
// its catch all virtual cluster included.
TEST_F(DynamicModuleVirtualClusterTest, PreferredSelectionOfNoneLeavesTheRequestToTheVirtualHost) {
  const auto config =
      loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection)));
  ASSERT_TRUE(config.ok());
  const auto route = resolve(*config.value(), "/rides", {{"x-virtual-cluster", ""}});
  ASSERT_NE(nullptr, route);
  EXPECT_EQ(1, lastVirtualClusterResult());
  EXPECT_EQ("rides", countedIn(*route, "/rides"));
  const Envoy::Router::VirtualCluster* catch_all = virtualClusterOf(*route, "/other");
  ASSERT_NE(nullptr, catch_all);
  EXPECT_EQ(std::nullopt, catch_all->name());
  EXPECT_EQ(2, counterValue("virtual_cluster_none"));
  EXPECT_EQ(0, counterValue("virtual_cluster_selected"));
}

// Without prefer_selection the router counts the request in the virtual cluster the virtual host
// matches and compares the selection with it, where other is the catch all virtual cluster, while a
// selection of none is left to the virtual host without a comparison.
TEST_F(DynamicModuleVirtualClusterTest, ComparedSelectionLeavesTheRequestToTheVirtualHost) {
  const auto config = loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml()));
  ASSERT_TRUE(config.ok());
  struct TestCase {
    std::string path;
    std::string selection;
    std::optional<std::string> counted;
    bool agrees;
  };
  for (const TestCase& test_case : std::vector<TestCase>{
           {"/rides", "rides", "rides", true},
           {"/rides", "trips", "rides", false},
           {"/rides", "other", "rides", false},
           {"/unmatched", "other", std::nullopt, true},
           {"/unmatched", "bikes", std::nullopt, false},
       }) {
    SCOPED_TRACE(absl::StrCat(test_case.path, " selecting '", test_case.selection, "'"));
    const uint64_t agree = counterValue("virtual_cluster_agree");
    const uint64_t disagree = counterValue("virtual_cluster_disagree");
    const auto route =
        resolve(*config.value(), test_case.path, {{"x-virtual-cluster", test_case.selection}});
    ASSERT_NE(nullptr, route);
    const Envoy::Router::VirtualCluster* counted = virtualClusterOf(*route, test_case.path);
    ASSERT_NE(nullptr, counted);
    EXPECT_EQ(test_case.counted, counted->name());
    EXPECT_EQ(virtualClusterOf(*resolve(*config.value(), test_case.path), test_case.path), counted);
    EXPECT_EQ(agree + (test_case.agrees ? 1 : 0), counterValue("virtual_cluster_agree"));
    EXPECT_EQ(disagree + (test_case.agrees ? 0 : 1), counterValue("virtual_cluster_disagree"));
  }
  EXPECT_EQ(0, counterValue("virtual_cluster_selected") + counterValue("virtual_cluster_none"));

  const auto none = resolve(*config.value(), "/rides", {{"x-virtual-cluster", ""}});
  ASSERT_NE(nullptr, none);
  EXPECT_EQ("rides", countedIn(*none, "/rides"));
  EXPECT_EQ(1, counterValue("virtual_cluster_none"));
  EXPECT_EQ(5, counterValue("virtual_cluster_agree") + counterValue("virtual_cluster_disagree"));
}

// Selecting other selects the catch all virtual cluster, which has no name and uses the statistics
// of the catch all virtual cluster of the virtual host.
TEST_F(DynamicModuleVirtualClusterTest, OtherSelectsTheCatchAllVirtualCluster) {
  const auto config =
      loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection)));
  ASSERT_TRUE(config.ok());
  const auto route = resolve(*config.value(), "/trips", {{"x-virtual-cluster", "other"}});
  ASSERT_NE(nullptr, route);
  EXPECT_EQ(1, lastVirtualClusterResult());
  const Envoy::Router::VirtualCluster* selected = virtualClusterOf(*route, "/trips");
  ASSERT_NE(nullptr, selected);
  EXPECT_EQ(std::nullopt, selected->name());
  const auto unselected = resolve(*config.value(), "/unmatched");
  ASSERT_NE(nullptr, unselected);
  const Envoy::Router::VirtualCluster* catch_all = virtualClusterOf(*unselected, "/unmatched");
  ASSERT_NE(nullptr, catch_all);
  EXPECT_NE(selected, catch_all);
  EXPECT_EQ(catch_all->statName(), selected->statName());
  EXPECT_EQ(&catch_all->stats().upstream_rq_total_, &selected->stats().upstream_rq_total_);
  EXPECT_EQ(1, counterValue("virtual_cluster_selected"));
}

// A selection agrees with the virtual cluster the virtual host matches when both count the request
// in the same statistics, so the catch all agrees with a virtual cluster the virtual host names
// other.
TEST_F(DynamicModuleVirtualClusterTest, SelectionAgreesWhenTheStatisticsAreTheSame) {
  const auto config = loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(),
                                                       R"EOF(  virtual_clusters:
  - name: other
    headers:
    - name: ":path"
      string_match: {prefix: "/named"}
)EOF"));
  ASSERT_TRUE(config.ok());
  const auto route = resolve(*config.value(), "/named", {{"x-virtual-cluster", "other"}});
  ASSERT_NE(nullptr, route);
  EXPECT_EQ("other", countedIn(*route, "/named"));
  EXPECT_EQ(1, counterValue("virtual_cluster_agree"));
  EXPECT_EQ(0, counterValue("virtual_cluster_disagree"));
}

// A disagreement is logged at debug level with the method and path of the request, naming the
// catch all virtual cluster other.
TEST_F(DynamicModuleVirtualClusterTest, DisagreementIsLoggedWithTheRequest) {
  const auto config = loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml()));
  ASSERT_TRUE(config.ok());
  const auto route = resolve(*config.value(), "/unmatched", {{"x-virtual-cluster", "bikes"}});
  ASSERT_NE(nullptr, route);
  EXPECT_LOG_CONTAINS("debug",
                      "dynamic module route specifier 'test' selected virtual cluster 'bikes' for "
                      "GET '/unmatched' while virtual host 'test_vhost' matched 'other'",
                      virtualClusterOf(*route, "/unmatched"));
  EXPECT_EQ(1, counterValue("virtual_cluster_disagree"));
}

// A virtual host without virtual clusters counts a request in none, which leaves nothing to compare
// a selection with, so the router counts the request in the selection instead, in either mode.
TEST_F(DynamicModuleVirtualClusterTest, SelectionOnAVirtualHostWithoutVirtualClusters) {
  const auto config = loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(), ""));
  ASSERT_TRUE(config.ok());
  const auto rides = resolve(*config.value(), "/rides", {{"x-virtual-cluster", "rides"}});
  ASSERT_NE(nullptr, rides);
  EXPECT_EQ("rides", countedIn(*rides, "/rides"));
  EXPECT_EQ(1, counterValue("virtual_cluster_selected"));
  const auto none = resolve(*config.value(), "/rides", {{"x-virtual-cluster", ""}});
  ASSERT_NE(nullptr, none);
  EXPECT_EQ(nullptr, virtualClusterOf(*none, "/rides"));
  EXPECT_EQ(1, counterValue("virtual_cluster_none"));

  const auto sampled = loadRaw(virtualClusterConfigYaml(
      virtualClusterOverrideYaml("        prefer_selection: true\n        verify_one_in: 1\n"),
      ""));
  ASSERT_TRUE(sampled.ok());
  const auto preferred = resolve(*sampled.value(), "/rides", {{"x-virtual-cluster", "rides"}});
  ASSERT_NE(nullptr, preferred);
  EXPECT_EQ("rides", countedIn(*preferred, "/rides"));
  EXPECT_EQ(2, counterValue("virtual_cluster_selected"));
  EXPECT_EQ(0, counterValue("virtual_cluster_agree") + counterValue("virtual_cluster_disagree"));
}

// A preferred selection is compared with the virtual host with a chance of one in verify_one_in,
// which defaults to 1024, and zero compares none. The selection stays the virtual cluster of the
// request when it is compared, and a selection of none is never compared.
TEST_F(DynamicModuleVirtualClusterTest, PreferredSelectionIsComparedWithAChanceOfOneInVerifyOneIn) {
  const auto sampled = loadRaw(virtualClusterConfigYaml(
      virtualClusterOverrideYaml("        prefer_selection: true\n        verify_one_in: 3\n")));
  ASSERT_TRUE(sampled.ok());
  const auto rides = resolve(*sampled.value(), "/trips", {{"x-virtual-cluster", "rides"}});
  ASSERT_NE(nullptr, rides);
  for (const uint64_t value : {1, 2, 3, 4, 6}) {
    setComparisonRandomValue(value);
    EXPECT_EQ("rides", countedIn(*rides, "/trips"));
  }
  EXPECT_EQ(3, counterValue("virtual_cluster_selected"));
  EXPECT_EQ(2, counterValue("virtual_cluster_disagree"));
  const auto none = resolve(*sampled.value(), "/trips", {{"x-virtual-cluster", ""}});
  ASSERT_NE(nullptr, none);
  EXPECT_EQ("trips", countedIn(*none, "/trips"));
  EXPECT_EQ(2, counterValue("virtual_cluster_disagree"));
  EXPECT_EQ(1, counterValue("virtual_cluster_none"));

  const auto defaulted =
      loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection)));
  ASSERT_TRUE(defaulted.ok());
  const auto defaulted_rides =
      resolve(*defaulted.value(), "/rides", {{"x-virtual-cluster", "rides"}});
  ASSERT_NE(nullptr, defaulted_rides);
  for (const uint64_t value : {1023, 512}) {
    setComparisonRandomValue(value);
    EXPECT_EQ("rides", countedIn(*defaulted_rides, "/rides"));
  }
  EXPECT_EQ(5, counterValue("virtual_cluster_selected"));
  setComparisonRandomValue(1024);
  EXPECT_EQ("rides", countedIn(*defaulted_rides, "/rides"));
  EXPECT_EQ(1, counterValue("virtual_cluster_agree"));

  const auto uncompared = loadRaw(virtualClusterConfigYaml(
      virtualClusterOverrideYaml("        prefer_selection: true\n        verify_one_in: 0\n")));
  ASSERT_TRUE(uncompared.ok());
  const auto uncompared_rides =
      resolve(*uncompared.value(), "/trips", {{"x-virtual-cluster", "rides"}});
  ASSERT_NE(nullptr, uncompared_rides);
  EXPECT_CALL(context_.api_.random_, random()).Times(0);
  EXPECT_EQ("rides", countedIn(*uncompared_rides, "/trips"));
  EXPECT_EQ(6, counterValue("virtual_cluster_selected"));
}

// A name the configuration does not declare is rejected, counted and logged at debug level, so the
// route keeps the virtual host it wraps. Every name is rejected without being counted when the
// configuration declares no virtual clusters.
TEST_F(DynamicModuleVirtualClusterTest, UnknownVirtualClusterIsRejected) {
  const auto config =
      loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection)));
  ASSERT_TRUE(config.ok());
  Envoy::Router::RouteConstSharedPtr route;
  EXPECT_LOG_CONTAINS(
      "debug", "dynamic module route specifier selected unknown virtual cluster 'unknown'",
      route = resolve(*config.value(), "/trips", {{"x-virtual-cluster", "unknown"}}));
  ASSERT_NE(nullptr, route);
  EXPECT_EQ(0, lastVirtualClusterResult());
  EXPECT_EQ(1, counterValue("virtual_cluster_unknown"));
  EXPECT_EQ(route->virtualHostSharedPtr().get(), &route->virtualHost());
  EXPECT_EQ("trips", countedIn(*route, "/trips"));

  const auto undeclared = loadRaw(virtualClusterConfigYaml(""));
  ASSERT_TRUE(undeclared.ok());
  for (const std::string name : {"rides", ""}) {
    resolve(*undeclared.value(), "/trips", {{"x-virtual-cluster", name}});
    EXPECT_EQ(0, lastVirtualClusterResult());
  }
  EXPECT_EQ(1, counterValue("virtual_cluster_unknown"));
}

// A later selection replaces the earlier one, a selection of none included, while a rejected one
// leaves the earlier one in place.
TEST_F(DynamicModuleVirtualClusterTest, LaterSelectionReplacesTheEarlierOne) {
  const auto config =
      loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection)));
  ASSERT_TRUE(config.ok());
  struct TestCase {
    std::string later;
    int result;
    std::optional<std::string> counted;
  };
  for (const TestCase& test_case : std::vector<TestCase>{
           {"bikes", 1, "bikes"},
           {"unknown", 0, "rides"},
           {"", 1, "trips"},
       }) {
    SCOPED_TRACE(absl::StrCat("later selecting '", test_case.later, "'"));
    const auto route =
        resolve(*config.value(), "/trips",
                {{"x-virtual-cluster", "rides"}, {"x-later-virtual-cluster", test_case.later}});
    ASSERT_NE(nullptr, route);
    EXPECT_EQ(test_case.result, lastVirtualClusterResult());
    EXPECT_EQ(test_case.counted, countedIn(*route, "/trips"));
  }
  EXPECT_EQ(1, counterValue("virtual_cluster_unknown"));
  EXPECT_EQ(1, counterValue("virtual_cluster_none"));
}

// A null buffer with a length names no virtual cluster, so it is rejected and logged as such rather
// than selecting none, and the earlier selection stays.
TEST_F(DynamicModuleVirtualClusterTest, NullBufferWithALengthIsRejected) {
  const auto config =
      loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection)));
  ASSERT_TRUE(config.ok());
  Envoy::Router::RouteConstSharedPtr route;
  EXPECT_LOG_CONTAINS(
      "debug",
      "dynamic module route specifier selected a virtual cluster with a null "
      "buffer of length 5",
      route = resolve(*config.value(), "/trips",
                      {{"x-virtual-cluster", "rides"}, {"x-null-virtual-cluster", "rides"}}));
  ASSERT_NE(nullptr, route);
  EXPECT_EQ(0, lastVirtualClusterResult());
  EXPECT_EQ("rides", countedIn(*route, "/trips"));
  EXPECT_EQ(1, counterValue("virtual_cluster_unknown"));
}

// A configuration that declares a name twice, or an empty name, is rejected.
TEST_F(DynamicModuleVirtualClusterTest, InvalidVirtualClusterNamesAreRejected) {
  const auto config_declaring = [](absl::string_view names) {
    return virtualClusterConfigYaml(fmt::format(R"EOF(      virtual_cluster_override:
        virtual_host_name: test_vhost
        names: {}
)EOF",
                                                names));
  };
  EXPECT_THAT(loadRaw(config_declaring("[rides, trips, rides]")).status(),
              HasStatusMessage(HasSubstr("duplicate virtual cluster name 'rides'")));
  EXPECT_THROW_WITH_REGEX(std::ignore = loadRaw(config_declaring("[rides, '']")), EnvoyException,
                          "at least 1 characters");
}

// A selection applies to any route entry the decision produces, while a decision whose route
// answers the request directly drops it rather than failing like a route entry property.
TEST_F(DynamicModuleVirtualClusterTest, SelectionAppliesToTheRouteEntryTheDecisionProduces) {
  const auto config =
      loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection)));
  ASSERT_TRUE(config.ok());
  const auto direct = resolve(*config.value(), "/trips",
                              {{"x-template", "direct"}, {"x-virtual-cluster", "rides"}});
  ASSERT_NE(nullptr, direct);
  ASSERT_NE(nullptr, direct->directResponseEntry());
  EXPECT_EQ(direct->virtualHostSharedPtr().get(), &direct->virtualHost());
  const auto entry = resolve(*config.value(), "/trips",
                             {{"x-cluster", "selected_cluster"}, {"x-virtual-cluster", "rides"}});
  ASSERT_NE(nullptr, entry);
  EXPECT_EQ("selected_cluster", entry->routeEntry()->clusterName());
  EXPECT_EQ("rides", countedIn(*entry, "/trips"));
  const auto templated = resolve(*config.value(), "/trips",
                                 {{"x-template", "rerouted"}, {"x-virtual-cluster", "rides"}});
  ASSERT_NE(nullptr, templated);
  EXPECT_EQ("rerouted_cluster", templated->routeEntry()->clusterName());
  EXPECT_EQ("rides", countedIn(*templated, "/trips"));
  EXPECT_EQ(0, counterValue("failure_override_on_non_route_entry"));
}

// A route built without a selection keeps the virtual host of the route it wraps, which the router
// counts the request in without the selection statistics.
TEST_F(DynamicModuleVirtualClusterTest, RouteWithoutASelectionKeepsTheVirtualHost) {
  const auto config =
      loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection)));
  ASSERT_TRUE(config.ok());
  const auto given = resolve(*config.value(), "/trips");
  ASSERT_NE(nullptr, given);
  const auto entry = resolve(*config.value(), "/trips", {{"x-cluster", "selected_cluster"}});
  ASSERT_NE(nullptr, entry);
  EXPECT_EQ("selected_cluster", entry->routeEntry()->clusterName());
  const auto named = resolve(*config.value(), "/trips", {{"x-route-name", "named_route"}});
  ASSERT_NE(nullptr, named);
  EXPECT_EQ("named_route", named->routeName());
  for (const auto& route : {entry, named}) {
    EXPECT_NE(given, route);
    EXPECT_EQ(route->virtualHostSharedPtr().get(), &route->virtualHost());
    EXPECT_EQ("trips", countedIn(*route, "/trips"));
  }
  EXPECT_EQ(0, counterValue("virtual_cluster_selected") + counterValue("virtual_cluster_none"));
}

// A selection only labels the request, so a decision left Unspecified drops it without failing when
// route matching resolved no route.
TEST_F(DynamicModuleVirtualClusterTest, SelectionWithoutARouteIsDropped) {
  const auto config = loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection),
                                                       RidesAndTripsVirtualClusters, "/api"));
  ASSERT_TRUE(config.ok());
  EXPECT_EQ(nullptr, resolve(*config.value(), "/other", {{"x-virtual-cluster", "rides"}}));
  EXPECT_EQ(1, lastVirtualClusterResult());
  EXPECT_EQ(0, counterValue("failure_override_without_route"));
}

// The PassThrough, NoRoute and Error decisions ignore a selection like the recorded overrides, and
// the failure policy passes the route through for an Error.
TEST_F(DynamicModuleVirtualClusterTest, OtherDecisionsIgnoreTheSelection) {
  const auto config =
      loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection)));
  ASSERT_TRUE(config.ok());
  const auto given = resolve(*config.value(), "/trips");
  ASSERT_NE(nullptr, given);
  EXPECT_EQ(given, resolve(*config.value(), "/trips",
                           {{"x-decision", "pass-through"}, {"x-virtual-cluster", "rides"}}));
  EXPECT_EQ(1, lastVirtualClusterResult());
  EXPECT_EQ(nullptr, resolve(*config.value(), "/trips",
                             {{"x-decision", "no-route"}, {"x-virtual-cluster", "rides"}}));
  EXPECT_EQ(given, resolve(*config.value(), "/trips",
                           {{"x-decision", "error"}, {"x-virtual-cluster", "rides"}}));
  EXPECT_EQ(1, counterValue("decision_pass_through"));
  EXPECT_EQ(1, counterValue("decision_no_route"));
  EXPECT_EQ(1, counterValue("decision_error"));
}

// A decision that recorded only a selection builds a route without the recorded overrides, unless
// the built route cache looks the route up and needs them to compare.
TEST_F(DynamicModuleVirtualClusterTest, SelectionAloneBuildsAVirtualClusterRoute) {
  const auto config = loadRaw(virtualClusterConfigYaml(
      absl::StrCat(virtualClusterOverrideYaml(PreferSelection), builtRouteCacheYaml(16))));
  ASSERT_TRUE(config.ok());
  const auto selection_only = resolve(*config.value(), "/trips", {{"x-virtual-cluster", "rides"}});
  EXPECT_NE(nullptr,
            dynamic_cast<const VirtualClusterSelectingRoute<DynamicModuleVirtualClusterRoute>*>(
                selection_only.get()));
  const auto cached =
      resolve(*config.value(), "/trips",
              {{"x-template", "rerouted"}, {"x-key", "1"}, {"x-virtual-cluster", "rides"}});
  EXPECT_NE(nullptr,
            dynamic_cast<const VirtualClusterSelectingRoute<DynamicModuleRoute>*>(cached.get()));
}

// Reusing the previous route keeps the virtual cluster it carries, and the selection the decision
// records is ignored.
TEST_F(DynamicModuleVirtualClusterTest, ReusedPreviousRouteKeepsItsSelection) {
  const auto config =
      loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection)));
  ASSERT_TRUE(config.ok());
  stream_info_.route_ = resolve(*config.value(), "/trips", {{"x-virtual-cluster", "rides"}});
  ASSERT_NE(nullptr, stream_info_.route_);
  const auto reused = resolve(*config.value(), "/trips",
                              {{"x-decision", "reuse-previous"}, {"x-virtual-cluster", "bikes"}});
  EXPECT_EQ(stream_info_.route_, reused);
  EXPECT_EQ("rides", countedIn(*reused, "/trips"));
}

// A virtual cluster selected for a route of a virtual host other than the declared one is dropped
// and counted, since its statistics would be named after the declared one. A decision that recorded
// nothing else then passes the route through, while a selection of none names no statistics and is
// kept.
TEST_F(DynamicModuleVirtualClusterTest, SelectionForAnotherVirtualHostIsDropped) {
  const auto config =
      loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection, "other_vhost")));
  ASSERT_TRUE(config.ok());
  const auto given = resolve(*config.value(), "/trips");
  ASSERT_NE(nullptr, given);
  EXPECT_EQ(given, resolve(*config.value(), "/trips", {{"x-virtual-cluster", "rides"}}));
  EXPECT_EQ(1, counterValue("virtual_cluster_vhost_mismatch"));
  for (const auto& extra_header : std::vector<std::pair<std::string, std::string>>{
           {"x-route-name", "named_route"}, {"x-cluster", "selected_cluster"}}) {
    SCOPED_TRACE(extra_header.first);
    const auto route =
        resolve(*config.value(), "/trips", {{"x-virtual-cluster", "rides"}, extra_header});
    ASSERT_NE(nullptr, route);
    EXPECT_EQ(1, lastVirtualClusterResult());
    EXPECT_EQ(route->virtualHostSharedPtr().get(), &route->virtualHost());
    EXPECT_EQ("trips", countedIn(*route, "/trips"));
  }
  EXPECT_EQ(3, counterValue("virtual_cluster_vhost_mismatch"));
  EXPECT_EQ(0, counterValue("virtual_cluster_selected"));

  const auto none = resolve(*config.value(), "/trips", {{"x-virtual-cluster", ""}});
  ASSERT_NE(nullptr, none);
  EXPECT_EQ("trips", countedIn(*none, "/trips"));
  EXPECT_EQ(1, counterValue("virtual_cluster_none"));
  EXPECT_EQ(3, counterValue("virtual_cluster_vhost_mismatch"));
}

// The built route cache serves the route a miss built along with its selection, so a hit counts the
// request in the virtual cluster of the miss.
TEST_F(DynamicModuleVirtualClusterTest, HitServesTheSelectionOfTheMiss) {
  const auto config = loadRaw(virtualClusterConfigYaml(
      absl::StrCat(virtualClusterOverrideYaml(PreferSelection), builtRouteCacheYaml(16))));
  ASSERT_TRUE(config.ok());
  const auto miss =
      resolve(*config.value(), "/trips",
              {{"x-template", "rerouted"}, {"x-key", "1"}, {"x-virtual-cluster", "rides"}});
  ASSERT_NE(nullptr, miss);
  EXPECT_EQ(1, lastVirtualClusterResult());
  const auto hit =
      resolve(*config.value(), "/trips",
              {{"x-template", "rerouted"}, {"x-key", "1"}, {"x-virtual-cluster", "bikes"}});
  EXPECT_EQ(-1, lastVirtualClusterResult());
  EXPECT_EQ(miss, hit);
  EXPECT_EQ("rides", countedIn(*hit, "/trips"));
  EXPECT_EQ(1, counterValue("built_route_cache_hit"));
}

// A route that carries a selection keeps the specifier configuration, which owns the selected
// virtual cluster, alive after the route configuration is released, whether the decision recorded
// only the selection or more.
TEST_F(DynamicModuleVirtualClusterTest, RouteKeepsItsSelectionAfterTheConfigurationIsReleased) {
  // Comparing also reads the virtual host of the released route configuration.
  setComparisonRandomValue(1024);
  for (const auto& extra_headers : std::vector<std::vector<std::pair<std::string, std::string>>>{
           {}, {{"x-cluster", "selected_cluster"}}}) {
    SCOPED_TRACE(extra_headers.size());
    auto config = loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection)));
    ASSERT_TRUE(config.ok());
    auto headers = extra_headers;
    headers.emplace_back("x-virtual-cluster", "bikes");
    auto route = resolve(*config.value(), "/trips", headers);
    ASSERT_NE(nullptr, route);
    const int destroys = moduleValue("getConfigDestroyCount");
    config.value().reset();
    EXPECT_EQ(destroys, moduleValue("getConfigDestroyCount"));
    const Envoy::Router::VirtualCluster* cluster = virtualClusterOf(*route, "/trips");
    ASSERT_NE(nullptr, cluster);
    EXPECT_EQ("bikes", cluster->name());
    cluster->stats().upstream_rq_total_.inc();
    route.reset();
    EXPECT_EQ(destroys + 1, moduleValue("getConfigDestroyCount"));
  }
  EXPECT_EQ(2, counterValue("virtual_cluster_disagree"));
}

// A decision that builds a route for a selection alone reads the clock once more, like any decision
// that builds a route after the module returned.
TEST_F(DynamicModuleVirtualClusterTest, SelectionAloneIsTimedLikeAnyBuiltRoute) {
  NiceMock<MockTimeSystem> time_system;
  ON_CALL(context_, timeSource()).WillByDefault(testing::ReturnRef(time_system));
  const auto config =
      loadRaw(virtualClusterConfigYaml(virtualClusterOverrideYaml(PreferSelection)));
  ASSERT_TRUE(config.ok());
  EXPECT_CALL(time_system, monotonicTime()).Times(3);
  resolve(*config.value(), "/trips", {{"x-virtual-cluster", "rides"}});
  testing::Mock::VerifyAndClearExpectations(&time_system);
  EXPECT_CALL(time_system, monotonicTime()).Times(2);
  resolve(*config.value(), "/trips");
}

// Route specifiers that declare the same names for a virtual host share their virtual clusters,
// whatever the order of the names or whether they prefer the selection, while other names or
// another virtual host get virtual clusters of their own. Virtual clusters of one name and virtual
// host share their statistics either way.
TEST_F(DynamicModuleRouteSpecifierTest, IdenticalDeclarationsShareTheirVirtualClusters) {
  const auto get = [this](absl::string_view yaml) {
    VirtualClusterOverrideProto declaration;
    TestUtility::loadFromYaml(std::string(yaml), declaration);
    return DeclaredVirtualClusterRegistry::getOrCreate(declaration, context_);
  };
  const auto clusters = get("{virtual_host_name: test_vhost, names: [rides, trips]}");
  ASSERT_NE(nullptr, clusters);
  EXPECT_EQ(clusters, get("{virtual_host_name: test_vhost, names: [trips, rides]}"));
  EXPECT_EQ(clusters, get("{virtual_host_name: test_vhost, names: [rides, trips], "
                          "prefer_selection: true}"));
  const auto other_names = get("{virtual_host_name: test_vhost, names: [rides, bikes]}");
  auto fewer = get("{virtual_host_name: test_vhost, names: [rides]}");
  const auto other_vhost = get("{virtual_host_name: other_vhost, names: [rides, trips]}");
  ASSERT_NE(nullptr, other_names);
  ASSERT_NE(nullptr, fewer);
  ASSERT_NE(nullptr, other_vhost);
  EXPECT_NE(clusters, other_names);
  EXPECT_NE(clusters, fewer);
  EXPECT_NE(clusters, other_vhost);
  EXPECT_EQ(nullptr, fewer->find("trips"));
  ASSERT_NE(nullptr, clusters->find("rides"));
  ASSERT_NE(nullptr, fewer->find("rides"));
  EXPECT_NE(clusters->find("rides"), fewer->find("rides"));
  EXPECT_EQ(&clusters->find("rides")->stats().upstream_rq_total_,
            &fewer->find("rides")->stats().upstream_rq_total_);
  EXPECT_NE(&clusters->find("rides")->stats().upstream_rq_total_,
            &other_vhost->find("rides")->stats().upstream_rq_total_);

  const std::weak_ptr<const DeclaredVirtualClusterSet> released = fewer;
  fewer.reset();
  EXPECT_TRUE(released.expired());
  const auto rebuilt = get("{virtual_host_name: test_vhost, names: [rides]}");
  ASSERT_NE(nullptr, rebuilt);
  EXPECT_NE(nullptr, rebuilt->find("rides"));
  EXPECT_EQ(clusters, get("{virtual_host_name: test_vhost, names: [rides, trips]}"));
}

// The registry keeps an entry per virtual host and set of names. It prunes the released
// declarations of the entry it looks up, and sweeps the entries whose declarations are all
// released once it holds twice as many entries as its last sweep left.
TEST_F(DynamicModuleRouteSpecifierTest, RegistrySweepsReleasedDeclarations) {
  const auto get = [this](absl::string_view virtual_host_name, absl::string_view names) {
    VirtualClusterOverrideProto declaration;
    TestUtility::loadFromYaml(
        fmt::format("{{virtual_host_name: {}, names: {}}}", virtual_host_name, names), declaration);
    return DeclaredVirtualClusterRegistry::getOrCreate(declaration, context_);
  };
  const auto first = get("first", "[rides]");
  const auto second = get("second", "[rides]");
  auto third = get("third", "[rides]");
  third.reset();
  // The last sweep left two entries, so the fourth declaration keeps the entry of the third.
  const auto fourth = get("fourth", "[rides]");
  EXPECT_EQ(4, DeclaredVirtualClusterRegistryPeer::entryCount(*first));
  const auto fifth = get("fifth", "[rides]");
  EXPECT_EQ(4, DeclaredVirtualClusterRegistryPeer::entryCount(*first));

  auto released = get("first", "[trips]");
  EXPECT_EQ(5, DeclaredVirtualClusterRegistryPeer::entryCount(*first));
  EXPECT_EQ(5, DeclaredVirtualClusterRegistryPeer::declarationCount(*first));
  released.reset();
  const auto again = get("first", "[trips]");
  EXPECT_EQ(5, DeclaredVirtualClusterRegistryPeer::declarationCount(*first));
  EXPECT_EQ(first, get("first", "[rides]"));
}

// Declarations whose fingerprints collide keep virtual clusters of their own, a declaration of a
// subset of the names of another included.
TEST_F(DynamicModuleRouteSpecifierTest, CollidingDeclarationsKeepTheirOwnVirtualClusters) {
  VirtualClusterOverrideProto rides;
  TestUtility::loadFromYaml("{virtual_host_name: test_vhost, names: [rides]}", rides);
  VirtualClusterOverrideProto trips;
  TestUtility::loadFromYaml("{virtual_host_name: test_vhost, names: [trips]}", trips);
  const auto rides_clusters = DeclaredVirtualClusterRegistry::getOrCreate(rides, context_);
  DeclaredVirtualClusterRegistryPeer::collide(rides_clusters, trips);
  const auto trips_clusters = DeclaredVirtualClusterRegistry::getOrCreate(trips, context_);
  EXPECT_NE(rides_clusters, trips_clusters);
  EXPECT_NE(nullptr, trips_clusters->find("trips"));
  EXPECT_EQ(trips_clusters, DeclaredVirtualClusterRegistry::getOrCreate(trips, context_));

  VirtualClusterOverrideProto superset_declaration;
  TestUtility::loadFromYaml("{virtual_host_name: other_vhost, names: [rides, trips]}",
                            superset_declaration);
  const auto superset = DeclaredVirtualClusterRegistry::getOrCreate(superset_declaration, context_);
  VirtualClusterOverrideProto subset;
  TestUtility::loadFromYaml("{virtual_host_name: other_vhost, names: [rides]}", subset);
  DeclaredVirtualClusterRegistryPeer::collide(superset, subset);
  EXPECT_NE(superset, DeclaredVirtualClusterRegistry::getOrCreate(subset, context_));
}

// The virtual host of a route that carries a selection forwards everything but the virtual cluster
// lookup to the virtual host of the route it wraps.
TEST_F(DynamicModuleRouteSpecifierTest,
       VirtualClusterSelectingVirtualHostForwardsToTheVirtualHostItWraps) {
  auto module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("route_specifier_virtual_cluster", "c"),
      /*do_not_close=*/true);
  ASSERT_TRUE(module.ok());
  Envoy::Router::RouteSpecifierFactoryContextImpl factory_context(context_, {});
  const DynamicModuleRouteSpecifierConfig config(DynamicModuleRouteSpecifierProto(), "",
                                                 std::move(module.value()), factory_context,
                                                 DefaultMetricsNamespace);
  testing::StrictMock<Envoy::Router::MockVirtualHost> base;
  const VirtualClusterSelectingVirtualHost host(base, config, nullptr);

  EXPECT_CALL(base, corsPolicy()).WillOnce(testing::Return(&base.cors_policy_));
  EXPECT_EQ(&base.cors_policy_, host.corsPolicy());
  EXPECT_CALL(base, name()).WillRepeatedly(testing::ReturnRef(base.name_));
  EXPECT_EQ(&base.name_, &host.name());
  EXPECT_EQ("fake_vhost", base.symbol_table_->toString(host.statName()));
  EXPECT_CALL(base, rateLimitPolicy()).WillOnce(testing::ReturnRef(base.rate_limit_policy_));
  EXPECT_EQ(&base.rate_limit_policy_, &host.rateLimitPolicy());
  NiceMock<Envoy::Router::MockConfig> route_config;
  EXPECT_CALL(base, routeConfig()).WillOnce(testing::ReturnRef(route_config));
  EXPECT_EQ(&route_config, &host.routeConfig());
  EXPECT_CALL(base, includeAttemptCountInRequest()).WillOnce(testing::Return(true));
  EXPECT_TRUE(host.includeAttemptCountInRequest());
  EXPECT_CALL(base, includeAttemptCountInResponse()).WillOnce(testing::Return(true));
  EXPECT_TRUE(host.includeAttemptCountInResponse());
  EXPECT_CALL(base, includeIsTimeoutRetryHeader()).WillOnce(testing::Return(true));
  EXPECT_TRUE(host.includeIsTimeoutRetryHeader());
  const Envoy::Router::RouteSpecificFilterConfig filter_config;
  EXPECT_CALL(base, mostSpecificPerFilterConfig("filter"))
      .WillOnce(testing::Return(&filter_config));
  EXPECT_EQ(&filter_config, host.mostSpecificPerFilterConfig("filter"));
  EXPECT_CALL(base, perFilterConfigs("filter"))
      .WillOnce(testing::Return(Envoy::Router::RouteSpecificFilterConfigs{&filter_config}));
  EXPECT_THAT(host.perFilterConfigs("filter"), testing::ElementsAre(&filter_config));
  EXPECT_CALL(base, metadata()).WillOnce(testing::ReturnRef(base.metadata_));
  EXPECT_EQ(&base.metadata_, &host.metadata());
  EXPECT_CALL(base, typedMetadata()).WillOnce(testing::ReturnRef(base.typed_metadata_));
  EXPECT_EQ(&base.typed_metadata_, &host.typedMetadata());
}

// A preferred selection consults the virtual host it wraps only when it is compared, while a
// selection the router always compares and a selection of none consult it on every lookup. A later
// selection in a chain that consults the virtual host does so through the earlier one, which counts
// the request.
TEST_F(DynamicModuleRouteSpecifierTest, SelectionConsultsTheVirtualHostOnlyToCompare) {
  Envoy::Router::RouteSpecifierFactoryContextImpl factory_context(context_, {});
  std::vector<std::unique_ptr<const DynamicModuleRouteSpecifierConfig>> configs;
  for (const bool prefer_selection : {true, false}) {
    auto module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("route_specifier_virtual_cluster", "c"),
        /*do_not_close=*/true);
    ASSERT_TRUE(module.ok());
    DynamicModuleRouteSpecifierProto proto_config;
    proto_config.set_stat_prefix(prefer_selection ? "preferred" : "compared");
    proto_config.mutable_virtual_cluster_override()->set_prefer_selection(prefer_selection);
    proto_config.mutable_virtual_cluster_override()->mutable_verify_one_in()->set_value(3);
    configs.push_back(std::make_unique<const DynamicModuleRouteSpecifierConfig>(
        proto_config, "", std::move(module.value()), factory_context, DefaultMetricsNamespace));
  }
  testing::StrictMock<Envoy::Router::MockVirtualHost> base;
  const Envoy::Router::VirtualCluster* matched = &base.virtual_cluster_;
  // The test printer of an unexpected lookup only prints a header map of this type.
  const Http::RequestHeaderMapPtr headers = Http::RequestHeaderMapImpl::create();
  const VirtualClusterSelectingVirtualHost preferred(base, *configs[0], matched);
  const VirtualClusterSelectingVirtualHost compared(base, *configs[1], matched);
  const VirtualClusterSelectingVirtualHost none(base, *configs[0], nullptr);

  EXPECT_CALL(context_.api_.random_, random()).WillOnce(testing::Return(1));
  EXPECT_EQ(matched, preferred.virtualCluster(*headers));
  testing::Mock::VerifyAndClearExpectations(&context_.api_.random_);

  EXPECT_CALL(context_.api_.random_, random()).WillOnce(testing::Return(3));
  EXPECT_CALL(base, virtualCluster(testing::Ref(*headers))).WillOnce(testing::Return(matched));
  EXPECT_EQ(matched, preferred.virtualCluster(*headers));
  testing::Mock::VerifyAndClearExpectations(&base);
  testing::Mock::VerifyAndClearExpectations(&context_.api_.random_);

  EXPECT_CALL(context_.api_.random_, random()).Times(0);
  EXPECT_CALL(base, virtualCluster(testing::Ref(*headers)))
      .Times(2)
      .WillRepeatedly(testing::Return(matched));
  EXPECT_EQ(matched, compared.virtualCluster(*headers));
  EXPECT_EQ(matched, none.virtualCluster(*headers));
  testing::Mock::VerifyAndClearExpectations(&base);
  testing::Mock::VerifyAndClearExpectations(&context_.api_.random_);

  const auto counter = [this](absl::string_view stat_prefix, absl::string_view name) {
    return context_.store_
        .counter(absl::StrCat("dynamicmodulescustom.route_specifier.", stat_prefix, ".", name))
        .value();
  };
  const VirtualClusterSelectingVirtualHost compared_after_preferred(preferred, *configs[1],
                                                                    matched);
  EXPECT_CALL(context_.api_.random_, random()).WillOnce(testing::Return(1));
  EXPECT_EQ(matched, compared_after_preferred.virtualCluster(*headers));
  EXPECT_EQ(2, counter("preferred", "virtual_cluster_selected"));
  EXPECT_EQ(2, counter("compared", "virtual_cluster_agree"));
  testing::Mock::VerifyAndClearExpectations(&context_.api_.random_);
  const VirtualClusterSelectingVirtualHost preferred_after_compared(compared, *configs[0], matched);
  EXPECT_CALL(context_.api_.random_, random()).WillOnce(testing::Return(1));
  EXPECT_EQ(matched, preferred_after_compared.virtualCluster(*headers));
  EXPECT_EQ(3, counter("preferred", "virtual_cluster_selected"));
  EXPECT_EQ(2, counter("compared", "virtual_cluster_agree"));
}

} // namespace
} // namespace DynamicModules
} // namespace RouteSpecifiers
} // namespace Extensions
} // namespace Envoy

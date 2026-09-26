#include <memory>
#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/extensions/router/route_specifiers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"
#include "source/common/router/config_impl.h"
#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/router/route_specifiers/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace RouteSpecifiers {
namespace DynamicModules {
namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;
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
    envoy::config::route::v3::RouteConfiguration proto_config;
    TestUtility::loadFromYaml(routeConfigYaml(specifier_yaml), proto_config);
    return Envoy::Router::ConfigImpl::create(proto_config, context_, creation_status_visitor_,
                                             init_manager_, false);
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

TEST_F(DynamicModuleRouteSpecifierTest, ShadowModeWithoutFailurePolicy) {
  const auto config = loadConfig(specifierYaml("route_specifier_no_op", R"EOF(      shadow_mode: {}
)EOF"));
  EXPECT_TRUE(config.ok());
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

// A module built against a newer ABI could return a decision this build does not know, which is
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

// A virtual host that configures no routes at all is routed entirely by the module, which is how a
// module owns the routing of a virtual host once shadow mode has validated it.
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
  EXPECT_EQ(
      1,
      context_.store_.counter("custom_metrics.route_specifier.test.decision_pass_through").value());
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

  // The recorded cluster and path replace those of the route.
  EXPECT_EQ("canary", entry->clusterName());
  auto headers = requestHeaders();
  Formatter::Context formatter_context(&headers);
  EXPECT_EQ("/rewritten",
            entry->currentUrlPathAfterRewrite(headers, formatter_context, stream_info_));

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
  // Only a filter override was recorded, so the metadata accessors fall back to the route, which
  // carries no envoy.test.route metadata.
  EXPECT_FALSE(route.route->metadata().filter_metadata().contains("envoy.test.route"));
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
  // Only a cluster override was recorded, so the metadata accessors fall back to the route.
  EXPECT_FALSE(route.route->metadata().filter_metadata().contains("envoy.test.route"));
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

} // namespace
} // namespace DynamicModules
} // namespace RouteSpecifiers
} // namespace Extensions
} // namespace Envoy

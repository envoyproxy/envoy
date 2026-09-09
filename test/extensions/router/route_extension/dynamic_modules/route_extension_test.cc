#include <tuple>

#include "envoy/extensions/router/route_extension/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

#include "source/common/common/fmt.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/router/route_extension/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {
namespace {

using ::testing::NiceMock;
using ::testing::Return;

void mockCustomStatNamespaces(Server::Configuration::MockServerFactoryContext& context,
                              Stats::CustomStatNamespacesImpl& custom_stat_namespaces) {
  ON_CALL(context.api_, customStatNamespaces())
      .WillByDefault(testing::ReturnRef(custom_stat_namespaces));
}

DynamicModuleRouteExtensionProto protoConfig(absl::string_view module_name,
                                             absl::string_view extension_name,
                                             absl::string_view route_action_overrides_yaml = "") {
  const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
  name: {}
  do_not_close: true
extension_name: {}
{}
)EOF",
                                       module_name, extension_name, route_action_overrides_yaml);
  DynamicModuleRouteExtensionProto proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  return proto_config;
}

// Tests for the factory using minimal C modules to drive symbol resolution and config lifecycle.
class DynamicModuleRouteExtensionFactoryTest : public testing::Test {
public:
  DynamicModuleRouteExtensionFactoryTest() {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
    mockCustomStatNamespaces(context_, custom_stat_namespaces_);
  }

  // Returns the number of times the no-op module has been handed its configuration back, so that a
  // test can observe the config destroy hook. Holding the module reference keeps the counter alive,
  // since the extension and this handle share one dlopen of the same path.
  std::function<int()> configDestroyCounter() {
    using GetConfigDestroyCountFuncType = int (*)(void);
    auto module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("route_extension_no_op", "c"),
        /*do_not_close=*/true);
    EXPECT_TRUE(module.ok());
    auto destroy_count =
        module.value()->getFunctionPointer<GetConfigDestroyCountFuncType>("getConfigDestroyCount");
    EXPECT_TRUE(destroy_count.ok());
    return [module = std::shared_ptr(std::move(module.value())), count = destroy_count.value()]() {
      return count();
    };
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Stats::CustomStatNamespacesImpl custom_stat_namespaces_;
  DynamicModuleRouteExtensionFactory factory_;
};

TEST_F(DynamicModuleRouteExtensionFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.router.route_extension.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleRouteExtensionFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  ASSERT_NE(nullptr, proto);
  EXPECT_NE(nullptr, dynamic_cast<DynamicModuleRouteExtensionProto*>(proto.get()));
}

TEST_F(DynamicModuleRouteExtensionFactoryTest, FactoryRegistration) {
  auto* factory = Registry::FactoryRegistry<Envoy::Router::RouteExtensionFactory>::getFactory(
      "envoy.router.route_extension.dynamic_modules");
  EXPECT_NE(nullptr, factory);
}

TEST_F(DynamicModuleRouteExtensionFactoryTest, ValidConfig) {
  auto proto_config = protoConfig("route_extension_no_op", "test_route_extension");
  auto extension = factory_.createRouteExtension(proto_config, context_);
  ASSERT_TRUE(extension.ok());
  EXPECT_NE(nullptr, extension.value());
}

TEST_F(DynamicModuleRouteExtensionFactoryTest, InvalidModule) {
  auto proto_config = protoConfig("nonexistent_module", "test_route_extension");
  auto extension = factory_.createRouteExtension(proto_config, context_);
  EXPECT_FALSE(extension.ok());
  EXPECT_EQ(1U, Extensions::DynamicModules::failureCounter(context_.scope(), "module_load_error",
                                                           "test_route_extension"));
}

TEST_F(DynamicModuleRouteExtensionFactoryTest, MissingConfigNew) {
  auto proto_config = protoConfig("route_extension_missing_config_new", "test_route_extension");
  EXPECT_FALSE(factory_.createRouteExtension(proto_config, context_).ok());
}

TEST_F(DynamicModuleRouteExtensionFactoryTest, MissingConfigDestroy) {
  auto proto_config = protoConfig("route_extension_missing_config_destroy", "test_route_extension");
  EXPECT_FALSE(factory_.createRouteExtension(proto_config, context_).ok());
}

TEST_F(DynamicModuleRouteExtensionFactoryTest, MissingOnRoute) {
  auto proto_config = protoConfig("route_extension_missing_on_route", "test_route_extension");
  EXPECT_FALSE(factory_.createRouteExtension(proto_config, context_).ok());
}

TEST_F(DynamicModuleRouteExtensionFactoryTest, ConfigNewFails) {
  auto proto_config = protoConfig("route_extension_config_new_fail", "test_route_extension");
  auto extension = factory_.createRouteExtension(proto_config, context_);
  EXPECT_FALSE(extension.ok());
  EXPECT_EQ(1U, Extensions::DynamicModules::failureCounter(context_.scope(), "config_init_error",
                                                           "test_route_extension"));
}

// An override whose metadata_match has no envoy.lb entry replaces no property, so it is rejected at
// configuration load rather than accepted as a decision on the request path.
TEST_F(DynamicModuleRouteExtensionFactoryTest, RejectsRouteActionOverrideThatReplacesNothing) {
  auto proto_config = protoConfig("route_extension_no_op", "test_route_extension", R"EOF(
route_action_overrides:
  empty:
    metadata_match:
      filter_metadata:
        not_envoy_lb:
          version: canary
)EOF");
  auto extension = factory_.createRouteExtension(proto_config, context_);
  EXPECT_FALSE(extension.ok());
  EXPECT_THAT(extension.status().message(),
              testing::HasSubstr("must replace at least one route action property"));
}

// An override that replaces nothing is a configuration mistake rather than a silent no-op.
TEST_F(DynamicModuleRouteExtensionFactoryTest, RejectsEmptyRouteActionOverride) {
  auto proto_config = protoConfig("route_extension_no_op", "test_route_extension", R"EOF(
route_action_overrides:
  empty: {}
)EOF");
  auto extension = factory_.createRouteExtension(proto_config, context_);
  EXPECT_FALSE(extension.ok());
  EXPECT_THAT(extension.status().message(),
              testing::HasSubstr("must replace at least one route action property"));
}

// An invalid retry policy inside a route action override is rejected at configuration load.
TEST_F(DynamicModuleRouteExtensionFactoryTest, RejectsInvalidRetryPolicyInOverride) {
  auto proto_config = protoConfig("route_extension_no_op", "test_route_extension", R"EOF(
route_action_overrides:
  bad:
    retry_policy:
      retry_back_off:
        base_interval: 2s
        max_interval: 1s
)EOF");
  auto extension = factory_.createRouteExtension(proto_config, context_);
  EXPECT_FALSE(extension.ok());
  EXPECT_THAT(extension.status().message(), testing::HasSubstr("max_interval"));
}

// An invalid request mirror policy inside a route action override is rejected at configuration
// load.
TEST_F(DynamicModuleRouteExtensionFactoryTest, RejectsInvalidMirrorPolicyInOverride) {
  auto proto_config = protoConfig("route_extension_no_op", "test_route_extension", R"EOF(
route_action_overrides:
  bad:
    request_mirror_policies:
    - cluster: mirror-cluster
      cluster_header: x-mirror
)EOF");
  auto extension = factory_.createRouteExtension(proto_config, context_);
  EXPECT_FALSE(extension.ok());
}

// Releasing the extension must hand the in-module configuration back to the module.
TEST_F(DynamicModuleRouteExtensionFactoryTest, ReleasingExtensionDestroysInModuleConfig) {
  const auto destroy_count = configDestroyCounter();
  const int before = destroy_count();
  auto proto_config = protoConfig("route_extension_no_op", "test_route_extension");
  auto extension = factory_.createRouteExtension(proto_config, context_);
  ASSERT_TRUE(extension.ok());
  auto extension_ptr = std::move(extension.value());
  EXPECT_EQ(before, destroy_count());
  extension_ptr.reset();
  EXPECT_EQ(before + 1, destroy_count());
}

// Tests for the on_route decisions using the Rust module, which is driven by request headers.
class DynamicModuleRouteExtensionTest : public testing::Test {
public:
  DynamicModuleRouteExtensionTest() {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
  }

  void setUpExtension(absl::string_view default_cluster = "",
                      absl::string_view route_action_overrides_yaml = "") {
    auto proto_config = protoConfig("route_extension_integration_test", "test_route_extension",
                                    route_action_overrides_yaml);
    if (!default_cluster.empty()) {
      Protobuf::StringValue value;
      value.set_value(std::string(default_cluster));
      std::ignore = proto_config.mutable_extension_config()->PackFrom(value);
    }
    auto extension = factory_.createRouteExtension(proto_config, context_);
    ASSERT_TRUE(extension.ok());
    extension_ = std::move(extension.value());
  }

  Envoy::Router::RouteConstSharedPtr onRoute(Http::TestRequestHeaderMapImpl& headers) {
    return extension_->onRoute(base_route_, headers, stream_info_, 0);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  DynamicModuleRouteExtensionFactory factory_;
  Envoy::Router::RouteExtensionSharedPtr extension_;
  std::shared_ptr<NiceMock<Envoy::Router::MockRoute>> base_route_{
      std::make_shared<NiceMock<Envoy::Router::MockRoute>>()};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(DynamicModuleRouteExtensionTest, KeepsRouteWithoutAction) {
  setUpExtension();
  ON_CALL(*base_route_, routeEntry()).WillByDefault(Return(&base_route_->route_entry_));
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}};
  EXPECT_EQ(base_route_, onRoute(headers));
}

TEST_F(DynamicModuleRouteExtensionTest, OverridesClusterFromHeader) {
  setUpExtension();
  ON_CALL(*base_route_, routeEntry()).WillByDefault(Return(&base_route_->route_entry_));
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"x-route-action", "override"}, {"x-cluster", "overridden-cluster"}};
  auto route = onRoute(headers);
  ASSERT_NE(nullptr, route);
  EXPECT_EQ("overridden-cluster", route->routeEntry()->clusterName());
}

TEST_F(DynamicModuleRouteExtensionTest, DropsRoute) {
  setUpExtension();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"x-route-action", "drop"}};
  EXPECT_EQ(nullptr, onRoute(headers));
}

// A route without a route entry, such as a direct response, cannot be overridden, so it is kept.
TEST_F(DynamicModuleRouteExtensionTest, KeepsRouteWithoutRouteEntryOnOverride) {
  setUpExtension();
  ON_CALL(*base_route_, routeEntry()).WillByDefault(Return(nullptr));
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"x-route-action", "override"}, {"x-cluster", "overridden-cluster"}};
  EXPECT_EQ(base_route_, onRoute(headers));
}

TEST_F(DynamicModuleRouteExtensionTest, OverrideUsesTheClusterFromExtensionConfig) {
  setUpExtension("default-from-config");
  ON_CALL(*base_route_, routeEntry()).WillByDefault(Return(&base_route_->route_entry_));
  // Without an x-cluster header the module falls back to the cluster carried in extension_config.
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"x-route-action", "override"}};
  auto route = onRoute(headers);
  ASSERT_NE(nullptr, route);
  EXPECT_EQ("default-from-config", route->routeEntry()->clusterName());
}

TEST_F(DynamicModuleRouteExtensionTest, OverrideWithoutAClusterKeepsRoute) {
  setUpExtension();
  ON_CALL(*base_route_, routeEntry()).WillByDefault(Return(&base_route_->route_entry_));
  // An override that sets no cluster keeps the matched route rather than clearing its cluster.
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"x-route-action", "override"}};
  EXPECT_EQ(base_route_, onRoute(headers));
}

// The module selects a declared override by name and its properties replace those of the matched
// route entry.
TEST_F(DynamicModuleRouteExtensionTest, SelectsRouteActionOverride) {
  setUpExtension("", R"EOF(
route_action_overrides:
  canary:
    retry_policy:
      retry_on: "5xx"
      num_retries: 3
    metadata_match:
      filter_metadata:
        envoy.lb:
          version: canary
    request_mirror_policies:
    - cluster: shadow-cluster
    hash_policy:
    - header:
        header_name: "x-user"
)EOF");
  ON_CALL(*base_route_, routeEntry()).WillByDefault(Return(&base_route_->route_entry_));
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"x-route-action", "override"}, {"x-action-override", "canary"}};
  auto route = onRoute(headers);
  ASSERT_NE(nullptr, route);
  const auto* entry = route->routeEntry();
  ASSERT_NE(nullptr, entry);
  // No cluster was set, so the cluster name delegates to the matched route.
  EXPECT_EQ(base_route_->route_entry_.clusterName(), entry->clusterName());
  const auto& retry_policy = entry->retryPolicy();
  ASSERT_NE(nullptr, retry_policy);
  EXPECT_EQ(3U, retry_policy->numRetries());
  const auto* metadata_criteria = entry->metadataMatchCriteria();
  ASSERT_NE(nullptr, metadata_criteria);
  ASSERT_FALSE(metadata_criteria->metadataMatchCriteria().empty());
  EXPECT_EQ("version", metadata_criteria->metadataMatchCriteria()[0]->name());
  ASSERT_EQ(1, entry->shadowPolicies().size());
  EXPECT_EQ("shadow-cluster", entry->shadowPolicies()[0]->cluster());
  EXPECT_NE(nullptr, entry->hashPolicy());
}

// The module can override both the cluster and the route action properties for a request.
TEST_F(DynamicModuleRouteExtensionTest, OverridesClusterAndRouteActionTogether) {
  setUpExtension("", R"EOF(
route_action_overrides:
  canary:
    retry_policy:
      num_retries: 3
)EOF");
  ON_CALL(*base_route_, routeEntry()).WillByDefault(Return(&base_route_->route_entry_));
  Http::TestRequestHeaderMapImpl headers{{":path", "/"},
                                         {"x-route-action", "override"},
                                         {"x-cluster", "overridden-cluster"},
                                         {"x-action-override", "canary"}};
  auto route = onRoute(headers);
  ASSERT_NE(nullptr, route);
  const auto* entry = route->routeEntry();
  ASSERT_NE(nullptr, entry);
  EXPECT_EQ("overridden-cluster", entry->clusterName());
  ASSERT_NE(nullptr, entry->retryPolicy());
  EXPECT_EQ(3U, entry->retryPolicy()->numRetries());
}

// An override with a route entry but no selected route action template delegates every route action
// property to the matched route entry.
TEST_F(DynamicModuleRouteExtensionTest, OverrideWithoutTemplateDelegatesRouteAction) {
  setUpExtension();
  ON_CALL(*base_route_, routeEntry()).WillByDefault(Return(&base_route_->route_entry_));
  // A cluster is set so the route is wrapped, but no action override is selected.
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"x-route-action", "override"}, {"x-cluster", "some-cluster"}};
  auto route = onRoute(headers);
  ASSERT_NE(nullptr, route);
  const auto* entry = route->routeEntry();
  ASSERT_NE(nullptr, entry);
  EXPECT_EQ("some-cluster", entry->clusterName());
  EXPECT_EQ(base_route_->route_entry_.retryPolicy(), entry->retryPolicy());
  EXPECT_EQ(base_route_->route_entry_.metadataMatchCriteria(), entry->metadataMatchCriteria());
  EXPECT_EQ(base_route_->route_entry_.hashPolicy(), entry->hashPolicy());
  EXPECT_EQ(base_route_->route_entry_.shadowPolicies().size(), entry->shadowPolicies().size());
}

// The module returns Override with no matched route, so the null route flows through unchanged.
TEST_F(DynamicModuleRouteExtensionTest, KeepsNullRouteOnOverride) {
  setUpExtension();
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"x-route-action", "override"}, {"x-cluster", "some-cluster"}};
  EXPECT_EQ(nullptr, extension_->onRoute(nullptr, headers, stream_info_, 0));
}

// A mirror cluster named through a request header is resolved per request, so it is not checked
// against the cluster manager.
TEST_F(DynamicModuleRouteExtensionTest, HeaderNamedShadowClusterIsNotValidated) {
  setUpExtension("", R"EOF(
route_action_overrides:
  canary:
    request_mirror_policies:
    - cluster_header: x-mirror
)EOF");
  EXPECT_TRUE(extension_->validateClusters(context_.cluster_manager_).ok());
}

// Selecting an override that is not declared records nothing and logs a warning, so the route
// action properties of the matched route stay in effect.
TEST_F(DynamicModuleRouteExtensionTest, UnknownRouteActionOverrideKeepsRouteAction) {
  setUpExtension("", R"EOF(
route_action_overrides:
  canary:
    retry_policy:
      num_retries: 3
)EOF");
  ON_CALL(*base_route_, routeEntry()).WillByDefault(Return(&base_route_->route_entry_));
  Http::TestRequestHeaderMapImpl headers{{":path", "/"},
                                         {"x-route-action", "override"},
                                         {"x-cluster", "some-cluster"},
                                         {"x-action-override", "unknown"}};
  Envoy::Router::RouteConstSharedPtr route;
  EXPECT_LOG_CONTAINS("warn", "unknown route action override 'unknown'",
                      { route = onRoute(headers); });
  ASSERT_NE(nullptr, route);
  // The cluster override still applies, but the unknown action override records nothing, so the
  // route action properties delegate to the matched route.
  EXPECT_EQ("some-cluster", route->routeEntry()->clusterName());
  EXPECT_EQ(base_route_->route_entry_.retryPolicy(), route->routeEntry()->retryPolicy());
}

// A statically named mirror cluster that the cluster manager does not know is rejected.
TEST_F(DynamicModuleRouteExtensionTest, RejectsUnknownShadowClusterWhenValidating) {
  setUpExtension("", R"EOF(
route_action_overrides:
  canary:
    request_mirror_policies:
    - cluster: missing-shadow
)EOF");
  const auto status = extension_->validateClusters(context_.cluster_manager_);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), testing::HasSubstr("unknown shadow cluster 'missing-shadow'"));
}

TEST_F(DynamicModuleRouteExtensionTest, AcceptsKnownShadowClusterWhenValidating) {
  setUpExtension("", R"EOF(
route_action_overrides:
  canary:
    request_mirror_policies:
    - cluster: known-shadow
)EOF");
  ON_CALL(context_.cluster_manager_, hasCluster("known-shadow")).WillByDefault(Return(true));
  EXPECT_TRUE(extension_->validateClusters(context_.cluster_manager_).ok());
}

// A configuration with no overrides has no clusters to validate.
TEST_F(DynamicModuleRouteExtensionTest, ValidatesEmptyOverridesIsOk) {
  setUpExtension();
  EXPECT_TRUE(extension_->validateClusters(context_.cluster_manager_).ok());
}

} // namespace
} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy

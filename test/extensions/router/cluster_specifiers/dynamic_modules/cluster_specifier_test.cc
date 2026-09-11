#include <array>
#include <limits>

#include "envoy/extensions/router/cluster_specifiers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

#include "source/common/common/fmt.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/router/cluster_specifiers/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {
namespace {

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

void mockCustomStatNamespaces(Server::Configuration::MockServerFactoryContext& context,
                              Stats::CustomStatNamespacesImpl& custom_stat_namespaces) {
  ON_CALL(context.api_, customStatNamespaces())
      .WillByDefault(testing::ReturnRef(custom_stat_namespaces));
}

envoy_dynamic_module_type_module_buffer metricBuffer(absl::string_view value) {
  return {.ptr = value.data(), .length = value.size()};
}

// Builds a proto config that loads the named module with the given in-module specifier name.
DynamicModuleClusterSpecifierProto protoConfig(absl::string_view module_name,
                                               absl::string_view specifier_name) {
  const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
  name: {}
  do_not_close: true
specifier_name: {}
)EOF",
                                       module_name, specifier_name);
  DynamicModuleClusterSpecifierProto proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  return proto_config;
}

// Tests for the factory using minimal C modules to drive symbol resolution and config lifecycle.
class DynamicModuleClusterSpecifierFactoryTest : public testing::Test {
public:
  DynamicModuleClusterSpecifierFactoryTest() {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
    mockCustomStatNamespaces(context_, custom_stat_namespaces_);
  }

  // Returns the number of times the no-op module has been handed its configuration back, so that a
  // test can observe the config destroy hook. Holding the module reference keeps the counter alive,
  // since the plugin and this handle share one dlopen of the same path.
  std::function<int()> configDestroyCounter() {
    using GetConfigDestroyCountFuncType = int (*)(void);
    auto module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("cluster_specifier_no_op", "c"),
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
  DynamicModuleClusterSpecifierPluginFactoryConfig factory_;
};

TEST_F(DynamicModuleClusterSpecifierFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.router.cluster_specifier_plugin.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleClusterSpecifierFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  ASSERT_NE(nullptr, proto);
  EXPECT_NE(nullptr, dynamic_cast<DynamicModuleClusterSpecifierProto*>(proto.get()));
}

TEST_F(DynamicModuleClusterSpecifierFactoryTest, FactoryRegistration) {
  auto* factory =
      Registry::FactoryRegistry<Envoy::Router::ClusterSpecifierPluginFactoryConfig>::getFactory(
          "envoy.router.cluster_specifier_plugin.dynamic_modules");
  EXPECT_NE(nullptr, factory);
}

TEST_F(DynamicModuleClusterSpecifierFactoryTest, ValidConfig) {
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  EXPECT_NE(nullptr, factory_.createClusterSpecifierPlugin(proto_config, context_));
}

// Releasing the plugin must hand the in-module configuration back to the module.
TEST_F(DynamicModuleClusterSpecifierFactoryTest, ReleasingPluginDestroysInModuleConfig) {
  const auto destroy_count = configDestroyCounter();
  const int before = destroy_count();
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  auto plugin = factory_.createClusterSpecifierPlugin(proto_config, context_);
  ASSERT_NE(nullptr, plugin);
  EXPECT_EQ(before, destroy_count());
  plugin.reset();
  EXPECT_EQ(before + 1, destroy_count());
}

// A route the plugin resolved keeps the module configuration alive, since a route config update can
// release the last plugin reference while a request still holds the route.
TEST_F(DynamicModuleClusterSpecifierFactoryTest, ResolvedRouteKeepsConfigAlive) {
  const auto destroy_count = configDestroyCounter();
  const int before = destroy_count();
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  auto plugin = factory_.createClusterSpecifierPlugin(proto_config, context_);
  ASSERT_NE(nullptr, plugin);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  auto parent = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}};
  auto route = plugin->route(parent, headers, stream_info, 42);
  ASSERT_NE(nullptr, route);

  plugin.reset();
  EXPECT_EQ(before, destroy_count());
  // The module is still reachable through the route, so a refresh must not call through a freed
  // configuration.
  route->routeEntry()->refreshRouteCluster(headers, stream_info);
  route.reset();
  EXPECT_EQ(before + 1, destroy_count());
}

TEST_F(DynamicModuleClusterSpecifierFactoryTest, InvalidModule) {
  auto proto_config = protoConfig("nonexistent_module", "test_cluster_specifier");
  EXPECT_THROW_WITH_REGEX(factory_.createClusterSpecifierPlugin(proto_config, context_),
                          EnvoyException, "Failed to load.*");
  EXPECT_EQ(1U, Extensions::DynamicModules::failureCounter(context_.scope(), "module_load_error",
                                                           "test_cluster_specifier"));
}

TEST_F(DynamicModuleClusterSpecifierFactoryTest, MissingConfigNew) {
  auto proto_config = protoConfig("cluster_specifier_missing_config_new", "test_cluster_specifier");
  EXPECT_THROW_WITH_REGEX(factory_.createClusterSpecifierPlugin(proto_config, context_),
                          EnvoyException, "Failed to create cluster specifier config.*config_new");
}

TEST_F(DynamicModuleClusterSpecifierFactoryTest, MissingConfigDestroy) {
  auto proto_config =
      protoConfig("cluster_specifier_missing_config_destroy", "test_cluster_specifier");
  EXPECT_THROW_WITH_REGEX(factory_.createClusterSpecifierPlugin(proto_config, context_),
                          EnvoyException,
                          "Failed to create cluster specifier config.*config_destroy");
}

TEST_F(DynamicModuleClusterSpecifierFactoryTest, MissingSelect) {
  auto proto_config = protoConfig("cluster_specifier_missing_select", "test_cluster_specifier");
  EXPECT_THROW_WITH_REGEX(factory_.createClusterSpecifierPlugin(proto_config, context_),
                          EnvoyException, "Failed to create cluster specifier config.*select");
}

TEST_F(DynamicModuleClusterSpecifierFactoryTest, ConfigNewReturnsNull) {
  auto proto_config = protoConfig("cluster_specifier_config_new_fail", "test_cluster_specifier");
  EXPECT_THROW_WITH_REGEX(factory_.createClusterSpecifierPlugin(proto_config, context_),
                          EnvoyException,
                          "Failed to create cluster specifier config.*Failed to initialize");
}

// A specifier config that cannot be parsed as its declared type is rejected.
TEST_F(DynamicModuleClusterSpecifierFactoryTest, SpecifierConfigMalformedRejected) {
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  auto* specifier_config = proto_config.mutable_specifier_config();
  specifier_config->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  specifier_config->set_value(std::string("\x0a\x05\x41", 3));
  EXPECT_THROW_WITH_REGEX(factory_.createClusterSpecifierPlugin(proto_config, context_),
                          EnvoyException, "Failed to create cluster specifier config.*");
}

// An invalid retry policy inside a route action override is rejected at config load.
TEST_F(DynamicModuleClusterSpecifierFactoryTest, InvalidRetryPolicyOverrideRejected) {
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  auto& entry = (*proto_config.mutable_route_action_overrides())["bad"];
  auto* back_off = entry.mutable_retry_policy()->mutable_retry_back_off();
  back_off->mutable_base_interval()->set_seconds(2);
  back_off->mutable_max_interval()->set_seconds(1);
  EXPECT_THROW_WITH_REGEX(factory_.createClusterSpecifierPlugin(proto_config, context_),
                          EnvoyException,
                          "Failed to create cluster specifier config.*max_interval");
}

// An invalid request mirror policy inside a route action override is rejected at config load.
TEST_F(DynamicModuleClusterSpecifierFactoryTest, InvalidMirrorPolicyOverrideRejected) {
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  auto& entry = (*proto_config.mutable_route_action_overrides())["bad"];
  auto* mirror_policy = entry.add_request_mirror_policies();
  mirror_policy->set_cluster("mirror-cluster");
  mirror_policy->set_cluster_header("x-mirror");
  EXPECT_THROW_WITH_REGEX(factory_.createClusterSpecifierPlugin(proto_config, context_),
                          EnvoyException, "Failed to create cluster specifier config.*");
}

// An override that replaces nothing is a configuration mistake rather than a silent no-op.
TEST_F(DynamicModuleClusterSpecifierFactoryTest, EmptyOverrideRejected) {
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  (*proto_config.mutable_route_action_overrides())["empty"] = {};
  EXPECT_THROW_WITH_REGEX(factory_.createClusterSpecifierPlugin(proto_config, context_),
                          EnvoyException,
                          "Route action override must replace at least one route action property");
}

// Only the envoy.lb entry of a metadata match is used, so an override whose only property is a
// metadata match without one replaces nothing even though the proto looks populated.
TEST_F(DynamicModuleClusterSpecifierFactoryTest, OverrideWithNonLoadBalancingMetadataOnlyRejected) {
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  auto& entry = (*proto_config.mutable_route_action_overrides())["hollow"];
  Protobuf::Struct metadata;
  (*metadata.mutable_fields())["shard"] = ValueUtil::stringValue("a");
  (*entry.mutable_metadata_match()->mutable_filter_metadata())["envoy.not_lb"] = metadata;
  EXPECT_THROW_WITH_REGEX(factory_.createClusterSpecifierPlugin(proto_config, context_),
                          EnvoyException,
                          "Route action override must replace at least one route action property");
}

// The route level cluster validation only walks the mirror policies of the matched route, so the
// plugin has to validate the ones its overrides carry.
TEST_F(DynamicModuleClusterSpecifierFactoryTest, UnknownMirrorClusterInOverrideRejected) {
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  (*proto_config.mutable_route_action_overrides())["mirror"]
      .add_request_mirror_policies()
      ->set_cluster("absent-cluster");
  auto plugin = factory_.createClusterSpecifierPlugin(proto_config, context_);
  EXPECT_CALL(context_.cluster_manager_, hasCluster(absl::string_view("absent-cluster")))
      .WillOnce(Return(false));
  EXPECT_THAT(plugin->validateClusters(context_.cluster_manager_).message(),
              testing::HasSubstr("route action override 'mirror': unknown shadow cluster "
                                 "'absent-cluster'"));
}

TEST_F(DynamicModuleClusterSpecifierFactoryTest, KnownMirrorClusterInOverrideAccepted) {
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  (*proto_config.mutable_route_action_overrides())["mirror"]
      .add_request_mirror_policies()
      ->set_cluster("present-cluster");
  auto plugin = factory_.createClusterSpecifierPlugin(proto_config, context_);
  EXPECT_CALL(context_.cluster_manager_, hasCluster(absl::string_view("present-cluster")))
      .WillOnce(Return(true));
  EXPECT_TRUE(plugin->validateClusters(context_.cluster_manager_).ok());
}

// A mirror policy that names its cluster through a header resolves it per request, so there is
// nothing to look up at configuration load.
TEST_F(DynamicModuleClusterSpecifierFactoryTest, HeaderNamedMirrorClusterNotValidated) {
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  (*proto_config.mutable_route_action_overrides())["mirror"]
      .add_request_mirror_policies()
      ->set_cluster_header("x-mirror");
  auto plugin = factory_.createClusterSpecifierPlugin(proto_config, context_);
  EXPECT_CALL(context_.cluster_manager_, hasCluster(testing::_)).Times(0);
  EXPECT_TRUE(plugin->validateClusters(context_.cluster_manager_).ok());
}

// An override without mirror policies has no cluster to validate.
TEST_F(DynamicModuleClusterSpecifierFactoryTest, OverrideWithoutMirrorPolicyNotValidated) {
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  (*proto_config.mutable_route_action_overrides())["retry"].mutable_retry_policy()->set_retry_on(
      "5xx");
  auto plugin = factory_.createClusterSpecifierPlugin(proto_config, context_);
  EXPECT_CALL(context_.cluster_manager_, hasCluster(testing::_)).Times(0);
  EXPECT_TRUE(plugin->validateClusters(context_.cluster_manager_).ok());
}

// An override key must not be empty.
TEST_F(DynamicModuleClusterSpecifierFactoryTest, EmptyOverrideKeyRejected) {
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  (*proto_config.mutable_route_action_overrides())[""] = {};
  EXPECT_THROW_WITH_REGEX(factory_.createClusterSpecifierPlugin(proto_config, context_),
                          ProtoValidationException, "value length must be at least 1");
}

// The dynamic module config is required.
TEST_F(DynamicModuleClusterSpecifierFactoryTest, MissingDynamicModuleConfigRejected) {
  DynamicModuleClusterSpecifierProto proto_config;
  EXPECT_THROW_WITH_REGEX(factory_.createClusterSpecifierPlugin(proto_config, context_),
                          ProtoValidationException, "value is required");
}

// A failure to initialize the in-module configuration is counted.
TEST_F(DynamicModuleClusterSpecifierFactoryTest, ConfigInitFailureIncrementsStat) {
  EXPECT_EQ(0U, Extensions::DynamicModules::failureCounter(context_.scope(), "config_init_error",
                                                           "test_cluster_specifier"));
  auto proto_config = protoConfig("cluster_specifier_config_new_fail", "test_cluster_specifier");
  EXPECT_THROW(factory_.createClusterSpecifierPlugin(proto_config, context_), EnvoyException);
  EXPECT_EQ(1U, Extensions::DynamicModules::failureCounter(context_.scope(), "config_init_error",
                                                           "test_cluster_specifier"));
}

// Tests that drive the full selection path through the reference Rust module. These exercise every
// cluster specifier callback and every route action property the module can replace.
class DynamicModuleClusterSpecifierTest : public testing::Test {
public:
  DynamicModuleClusterSpecifierTest() {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
    mockCustomStatNamespaces(context_, custom_stat_namespaces_);
  }

  // Creates the plugin from a YAML fragment appended to the base module configuration.
  void setUpPlugin(absl::string_view extra_config = "") {
    const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
  name: cluster_specifier_integration_test
  do_not_close: true
specifier_name: test_cluster_specifier
{}
)EOF",
                                         extra_config);
    DynamicModuleClusterSpecifierProto proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    plugin_ = factory_.createClusterSpecifierPlugin(proto_config, context_);
    ASSERT_NE(nullptr, plugin_);
  }

  // Resolves the route for the given request headers and keeps it alive for the rest of the test.
  // The matched route is a mock so that delegation to its properties is observable.
  const Envoy::Router::RouteEntry* resolveRouteEntry(Http::RequestHeaderMap& headers) {
    resolved_route_ = plugin_->route(parent_, headers, stream_info_, 42);
    return resolved_route_->routeEntry();
  }

  // Gives the matched route criteria to fall back to. The mock returns none by default, which would
  // make a fallback assertion pass even if the override stopped falling back.
  const Envoy::Router::MetadataMatchCriteria* giveMatchedRouteMetadataMatchCriteria() {
    EXPECT_CALL(parent_->route_entry_, metadataMatchCriteria())
        .WillRepeatedly(Return(&parent_->route_entry_.metadata_matches_criteria_));
    return &parent_->route_entry_.metadata_matches_criteria_;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Stats::CustomStatNamespacesImpl custom_stat_namespaces_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::shared_ptr<NiceMock<Envoy::Router::MockRoute>> parent_{
      std::make_shared<NiceMock<Envoy::Router::MockRoute>>()};
  DynamicModuleClusterSpecifierPluginFactoryConfig factory_;
  Envoy::Router::ClusterSpecifierPluginSharedPtr plugin_;
  Envoy::Router::RouteConstSharedPtr resolved_route_;
};

TEST_F(DynamicModuleClusterSpecifierTest, SelectsClusterFromRequestHeader) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  EXPECT_LOG_CONTAINS("debug", "selected cluster 'prod'",
                      { EXPECT_EQ("prod", resolveRouteEntry(headers)->clusterName()); });
}

// The specifier config bytes reach the module and are used to build the cluster name.
TEST_F(DynamicModuleClusterSpecifierTest, SelectsClusterUsingSpecifierConfig) {
  setUpPlugin(R"EOF(
specifier_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: "shard-"
)EOF");
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  EXPECT_EQ("shard-prod", resolveRouteEntry(headers)->clusterName());
}

// A specifier config that is not a StringValue reaches the module as its JSON serialization.
TEST_F(DynamicModuleClusterSpecifierTest, SelectsClusterUsingStructSpecifierConfig) {
  setUpPlugin(R"EOF(
specifier_config:
  "@type": type.googleapis.com/google.protobuf.Struct
  value:
    prefix: shard-
)EOF");
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  EXPECT_EQ(R"({"prefix":"shard-"}prod)", resolveRouteEntry(headers)->clusterName());
}

// The selected cluster name is held by value rather than in an optional, so a later selection that
// names no cluster leaves any reference the router already took valid.
TEST_F(DynamicModuleClusterSpecifierTest, EmptyClusterNameDelegatesAndKeepsReferenceValid) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  const auto* entry = resolveRouteEntry(headers);
  const std::string& cluster_name = entry->clusterName();
  EXPECT_EQ("prod", cluster_name);

  headers.setCopy(Http::LowerCaseString("env"), "");
  entry->refreshRouteCluster(headers, stream_info_);
  EXPECT_EQ(parent_->route_entry_.cluster_name_, entry->clusterName());
  EXPECT_TRUE(cluster_name.empty());
}

// Without a decision from the module the properties of the matched route stay in effect. The
// outcome is logged because it is otherwise indistinguishable from a cluster that does not exist.
TEST_F(DynamicModuleClusterSpecifierTest, NoDecisionDelegatesToMatchedRoute) {
  setUpPlugin();
  parent_->route_entry_.cluster_name_ = "matched-cluster";
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("debug", "reached no cluster selection",
                      { EXPECT_EQ("matched-cluster", resolveRouteEntry(headers)->clusterName()); });
}

// A refresh re-enters the module so that a later decision replaces the earlier one.
TEST_F(DynamicModuleClusterSpecifierTest, RefreshAppliesNewDecision) {
  setUpPlugin();
  parent_->route_entry_.cluster_name_ = "matched-cluster";
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ("matched-cluster", entry->clusterName());

  headers.setCopy(Http::LowerCaseString("env"), "prod");
  entry->refreshRouteCluster(headers, stream_info_);
  EXPECT_EQ("prod", entry->clusterName());
}

// A refresh where the module reports no decision keeps the earlier decision in effect.
TEST_F(DynamicModuleClusterSpecifierTest, RefreshWithoutDecisionKeepsPreviousSelection) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ("prod", entry->clusterName());

  headers.remove(Http::LowerCaseString("env"));
  entry->refreshRouteCluster(headers, stream_info_);
  EXPECT_EQ("prod", entry->clusterName());
}

TEST_F(DynamicModuleClusterSpecifierTest, ScalarPropertiesDelegateByDefault) {
  setUpPlugin();
  EXPECT_CALL(parent_->route_entry_, timeout())
      .WillRepeatedly(Return(std::chrono::milliseconds(11)));
  EXPECT_CALL(parent_->route_entry_, idleTimeout())
      .WillRepeatedly(Return(std::chrono::milliseconds(22)));
  EXPECT_CALL(parent_->route_entry_, requestBodyBufferLimit()).WillRepeatedly(Return(33));
  EXPECT_CALL(parent_->route_entry_, priority())
      .WillRepeatedly(Return(Upstream::ResourcePriority::High));

  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ(std::chrono::milliseconds(11), entry->timeout());
  EXPECT_EQ(std::chrono::milliseconds(22), entry->idleTimeout());
  EXPECT_EQ(33, entry->requestBodyBufferLimit());
  EXPECT_EQ(Upstream::ResourcePriority::High, entry->priority());
}

TEST_F(DynamicModuleClusterSpecifierTest, ScalarPropertiesReplaced) {
  setUpPlugin();
  EXPECT_CALL(parent_->route_entry_, timeout()).Times(0);
  EXPECT_CALL(parent_->route_entry_, idleTimeout()).Times(0);
  EXPECT_CALL(parent_->route_entry_, requestBodyBufferLimit()).Times(0);
  EXPECT_CALL(parent_->route_entry_, priority()).Times(0);

  Http::TestRequestHeaderMapImpl headers{{":path", "/"},
                                         {"env", "prod"},
                                         {"x-timeout-ms", "1234"},
                                         {"x-idle-timeout-ms", "4321"},
                                         {"x-buffer-limit", "9999"},
                                         {"x-priority", "high"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ(std::chrono::milliseconds(1234), entry->timeout());
  EXPECT_EQ(std::chrono::milliseconds(4321), entry->idleTimeout());
  EXPECT_EQ(9999, entry->requestBodyBufferLimit());
  EXPECT_EQ(Upstream::ResourcePriority::High, entry->priority());
}

// A zero timeout disables the timeout, so it must be reported instead of falling back to the
// matched route.
TEST_F(DynamicModuleClusterSpecifierTest, ZeroScalarPropertiesReplaced) {
  setUpPlugin();
  EXPECT_CALL(parent_->route_entry_, timeout()).Times(0);
  EXPECT_CALL(parent_->route_entry_, idleTimeout()).Times(0);
  EXPECT_CALL(parent_->route_entry_, requestBodyBufferLimit()).Times(0);

  Http::TestRequestHeaderMapImpl headers{{":path", "/"},
                                         {"env", "prod"},
                                         {"x-timeout-ms", "0"},
                                         {"x-idle-timeout-ms", "0"},
                                         {"x-buffer-limit", "0"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ(std::chrono::milliseconds(0), entry->timeout());
  EXPECT_EQ(std::chrono::milliseconds(0), entry->idleTimeout());
  EXPECT_EQ(0, entry->requestBodyBufferLimit());
}

// A refresh replaces the whole decision, so scalars the new decision leaves unset go back to the
// matched route rather than keeping what an earlier call set.
TEST_F(DynamicModuleClusterSpecifierTest, RefreshWithoutScalarsRevertsToMatchedRoute) {
  setUpPlugin();
  const std::chrono::milliseconds route_timeout(77);
  const uint64_t route_buffer_limit = 4096;
  ON_CALL(parent_->route_entry_, timeout()).WillByDefault(Return(route_timeout));
  ON_CALL(parent_->route_entry_, requestBodyBufferLimit())
      .WillByDefault(Return(route_buffer_limit));

  Http::TestRequestHeaderMapImpl headers{{":path", "/"},
                                         {"env", "prod"},
                                         {"x-timeout-ms", "1234"},
                                         {"x-buffer-limit", "9999"},
                                         {"x-priority", "high"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ(std::chrono::milliseconds(1234), entry->timeout());
  EXPECT_EQ(9999, entry->requestBodyBufferLimit());
  EXPECT_EQ(Upstream::ResourcePriority::High, entry->priority());

  headers.remove(Http::LowerCaseString("x-timeout-ms"));
  headers.remove(Http::LowerCaseString("x-buffer-limit"));
  headers.remove(Http::LowerCaseString("x-priority"));
  entry->refreshRouteCluster(headers, stream_info_);
  EXPECT_EQ(route_timeout, entry->timeout());
  EXPECT_EQ(route_buffer_limit, entry->requestBodyBufferLimit());
  EXPECT_EQ(parent_->route_entry_.priority(), entry->priority());
}

// A duration the millisecond representation cannot hold is clamped rather than wrapping to a
// negative duration, which would trip the timer implementation.
TEST_F(DynamicModuleClusterSpecifierTest, OutOfRangeDurationsClamped) {
  setUpPlugin();
  // The mock returns the maximum buffer limit by default, so the values below are only meaningful
  // if the matched route is never consulted for them.
  EXPECT_CALL(parent_->route_entry_, timeout()).Times(0);
  EXPECT_CALL(parent_->route_entry_, idleTimeout()).Times(0);
  EXPECT_CALL(parent_->route_entry_, requestBodyBufferLimit()).Times(0);

  Http::TestRequestHeaderMapImpl headers{{":path", "/"},
                                         {"env", "prod"},
                                         {"x-timeout-ms", "max"},
                                         {"x-idle-timeout-ms", "max"},
                                         {"x-buffer-limit", "max"}};
  // Durations are clamped to the ceiling that the timer implementation imposes rather than to the
  // millisecond representation maximum, which would overflow the scale factor of the idle timer.
  const auto max_duration =
      std::chrono::milliseconds(std::chrono::seconds(std::numeric_limits<int32_t>::max()));
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ(max_duration, entry->timeout());
  EXPECT_EQ(max_duration, entry->idleTimeout());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(), entry->requestBodyBufferLimit());
}

// The default priority is replaced as well, so the matched route is not consulted for it.
TEST_F(DynamicModuleClusterSpecifierTest, DefaultPriorityReplaced) {
  setUpPlugin();
  EXPECT_CALL(parent_->route_entry_, priority()).Times(0);
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-priority", "default"}};
  EXPECT_EQ(Upstream::ResourcePriority::Default, resolveRouteEntry(headers)->priority());
}

TEST_F(DynamicModuleClusterSpecifierTest, ClusterNotFoundResponseCodeDelegatesByDefault) {
  setUpPlugin();
  EXPECT_CALL(parent_->route_entry_, clusterNotFoundResponseCode())
      .WillRepeatedly(Return(Http::Code::InternalServerError));
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  EXPECT_EQ(Http::Code::InternalServerError,
            resolveRouteEntry(headers)->clusterNotFoundResponseCode());
}

TEST_F(DynamicModuleClusterSpecifierTest, ClusterNotFoundResponseCodeReplaced) {
  setUpPlugin();
  EXPECT_CALL(parent_->route_entry_, clusterNotFoundResponseCode()).Times(0);
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-not-found-code", "404"}};
  EXPECT_EQ(Http::Code::NotFound, resolveRouteEntry(headers)->clusterNotFoundResponseCode());
}

// The code is only read once the selected cluster turns out to be missing, so an invalid one would
// otherwise surface as a malformed response far from the module call that set it.
TEST_F(DynamicModuleClusterSpecifierTest, OutOfRangeClusterNotFoundResponseCodeRejected) {
  setUpPlugin();
  EXPECT_CALL(parent_->route_entry_, clusterNotFoundResponseCode())
      .WillRepeatedly(Return(Http::Code::ServiceUnavailable));
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-not-found-code", "max"}};
  EXPECT_LOG_CONTAINS("warn", "out of range cluster not found response code", {
    EXPECT_EQ(Http::Code::ServiceUnavailable,
              resolveRouteEntry(headers)->clusterNotFoundResponseCode());
  });
}

// A refresh replaces the whole decision, so a response code an earlier call set goes back to the
// matched route.
TEST_F(DynamicModuleClusterSpecifierTest, RefreshWithoutResponseCodeRevertsToMatchedRoute) {
  setUpPlugin();
  ON_CALL(parent_->route_entry_, clusterNotFoundResponseCode())
      .WillByDefault(Return(Http::Code::ServiceUnavailable));
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-not-found-code", "404"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ(Http::Code::NotFound, entry->clusterNotFoundResponseCode());

  headers.remove(Http::LowerCaseString("x-not-found-code"));
  entry->refreshRouteCluster(headers, stream_info_);
  EXPECT_EQ(Http::Code::ServiceUnavailable, entry->clusterNotFoundResponseCode());
}

// Tests that assert the values the module read through the context callbacks. The module echoes the
// value named by the x-echo header into the cluster name, so every read is observable.
class DynamicModuleClusterSpecifierReadTest : public DynamicModuleClusterSpecifierTest {
public:
  std::string echoedValue(Http::RequestHeaderMap& headers, absl::string_view accessor) {
    headers.setCopy(Http::LowerCaseString("x-echo"), accessor);
    return resolveRouteEntry(headers)->clusterName();
  }
};

TEST_F(DynamicModuleClusterSpecifierReadTest, RequestHeaderCountReadable) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-echo", "header-count"}};
  headers.addCopy(Http::LowerCaseString("x-multi"), "first");
  headers.addCopy(Http::LowerCaseString("x-multi"), "second");
  EXPECT_EQ(absl::StrCat(headers.size()), resolveRouteEntry(headers)->clusterName());
}

TEST_F(DynamicModuleClusterSpecifierReadTest, RequestHeaderValuesReadable) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  headers.addCopy(Http::LowerCaseString("x-multi"), "first");
  headers.addCopy(Http::LowerCaseString("x-multi"), "second");
  EXPECT_EQ("first", echoedValue(headers, "header-value"));
  EXPECT_EQ("second", echoedValue(headers, "header-value-index"));
  EXPECT_EQ("first", echoedValue(headers, "header-bulk"));
  EXPECT_EQ("2", echoedValue(headers, "header-value-total"));
}

TEST_F(DynamicModuleClusterSpecifierReadTest, AbsentRequestHeaderReadable) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  EXPECT_EQ("absent", echoedValue(headers, "header-value"));
  EXPECT_EQ("absent", echoedValue(headers, "header-value-index"));
  EXPECT_EQ("absent", echoedValue(headers, "header-bulk"));
  EXPECT_EQ("absent", echoedValue(headers, "header-value-total"));
}

TEST_F(DynamicModuleClusterSpecifierReadTest, StreamInfoAttributesReadable) {
  setUpPlugin();
  stream_info_.protocol_ = Http::Protocol::Http2;
  stream_info_.attempt_count_ = 3;
  EXPECT_CALL(stream_info_, healthCheck()).WillRepeatedly(Return(true));
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  EXPECT_EQ("HTTP/2", echoedValue(headers, "attribute-string"));
  EXPECT_EQ("3", echoedValue(headers, "attribute-int"));
  EXPECT_EQ("true", echoedValue(headers, "attribute-bool"));
  EXPECT_EQ("absent", echoedValue(headers, "attribute-string-unset"));
}

TEST_F(DynamicModuleClusterSpecifierReadTest, DynamicMetadataReadable) {
  setUpPlugin();
  Protobuf::Struct metadata;
  (*metadata.mutable_fields())["shard"] = ValueUtil::stringValue("a");
  (*stream_info_.metadata_.mutable_filter_metadata())["envoy.test"] = metadata;
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  EXPECT_EQ("a", echoedValue(headers, "dynamic-metadata"));
}

TEST_F(DynamicModuleClusterSpecifierReadTest, AbsentDynamicMetadataReadable) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  EXPECT_EQ("absent", echoedValue(headers, "dynamic-metadata"));
  EXPECT_EQ("absent", echoedValue(headers, "dynamic-metadata-number"));
  EXPECT_EQ("absent", echoedValue(headers, "dynamic-metadata-bool"));
}

TEST_F(DynamicModuleClusterSpecifierReadTest, NumberAndBoolDynamicMetadataReadable) {
  setUpPlugin();
  Protobuf::Struct metadata;
  (*metadata.mutable_fields())["weight"] = ValueUtil::numberValue(7);
  (*metadata.mutable_fields())["enabled"] = ValueUtil::boolValue(true);
  (*stream_info_.metadata_.mutable_filter_metadata())["envoy.test"] = metadata;
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  EXPECT_EQ("7", echoedValue(headers, "dynamic-metadata-number"));
  EXPECT_EQ("true", echoedValue(headers, "dynamic-metadata-bool"));
}

// A value of the wrong type is not returned, so a module cannot read a string as a number.
TEST_F(DynamicModuleClusterSpecifierReadTest, MistypedDynamicMetadataNotReadable) {
  setUpPlugin();
  Protobuf::Struct metadata;
  (*metadata.mutable_fields())["weight"] = ValueUtil::stringValue("7");
  (*metadata.mutable_fields())["enabled"] = ValueUtil::stringValue("true");
  (*stream_info_.metadata_.mutable_filter_metadata())["envoy.test"] = metadata;
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  EXPECT_EQ("absent", echoedValue(headers, "dynamic-metadata-number"));
  EXPECT_EQ("absent", echoedValue(headers, "dynamic-metadata-bool"));
}

TEST_F(DynamicModuleClusterSpecifierReadTest, RouteNameReadable) {
  setUpPlugin();
  const std::string route_name = "named-route";
  EXPECT_CALL(*parent_, routeName()).WillRepeatedly(ReturnRef(route_name));
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  EXPECT_EQ("named-route", echoedValue(headers, "route-name"));
}

// A route with no name is reported as absent rather than as an empty value.
TEST_F(DynamicModuleClusterSpecifierReadTest, UnnamedRouteReadable) {
  setUpPlugin();
  const std::string route_name;
  EXPECT_CALL(*parent_, routeName()).WillRepeatedly(ReturnRef(route_name));
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  EXPECT_EQ("absent", echoedValue(headers, "route-name"));
}

// The random value Envoy generated for the request is readable and is stable across a refresh.
TEST_F(DynamicModuleClusterSpecifierReadTest, RandomValueReadable) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-echo", "random-value"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ("42", entry->clusterName());
  entry->refreshRouteCluster(headers, stream_info_);
  EXPECT_EQ("42", entry->clusterName());
}

// Tests for the route action overrides that replace the object-valued route action properties.
class DynamicModuleRouteActionOverrideTest : public DynamicModuleClusterSpecifierTest {
public:
  void setUpPluginWithOverrides() {
    setUpPlugin(R"EOF(
route_action_overrides:
  full:
    retry_policy:
      retry_on: 5xx
      num_retries: 7
      retry_back_off:
        base_interval: 0.05s
        max_interval: 0.5s
      retry_host_predicate:
      - name: envoy.retry_host_predicates.previous_hosts
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.retry.host.previous_hosts.v3.PreviousHostsPredicate
    metadata_match:
      filter_metadata:
        envoy.lb:
          version: canary
    request_mirror_policies:
    - cluster: mirror-cluster
  mirror_only:
    request_mirror_policies:
    - cluster: mirror-cluster
  hash_only:
    hash_policy:
    - header:
        header_name: x-hash
)EOF");
  }
};

TEST_F(DynamicModuleRouteActionOverrideTest, HashPolicyOverrideReplacesRouteHashPolicy) {
  setUpPluginWithOverrides();
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-override", "hash_only"}, {"x-hash", "tenant-a"}};
  const auto* entry = resolveRouteEntry(headers);
  ASSERT_NE(nullptr, entry->hashPolicy());
  Http::HashPolicy::AddCookieCallback add_cookie_nop;
  EXPECT_TRUE(entry->hashPolicy()->generateHash(headers, stream_info_, add_cookie_nop).has_value());
}

TEST_F(DynamicModuleRouteActionOverrideTest, HashPolicyOverrideDelegatesWhenNotSelected) {
  setUpPluginWithOverrides();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  const auto* matched_hash_policy = parent_->route_entry_.hashPolicy();
  EXPECT_CALL(parent_->route_entry_, hashPolicy()).WillOnce(Return(matched_hash_policy));
  EXPECT_EQ(matched_hash_policy, resolveRouteEntry(headers)->hashPolicy());
}

TEST_F(DynamicModuleRouteActionOverrideTest, OverrideReplacesRouteActionProperties) {
  setUpPluginWithOverrides();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}, {"x-override", "full"}};
  const auto* entry = resolveRouteEntry(headers);

  ASSERT_NE(nullptr, entry->retryPolicy());
  EXPECT_EQ(7, entry->retryPolicy()->numRetries());
  EXPECT_EQ(std::chrono::milliseconds(50), entry->retryPolicy()->baseInterval());
  EXPECT_EQ(std::chrono::milliseconds(500), entry->retryPolicy()->maxInterval());
  EXPECT_EQ(1, entry->retryPolicy()->retryHostPredicates().size());

  ASSERT_NE(nullptr, entry->metadataMatchCriteria());
  ASSERT_EQ(1, entry->metadataMatchCriteria()->metadataMatchCriteria().size());
  EXPECT_EQ("version", entry->metadataMatchCriteria()->metadataMatchCriteria()[0]->name());

  ASSERT_EQ(1, entry->shadowPolicies().size());
  EXPECT_EQ("mirror-cluster", entry->shadowPolicies()[0]->cluster());
}

// An override replaces only the properties it sets, so the rest stay with the matched route.
TEST_F(DynamicModuleRouteActionOverrideTest, PartialOverrideDelegatesRemainingProperties) {
  setUpPluginWithOverrides();
  const auto* matched_route_criteria = giveMatchedRouteMetadataMatchCriteria();
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-override", "mirror_only"}};
  const auto* entry = resolveRouteEntry(headers);
  ASSERT_EQ(1, entry->shadowPolicies().size());
  EXPECT_EQ("mirror-cluster", entry->shadowPolicies()[0]->cluster());
  EXPECT_EQ(parent_->route_entry_.base_retry_policy_.get(), entry->retryPolicy().get());
  EXPECT_EQ(matched_route_criteria, entry->metadataMatchCriteria());
}

// Selecting an override that is not configured leaves the matched route in effect.
TEST_F(DynamicModuleRouteActionOverrideTest, UnknownOverrideDelegatesToMatchedRoute) {
  setUpPluginWithOverrides();
  const auto* matched_route_criteria = giveMatchedRouteMetadataMatchCriteria();
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-override", "unknown"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ(parent_->route_entry_.base_retry_policy_.get(), entry->retryPolicy().get());
  EXPECT_EQ(matched_route_criteria, entry->metadataMatchCriteria());
  // Both branches yield an empty vector here, so only the identity of the reference proves that the
  // policies of the matched route are the ones in effect.
  EXPECT_EQ(&parent_->route_entry_.shadow_policies_, &entry->shadowPolicies());
}

// A refresh that reaches a decision without selecting an override drops the previous override.
TEST_F(DynamicModuleRouteActionOverrideTest, RefreshWithoutOverrideDelegatesToMatchedRoute) {
  setUpPluginWithOverrides();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}, {"x-override", "full"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ(7, entry->retryPolicy()->numRetries());

  headers.remove(Http::LowerCaseString("x-override"));
  entry->refreshRouteCluster(headers, stream_info_);
  EXPECT_EQ(parent_->route_entry_.base_retry_policy_.get(), entry->retryPolicy().get());
}

// Metadata under a namespace other than envoy.lb is ignored, matching RouteAction.metadata_match.
// The retry policy makes the override replace something, since one that replaces nothing is
// rejected at configuration load.
TEST_F(DynamicModuleClusterSpecifierTest, NonLoadBalancingMetadataIgnored) {
  setUpPlugin(R"EOF(
route_action_overrides:
  other:
    retry_policy:
      num_retries: 7
    metadata_match:
      filter_metadata:
        envoy.other:
          version: canary
)EOF");
  const auto* matched_route_criteria = giveMatchedRouteMetadataMatchCriteria();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}, {"x-override", "other"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ(matched_route_criteria, entry->metadataMatchCriteria());
  // The retry policy still comes from the override, so the entry itself was selected.
  EXPECT_EQ(7, entry->retryPolicy()->numRetries());
}

// A typed metadata object that keeps the type URL of the Any it was parsed from, so a test can tell
// the module set typed route metadata that the pack parsed out of typedMetadata().
struct TestTypedRouteMetadata : public Envoy::Config::TypedMetadata::Object {
  explicit TestTypedRouteMetadata(std::string type_url) : type_url_(std::move(type_url)) {}
  const std::string type_url_;
};

// Parses the typed route metadata the module sets under envoy.test.typed_route so that a test can
// read it back through typedMetadata().
class TestTypedRouteMetadataFactory : public Envoy::Router::HttpRouteTypedMetadataFactory {
public:
  std::string name() const override { return "envoy.test.typed_route"; }
  std::unique_ptr<const Envoy::Config::TypedMetadata::Object>
  parse(const Protobuf::Struct&) const override {
    return nullptr;
  }
  std::unique_ptr<const Envoy::Config::TypedMetadata::Object>
  parse(const Protobuf::Any& any) const override {
    // A sentinel type URL lets a test drive the parse failure that the pack build guards against.
    if (any.type_url() == "t/throw") {
      throw EnvoyException("typed route metadata rejected by the test factory");
    }
    return std::make_unique<TestTypedRouteMetadata>(any.type_url());
  }
};

REGISTER_FACTORY(TestTypedRouteMetadataFactory, Envoy::Router::HttpRouteTypedMetadataFactory);

// Tests for the route metadata the module layers onto the matched route. The reference module sets
// entries under envoy.test.route from the x-route-meta-* headers and a typed entry under
// envoy.test.typed_route from x-route-typed-meta.
class DynamicModuleRouteMetadataTest : public DynamicModuleClusterSpecifierTest {
public:
  // Returns the value of a string field of the route metadata under the given namespace and key, or
  // "absent" when it is missing, so a test can assert on both the merged and the fallback outcome.
  std::string routeMetadataString(absl::string_view ns, absl::string_view key) {
    const auto& filter_metadata = resolved_route_->metadata().filter_metadata();
    const auto ns_it = filter_metadata.find(ns);
    if (ns_it == filter_metadata.end()) {
      return "absent";
    }
    const auto field_it = ns_it->second.fields().find(key);
    return field_it == ns_it->second.fields().end() ? "absent" : field_it->second.string_value();
  }
};

// The scalar setters layer number, string and bool entries onto the matched route so consumers that
// read route metadata observe them.
TEST_F(DynamicModuleRouteMetadataTest, ScalarMetadataLayeredOntoMatchedRoute) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"},
                                         {"env", "prod"},
                                         {"x-route-meta-string", "shard-a"},
                                         {"x-route-meta-number", "0.5"},
                                         {"x-route-meta-bool", "true"}};
  resolveRouteEntry(headers);
  const auto& filter_metadata = resolved_route_->metadata().filter_metadata();
  ASSERT_NE(filter_metadata.end(), filter_metadata.find("envoy.test.route"));
  const auto& fields = filter_metadata.at("envoy.test.route").fields();
  ASSERT_TRUE(fields.contains("string_key"));
  ASSERT_TRUE(fields.contains("number_key"));
  ASSERT_TRUE(fields.contains("bool_key"));
  EXPECT_EQ("shard-a", fields.at("string_key").string_value());
  EXPECT_DOUBLE_EQ(0.5, fields.at("number_key").number_value());
  EXPECT_TRUE(fields.at("bool_key").bool_value());
}

// Module metadata is layered onto the matched route. An entry under the same namespace and key is
// overwritten, an untouched entry under that namespace stays, and another namespace is preserved.
TEST_F(DynamicModuleRouteMetadataTest, ModuleMetadataMergesWithMatchedRoute) {
  setUpPlugin();
  Protobuf::Struct same_namespace;
  (*same_namespace.mutable_fields())["string_key"] = ValueUtil::stringValue("base-value");
  (*same_namespace.mutable_fields())["kept"] = ValueUtil::stringValue("yes");
  (*parent_->metadata_.mutable_filter_metadata())["envoy.test.route"] = same_namespace;
  Protobuf::Struct other_namespace;
  (*other_namespace.mutable_fields())["kept"] = ValueUtil::stringValue("yes");
  (*parent_->metadata_.mutable_filter_metadata())["envoy.other"] = other_namespace;

  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-route-meta-string", "shard-a"}};
  resolveRouteEntry(headers);
  EXPECT_EQ("shard-a", routeMetadataString("envoy.test.route", "string_key"));
  EXPECT_EQ("yes", routeMetadataString("envoy.test.route", "kept"));
  EXPECT_EQ("yes", routeMetadataString("envoy.other", "kept"));
}

// Without any setter the accessors return the references of the matched route rather than a merged
// copy, so a consumer sees exactly what the route configures.
TEST_F(DynamicModuleRouteMetadataTest, NoMetadataFallsBackToMatchedRoute) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
  resolveRouteEntry(headers);
  EXPECT_EQ(&parent_->metadata_, &resolved_route_->metadata());
  EXPECT_EQ(&parent_->typed_metadata_, &resolved_route_->typedMetadata());
}

// A refresh replaces the whole decision, so metadata an earlier call layered on goes back to the
// matched route when the refreshed decision sets none.
TEST_F(DynamicModuleRouteMetadataTest, RefreshWithoutMetadataRevertsToMatchedRoute) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-route-meta-string", "shard-a"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ("shard-a", routeMetadataString("envoy.test.route", "string_key"));

  headers.remove(Http::LowerCaseString("x-route-meta-string"));
  entry->refreshRouteCluster(headers, stream_info_);
  EXPECT_EQ(&parent_->metadata_, &resolved_route_->metadata());
  EXPECT_EQ(&parent_->typed_metadata_, &resolved_route_->typedMetadata());
}

// A refresh rebuilds the metadata from scratch, so a key an earlier decision layered on is gone
// once the refreshed decision sets a different one.
TEST_F(DynamicModuleRouteMetadataTest, RefreshRebuildsMetadataFromScratch) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-route-meta-string", "shard-a"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ("shard-a", routeMetadataString("envoy.test.route", "string_key"));

  headers.remove(Http::LowerCaseString("x-route-meta-string"));
  headers.setCopy(Http::LowerCaseString("x-route-meta-number"), "0.5");
  entry->refreshRouteCluster(headers, stream_info_);
  EXPECT_EQ("absent", routeMetadataString("envoy.test.route", "string_key"));
  const auto& fields =
      resolved_route_->metadata().filter_metadata().at("envoy.test.route").fields();
  ASSERT_TRUE(fields.contains("number_key"));
  EXPECT_DOUBLE_EQ(0.5, fields.at("number_key").number_value());
}

// The typed setter merges an Any that a registered factory parses, so typedMetadata() exposes the
// object the module set while the Any also reaches the proto metadata.
TEST_F(DynamicModuleRouteMetadataTest, TypedMetadataLayeredOntoMatchedRoute) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-route-typed-meta", "set"}};
  resolveRouteEntry(headers);
  const auto& typed_filter_metadata = resolved_route_->metadata().typed_filter_metadata();
  ASSERT_NE(typed_filter_metadata.end(), typed_filter_metadata.find("envoy.test.typed_route"));
  EXPECT_EQ("t/x", typed_filter_metadata.at("envoy.test.typed_route").type_url());
  const auto* parsed =
      resolved_route_->typedMetadata().get<TestTypedRouteMetadata>("envoy.test.typed_route");
  ASSERT_NE(nullptr, parsed);
  EXPECT_EQ("t/x", parsed->type_url_);
}

// The overlay copies the matched route metadata whole, so a typed entry the matched route carries
// survives when the module layers only untyped metadata on top.
TEST_F(DynamicModuleRouteMetadataTest, MatchedRouteTypedMetadataPreserved) {
  setUpPlugin();
  Protobuf::Any base_typed;
  base_typed.set_type_url("t/base");
  (*parent_->metadata_.mutable_typed_filter_metadata())["envoy.test.typed_route"] = base_typed;

  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-route-meta-string", "shard-a"}};
  resolveRouteEntry(headers);
  const auto* parsed =
      resolved_route_->typedMetadata().get<TestTypedRouteMetadata>("envoy.test.typed_route");
  ASSERT_NE(nullptr, parsed);
  EXPECT_EQ("t/base", parsed->type_url_);
}

// A typed entry the matched route carries under the same namespace is replaced wholesale by the one
// the module sets, matching the assign semantics of the typed setter. The same decision also layers
// an untyped entry, so both merge loops run together.
TEST_F(DynamicModuleRouteMetadataTest, ModuleTypedMetadataOverwritesMatchedRoute) {
  setUpPlugin();
  Protobuf::Any base_typed;
  base_typed.set_type_url("t/base");
  (*parent_->metadata_.mutable_typed_filter_metadata())["envoy.test.typed_route"] = base_typed;

  Http::TestRequestHeaderMapImpl headers{{":path", "/"},
                                         {"env", "prod"},
                                         {"x-route-typed-meta", "set"},
                                         {"x-route-meta-string", "shard-a"}};
  resolveRouteEntry(headers);
  const auto& typed = resolved_route_->metadata().typed_filter_metadata();
  EXPECT_EQ("t/x", typed.at("envoy.test.typed_route").type_url());
  const auto* parsed =
      resolved_route_->typedMetadata().get<TestTypedRouteMetadata>("envoy.test.typed_route");
  ASSERT_NE(nullptr, parsed);
  EXPECT_EQ("t/x", parsed->type_url_);
  EXPECT_EQ("shard-a", routeMetadataString("envoy.test.route", "string_key"));
}

// A pack is kept for the life of the entry, so a metadata reference taken before a refresh stays
// valid after it and still holds the value of the decision it was taken from.
TEST_F(DynamicModuleRouteMetadataTest, MetadataReferenceStaysValidAcrossRefresh) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-route-meta-string", "shard-a"}};
  const auto* entry = resolveRouteEntry(headers);
  const auto& first_fields =
      resolved_route_->metadata().filter_metadata().at("envoy.test.route").fields();
  EXPECT_EQ("shard-a", first_fields.at("string_key").string_value());

  headers.remove(Http::LowerCaseString("x-route-meta-string"));
  headers.setCopy(Http::LowerCaseString("x-route-meta-number"), "0.5");
  entry->refreshRouteCluster(headers, stream_info_);

  // The earlier reference stays valid and holds its own decision, while the current view reflects
  // the new one.
  EXPECT_EQ("shard-a", first_fields.at("string_key").string_value());
  const auto& second_fields =
      resolved_route_->metadata().filter_metadata().at("envoy.test.route").fields();
  EXPECT_FALSE(second_fields.contains("string_key"));
  EXPECT_DOUBLE_EQ(0.5, second_fields.at("number_key").number_value());
}

// A refreshed decision that reports nothing leaves the active pack untouched, so metadata an
// earlier decision layered on stays in effect.
TEST_F(DynamicModuleRouteMetadataTest, RefreshWithoutDecisionKeepsLayeredMetadata) {
  setUpPlugin();
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-route-meta-string", "shard-a"}};
  const auto* entry = resolveRouteEntry(headers);
  EXPECT_EQ("shard-a", routeMetadataString("envoy.test.route", "string_key"));

  // Dropping env makes the module report no decision, so the previous metadata stays in effect.
  headers.remove(Http::LowerCaseString("env"));
  entry->refreshRouteCluster(headers, stream_info_);
  EXPECT_EQ("shard-a", routeMetadataString("envoy.test.route", "string_key"));
}

// A typed metadata factory can reject the Any the module sets. The pack build catches that, so a
// refresh reverts to the matched route rather than keeping the earlier pack or letting the
// exception reach the worker.
TEST_F(DynamicModuleRouteMetadataTest, TypedMetadataParseFailureFallsBackToMatchedRoute) {
  setUpPlugin();
  // A first decision layers a typed entry the factory accepts, so the active pack is non-null.
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"env", "prod"}, {"x-route-typed-meta", "set"}};
  const auto* entry = resolveRouteEntry(headers);
  ASSERT_NE(nullptr,
            resolved_route_->typedMetadata().get<TestTypedRouteMetadata>("envoy.test.typed_route"));

  // A refresh whose typed Any the factory rejects reverts to the matched route.
  headers.setCopy(Http::LowerCaseString("x-route-typed-meta"), "throw");
  EXPECT_LOG_CONTAINS("warn", "rejected by a typed metadata factory",
                      { entry->refreshRouteCluster(headers, stream_info_); });
  EXPECT_EQ(&parent_->metadata_, &resolved_route_->metadata());
  EXPECT_EQ(&parent_->typed_metadata_, &resolved_route_->typedMetadata());
}

// Shared scaffolding for tests that call the cluster specifier callbacks directly against a no-op
// module, so a test can drive a single callback and inspect the selection it fills in.
class DynamicModuleClusterSpecifierAbiTest : public testing::Test {
public:
  DynamicModuleClusterSpecifierAbiTest() {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
    mockCustomStatNamespaces(context_, custom_stat_namespaces_);
  }

  DynamicModuleClusterSpecifierConfigSharedPtr makeConfig() {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("cluster_specifier_no_op", "c"),
        /*do_not_close=*/true);
    EXPECT_TRUE(dynamic_module.ok());
    auto config_or_error = newDynamicModuleClusterSpecifierConfig(
        protoConfig("cluster_specifier_no_op", "test_cluster_specifier"),
        std::move(dynamic_module.value()), context_);
    EXPECT_TRUE(config_or_error.ok());
    return config_or_error.value();
  }

  ClusterSpecifierContext makeContext(DynamicModuleClusterSpecifierConfigSharedPtr config) {
    return ClusterSpecifierContext{*config, headers_, stream_info_, route_name_, /*random=*/0, {}};
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Stats::CustomStatNamespacesImpl custom_stat_namespaces_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Http::TestRequestHeaderMapImpl headers_;
  const std::string route_name_{"named-route"};
};

// ABI level tests for the serialized metadata setters. The reference module cannot drive these
// because it has no protobuf dependency to encode an arbitrary Struct or a malformed buffer.

// A serialized Struct is merged into the namespace, overwriting an existing key and keeping the
// others.
TEST_F(DynamicModuleClusterSpecifierAbiTest, StructMetadataMerged) {
  auto config = makeConfig();
  auto context = makeContext(config);
  auto& existing =
      (*context.selection.route_metadata.mutable_filter_metadata())["envoy.test.route"];
  (*existing.mutable_fields())["shard"] = ValueUtil::stringValue("old");
  (*existing.mutable_fields())["kept"] = ValueUtil::stringValue("yes");

  Protobuf::Struct value;
  (*value.mutable_fields())["shard"] = ValueUtil::stringValue("a");
  const std::string serialized = value.SerializeAsString();
  envoy_dynamic_module_callback_cluster_specifier_set_route_metadata_struct(
      static_cast<void*>(&context), metricBuffer("envoy.test.route"), metricBuffer(serialized));

  const auto& filter_metadata = context.selection.route_metadata.filter_metadata();
  ASSERT_NE(filter_metadata.end(), filter_metadata.find("envoy.test.route"));
  const auto& fields = filter_metadata.at("envoy.test.route").fields();
  EXPECT_EQ("a", fields.at("shard").string_value());
  EXPECT_EQ("yes", fields.at("kept").string_value());
}

// A buffer that does not parse as a Struct leaves the metadata untouched and is logged.
TEST_F(DynamicModuleClusterSpecifierAbiTest, MalformedStructIgnored) {
  auto config = makeConfig();
  auto context = makeContext(config);
  EXPECT_LOG_CONTAINS("warn", "does not parse as a google.protobuf.Struct", {
    envoy_dynamic_module_callback_cluster_specifier_set_route_metadata_struct(
        static_cast<void*>(&context), metricBuffer("envoy.test.route"),
        metricBuffer(absl::string_view("\x0f", 1)));
  });
  EXPECT_TRUE(context.selection.route_metadata.filter_metadata().empty());
}

// A buffer that does not parse as an Any leaves the typed metadata untouched and is logged.
TEST_F(DynamicModuleClusterSpecifierAbiTest, MalformedTypedMetadataIgnored) {
  auto config = makeConfig();
  auto context = makeContext(config);
  EXPECT_LOG_CONTAINS("warn", "does not parse as a google.protobuf.Any", {
    envoy_dynamic_module_callback_cluster_specifier_set_route_typed_metadata(
        static_cast<void*>(&context), metricBuffer("envoy.test.typed_route"),
        metricBuffer(absl::string_view("\x0f", 1)));
  });
  EXPECT_TRUE(context.selection.route_metadata.typed_filter_metadata().empty());
}

// Adds the cluster manager the host count callback reads to the shared ABI scaffolding.
class DynamicModuleClusterSpecifierHostCountAbiTest : public DynamicModuleClusterSpecifierAbiTest {
public:
  DynamicModuleClusterSpecifierHostCountAbiTest() {
    EXPECT_CALL(context_, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager_));
  }

  Upstream::MockClusterManager cluster_manager_;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
};

TEST_F(DynamicModuleClusterSpecifierHostCountAbiTest, UnknownClusterReturnsFalse) {
  auto config = makeConfig();
  auto context = makeContext(config);
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view("absent")))
      .WillOnce(testing::Return(nullptr));
  size_t total = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_specifier_get_cluster_host_count(
      static_cast<void*>(&context), {"absent", 6}, 0, &total, nullptr, nullptr));
}

TEST_F(DynamicModuleClusterSpecifierHostCountAbiTest, HostCountsReturnedForKnownCluster) {
  auto config = makeConfig();
  auto context = makeContext(config);
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view("present")))
      .WillOnce(testing::Return(&thread_local_cluster_));
  auto* mock_host_set = thread_local_cluster_.cluster_.priority_set_.getMockHostSet(0);
  mock_host_set->hosts_.resize(4);
  mock_host_set->healthy_hosts_.resize(2);
  mock_host_set->degraded_hosts_.resize(1);

  size_t total = 0;
  size_t healthy = 0;
  size_t degraded = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_specifier_get_cluster_host_count(
      static_cast<void*>(&context), {"present", 7}, 0, &total, &healthy, &degraded));
  EXPECT_EQ(4, total);
  EXPECT_EQ(2, healthy);
  EXPECT_EQ(1, degraded);
}

TEST_F(DynamicModuleClusterSpecifierReadTest, ClusterHostCountReadable) {
  setUpPlugin();
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(absl::string_view("cluster_0")))
      .WillOnce(testing::Return(nullptr));
  Http::TestRequestHeaderMapImpl headers{{":path", "/"},
                                         {"env", "prod"},
                                         {"x-echo", "cluster-host-count"},
                                         {"x-query-cluster", "cluster_0"}};
  EXPECT_EQ("absent", echoedValue(headers, "cluster-host-count"));
}

class DynamicModuleClusterSpecifierMetricsAbiTest : public testing::Test {
public:
  DynamicModuleClusterSpecifierMetricsAbiTest() {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
    EXPECT_CALL(context_, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager_));
    mockCustomStatNamespaces(context_, custom_stat_namespaces_);
  }

  static void unfreezeStatCreation(DynamicModuleClusterSpecifierConfig& config) {
    config.stat_creation_frozen_.store(false, std::memory_order_release);
  }

  DynamicModuleClusterSpecifierConfigSharedPtr makeConfig() {
    return makeConfig(protoConfig("cluster_specifier_no_op", "test_cluster_specifier"));
  }

  DynamicModuleClusterSpecifierConfigSharedPtr
  makeConfig(const DynamicModuleClusterSpecifierProto& proto_config) {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("cluster_specifier_no_op", "c"),
        /*do_not_close=*/true);
    EXPECT_TRUE(dynamic_module.ok());
    auto config_or_error = newDynamicModuleClusterSpecifierConfig(
        proto_config, std::move(dynamic_module.value()), context_);
    EXPECT_TRUE(config_or_error.ok());
    return config_or_error.value();
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Stats::CustomStatNamespacesImpl custom_stat_namespaces_;
  Upstream::MockClusterManager cluster_manager_;
};

TEST_F(DynamicModuleClusterSpecifierMetricsAbiTest, CounterDefineAndIncrement) {
  auto config = makeConfig();
  unfreezeStatCreation(*config);
  auto* config_ptr = static_cast<void*>(config.get());

  size_t counter_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_counter(
                config_ptr, metricBuffer("test_counter"), nullptr, 0, &counter_id));
  EXPECT_EQ(1, counter_id);

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_counter(
                config_ptr, counter_id, nullptr, 0, 5));
  auto counter = TestUtility::findCounter(context_.store_, "dynamicmodulescustom.test_counter");
  ASSERT_NE(nullptr, counter);
  EXPECT_EQ(5, counter->value());
}

TEST_F(DynamicModuleClusterSpecifierMetricsAbiTest, CustomMetricsNamespaceUsed) {
  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  proto_config.mutable_dynamic_module_config()->set_metrics_namespace("cluster_specifier_custom");
  auto config = makeConfig(proto_config);
  unfreezeStatCreation(*config);

  size_t counter_id = 0;
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_Success,
      envoy_dynamic_module_callback_cluster_specifier_config_define_counter(
          static_cast<void*>(config.get()), metricBuffer("test_counter"), nullptr, 0, &counter_id));
  EXPECT_NE(nullptr,
            TestUtility::findCounter(context_.store_, "cluster_specifier_custom.test_counter"));
}

// Test that the legacy behavior registers the custom stat namespace when the runtime guard is
// enabled.
TEST_F(DynamicModuleClusterSpecifierMetricsAbiTest, RegisterStatNamespaceWithRuntimeGuard) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix", "true"}});

  auto proto_config = protoConfig("cluster_specifier_no_op", "test_cluster_specifier");
  proto_config.mutable_dynamic_module_config()->set_metrics_namespace("cluster_specifier_custom");
  auto config = makeConfig(proto_config);
  EXPECT_TRUE(custom_stat_namespaces_.registered("cluster_specifier_custom"));
}

TEST_F(DynamicModuleClusterSpecifierMetricsAbiTest, ScalarGaugeDefineAndOperate) {
  auto config = makeConfig();
  unfreezeStatCreation(*config);
  auto* config_ptr = static_cast<void*>(config.get());

  size_t gauge_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_gauge(
                config_ptr, metricBuffer("test_gauge"), nullptr, 0, &gauge_id));
  EXPECT_EQ(1, gauge_id);

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_gauge(
                config_ptr, gauge_id, nullptr, 0, 10));
  auto gauge = TestUtility::findGauge(context_.store_, "dynamicmodulescustom.test_gauge");
  ASSERT_NE(nullptr, gauge);
  EXPECT_EQ(10, gauge->value());

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_decrement_gauge(
                config_ptr, gauge_id, nullptr, 0, 3));
  EXPECT_EQ(7, gauge->value());

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_set_gauge(config_ptr, gauge_id,
                                                                             nullptr, 0, 42));
  EXPECT_EQ(42, gauge->value());
}

TEST_F(DynamicModuleClusterSpecifierMetricsAbiTest, ScalarHistogramDefineAndRecord) {
  auto config = makeConfig();
  unfreezeStatCreation(*config);
  auto* config_ptr = static_cast<void*>(config.get());

  size_t histogram_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_histogram(
                config_ptr, metricBuffer("test_histogram"), nullptr, 0, &histogram_id));
  EXPECT_EQ(1, histogram_id);

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_record_histogram_value(
                config_ptr, histogram_id, nullptr, 0, 42));
  EXPECT_TRUE(config->getHistogramById(histogram_id).has_value());
}

TEST_F(DynamicModuleClusterSpecifierMetricsAbiTest, MetricIdsAreSequentialAndIndependent) {
  auto config = makeConfig();
  unfreezeStatCreation(*config);
  auto* config_ptr = static_cast<void*>(config.get());

  size_t first_counter_id = 0;
  size_t second_counter_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_counter(
                config_ptr, metricBuffer("first_counter"), nullptr, 0, &first_counter_id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_counter(
                config_ptr, metricBuffer("second_counter"), nullptr, 0, &second_counter_id));
  EXPECT_EQ(1, first_counter_id);
  EXPECT_EQ(2, second_counter_id);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_counter(
                config_ptr, second_counter_id, nullptr, 0, 7));
  auto first_counter =
      TestUtility::findCounter(context_.store_, "dynamicmodulescustom.first_counter");
  auto second_counter =
      TestUtility::findCounter(context_.store_, "dynamicmodulescustom.second_counter");
  ASSERT_NE(nullptr, first_counter);
  ASSERT_NE(nullptr, second_counter);
  EXPECT_EQ(0, first_counter->value());
  EXPECT_EQ(7, second_counter->value());

  size_t first_gauge_id = 0;
  size_t second_gauge_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_gauge(
                config_ptr, metricBuffer("first_gauge"), nullptr, 0, &first_gauge_id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_gauge(
                config_ptr, metricBuffer("second_gauge"), nullptr, 0, &second_gauge_id));
  EXPECT_EQ(1, first_gauge_id);
  EXPECT_EQ(2, second_gauge_id);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_set_gauge(
                config_ptr, second_gauge_id, nullptr, 0, 9));
  auto first_gauge = TestUtility::findGauge(context_.store_, "dynamicmodulescustom.first_gauge");
  auto second_gauge = TestUtility::findGauge(context_.store_, "dynamicmodulescustom.second_gauge");
  ASSERT_NE(nullptr, first_gauge);
  ASSERT_NE(nullptr, second_gauge);
  EXPECT_EQ(0, first_gauge->value());
  EXPECT_EQ(9, second_gauge->value());

  size_t first_histogram_id = 0;
  size_t second_histogram_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_histogram(
                config_ptr, metricBuffer("first_histogram"), nullptr, 0, &first_histogram_id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_histogram(
                config_ptr, metricBuffer("second_histogram"), nullptr, 0, &second_histogram_id));
  EXPECT_EQ(1, first_histogram_id);
  EXPECT_EQ(2, second_histogram_id);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_record_histogram_value(
                config_ptr, second_histogram_id, nullptr, 0, 11));
  EXPECT_TRUE(config->getHistogramById(first_histogram_id).has_value());
  EXPECT_TRUE(config->getHistogramById(second_histogram_id).has_value());
}

TEST_F(DynamicModuleClusterSpecifierMetricsAbiTest, LabeledMetricsDefineAndOperate) {
  auto config = makeConfig();
  unfreezeStatCreation(*config);
  auto* config_ptr = static_cast<void*>(config.get());
  std::array<envoy_dynamic_module_type_module_buffer, 2> label_names = {metricBuffer("source"),
                                                                        metricBuffer("result")};
  std::array<envoy_dynamic_module_type_module_buffer, 2> label_values = {metricBuffer("module"),
                                                                         metricBuffer("success")};

  size_t counter_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_counter(
                config_ptr, metricBuffer("labeled_counter"), label_names.data(), label_names.size(),
                &counter_id));
  EXPECT_EQ(1, counter_id);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_counter(
                config_ptr, counter_id, label_values.data(), label_values.size(), 5));
  auto counter = TestUtility::findCounter(
      context_.store_, "dynamicmodulescustom.labeled_counter.source.module.result.success");
  ASSERT_NE(nullptr, counter);
  EXPECT_EQ(5, counter->value());
  EXPECT_EQ("dynamicmodulescustom.labeled_counter", counter->tagExtractedName());
  EXPECT_THAT(counter->tags(), testing::ElementsAre(Stats::Tag{"source", "module"},
                                                    Stats::Tag{"result", "success"}));

  size_t gauge_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_gauge(
                config_ptr, metricBuffer("labeled_gauge"), label_names.data(), label_names.size(),
                &gauge_id));
  EXPECT_EQ(1, gauge_id);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_gauge(
                config_ptr, gauge_id, label_values.data(), label_values.size(), 10));
  auto gauge = TestUtility::findGauge(
      context_.store_, "dynamicmodulescustom.labeled_gauge.source.module.result.success");
  ASSERT_NE(nullptr, gauge);
  EXPECT_EQ(10, gauge->value());
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_decrement_gauge(
                config_ptr, gauge_id, label_values.data(), label_values.size(), 3));
  EXPECT_EQ(7, gauge->value());
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_set_gauge(
                config_ptr, gauge_id, label_values.data(), label_values.size(), 42));
  EXPECT_EQ(42, gauge->value());

  size_t histogram_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_histogram(
                config_ptr, metricBuffer("labeled_histogram"), label_names.data(),
                label_names.size(), &histogram_id));
  EXPECT_EQ(1, histogram_id);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_record_histogram_value(
                config_ptr, histogram_id, label_values.data(), label_values.size(), 42));
  EXPECT_TRUE(config->getHistogramVecById(histogram_id).has_value());
}

TEST_F(DynamicModuleClusterSpecifierMetricsAbiTest, InvalidMetricIdsReturnMetricNotFound) {
  auto config = makeConfig();
  auto* config_ptr = static_cast<void*>(config.get());
  auto label_value = metricBuffer("module");

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_counter(
                config_ptr, 0, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_gauge(config_ptr, 0,
                                                                                   nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_decrement_gauge(config_ptr, 0,
                                                                                   nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_set_gauge(config_ptr, 0, nullptr,
                                                                             0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_record_histogram_value(
                config_ptr, 0, nullptr, 0, 1));

  constexpr size_t invalid_id = 9999;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_counter(
                config_ptr, invalid_id, &label_value, 1, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_gauge(
                config_ptr, invalid_id, &label_value, 1, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_decrement_gauge(
                config_ptr, invalid_id, &label_value, 1, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_set_gauge(config_ptr, invalid_id,
                                                                             &label_value, 1, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_record_histogram_value(
                config_ptr, invalid_id, &label_value, 1, 1));

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_counter(
                config_ptr, invalid_id, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_gauge(
                config_ptr, invalid_id, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_decrement_gauge(
                config_ptr, invalid_id, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_set_gauge(config_ptr, invalid_id,
                                                                             nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_record_histogram_value(
                config_ptr, invalid_id, nullptr, 0, 1));
}

TEST_F(DynamicModuleClusterSpecifierMetricsAbiTest, ScalarMetricIdsWithLabelsReturnMetricNotFound) {
  auto config = makeConfig();
  unfreezeStatCreation(*config);
  auto* config_ptr = static_cast<void*>(config.get());
  auto label_value = metricBuffer("module");

  size_t counter_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_counter(
                config_ptr, metricBuffer("scalar_counter"), nullptr, 0, &counter_id));
  size_t gauge_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_gauge(
                config_ptr, metricBuffer("scalar_gauge"), nullptr, 0, &gauge_id));
  size_t histogram_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_histogram(
                config_ptr, metricBuffer("scalar_histogram"), nullptr, 0, &histogram_id));

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_counter(
                config_ptr, counter_id, &label_value, 1, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_gauge(
                config_ptr, gauge_id, &label_value, 1, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_decrement_gauge(
                config_ptr, gauge_id, &label_value, 1, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_set_gauge(config_ptr, gauge_id,
                                                                             &label_value, 1, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_cluster_specifier_config_record_histogram_value(
                config_ptr, histogram_id, &label_value, 1, 1));
}

TEST_F(DynamicModuleClusterSpecifierMetricsAbiTest, MissingOrExtraLabelsReturnInvalidLabels) {
  auto config = makeConfig();
  unfreezeStatCreation(*config);
  auto* config_ptr = static_cast<void*>(config.get());
  auto label_name = metricBuffer("source");
  std::array<envoy_dynamic_module_type_module_buffer, 2> extra_label_values = {
      metricBuffer("module"), metricBuffer("extra")};

  size_t counter_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_counter(
                config_ptr, metricBuffer("labeled_counter"), &label_name, 1, &counter_id));
  size_t gauge_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_gauge(
                config_ptr, metricBuffer("labeled_gauge"), &label_name, 1, &gauge_id));
  size_t histogram_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_specifier_config_define_histogram(
                config_ptr, metricBuffer("labeled_histogram"), &label_name, 1, &histogram_id));

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_counter(
                config_ptr, counter_id, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_gauge(
                config_ptr, gauge_id, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_cluster_specifier_config_decrement_gauge(
                config_ptr, gauge_id, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_cluster_specifier_config_set_gauge(config_ptr, gauge_id,
                                                                             nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_cluster_specifier_config_record_histogram_value(
                config_ptr, histogram_id, nullptr, 0, 1));

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_counter(
                config_ptr, counter_id, extra_label_values.data(), extra_label_values.size(), 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_cluster_specifier_config_increment_gauge(
                config_ptr, gauge_id, extra_label_values.data(), extra_label_values.size(), 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_cluster_specifier_config_decrement_gauge(
                config_ptr, gauge_id, extra_label_values.data(), extra_label_values.size(), 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_cluster_specifier_config_set_gauge(
                config_ptr, gauge_id, extra_label_values.data(), extra_label_values.size(), 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_cluster_specifier_config_record_histogram_value(
                config_ptr, histogram_id, extra_label_values.data(), extra_label_values.size(), 1));
}

TEST_F(DynamicModuleClusterSpecifierMetricsAbiTest, MetricDefinitionsAfterFreezeReturnFrozen) {
  auto config = makeConfig();
  auto* config_ptr = static_cast<void*>(config.get());
  auto label_name = metricBuffer("source");
  size_t metric_id = 0;

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_cluster_specifier_config_define_counter(
                config_ptr, metricBuffer("late_counter"), &label_name, 1, &metric_id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_cluster_specifier_config_define_gauge(
                config_ptr, metricBuffer("late_gauge"), &label_name, 1, &metric_id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_cluster_specifier_config_define_histogram(
                config_ptr, metricBuffer("late_histogram"), &label_name, 1, &metric_id));
}

} // namespace
} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy

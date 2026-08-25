#include <limits>

#include "envoy/extensions/router/cluster_specifiers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/router/cluster_specifiers/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/logging.h"
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
                          "Route action override must specify at least one property to replace");
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
)EOF");
  }
};

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
TEST_F(DynamicModuleClusterSpecifierTest, NonLoadBalancingMetadataIgnored) {
  setUpPlugin(R"EOF(
route_action_overrides:
  other:
    metadata_match:
      filter_metadata:
        envoy.other:
          version: canary
)EOF");
  const auto* matched_route_criteria = giveMatchedRouteMetadataMatchCriteria();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}, {"x-override", "other"}};
  EXPECT_EQ(matched_route_criteria, resolveRouteEntry(headers)->metadataMatchCriteria());
}

} // namespace
} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy

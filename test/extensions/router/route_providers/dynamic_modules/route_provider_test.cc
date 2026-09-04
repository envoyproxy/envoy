#include "envoy/extensions/router/route_providers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/route_producer.h"
#include "envoy/router/router.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/fmt.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/router/route_providers/dynamic_modules/config.h"

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
namespace RouteProviders {
namespace DynamicModules {
namespace {

using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Pair;

// Builds route templates as mock routes with distinct cluster names, so that a test can tell which
// template the module selected by reading the cluster name the produced route delegates to.
std::vector<Envoy::Router::RouteEntryAndRouteConstSharedPtr> makeRouteTemplates(size_t count) {
  std::vector<Envoy::Router::RouteEntryAndRouteConstSharedPtr> templates;
  templates.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    auto route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
    route->route_entry_.cluster_name_ = absl::StrCat("cluster_", i);
    templates.push_back(std::move(route));
  }
  return templates;
}

// Builds a proto config that loads the named module with the given in-module provider name and an
// optional YAML fragment appended to the base configuration.
DynamicModuleRouteProviderProto protoConfig(absl::string_view module_name,
                                            absl::string_view provider_name,
                                            absl::string_view extra_config = "") {
  const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
  name: {}
  do_not_close: true
provider_name: {}
{}
)EOF",
                                       module_name, provider_name, extra_config);
  DynamicModuleRouteProviderProto proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  return proto_config;
}

// Tests for the factory using minimal C modules to drive symbol resolution and config lifecycle.
class DynamicModuleRouteProviderFactoryTest : public testing::Test {
public:
  DynamicModuleRouteProviderFactoryTest() {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
  }

  // Returns the number of times the no-op module has been handed its configuration back, so that a
  // test can observe the config destroy hook. Holding the module reference keeps the counter alive,
  // since the provider and this handle share one dlopen of the same path.
  std::function<int()> configDestroyCounter() {
    using GetConfigDestroyCountFuncType = int (*)(void);
    auto module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("route_provider_no_op", "c"),
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
  DynamicModuleRouteProviderFactory factory_;
};

TEST_F(DynamicModuleRouteProviderFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.router.route_provider.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleRouteProviderFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  ASSERT_NE(nullptr, proto);
  EXPECT_NE(nullptr, dynamic_cast<DynamicModuleRouteProviderProto*>(proto.get()));
}

TEST_F(DynamicModuleRouteProviderFactoryTest, FactoryRegistration) {
  auto* factory = Registry::FactoryRegistry<Envoy::Router::RouteProviderFactory>::getFactory(
      "envoy.router.route_provider.dynamic_modules");
  EXPECT_NE(nullptr, factory);
}

TEST_F(DynamicModuleRouteProviderFactoryTest, ValidConfig) {
  auto provider =
      factory_.createRouteProvider(protoConfig("route_provider_no_op", "test_route_provider"),
                                   makeRouteTemplates(1), context_, context_.initManager());
  ASSERT_TRUE(provider.ok());
  EXPECT_NE(nullptr, provider.value());
}

// An override that replaces object-valued route action properties is built at configuration time.
TEST_F(DynamicModuleRouteProviderFactoryTest, ValidConfigWithOverride) {
  auto provider =
      factory_.createRouteProvider(protoConfig("route_provider_no_op", "test_route_provider", R"EOF(
route_action_overrides:
  canary:
    retry_policy:
      retry_on: 5xx
      num_retries: 3
      retry_host_predicate:
      - name: envoy.retry_host_predicates.previous_hosts
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.retry.host.previous_hosts.v3.PreviousHostsPredicate
    metadata_match:
      filter_metadata:
        envoy.lb:
          version: canary
)EOF"),
                                   makeRouteTemplates(1), context_, context_.initManager());
  ASSERT_TRUE(provider.ok());
  EXPECT_NE(nullptr, provider.value());
}

// Releasing the provider must hand the in-module configuration back to the module.
TEST_F(DynamicModuleRouteProviderFactoryTest, ConfigDestroyHookInvoked) {
  const auto destroy_count = configDestroyCounter();
  const int before = destroy_count();
  auto provider_or =
      factory_.createRouteProvider(protoConfig("route_provider_no_op", "test_route_provider"),
                                   makeRouteTemplates(1), context_, context_.initManager());
  ASSERT_TRUE(provider_or.ok());
  Envoy::Router::RouteProducerSharedPtr provider = std::move(provider_or.value());
  ASSERT_NE(nullptr, provider);
  EXPECT_EQ(before, destroy_count());
  provider.reset();
  EXPECT_EQ(before + 1, destroy_count());
}

TEST_F(DynamicModuleRouteProviderFactoryTest, MissingConfigNew) {
  auto provider = factory_.createRouteProvider(
      protoConfig("route_provider_missing_config_new", "test_route_provider"),
      makeRouteTemplates(1), context_, context_.initManager());
  ASSERT_FALSE(provider.ok());
  EXPECT_THAT(
      provider.status().message(),
      HasSubstr("Failed to resolve symbol envoy_dynamic_module_on_route_provider_config_new"));
}

TEST_F(DynamicModuleRouteProviderFactoryTest, MissingConfigDestroy) {
  auto provider = factory_.createRouteProvider(
      protoConfig("route_provider_missing_config_destroy", "test_route_provider"),
      makeRouteTemplates(1), context_, context_.initManager());
  ASSERT_FALSE(provider.ok());
  EXPECT_THAT(
      provider.status().message(),
      HasSubstr("Failed to resolve symbol envoy_dynamic_module_on_route_provider_config_destroy"));
}

TEST_F(DynamicModuleRouteProviderFactoryTest, MissingSelect) {
  auto provider = factory_.createRouteProvider(
      protoConfig("route_provider_missing_select", "test_route_provider"), makeRouteTemplates(1),
      context_, context_.initManager());
  ASSERT_FALSE(provider.ok());
  EXPECT_THAT(provider.status().message(),
              HasSubstr("Failed to resolve symbol envoy_dynamic_module_on_route_provider_select"));
}

TEST_F(DynamicModuleRouteProviderFactoryTest, ConfigNewReturnsNull) {
  auto provider = factory_.createRouteProvider(
      protoConfig("route_provider_config_new_fail", "test_route_provider"), makeRouteTemplates(1),
      context_, context_.initManager());
  ASSERT_FALSE(provider.ok());
  EXPECT_THAT(provider.status().message(),
              HasSubstr("Failed to initialize dynamic module route provider config"));
}

// A failure to initialize the in-module configuration is counted.
TEST_F(DynamicModuleRouteProviderFactoryTest, ConfigInitFailureIncrementsStat) {
  EXPECT_EQ(0U, Extensions::DynamicModules::failureCounter(context_.scope(), "config_init_error",
                                                           "test_route_provider"));
  auto provider = factory_.createRouteProvider(
      protoConfig("route_provider_config_new_fail", "test_route_provider"), makeRouteTemplates(1),
      context_, context_.initManager());
  EXPECT_FALSE(provider.ok());
  EXPECT_EQ(1U, Extensions::DynamicModules::failureCounter(context_.scope(), "config_init_error",
                                                           "test_route_provider"));
}

// A module that cannot be loaded fails and is counted.
TEST_F(DynamicModuleRouteProviderFactoryTest, InvalidModule) {
  auto provider =
      factory_.createRouteProvider(protoConfig("nonexistent_module", "test_route_provider"),
                                   makeRouteTemplates(1), context_, context_.initManager());
  ASSERT_FALSE(provider.ok());
  EXPECT_THAT(provider.status().message(), HasSubstr("Failed to load"));
  EXPECT_EQ(1U, Extensions::DynamicModules::failureCounter(context_.scope(), "module_load_error",
                                                           "test_route_provider"));
}

// The dynamic module config is required.
TEST_F(DynamicModuleRouteProviderFactoryTest, MissingDynamicModuleConfigRejected) {
  DynamicModuleRouteProviderProto proto_config;
  EXPECT_THROW_WITH_REGEX((void)factory_.createRouteProvider(proto_config, makeRouteTemplates(1),
                                                             context_, context_.initManager()),
                          ProtoValidationException, "value is required");
}

// An override key must not be empty.
TEST_F(DynamicModuleRouteProviderFactoryTest, EmptyOverrideKeyRejected) {
  auto proto_config = protoConfig("route_provider_no_op", "test_route_provider");
  (*proto_config.mutable_route_action_overrides())[""] = {};
  EXPECT_THROW_WITH_REGEX((void)factory_.createRouteProvider(proto_config, makeRouteTemplates(1),
                                                             context_, context_.initManager()),
                          ProtoValidationException, "value length must be at least 1");
}

// An override that replaces nothing is a configuration mistake rather than a silent no-op.
TEST_F(DynamicModuleRouteProviderFactoryTest, EmptyOverrideRejected) {
  auto proto_config = protoConfig("route_provider_no_op", "test_route_provider");
  (*proto_config.mutable_route_action_overrides())["empty"] = {};
  auto provider = factory_.createRouteProvider(proto_config, makeRouteTemplates(1), context_,
                                               context_.initManager());
  ASSERT_FALSE(provider.ok());
  EXPECT_THAT(provider.status().message(),
              HasSubstr("Route action override must replace at least one route action property"));
}

// A per-route configuration override that replaces nothing is a configuration mistake rather than a
// silent no-op.
TEST_F(DynamicModuleRouteProviderFactoryTest, EmptyPerRouteConfigOverrideRejected) {
  auto proto_config = protoConfig("route_provider_no_op", "test_route_provider");
  (*proto_config.mutable_per_route_config_overrides())["empty"] = {};
  auto provider = factory_.createRouteProvider(proto_config, makeRouteTemplates(1), context_,
                                               context_.initManager());
  ASSERT_FALSE(provider.ok());
  EXPECT_THAT(provider.status().message(),
              HasSubstr("Per-route configuration override must replace at least one property"));
}

// Tests that drive the full selection path through the reference Rust module.
class DynamicModuleRouteProviderTest : public testing::Test {
public:
  DynamicModuleRouteProviderTest() {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
  }

  absl::StatusOr<Envoy::Router::RouteProducerSharedPtr>
  createProvider(absl::string_view provider_name, size_t template_count,
                 absl::string_view extra_config = "") {
    const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
  name: route_provider_integration_test
  do_not_close: true
provider_name: {}
{}
)EOF",
                                         provider_name, extra_config);
    DynamicModuleRouteProviderProto proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    return factory_.createRouteProvider(proto_config, makeRouteTemplates(template_count), context_,
                                        context_.initManager());
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  DynamicModuleRouteProviderFactory factory_;
};

// The module loads by name, selects the default template and produces a route that delegates to it.
TEST_F(DynamicModuleRouteProviderTest, LoadsModuleAndProducesRoute) {
  auto provider = createProvider("test_route_provider", 2);
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"route-index", "0"}};
  auto route = provider.value()->produceRoute(nullptr, headers, stream_info_, 42);
  ASSERT_NE(nullptr, route);
  EXPECT_EQ("cluster_0", route->routeEntry()->clusterName());
}

// The index the module selects picks the route template the produced route delegates to.
TEST_F(DynamicModuleRouteProviderTest, SelectsTemplateByIndex) {
  auto provider = createProvider("test_route_provider", 2);
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"route-index", "1"}};
  auto route = provider.value()->produceRoute(nullptr, headers, stream_info_, 42);
  ASSERT_NE(nullptr, route);
  EXPECT_EQ("cluster_1", route->routeEntry()->clusterName());
}

// When the module selects no route the provider produces no route, which is logged.
TEST_F(DynamicModuleRouteProviderTest, ModuleSelectsNoRoute) {
  auto provider = createProvider("test_route_provider", 2);
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"x-no-route", "true"}};
  EXPECT_LOG_CONTAINS("debug", "dynamic module route provider selected no route", {
    EXPECT_EQ(nullptr, provider.value()->produceRoute(nullptr, headers, stream_info_, 42));
  });
}

// An out of range index makes select_route fail, so the module produces no route.
TEST_F(DynamicModuleRouteProviderTest, OutOfRangeIndexReturnsNoRoute) {
  auto provider = createProvider("test_route_provider", 2);
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"route-index", "9"}};
  EXPECT_EQ(nullptr, provider.value()->produceRoute(nullptr, headers, stream_info_, 42));
}

// A provider name the module does not recognize makes config_new return null, which is rejected.
TEST_F(DynamicModuleRouteProviderTest, UnknownProviderNameRejected) {
  auto provider = createProvider("nonexistent", 2);
  ASSERT_FALSE(provider.ok());
  EXPECT_THAT(provider.status().message(),
              HasSubstr("Failed to initialize dynamic module route provider config"));
}

// When the module reports a decision without selecting a route the provider produces no route,
// which is logged distinctly from the module selecting no route at all.
TEST_F(DynamicModuleRouteProviderTest, ModuleReportsDecisionWithoutSelectingRoute) {
  auto provider = createProvider("test_route_provider", 2);
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"x-decide-no-select", "true"}};
  EXPECT_LOG_CONTAINS("debug", "reported a decision without selecting a route", {
    EXPECT_EQ(nullptr, provider.value()->produceRoute(nullptr, headers, stream_info_, 42));
  });
}

// A direct response the module resolved the request to yields a terminal route that reports the
// direct response entry and no route entry, so the router honors it before touching a cluster. A
// redirect is reported the same way, carrying its status code and location.
TEST_F(DynamicModuleRouteProviderTest, TerminalRouteReportsNoRouteEntry) {
  auto provider = createProvider("test_route_provider", 2);
  ASSERT_TRUE(provider.ok());

  Http::TestRequestHeaderMapImpl direct_headers{
      {":path", "/"}, {"route-index", "0"}, {"x-direct-response", "201"}};
  auto direct_route = provider.value()->produceRoute(nullptr, direct_headers, stream_info_, 42);
  ASSERT_NE(nullptr, direct_route);
  ASSERT_NE(nullptr, direct_route->directResponseEntry());
  EXPECT_EQ(nullptr, direct_route->routeEntry());
  EXPECT_EQ(Http::Code::Created, direct_route->directResponseEntry()->responseCode());

  Http::TestRequestHeaderMapImpl redirect_headers{
      {":path", "/"}, {"route-index", "0"}, {"x-redirect", "302"}};
  auto redirect_route = provider.value()->produceRoute(nullptr, redirect_headers, stream_info_, 42);
  ASSERT_NE(nullptr, redirect_route);
  ASSERT_NE(nullptr, redirect_route->directResponseEntry());
  EXPECT_EQ(nullptr, redirect_route->routeEntry());
  EXPECT_EQ(Http::Code::Found, redirect_route->directResponseEntry()->responseCode());
  EXPECT_EQ("http://redirect.example.com/new",
            redirect_route->directResponseEntry()->newUri(redirect_headers, stream_info_));
}

// A selected override that replaces every object-valued route action property applies each of them
// to the produced route entry rather than falling back to the route template.
TEST_F(DynamicModuleRouteProviderTest, SelectedRouteActionOverrideApplied) {
  auto provider = createProvider("test_route_provider", 2, R"EOF(
route_action_overrides:
  canary:
    hash_policy:
    - header:
        header_name: x-hash
    metadata_match:
      filter_metadata:
        envoy.lb:
          version: canary
    request_mirror_policies:
    - cluster: mirror-cluster
    rate_limits:
    - actions:
      - request_headers:
          header_name: x-tenant
          descriptor_key: tenant
)EOF");
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"route-index", "0"}, {"x-override", "canary"}, {"x-hash", "tenant-a"}};
  auto route = provider.value()->produceRoute(nullptr, headers, stream_info_, 42);
  ASSERT_NE(nullptr, route);
  const auto* entry = route->routeEntry();
  ASSERT_NE(nullptr, entry);
  EXPECT_NE(nullptr, entry->hashPolicy());
  EXPECT_NE(nullptr, entry->metadataMatchCriteria());
  EXPECT_FALSE(entry->shadowPolicies().empty());
  EXPECT_FALSE(entry->rateLimitPolicy().empty());
}

// Selecting an override the configuration does not declare is logged and leaves the route template
// properties in effect, so the produced route still routes to the template cluster.
TEST_F(DynamicModuleRouteProviderTest, UnknownRouteActionOverrideFallsBack) {
  auto provider = createProvider("test_route_provider", 2);
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"route-index", "0"}, {"x-override", "does-not-exist"}};
  Envoy::Router::RouteConstSharedPtr route;
  EXPECT_LOG_CONTAINS("warn", "unknown route action override", {
    route = provider.value()->produceRoute(nullptr, headers, stream_info_, 42);
  });
  ASSERT_NE(nullptr, route);
  ASSERT_NE(nullptr, route->routeEntry());
  EXPECT_EQ("cluster_0", route->routeEntry()->clusterName());
}

// A selected per-route configuration override that replaces the route-level tracing applies it to
// the produced route rather than falling back to the route template, which carries none.
TEST_F(DynamicModuleRouteProviderTest, SelectedPerRouteConfigOverrideAppliesTracing) {
  auto provider = createProvider("test_route_provider", 2, R"EOF(
per_route_config_overrides:
  trace:
    tracing:
      overall_sampling:
        numerator: 100
        denominator: MILLION
)EOF");
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"route-index", "0"}, {"x-per-route-config", "trace"}};
  auto route = provider.value()->produceRoute(nullptr, headers, stream_info_, 42);
  ASSERT_NE(nullptr, route);
  const auto* tracing = route->tracingConfig();
  ASSERT_NE(nullptr, tracing);
  EXPECT_EQ(100, tracing->getOverallSampling().numerator());
  EXPECT_EQ(envoy::type::v3::FractionalPercent::MILLION,
            tracing->getOverallSampling().denominator());
}

// A selected per-route configuration override that replaces the per-filter configuration overlays
// it onto the produced route, so it is the most specific config and appears in the per-filter
// configuration chain. Without an override the route template carries none.
TEST_F(DynamicModuleRouteProviderTest, SelectedPerRouteConfigOverrideAppliesFilterConfig) {
  auto provider = createProvider("test_route_provider", 2, R"EOF(
per_route_config_overrides:
  mutate:
    typed_per_filter_config:
      envoy.filters.http.header_mutation:
        "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutationPerRoute
        mutations:
          response_mutations:
          - append:
              header:
                key: x-per-route-applied
                value: "yes"
)EOF");
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"route-index", "0"}, {"x-per-route-config", "mutate"}};
  auto route = provider.value()->produceRoute(nullptr, headers, stream_info_, 42);
  ASSERT_NE(nullptr, route);
  EXPECT_NE(nullptr, route->mostSpecificPerFilterConfig("envoy.filters.http.header_mutation"));
  EXPECT_EQ(1, route->perFilterConfigs("envoy.filters.http.header_mutation").size());

  // Without selecting the override the route template carries no per-filter configuration, so the
  // most specific config falls back to the template and the chain is empty.
  Http::TestRequestHeaderMapImpl no_override{{":path", "/"}, {"route-index", "0"}};
  auto template_route = provider.value()->produceRoute(nullptr, no_override, stream_info_, 42);
  ASSERT_NE(nullptr, template_route);
  EXPECT_EQ(nullptr,
            template_route->mostSpecificPerFilterConfig("envoy.filters.http.header_mutation"));
  EXPECT_TRUE(template_route->perFilterConfigs("envoy.filters.http.header_mutation").empty());
}

// Selecting a per-route configuration override the configuration does not declare is logged and
// leaves the route template configuration in effect, so the produced route still routes to the
// template cluster.
TEST_F(DynamicModuleRouteProviderTest, UnknownPerRouteConfigOverrideFallsBack) {
  auto provider = createProvider("test_route_provider", 2);
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"route-index", "0"}, {"x-per-route-config", "does-not-exist"}};
  Envoy::Router::RouteConstSharedPtr route;
  EXPECT_LOG_CONTAINS("warn", "unknown per-route configuration override", {
    route = provider.value()->produceRoute(nullptr, headers, stream_info_, 42);
  });
  ASSERT_NE(nullptr, route);
  ASSERT_NE(nullptr, route->routeEntry());
  EXPECT_EQ("cluster_0", route->routeEntry()->clusterName());
}

// A terminal route the module resolved the request to also applies the selected per-route
// configuration override, so its route-level tracing reflects the override rather than the
// template, which carries none.
TEST_F(DynamicModuleRouteProviderTest, TerminalRouteAppliesPerRouteConfigOverride) {
  auto provider = createProvider("test_route_provider", 2, R"EOF(
per_route_config_overrides:
  trace:
    tracing:
      overall_sampling:
        numerator: 100
        denominator: MILLION
)EOF");
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{{":path", "/"},
                                         {"route-index", "0"},
                                         {"x-direct-response", "201"},
                                         {"x-per-route-config", "trace"}};
  auto route = provider.value()->produceRoute(nullptr, headers, stream_info_, 42);
  ASSERT_NE(nullptr, route);
  ASSERT_NE(nullptr, route->directResponseEntry());
  const auto* tracing = route->tracingConfig();
  ASSERT_NE(nullptr, tracing);
  EXPECT_EQ(100, tracing->getOverallSampling().numerator());
}

// A typed metadata object that keeps the type URL of the Any it was parsed from, so a test can tell
// the module set typed route metadata that the pack parsed out of typedMetadata().
struct TestTypedRouteMetadata : public Envoy::Config::TypedMetadata::Object {
  explicit TestTypedRouteMetadata(std::string type_url) : type_url_(std::move(type_url)) {}
  const std::string type_url_;
};

// Parses the typed route metadata the module sets under envoy.test.typed_route so that a test can
// read it back through typedMetadata(), and rejects a sentinel type URL to drive the parse failure
// the pack build guards against.
class TestTypedRouteMetadataFactory : public Envoy::Router::HttpRouteTypedMetadataFactory {
public:
  std::string name() const override { return "envoy.test.typed_route"; }
  std::unique_ptr<const Envoy::Config::TypedMetadata::Object>
  parse(const Protobuf::Struct&) const override {
    return nullptr;
  }
  std::unique_ptr<const Envoy::Config::TypedMetadata::Object>
  parse(const Protobuf::Any& any) const override {
    if (any.type_url() == "t/throw") {
      throw EnvoyException("typed route metadata rejected by the test factory");
    }
    return std::make_unique<TestTypedRouteMetadata>(any.type_url());
  }
};

REGISTER_FACTORY(TestTypedRouteMetadataFactory, Envoy::Router::HttpRouteTypedMetadataFactory);

// The typed setter merges an Any that a registered factory parses, so typedMetadata() exposes the
// object the module set while the Any also reaches the proto metadata of the produced route.
TEST_F(DynamicModuleRouteProviderTest, TypedRouteMetadataLayeredOntoRoute) {
  auto provider = createProvider("test_route_provider", 2);
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"route-index", "0"}, {"x-route-typed-meta", "t/x"}};
  auto route = provider.value()->produceRoute(nullptr, headers, stream_info_, 42);
  ASSERT_NE(nullptr, route);
  const auto& typed_filter_metadata = route->metadata().typed_filter_metadata();
  ASSERT_NE(typed_filter_metadata.end(), typed_filter_metadata.find("envoy.test.typed_route"));
  EXPECT_EQ("t/x", typed_filter_metadata.at("envoy.test.typed_route").type_url());
  const auto* parsed = route->typedMetadata().get<TestTypedRouteMetadata>("envoy.test.typed_route");
  ASSERT_NE(nullptr, parsed);
  EXPECT_EQ("t/x", parsed->type_url_);
}

// A typed metadata factory can reject the Any the module sets. The pack build catches that, so the
// produced route reverts to the route template metadata rather than letting the exception reach the
// worker.
TEST_F(DynamicModuleRouteProviderTest, TypedRouteMetadataParseFailureFallsBack) {
  auto provider = createProvider("test_route_provider", 2);
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"route-index", "0"}, {"x-route-typed-meta", "throw"}};
  Envoy::Router::RouteConstSharedPtr route;
  EXPECT_LOG_CONTAINS("warn", "rejected by a typed metadata factory", {
    route = provider.value()->produceRoute(nullptr, headers, stream_info_, 42);
  });
  ASSERT_NE(nullptr, route);
  ASSERT_NE(nullptr, route->routeEntry());
  EXPECT_EQ("cluster_0", route->routeEntry()->clusterName());
  // The rejected typed metadata did not reach the route, so the template metadata is in effect and
  // it carries none.
  EXPECT_TRUE(route->metadata().typed_filter_metadata().empty());
}

// The header mutations the module recorded are appended to the route entry header transforms
// preview on top of the route template transforms, so the router observes an appended request
// header, an overwritten response header and a removed request header.
TEST_F(DynamicModuleRouteProviderTest, ModuleHeaderMutationsInTransforms) {
  auto provider = createProvider("test_route_provider", 2);
  ASSERT_TRUE(provider.ok());

  Http::TestRequestHeaderMapImpl mutate_headers{{":path", "/"},
                                                {"route-index", "0"},
                                                {"x-req-header-add", "hello"},
                                                {"x-resp-header-set", "world"}};
  auto route = provider.value()->produceRoute(nullptr, mutate_headers, stream_info_, 42);
  ASSERT_NE(nullptr, route);
  const auto* entry = route->routeEntry();
  ASSERT_NE(nullptr, entry);
  EXPECT_THAT(entry->requestHeaderTransforms(stream_info_).headers_to_append_or_add,
              Contains(Pair(Http::LowerCaseString("x-module-req"), "hello")));
  EXPECT_THAT(entry->responseHeaderTransforms(stream_info_).headers_to_overwrite_or_add,
              Contains(Pair(Http::LowerCaseString("x-module-resp"), "world")));

  Http::TestRequestHeaderMapImpl remove_headers{
      {":path", "/"}, {"route-index", "0"}, {"x-req-header-remove", "true"}};
  auto remove_route = provider.value()->produceRoute(nullptr, remove_headers, stream_info_, 42);
  ASSERT_NE(nullptr, remove_route);
  ASSERT_NE(nullptr, remove_route->routeEntry());
  EXPECT_THAT(remove_route->routeEntry()->requestHeaderTransforms(stream_info_).headers_to_remove,
              Contains(Http::LowerCaseString("x-remove-me")));
}

// A terminal route resolves to a direct response, so the response header mutations the module
// recorded are exposed through the direct response entry header transforms preview.
TEST_F(DynamicModuleRouteProviderTest, TerminalRouteResponseHeaderTransforms) {
  auto provider = createProvider("test_route_provider", 2);
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{{":path", "/"},
                                         {"route-index", "0"},
                                         {"x-direct-response", "201"},
                                         {"x-resp-header-set", "world"}};
  auto route = provider.value()->produceRoute(nullptr, headers, stream_info_, 42);
  ASSERT_NE(nullptr, route);
  const auto* direct_response = route->directResponseEntry();
  ASSERT_NE(nullptr, direct_response);
  EXPECT_THAT(direct_response->responseHeaderTransforms(stream_info_).headers_to_overwrite_or_add,
              Contains(Pair(Http::LowerCaseString("x-module-resp"), "world")));
}

// The path the module set is reflected by the current URL path accessor, so it overrides the route
// template rewrite without the request headers being finalized.
TEST_F(DynamicModuleRouteProviderTest, ModulePathReflectedInCurrentUrlPath) {
  auto provider = createProvider("test_route_provider", 2);
  ASSERT_TRUE(provider.ok());
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"route-index", "0"}, {"x-set-path", "/rewritten"}};
  auto route = provider.value()->produceRoute(nullptr, headers, stream_info_, 42);
  ASSERT_NE(nullptr, route);
  ASSERT_NE(nullptr, route->routeEntry());
  EXPECT_EQ("/rewritten", route->routeEntry()->currentUrlPathAfterRewrite(
                              headers, Formatter::Context{}, stream_info_));
}

// A route template that is itself a direct response is returned unchanged, since a route entry
// override cannot wrap it.
TEST_F(DynamicModuleRouteProviderTest, DirectResponseTemplatePassedThrough) {
  auto direct_response_template = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  NiceMock<Envoy::Router::MockDirectResponseEntry> direct_response;
  ON_CALL(*direct_response_template, directResponseEntry())
      .WillByDefault(testing::Return(&direct_response));
  std::vector<Envoy::Router::RouteEntryAndRouteConstSharedPtr> templates{direct_response_template};

  DynamicModuleRouteProviderProto proto_config;
  TestUtility::loadFromYaml(R"EOF(
dynamic_module_config:
  name: route_provider_integration_test
  do_not_close: true
provider_name: test_route_provider
)EOF",
                            proto_config);
  auto provider =
      factory_.createRouteProvider(proto_config, templates, context_, context_.initManager());
  ASSERT_TRUE(provider.ok());

  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"route-index", "0"}};
  auto route = provider.value()->produceRoute(nullptr, headers, stream_info_, 42);
  ASSERT_NE(nullptr, route);
  EXPECT_EQ(direct_response_template.get(), route.get());
  EXPECT_EQ(&direct_response, route->directResponseEntry());
}

} // namespace
} // namespace DynamicModules
} // namespace RouteProviders
} // namespace Router
} // namespace Extensions
} // namespace Envoy

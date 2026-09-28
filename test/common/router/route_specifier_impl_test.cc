#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/router/route_specifier.h"

#include "source/common/router/config_impl.h"
#include "source/common/router/delegating_route_impl.h"
#include "source/common/router/route_specifier_impl.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::IsNull;
using ::testing::NiceMock;
using ::testing::Return;

// A route that reports a name of its own while delegating everything else, so that a test can see
// which specifier produced the route it got back.
class NamedRoute : public DelegatingRoute {
public:
  NamedRoute(RouteConstSharedPtr base, std::string name)
      : DelegatingRoute(std::move(base)), name_(std::move(name)) {}

  const std::string& routeName() const override { return name_; }

private:
  const std::string name_;
};

RouteConstSharedPtr namedRoute(const std::string& name) {
  auto base = std::make_shared<NiceMock<MockRoute>>();
  return std::make_shared<NamedRoute>(std::move(base), name);
}

// Makes the specifier wrap whatever it is given in a route named `name`, recording the name of the
// route it received in `trace`.
auto wrapWith(const std::string& name, std::vector<std::string>& trace,
              OnRouteResultStatus status = OnRouteResultStatus::Continue,
              bool continue_matching = false) {
  return Invoke([&trace, name, status,
                 continue_matching](RouteConstSharedPtr route, const Http::RequestHeaderMap&,
                                    const StreamInfo::StreamInfo&, uint64_t) -> OnRouteResult {
    trace.push_back(route == nullptr ? "<none>" : route->routeName());
    RouteConstSharedPtr base =
        route != nullptr ? std::move(route) : std::make_shared<NiceMock<MockRoute>>();
    return {std::make_shared<NamedRoute>(std::move(base), name), status, continue_matching};
  });
}

// A specifier that skips its route and asks matching to carry on, without changing the route.
auto continueMatching() {
  return Invoke([](RouteConstSharedPtr route, const Http::RequestHeaderMap&,
                   const StreamInfo::StreamInfo&, uint64_t) -> OnRouteResult {
    return {std::move(route), OnRouteResultStatus::Continue, /*continue_matching=*/true};
  });
}

class RunRouteSpecifiersTest : public testing::Test {
protected:
  std::shared_ptr<NiceMock<MockRouteSpecifier>> addSpecifier() {
    auto specifier = std::make_shared<NiceMock<MockRouteSpecifier>>();
    specifiers_.push_back(specifier);
    return specifier;
  }

  OnRouteResult run(RouteConstSharedPtr route) {
    return runRouteSpecifiers(specifiers_, std::move(route), headers_, stream_info_, 0);
  }

  RouteSpecifierList specifiers_;
  Http::TestRequestHeaderMapImpl headers_{{":authority", "host"}, {":path", "/"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  std::vector<std::string> trace_;
};

// An empty chain hands the input route straight back and asks for nothing.
TEST_F(RunRouteSpecifiersTest, EmptyChainReturnsTheInput) {
  RouteConstSharedPtr route = namedRoute("matched");
  OnRouteResult result = run(route);
  EXPECT_EQ(route, result.route);
  EXPECT_EQ(OnRouteResultStatus::Continue, result.status);
  EXPECT_FALSE(result.continue_matching);
}

// An empty chain leaves a nullptr route as nullptr.
TEST_F(RunRouteSpecifiersTest, EmptyChainPreservesNoRoute) {
  OnRouteResult result = run(nullptr);
  EXPECT_EQ(nullptr, result.route);
  EXPECT_EQ(OnRouteResultStatus::Continue, result.status);
}

// Specifiers run in order and each one is handed what the previous produced.
TEST_F(RunRouteSpecifiersTest, SpecifiersRunInOrderAndFeedTheNext) {
  EXPECT_CALL(*addSpecifier(), onRoute(_, _, _, _)).WillOnce(wrapWith("first", trace_));
  EXPECT_CALL(*addSpecifier(), onRoute(_, _, _, _)).WillOnce(wrapWith("second", trace_));

  OnRouteResult result = run(namedRoute("matched"));

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("second", result.route->routeName());
  EXPECT_THAT(trace_, ElementsAre("matched", "first"));
}

// StopIteration ends the chain, so a later specifier does not run.
TEST_F(RunRouteSpecifiersTest, StopIterationEndsTheChain) {
  EXPECT_CALL(*addSpecifier(), onRoute(_, _, _, _))
      .WillOnce(wrapWith("final", trace_, OnRouteResultStatus::StopIteration));
  EXPECT_CALL(*addSpecifier(), onRoute(_, _, _, _)).Times(0);

  OnRouteResult result = run(namedRoute("matched"));

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("final", result.route->routeName());
  EXPECT_EQ(OnRouteResultStatus::StopIteration, result.status);
}

// A nullptr route is a normal value that flows to the next specifier rather than ending the chain.
TEST_F(RunRouteSpecifiersTest, NullRouteFlowsToTheNextSpecifier) {
  EXPECT_CALL(*addSpecifier(), onRoute(_, _, _, _)).WillOnce(Return(OnRouteResult{nullptr}));
  EXPECT_CALL(*addSpecifier(), onRoute(IsNull(), _, _, _)).WillOnce(wrapWith("rescued", trace_));

  OnRouteResult result = run(namedRoute("matched"));

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("rescued", result.route->routeName());
  EXPECT_THAT(trace_, ElementsAre("<none>"));
}

// A specifier that asks matching to carry on ends the chain, so a later specifier cannot overturn
// it back to using the route.
TEST_F(RunRouteSpecifiersTest, ContinueMatchingEndsTheChain) {
  EXPECT_CALL(*addSpecifier(), onRoute(_, _, _, _)).WillOnce(continueMatching());
  EXPECT_CALL(*addSpecifier(), onRoute(_, _, _, _)).Times(0);

  OnRouteResult result = run(namedRoute("matched"));

  EXPECT_TRUE(result.continue_matching);
}

// The last specifier can ask matching to carry on.
TEST_F(RunRouteSpecifiersTest, LastSpecifierCanRequestContinueMatching) {
  EXPECT_CALL(*addSpecifier(), onRoute(_, _, _, _)).WillOnce(continueMatching());

  OnRouteResult result = run(namedRoute("matched"));

  EXPECT_TRUE(result.continue_matching);
  EXPECT_EQ(OnRouteResultStatus::Continue, result.status);
}

// A specifier that ends the chain can still ask matching to carry on.
TEST_F(RunRouteSpecifiersTest, StopIterationCarriesContinueMatching) {
  EXPECT_CALL(*addSpecifier(), onRoute(_, _, _, _))
      .WillOnce(Invoke([](RouteConstSharedPtr route, const Http::RequestHeaderMap&,
                          const StreamInfo::StreamInfo&, uint64_t) -> OnRouteResult {
        return {std::move(route), OnRouteResultStatus::StopIteration, /*continue_matching=*/true};
      }));
  EXPECT_CALL(*addSpecifier(), onRoute(_, _, _, _)).Times(0);

  OnRouteResult result = run(namedRoute("matched"));

  EXPECT_TRUE(result.continue_matching);
  EXPECT_EQ(OnRouteResultStatus::StopIteration, result.status);
}

// Every specifier sees the request headers, stream info and random value of the request.
TEST_F(RunRouteSpecifiersTest, RequestContextIsForwarded) {
  EXPECT_CALL(*addSpecifier(), onRoute(_, _, _, 1234))
      .WillOnce(
          Invoke([this](RouteConstSharedPtr route, const Http::RequestHeaderMap& headers,
                        const StreamInfo::StreamInfo& stream_info, uint64_t) -> OnRouteResult {
            EXPECT_EQ(&headers_, &headers);
            EXPECT_EQ(&stream_info_, &stream_info);
            return {std::move(route)};
          }));

  RouteConstSharedPtr route = namedRoute("matched");
  EXPECT_EQ(route, runRouteSpecifiers(specifiers_, route, headers_, stream_info_, 1234).route);
}

class ApplyRouteSpecifiersTest : public testing::Test {
protected:
  std::shared_ptr<NiceMock<MockRouteSpecifier>> addSpecifier(RouteSpecifierList& list) {
    auto specifier = std::make_shared<NiceMock<MockRouteSpecifier>>();
    list.push_back(specifier);
    return specifier;
  }

  RouteConstSharedPtr apply(RouteConstSharedPtr route) {
    return applyRouteSpecifiers(std::move(route), config_specifiers_, vhost_specifiers_, headers_,
                                stream_info_, 0);
  }

  RouteSpecifierList config_specifiers_;
  RouteSpecifierList vhost_specifiers_;
  Http::TestRequestHeaderMapImpl headers_{{":authority", "host"}, {":path", "/"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  std::vector<std::string> trace_;
};

// With nothing configured on the outer levels the matched route is handed straight back.
TEST_F(ApplyRouteSpecifiersTest, EmptyChainsReturnTheInput) {
  RouteConstSharedPtr route = namedRoute("matched");
  EXPECT_EQ(route, apply(route));
}

// With nothing configured on the outer levels, a nullptr route stays nullptr.
TEST_F(ApplyRouteSpecifiersTest, EmptyChainsPreserveNoRoute) { EXPECT_EQ(nullptr, apply(nullptr)); }

// The route configuration level runs first, then the virtual host level, each fed the last result.
TEST_F(ApplyRouteSpecifiersTest, LevelsRunInOrderAndFeedTheNext) {
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _))
      .WillOnce(wrapWith("config", trace_));
  EXPECT_CALL(*addSpecifier(vhost_specifiers_), onRoute(_, _, _, _))
      .WillOnce(wrapWith("vhost", trace_));

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("vhost", result->routeName());
  EXPECT_THAT(trace_, ElementsAre("matched", "config"));
}

// Matching found nothing, and a specifier supplies a route of its own.
TEST_F(ApplyRouteSpecifiersTest, SpecifierCanGenerateRouteFromNoRoute) {
  RouteConstSharedPtr generated = namedRoute("generated");
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(IsNull(), _, _, _))
      .WillOnce(Return(OnRouteResult{generated}));

  EXPECT_EQ(generated, apply(nullptr));
}

// A specifier drops the matched route, and the next level still gets its turn.
TEST_F(ApplyRouteSpecifiersTest, DroppedRouteIsPassedOnAsNoRoute) {
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _))
      .WillOnce(Return(OnRouteResult{nullptr}));
  EXPECT_CALL(*addSpecifier(vhost_specifiers_), onRoute(IsNull(), _, _, _))
      .WillOnce(wrapWith("rescued", trace_));

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("rescued", result->routeName());
  EXPECT_THAT(trace_, ElementsAre("<none>"));
}

// StopIteration on the route configuration level skips the virtual host level.
TEST_F(ApplyRouteSpecifiersTest, StopIterationSkipsTheLaterLevel) {
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _))
      .WillOnce(wrapWith("final", trace_, OnRouteResultStatus::StopIteration));
  EXPECT_CALL(*addSpecifier(vhost_specifiers_), onRoute(_, _, _, _)).Times(0);

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("final", result->routeName());
}

// A specifier can force "no route" and have that stand, even though a nullptr route on its own
// would have been passed on to the next specifier.
TEST_F(ApplyRouteSpecifiersTest, StopIterationWithNoRouteIsFinal) {
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _))
      .WillOnce(Return(OnRouteResult{nullptr, OnRouteResultStatus::StopIteration}));
  EXPECT_CALL(*addSpecifier(vhost_specifiers_), onRoute(_, _, _, _)).Times(0);

  EXPECT_EQ(nullptr, apply(namedRoute("matched")));
}

// continue_matching has no meaning outside the route level, so an outer chain that sets it is
// ignored and the later level still runs.
TEST_F(ApplyRouteSpecifiersTest, ContinueMatchingFromAnOuterChainIsIgnored) {
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _))
      .WillOnce(wrapWith("config", trace_, OnRouteResultStatus::Continue,
                         /*continue_matching=*/true));
  EXPECT_CALL(*addSpecifier(vhost_specifiers_), onRoute(_, _, _, _))
      .WillOnce(wrapWith("vhost", trace_));

  RouteConstSharedPtr result;
  EXPECT_LOG_CONTAINS("debug", "continue_matching from an outer route specifier chain is ignored",
                      { result = apply(namedRoute("matched")); });

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("vhost", result->routeName());
  EXPECT_THAT(trace_, ElementsAre("matched", "config"));
}

// A single registered factory backs every test specifier; the string config names which one to
// hand back. Registering one factory per specifier is not possible, since factories are looked up
// by the type of their configuration proto.
class TestRouteSpecifierFactory : public RouteSpecifierFactory {
public:
  absl::StatusOr<RouteSpecifierSharedPtr>
  createRouteSpecifier(const Protobuf::Message& config,
                       RouteSpecifierFactoryContext& context) override {
    const std::string& key = dynamic_cast<const Protobuf::StringValue&>(config).value();
    auto it = specifiers_.find(key);
    if (it == specifiers_.end()) {
      return absl::InvalidArgumentError(absl::StrCat("no test route specifier named ", key));
    }
    route_builders_[key] = context.routeBuilder().ptr();
    levels_[key] = context.level();
    return it->second;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::StringValue>();
  }
  std::string name() const override { return "envoy.test.route_specifier"; }

  // The specifiers this factory can hand back, by config key. Owned by the factory, so each test
  // fixture starts with an empty one.
  absl::flat_hash_map<std::string, RouteSpecifierSharedPtr> specifiers_;
  // The route builder each specifier was created with, to check which levels can build routes.
  absl::flat_hash_map<std::string, RouteBuilder*> route_builders_;
  // The configuration level each specifier was created on.
  absl::flat_hash_map<std::string, RouteSpecifierLevel> levels_;
};

envoy::config::core::v3::TypedExtensionConfig testSpecifierConfig(const std::string& key) {
  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.test.route_specifier");
  Protobuf::StringValue value;
  value.set_value(key);
  std::ignore = config.mutable_typed_config()->PackFrom(value);
  return config;
}

class CreateRouteSpecifiersTest : public testing::Test {
protected:
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>
  configs(const std::vector<std::string>& keys) {
    Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs;
    for (const std::string& key : keys) {
      *configs.Add() = testSpecifierConfig(key);
    }
    return configs;
  }

  RouteSpecifierFactoryContextImpl
  specifierContext(RouteSpecifierLevel level = RouteSpecifierLevel::Route) {
    return {context_, {}, level};
  }

  TestRouteSpecifierFactory factory_;
  Registry::InjectFactory<RouteSpecifierFactory> registered_{factory_};
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(CreateRouteSpecifiersTest, CreatesSpecifiersInConfiguredOrder) {
  RouteSpecifierFactoryContextImpl specifier_context = specifierContext();
  auto first = std::make_shared<NiceMock<MockRouteSpecifier>>();
  auto second = std::make_shared<NiceMock<MockRouteSpecifier>>();
  factory_.specifiers_["first"] = first;
  factory_.specifiers_["second"] = second;

  auto specifiers = createRouteSpecifiers(configs({"second", "first"}), specifier_context);

  ASSERT_TRUE(specifiers.ok());
  EXPECT_THAT(*specifiers, ElementsAre(second, first));
}

TEST_F(CreateRouteSpecifiersTest, EmptyConfigCreatesNoSpecifiers) {
  RouteSpecifierFactoryContextImpl specifier_context = specifierContext();
  auto specifiers = createRouteSpecifiers(configs({}), specifier_context);

  ASSERT_TRUE(specifiers.ok());
  EXPECT_TRUE(specifiers->empty());
}

TEST_F(CreateRouteSpecifiersTest, UnknownSpecifierIsRejected) {
  RouteSpecifierFactoryContextImpl specifier_context = specifierContext();
  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.test.not_registered");
  Protobuf::UInt32Value value;
  std::ignore = config.mutable_typed_config()->PackFrom(value);
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs;
  *configs.Add() = config;

  EXPECT_THAT(
      createRouteSpecifiers(configs, specifier_context).status(),
      HasStatusMessage(HasSubstr("Didn't find a registered route specifier implementation for "
                                 "'envoy.test.not_registered'")));
}

TEST_F(CreateRouteSpecifiersTest, FactoryErrorIsPropagated) {
  RouteSpecifierFactoryContextImpl specifier_context = specifierContext();
  EXPECT_THAT(createRouteSpecifiers(configs({"missing"}), specifier_context).status(),
              HasStatusMessage("no test route specifier named missing"));
}

// A factory that reports success but hands back nothing would silently drop configuration.
TEST_F(CreateRouteSpecifiersTest, NullSpecifierIsRejected) {
  RouteSpecifierFactoryContextImpl specifier_context = specifierContext();
  factory_.specifiers_["null"] = nullptr;

  EXPECT_THAT(createRouteSpecifiers(configs({"null"}), specifier_context).status(),
              HasStatusMessage(HasSubstr("produced a null specifier")));
}

// The factory context reports the level it was built for, so a factory can reject an option that is
// only valid on one level.
TEST_F(CreateRouteSpecifiersTest, LevelIsExposedToTheFactory) {
  EXPECT_EQ(RouteSpecifierLevel::RouteConfiguration,
            specifierContext(RouteSpecifierLevel::RouteConfiguration).level());
  EXPECT_EQ(RouteSpecifierLevel::VirtualHost,
            specifierContext(RouteSpecifierLevel::VirtualHost).level());
  EXPECT_EQ(RouteSpecifierLevel::Route, specifierContext(RouteSpecifierLevel::Route).level());
}

// Tests that the three levels of the route configuration are wired to the chain, driven through
// the public ConfigImpl::route() entry point.
class RouteSpecifierConfigImplTest : public testing::Test {
protected:
  std::shared_ptr<NiceMock<MockRouteSpecifier>> registerSpecifier(const std::string& key) {
    auto specifier = std::make_shared<NiceMock<MockRouteSpecifier>>();
    factory_.specifiers_[key] = specifier;
    return specifier;
  }

  std::shared_ptr<ConfigImpl> config(const std::string& yaml) {
    auto config_or_error = ConfigImpl::create(
        TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(yaml), context_,
        ProtobufMessage::getNullValidationVisitor(), context_.initManager(), false);
    EXPECT_TRUE(config_or_error.ok()) << config_or_error.status();
    return config_or_error.ok() ? *config_or_error : nullptr;
  }

  VirtualHostRoute route(const Config& config) { return config.route(headers_, stream_info_, 0); }

  TestRouteSpecifierFactory factory_;
  Registry::InjectFactory<RouteSpecifierFactory> registered_{factory_};
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Http::TestRequestHeaderMapImpl headers_{
      {":authority", "host"}, {":path", "/"}, {"x-forwarded-proto", "http"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  std::vector<std::string> trace_;
};

// The route level chain runs first, during matching, then the route configuration and virtual host
// chains run on its result.
TEST_F(RouteSpecifierConfigImplTest, AllThreeLevelsApplyToTheMatchedRoute) {
  EXPECT_CALL(*registerSpecifier("config"), onRoute(_, _, _, _))
      .WillOnce(wrapWith("config", trace_));
  EXPECT_CALL(*registerSpecifier("vhost"), onRoute(_, _, _, _)).WillOnce(wrapWith("vhost", trace_));
  EXPECT_CALL(*registerSpecifier("route"), onRoute(_, _, _, _)).WillOnce(wrapWith("route", trace_));

  const std::string yaml = R"EOF(
name: config
route_specifiers:
- name: envoy.test.route_specifier
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  route_specifiers:
  - name: envoy.test.route_specifier
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: vhost
  routes:
  - match: {prefix: "/"}
    name: matched
    route: {cluster: some_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: route
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("vhost", result.route->routeName());
  EXPECT_THAT(trace_, ElementsAre("matched", "route", "config"));
  // The specifiers wrapped the matched route, so the virtual host is still the one that matched,
  // and it agrees with the route that came out of the chains.
  ASSERT_NE(nullptr, result.vhost);
  EXPECT_EQ("vhost", result.vhost->name());
  EXPECT_EQ(result.vhost.get(), std::addressof(result.route->virtualHost()));
}

// Each level is built with the configuration level it belongs to.
TEST_F(RouteSpecifierConfigImplTest, EachLevelReportsItsConfigurationLevel) {
  registerSpecifier("config");
  registerSpecifier("vhost");
  registerSpecifier("route");

  const std::string yaml = R"EOF(
name: config
route_specifiers:
- name: envoy.test.route_specifier
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  route_specifiers:
  - name: envoy.test.route_specifier
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: vhost
  routes:
  - match: {prefix: "/"}
    name: matched
    route: {cluster: some_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: route
  )EOF";

  ASSERT_NE(nullptr, config(yaml));
  EXPECT_EQ(RouteSpecifierLevel::RouteConfiguration, factory_.levels_.at("config"));
  EXPECT_EQ(RouteSpecifierLevel::VirtualHost, factory_.levels_.at("vhost"));
  EXPECT_EQ(RouteSpecifierLevel::Route, factory_.levels_.at("route"));
}

// A route level specifier can drop its route and let matching carry on to the next route.
TEST_F(RouteSpecifierConfigImplTest, RouteLevelContinueMatchingSkipsToTheNextRoute) {
  EXPECT_CALL(*registerSpecifier("skipper"), onRoute(_, _, _, _)).WillOnce(continueMatching());

  const std::string yaml = R"EOF(
name: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  routes:
  - match: {prefix: "/"}
    name: catch_all
    route: {cluster: some_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: skipper
  - match: {prefix: "/specific"}
    name: specific
    route: {cluster: some_cluster}
  )EOF";

  headers_.setPath("/specific");
  VirtualHostRoute result = route(*config(yaml));

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("specific", result.route->routeName());
}

// A route level specifier that asks to carry on when there is no next route yields no route.
TEST_F(RouteSpecifierConfigImplTest, RouteLevelContinueMatchingOnTheLastRouteYieldsNoRoute) {
  EXPECT_CALL(*registerSpecifier("skipper"), onRoute(_, _, _, _)).WillOnce(continueMatching());

  const std::string yaml = R"EOF(
name: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  routes:
  - match: {prefix: "/"}
    name: catch_all
    route: {cluster: some_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: skipper
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  EXPECT_EQ(nullptr, result.route);
}

// A route level specifier that ends the chain makes its result final, so the outer chains do not
// run.
TEST_F(RouteSpecifierConfigImplTest, RouteLevelStopIterationSkipsTheOuterChains) {
  EXPECT_CALL(*registerSpecifier("finalizer"), onRoute(_, _, _, _))
      .WillOnce(wrapWith("final", trace_, OnRouteResultStatus::StopIteration));
  EXPECT_CALL(*registerSpecifier("config"), onRoute(_, _, _, _)).Times(0);
  EXPECT_CALL(*registerSpecifier("vhost"), onRoute(_, _, _, _)).Times(0);

  const std::string yaml = R"EOF(
name: config
route_specifiers:
- name: envoy.test.route_specifier
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  route_specifiers:
  - name: envoy.test.route_specifier
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: vhost
  routes:
  - match: {prefix: "/"}
    name: matched
    route: {cluster: some_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: finalizer
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("final", result.route->routeName());
  EXPECT_THAT(trace_, ElementsAre("matched"));
}

// A route callback sees the route the route level chain produced, not the raw matched route, and
// can still decline it so matching carries on to the next route.
TEST_F(RouteSpecifierConfigImplTest, RouteCallbackSeesTheChainOutputAndCanContinue) {
  EXPECT_CALL(*registerSpecifier("wrapper"), onRoute(_, _, _, _))
      .WillOnce(wrapWith("wrapped", trace_));

  const std::string yaml = R"EOF(
name: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  routes:
  - match: {prefix: "/"}
    name: first
    route: {cluster: some_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: wrapper
  - match: {prefix: "/"}
    name: second
    route: {cluster: some_cluster}
  )EOF";

  std::vector<std::string> seen;
  RouteCallback cb = [&seen](RouteConstSharedPtr route, RouteEvalStatus eval_status) {
    seen.push_back(route->routeName());
    return eval_status == RouteEvalStatus::NoMoreRoutes ? RouteMatchStatus::Accept
                                                        : RouteMatchStatus::Continue;
  };

  VirtualHostRoute result = config(yaml)->route(cb, headers_, stream_info_, 0);

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("second", result.route->routeName());
  EXPECT_THAT(seen, ElementsAre("wrapped", "second"));
}

// A route callback never sees a route the route level chain skipped with continue_matching.
TEST_F(RouteSpecifierConfigImplTest, RouteCallbackSkipsAContinueMatchingRoute) {
  EXPECT_CALL(*registerSpecifier("skipper"), onRoute(_, _, _, _)).WillOnce(continueMatching());

  const std::string yaml = R"EOF(
name: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  routes:
  - match: {prefix: "/"}
    name: catch_all
    route: {cluster: some_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: skipper
  - match: {prefix: "/"}
    name: second
    route: {cluster: some_cluster}
  )EOF";

  std::vector<std::string> seen;
  RouteCallback cb = [&seen](RouteConstSharedPtr route, RouteEvalStatus) {
    seen.push_back(route->routeName());
    return RouteMatchStatus::Accept;
  };

  VirtualHostRoute result = config(yaml)->route(cb, headers_, stream_info_, 0);

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("second", result.route->routeName());
  EXPECT_THAT(seen, ElementsAre("second"));
}

// A route level specifier can drop its route without asking to carry on, and the route
// configuration level then supplies a fallback.
TEST_F(RouteSpecifierConfigImplTest, RouteLevelDroppedRouteFallsThroughToTheOuterLevel) {
  EXPECT_CALL(*registerSpecifier("dropper"), onRoute(_, _, _, _))
      .WillOnce(Return(OnRouteResult{nullptr}));
  EXPECT_CALL(*registerSpecifier("config"), onRoute(IsNull(), _, _, _))
      .WillOnce(wrapWith("config", trace_));

  const std::string yaml = R"EOF(
name: config
route_specifiers:
- name: envoy.test.route_specifier
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  routes:
  - match: {prefix: "/"}
    name: matched
    route: {cluster: some_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: dropper
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("config", result.route->routeName());
}

// A route level specifier that drops its route with no outer level to rescue it leaves the request
// with no route.
TEST_F(RouteSpecifierConfigImplTest, RouteLevelDroppedRouteWithoutFallbackIsNoRoute) {
  EXPECT_CALL(*registerSpecifier("dropper"), onRoute(_, _, _, _))
      .WillOnce(Return(OnRouteResult{nullptr}));

  const std::string yaml = R"EOF(
name: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  routes:
  - match: {prefix: "/"}
    name: matched
    route: {cluster: some_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: dropper
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  EXPECT_EQ(nullptr, result.route);
}

// A route level specifier that swaps in a route from another virtual host, with no outer level
// running, still brings that route's virtual host along.
TEST_F(RouteSpecifierConfigImplTest, RouteLevelVirtualHostFollowsTheReturnedRoute) {
  auto replacement = std::make_shared<NiceMock<MockRoute>>();
  EXPECT_CALL(*registerSpecifier("swap"), onRoute(_, _, _, _))
      .WillOnce(Invoke([replacement](RouteConstSharedPtr, const Http::RequestHeaderMap&,
                                     const StreamInfo::StreamInfo&,
                                     uint64_t) -> OnRouteResult { return {replacement}; }));

  const std::string yaml = R"EOF(
name: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  routes:
  - match: {prefix: "/"}
    name: matched
    route: {cluster: some_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: swap
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  ASSERT_EQ(replacement.get(), result.route.get());
  // The virtual host is taken from the returned route, not the one that matched.
  ASSERT_NE(nullptr, result.vhost);
  EXPECT_EQ(replacement->virtualHostSharedPtr(), result.vhost);
}

// The route configuration and virtual host levels run for a request that matched no route, so a
// specifier can answer for it.
TEST_F(RouteSpecifierConfigImplTest, LevelsAboveTheRouteRunWhenNothingMatched) {
  EXPECT_CALL(*registerSpecifier("config"), onRoute(IsNull(), _, _, _))
      .WillOnce(wrapWith("config", trace_));
  EXPECT_CALL(*registerSpecifier("vhost"), onRoute(_, _, _, _)).WillOnce(wrapWith("vhost", trace_));
  // Never reached: there is no matched route to read a route level chain from.
  EXPECT_CALL(*registerSpecifier("route"), onRoute(_, _, _, _)).Times(0);

  const std::string yaml = R"EOF(
name: config
route_specifiers:
- name: envoy.test.route_specifier
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  route_specifiers:
  - name: envoy.test.route_specifier
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: vhost
  routes:
  - match: {prefix: "/no-such-prefix"}
    name: matched
    route: {cluster: some_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: route
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("vhost", result.route->routeName());
  EXPECT_THAT(trace_, ElementsAre("<none>", "config"));
  // The specifiers supplied a route of their own, and the virtual host follows that route rather
  // than staying on the one that matched.
  ASSERT_NE(nullptr, result.vhost);
  EXPECT_EQ(result.vhost.get(), std::addressof(result.route->virtualHost()));
}

// No virtual host matched, so only the route configuration level is reachable.
TEST_F(RouteSpecifierConfigImplTest, OnlyTheConfigLevelRunsWhenNoVirtualHostMatched) {
  EXPECT_CALL(*registerSpecifier("config"), onRoute(IsNull(), _, _, _))
      .WillOnce(wrapWith("config", trace_));
  EXPECT_CALL(*registerSpecifier("vhost"), onRoute(_, _, _, _)).Times(0);

  const std::string yaml = R"EOF(
name: config
route_specifiers:
- name: envoy.test.route_specifier
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: config
virtual_hosts:
- name: vhost
  domains: ["other.example.com"]
  route_specifiers:
  - name: envoy.test.route_specifier
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: vhost
  routes:
  - match: {prefix: "/"}
    route: {cluster: some_cluster}
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("config", result.route->routeName());
  // No virtual host matched, but a specifier answered with a route, so the virtual host is taken
  // from that route instead of being left null.
  ASSERT_NE(nullptr, result.vhost);
  EXPECT_EQ(result.vhost.get(), std::addressof(result.route->virtualHost()));
}

// A route configuration that fails to build its specifiers fails to build at all.
TEST_F(RouteSpecifierConfigImplTest, UnknownSpecifierFailsConfigLoad) {
  const std::string yaml = R"EOF(
name: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  routes:
  - match: {prefix: "/"}
    route: {cluster: some_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: missing
  )EOF";

  EXPECT_THAT(
      ConfigImpl::create(TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(yaml),
                         context_, ProtobufMessage::getNullValidationVisitor(),
                         context_.initManager(), false)
          .status(),
      HasStatusMessage("no test route specifier named missing"));
}

// A specifier that swaps in a route from somewhere else brings that route's virtual host along, so
// the result never reports a virtual host the route does not belong to.
TEST_F(RouteSpecifierConfigImplTest, VirtualHostFollowsTheRouteTheSpecifierReturned) {
  auto replacement = std::make_shared<NiceMock<MockRoute>>();
  EXPECT_CALL(*registerSpecifier("config"), onRoute(_, _, _, _))
      .WillOnce(Invoke([replacement](RouteConstSharedPtr, const Http::RequestHeaderMap&,
                                     const StreamInfo::StreamInfo&,
                                     uint64_t) -> OnRouteResult { return {replacement}; }));

  const std::string yaml = R"EOF(
name: config
route_specifiers:
- name: envoy.test.route_specifier
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  routes:
  - match: {prefix: "/"}
    name: matched
    route: {cluster: some_cluster}
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  ASSERT_EQ(replacement.get(), result.route.get());
  // Not the virtual host that matched the request: the one the returned route reports.
  ASSERT_NE(nullptr, result.vhost);
  EXPECT_EQ(replacement->virtualHostSharedPtr(), result.vhost);
  EXPECT_EQ(result.vhost.get(), std::addressof(result.route->virtualHost()));
}

} // namespace
} // namespace Router
} // namespace Envoy

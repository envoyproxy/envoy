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
              OnRouteStatus status = OnRouteStatus::Continue) {
  return Invoke([&trace, name, status](RouteConstSharedPtr route, const Http::RequestHeaderMap&,
                                       const StreamInfo::StreamInfo&, uint64_t,
                                       OnRouteInputStatus) -> OnRouteResult {
    trace.push_back(route == nullptr ? "<none>" : route->routeName());
    RouteConstSharedPtr base =
        route != nullptr ? std::move(route) : std::make_shared<NiceMock<MockRoute>>();
    return {std::make_shared<NamedRoute>(std::move(base), name), status};
  });
}

class ApplyRouteSpecifiersTest : public testing::Test {
protected:
  std::shared_ptr<NiceMock<MockRouteSpecifier>> addSpecifier(RouteSpecifierList& list) {
    auto specifier = std::make_shared<NiceMock<MockRouteSpecifier>>();
    list.push_back(specifier);
    return specifier;
  }

  RouteConstSharedPtr apply(RouteConstSharedPtr route,
                            OnRouteInputStatus input_status = OnRouteInputStatus::Unspecified) {
    OnRouteResult result =
        applyRouteSpecifiers(std::move(route), config_specifiers_, vhost_specifiers_,
                             route_specifiers_, headers_, stream_info_, 0, input_status);
    status_ = result.status;
    return std::move(result.route);
  }

  RouteSpecifierList config_specifiers_;
  RouteSpecifierList vhost_specifiers_;
  RouteSpecifierList route_specifiers_;
  Http::TestRequestHeaderMapImpl headers_{{":authority", "host"}, {":path", "/"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  std::vector<std::string> trace_;
  OnRouteStatus status_{OnRouteStatus::Continue};
};

// With nothing configured at any level the matched route is handed straight back.
TEST_F(ApplyRouteSpecifiersTest, EmptyChainsReturnTheInput) {
  RouteConstSharedPtr route = namedRoute("matched");
  EXPECT_EQ(route, apply(route));
}

// With nothing configured at any level, a nullptr route stays nullptr.
TEST_F(ApplyRouteSpecifiersTest, EmptyChainsPreserveNoRoute) { EXPECT_EQ(nullptr, apply(nullptr)); }

// Levels run route first, then virtual host, then route configuration, and each specifier is
// handed what the previous one produced.
TEST_F(ApplyRouteSpecifiersTest, LevelsRunInOrderAndFeedTheNext) {
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("config", trace_));
  EXPECT_CALL(*addSpecifier(vhost_specifiers_), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("vhost", trace_));
  EXPECT_CALL(*addSpecifier(route_specifiers_), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("route", trace_));

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("config", result->routeName());
  EXPECT_THAT(trace_, ElementsAre("matched", "route", "vhost"));
}

// Specifiers of one level run in configuration order.
TEST_F(ApplyRouteSpecifiersTest, SpecifiersOfOneLevelRunInOrder) {
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("first", trace_));
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("second", trace_));

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("second", result->routeName());
  EXPECT_THAT(trace_, ElementsAre("matched", "first"));
}

// Matching found nothing, and a specifier supplies a route of its own.
TEST_F(ApplyRouteSpecifiersTest, SpecifierCanGenerateRouteFromNoRoute) {
  RouteConstSharedPtr generated = namedRoute("generated");
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(IsNull(), _, _, _, _))
      .WillOnce(Return(OnRouteResult{generated}));

  EXPECT_EQ(generated, apply(nullptr));
}

// A specifier drops the matched route, and the next one still gets its turn.
TEST_F(ApplyRouteSpecifiersTest, DroppedRouteIsPassedOnAsNoRoute) {
  EXPECT_CALL(*addSpecifier(vhost_specifiers_), onRoute(_, _, _, _, _))
      .WillOnce(Return(OnRouteResult{nullptr}));
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(IsNull(), _, _, _, _))
      .WillOnce(wrapWith("rescued", trace_));

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("rescued", result->routeName());
  EXPECT_THAT(trace_, ElementsAre("<none>"));
}

// Nothing rescues the route, so the request ends up with none.
TEST_F(ApplyRouteSpecifiersTest, RouteDroppedByTheLastSpecifierIsTheResult) {
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _, _))
      .WillOnce(Return(OnRouteResult{nullptr}));

  EXPECT_EQ(nullptr, apply(namedRoute("matched")));
}

// StopIteration skips the rest of its own level.
TEST_F(ApplyRouteSpecifiersTest, StopIterationSkipsTheRestOfTheLevel) {
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("final", trace_, OnRouteStatus::StopIteration));
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _, _)).Times(0);

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("final", result->routeName());
}

// StopIteration ends the whole chain, not just the level that raised it.
TEST_F(ApplyRouteSpecifiersTest, StopIterationSkipsTheLaterLevels) {
  EXPECT_CALL(*addSpecifier(route_specifiers_), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("final", trace_, OnRouteStatus::StopIteration));
  EXPECT_CALL(*addSpecifier(vhost_specifiers_), onRoute(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _, _)).Times(0);

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("final", result->routeName());
}

// A specifier can force "no route" and have that stand, even though a nullptr route on its own
// would have been passed on to the next specifier.
TEST_F(ApplyRouteSpecifiersTest, StopIterationWithNoRouteIsFinal) {
  EXPECT_CALL(*addSpecifier(vhost_specifiers_), onRoute(_, _, _, _, _))
      .WillOnce(Return(OnRouteResult{nullptr, OnRouteStatus::StopIteration}));
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _, _)).Times(0);

  EXPECT_EQ(nullptr, apply(namedRoute("matched")));
}

// Every specifier sees the request headers, stream info and random value of the request.
TEST_F(ApplyRouteSpecifiersTest, RequestContextIsForwarded) {
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, 1234, _))
      .WillOnce(Invoke([this](RouteConstSharedPtr route, const Http::RequestHeaderMap& headers,
                              const StreamInfo::StreamInfo& stream_info, uint64_t,
                              OnRouteInputStatus) -> OnRouteResult {
        EXPECT_EQ(&headers_, &headers);
        EXPECT_EQ(&stream_info_, &stream_info);
        return {std::move(route)};
      }));

  RouteConstSharedPtr route = namedRoute("matched");
  EXPECT_EQ(route,
            applyRouteSpecifiers(route, config_specifiers_, vhost_specifiers_, route_specifiers_,
                                 headers_, stream_info_, 1234, OnRouteInputStatus::Unspecified)
                .route);
}

// A single registered factory backs every test specifier; the string config names which one to
// hand back. Registering one factory per specifier is not possible, since factories are looked up
// by the type of their configuration proto.
class TestRouteSpecifierFactory : public RouteSpecifierFactory {
public:
  absl::StatusOr<RouteSpecifierSharedPtr>
  createRouteSpecifier(const Protobuf::Message& config,
                       RouteSpecifierFactoryContext& context) override {
    const std::string& key =
        Envoy::Protobuf::DynamicCastMessage<Protobuf::StringValue>(config).value();
    auto it = specifiers_.find(key);
    if (it == specifiers_.end()) {
      return absl::InvalidArgumentError(absl::StrCat("no test route specifier named ", key));
    }
    route_builders_[key] = context.routeBuilder().ptr();
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

  RouteSpecifierFactoryContextImpl specifierContext() { return {context_, {}}; }

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

TEST_F(RouteSpecifierConfigImplTest, AllThreeLevelsApplyToTheMatchedRoute) {
  EXPECT_CALL(*registerSpecifier("config"), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("config", trace_));
  EXPECT_CALL(*registerSpecifier("vhost"), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("vhost", trace_));
  EXPECT_CALL(*registerSpecifier("route"), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("route", trace_));

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
  EXPECT_EQ("config", result.route->routeName());
  EXPECT_THAT(trace_, ElementsAre("matched", "route", "vhost"));
  // The specifiers wrapped the matched route, so the virtual host is still the one that matched,
  // and it agrees with the route that came out of the chains.
  ASSERT_NE(nullptr, result.vhost);
  EXPECT_EQ("vhost", result.vhost->name());
  EXPECT_EQ(result.vhost.get(), std::addressof(result.route->virtualHost()));
}

// The route configuration and virtual host levels run for a request that matched no route, so an
// specifier can answer for it.
TEST_F(RouteSpecifierConfigImplTest, LevelsAboveTheRouteRunWhenNothingMatched) {
  EXPECT_CALL(*registerSpecifier("vhost"), onRoute(IsNull(), _, _, _, _))
      .WillOnce(wrapWith("vhost", trace_));
  EXPECT_CALL(*registerSpecifier("config"), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("config", trace_));
  // Never reached: there is no matched route to read a route level chain from.
  EXPECT_CALL(*registerSpecifier("route"), onRoute(_, _, _, _, _)).Times(0);

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
  EXPECT_EQ("config", result.route->routeName());
  EXPECT_THAT(trace_, ElementsAre("<none>", "vhost"));
  // The specifiers supplied a route of their own, and the virtual host follows that route rather
  // than staying on the one that matched.
  ASSERT_NE(nullptr, result.vhost);
  EXPECT_EQ(result.vhost.get(), std::addressof(result.route->virtualHost()));
}

// No virtual host matched, so only the route configuration level is reachable.
TEST_F(RouteSpecifierConfigImplTest, OnlyTheConfigLevelRunsWhenNoVirtualHostMatched) {
  EXPECT_CALL(*registerSpecifier("config"), onRoute(IsNull(), _, _, _, _))
      .WillOnce(wrapWith("config", trace_));
  EXPECT_CALL(*registerSpecifier("vhost"), onRoute(_, _, _, _, _)).Times(0);

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
  EXPECT_CALL(*registerSpecifier("config"), onRoute(_, _, _, _, _))
      .WillOnce(
          Invoke([replacement](RouteConstSharedPtr, const Http::RequestHeaderMap&,
                               const StreamInfo::StreamInfo&, uint64_t,
                               OnRouteInputStatus) -> OnRouteResult { return {replacement}; }));

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

// Makes the specifier hand back the route it was given together with `status`, recording the
// name of the route it received in `trace`.
auto passWith(std::vector<std::string>& trace, OnRouteStatus status = OnRouteStatus::Continue) {
  return Invoke([&trace, status](RouteConstSharedPtr route, const Http::RequestHeaderMap&,
                                 const StreamInfo::StreamInfo&, uint64_t,
                                 OnRouteInputStatus) -> OnRouteResult {
    trace.push_back(route == nullptr ? "<none>" : route->routeName());
    return {std::move(route), status};
  });
}

// A chain that runs to completion accepts its result: the status that comes out is Continue.
TEST_F(ApplyRouteSpecifiersTest, CompletedChainsAcceptTheResult) {
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("config", trace_));

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("config", result->routeName());
  EXPECT_EQ(OnRouteStatus::Continue, status_);
}

// The status of the specifier that ended the chain comes out of the chains unchanged.
TEST_F(ApplyRouteSpecifiersTest, StopIterationComesOutOfTheChains) {
  EXPECT_CALL(*addSpecifier(vhost_specifiers_), onRoute(_, _, _, _, _))
      .WillOnce(passWith(trace_, OnRouteStatus::StopIteration));
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _, _)).Times(0);

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("matched", result->routeName());
  EXPECT_EQ(OnRouteStatus::StopIteration, status_);
}

// StopIterationAndSkipRoute ends the chain at once, exactly like StopIteration: no later
// specifier runs, at this level or any of the levels after it, and the skip comes out of the
// chains for route matching to act on.
TEST_F(ApplyRouteSpecifiersTest, SkipRouteEndsTheChainImmediately) {
  EXPECT_CALL(*addSpecifier(route_specifiers_), onRoute(_, _, _, _, _))
      .WillOnce(passWith(trace_, OnRouteStatus::StopIterationAndSkipRoute));
  EXPECT_CALL(*addSpecifier(vhost_specifiers_), onRoute(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*addSpecifier(config_specifiers_), onRoute(_, _, _, _, _)).Times(0);

  apply(namedRoute("matched"));

  EXPECT_EQ(OnRouteStatus::StopIterationAndSkipRoute, status_);
  EXPECT_THAT(trace_, ElementsAre("matched"));
}

// A specifier turns the first route that matched down, which ends the chains for that route at
// once, and route matching moves on to the next one, which runs the chains of its own.
TEST_F(RouteSpecifierConfigImplTest, SkipRouteMovesOnToTheNextRoute) {
  EXPECT_CALL(*registerSpecifier("vhost"), onRoute(_, _, _, _, _)).WillOnce(passWith(trace_));
  EXPECT_CALL(*registerSpecifier("route"), onRoute(_, _, _, _, _))
      .WillOnce(passWith(trace_, OnRouteStatus::StopIterationAndSkipRoute));

  const std::string yaml = R"EOF(
name: config
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
    name: first
    route: {cluster: first_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: route
  - match: {prefix: "/"}
    name: second
    route: {cluster: second_cluster}
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("second", result.route->routeName());
  // The route level of "first" skipped the route, so the virtual host level only ever ran for
  // "second".
  EXPECT_THAT(trace_, ElementsAre("first", "second"));
  ASSERT_NE(nullptr, result.vhost);
  EXPECT_EQ("vhost", result.vhost->name());
}

// Dropping the route is not asking to continue: the match is accepted with no route, the next
// route is not evaluated and the levels above the route do not run again.
TEST_F(RouteSpecifierConfigImplTest, DroppedRouteIsFinal) {
  EXPECT_CALL(*registerSpecifier("vhost"), onRoute(_, _, _, _, _)).WillOnce(passWith(trace_));
  EXPECT_CALL(*registerSpecifier("route"), onRoute(_, _, _, _, _))
      .WillOnce(Return(OnRouteResult{nullptr}));

  const std::string yaml = R"EOF(
name: config
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
    name: first
    route: {cluster: first_cluster}
    route_specifiers:
    - name: envoy.test.route_specifier
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: route
  - match: {prefix: "/"}
    name: second
    route: {cluster: second_cluster}
  )EOF";

  bool callback_called = false;
  VirtualHostRoute result = config(yaml)->route(
      [&callback_called](RouteConstSharedPtr, RouteEvalStatus) {
        callback_called = true;
        return RouteMatchStatus::Accept;
      },
      headers_, stream_info_, 0);

  EXPECT_EQ(nullptr, result.route);
  // The route level runs first, so the virtual host level is handed the dropped route.
  EXPECT_THAT(trace_, ElementsAre("<none>"));
  // There is no route for the callback to look at.
  EXPECT_FALSE(callback_called);
  ASSERT_NE(nullptr, result.vhost);
  EXPECT_EQ("vhost", result.vhost->name());
}

// Every route that matched was turned down, which leaves the request with no route, so the levels
// above the route run once more to answer for it.
TEST_F(RouteSpecifierConfigImplTest, LevelsAboveTheRouteRunAgainWhenEveryRouteWasTurnedDown) {
  EXPECT_CALL(*registerSpecifier("vhost"), onRoute(_, _, _, _, _))
      .WillOnce(passWith(trace_, OnRouteStatus::StopIterationAndSkipRoute))
      .WillOnce(passWith(trace_));

  const std::string yaml = R"EOF(
name: config
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
    name: only
    route: {cluster: some_cluster}
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  EXPECT_EQ(nullptr, result.route);
  EXPECT_THAT(trace_, ElementsAre("only", "<none>"));
  ASSERT_NE(nullptr, result.vhost);
  EXPECT_EQ("vhost", result.vhost->name());
}

// The SSL redirect route has no route entry behind it to run the chains, so the levels above the
// route run on it after the virtual host resolved it.
TEST_F(RouteSpecifierConfigImplTest, LevelsAboveTheRouteRunOnTheSslRedirectRoute) {
  EXPECT_CALL(*registerSpecifier("config"), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("config", trace_));
  EXPECT_CALL(*registerSpecifier("route"), onRoute(_, _, _, _, _)).Times(0);

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
  require_tls: ALL
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
  EXPECT_EQ("config", result.route->routeName());
  EXPECT_NE(nullptr, result.route->directResponseEntry());
  EXPECT_EQ(1, trace_.size());
}

// A route callback is handed the route that came out of the chains.
TEST_F(RouteSpecifierConfigImplTest, RouteCallbackSeesTheRouteOfTheSpecifiers) {
  EXPECT_CALL(*registerSpecifier("route"), onRoute(_, _, _, _, _))
      .WillOnce(wrapWith("route", trace_));

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
        value: route
  )EOF";

  std::string seen;
  VirtualHostRoute result = config(yaml)->route(
      [&seen](RouteConstSharedPtr route, RouteEvalStatus) {
        seen = route->routeName();
        return RouteMatchStatus::Accept;
      },
      headers_, stream_info_, 0);

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("route", result.route->routeName());
  EXPECT_EQ("route", seen);
  EXPECT_THAT(trace_, ElementsAre("matched"));
  ASSERT_NE(nullptr, result.vhost);
  EXPECT_EQ(result.vhost.get(), std::addressof(result.route->virtualHost()));
}

// The input status handed to applyRouteSpecifiers reaches the specifiers of every level
// unchanged.
TEST_F(ApplyRouteSpecifiersTest, InputStatusIsForwardedToEveryLevel) {
  EXPECT_CALL(*addSpecifier(route_specifiers_),
              onRoute(_, _, _, _, OnRouteInputStatus::HasMoreRoutes))
      .WillOnce(passWith(trace_));
  EXPECT_CALL(*addSpecifier(vhost_specifiers_),
              onRoute(_, _, _, _, OnRouteInputStatus::HasMoreRoutes))
      .WillOnce(passWith(trace_));
  EXPECT_CALL(*addSpecifier(config_specifiers_),
              onRoute(_, _, _, _, OnRouteInputStatus::HasMoreRoutes))
      .WillOnce(passWith(trace_));

  RouteConstSharedPtr route = namedRoute("matched");
  EXPECT_EQ(route, apply(route, OnRouteInputStatus::HasMoreRoutes));
}

// While a route list is being evaluated, the specifiers are told whether there are more routes to
// try after the one that matched: HasMoreRoutes for every route but the last, NoMoreRoutes for
// the last one.
TEST_F(RouteSpecifierConfigImplTest, InputStatusTellsWhetherMoreRoutesRemain) {
  auto specifier = registerSpecifier("vhost");
  EXPECT_CALL(*specifier, onRoute(_, _, _, _, OnRouteInputStatus::HasMoreRoutes))
      .WillOnce(passWith(trace_, OnRouteStatus::StopIterationAndSkipRoute));
  EXPECT_CALL(*specifier, onRoute(_, _, _, _, OnRouteInputStatus::NoMoreRoutes))
      .WillOnce(passWith(trace_));

  const std::string yaml = R"EOF(
name: config
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
    name: first
    route: {cluster: first_cluster}
  - match: {prefix: "/"}
    name: second
    route: {cluster: second_cluster}
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("second", result.route->routeName());
  EXPECT_THAT(trace_, ElementsAre("first", "second"));
}

// The run that happens when no route matched is the last chance to produce a route for the
// request, so the specifiers are told that there are no more routes to try.
TEST_F(RouteSpecifierConfigImplTest, InputStatusIsNoMoreRoutesWhenNoRouteMatched) {
  EXPECT_CALL(*registerSpecifier("vhost"),
              onRoute(IsNull(), _, _, _, OnRouteInputStatus::NoMoreRoutes))
      .WillOnce(passWith(trace_));

  const std::string yaml = R"EOF(
name: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  route_specifiers:
  - name: envoy.test.route_specifier
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: vhost
  routes:
  - match: {prefix: "/nomatch"}
    name: only
    route: {cluster: some_cluster}
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  EXPECT_EQ(nullptr, result.route);
  EXPECT_THAT(trace_, ElementsAre("<none>"));
}

} // namespace
} // namespace Router
} // namespace Envoy

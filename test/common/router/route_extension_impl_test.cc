#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/router/route_extension.h"

#include "source/common/router/config_impl.h"
#include "source/common/router/delegating_route_impl.h"
#include "source/common/router/route_extension_impl.h"

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
// which extension produced the route it got back.
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

// Makes the extension wrap whatever it is given in a route named `name`, recording the name of the
// route it received in `trace`.
auto wrapWith(const std::string& name, std::vector<std::string>& trace,
              OnRouteResultStatus status = OnRouteResultStatus::Continue) {
  return Invoke([&trace, name, status](RouteConstSharedPtr route, const Http::RequestHeaderMap&,
                                       const StreamInfo::StreamInfo&, uint64_t) -> OnRouteResult {
    trace.push_back(route == nullptr ? "<none>" : route->routeName());
    RouteConstSharedPtr base =
        route != nullptr ? std::move(route) : std::make_shared<NiceMock<MockRoute>>();
    return {std::make_shared<NamedRoute>(std::move(base), name), status};
  });
}

class ApplyRouteExtensionsTest : public testing::Test {
protected:
  std::shared_ptr<NiceMock<MockRouteExtension>> addExtension(RouteExtensionList& list) {
    auto extension = std::make_shared<NiceMock<MockRouteExtension>>();
    list.push_back(extension);
    return extension;
  }

  RouteConstSharedPtr apply(RouteConstSharedPtr route) {
    return applyRouteExtensions(std::move(route), config_extensions_, vhost_extensions_,
                                route_extensions_, headers_, stream_info_, 0);
  }

  RouteExtensionList config_extensions_;
  RouteExtensionList vhost_extensions_;
  RouteExtensionList route_extensions_;
  Http::TestRequestHeaderMapImpl headers_{{":authority", "host"}, {":path", "/"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  std::vector<std::string> trace_;
};

// With nothing configured at any level the matched route is handed straight back.
TEST_F(ApplyRouteExtensionsTest, EmptyChainsReturnTheInput) {
  RouteConstSharedPtr route = namedRoute("matched");
  EXPECT_EQ(route, apply(route));
}

// With nothing configured at any level, a nullptr route stays nullptr.
TEST_F(ApplyRouteExtensionsTest, EmptyChainsPreserveNoRoute) { EXPECT_EQ(nullptr, apply(nullptr)); }

// Levels run route configuration first, then virtual host, then route, and each extension is
// handed what the previous one produced.
TEST_F(ApplyRouteExtensionsTest, LevelsRunInOrderAndFeedTheNext) {
  EXPECT_CALL(*addExtension(config_extensions_), onRoute(_, _, _, _))
      .WillOnce(wrapWith("config", trace_));
  EXPECT_CALL(*addExtension(vhost_extensions_), onRoute(_, _, _, _))
      .WillOnce(wrapWith("vhost", trace_));
  EXPECT_CALL(*addExtension(route_extensions_), onRoute(_, _, _, _))
      .WillOnce(wrapWith("route", trace_));

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("route", result->routeName());
  EXPECT_THAT(trace_, ElementsAre("matched", "config", "vhost"));
}

// Extensions of one level run in configuration order.
TEST_F(ApplyRouteExtensionsTest, ExtensionsOfOneLevelRunInOrder) {
  EXPECT_CALL(*addExtension(config_extensions_), onRoute(_, _, _, _))
      .WillOnce(wrapWith("first", trace_));
  EXPECT_CALL(*addExtension(config_extensions_), onRoute(_, _, _, _))
      .WillOnce(wrapWith("second", trace_));

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("second", result->routeName());
  EXPECT_THAT(trace_, ElementsAre("matched", "first"));
}

// Matching found nothing, and an extension supplies a route of its own.
TEST_F(ApplyRouteExtensionsTest, ExtensionCanGenerateRouteFromNoRoute) {
  RouteConstSharedPtr generated = namedRoute("generated");
  EXPECT_CALL(*addExtension(config_extensions_), onRoute(IsNull(), _, _, _))
      .WillOnce(Return(OnRouteResult{generated}));

  EXPECT_EQ(generated, apply(nullptr));
}

// An extension drops the matched route, and the next one still gets its turn.
TEST_F(ApplyRouteExtensionsTest, DroppedRouteIsPassedOnAsNoRoute) {
  EXPECT_CALL(*addExtension(config_extensions_), onRoute(_, _, _, _))
      .WillOnce(Return(OnRouteResult{nullptr}));
  EXPECT_CALL(*addExtension(vhost_extensions_), onRoute(IsNull(), _, _, _))
      .WillOnce(wrapWith("rescued", trace_));

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("rescued", result->routeName());
  EXPECT_THAT(trace_, ElementsAre("<none>"));
}

// Nothing rescues the route, so the request ends up with none.
TEST_F(ApplyRouteExtensionsTest, RouteDroppedByTheLastExtensionIsTheResult) {
  EXPECT_CALL(*addExtension(route_extensions_), onRoute(_, _, _, _))
      .WillOnce(Return(OnRouteResult{nullptr}));

  EXPECT_EQ(nullptr, apply(namedRoute("matched")));
}

// StopIteration skips the rest of its own level.
TEST_F(ApplyRouteExtensionsTest, StopIterationSkipsTheRestOfTheLevel) {
  EXPECT_CALL(*addExtension(config_extensions_), onRoute(_, _, _, _))
      .WillOnce(wrapWith("final", trace_, OnRouteResultStatus::StopIteration));
  EXPECT_CALL(*addExtension(config_extensions_), onRoute(_, _, _, _)).Times(0);

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("final", result->routeName());
}

// StopIteration ends the whole chain, not just the level that raised it.
TEST_F(ApplyRouteExtensionsTest, StopIterationSkipsTheLaterLevels) {
  EXPECT_CALL(*addExtension(config_extensions_), onRoute(_, _, _, _))
      .WillOnce(wrapWith("final", trace_, OnRouteResultStatus::StopIteration));
  EXPECT_CALL(*addExtension(vhost_extensions_), onRoute(_, _, _, _)).Times(0);
  EXPECT_CALL(*addExtension(route_extensions_), onRoute(_, _, _, _)).Times(0);

  RouteConstSharedPtr result = apply(namedRoute("matched"));

  ASSERT_NE(nullptr, result);
  EXPECT_EQ("final", result->routeName());
}

// An extension can force "no route" and have that stand, even though a nullptr route on its own
// would have been passed on to the next extension.
TEST_F(ApplyRouteExtensionsTest, StopIterationWithNoRouteIsFinal) {
  EXPECT_CALL(*addExtension(config_extensions_), onRoute(_, _, _, _))
      .WillOnce(Return(OnRouteResult{nullptr, OnRouteResultStatus::StopIteration}));
  EXPECT_CALL(*addExtension(vhost_extensions_), onRoute(_, _, _, _)).Times(0);

  EXPECT_EQ(nullptr, apply(namedRoute("matched")));
}

// Every extension sees the request headers, stream info and random value of the request.
TEST_F(ApplyRouteExtensionsTest, RequestContextIsForwarded) {
  EXPECT_CALL(*addExtension(config_extensions_), onRoute(_, _, _, 1234))
      .WillOnce(
          Invoke([this](RouteConstSharedPtr route, const Http::RequestHeaderMap& headers,
                        const StreamInfo::StreamInfo& stream_info, uint64_t) -> OnRouteResult {
            EXPECT_EQ(&headers_, &headers);
            EXPECT_EQ(&stream_info_, &stream_info);
            return {std::move(route)};
          }));

  RouteConstSharedPtr route = namedRoute("matched");
  EXPECT_EQ(route, applyRouteExtensions(route, config_extensions_, vhost_extensions_,
                                        route_extensions_, headers_, stream_info_, 1234));
}

// A single registered factory backs every test extension; the string config names which one to
// hand back. Registering one factory per extension is not possible, since factories are looked up
// by the type of their configuration proto.
class TestRouteExtensionFactory : public RouteExtensionFactory {
public:
  absl::StatusOr<RouteExtensionSharedPtr>
  createRouteExtension(const Protobuf::Message& config,
                       Server::Configuration::ServerFactoryContext&) override {
    const std::string& key = dynamic_cast<const Protobuf::StringValue&>(config).value();
    auto it = extensions_.find(key);
    if (it == extensions_.end()) {
      return absl::InvalidArgumentError(absl::StrCat("no test route extension named ", key));
    }
    return it->second;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::StringValue>();
  }
  std::string name() const override { return "envoy.test.route_extension"; }

  // The extensions this factory can hand back, by config key. Owned by the factory, so each test
  // fixture starts with an empty one.
  absl::flat_hash_map<std::string, RouteExtensionSharedPtr> extensions_;
};

envoy::config::core::v3::TypedExtensionConfig testExtensionConfig(const std::string& key) {
  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.test.route_extension");
  Protobuf::StringValue value;
  value.set_value(key);
  std::ignore = config.mutable_typed_config()->PackFrom(value);
  return config;
}

class CreateRouteExtensionsTest : public testing::Test {
protected:
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>
  configs(const std::vector<std::string>& keys) {
    Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs;
    for (const std::string& key : keys) {
      *configs.Add() = testExtensionConfig(key);
    }
    return configs;
  }

  TestRouteExtensionFactory factory_;
  Registry::InjectFactory<RouteExtensionFactory> registered_{factory_};
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(CreateRouteExtensionsTest, CreatesExtensionsInConfiguredOrder) {
  auto first = std::make_shared<NiceMock<MockRouteExtension>>();
  auto second = std::make_shared<NiceMock<MockRouteExtension>>();
  factory_.extensions_["first"] = first;
  factory_.extensions_["second"] = second;

  auto extensions = createRouteExtensions(configs({"second", "first"}), context_);

  ASSERT_TRUE(extensions.ok());
  EXPECT_THAT(*extensions, ElementsAre(second, first));
}

TEST_F(CreateRouteExtensionsTest, EmptyConfigCreatesNoExtensions) {
  auto extensions = createRouteExtensions(configs({}), context_);

  ASSERT_TRUE(extensions.ok());
  EXPECT_TRUE(extensions->empty());
}

TEST_F(CreateRouteExtensionsTest, UnknownExtensionIsRejected) {
  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.test.not_registered");
  Protobuf::UInt32Value value;
  std::ignore = config.mutable_typed_config()->PackFrom(value);
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs;
  *configs.Add() = config;

  EXPECT_THAT(
      createRouteExtensions(configs, context_).status(),
      HasStatusMessage(HasSubstr("Didn't find a registered route extension implementation for "
                                 "'envoy.test.not_registered'")));
}

TEST_F(CreateRouteExtensionsTest, FactoryErrorIsPropagated) {
  EXPECT_THAT(createRouteExtensions(configs({"missing"}), context_).status(),
              HasStatusMessage("no test route extension named missing"));
}

// A factory that reports success but hands back nothing would silently drop configuration.
TEST_F(CreateRouteExtensionsTest, NullExtensionIsRejected) {
  factory_.extensions_["null"] = nullptr;

  EXPECT_THAT(createRouteExtensions(configs({"null"}), context_).status(),
              HasStatusMessage(HasSubstr("produced a null extension")));
}

// Tests that the three levels of the route configuration are wired to the chain, driven through
// the public ConfigImpl::route() entry point.
class RouteExtensionConfigImplTest : public testing::Test {
protected:
  std::shared_ptr<NiceMock<MockRouteExtension>> registerExtension(const std::string& key) {
    auto extension = std::make_shared<NiceMock<MockRouteExtension>>();
    factory_.extensions_[key] = extension;
    return extension;
  }

  std::shared_ptr<ConfigImpl> config(const std::string& yaml) {
    auto config_or_error = ConfigImpl::create(
        TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(yaml), context_,
        ProtobufMessage::getNullValidationVisitor(), context_.initManager(), false);
    EXPECT_TRUE(config_or_error.ok()) << config_or_error.status();
    return config_or_error.ok() ? *config_or_error : nullptr;
  }

  VirtualHostRoute route(const Config& config) { return config.route(headers_, stream_info_, 0); }

  TestRouteExtensionFactory factory_;
  Registry::InjectFactory<RouteExtensionFactory> registered_{factory_};
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Http::TestRequestHeaderMapImpl headers_{
      {":authority", "host"}, {":path", "/"}, {"x-forwarded-proto", "http"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  std::vector<std::string> trace_;
};

TEST_F(RouteExtensionConfigImplTest, AllThreeLevelsApplyToTheMatchedRoute) {
  EXPECT_CALL(*registerExtension("config"), onRoute(_, _, _, _))
      .WillOnce(wrapWith("config", trace_));
  EXPECT_CALL(*registerExtension("vhost"), onRoute(_, _, _, _)).WillOnce(wrapWith("vhost", trace_));
  EXPECT_CALL(*registerExtension("route"), onRoute(_, _, _, _)).WillOnce(wrapWith("route", trace_));

  const std::string yaml = R"EOF(
name: config
route_extensions:
- name: envoy.test.route_extension
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  route_extensions:
  - name: envoy.test.route_extension
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: vhost
  routes:
  - match: {prefix: "/"}
    name: matched
    route: {cluster: some_cluster}
    route_extensions:
    - name: envoy.test.route_extension
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: route
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("route", result.route->routeName());
  EXPECT_THAT(trace_, ElementsAre("matched", "config", "vhost"));
  // The extensions produced the route, but the virtual host is still the one that matched.
  ASSERT_NE(nullptr, result.vhost);
  EXPECT_EQ("vhost", result.vhost->name());
}

// The route configuration and virtual host levels run for a request that matched no route, so an
// extension can answer for it.
TEST_F(RouteExtensionConfigImplTest, LevelsAboveTheRouteRunWhenNothingMatched) {
  EXPECT_CALL(*registerExtension("config"), onRoute(IsNull(), _, _, _))
      .WillOnce(wrapWith("config", trace_));
  EXPECT_CALL(*registerExtension("vhost"), onRoute(_, _, _, _)).WillOnce(wrapWith("vhost", trace_));
  // Never reached: there is no matched route to read a route level chain from.
  EXPECT_CALL(*registerExtension("route"), onRoute(_, _, _, _)).Times(0);

  const std::string yaml = R"EOF(
name: config
route_extensions:
- name: envoy.test.route_extension
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  route_extensions:
  - name: envoy.test.route_extension
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: vhost
  routes:
  - match: {prefix: "/no-such-prefix"}
    name: matched
    route: {cluster: some_cluster}
    route_extensions:
    - name: envoy.test.route_extension
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: route
  )EOF";

  VirtualHostRoute result = route(*config(yaml));

  ASSERT_NE(nullptr, result.route);
  EXPECT_EQ("vhost", result.route->routeName());
  EXPECT_THAT(trace_, ElementsAre("<none>", "config"));
}

// No virtual host matched, so only the route configuration level is reachable.
TEST_F(RouteExtensionConfigImplTest, OnlyTheConfigLevelRunsWhenNoVirtualHostMatched) {
  EXPECT_CALL(*registerExtension("config"), onRoute(IsNull(), _, _, _))
      .WillOnce(wrapWith("config", trace_));
  EXPECT_CALL(*registerExtension("vhost"), onRoute(_, _, _, _)).Times(0);

  const std::string yaml = R"EOF(
name: config
route_extensions:
- name: envoy.test.route_extension
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: config
virtual_hosts:
- name: vhost
  domains: ["other.example.com"]
  route_extensions:
  - name: envoy.test.route_extension
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
  // The extension produces a route, but it is not asked to produce a virtual host.
  EXPECT_EQ(nullptr, result.vhost);
}

// A route configuration that fails to build its extensions fails to build at all.
TEST_F(RouteExtensionConfigImplTest, UnknownExtensionFailsConfigLoad) {
  const std::string yaml = R"EOF(
name: config
virtual_hosts:
- name: vhost
  domains: ["*"]
  routes:
  - match: {prefix: "/"}
    route: {cluster: some_cluster}
    route_extensions:
    - name: envoy.test.route_extension
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: missing
  )EOF";

  EXPECT_THAT(
      ConfigImpl::create(TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(yaml),
                         context_, ProtobufMessage::getNullValidationVisitor(),
                         context_.initManager(), false)
          .status(),
      HasStatusMessage("no test route extension named missing"));
}

} // namespace
} // namespace Router
} // namespace Envoy

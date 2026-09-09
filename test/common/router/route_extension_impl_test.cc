#include <tuple>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/router/route_extension.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/router/route_extension_impl.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using ::testing::NiceMock;
using ::testing::Ref;
using ::testing::Return;

// A route extension that runs a function on the route, used to drive the chain in tests.
class FunctionRouteExtension : public RouteExtension {
public:
  using RouteFunction = std::function<RouteConstSharedPtr(RouteConstSharedPtr)>;
  explicit FunctionRouteExtension(RouteFunction function) : function_(std::move(function)) {}

  RouteConstSharedPtr onRoute(RouteConstSharedPtr route, const Http::RequestHeaderMap&,
                              const StreamInfo::StreamInfo&, uint64_t) const override {
    return function_(std::move(route));
  }

private:
  const RouteFunction function_;
};

RouteExtensionSharedPtr functionExtension(FunctionRouteExtension::RouteFunction function) {
  return std::make_shared<FunctionRouteExtension>(std::move(function));
}

class MockRouteExtension : public RouteExtension {
public:
  MOCK_METHOD(RouteConstSharedPtr, onRoute,
              (RouteConstSharedPtr, const Http::RequestHeaderMap&, const StreamInfo::StreamInfo&,
               uint64_t),
              (const, override));
};

// A route extension whose validateClusters returns a fixed status, used to drive cluster
// validation.
class ValidatingRouteExtension : public RouteExtension {
public:
  explicit ValidatingRouteExtension(absl::Status status) : status_(std::move(status)) {}

  RouteConstSharedPtr onRoute(RouteConstSharedPtr route, const Http::RequestHeaderMap&,
                              const StreamInfo::StreamInfo&, uint64_t) const override {
    return route;
  }
  absl::Status validateClusters(const Upstream::ClusterManager&) const override { return status_; }

private:
  const absl::Status status_;
};

// A factory for tests. The value of the string config selects the behavior at creation time.
class TestRouteExtensionFactory : public RouteExtensionFactory {
public:
  absl::StatusOr<RouteExtensionSharedPtr>
  createRouteExtension(const Protobuf::Message& config,
                       Server::Configuration::ServerFactoryContext&) override {
    const auto& struct_config = dynamic_cast<const Protobuf::Struct&>(config);
    const auto mode = struct_config.fields().find("mode");
    const std::string value =
        mode != struct_config.fields().end() ? mode->second.string_value() : "";
    if (value == "null") {
      return RouteExtensionSharedPtr(nullptr);
    }
    if (value == "error") {
      return absl::InvalidArgumentError("route extension configuration is invalid");
    }
    return functionExtension([](RouteConstSharedPtr route) { return route; });
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return "envoy.router.route_extension.test"; }
};

envoy::config::core::v3::TypedExtensionConfig structExtensionConfig(absl::string_view mode) {
  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("test");
  Protobuf::Struct value;
  (*value.mutable_fields())["mode"].set_string_value(std::string(mode));
  std::ignore = config.mutable_typed_config()->PackFrom(value);
  return config;
}

Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>
extensionConfigs(std::initializer_list<absl::string_view> modes) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs;
  for (const absl::string_view mode : modes) {
    *configs.Add() = structExtensionConfig(mode);
  }
  return configs;
}

class CreateRouteExtensionsTest : public testing::Test {
public:
  TestRouteExtensionFactory factory_;
  Registry::InjectFactory<RouteExtensionFactory> registration_{factory_};
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(CreateRouteExtensionsTest, Empty) {
  auto extensions = createRouteExtensions(extensionConfigs({}), context_);
  ASSERT_TRUE(extensions.ok());
  EXPECT_TRUE(extensions->empty());
}

TEST_F(CreateRouteExtensionsTest, ValidChain) {
  auto extensions = createRouteExtensions(extensionConfigs({"", ""}), context_);
  ASSERT_TRUE(extensions.ok());
  EXPECT_EQ(2, extensions->size());
}

TEST_F(CreateRouteExtensionsTest, UnknownExtensionRejected) {
  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("test");
  config.mutable_typed_config()->set_type_url("type.googleapis.com/test.Unknown");
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs;
  *configs.Add() = config;
  EXPECT_FALSE(createRouteExtensions(configs, context_).ok());
}

TEST_F(CreateRouteExtensionsTest, NullExtensionRejected) {
  EXPECT_FALSE(createRouteExtensions(extensionConfigs({"null"}), context_).ok());
}

TEST_F(CreateRouteExtensionsTest, FactoryErrorPropagated) {
  EXPECT_FALSE(createRouteExtensions(extensionConfigs({"error"}), context_).ok());
}

class RunRouteExtensionsTest : public testing::Test {
public:
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Http::TestRequestHeaderMapImpl headers_;
};

TEST_F(RunRouteExtensionsTest, EmptyReturnsInput) {
  RouteConstSharedPtr input = std::make_shared<NiceMock<MockRoute>>();
  EXPECT_EQ(input, runRouteExtensions({}, input, headers_, stream_info_, 0));
}

TEST_F(RunRouteExtensionsTest, EachExtensionReceivesThePreviousOutput) {
  RouteConstSharedPtr route_a = std::make_shared<NiceMock<MockRoute>>();
  RouteConstSharedPtr route_b = std::make_shared<NiceMock<MockRoute>>();
  RouteExtensionList extensions;
  extensions.push_back(functionExtension([route_a](RouteConstSharedPtr) { return route_a; }));
  extensions.push_back(functionExtension([route_a, route_b](RouteConstSharedPtr route) {
    EXPECT_EQ(route_a, route);
    return route_b;
  }));
  EXPECT_EQ(route_b, runRouteExtensions(extensions, nullptr, headers_, stream_info_, 0));
}

TEST_F(RunRouteExtensionsTest, NullRouteFlowsThroughTheChain) {
  bool second_ran = false;
  RouteExtensionList extensions;
  extensions.push_back(
      functionExtension([](RouteConstSharedPtr) { return RouteConstSharedPtr(nullptr); }));
  extensions.push_back(functionExtension([&second_ran](RouteConstSharedPtr route) {
    second_ran = true;
    EXPECT_EQ(nullptr, route);
    return route;
  }));
  RouteConstSharedPtr input = std::make_shared<NiceMock<MockRoute>>();
  EXPECT_EQ(nullptr, runRouteExtensions(extensions, input, headers_, stream_info_, 0));
  EXPECT_TRUE(second_ran);
}

TEST_F(RunRouteExtensionsTest, ForwardsHeadersStreamInfoAndRandomValue) {
  auto extension = std::make_shared<MockRouteExtension>();
  RouteExtensionList extensions;
  extensions.push_back(extension);
  RouteConstSharedPtr input = std::make_shared<NiceMock<MockRoute>>();
  EXPECT_CALL(*extension, onRoute(input, Ref(headers_), Ref(stream_info_), 42))
      .WillOnce(Return(input));
  EXPECT_EQ(input, runRouteExtensions(extensions, input, headers_, stream_info_, 42));
}

class ValidateRouteExtensionClustersTest : public testing::Test {
public:
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
};

TEST_F(ValidateRouteExtensionClustersTest, EmptyIsOk) {
  EXPECT_TRUE(validateRouteExtensionClusters({}, cluster_manager_).ok());
}

TEST_F(ValidateRouteExtensionClustersTest, AllValidIsOk) {
  RouteExtensionList extensions;
  extensions.push_back(std::make_shared<ValidatingRouteExtension>(absl::OkStatus()));
  extensions.push_back(std::make_shared<ValidatingRouteExtension>(absl::OkStatus()));
  EXPECT_TRUE(validateRouteExtensionClusters(extensions, cluster_manager_).ok());
}

TEST_F(ValidateRouteExtensionClustersTest, FirstErrorPropagated) {
  RouteExtensionList extensions;
  extensions.push_back(std::make_shared<ValidatingRouteExtension>(absl::OkStatus()));
  extensions.push_back(
      std::make_shared<ValidatingRouteExtension>(absl::InvalidArgumentError("unknown cluster")));
  const auto status = validateRouteExtensionClusters(extensions, cluster_manager_);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ("unknown cluster", status.message());
}

// An extension that does not reference clusters uses the default, which validates nothing.
TEST_F(ValidateRouteExtensionClustersTest, DefaultValidatesNothing) {
  RouteExtensionList extensions;
  extensions.push_back(functionExtension([](RouteConstSharedPtr route) { return route; }));
  EXPECT_TRUE(validateRouteExtensionClusters(extensions, cluster_manager_).ok());
}

} // namespace
} // namespace Router
} // namespace Envoy

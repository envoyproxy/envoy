#include "envoy/extensions/filters/http/grpc_http1_reverse_bridge/v3/config.pb.h"

#include "extensions/filters/http/grpc_http1_reverse_bridge/config.h"
#include "extensions/filters/http/grpc_http1_reverse_bridge/filter.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1ReverseBridge {
namespace {

TEST(ReversBridgeFilterFactoryTest, ReverseBridgeFilter) {
  const std::string yaml_string = R"EOF(
content_type: application/grpc+proto
withhold_grpc_frames: true
  )EOF";

  envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3::FilterConfig proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  Config config_factory;
  Http::FilterFactoryCb cb =
      config_factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(ReverseBridgeFilterFactoryTest, ReverseBridgeFilterRouteSpecificConfig) {
  Config config_factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  ProtobufTypes::MessagePtr proto_config = config_factory.createEmptyRouteConfigProto();
  EXPECT_TRUE(proto_config.get());

  auto& cfg = dynamic_cast<
      envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3::FilterConfigPerRoute&>(
      *proto_config.get());
  cfg.set_disabled(true);

  Router::RouteSpecificFilterConfigConstSharedPtr route_config =
      config_factory.createRouteSpecificFilterConfig(*proto_config, factory_context,
                                                     ProtobufMessage::getNullValidationVisitor());
  EXPECT_TRUE(route_config.get());

  const auto* inflated = dynamic_cast<const FilterConfigPerRoute*>(route_config.get());
  EXPECT_TRUE(inflated);
}

} // namespace
} // namespace GrpcHttp1ReverseBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#include "envoy/config/filter/http/buffer/v2/buffer.pb.h"
#include "envoy/config/filter/http/buffer/v2/buffer.pb.validate.h"

#include "extensions/filters/http/buffer/buffer_filter.h"
#include "extensions/filters/http/buffer/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {
namespace {

TEST(BufferFilterFactoryTest, BufferFilterCorrectYaml) {
  const std::string yaml_string = R"EOF(
  max_request_bytes: 1028
  )EOF";

  envoy::config::filter::http::buffer::v2::Buffer proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  BufferFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(BufferFilterFactoryTest, BufferFilterCorrectProto) {
  envoy::config::filter::http::buffer::v2::Buffer config;
  config.mutable_max_request_bytes()->set_value(1028);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  BufferFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(BufferFilterFactoryTest, BufferFilterEmptyProto) {
  BufferFilterFactory factory;
  envoy::config::filter::http::buffer::v2::Buffer config =
      *dynamic_cast<envoy::config::filter::http::buffer::v2::Buffer*>(
          factory.createEmptyConfigProto().get());

  config.mutable_max_request_bytes()->set_value(1028);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(BufferFilterFactoryTest, BufferFilterNoMaxRequestBytes) {
  BufferFilterFactory factory;
  envoy::config::filter::http::buffer::v2::Buffer config =
      *dynamic_cast<envoy::config::filter::http::buffer::v2::Buffer*>(
          factory.createEmptyConfigProto().get());

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(factory.createFilterFactoryFromProto(config, "stats", context),
                          EnvoyException, "Proto constraint validation failed");
}

TEST(BufferFilterFactoryTest, BufferFilterEmptyRouteProto) {
  BufferFilterFactory factory;
  EXPECT_NO_THROW({
    envoy::config::filter::http::buffer::v2::BufferPerRoute* config =
        dynamic_cast<envoy::config::filter::http::buffer::v2::BufferPerRoute*>(
            factory.createEmptyRouteConfigProto().get());
    EXPECT_NE(nullptr, config);
  });
}

TEST(BufferFilterFactoryTest, BufferFilterRouteSpecificConfig) {
  BufferFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  EXPECT_TRUE(proto_config.get());

  auto& cfg =
      dynamic_cast<envoy::config::filter::http::buffer::v2::BufferPerRoute&>(*proto_config.get());
  cfg.set_disabled(true);

  Router::RouteSpecificFilterConfigConstSharedPtr route_config =
      factory.createRouteSpecificFilterConfig(*proto_config, factory_context,
                                              ProtobufMessage::getNullValidationVisitor());
  EXPECT_TRUE(route_config.get());

  const auto* inflated = dynamic_cast<const BufferFilterSettings*>(route_config.get());
  EXPECT_TRUE(inflated);
}

} // namespace
} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

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

TEST(BufferFilterFactoryTest, BufferFilterCorrectJson) {
  std::string json_string = R"EOF(
  {
    "max_request_bytes" : 1028
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  BufferFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(BufferFilterFactoryTest, BufferFilterIncorrectJson) {
  // This is incorrect because the number is quote-wrapped
  std::string json_string = R"EOF(
  {
    "max_request_bytes" : "1028"
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  BufferFilterFactory factory;
  EXPECT_THROW(factory.createFilterFactory(*json_config, "stats", context), Json::Exception);
}

TEST(BufferFilterFactoryTest, BufferFilterCorrectProto) {
  envoy::config::filter::http::buffer::v2::Buffer config{};
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

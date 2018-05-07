#include "envoy/config/filter/http/buffer/v2/buffer.pb.validate.h"

#include "extensions/filters/http/buffer/buffer_filter.h"
#include "extensions/filters/http/buffer/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

TEST(BufferFilterFactoryTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(BufferFilterFactory().createFilterFactoryFromProto(
                   envoy::config::filter::http::buffer::v2::Buffer(), "stats", context),
               ProtoValidationException);
}

TEST(BufferFilterFactoryTest, BufferFilterCorrectJson) {
  std::string json_string = R"EOF(
  {
    "max_request_bytes" : 1028,
    "max_request_time_s" : 2
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
  std::string json_string = R"EOF(
  {
    "max_request_bytes" : 1028,
    "max_request_time_s" : "2"
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
  config.mutable_max_request_time()->set_seconds(2);

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
  config.mutable_max_request_time()->set_seconds(2);

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
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  EXPECT_TRUE(proto_config.get());

  auto& cfg =
      dynamic_cast<envoy::config::filter::http::buffer::v2::BufferPerRoute&>(*proto_config.get());
  cfg.set_disabled(true);

  Router::RouteSpecificFilterConfigConstSharedPtr route_config =
      factory.createRouteSpecificFilterConfig(*proto_config, factory_context);
  EXPECT_TRUE(route_config.get());

  const auto* inflated = dynamic_cast<const BufferFilterSettings*>(route_config.get());
  EXPECT_TRUE(inflated);
}

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

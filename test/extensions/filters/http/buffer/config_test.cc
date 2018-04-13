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

TEST(BufferFilterConfigFactoryTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(BufferFilterConfigFactory().createFilterFactoryFromProto(
                   envoy::config::filter::http::buffer::v2::Buffer(), "stats", context),
               ProtoValidationException);
}

TEST(BufferFilterConfigFactoryTest, BufferFilterCorrectJson) {
  std::string json_string = R"EOF(
  {
    "max_request_bytes" : 1028,
    "max_request_time_s" : 2
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  BufferFilterConfigFactory factory;
  Server::Configuration::HttpFilterFactoryCb cb =
      factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(BufferFilterConfigFactoryTest, BufferFilterIncorrectJson) {
  std::string json_string = R"EOF(
  {
    "max_request_bytes" : 1028,
    "max_request_time_s" : "2"
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  BufferFilterConfigFactory factory;
  EXPECT_THROW(factory.createFilterFactory(*json_config, "stats", context), Json::Exception);
}

TEST(BufferFilterConfigFactoryTest, BufferFilterCorrectProto) {
  envoy::config::filter::http::buffer::v2::Buffer config{};
  config.mutable_max_request_bytes()->set_value(1028);
  config.mutable_max_request_time()->set_seconds(2);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  BufferFilterConfigFactory factory;
  Server::Configuration::HttpFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(BufferFilterConfigFactoryTest, BufferFilterEmptyProto) {
  BufferFilterConfigFactory factory;
  envoy::config::filter::http::buffer::v2::Buffer config =
      *dynamic_cast<envoy::config::filter::http::buffer::v2::Buffer*>(
          factory.createEmptyConfigProto().get());

  config.mutable_max_request_bytes()->set_value(1028);
  config.mutable_max_request_time()->set_seconds(2);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  Server::Configuration::HttpFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

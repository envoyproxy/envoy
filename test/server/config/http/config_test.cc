#include "envoy/config/filter/http/router/v2/router.pb.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/config/well_known_names.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/router/router.h"

#include "server/config/http/fault.h"
#include "server/config/http/ip_tagging.h"
#include "server/config/http/router.h"

#include "extensions/filters/http/buffer/config.h"
#include "extensions/filters/http/grpc_json_transcoder/config.h"
#include "extensions/filters/http/lua/config.h"
#include "extensions/filters/http/ratelimit/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Server {
namespace Configuration {

// Negative test for protoc-gen-validate constraints.
// TODO(mattklein123): Break this test apart into per extension tests.
TEST(HttpFilterConfigTest, ValidateFail) {
  NiceMock<MockFactoryContext> context;

  Extensions::HttpFilters::BufferFilter::BufferFilterConfigFactory buffer_factory;
  envoy::config::filter::http::buffer::v2::Buffer buffer_proto;
  FaultFilterConfig fault_factory;
  envoy::config::filter::http::fault::v2::HTTPFault fault_proto;
  fault_proto.mutable_abort();
  Extensions::HttpFilters::GrpcJsonTranscoder::GrpcJsonTranscoderFilterConfig
      grpc_json_transcoder_factory;
  envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder grpc_json_transcoder_proto;
  Extensions::HttpFilters::Lua::LuaFilterConfig lua_factory;
  envoy::config::filter::http::lua::v2::Lua lua_proto;
  Extensions::HttpFilters::RateLimitFilter::RateLimitFilterConfig rate_limit_factory;
  envoy::config::filter::http::rate_limit::v2::RateLimit rate_limit_proto;
  const std::vector<std::pair<NamedHttpFilterConfigFactory&, Protobuf::Message&>> filter_cases = {
      {buffer_factory, buffer_proto},
      {fault_factory, fault_proto},
      {grpc_json_transcoder_factory, grpc_json_transcoder_proto},
      {lua_factory, lua_proto},
      {rate_limit_factory, rate_limit_proto},
  };

  for (const auto& filter_case : filter_cases) {
    EXPECT_THROW(
        filter_case.first.createFilterFactoryFromProto(filter_case.second, "stats", context),
        ProtoValidationException);
  }
}

TEST(HttpFilterConfigTest, FaultFilterCorrectJson) {
  std::string json_string = R"EOF(
  {
    "delay" : {
      "type" : "fixed",
      "fixed_delay_percent" : 100,
      "fixed_duration_ms" : 5000
    }
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  FaultFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, FaultFilterCorrectProto) {
  envoy::config::filter::http::fault::v2::HTTPFault config{};
  config.mutable_delay()->set_percent(100);
  config.mutable_delay()->mutable_fixed_delay()->set_seconds(5);

  NiceMock<MockFactoryContext> context;
  FaultFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, InvalidFaultFilterInProto) {
  envoy::config::filter::http::fault::v2::HTTPFault config{};
  NiceMock<MockFactoryContext> context;
  FaultFilterConfig factory;
  EXPECT_THROW(factory.createFilterFactoryFromProto(config, "stats", context), EnvoyException);
}

TEST(HttpFilterConfigTest, FaultFilterEmptyProto) {
  NiceMock<MockFactoryContext> context;
  FaultFilterConfig factory;
  EXPECT_THROW(
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), "stats", context),
      EnvoyException);
}

TEST(HttpFilterConfigTest, RouterFilterInJson) {
  std::string json_string = R"EOF(
  {
    "dynamic_stats" : true,
    "start_child_span" : true
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  RouterFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, BadRouterFilterConfig) {
  std::string json_string = R"EOF(
  {
    "dynamic_stats" : true,
    "route" : {}
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  RouterFilterConfig factory;
  EXPECT_THROW(factory.createFilterFactory(*json_config, "stats", context), Json::Exception);
}

TEST(HttpFilterConigTest, RouterV2Filter) {
  envoy::config::filter::http::router::v2::Router router_config;
  router_config.mutable_dynamic_stats()->set_value(true);

  NiceMock<MockFactoryContext> context;
  RouterFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactoryFromProto(router_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, RouterFilterWithEmptyProtoConfig) {
  NiceMock<MockFactoryContext> context;
  RouterFilterConfig factory;
  HttpFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(
      (Registry::RegisterFactory<RouterFilterConfig, NamedHttpFilterConfigFactory>()),
      EnvoyException,
      fmt::format("Double registration for name: '{}'", Config::HttpFilterNames::get().ROUTER));
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy

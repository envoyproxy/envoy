#include "envoy/config/filter/http/router/v2/router.pb.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/config/well_known_names.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/router/router.h"

#include "server/config/http/buffer.h"
#include "server/config/http/fault.h"
#include "server/config/http/grpc_http1_bridge.h"
#include "server/config/http/grpc_json_transcoder.h"
#include "server/config/http/grpc_web.h"
#include "server/config/http/ip_tagging.h"
#include "server/config/http/router.h"
#include "server/config/http/squash.h"
#include "server/config/network/http_connection_manager.h"
#include "server/http/health_check.h"

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

  BufferFilterConfig buffer_factory;
  envoy::config::filter::http::buffer::v2::Buffer buffer_proto;
  FaultFilterConfig fault_factory;
  envoy::config::filter::http::fault::v2::HTTPFault fault_proto;
  fault_proto.mutable_abort();
  GrpcJsonTranscoderFilterConfig grpc_json_transcoder_factory;
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

TEST(HttpFilterConfigTest, BufferFilterCorrectJson) {
  std::string json_string = R"EOF(
  {
    "max_request_bytes" : 1028,
    "max_request_time_s" : 2
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  BufferFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, BufferFilterIncorrectJson) {
  std::string json_string = R"EOF(
  {
    "max_request_bytes" : 1028,
    "max_request_time_s" : "2"
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  BufferFilterConfig factory;
  EXPECT_THROW(factory.createFilterFactory(*json_config, "stats", context), Json::Exception);
}

TEST(HttpFilterConfigTest, BufferFilterCorrectProto) {
  envoy::config::filter::http::buffer::v2::Buffer config{};
  config.mutable_max_request_bytes()->set_value(1028);
  config.mutable_max_request_time()->set_seconds(2);

  NiceMock<MockFactoryContext> context;
  BufferFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, BufferFilterEmptyProto) {
  BufferFilterConfig factory;
  envoy::config::filter::http::buffer::v2::Buffer config =
      *dynamic_cast<envoy::config::filter::http::buffer::v2::Buffer*>(
          factory.createEmptyConfigProto().get());

  config.mutable_max_request_bytes()->set_value(1028);
  config.mutable_max_request_time()->set_seconds(2);

  NiceMock<MockFactoryContext> context;
  HttpFilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
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

TEST(HttpFilterConfigTest, GrpcHttp1BridgeFilter) {
  std::string json_string = R"EOF(
  {
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  GrpcHttp1BridgeFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, GrpcWebFilter) {
  std::string json_string = R"EOF(
  {
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  GrpcWebFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, HealthCheckFilter) {
  std::string json_string = R"EOF(
  {
    "pass_through_mode" : true,
    "endpoint" : "/hc"
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  HealthCheckFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, BadHealthCheckFilterConfig) {
  std::string json_string = R"EOF(
  {
    "pass_through_mode" : true,
    "endpoint" : "/hc",
    "status" : 500
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  HealthCheckFilterConfig factory;
  EXPECT_THROW(factory.createFilterFactory(*json_config, "stats", context), Json::Exception);
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

TEST(HttpFilterConfigTest, SquashFilterCorrectJson) {
  std::string json_string = R"EOF(
    {
      "cluster" : "fake_cluster",
      "attachment_template" : {"a":"b"},
      "request_timeout_ms" : 1001,
      "attachment_poll_period_ms" : 2002,
      "attachment_timeout_ms" : 3003
    }
    )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  SquashFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy

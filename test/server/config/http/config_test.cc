#include <string>

#include "envoy/config/filter/http/router/v2/router.pb.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/config/well_known_names.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/router/router.h"

#include "server/config/http/buffer.h"
#include "server/config/http/dynamo.h"
#include "server/config/http/ext_authz.h"
#include "server/config/http/fault.h"
#include "server/config/http/grpc_http1_bridge.h"
#include "server/config/http/grpc_json_transcoder.h"
#include "server/config/http/grpc_web.h"
#include "server/config/http/ip_tagging.h"
#include "server/config/http/lua.h"
#include "server/config/http/ratelimit.h"
#include "server/config/http/router.h"
#include "server/config/http/squash.h"
#include "server/config/http/zipkin_http_tracer.h"
#include "server/config/network/http_connection_manager.h"
#include "server/http/health_check.h"

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
TEST(HttpFilterConfigTest, ValidateFail) {
  NiceMock<MockFactoryContext> context;

  BufferFilterConfig buffer_factory;
  envoy::config::filter::http::buffer::v2::Buffer buffer_proto;
  FaultFilterConfig fault_factory;
  envoy::config::filter::http::fault::v2::HTTPFault fault_proto;
  fault_proto.mutable_abort();
  GrpcJsonTranscoderFilterConfig grpc_json_transcoder_factory;
  envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder grpc_json_transcoder_proto;
  LuaFilterConfig lua_factory;
  envoy::config::filter::http::lua::v2::Lua lua_proto;
  RateLimitFilterConfig rate_limit_factory;
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

TEST(HttpFilterConfigTest, RateLimitFilterCorrectJson) {
  std::string json_string = R"EOF(
  {
    "domain" : "test",
    "timeout_ms" : 1337
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  RateLimitFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, RateLimitFilterCorrectProto) {
  std::string json_string = R"EOF(
  {
    "domain" : "test",
    "timeout_ms" : 1337
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  envoy::config::filter::http::rate_limit::v2::RateLimit proto_config{};
  Envoy::Config::FilterJson::translateHttpRateLimitFilter(*json_config, proto_config);

  NiceMock<MockFactoryContext> context;
  RateLimitFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, RateLimitFilterEmptyProto) {
  std::string json_string = R"EOF(
  {
    "domain" : "test",
    "timeout_ms" : 1337
  }
  )EOF";

  NiceMock<MockFactoryContext> context;
  RateLimitFilterConfig factory;

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  envoy::config::filter::http::rate_limit::v2::RateLimit proto_config =
      *dynamic_cast<envoy::config::filter::http::rate_limit::v2::RateLimit*>(
          factory.createEmptyConfigProto().get());
  Envoy::Config::FilterJson::translateHttpRateLimitFilter(*json_config, proto_config);

  HttpFilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, BadRateLimitFilterConfig) {
  std::string json_string = R"EOF(
  {
    "domain" : "test",
    "timeout_ms" : 0
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  RateLimitFilterConfig factory;
  EXPECT_THROW(factory.createFilterFactory(*json_config, "stats", context), Json::Exception);
}

TEST(HttpFilterConfigTest, DynamoFilter) {
  std::string json_string = R"EOF(
  {
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  DynamoFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
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

TEST(HttpFilterConfigTest, LuaFilterInJson) {
  std::string json_string = R"EOF(
  {
    "inline_code" : "print(5)"
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  LuaFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
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

TEST(HttpTracerConfigTest, ZipkinHttpTracer) {
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("fake_cluster")).WillRepeatedly(Return(&cm.thread_local_cluster_));

  std::string valid_config = R"EOF(
  {
    "collector_cluster": "fake_cluster",
    "collector_endpoint": "/api/v1/spans"
  }
  )EOF";
  Json::ObjectSharedPtr valid_json = Json::Factory::loadFromString(valid_config);
  NiceMock<MockInstance> server;
  ZipkinHttpTracerFactory factory;
  Tracing::HttpTracerPtr zipkin_tracer = factory.createHttpTracer(*valid_json, server, cm);
  EXPECT_NE(nullptr, zipkin_tracer);
}

TEST(HttpTracerConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(
      (Registry::RegisterFactory<ZipkinHttpTracerFactory, HttpTracerFactory>()), EnvoyException,
      "Double registration for name: 'envoy.zipkin'");
}

TEST(HttpExtAuthzConfigTest, ExtAuthzCorrectProto) {
  std::string yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_authz_server
      stat_prefix: google
  failure_mode_allow: false
)EOF";

  envoy::config::filter::http::ext_authz::v2::ExtAuthz proto_config{};
  MessageUtil::loadFromYaml(yaml, proto_config);

  NiceMock<MockFactoryContext> context;
  ExtAuthzFilterConfig factory;

  EXPECT_CALL(context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _))
      .WillOnce(Invoke([](const envoy::api::v2::core::GrpcService&, Stats::Scope&) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));
  HttpFilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpExtAuthzConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(
      (Registry::RegisterFactory<ExtAuthzFilterConfig, NamedHttpFilterConfigFactory>()),
      EnvoyException, "Double registration for name: 'envoy.ext_authz'");
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy

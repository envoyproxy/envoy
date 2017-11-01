#include <string>

#include "envoy/registry/registry.h"

#include "common/config/well_known_names.h"
#include "common/http/access_log/access_log_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "server/config/http/buffer.h"
#include "server/config/http/dynamo.h"
#include "server/config/http/fault.h"
#include "server/config/http/file_access_log.h"
#include "server/config/http/grpc_http1_bridge.h"
#include "server/config/http/grpc_web.h"
#include "server/config/http/ip_tagging.h"
#include "server/config/http/ratelimit.h"
#include "server/config/http/router.h"
#include "server/config/http/zipkin_http_tracer.h"
#include "server/config/network/http_connection_manager.h"
#include "server/http/health_check.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Server {
namespace Configuration {

TEST(HttpFilterConfigTest, BufferFilter) {
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

TEST(HttpFilterConfigTest, BadBufferFilterConfig) {
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

TEST(HttpFilterConfigTest, RateLimitFilter) {
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

TEST(HttpFilterConfigTest, CorrectFaultFilterInJson) {
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

TEST(HttpFilterConfigTest, CorrectFaultFilterInProto) {
  envoy::api::v2::filter::http::HTTPFault config{};
  config.mutable_delay()->set_percent(100);
  config.mutable_delay()->mutable_fixed_delay()->set_seconds(5);

  NiceMock<MockFactoryContext> context;
  FaultFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, EmptyFaultFilterInProto) {
  envoy::api::v2::filter::http::HTTPFault config{};
  NiceMock<MockFactoryContext> context;
  FaultFilterConfig factory;
  EXPECT_THROW(factory.createFilterFactoryFromProto(config, "stats", context), EnvoyException);
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

TEST(HttpFilterConfigTest, RouterFilter) {
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

TEST(HttpFilterConfigTest, IpTaggingFilter) {
  std::string json_string = R"EOF(
  {
    "request_type" : "internal",
    "ip_tags" : [
      { "ip_tag_name" : "example_tag",
        "ip_list" : ["0.0.0.0"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  IpTaggingFilterConfig factory;
  HttpFilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, BadIpTaggingFilterConfig) {
  std::string json_string = R"EOF(
  {
    "request_type" : "internal",
    "ip_tags" : [
      { "ip_tag_name" : "example_tag"
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  IpTaggingFilterConfig factory;
  EXPECT_THROW(factory.createFilterFactory(*json_config, "stats", context), Json::Exception);
}

TEST(HttpFilterConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(
      (Registry::RegisterFactory<RouterFilterConfig, NamedHttpFilterConfigFactory>()),
      EnvoyException,
      fmt::format("Double registration for name: '{}'", Config::HttpFilterNames::get().ROUTER));
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

TEST(AccessLogConfigTest, FileAccessLogTest) {
  auto factory = Registry::FactoryRegistry<AccessLogInstanceFactory>::getFactory(
      Config::AccessLogNames::get().FILE);
  ASSERT_NE(nullptr, factory);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  ASSERT_NE(nullptr, message);

  envoy::api::v2::filter::FileAccessLog file_access_log;
  file_access_log.set_path("/dev/null");
  file_access_log.set_format("%START_TIME%");
  MessageUtil::jsonConvert(file_access_log, *message);

  Http::AccessLog::FilterPtr filter;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  Http::AccessLog::InstanceSharedPtr instance =
      factory->createAccessLogInstance(*message, std::move(filter), context);
  EXPECT_NE(nullptr, instance);
  EXPECT_NE(nullptr, dynamic_cast<Http::AccessLog::FileAccessLog*>(instance.get()));
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy

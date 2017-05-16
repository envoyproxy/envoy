#include <string>

#include "server/config/http/buffer.h"
#include "server/config/http/dynamo.h"
#include "server/config/http/fault.h"
#include "server/config/http/grpc_http1_bridge.h"
#include "server/config/http/ratelimit.h"
#include "server/config/http/router.h"
#include "server/http/health_check.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::_;
using testing::NiceMock;

namespace Server {
namespace Configuration {

TEST(HttpFilterConfigTest, BufferFilter) {
  std::string json_string = R"EOF(
  {
    "max_request_bytes" : 1028,
    "max_request_time_s" : 2
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  BufferFilterConfig factory;
  HttpFilterFactoryCb cb = factory.tryCreateFilterFactory(HttpFilterType::Decoder, "buffer",
                                                          *json_config, "stats", server);
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

  Json::ObjectPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  BufferFilterConfig factory;
  EXPECT_THROW(factory.tryCreateFilterFactory(HttpFilterType::Decoder, "buffer", *json_config,
                                              "stats", server),
               Json::Exception);
}

TEST(HttpFilterConfigTest, DynamoFilter) {
  std::string json_string = R"EOF(
  {
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  DynamoFilterConfig factory;
  HttpFilterFactoryCb cb = factory.tryCreateFilterFactory(
      HttpFilterType::Both, "http_dynamo_filter", *json_config, "stats", server);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, FaultFilter) {
  std::string json_string = R"EOF(
  {
    "delay" : {
      "type" : "fixed",
      "fixed_delay_percent" : 100,
      "fixed_duration_ms" : 5000
    }
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  FaultFilterConfig factory;
  HttpFilterFactoryCb cb = factory.tryCreateFilterFactory(HttpFilterType::Decoder, "fault",
                                                          *json_config, "stats", server);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpFilterConfigTest, GrpcHttp1BridgeFilter) {
  std::string json_string = R"EOF(
  {
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  GrpcHttp1BridgeFilterConfig factory;
  HttpFilterFactoryCb cb = factory.tryCreateFilterFactory(HttpFilterType::Both, "grpc_http1_bridge",
                                                          *json_config, "stats", server);
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

  Json::ObjectPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  HealthCheckFilterConfig factory;
  HttpFilterFactoryCb cb = factory.tryCreateFilterFactory(HttpFilterType::Both, "health_check",
                                                          *json_config, "stats", server);
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

  Json::ObjectPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  HealthCheckFilterConfig factory;
  EXPECT_THROW(factory.tryCreateFilterFactory(HttpFilterType::Both, "health_check", *json_config,
                                              "stats", server),
               Json::Exception);
}

TEST(HttpFilterConfigTest, RouterFilter) {
  std::string json_string = R"EOF(
  {
    "dynamic_stats" : true
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  RouterFilterConfig factory;
  HttpFilterFactoryCb cb = factory.tryCreateFilterFactory(HttpFilterType::Decoder, "router",
                                                          *json_config, "stats", server);
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

  Json::ObjectPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  RouterFilterConfig factory;
  EXPECT_THROW(factory.tryCreateFilterFactory(HttpFilterType::Decoder, "router", *json_config,
                                              "stats", server),
               Json::Exception);
}

} // Configuration
} // Server
} // Envoy

#include <string>

#include "server/config/http/buffer.h"
#include "server/config/http/dynamo.h"
#include "server/config/http/fault.h"
#include "server/config/http/grpc_http1_bridge.h"
#include "server/config/http/lightstep_http_tracer.h"
#include "server/config/http/ratelimit.h"
#include "server/config/http/router.h"
#include "server/config/http/zipkin_http_tracer.h"
#include "server/http/health_check.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::_;
using testing::NiceMock;
using testing::Return;

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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  RouterFilterConfig factory;
  EXPECT_THROW(factory.tryCreateFilterFactory(HttpFilterType::Decoder, "router", *json_config,
                                              "stats", server),
               Json::Exception);
}

TEST(HttpTracerConfigTest, LightstepHttpTracer) {
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("fake_cluster")).WillRepeatedly(Return(&cm.thread_local_cluster_));
  ON_CALL(*cm.thread_local_cluster_.cluster_.info_, features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

  std::string valid_config = R"EOF(
  {
    "collector_cluster": "fake_cluster",
    "access_token_file": "fake_file"
  }
  )EOF";
  Json::ObjectSharedPtr valid_json = Json::Factory::loadFromString(valid_config);
  NiceMock<MockInstance> server;
  LightstepHttpTracerFactory factory;
  Tracing::HttpTracerPtr lightstep_tracer =
      factory.tryCreateHttpTracer("lightstep", *valid_json, server, cm);
  EXPECT_NE(nullptr, lightstep_tracer);
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
  Tracing::HttpTracerPtr zipkin_tracer =
      factory.tryCreateHttpTracer("zipkin", *valid_json, server, cm);
  EXPECT_NE(nullptr, zipkin_tracer);
}

TEST(HttpTracerConfigTest, InvalidHttpTracer) {
  NiceMock<Upstream::MockClusterManager> cm;

  std::string valid_config = R"EOF(
      {"collector_cluster": "fake_cluster"}
    )EOF";
  Json::ObjectSharedPtr valid_json = Json::Factory::loadFromString(valid_config);
  NiceMock<MockInstance> server;
  LightstepHttpTracerFactory lightstep_factory;
  ZipkinHttpTracerFactory zipkin_factory;
  std::string tracer_type = "invalid";
  Tracing::HttpTracerPtr tracer =
      lightstep_factory.tryCreateHttpTracer(tracer_type, *valid_json, server, cm);
  EXPECT_EQ(nullptr, tracer);
  tracer = zipkin_factory.tryCreateHttpTracer(tracer_type, *valid_json, server, cm);
  EXPECT_EQ(nullptr, tracer);
}

} // Configuration
} // Server
} // Envoy

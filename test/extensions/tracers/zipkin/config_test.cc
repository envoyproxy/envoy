#include "envoy/registry/registry.h"

#include "extensions/tracers/zipkin/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

TEST(ZipkinTracerConfigTest, ZipkinHttpTracer) {
  NiceMock<Server::MockInstance> server;
  EXPECT_CALL(server.cluster_manager_, get("fake_cluster"))
      .WillRepeatedly(Return(&server.cluster_manager_.thread_local_cluster_));

  std::string valid_config = R"EOF(
  {
    "collector_cluster": "fake_cluster",
    "collector_endpoint": "/api/v1/spans"
  }
  )EOF";
  Json::ObjectSharedPtr valid_json = Json::Factory::loadFromString(valid_config);
  ZipkinHttpTracerFactory factory;
  Tracing::HttpTracerPtr zipkin_tracer = factory.createHttpTracer(*valid_json, server);
  EXPECT_NE(nullptr, zipkin_tracer);
}

TEST(ZipkinTracerConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE((Registry::RegisterFactory<ZipkinHttpTracerFactory,
                                                       Server::Configuration::HttpTracerFactory>()),
                            EnvoyException, "Double registration for name: 'envoy.zipkin'");
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

#include <string>

#include "server/config/http/dynamic_opentracing_http_tracer.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "fmt/printf.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Server {
namespace Configuration {

TEST(HttpTracerConfigTest, DynamicOpentracingHttpTracer) {
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("fake_cluster")).WillRepeatedly(Return(&cm.thread_local_cluster_));
  ON_CALL(*cm.thread_local_cluster_.cluster_.info_, features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

  const std::string valid_config = fmt::sprintf(R"EOF(
  {
    "library": "%s/external/io_opentracing_cpp/mocktracer/libmocktracer_plugin.so",
    "config": {
      "output_file" : "fake_file"
    }
  }
  )EOF",
                                                TestEnvironment::runfilesDirectory());
  const Json::ObjectSharedPtr valid_json = Json::Factory::loadFromString(valid_config);
  NiceMock<MockInstance> server;
  DynamicOpenTracingHttpTracerFactory factory;

  const Tracing::HttpTracerPtr tracer = factory.createHttpTracer(*valid_json, server, cm);
  EXPECT_NE(nullptr, tracer);
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy

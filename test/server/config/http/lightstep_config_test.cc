#include <string>

#include "server/config/http/lightstep_http_tracer.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Server {
namespace Configuration {

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
  Tracing::HttpTracerPtr lightstep_tracer = factory.createHttpTracer(*valid_json, server, cm);
  EXPECT_NE(nullptr, lightstep_tracer);
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy

#include "extensions/tracers/lightstep/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Lightstep {

TEST(LightstepTracerConfigTest, LightstepHttpTracer) {
  NiceMock<Server::MockInstance> server;
  EXPECT_CALL(server.cluster_manager_, get("fake_cluster"))
      .WillRepeatedly(Return(&server.cluster_manager_.thread_local_cluster_));
  ON_CALL(*server.cluster_manager_.thread_local_cluster_.cluster_.info_, features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

  std::string valid_config = R"EOF(
  {
    "collector_cluster": "fake_cluster",
    "access_token_file": "fake_file"
  }
  )EOF";
  Json::ObjectSharedPtr valid_json = Json::Factory::loadFromString(valid_config);

  LightstepHttpTracerFactory factory;
  Tracing::HttpTracerPtr lightstep_tracer = factory.createHttpTracer(*valid_json, server);
  EXPECT_NE(nullptr, lightstep_tracer);
}

} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

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

/**
 * With `cc_test`, if `data` points to a library, then bazel puts in in a platform-architecture
 * dependent location, so this function uses `find` to discover the path.
 *
 * See https://stackoverflow.com/q/48461242/4447365.
 */
const char* mocktracer_library_path() {
  static const std::string path = [] {
    const std::string result =
        TestEnvironment::runfilesDirectory() + "/libopentracing_mocktracer.so";
    TestEnvironment::exec({"find", TestEnvironment::runfilesDirectory(),
                           "-name *libmock* -exec ln -s {}", result, "\\;"});
    return result;
  }();
  return path.c_str();
}

TEST(HttpTracerConfigTest, DynamicOpentracingHttpTracer) {
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("fake_cluster")).WillRepeatedly(Return(&cm.thread_local_cluster_));
  ON_CALL(*cm.thread_local_cluster_.cluster_.info_, features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

  std::string valid_config = fmt::sprintf(R"EOF(
  {
    "library": "%s",
    "config": {
      "output_file" : "fake_file"
    }
  }
  )EOF",
                                          mocktracer_library_path());
  Json::ObjectSharedPtr valid_json = Json::Factory::loadFromString(valid_config);
  NiceMock<MockInstance> server;
  DynamicOpenTracingHttpTracerFactory factory;

  Tracing::HttpTracerPtr tracer = factory.createHttpTracer(*valid_json, server, cm);
  EXPECT_NE(nullptr, tracer);
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy

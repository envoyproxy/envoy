#include "extensions/tracers/skywalking/skywalking_tracer_impl.h"

#include "test/extensions/tracers/skywalking/skywalking_test_helper.h"
#include "test/mocks/common.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class SkyWalkingDriverTest : public testing::Test {
public:
  void setupSkyWalkingDriver(const std::string& yaml_string) {
    TestUtility::loadFromYaml(yaml_string, config_);
    driver_ = std::make_unique<Driver>(config_, context_);
  }

protected:
  NiceMock<Envoy::Server::Configuration::MockTracerFactoryContext> context_;
  envoy::config::trace::v3::SkyWalkingConfig config_;
  DriverPtr driver_;
};

static const std::string SKYWALKING_CONFIG_WITH_CLIENT_CONFIG = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: fake_cluster
  client_config:
    authentication: "A fake auth string for SkyWalking test"
    service_name: "Test Service"
    instance_name: "Test Instance"
    pass_endpoint: true
    max_cache_size: 2333
)EOF";

TEST_F(SkyWalkingDriverTest, SkyWalkingDriverInitTest) {
  setupSkyWalkingDriver(SKYWALKING_CONFIG_WITH_CLIENT_CONFIG);
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

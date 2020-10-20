#include "extensions/tracers/skywalking/skywalking_client_config.h"

#include "test/mocks/common.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class SkyWalkingClientConfigTest : public testing::Test {
public:
  void setupSkyWalkingClientConfig(const std::string& yaml_string) {
    auto& local_info = context_.server_factory_context_.local_info_;

    ON_CALL(local_info, clusterName()).WillByDefault(ReturnRef(test_string));
    ON_CALL(local_info, nodeName()).WillByDefault(ReturnRef(test_string));

    envoy::config::trace::v3::SkyWalkingConfig proto_config;
    TestUtility::loadFromYaml(yaml_string, proto_config);

    client_config_ =
        std::make_unique<SkyWalkingClientConfig>(context_, proto_config.client_config());
  }

protected:
  NiceMock<Envoy::Server::Configuration::MockTracerFactoryContext> context_;

  std::string test_string = "ABCDEFGHIJKLMN";

  SkyWalkingClientConfigPtr client_config_;
};

static const std::string SKYWALKING_CONFIG_WITH_CLIENT_CONFIG = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: fake_cluster
  client_config:
    authentication: "FAKE_FAKE_FAKE_FAKE_FAKE_FAKE"
    service_name: "FAKE_FAKE_FAKE"
    instance_name: "FAKE_FAKE_FAKE"
    max_cache_size: 2333
)EOF";

static const std::string SKYWALKING_CONFIG_NO_CLIENT_CONFIG = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: fake_cluster
)EOF";

// Test whether the default value can be set correctly when there is no proto client config
// provided.
TEST_F(SkyWalkingClientConfigTest, NoProtoClientConfigTest) {
  setupSkyWalkingClientConfig(SKYWALKING_CONFIG_NO_CLIENT_CONFIG);

  EXPECT_EQ(client_config_->service(), test_string);
  EXPECT_EQ(client_config_->serviceInstance(), test_string);
  EXPECT_EQ(client_config_->maxCacheSize(), 1024);
  EXPECT_EQ(client_config_->authentication(), "");
}

// Test whether the client config can work correctly when the proto client config is provided.
TEST_F(SkyWalkingClientConfigTest, WithProtoClientConfigTest) {
  setupSkyWalkingClientConfig(SKYWALKING_CONFIG_WITH_CLIENT_CONFIG);

  EXPECT_EQ(client_config_->service(), "FAKE_FAKE_FAKE");
  EXPECT_EQ(client_config_->serviceInstance(), "FAKE_FAKE_FAKE");
  EXPECT_EQ(client_config_->maxCacheSize(), 2333);
  EXPECT_EQ(client_config_->authentication(), "FAKE_FAKE_FAKE_FAKE_FAKE_FAKE");
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

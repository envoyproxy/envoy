#include "source/common/tcp_proxy/tcp_proxy.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "contrib/istio/filters/network/hbone/source/config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Istio {
namespace Hbone {
namespace {

class HboneConfigTest : public testing::Test {
protected:
  HboneConfigTest() {
    TestUtility::loadFromYaml(R"EOF(
tcp_proxy:
  stat_prefix: hbone
  cluster: hbone
  tunneling_config:
    hostname: "app:8080"
)EOF",
                              config_);
  }
  FilterConfig config_;
  FilterConfigFactory factory_;
  testing::NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(HboneConfigTest, RequiresConnect) {
  config_.mutable_tcp_proxy()->clear_tunneling_config();
  EXPECT_FALSE(factory_.createFilterFactoryFromProto(config_, context_).ok());
}

TEST_F(HboneConfigTest, RejectsPostTunneling) {
  config_.mutable_tcp_proxy()->mutable_tunneling_config()->set_use_post(true);
  EXPECT_FALSE(factory_.createFilterFactoryFromProto(config_, context_).ok());
}

TEST_F(HboneConfigTest, RejectsWaitingForApplicationData) {
  config_.mutable_tcp_proxy()->set_upstream_connect_mode(
      envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
  EXPECT_FALSE(factory_.createFilterFactoryFromProto(config_, context_).ok());
}

TEST_F(HboneConfigTest, AcceptsConnectConfiguration) {
  EXPECT_TRUE(factory_.createFilterFactoryFromProto(config_, context_).ok());
}

TEST_F(HboneConfigTest, CreatesStockTcpProxy) {
  auto callback = factory_.createFilterFactoryFromProto(config_, context_);
  ASSERT_TRUE(callback.ok());
  testing::StrictMock<Network::MockFilterManager> manager;
  EXPECT_CALL(manager, addReadFilter(testing::_)).WillOnce([](Network::ReadFilterSharedPtr filter) {
    const auto* raw_filter = filter.get();
    EXPECT_EQ(typeid(TcpProxy::Filter), typeid(*raw_filter));
  });
  callback.value()(manager);
}

} // namespace
} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy

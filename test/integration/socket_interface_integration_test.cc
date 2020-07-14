#include "common/network/socket_interface.h"

#include "test/integration/integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class SocketInterfaceIntegrationTest : public BaseIntegrationTest,
                                       public testing::TestWithParam<Network::Address::IpVersion> {
public:
  SocketInterfaceIntegrationTest() : BaseIntegrationTest(GetParam(), config()) {
    use_lds_ = false;
  };

  static std::string config() {
    // At least one empty filter chain needs to be specified.
    return absl::StrCat(ConfigHelper::httpProxyConfig(), R"EOF(
bootstrap_extensions:
  - name: envoy.extensions.network.socket_interface.default_socket_interface
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network.socket_interface.v3.DefaultSocketInterface
default_socket_interface: "envoy.extensions.network.socket_interface.default_socket_interface"
    )EOF");
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SocketInterfaceIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SocketInterfaceIntegrationTest, Basic) {
  BaseIntegrationTest::initialize();
  const Network::SocketInterface* factory = Network::socketInterface(
      "envoy.extensions.network.socket_interface.default_socket_interface");
  ASSERT_TRUE(Network::SocketInterfaceSingleton::getExisting() == factory);
}

} // namespace
} // namespace Envoy
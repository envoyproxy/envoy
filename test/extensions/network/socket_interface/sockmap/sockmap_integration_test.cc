#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/socket_interface.h"

#include "test/integration/integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Boots Envoy with the sockmap socket interface as the default and accelerated_ports set, so the
// scoped configuration is validated end to end. The eBPF datapath cannot be loaded in the test
// environment, so every socket falls back to the standard datapath and proxying is unchanged.
class SockmapSocketInterfaceIntegrationTest
    : public BaseIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  SockmapSocketInterfaceIntegrationTest() : BaseIntegrationTest(GetParam(), config()) {
    use_lds_ = false;
  }

  static std::string config() {
    return absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
        name: envoy.filters.network.echo
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo
bootstrap_extensions:
  - name: envoy.extensions.network.socket_interface.sockmap
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network.socket_interface.sockmap.v3.Sockmap
      accelerated_ports:
      - start: 9211
        end: 9212
default_socket_interface: "envoy.extensions.network.socket_interface.sockmap"
    )EOF");
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SockmapSocketInterfaceIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SockmapSocketInterfaceIntegrationTest, FallsBackToStandardDatapath) {
  BaseIntegrationTest::initialize();

  const Network::SocketInterface* factory =
      Network::socketInterface("envoy.extensions.network.socket_interface.sockmap");
  ASSERT_TRUE(Network::SocketInterfaceSingleton::getExisting() == factory);

  std::string response;
  auto connection = createConnectionDriver(
      lookupPort("listener_0"), "hello",
      [&response](Network::ClientConnection& conn, const Buffer::Instance& data) -> void {
        response.append(data.toString());
        conn.close(Network::ConnectionCloseType::FlushWrite);
      });
  ASSERT_TRUE(connection->run());
  EXPECT_EQ("hello", response);
}

} // namespace
} // namespace Envoy

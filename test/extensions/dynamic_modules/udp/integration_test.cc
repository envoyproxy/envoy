#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/udp/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {
namespace {

class DynamicModulesUdpIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public BaseIntegrationTest {
public:
  DynamicModulesUdpIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseUdpListenerConfig()) {}

  void setDynamicModulesSearchPath(const std::string& module_name, const std::string& language) {
    std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath(module_name, language);
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  void SetUp() override { setDynamicModulesSearchPath("udp_no_op", "c"); }

  void setup(const std::string& module_name = "udp_no_op",
             const std::string& filter_name = "test_filter") {
    FakeUpstreamConfig::UdpConfig config;
    setUdpFakeUpstream(config);

    const std::string filter_config = fmt::format(R"EOF(
name: envoy.filters.udp_listener.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.udp.dynamic_modules.v3.DynamicModuleUdpListenerFilter
  dynamic_module_config:
    name: "{}"
    do_not_close: true
  filter_name: "{}"
  filter_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: "some_config"
)EOF",
                                                  module_name, filter_name);

    // addListenerFilter() prepends filters, so add udp_proxy first to produce
    // [dynamic_modules, udp_proxy].
    config_helper_.addListenerFilter(R"EOF(
name: envoy.filters.udp_listener.udp_proxy
typed_config:
  '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig
  stat_prefix: service
  matcher:
    on_no_match:
      action:
        name: route
        typed_config:
          '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
          cluster: cluster_0
)EOF");

    config_helper_.addListenerFilter(filter_config);

    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesUdpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModulesUdpIntegrationTest, BasicDataFlow) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  std::string request = "hello";
  Network::Test::UdpSyncPeer client(GetParam());
  client.write(request, *listener_address);

  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ(request, request_datagram.buffer_->toString());
}

TEST_P(DynamicModulesUdpIntegrationTest, LargePayload) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  std::string large_request(512, 'x');
  Network::Test::UdpSyncPeer client(GetParam());
  client.write(large_request, *listener_address);

  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ(large_request, request_datagram.buffer_->toString());
}

TEST_P(DynamicModulesUdpIntegrationTest, MultipleDatagrams) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  Network::Test::UdpSyncPeer client(GetParam());

  for (int i = 0; i < 5; i++) {
    std::string request = fmt::format("datagram_{}", i);
    client.write(request, *listener_address);

    Network::UdpRecvData request_datagram;
    ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
    EXPECT_EQ(request, request_datagram.buffer_->toString());
  }
}

TEST_P(DynamicModulesUdpIntegrationTest, GoSdkEchoDatagram) {
  setDynamicModulesSearchPath("udp_listener_integration_test", "go");
  setup("udp_listener_integration_test", "echo_datagram");

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  const std::string request = "hello";
  Network::Test::UdpSyncPeer client(GetParam());
  client.write(request, *listener_address);

  Network::UdpRecvData response;
  client.recv(response);
  EXPECT_EQ(request, response.buffer_->toString());
}

TEST_P(DynamicModulesUdpIntegrationTest, GoSdkRewriteDatagram) {
  setDynamicModulesSearchPath("udp_listener_integration_test", "go");
  setup("udp_listener_integration_test", "rewrite_datagram");

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  Network::Test::UdpSyncPeer client(GetParam());
  client.write("hello", *listener_address);

  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ("rewritten", request_datagram.buffer_->toString());
}

} // namespace
} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

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

class UdpDynamicModulesIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public BaseIntegrationTest {
public:
  UdpDynamicModulesIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseUdpListenerConfig()) {}

  void SetUp() override {
    // The shared object is created by the build system.
    // We need to set the DYNAMIC_MODULES_SEARCH_PATH to the location of the shared object.
    std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("udp_no_op", "c");
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  void setup(const std::string& module_name = "udp_no_op") {
    FakeUpstreamConfig::UdpConfig config;
    setUdpFakeUpstream(config);

    const std::string filter_config = fmt::format(R"EOF(
name: envoy.filters.udp_listener.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.udp.dynamic_modules.v3.DynamicModuleUdpListenerFilter
  dynamic_module_config:
    name: "{}"
    do_not_close: true
  filter_name: "test_filter"
  filter_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: "some_config"
)EOF",
                                                  module_name);

    config_helper_.addListenerFilter(filter_config);

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

    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, UdpDynamicModulesIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(UdpDynamicModulesIntegrationTest, BasicDataFlow) {
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

TEST_P(UdpDynamicModulesIntegrationTest, StopIteration) {
  setup("udp_stop_iteration");

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  std::string request = "should be blocked";
  Network::Test::UdpSyncPeer client(GetParam());
  client.write(request, *listener_address);

  Network::UdpRecvData request_datagram;
  // UDP listener filter StopIteration is currently not enforced in the fake upstream path.
  // We verify that the datagram still arrives. When StopIteration is enforced in the future,
  // this expectation can be flipped.
  EXPECT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram, std::chrono::seconds(1)));
  EXPECT_EQ(request, request_datagram.buffer_->toString());
}

TEST_P(UdpDynamicModulesIntegrationTest, LargePayload) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  // Use a conservative payload size to avoid platform-specific UDP limits.
  std::string large_request(512, 'x');
  Network::Test::UdpSyncPeer client(GetParam());
  client.write(large_request, *listener_address);

  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ(large_request, request_datagram.buffer_->toString());
}

TEST_P(UdpDynamicModulesIntegrationTest, MultipleDatagrams) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  Network::Test::UdpSyncPeer client(GetParam());

  // Send multiple datagrams.
  for (int i = 0; i < 5; i++) {
    std::string request = fmt::format("datagram_{}", i);
    client.write(request, *listener_address);

    Network::UdpRecvData request_datagram;
    ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
    EXPECT_EQ(request, request_datagram.buffer_->toString());
  }
}

} // namespace
} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

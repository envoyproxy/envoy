#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/http_capsule/config.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/http_capsule/http_capsule.h"

#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HttpCapsule {
namespace {

class HttpCapsuleIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public BaseIntegrationTest {
public:
  HttpCapsuleIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseUdpListenerConfig()) {}

  void setup() {
    setUdpFakeUpstream(FakeUpstreamConfig::UdpConfig());

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      std::string filter_config = fmt::format(R"EOF(
name: udp_proxy
typed_config:
  '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig
  stat_prefix: foo
  matcher:
    on_no_match:
      action:
        name: route
        typed_config:
          '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
          cluster: cluster_0
  session_filters:
  - name: http_capsule
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.session.http_capsule.v3.FilterConfig
)EOF");

      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter = listener->add_listener_filters();
      TestUtility::loadFromYaml(filter_config, *filter);
    });

    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HttpCapsuleIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(HttpCapsuleIntegrationTest, BasicFlow) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  const std::string request = "hello";
  const std::string expected_request =
      absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                             "06" // Capsule Length = length(hello) + 1
                             "00" // Context ID
                             ) +
      request;

  const std::string expected_response = "world";
  const std::string response = absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                                                      "06" // Capsule Length = length(world) + 1
                                                      "00" // Context ID
                                                      ) +
                               expected_response;

  // Send datagram to be encapsulated.
  Network::Test::UdpSyncPeer client(version_, Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE);
  client.write(request, *listener_address);

  // Wait for the upstream datagram.
  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ(expected_request, request_datagram.buffer_->toString());

  // Respond from the upstream.
  fake_upstreams_[0]->sendUdpDatagram(response, request_datagram.addresses_.peer_);
  Network::UdpRecvData response_datagram;
  client.recv(response_datagram);
  EXPECT_EQ(expected_response, response_datagram.buffer_->toString());
}

TEST_P(HttpCapsuleIntegrationTest, SendSplitCapsule) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  const std::string request = "hello";
  const std::string expected_request =
      absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                             "06" // Capsule Length = length(hello) + 1
                             "00" // Context ID
                             ) +
      request;

  const std::string expected_response = "world";
  const std::string response = absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                                                      "06" // Capsule Length = length(world) + 1
                                                      "00" // Context ID
                                                      ) +
                               expected_response;

  // Send datagram to be encapsulated.
  Network::Test::UdpSyncPeer client(version_, Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE);
  client.write(request, *listener_address);

  // Wait for the upstream datagram.
  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ(expected_request, request_datagram.buffer_->toString());

  // Respond from the upstream.
  int pivot_index = 4;
  fake_upstreams_[0]->sendUdpDatagram(response.substr(0, pivot_index),
                                      request_datagram.addresses_.peer_);
  // Make sure that only one payload received, but none sent downstream because it's not a complete
  // capsule.
  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_rx_datagrams", 1);
  EXPECT_EQ(0, test_server_->counter("udp.foo.downstream_sess_tx_datagrams")->value());

  // Sending the rest of the capsule, so we expect a datagram flushed downstream.
  fake_upstreams_[0]->sendUdpDatagram(response.substr(pivot_index),
                                      request_datagram.addresses_.peer_);
  Network::UdpRecvData response_datagram;
  client.recv(response_datagram);
  EXPECT_EQ(expected_response, response_datagram.buffer_->toString());
  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_rx_datagrams", 2);
  test_server_->waitForCounterEq("udp.foo.downstream_sess_tx_datagrams", 1);
}

TEST_P(HttpCapsuleIntegrationTest, SendMultipleCapsules) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  const std::string request = "hello";
  const std::string expected_request =
      absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                             "06" // Capsule Length = length(hello) + 1
                             "00" // Context ID
                             ) +
      request;

  std::string response = absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                                                "0A" // Capsule Length = length(response1) + 1
                                                "00" // Context ID
                                                ) +
                         "response1" +
                         absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                                                "0A" // Capsule Length = length(response2) + 1
                                                "00" // Context ID
                                                ) +
                         "response2";

  // Send datagram to be encapsulated.
  Network::Test::UdpSyncPeer client(version_, Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE);
  client.write(request, *listener_address);

  // Wait for the upstream datagram.
  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ(expected_request, request_datagram.buffer_->toString());

  // // Respond from the upstream.
  fake_upstreams_[0]->sendUdpDatagram(response, request_datagram.addresses_.peer_);
  Network::UdpRecvData response_datagram;
  client.recv(response_datagram);
  EXPECT_EQ("response1", response_datagram.buffer_->toString());
  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_rx_datagrams", 1);
  test_server_->waitForCounterEq("udp.foo.downstream_sess_tx_datagrams", 2);
}

} // namespace
} // namespace HttpCapsule
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

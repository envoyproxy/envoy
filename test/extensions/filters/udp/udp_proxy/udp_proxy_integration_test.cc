#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"

namespace Envoy {
namespace {
class UdpReverseFilter : public Network::UdpListenerReadFilter {
public:
  UdpReverseFilter(Network::UdpReadFilterCallbacks& callbacks) : UdpListenerReadFilter(callbacks) {}

  // Network::UdpListenerReadFilter
  Network::FilterStatus onData(Network::UdpRecvData& data) override {
    std::string content = data.buffer_->toString();
    std::reverse(content.begin(), content.end());

    data.buffer_->drain(data.buffer_->length());
    data.buffer_->add(content);

    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode) override {
    return Network::FilterStatus::Continue;
  }
};

class UdpReverseFilterConfigFactory
    : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  // NamedUdpListenerFilterConfigFactory
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::ListenerFactoryContext&) override {
    return [](Network::UdpListenerFilterManager& filter_manager,
              Network::UdpReadFilterCallbacks& callbacks) -> void {
      filter_manager.addReadFilter(std::make_unique<UdpReverseFilter>(callbacks));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return "test.udp_listener.reverse"; }
};

class UdpProxyIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public BaseIntegrationTest {
public:
  UdpProxyIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseUdpListenerConfig()),
        registration_(factory_) {}

  void setup(uint32_t upstream_count,
             absl::optional<uint64_t> max_rx_datagram_size = absl::nullopt) {
    FakeUpstreamConfig::UdpConfig config;
    config.max_rx_datagram_size_ = max_rx_datagram_size;
    setUdpFakeUpstream(config);
    if (upstream_count > 1) {
      setDeterministicValue();
      setUpstreamCount(upstream_count);
      config_helper_.addConfigModifier(
          [upstream_count](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
            for (uint32_t i = 1; i < upstream_count; i++) {
              bootstrap.mutable_static_resources()
                  ->mutable_clusters(0)
                  ->mutable_load_assignment()
                  ->mutable_endpoints(0)
                  ->add_lb_endpoints()
                  ->mutable_endpoint()
                  ->MergeFrom(ConfigHelper::buildEndpoint(
                      Network::Test::getLoopbackAddressString(GetParam())));
            }
          });
    }

    if (max_rx_datagram_size.has_value()) {
      if (max_rx_datagram_size.has_value()) {
        config_helper_.addConfigModifier(
            [max_rx_datagram_size](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
              bootstrap.mutable_static_resources()
                  ->mutable_listeners(0)
                  ->mutable_udp_listener_config()
                  ->mutable_downstream_socket_config()
                  ->mutable_max_rx_datagram_size()
                  ->set_value(max_rx_datagram_size.value());
            });
      }

      config_helper_.addListenerFilter(fmt::format(R"EOF(
name: udp_proxy
typed_config:
  '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig
  stat_prefix: foo
  cluster: cluster_0
  upstream_socket_config:
    max_rx_datagram_size: {}
)EOF",
                                                   max_rx_datagram_size.value()));
    } else {
      config_helper_.addListenerFilter(R"EOF(
name: udp_proxy
typed_config:
  '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig
  stat_prefix: foo
  cluster: cluster_0
)EOF");
    }

    BaseIntegrationTest::initialize();
  }

  void setupMultiple() {
    FakeUpstreamConfig::UdpConfig config;
    config.max_rx_datagram_size_ = absl::nullopt;
    setUdpFakeUpstream(config);

    config_helper_.addListenerFilter(R"EOF(
name: udp_proxy
typed_config:
  '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig
  stat_prefix: foo
  cluster: cluster_0
)EOF");
    // Add a reverse filter for reversing payload prior to UDP proxy
    config_helper_.addListenerFilter(R"EOF(
name: test.udp_listener.reverse
)EOF");

    BaseIntegrationTest::initialize();
  }

  void requestResponseWithListenerAddress(
      const Network::Address::Instance& listener_address, std::string request = "hello",
      std::string expected_request = "hello", std::string response = "world1",
      std::string expected_response = "world1",
      uint64_t max_rx_datagram_size = Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE) {
    // Send datagram to be proxied.
    Network::Test::UdpSyncPeer client(version_, max_rx_datagram_size);
    client.write(request, listener_address);

    // Wait for the upstream datagram.
    Network::UdpRecvData request_datagram;
    ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
    EXPECT_EQ(expected_request, request_datagram.buffer_->toString());

    // Respond from the upstream.
    fake_upstreams_[0]->sendUdpDatagram(response, request_datagram.addresses_.peer_);
    Network::UdpRecvData response_datagram;
    client.recv(response_datagram);
    EXPECT_EQ(expected_response, response_datagram.buffer_->toString());
    EXPECT_EQ(listener_address.asString(), response_datagram.addresses_.peer_->asString());

    EXPECT_EQ(expected_request.size(),
              test_server_->counter("udp.foo.downstream_sess_rx_bytes")->value());
    EXPECT_EQ(1, test_server_->counter("udp.foo.downstream_sess_rx_datagrams")->value());
    EXPECT_EQ(expected_request.size(),
              test_server_->counter("cluster.cluster_0.upstream_cx_tx_bytes_total")->value());
    EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.udp.sess_tx_datagrams")->value());

    EXPECT_EQ(expected_response.size(),
              test_server_->counter("cluster.cluster_0.upstream_cx_rx_bytes_total")->value());
    EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.udp.sess_rx_datagrams")->value());
    // The stat is incremented after the send so there is a race condition and we must wait for
    // the counter to be incremented.
    test_server_->waitForCounterEq("udp.foo.downstream_sess_tx_bytes", expected_response.size());
    test_server_->waitForCounterEq("udp.foo.downstream_sess_tx_datagrams", 1);

    EXPECT_EQ(1, test_server_->counter("udp.foo.downstream_sess_total")->value());
    EXPECT_EQ(1, test_server_->gauge("udp.foo.downstream_sess_active")->value());
  }

  UdpReverseFilterConfigFactory factory_;
  Registry::InjectFactory<Server::Configuration::NamedUdpListenerFilterConfigFactory> registration_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, UdpProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Make sure that we gracefully fail if the user does not configure reuse_port and concurrency is
// > 1.
TEST_P(UdpProxyIntegrationTest, NoReusePort) {
  concurrency_ = 2;
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->mutable_enable_reuse_port()
        ->set_value(false);
  });
  // Do not wait for listeners to start as the listener will fail.
  defer_listener_finalization_ = true;
  setup(1);
  test_server_->waitForCounterGe("listener_manager.lds.update_rejected", 1);
}

// Basic loopback test.
TEST_P(UdpProxyIntegrationTest, HelloWorldOnLoopback) {
  setup(1);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
  requestResponseWithListenerAddress(*listener_address);
}

// Verify downstream drops are handled correctly with stats.
TEST_P(UdpProxyIntegrationTest, DownstreamDrop) {
  setup(1);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
  Network::Test::UdpSyncPeer client(version_);
  const uint64_t large_datagram_size =
      (Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE * Network::NUM_DATAGRAMS_PER_RECEIVE) + 1024;
  client.write(std::string(large_datagram_size, 'a'), *listener_address);
  if (GetParam() == Network::Address::IpVersion::v4) {
    test_server_->waitForCounterEq("listener.0.0.0.0_0.udp.downstream_rx_datagram_dropped", 1);
  } else {
    test_server_->waitForCounterEq("listener.[__]_0.udp.downstream_rx_datagram_dropped", 1);
  }
}

// Verify upstream drops are handled correctly with stats.
TEST_P(UdpProxyIntegrationTest, UpstreamDrop) {
  setup(1);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
  Network::Test::UdpSyncPeer client(version_);

  client.write("hello", *listener_address);
  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ("hello", request_datagram.buffer_->toString());

  const uint64_t large_datagram_size =
      (Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE * Network::NUM_DATAGRAMS_PER_RECEIVE) + 1024;
  fake_upstreams_[0]->sendUdpDatagram(std::string(large_datagram_size, 'a'),
                                      request_datagram.addresses_.peer_);
  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_rx_datagrams_dropped", 1);
}

// Test with large packet sizes.
TEST_P(UdpProxyIntegrationTest, LargePacketSizesOnLoopback) {
  // The following tests large packets end to end. We use a size larger than
  // the default GRO receive buffer size of
  // DEFAULT_UDP_MAX_DATAGRAM_SIZE x NUM_DATAGRAMS_PER_RECEIVE.
  const uint64_t max_rx_datagram_size =
      (Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE * Network::NUM_DATAGRAMS_PER_RECEIVE) + 1024;
  setup(1, max_rx_datagram_size);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
  requestResponseWithListenerAddress(*listener_address, std::string(max_rx_datagram_size, 'a'),
                                     std::string(max_rx_datagram_size, 'a'),
                                     std::string(max_rx_datagram_size, 'b'),
                                     std::string(max_rx_datagram_size, 'b'), max_rx_datagram_size);
}

// Verifies calling sendmsg with a non-local address. Note that this test is only fully complete
// for IPv4. See the comment below for more details.
TEST_P(UdpProxyIntegrationTest, HelloWorldOnNonLocalAddress) {
  setup(1);
  const uint32_t port = lookupPort("listener_0");
  Network::Address::InstanceConstSharedPtr listener_address;
  if (version_ == Network::Address::IpVersion::v4) {
    // Kernel regards any 127.x.x.x as local address.
    listener_address = std::make_shared<Network::Address::Ipv4Instance>(
#if defined(__APPLE__) || defined(WIN32)
        "127.0.0.1",
#else
        "127.0.0.3",
#endif
        port);
  } else {
    // IPv6 doesn't allow any non-local source address for sendmsg. And the only
    // local address guaranteed in tests in loopback. Unfortunately, even if it's not
    // specified, kernel will pick this address as source address. So this test
    // only checks if IoSocketHandle::sendmsg() sets up CMSG_DATA correctly,
    // i.e. cmsg_len is big enough when that code path is executed.
    listener_address = std::make_shared<Network::Address::Ipv6Instance>("::1", port);
  }

  requestResponseWithListenerAddress(*listener_address);
}

// Make sure multiple clients are routed correctly to a single upstream host.
TEST_P(UdpProxyIntegrationTest, MultipleClients) {
  setup(1);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  Network::Test::UdpSyncPeer client1(version_);
  client1.write("client1_hello", *listener_address);

  Network::Test::UdpSyncPeer client2(version_);
  client2.write("client2_hello", *listener_address);
  client2.write("client2_hello_2", *listener_address);

  Network::UdpRecvData client1_request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(client1_request_datagram));
  EXPECT_EQ("client1_hello", client1_request_datagram.buffer_->toString());

  Network::UdpRecvData client2_request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(client2_request_datagram));
  EXPECT_EQ("client2_hello", client2_request_datagram.buffer_->toString());
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(client2_request_datagram));
  EXPECT_EQ("client2_hello_2", client2_request_datagram.buffer_->toString());

  // We should not be getting datagrams from the same peer.
  EXPECT_NE(*client1_request_datagram.addresses_.peer_, *client2_request_datagram.addresses_.peer_);

  // Send two datagrams back to client 2.
  fake_upstreams_[0]->sendUdpDatagram("client2_world", client2_request_datagram.addresses_.peer_);
  fake_upstreams_[0]->sendUdpDatagram("client2_world_2", client2_request_datagram.addresses_.peer_);
  Network::UdpRecvData response_datagram;
  client2.recv(response_datagram);
  EXPECT_EQ("client2_world", response_datagram.buffer_->toString());
  client2.recv(response_datagram);
  EXPECT_EQ("client2_world_2", response_datagram.buffer_->toString());

  // Send 1 datagram back to client 1.
  fake_upstreams_[0]->sendUdpDatagram("client1_world", client1_request_datagram.addresses_.peer_);
  client1.recv(response_datagram);
  EXPECT_EQ("client1_world", response_datagram.buffer_->toString());
}

// Make sure sessions correctly forward to the same upstream host when there are multiple upstream
// hosts.
TEST_P(UdpProxyIntegrationTest, MultipleUpstreams) {
  setup(2);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  Network::Test::UdpSyncPeer client(version_);
  client.write("hello1", *listener_address);
  client.write("hello2", *listener_address);
  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ("hello1", request_datagram.buffer_->toString());
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ("hello2", request_datagram.buffer_->toString());

  fake_upstreams_[0]->sendUdpDatagram("world1", request_datagram.addresses_.peer_);
  fake_upstreams_[0]->sendUdpDatagram("world2", request_datagram.addresses_.peer_);
  Network::UdpRecvData response_datagram;
  client.recv(response_datagram);
  EXPECT_EQ("world1", response_datagram.buffer_->toString());
  client.recv(response_datagram);
  EXPECT_EQ("world2", response_datagram.buffer_->toString());
}

// Make sure the UDP proxy filter on the chain will work.
TEST_P(UdpProxyIntegrationTest, MultipleFilters) {
  setupMultiple();
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
  requestResponseWithListenerAddress(*listener_address, "hello", "olleh");
}

} // namespace
} // namespace Envoy

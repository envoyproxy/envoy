#include "common/network/address_impl.h"
#include "common/network/listen_socket_impl.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"

namespace Envoy {

std::string udp_echo_config;

class UdpEchoIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                               public BaseIntegrationTest {
public:
  UdpEchoIntegrationTest() : BaseIntegrationTest(GetParam(), udp_echo_config) {}

  // Called once by the gtest framework before any UdpEchoIntegrationTests are run.
  static void SetUpTestSuite() {
    udp_echo_config = ConfigHelper::BASE_UDP_LISTENER_CONFIG + R"EOF(
    listener_filters:
      name: envoy.listener.udpecho
      )EOF";
  }

  /**
   * Initializer for an individual test.
   */
  void SetUp() override { BaseIntegrationTest::initialize(); }

  /**
   *  Destructor for an individual test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void
  requestResponseWithListenerAddress(Network::Address::InstanceConstSharedPtr& listener_address) {
    using NetworkSocketTraitType =
        Network::NetworkSocketTrait<Network::Address::SocketType::Datagram>;

    // Setup client socket.
    Network::SocketPtr client_socket =
        std::make_unique<Network::NetworkListenSocket<NetworkSocketTraitType>>(
            Network::Test::getCanonicalLoopbackAddress(version_), nullptr, true);
    ASSERT_NE(client_socket, nullptr);

    const std::string request("hello world");
    const void* void_pointer = static_cast<const void*>(request.c_str());
    Buffer::RawSlice slice{const_cast<void*>(void_pointer), request.length()};
    auto send_rc = client_socket->ioHandle().sendto(slice, 0, *listener_address);
    ASSERT_EQ(send_rc.rc_, request.length());

    auto& os_sys_calls = Api::OsSysCallsSingleton::get();
    sockaddr_storage peer_addr;
    socklen_t addr_len = sizeof(sockaddr_storage);

    const uint64_t bytes_to_read = request.length();
    auto recv_buf = std::make_unique<char[]>(bytes_to_read + 1);
    uint64_t bytes_read = 0;

    int retry = 0;
    do {
      Api::SysCallSizeResult result =
          os_sys_calls.recvfrom(client_socket->ioHandle().fd(), recv_buf.get(), bytes_to_read, 0,
                                reinterpret_cast<struct sockaddr*>(&peer_addr), &addr_len);
      if (result.rc_ >= 0) {
        bytes_read = result.rc_;
        Network::Address::InstanceConstSharedPtr peer_address =
            Network::Address::addressFromSockAddr(peer_addr, addr_len, false);
        // Expect to receive from the same peer address as it sent to.
        EXPECT_EQ(listener_address->asString(), peer_address->asString());
      } else if (retry == 10 || result.errno_ != EAGAIN) {
        break;
      }

      if (bytes_read >= bytes_to_read) {
        break;
      }
      ASSERT(bytes_read == 0);
      // Retry after 10ms
      timeSystem().sleep(std::chrono::milliseconds(10));
      retry++;
    } while (true);

    recv_buf[bytes_to_read] = '\0';
    EXPECT_EQ(recv_buf.get(), request);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, UdpEchoIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(UdpEchoIntegrationTest, HelloWorldOnLoopback) {
  uint32_t port = lookupPort("listener_0");
  auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
  requestResponseWithListenerAddress(listener_address);
}

TEST_P(UdpEchoIntegrationTest, HelloWorldOnNonLocalAddress) {
  uint32_t port = lookupPort("listener_0");
  Network::Address::InstanceConstSharedPtr listener_address;
  if (version_ == Network::Address::IpVersion::v4) {
    // Kernel regards any 127.x.x.x as local address.
    listener_address.reset(new Network::Address::Ipv4Instance(
#ifndef __APPLE__
        "127.0.0.3",
#else
        "127.0.0.1",
#endif
        port));
  } else {
    // IPv6 doesn't allow any non-local source address for sendmsg. And the only
    // local address guaranteed in tests in loopback. Unfortunately, even if it's not
    // specified, kernel will pick this address as source address. So this test
    // only checks if IoSocketHandle::sendmsg() sets up CMSG_DATA correctly,
    // i.e. cmsg_len is big enough when that code path is executed.
    listener_address.reset(new Network::Address::Ipv6Instance("::1", port));
  }

  requestResponseWithListenerAddress(listener_address);
}

} // namespace Envoy

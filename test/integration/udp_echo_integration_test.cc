#include "common/network/listen_socket_impl.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"

namespace Envoy {

std::string udp_echo_config;

class UdpEchoIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                               public BaseIntegrationTest {
public:
  UdpEchoIntegrationTest() : BaseIntegrationTest(GetParam(), udp_echo_config) {}

  // Called once by the gtest framework before any EchoIntegrationTests are run.
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
};

INSTANTIATE_TEST_SUITE_P(IpVersions, UdpEchoIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(UdpEchoIntegrationTest, HelloWorld) {
  uint32_t port = lookupPort("listener_0");
  auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

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

  Buffer::InstancePtr response_buffer(new Buffer::OwnedImpl());
  int retry = 0;
  do {
    Api::IoCallUint64Result result =
        response_buffer->read(client_socket->ioHandle(), request.size());

    if (result.ok() || retry == 10 ||
        result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
      break;
    }

    // Retry after 10ms
    timeSystem().sleep(std::chrono::milliseconds(10));
    retry++;
  } while (true);

  EXPECT_EQ(response_buffer->toString(), request);
}

} // namespace Envoy

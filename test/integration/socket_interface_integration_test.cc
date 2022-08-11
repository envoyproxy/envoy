#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface.h"

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
    return absl::StrCat(echoConfig(), R"EOF(
bootstrap_extensions:
  - name: envoy.extensions.network.socket_interface.default_socket_interface
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network.socket_interface.v3.DefaultSocketInterface
default_socket_interface: "envoy.extensions.network.socket_interface.default_socket_interface"
    )EOF");
  }
  static std::string echoConfig() {
    return absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
        name: ratelimit
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.rate_limit.v2.RateLimit
          domain: foo
          stats_prefix: name
          descriptors: [{"key": "foo", "value": "bar"}]
      filters:
        name: envoy.filters.network.echo
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

TEST_P(SocketInterfaceIntegrationTest, AddressWithSocketInterface) {
  BaseIntegrationTest::initialize();

  ConnectionStatusCallbacks connect_callbacks_;
  Network::ClientConnectionPtr client_;
  const Network::SocketInterface* sock_interface = Network::socketInterface(
      "envoy.extensions.network.socket_interface.default_socket_interface");
  Network::Address::InstanceConstSharedPtr address =
      std::make_shared<Network::Address::Ipv4Instance>(
          Network::Test::getLoopbackAddressUrlString(Network::Address::IpVersion::v4),
          lookupPort("listener_0"), sock_interface);

  client_ =
      dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                          Network::Test::createRawBufferSocket(), nullptr, nullptr);

  client_->addConnectionCallbacks(connect_callbacks_);
  client_->connect();
  while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  client_->close(Network::ConnectionCloseType::FlushWrite);
}

// Test that connecting to internal address will crash if the user space socket extension is not
// linked.
TEST_P(SocketInterfaceIntegrationTest, InternalAddressWithSocketInterface) {
  BaseIntegrationTest::initialize();

  ConnectionStatusCallbacks connect_callbacks_;
  Network::ClientConnectionPtr client_;
  const Network::SocketInterface* sock_interface = Network::socketInterface(
      "envoy.extensions.network.socket_interface.default_socket_interface");
  Network::Address::InstanceConstSharedPtr address =
      std::make_shared<Network::Address::EnvoyInternalInstance>("listener_0", "endpoint_id_0",
                                                                sock_interface);

  ASSERT_DEATH(client_ = dispatcher_->createClientConnection(
                   address, Network::Address::InstanceConstSharedPtr(),
                   Network::Test::createRawBufferSocket(), nullptr, nullptr),
               "" /* Nullptr dereference */);
}

// Test that recv from internal address will crash.
// TODO(lambdai): Add UDP internal listener implementation to enable the io path.
TEST_P(SocketInterfaceIntegrationTest, UdpRecvFromInternalAddressWithSocketInterface) {
  BaseIntegrationTest::initialize();

  const Network::SocketInterface* sock_interface = Network::socketInterface(
      "envoy.extensions.network.socket_interface.default_socket_interface");
  Network::Address::InstanceConstSharedPtr address =
      std::make_shared<Network::Address::EnvoyInternalInstance>("listener_0", "endpoint_id_0",
                                                                sock_interface);

  ASSERT_DEATH(std::make_unique<Network::SocketImpl>(Network::Socket::Type::Datagram, address,
                                                     nullptr, Network::SocketCreationOptions{}),
               "");
}

// Test that send to internal address will return io error.
TEST_P(SocketInterfaceIntegrationTest, UdpSendToInternalAddressWithSocketInterface) {
  BaseIntegrationTest::initialize();

  const Network::SocketInterface* sock_interface = Network::socketInterface(
      "envoy.extensions.network.socket_interface.default_socket_interface");
  Network::Address::InstanceConstSharedPtr peer_internal_address =
      std::make_shared<Network::Address::EnvoyInternalInstance>("listener_0", "endpoint_id_0",
                                                                sock_interface);
  Network::Address::InstanceConstSharedPtr local_valid_address =
      Network::Test::getCanonicalLoopbackAddress(version_);

  auto socket =
      std::make_unique<Network::SocketImpl>(Network::Socket::Type::Datagram, local_valid_address,
                                            nullptr, Network::SocketCreationOptions{});

  Buffer::OwnedImpl buffer;
  auto reservation = buffer.reserveSingleSlice(100);

  auto slice = reservation.slice();
  auto result =
      socket->ioHandle().sendmsg(&slice, 1, 0, local_valid_address->ip(), *peer_internal_address);
  ASSERT_FALSE(result.ok());
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::NoSupport);
}
} // namespace
} // namespace Envoy

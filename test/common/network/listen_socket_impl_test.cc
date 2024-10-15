#include <memory>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/exception.h"
#include "envoy/network/socket.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/utility.h"

#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Network {
namespace {

class ConnectionSocketImplTest : public testing::TestWithParam<Address::IpVersion> {};

INSTANTIATE_TEST_SUITE_P(IpVersions, ConnectionSocketImplTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(ConnectionSocketImplTest, LowerCaseRequestedServerName) {
  absl::string_view serverName("www.EXAMPLE.com");
  absl::string_view expectedServerName("www.example.com");
  auto loopback_addr = Network::Test::getCanonicalLoopbackAddress(Address::IpVersion::v4);
  auto conn_socket_ = ConnectionSocketImpl(Socket::Type::Stream, loopback_addr, loopback_addr, {});
  conn_socket_.setRequestedServerName(serverName);
  EXPECT_EQ(expectedServerName, conn_socket_.requestedServerName());
}

TEST_P(ConnectionSocketImplTest, IpVersion) {
  ClientSocketImpl socket(Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr);
  EXPECT_EQ(socket.ipVersion(), GetParam());
}

template <Network::Socket::Type Type>
class ListenSocketImplTest : public testing::TestWithParam<Address::IpVersion> {
  using ListenSocketType = NetworkListenSocket<NetworkSocketTrait<Type>>;

protected:
  ListenSocketImplTest() : version_(GetParam()) {}
  const Address::IpVersion version_;

  template <typename... Args>
  std::unique_ptr<ListenSocketType> createListenSocketPtr(Args&&... args) {
    return std::make_unique<ListenSocketType>(std::forward<Args>(args)...);
  }

  void testBindSpecificPort() {
    // This test has a small but real risk of flaky behavior if another thread or process should
    // bind to our assigned port during the interval between closing the fd and re-binding. In an
    // attempt to avoid this, we allow for retrying by placing the core of the test in a loop with
    // a catch of the SocketBindException, indicating we couldn't bind, at which point we retry.
    const int kLoopLimit = 20;
    int loop_number = 0;
    while (true) {
      ++loop_number;

      auto addr_fd = Network::Test::bindFreeLoopbackPort(version_, Socket::Type::Stream);
      auto addr = addr_fd.first;
      SocketPtr& sock = addr_fd.second;
      EXPECT_TRUE(sock->ioHandle().isOpen());

      // Confirm that we got a reasonable address and port.
      ASSERT_EQ(Address::Type::Ip, addr->type());
      ASSERT_EQ(version_, addr->ip()->version());
      ASSERT_LT(0U, addr->ip()->port());

      // Release the socket and re-bind it.
      EXPECT_TRUE(sock->isOpen());
      sock->close();

      auto option = std::make_unique<MockSocketOption>();
      auto options = std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>();
      EXPECT_CALL(*option, setOption(_, envoy::config::core::v3::SocketOption::STATE_PREBIND))
          .WillOnce(Return(true));
      options->emplace_back(std::move(option));
      std::unique_ptr<ListenSocketType> socket1;
      try {
        socket1 = createListenSocketPtr(addr, options, true);
      } catch (SocketBindException& e) {
        if (e.errorNumber() != EADDRINUSE) {
          ADD_FAILURE() << "Unexpected failure (" << e.errorNumber()
                        << ") to bind a free port: " << e.what();
          throw;
        } else if (loop_number >= kLoopLimit) {
          ADD_FAILURE() << "Too many failures (" << loop_number
                        << ") to bind a specific port: " << e.what();
          return;
        }
        continue;
      }

      // TODO (conqerAtapple): This is unfortunate. We should be able to templatize this
      // instead of if block.
      auto os_sys_calls = Api::OsSysCallsSingleton::get();
      if (NetworkSocketTrait<Type>::type == Socket::Type::Stream) {
        EXPECT_EQ(0, socket1->listen(0).return_value_);
      }

      EXPECT_EQ(addr->ip()->port(), socket1->connectionInfoProvider().localAddress()->ip()->port());
      EXPECT_EQ(addr->ip()->addressAsString(),
                socket1->connectionInfoProvider().localAddress()->ip()->addressAsString());
      EXPECT_EQ(Type, socket1->socketType());

      auto option2 = std::make_unique<MockSocketOption>();
      auto options2 = std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>();
      EXPECT_CALL(*option2, setOption(_, envoy::config::core::v3::SocketOption::STATE_PREBIND))
          .WillOnce(Return(true));
      options2->emplace_back(std::move(option2));
      // The address and port are bound already, should throw exception.
      EXPECT_THROW(createListenSocketPtr(addr, options2, true), SocketBindException);

      // Release socket and re-bind it.
      socket1->close();

      // Test createListenSocketPtr from IoHandlePtr's os_fd_t constructor
      int domain = version_ == Address::IpVersion::v4 ? AF_INET : AF_INET6;
      auto socket_result = os_sys_calls.socket(domain, SOCK_STREAM, 0);
      EXPECT_TRUE(SOCKET_VALID(socket_result.return_value_));
      Network::IoHandlePtr io_handle =
          std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(socket_result.return_value_);
      auto socket3 = createListenSocketPtr(std::move(io_handle), addr, nullptr);
      EXPECT_EQ(socket3->connectionInfoProvider().localAddress()->asString(), addr->asString());

      // Test successful.
      return;
    }
  }

  void testBindPortZero() {
    auto loopback = Network::Test::getCanonicalLoopbackAddress(version_);
    auto socket = createListenSocketPtr(loopback, nullptr, true);
    EXPECT_EQ(Address::Type::Ip, socket->connectionInfoProvider().localAddress()->type());
    EXPECT_EQ(version_, socket->connectionInfoProvider().localAddress()->ip()->version());
    EXPECT_EQ(loopback->ip()->addressAsString(),
              socket->connectionInfoProvider().localAddress()->ip()->addressAsString());
    EXPECT_GT(socket->connectionInfoProvider().localAddress()->ip()->port(), 0U);
    EXPECT_EQ(Type, socket->socketType());
  }

  // Verify that a listen sockets that do not bind to port can be duplicated and closed.
  void testNotBindToPort() {
    auto local_address = version_ == Address::IpVersion::v4 ? Utility::getIpv6AnyAddress()
                                                            : Utility::getIpv4AnyAddress();
    auto socket = NetworkListenSocket<NetworkSocketTrait<Type>>(local_address, nullptr,
                                                                /*bind_to_port=*/false);
    auto dup_socket = socket.duplicate();
    EXPECT_FALSE(socket.isOpen());
    EXPECT_FALSE(dup_socket->isOpen());
    socket.close();
    dup_socket->close();
  }
};

using ListenSocketImplTestTcp = ListenSocketImplTest<Network::Socket::Type::Stream>;
using ListenSocketImplTestUdp = ListenSocketImplTest<Network::Socket::Type::Datagram>;

INSTANTIATE_TEST_SUITE_P(IpVersions, ListenSocketImplTestTcp,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, ListenSocketImplTestUdp,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ListenSocketImplTestTcp, BindSpecificPort) { testBindSpecificPort(); }

/*
 * A simple implementation to test some of ListenSocketImpl's accessors without requiring
 * stack interaction.
 */
class TestListenSocket : public ListenSocketImpl {
public:
  TestListenSocket(Address::InstanceConstSharedPtr address)
      : ListenSocketImpl(std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(), address) {}

  TestListenSocket(Address::IpVersion ip_version)
      : ListenSocketImpl(/*io_handle=*/nullptr, ip_version == Address::IpVersion::v4
                                                    ? Utility::getIpv4AnyAddress()
                                                    : Utility::getIpv6AnyAddress()) {}
  Socket::Type socketType() const override { return Socket::Type::Stream; }

  bool isOpen() const override { return ListenSocketImpl::isOpen(); }
  void close() override { ListenSocketImpl::close(); }
};

TEST_P(ListenSocketImplTestTcp, NonIoHandleListenSocket) {
  TestListenSocket sock(version_);
  EXPECT_FALSE(sock.isOpen());
  sock.close();
}

TEST_P(ListenSocketImplTestTcp, SetLocalAddress) {
  std::string address_str = "10.1.2.3";
  if (version_ == Address::IpVersion::v6) {
    address_str = "1::2";
  }

  Address::InstanceConstSharedPtr address =
      Network::Utility::parseInternetAddressNoThrow(address_str);

  TestListenSocket socket(Utility::getIpv4AnyAddress());

  socket.connectionInfoProvider().setLocalAddress(address);

  EXPECT_EQ(socket.connectionInfoProvider().localAddress(), address);
}

TEST_P(ListenSocketImplTestTcp, CheckIpVersionWithNullLocalAddress) {
  TestListenSocket socket(Utility::getIpv4AnyAddress());
  EXPECT_EQ(Address::IpVersion::v4, socket.ipVersion());
}

TEST_P(ListenSocketImplTestTcp, SupportedIpFamilyVirtualSocketIsCreatedWithNoBsdSocketCreated) {
  auto mock_interface =
      std::make_unique<MockSocketInterface>(std::vector<Network::Address::IpVersion>{version_});
  auto* mock_interface_ptr = mock_interface.get();
  auto any_address = version_ == Address::IpVersion::v4 ? Utility::getIpv4AnyAddress()
                                                        : Utility::getIpv6AnyAddress();

  StackedScopedInjectableLoaderForTest<SocketInterface> new_interface(std::move(mock_interface));

  {
    EXPECT_CALL(*mock_interface_ptr, socket(_, _, _)).Times(0);
    EXPECT_CALL(*mock_interface_ptr, socket(_, _, _, _, _)).Times(0);
    TcpListenSocket virtual_listener_socket(any_address, nullptr,
                                            /*bind_to_port*/ false);
  }
}

TEST_P(ListenSocketImplTestTcp, DeathAtUnSupportedIpFamilyListenSocket) {
  auto mock_interface =
      std::make_unique<MockSocketInterface>(std::vector<Network::Address::IpVersion>{version_});
  auto* mock_interface_ptr = mock_interface.get();
  auto the_other_address = version_ == Address::IpVersion::v4 ? Utility::getIpv6AnyAddress()
                                                              : Utility::getIpv4AnyAddress();
  StackedScopedInjectableLoaderForTest<SocketInterface> new_interface(std::move(mock_interface));
  {
    EXPECT_CALL(*mock_interface_ptr, socket(_, _, _)).Times(0);
    EXPECT_CALL(*mock_interface_ptr, socket(_, _, _, _, _)).Times(0);
    EXPECT_DEATH(
        {
          TcpListenSocket virtual_listener_socket(the_other_address, nullptr,
                                                  /*bind_to_port*/ false);
        },
        ".*");
  }
}

TEST_P(ListenSocketImplTestUdp, BindSpecificPort) { testBindSpecificPort(); }

// Validate that we get port allocation when binding to port zero.
TEST_P(ListenSocketImplTestTcp, BindPortZero) { testBindPortZero(); }

TEST_P(ListenSocketImplTestUdp, BindPortZero) { testBindPortZero(); }

TEST_P(ListenSocketImplTestTcp, NotBindToPortAccess) { testNotBindToPort(); }

TEST_P(ListenSocketImplTestUdp, NotBindToPortAccess) { testNotBindToPort(); }

} // namespace
} // namespace Network
} // namespace Envoy

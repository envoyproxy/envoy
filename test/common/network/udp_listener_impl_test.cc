#include <memory>
#include <string>
#include <vector>

#include "common/network/address_impl.h"
#include "common/network/udp_listener_impl.h"
#include "common/network/utility.h"

#include "test/common/network/listener_impl_test_base.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Network {
namespace {

class TestUdpListenerImpl : public UdpListenerImpl {
public:
  TestUdpListenerImpl(Event::DispatcherImpl& dispatcher, Socket& socket, UdpListenerCallbacks& cb)
      : UdpListenerImpl(dispatcher, socket, cb) {}

  MOCK_METHOD2(doRecvFrom,
               UdpListenerImpl::ReceiveResult(sockaddr_storage& peer_addr, socklen_t& addr_len));

  UdpListenerImpl::ReceiveResult doRecvFrom_(sockaddr_storage& peer_addr, socklen_t& addr_len) {
    return UdpListenerImpl::doRecvFrom(peer_addr, addr_len);
  }
};

class UdpListenerImplTest : public ListenerImplTestBase {
protected:
  SocketPtr getSocket(Address::SocketType type, const Address::InstanceConstSharedPtr& address,
                      const Network::Socket::OptionsSharedPtr& options, bool bind) {
    if (type == Address::SocketType::Stream) {
      using NetworkSocketTraitType = NetworkSocketTrait<Address::SocketType::Stream>;
      return std::make_unique<NetworkListenSocket<NetworkSocketTraitType>>(address, options, bind);
    } else if (type == Address::SocketType::Datagram) {
      using NetworkSocketTraitType = NetworkSocketTrait<Address::SocketType::Datagram>;
      return std::make_unique<NetworkListenSocket<NetworkSocketTraitType>>(address, options, bind);
    }

    return nullptr;
  }

  // TODO(conqerAtapple): Move this to a common place(address.h?)
  void getSocketAddressInfo(const Address::Ip* ip, uint32_t port, sockaddr_storage& addr,
                            socklen_t& sz) {
    if (!ip) {
      sz = 0;
      return;
    }

    memset(&addr, 0, sizeof(addr));

    if (version_ == Address::IpVersion::v4) {
      addr.ss_family = AF_INET;
      auto const* ipv4 = ip->ipv4();
      if (!ipv4) {
        sz = 0;
        return;
      }

      sockaddr_in* addrv4 = reinterpret_cast<sockaddr_in*>(&addr);
      addrv4->sin_port = htons(port);
      addrv4->sin_addr.s_addr = ipv4->address();

      sz = sizeof(sockaddr_in);
    } else if (version_ == Address::IpVersion::v6) {
      addr.ss_family = AF_INET6;
      auto const* ipv6 = ip->ipv6();
      if (!ipv6) {
        sz = 0;
        return;
      }

      sockaddr_in6* addrv6 = reinterpret_cast<sockaddr_in6*>(&addr);
      addrv6->sin6_port = htons(port);

      const auto address = ipv6->address();
      memcpy(static_cast<void*>(&addrv6->sin6_addr.s6_addr), static_cast<const void*>(&address),
             sizeof(absl::uint128));

      sz = sizeof(sockaddr_in6);
    } else {
      sz = 0;
    }
  }

  void getSocketAddressInfo(const Socket& socket, uint32_t port, sockaddr_storage& addr,
                            socklen_t& sz) {
    getSocketAddressInfo(socket.localAddress()->ip(), port, addr, sz);
  }

  void getSocketAddressInfo(Address::InstanceConstSharedPtr address, uint32_t port,
                            sockaddr_storage& addr, socklen_t& sz) {
    ASSERT(address);

    getSocketAddressInfo(address->ip(), port, addr, sz);
  }
};
INSTANTIATE_TEST_CASE_P(IpVersions, UdpListenerImplTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

// Test that socket options are set after the listener is setup.
TEST_P(UdpListenerImplTest, UdpSetListeningSocketOptionsSuccess) {
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;

  Network::UdpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version_), nullptr,
                                  true);
  std::shared_ptr<MockSocketOption> option = std::make_shared<MockSocketOption>();
  socket.addOption(option);
}

/**
 * Tests UDP listener for actual destination and data.
 */
TEST_P(UdpListenerImplTest, UseActualDstUdp) {
  // Setup server socket
  SocketPtr server_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, true);

  ASSERT_NE(server_socket, nullptr);

  auto const* server_ip = server_socket->localAddress()->ip();
  ASSERT_NE(server_ip, nullptr);

  // Setup callback handler and listener.
  Network::MockUdpListenerCallbacks listener_callbacks;
  Network::TestUdpListenerImpl listener(dispatcherImpl(), *server_socket.get(), listener_callbacks);

  EXPECT_CALL(listener, doRecvFrom(_, _))
      .WillRepeatedly(Invoke([&](sockaddr_storage& peer_addr, socklen_t& addr_len) {
        return listener.doRecvFrom_(peer_addr, addr_len);
      }));

  // Setup client socket.
  SocketPtr client_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, false);

  const int client_sockfd = client_socket->ioHandle().fd();
  sockaddr_storage server_addr;
  socklen_t addr_len;

  getSocketAddressInfo(*client_socket.get(), server_ip->port(), server_addr, addr_len);
  ASSERT_GT(addr_len, 0);

  // We send 2 packets
  const std::string first("first");
  const std::string second("second");

  auto send_rc = ::sendto(client_sockfd, first.c_str(), first.length(), 0,
                          reinterpret_cast<const struct sockaddr*>(&server_addr), addr_len);

  ASSERT_EQ(send_rc, first.length());

  send_rc = ::sendto(client_sockfd, second.c_str(), second.length(), 0,
                     reinterpret_cast<const struct sockaddr*>(&server_addr), addr_len);

  ASSERT_EQ(send_rc, second.length());

  auto validateCallParams = [&](Address::InstanceConstSharedPtr local_address,
                                Address::InstanceConstSharedPtr peer_address) {
    ASSERT_NE(local_address, nullptr);

    ASSERT_NE(peer_address, nullptr);
    ASSERT_NE(peer_address->ip(), nullptr);

    EXPECT_EQ(local_address->asString(), server_socket->localAddress()->asString());

    EXPECT_EQ(peer_address->ip()->addressAsString(),
              client_socket->localAddress()->ip()->addressAsString());

    EXPECT_EQ(*local_address, *server_socket->localAddress());
  };

  EXPECT_CALL(listener_callbacks, onData_(_))
      .WillOnce(Invoke([&](const UdpData& data) -> void {
        validateCallParams(data.local_address_, data.peer_address_);

        EXPECT_EQ(data.buffer_->toString(), first);
      }))
      .WillOnce(Invoke([&](const UdpData& data) -> void {
        validateCallParams(data.local_address_, data.peer_address_);

        EXPECT_EQ(data.buffer_->toString(), second);

        dispatcher_->exit();
      }));

  EXPECT_CALL(listener_callbacks, onWriteReady_(_))
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(socket.ioHandle().fd(), server_socket->ioHandle().fd());
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener for read and write callbacks with actual data.
 */
TEST_P(UdpListenerImplTest, UdpEcho) {
  // Setup server socket
  SocketPtr server_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, true);

  ASSERT_NE(server_socket, nullptr);

  auto const* server_ip = server_socket->localAddress()->ip();
  ASSERT_NE(server_ip, nullptr);

  // Setup callback handler and listener.
  Network::MockUdpListenerCallbacks listener_callbacks;
  Network::TestUdpListenerImpl listener(dispatcherImpl(), *server_socket.get(), listener_callbacks);

  EXPECT_CALL(listener, doRecvFrom(_, _))
      .WillRepeatedly(Invoke([&](sockaddr_storage& peer_addr, socklen_t& addr_len) {
        return listener.doRecvFrom_(peer_addr, addr_len);
      }));

  // Setup client socket.
  SocketPtr client_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, false);

  const int client_sockfd = client_socket->ioHandle().fd();
  sockaddr_storage server_addr;
  socklen_t addr_len;

  getSocketAddressInfo(*client_socket.get(), server_ip->port(), server_addr, addr_len);
  ASSERT_GT(addr_len, 0);

  // We send 2 packets and expect it to echo.
  const std::string first("first");
  const std::string second("second");

  auto send_rc = ::sendto(client_sockfd, first.c_str(), first.length(), 0,
                          reinterpret_cast<const struct sockaddr*>(&server_addr), addr_len);

  ASSERT_EQ(send_rc, first.length());

  send_rc = ::sendto(client_sockfd, second.c_str(), second.length(), 0,
                     reinterpret_cast<const struct sockaddr*>(&server_addr), addr_len);

  ASSERT_EQ(send_rc, second.length());

  auto validateCallParams = [&](Address::InstanceConstSharedPtr local_address,
                                Address::InstanceConstSharedPtr peer_address) {
    ASSERT_NE(local_address, nullptr);

    ASSERT_NE(peer_address, nullptr);
    ASSERT_NE(peer_address->ip(), nullptr);

    EXPECT_EQ(local_address->asString(), server_socket->localAddress()->asString());

    EXPECT_EQ(peer_address->ip()->addressAsString(),
              client_socket->localAddress()->ip()->addressAsString());

    EXPECT_EQ(*local_address, *server_socket->localAddress());
  };

  Event::TimerPtr timer = dispatcher_->createTimer([&] { dispatcher_->exit(); });

  timer->enableTimer(std::chrono::milliseconds(2000));

  // For unit test purposes, we assume that the data was received in order.
  Address::InstanceConstSharedPtr test_peer_address;

  std::vector<std::string> server_received_data;

  EXPECT_CALL(listener_callbacks, onData_(_))
      .WillOnce(Invoke([&](const UdpData& data) -> void {
        validateCallParams(data.local_address_, data.peer_address_);

        test_peer_address = data.peer_address_;

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, first);

        server_received_data.push_back(data_str);
      }))
      .WillOnce(Invoke([&](const UdpData& data) -> void {
        validateCallParams(data.local_address_, data.peer_address_);

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, second);

        server_received_data.push_back(data_str);
      }));

  EXPECT_CALL(listener_callbacks, onWriteReady_(_))
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(socket.ioHandle().fd(), server_socket->ioHandle().fd());

        sockaddr_storage client_addr;
        socklen_t client_addr_len;

        ASSERT_NE(test_peer_address, nullptr);
        const uint32_t peer_port = test_peer_address->ip()->port();

        getSocketAddressInfo(test_peer_address, peer_port, client_addr, client_addr_len);
        ASSERT_GT(client_addr_len, 0);

        for (const auto& data : server_received_data) {
          const std::string::size_type data_size = data.length() + 1;
          uint64_t total_sent = 0;

          do {
            auto send_rc = ::sendto(
                socket.ioHandle().fd(), data.c_str() + total_sent, data_size - total_sent, 0,
                reinterpret_cast<const struct sockaddr*>(&client_addr), client_addr_len);

            if (send_rc > 0) {
              total_sent += send_rc;
              if (total_sent >= data_size) {
                break;
              }
            } else if (errno != EAGAIN) {
              break;
            }
          } while (((send_rc < 0) && (errno == EAGAIN)) || (total_sent < data_size));

          EXPECT_EQ(total_sent, data_size);
        }

        server_received_data.clear();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener's `enable` and `disable` APIs.
 */
TEST_P(UdpListenerImplTest, UdpListenerEnableDisable) {
  // Setup server socket
  SocketPtr server_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, true);

  ASSERT_NE(server_socket, nullptr);

  auto const* server_ip = server_socket->localAddress()->ip();
  ASSERT_NE(server_ip, nullptr);

  // Setup callback handler and listener.
  Network::MockUdpListenerCallbacks listener_callbacks;
  Network::TestUdpListenerImpl listener(dispatcherImpl(), *server_socket.get(), listener_callbacks);

  EXPECT_CALL(listener, doRecvFrom(_, _))
      .WillRepeatedly(Invoke([&](sockaddr_storage& peer_addr, socklen_t& addr_len) {
        return listener.doRecvFrom_(peer_addr, addr_len);
      }));

  // Setup client socket.
  SocketPtr client_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, false);

  const int client_sockfd = client_socket->ioHandle().fd();
  sockaddr_storage server_addr;
  socklen_t addr_len;

  getSocketAddressInfo(*client_socket.get(), server_ip->port(), server_addr, addr_len);
  ASSERT_GT(addr_len, 0);

  // We first disable the listener and then send two packets.
  // - With the listener disabled, we expect that none of the callbacks will be
  // called.
  // - When the listener is enabled back, we expect the callbacks to be called
  const std::string first("first");
  const std::string second("second");

  listener.disable();

  auto send_rc = ::sendto(client_sockfd, first.c_str(), first.length(), 0,
                          reinterpret_cast<const struct sockaddr*>(&server_addr), addr_len);

  ASSERT_EQ(send_rc, first.length());

  send_rc = ::sendto(client_sockfd, second.c_str(), second.length(), 0,
                     reinterpret_cast<const struct sockaddr*>(&server_addr), addr_len);

  ASSERT_EQ(send_rc, second.length());

  auto validateCallParams = [&](Address::InstanceConstSharedPtr local_address,
                                Address::InstanceConstSharedPtr peer_address) {
    ASSERT_NE(local_address, nullptr);

    ASSERT_NE(peer_address, nullptr);
    ASSERT_NE(peer_address->ip(), nullptr);

    ASSERT_EQ(local_address->asString(), server_socket->localAddress()->asString());

    ASSERT_EQ(peer_address->ip()->addressAsString(),
              client_socket->localAddress()->ip()->addressAsString());

    EXPECT_EQ(*local_address, *server_socket->localAddress());
  };

  Event::TimerPtr timer = dispatcher_->createTimer([&] { dispatcher_->exit(); });

  timer->enableTimer(std::chrono::milliseconds(2000));

  EXPECT_CALL(listener_callbacks, onData_(_)).Times(0);

  EXPECT_CALL(listener_callbacks, onWriteReady_(_)).Times(0);

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  listener.enable();

  EXPECT_CALL(listener_callbacks, onData_(_))
      .Times(2)
      .WillOnce(Return())
      .WillOnce(Invoke([&](const UdpData& data) -> void {
        validateCallParams(data.local_address_, data.peer_address_);

        EXPECT_EQ(data.buffer_->toString(), second);

        dispatcher_->exit();
      }));

  EXPECT_CALL(listener_callbacks, onWriteReady_(_))
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(socket.ioHandle().fd(), server_socket->ioHandle().fd());
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener's error callback.
 */
TEST_P(UdpListenerImplTest, UdpListenerRecvFromError) {
  // Setup server socket
  SocketPtr server_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, true);

  ASSERT_NE(server_socket, nullptr);

  auto const* server_ip = server_socket->localAddress()->ip();
  ASSERT_NE(server_ip, nullptr);

  // Setup callback handler and listener.
  Network::MockUdpListenerCallbacks listener_callbacks;
  Network::TestUdpListenerImpl listener(dispatcherImpl(), *server_socket.get(), listener_callbacks);

  EXPECT_CALL(listener, doRecvFrom(_, _)).WillRepeatedly(Invoke([&](sockaddr_storage&, socklen_t&) {
    return UdpListenerImpl::ReceiveResult{{-1, -1}, nullptr};
  }));

  SocketPtr client_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, false);

  const int client_sockfd = client_socket->ioHandle().fd();
  sockaddr_storage server_addr;
  socklen_t addr_len;

  getSocketAddressInfo(*client_socket.get(), server_ip->port(), server_addr, addr_len);
  ASSERT_GT(addr_len, 0);

  // When the `receive` system call returns an error, we expect the `onError`
  // callback callwed with `SyscallError` parameter.
  const std::string first("first");

  auto send_rc = ::sendto(client_sockfd, first.c_str(), first.length(), 0,
                          reinterpret_cast<const struct sockaddr*>(&server_addr), addr_len);

  ASSERT_EQ(send_rc, first.length());

  EXPECT_CALL(listener_callbacks, onData_(_)).Times(0);

  EXPECT_CALL(listener_callbacks, onWriteReady_(_))
      .Times(1)
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(socket.ioHandle().fd(), server_socket->ioHandle().fd());
      }));

  EXPECT_CALL(listener_callbacks, onError_(_, _))
      .Times(1)
      .WillOnce(Invoke([&](const UdpListenerCallbacks::ErrorCode& err_code, int err) -> void {
        ASSERT_EQ(err_code, UdpListenerCallbacks::ErrorCode::SyscallError);
        ASSERT_EQ(err, -1);

        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

} // namespace
} // namespace Network
} // namespace Envoy

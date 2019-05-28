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

  // Setup callback handler and listener.
  Network::MockUdpListenerCallbacks listener_callbacks;
  Network::TestUdpListenerImpl listener(dispatcherImpl(), *server_socket, listener_callbacks);

  EXPECT_CALL(listener, doRecvFrom(_, _))
      .WillRepeatedly(Invoke([&](sockaddr_storage& peer_addr, socklen_t& addr_len) {
        return listener.doRecvFrom_(peer_addr, addr_len);
      }));

  // Setup client socket.
  SocketPtr client_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, false);

  // We send 2 packets
  const std::string first("first");
  const void* void_pointer = static_cast<const void*>(first.c_str());
  Buffer::RawSlice first_slice{const_cast<void*>(void_pointer), first.length()};
  const std::string second("second");
  void_pointer = static_cast<const void*>(second.c_str());
  Buffer::RawSlice second_slice{const_cast<void*>(void_pointer), second.length()};

  auto send_rc = client_socket->ioHandle().sendto(first_slice, 0, *server_socket->localAddress());
  ASSERT_EQ(send_rc.rc_, first.length());

  send_rc = client_socket->ioHandle().sendto(second_slice, 0, *server_socket->localAddress());
  ASSERT_EQ(send_rc.rc_, second.length());

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
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateCallParams(data.local_address_, data.peer_address_);

        EXPECT_EQ(data.buffer_->toString(), first);
      }))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
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

  // Setup callback handler and listener.
  Network::MockUdpListenerCallbacks listener_callbacks;
  Network::TestUdpListenerImpl listener(dispatcherImpl(), *server_socket, listener_callbacks);

  EXPECT_CALL(listener, doRecvFrom(_, _))
      .WillRepeatedly(Invoke([&](sockaddr_storage& peer_addr, socklen_t& addr_len) {
        return listener.doRecvFrom_(peer_addr, addr_len);
      }));

  // Setup client socket.
  SocketPtr client_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, false);

  // We send 2 packets and expect it to echo.
  const std::string first("first");
  const void* void_pointer = static_cast<const void*>(first.c_str());
  Buffer::RawSlice first_slice{const_cast<void*>(void_pointer), first.length()};
  const std::string second("second");
  void_pointer = static_cast<const void*>(second.c_str());
  Buffer::RawSlice second_slice{const_cast<void*>(void_pointer), second.length()};

  auto send_rc = client_socket->ioHandle().sendto(first_slice, 0, *server_socket->localAddress());
  ASSERT_EQ(send_rc.rc_, first.length());

  send_rc = client_socket->ioHandle().sendto(second_slice, 0, *server_socket->localAddress());
  ASSERT_EQ(send_rc.rc_, second.length());

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
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateCallParams(data.local_address_, data.peer_address_);

        test_peer_address = data.peer_address_;

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, first);

        server_received_data.push_back(data_str);
      }))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateCallParams(data.local_address_, data.peer_address_);

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, second);

        server_received_data.push_back(data_str);
      }));

  EXPECT_CALL(listener_callbacks, onWriteReady_(_))
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(socket.ioHandle().fd(), server_socket->ioHandle().fd());
        ASSERT_NE(test_peer_address, nullptr);

        for (const auto& data : server_received_data) {
          const std::string::size_type data_size = data.length() + 1;
          uint64_t total_sent = 0;
          const void* void_data = static_cast<const void*>(data.c_str() + total_sent);
          Buffer::RawSlice slice{const_cast<void*>(void_data), data_size - total_sent};

          do {
            auto send_rc =
                const_cast<Socket*>(&socket)->ioHandle().sendto(slice, 0, *test_peer_address);

            if (send_rc.ok()) {
              total_sent += send_rc.rc_;
              if (total_sent >= data_size) {
                break;
              }
            } else if (send_rc.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
              break;
            }
          } while (((send_rc.rc_ == 0) &&
                    (send_rc.err_->getErrorCode() == Api::IoError::IoErrorCode::Again)) ||
                   (total_sent < data_size));

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
  Network::TestUdpListenerImpl listener(dispatcherImpl(), *server_socket, listener_callbacks);

  EXPECT_CALL(listener, doRecvFrom(_, _))
      .WillRepeatedly(Invoke([&](sockaddr_storage& peer_addr, socklen_t& addr_len) {
        return listener.doRecvFrom_(peer_addr, addr_len);
      }));

  // Setup client socket.
  SocketPtr client_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, false);

  // We first disable the listener and then send two packets.
  // - With the listener disabled, we expect that none of the callbacks will be
  // called.
  // - When the listener is enabled back, we expect the callbacks to be called
  const std::string first("first");
  const void* void_pointer = static_cast<const void*>(first.c_str());
  Buffer::RawSlice first_slice{const_cast<void*>(void_pointer), first.length()};
  const std::string second("second");
  void_pointer = static_cast<const void*>(second.c_str());
  Buffer::RawSlice second_slice{const_cast<void*>(void_pointer), second.length()};

  listener.disable();

  auto send_rc = client_socket->ioHandle().sendto(first_slice, 0, *server_socket->localAddress());
  ASSERT_EQ(send_rc.rc_, first.length());

  send_rc = client_socket->ioHandle().sendto(second_slice, 0, *server_socket->localAddress());
  ASSERT_EQ(send_rc.rc_, second.length());

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

  EXPECT_CALL(listener_callbacks, onData_(_)).Times(0);

  EXPECT_CALL(listener_callbacks, onWriteReady_(_)).Times(0);

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  listener.enable();

  EXPECT_CALL(listener_callbacks, onData_(_))
      .Times(2)
      .WillOnce(Return())
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
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
  Network::TestUdpListenerImpl listener(dispatcherImpl(), *server_socket, listener_callbacks);

  EXPECT_CALL(listener, doRecvFrom(_, _)).WillRepeatedly(Invoke([&](sockaddr_storage&, socklen_t&) {
    return UdpListenerImpl::ReceiveResult{{-1, -1}, nullptr};
  }));

  SocketPtr client_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, false);

  // When the `receive` system call returns an error, we expect the `onReceiveError`
  // callback callwed with `SyscallError` parameter.
  const std::string first("first");
  const void* void_pointer = static_cast<const void*>(first.c_str());
  Buffer::RawSlice first_slice{const_cast<void*>(void_pointer), first.length()};

  auto send_rc = client_socket->ioHandle().sendto(first_slice, 0, *server_socket->localAddress());
  ASSERT_EQ(send_rc.rc_, first.length());

  EXPECT_CALL(listener_callbacks, onData_(_)).Times(0);

  EXPECT_CALL(listener_callbacks, onWriteReady_(_))
      .Times(1)
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(socket.ioHandle().fd(), server_socket->ioHandle().fd());
      }));

  EXPECT_CALL(listener_callbacks, onReceiveError_(_, _))
      .Times(1)
      .WillOnce(Invoke([&](const UdpListenerCallbacks::ErrorCode& err_code, int err) -> void {
        ASSERT_EQ(err_code, UdpListenerCallbacks::ErrorCode::SyscallError);
        ASSERT_EQ(err, -1);

        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener for sending datagrams to destination.
 *  1. Setup a udp listener and client socket
 *  2. Send the data from the udp listener to the client socket and validate the contents
 */
TEST_P(UdpListenerImplTest, SendData) {
  // Setup server socket
  SocketPtr server_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, true);
  ASSERT_NE(server_socket, nullptr);

  // Setup server callback handler and listener.
  Network::MockUdpListenerCallbacks server_listener_callbacks;
  Network::TestUdpListenerImpl server_listener(dispatcherImpl(), *server_socket,
                                               server_listener_callbacks);

  // Setup client socket.
  SocketPtr client_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, true);
  ASSERT_NE(client_socket, nullptr);

  const std::string payload("hello world");
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  buffer->add(payload);
  UdpSendData send_data{client_socket->localAddress(), *buffer};

  auto send_result = server_listener.send(send_data);

  EXPECT_EQ(send_result.ok(), true);

  // This is trigerred on opening the listener on registering the event
  EXPECT_CALL(server_listener_callbacks, onWriteReady_(_))
      .WillOnce(Invoke([&](const Socket& socket) {
        EXPECT_EQ(socket.ioHandle().fd(), server_socket->ioHandle().fd());
      }));

  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  Buffer::InstancePtr result_buffer(new Buffer::OwnedImpl());
  const uint64_t bytes_to_read = 11;
  uint64_t bytes_read = 0;
  int retry = 0;

  do {
    Api::IoCallUint64Result result =
        result_buffer->read(client_socket->ioHandle(), bytes_to_read - bytes_read);

    if (result.ok()) {
      bytes_read += result.rc_;
    } else if (retry == 10 || result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
      break;
    }

    if (bytes_read == bytes_to_read) {
      break;
    }

    retry++;
    ::usleep(10000);
  } while (true);

  EXPECT_EQ(result_buffer->toString(), payload);
}

/**
 * The send fails because the server_socket is created with bind=false.
 */
TEST_P(UdpListenerImplTest, SendDataError) {
  // Setup server socket
  SocketPtr server_socket =
      getSocket(Address::SocketType::Datagram, Network::Test::getCanonicalLoopbackAddress(version_),
                nullptr, false /*do not bind the socket so that the send fails*/);
  ASSERT_NE(server_socket, nullptr);

  // Setup server callback handler and listener.
  Network::MockUdpListenerCallbacks server_listener_callbacks;
  Network::TestUdpListenerImpl server_listener(dispatcherImpl(), *server_socket,
                                               server_listener_callbacks);

  const std::string payload("hello world");
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  buffer->add(payload);
  // send data to itself
  UdpSendData send_data{server_socket->localAddress(), *buffer};

  // This is trigerred on opening the listener on registering the event
  EXPECT_CALL(server_listener_callbacks, onWriteReady_(_))
      .WillOnce(Invoke([&](const Socket& socket) {
        EXPECT_EQ(socket.ioHandle().fd(), server_socket->ioHandle().fd());
      }));

  auto send_result = server_listener.send(send_data);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(send_result.ok(), false);
  EXPECT_EQ(send_result.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
  dispatcher_->exit();
}

} // namespace
} // namespace Network
} // namespace Envoy

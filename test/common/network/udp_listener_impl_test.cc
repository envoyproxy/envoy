#include <memory>
#include <string>
#include <vector>

#include "common/network/address_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/udp_listener_impl.h"
#include "common/network/utility.h"

#include "test/common/network/listener_impl_test_base.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Network {
namespace {

class UdpListenerImplTest : public ListenerImplTestBase {
public:
  UdpListenerImplTest()
      : server_socket_(createServerSocket(true)), send_to_addr_(getServerLoopbackAddress()) {
    time_system_.sleep(std::chrono::milliseconds(100));
  }

  void SetUp() override {
    // Set listening socket options.
    server_socket_->addOptions(SocketOptionFactory::buildIpPacketInfoOptions());
    server_socket_->addOptions(SocketOptionFactory::buildRxQueueOverFlowOptions());

    listener_ = std::make_unique<UdpListenerImpl>(
        dispatcherImpl(), *server_socket_, listener_callbacks_, dispatcherImpl().timeSource());
  }

protected:
  Address::Instance* getServerLoopbackAddress() {
    if (version_ == Address::IpVersion::v4) {
      return new Address::Ipv4Instance(Network::Test::getLoopbackAddressString(version_),
                                       server_socket_->localAddress()->ip()->port());
    }
    return new Address::Ipv6Instance(Network::Test::getLoopbackAddressString(version_),
                                     server_socket_->localAddress()->ip()->port());
  }

  SocketPtr createServerSocket(bool bind) {
    return std::make_unique<NetworkListenSocket<NetworkSocketTrait<Address::SocketType::Datagram>>>(
        Network::Test::getAnyAddress(version_), nullptr, bind);
  }

  SocketPtr createClientSocket(bool bind) {
    return std::make_unique<NetworkListenSocket<NetworkSocketTrait<Address::SocketType::Datagram>>>(
        Network::Test::getCanonicalLoopbackAddress(version_), nullptr, bind);
  }

  // Validates receive data, source/destination address and received time.
  void validateRecvCallbackParams(const UdpRecvData& data) {
    ASSERT_NE(data.local_address_, nullptr);

    ASSERT_NE(data.peer_address_, nullptr);
    ASSERT_NE(data.peer_address_->ip(), nullptr);

    EXPECT_EQ(data.local_address_->asString(), send_to_addr_->asString());

    EXPECT_EQ(data.peer_address_->ip()->addressAsString(),
              client_socket_->localAddress()->ip()->addressAsString());

    EXPECT_EQ(*data.local_address_, *send_to_addr_);
    EXPECT_EQ(time_system_.monotonicTime(), data.receive_time_);
    // Advance time so that next onData() should have different received time.
    time_system_.sleep(std::chrono::milliseconds(100));
  }

  SocketPtr server_socket_;
  SocketPtr client_socket_;
  Address::InstanceConstSharedPtr send_to_addr_;
  MockUdpListenerCallbacks listener_callbacks_;
  std::unique_ptr<UdpListenerImpl> listener_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, UdpListenerImplTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

// Test that socket options are set after the listener is setup.
TEST_P(UdpListenerImplTest, UdpSetListeningSocketOptionsSuccess) {
  MockUdpListenerCallbacks listener_callbacks;
  Network::UdpListenSocket socket(Network::Test::getAnyAddress(version_), nullptr, true);
  std::shared_ptr<MockSocketOption> option = std::make_shared<MockSocketOption>();
  socket.addOption(option);
  EXPECT_CALL(*option, setOption(_, envoy::api::v2::core::SocketOption::STATE_BOUND))
      .WillOnce(Return(true));
  UdpListenerImpl listener(dispatcherImpl(), socket, listener_callbacks,
                           dispatcherImpl().timeSource());

#ifdef SO_RXQ_OVFL
  // Verify that overflow detection is enabled.
  int get_overflow = 0;
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  socklen_t int_size = static_cast<socklen_t>(sizeof(get_overflow));
  const Api::SysCallIntResult result = os_syscalls.getsockopt(
      server_socket_->ioHandle().fd(), SOL_SOCKET, SO_RXQ_OVFL, &get_overflow, &int_size);
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(1, get_overflow);
#endif
}

/**
 * Tests UDP listener for actual destination and data.
 */
TEST_P(UdpListenerImplTest, UseActualDstUdp) {
  // Setup client socket.
  client_socket_ = createClientSocket(false);

  // We send 2 packets
  const std::string first("first");
  const void* void_pointer = static_cast<const void*>(first.c_str());
  Buffer::RawSlice first_slice{const_cast<void*>(void_pointer), first.length()};
  const std::string second("second");
  void_pointer = static_cast<const void*>(second.c_str());
  Buffer::RawSlice second_slice{const_cast<void*>(void_pointer), second.length()};

  auto send_rc = client_socket_->ioHandle().sendto(first_slice, 0, *send_to_addr_);
  ASSERT_EQ(send_rc.rc_, first.length());

  send_rc = client_socket_->ioHandle().sendto(second_slice, 0, *send_to_addr_);
  ASSERT_EQ(send_rc.rc_, second.length());

  EXPECT_CALL(listener_callbacks_, onData_(_))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data);

        EXPECT_EQ(data.buffer_->toString(), first);
      }))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data);

        EXPECT_EQ(data.buffer_->toString(), second);

        dispatcher_->exit();
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady_(_))
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(socket.ioHandle().fd(), server_socket_->ioHandle().fd());
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener for read and write callbacks with actual data.
 */
TEST_P(UdpListenerImplTest, UdpEcho) {
  // Setup client socket.
  client_socket_ = createClientSocket(false);

  // We send 2 packets and expect it to echo.
  const std::string first("first");
  const void* void_pointer = static_cast<const void*>(first.c_str());
  Buffer::RawSlice first_slice{const_cast<void*>(void_pointer), first.length()};
  const std::string second("second");
  void_pointer = static_cast<const void*>(second.c_str());
  Buffer::RawSlice second_slice{const_cast<void*>(void_pointer), second.length()};

  auto send_rc = client_socket_->ioHandle().sendto(first_slice, 0, *send_to_addr_);
  ASSERT_EQ(send_rc.rc_, first.length());

  send_rc = client_socket_->ioHandle().sendto(second_slice, 0, *send_to_addr_);
  ASSERT_EQ(send_rc.rc_, second.length());

  // For unit test purposes, we assume that the data was received in order.
  Address::InstanceConstSharedPtr test_peer_address;

  std::vector<std::string> server_received_data;

  EXPECT_CALL(listener_callbacks_, onData_(_))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data);

        test_peer_address = data.peer_address_;

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, first);

        server_received_data.push_back(data_str);
      }))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data);

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, second);

        server_received_data.push_back(data_str);
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady_(_)).WillOnce(Invoke([&](const Socket& socket) {
    EXPECT_EQ(socket.ioHandle().fd(), server_socket_->ioHandle().fd());
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
    dispatcher_->exit();
  }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener's `enable` and `disable` APIs.
 */
TEST_P(UdpListenerImplTest, UdpListenerEnableDisable) {
  auto const* server_ip = server_socket_->localAddress()->ip();
  ASSERT_NE(server_ip, nullptr);

  // Setup client socket.
  client_socket_ = createClientSocket(false);

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

  listener_->disable();

  auto send_rc = client_socket_->ioHandle().sendto(first_slice, 0, *send_to_addr_);
  ASSERT_EQ(send_rc.rc_, first.length());

  send_rc = client_socket_->ioHandle().sendto(second_slice, 0, *send_to_addr_);
  ASSERT_EQ(send_rc.rc_, second.length());

  EXPECT_CALL(listener_callbacks_, onData_(_)).Times(0);

  EXPECT_CALL(listener_callbacks_, onWriteReady_(_)).Times(0);

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  listener_->enable();

  EXPECT_CALL(listener_callbacks_, onData_(_))
      .Times(2)
      .WillOnce(Return())
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data);

        EXPECT_EQ(data.buffer_->toString(), second);

        dispatcher_->exit();
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady_(_))
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(socket.ioHandle().fd(), server_socket_->ioHandle().fd());
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener's error callback.
 */
TEST_P(UdpListenerImplTest, UdpListenerRecvMsgError) {
  auto const* server_ip = server_socket_->localAddress()->ip();
  ASSERT_NE(server_ip, nullptr);

  client_socket_ = createClientSocket(false);

  // When the `receive` system call returns an error, we expect the `onReceiveError`
  // callback callwed with `SyscallError` parameter.
  const std::string first("first");
  const void* void_pointer = static_cast<const void*>(first.c_str());
  Buffer::RawSlice first_slice{const_cast<void*>(void_pointer), first.length()};

  auto send_rc = client_socket_->ioHandle().sendto(first_slice, 0, *send_to_addr_);
  ASSERT_EQ(send_rc.rc_, first.length());

  EXPECT_CALL(listener_callbacks_, onData_(_)).Times(0);

  EXPECT_CALL(listener_callbacks_, onWriteReady_(_))
      .Times(1)
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(socket.ioHandle().fd(), server_socket_->ioHandle().fd());
      }));

  EXPECT_CALL(listener_callbacks_, onReceiveError_(_, _))
      .Times(1)
      .WillOnce(Invoke([&](const UdpListenerCallbacks::ErrorCode& err_code,
                           Api::IoError::IoErrorCode err) -> void {
        ASSERT_EQ(UdpListenerCallbacks::ErrorCode::SyscallError, err_code);
        ASSERT_EQ(Api::IoError::IoErrorCode::NoSupport, err);

        dispatcher_->exit();
      }));
  // Inject mocked OsSysCalls implementation to mock a read failure.
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{-1, ENOTSUP}));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener for sending datagrams to destination.
 *  1. Setup a udp listener and client socket
 *  2. Send the data from the udp listener to the client socket and validate the contents
 */
TEST_P(UdpListenerImplTest, SendData) {
  // Setup client socket.
  client_socket_ = createClientSocket(true);
  ASSERT_NE(client_socket_, nullptr);

  const std::string payload("hello world");
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  buffer->add(payload);
  UdpSendData send_data{client_socket_->localAddress(), *buffer};

  auto send_result = listener_->send(send_data);

  EXPECT_EQ(send_result.ok(), true);

  // This is trigerred on opening the listener on registering the event
  EXPECT_CALL(listener_callbacks_, onWriteReady_(_)).WillOnce(Invoke([&](const Socket& socket) {
    EXPECT_EQ(socket.ioHandle().fd(), server_socket_->ioHandle().fd());
  }));

  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  Buffer::InstancePtr result_buffer(new Buffer::OwnedImpl());
  const uint64_t bytes_to_read = 11;
  uint64_t bytes_read = 0;
  int retry = 0;

  do {
    Api::IoCallUint64Result result =
        result_buffer->read(client_socket_->ioHandle(), bytes_to_read - bytes_read);

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
  const std::string payload("hello world");
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  buffer->add(payload);
  // send data to itself
  UdpSendData send_data{server_socket_->localAddress(), *buffer};

  // This is trigerred on opening the listener on registering the event
  EXPECT_CALL(listener_callbacks_, onWriteReady_(_)).WillOnce(Invoke([&](const Socket& socket) {
    EXPECT_EQ(socket.ioHandle().fd(), server_socket_->ioHandle().fd());
  }));
  // Inject mocked OsSysCalls implementation to mock a write failure.
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{-1, ENOTSUP}));
  auto send_result = listener_->send(send_data);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(send_result.ok());
  EXPECT_EQ(send_result.err_->getErrorCode(), Api::IoError::IoErrorCode::NoSupport);
  dispatcher_->exit();
}

} // namespace
} // namespace Network
} // namespace Envoy

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "envoy/api/os_sys_calls.h"
#include "envoy/config/core/v3/base.pb.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/network/address_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/socket_option_impl.h"
#include "common/network/udp_listener_impl.h"
#include "common/network/udp_packet_writer_handler_impl.h"
#include "common/network/utility.h"

#include "test/common/network/udp_listener_impl_test_base.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Network {
namespace {

// UdpGro is only supported on Linux versions >= 5.0. Also, the
// underlying platform only performs the payload concatenation when
// packets are sent from a network namespace different to that of
// the client. Currently, the testing framework does not support
// this behavior.
// This helper allows to intercept the supportsUdpGro syscall and
// toggle the gro behavior as per individual test requirements.
class MockSupportsUdpGro : public Api::OsSysCallsImpl {
public:
  MOCK_METHOD(bool, supportsUdpGro, (), (const));
};

class UdpListenerImplTest : public UdpListenerImplTestBase {
public:
  void SetUp() override {
    ON_CALL(udp_gro_syscall_, supportsUdpGro()).WillByDefault(Return(false));

    // Set listening socket options.
    server_socket_->addOptions(SocketOptionFactory::buildIpPacketInfoOptions());
    server_socket_->addOptions(SocketOptionFactory::buildRxQueueOverFlowOptions());
    if (Api::OsSysCallsSingleton::get().supportsUdpGro()) {
      server_socket_->addOptions(SocketOptionFactory::buildUdpGroOptions());
    }
    listener_ = std::make_unique<UdpListenerImpl>(
        dispatcherImpl(), server_socket_, listener_callbacks_, dispatcherImpl().timeSource());
    udp_packet_writer_ = std::make_unique<Network::UdpDefaultWriter>(server_socket_->ioHandle());
    ON_CALL(listener_callbacks_, udpPacketWriter()).WillByDefault(ReturnRef(*udp_packet_writer_));
  }

  NiceMock<MockSupportsUdpGro> udp_gro_syscall_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&udp_gro_syscall_};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, UdpListenerImplTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test that socket options are set after the listener is setup.
TEST_P(UdpListenerImplTest, UdpSetListeningSocketOptionsSuccess) {
  MockUdpListenerCallbacks listener_callbacks;
  auto socket = std::make_shared<Network::UdpListenSocket>(Network::Test::getAnyAddress(version_),
                                                           nullptr, true);
  std::shared_ptr<MockSocketOption> option = std::make_shared<MockSocketOption>();
  socket->addOption(option);
  EXPECT_CALL(*option, setOption(_, envoy::config::core::v3::SocketOption::STATE_BOUND))
      .WillOnce(Return(true));
  UdpListenerImpl listener(dispatcherImpl(), socket, listener_callbacks,
                           dispatcherImpl().timeSource());

#ifdef SO_RXQ_OVFL
  // Verify that overflow detection is enabled.
  int get_overflow = 0;
  socklen_t int_size = static_cast<socklen_t>(sizeof(get_overflow));
  const Api::SysCallIntResult result =
      server_socket_->getSocketOption(SOL_SOCKET, SO_RXQ_OVFL, &get_overflow, &int_size);
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(1, get_overflow);
#endif
}

/**
 * Tests UDP listener for actual destination and data.
 */
TEST_P(UdpListenerImplTest, UseActualDstUdp) {
  // We send 2 packets
  const std::string first("first");
  client_.write(first, *send_to_addr_);
  const std::string second("second");
  client_.write(second, *send_to_addr_);

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onData(_))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data, Api::OsSysCallsSingleton::get().supportsMmsg() ? 16u : 1u);
        EXPECT_EQ(data.buffer_->toString(), first);
      }))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data, Api::OsSysCallsSingleton::get().supportsMmsg() ? 16u : 1u);
        EXPECT_EQ(data.buffer_->toString(), second);

        dispatcher_->exit();
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady(_))
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(&socket.ioHandle(), &server_socket_->ioHandle());
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener for read and write callbacks with actual data.
 */
TEST_P(UdpListenerImplTest, UdpEcho) {
  // We send 17 packets and expect it to echo.
  absl::FixedArray<std::string> client_data({"first", "second", "third", "forth", "fifth", "sixth",
                                             "seventh", "eighth", "ninth", "tenth", "eleventh",
                                             "twelveth", "thirteenth", "fourteenth", "fifteenth",
                                             "sixteenth", "seventeenth"});
  for (const auto& i : client_data) {
    client_.write(i, *send_to_addr_);
  }

  // For unit test purposes, we assume that the data was received in order.
  Address::InstanceConstSharedPtr test_peer_address;

  std::vector<std::string> server_received_data;

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onData(_))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data, Api::OsSysCallsSingleton::get().supportsMmsg() ? 16u : 1u);

        test_peer_address = data.addresses_.peer_;

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, client_data[num_packets_received_by_listener_ - 1]);

        server_received_data.push_back(data_str);
      }))
      .WillRepeatedly(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data, Api::OsSysCallsSingleton::get().supportsMmsg() ? 16u : 1u);

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, client_data[num_packets_received_by_listener_ - 1]);

        server_received_data.push_back(data_str);
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady(_)).WillOnce(Invoke([&](const Socket& socket) {
    EXPECT_EQ(&socket.ioHandle(), &server_socket_->ioHandle());
    ASSERT_NE(test_peer_address, nullptr);

    for (const auto& data : server_received_data) {
      const std::string::size_type data_size = data.length() + 1;
      uint64_t total_sent = 0;
      const void* void_data = static_cast<const void*>(data.c_str() + total_sent);
      Buffer::RawSlice slice{const_cast<void*>(void_data), data_size - total_sent};

      Api::IoCallUint64Result send_rc = Api::ioCallUint64ResultNoError();
      do {
        send_rc = Network::Utility::writeToSocket(const_cast<Socket*>(&socket)->ioHandle(), &slice,
                                                  1, nullptr, *test_peer_address);

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

  // We first disable the listener and then send two packets.
  // - With the listener disabled, we expect that none of the callbacks will be
  // called.
  // - When the listener is enabled back, we expect the callbacks to be called
  listener_->disable();
  const std::string first("first");
  client_.write(first, *send_to_addr_);
  const std::string second("second");
  client_.write(second, *send_to_addr_);

  EXPECT_CALL(listener_callbacks_, onReadReady()).Times(0);
  EXPECT_CALL(listener_callbacks_, onData(_)).Times(0);

  EXPECT_CALL(listener_callbacks_, onWriteReady(_)).Times(0);

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  listener_->enable();

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onData(_))
      .Times(2)
      .WillOnce(Return())
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data, Api::OsSysCallsSingleton::get().supportsMmsg() ? 16u : 1u);

        EXPECT_EQ(data.buffer_->toString(), second);

        dispatcher_->exit();
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady(_))
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(&socket.ioHandle(), &server_socket_->ioHandle());
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener's error callback.
 */
TEST_P(UdpListenerImplTest, UdpListenerRecvMsgError) {
  auto const* server_ip = server_socket_->localAddress()->ip();
  ASSERT_NE(server_ip, nullptr);

  // When the `receive` system call returns an error, we expect the `onReceiveError`
  // callback called with `SyscallError` parameter.
  const std::string first("first");
  client_.write(first, *send_to_addr_);

  EXPECT_CALL(listener_callbacks_, onData(_)).Times(0);

  EXPECT_CALL(listener_callbacks_, onWriteReady(_)).WillOnce(Invoke([&](const Socket& socket) {
    EXPECT_EQ(&socket.ioHandle(), &server_socket_->ioHandle());
  }));

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onReceiveError(_))
      .WillOnce(Invoke([&](Api::IoError::IoErrorCode err) -> void {
        ASSERT_EQ(Api::IoError::IoErrorCode::NoSupport, err);

        dispatcher_->exit();
      }));
  // Inject mocked OsSysCalls implementation to mock a read failure.
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, supportsUdpGro());
  EXPECT_CALL(os_sys_calls, supportsMmsg());
  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_NOT_SUP}));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener for sending datagrams to destination.
 *  1. Setup a udp listener and client socket
 *  2. Send the data from the udp listener to the client socket and validate the contents and source
 * address.
 */
TEST_P(UdpListenerImplTest, SendData) {
  EXPECT_FALSE(udp_packet_writer_->isBatchMode());
  const std::string payload("hello world");
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  buffer->add(payload);

  Address::InstanceConstSharedPtr send_from_addr = getNonDefaultSourceAddress();

  UdpSendData send_data{send_from_addr->ip(), *client_.localAddress(), *buffer};

  auto send_result = listener_->send(send_data);

  EXPECT_TRUE(send_result.ok()) << "send() failed : " << send_result.err_->getErrorDetails();

  const uint64_t bytes_to_read = payload.length();
  UdpRecvData data;
  client_.recv(data);
  EXPECT_EQ(bytes_to_read, data.buffer_->length());
  EXPECT_EQ(send_from_addr->asString(), data.addresses_.peer_->asString());
  EXPECT_EQ(data.buffer_->toString(), payload);

  // Verify External Flush is a No-op
  auto flush_result = udp_packet_writer_->flush();
  EXPECT_TRUE(flush_result.ok());
  EXPECT_EQ(0, flush_result.rc_);
}

/**
 * The send fails because the server_socket is created with bind=false.
 */
TEST_P(UdpListenerImplTest, SendDataError) {
  const std::string payload("hello world");
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  buffer->add(payload);
  // send data to itself
  UdpSendData send_data{send_to_addr_->ip(), *server_socket_->localAddress(), *buffer};

  // Inject mocked OsSysCalls implementation to mock a write failure.
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_AGAIN}));
  auto send_result = listener_->send(send_data);
  EXPECT_FALSE(send_result.ok());
  EXPECT_EQ(send_result.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
  // Failed write shouldn't drain the data.
  EXPECT_EQ(payload.length(), buffer->length());
  // Verify the writer is set to blocked
  EXPECT_TRUE(udp_packet_writer_->isWriteBlocked());

  // Reset write_blocked status
  udp_packet_writer_->setWritable();
  EXPECT_FALSE(udp_packet_writer_->isWriteBlocked());

  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_NOT_SUP}));
  send_result = listener_->send(send_data);
  EXPECT_FALSE(send_result.ok());
  EXPECT_EQ(send_result.err_->getErrorCode(), Api::IoError::IoErrorCode::NoSupport);
  // Failed write shouldn't drain the data.
  EXPECT_EQ(payload.length(), buffer->length());

  ON_CALL(os_sys_calls, sendmsg(_, _, _))
      .WillByDefault(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_INVAL}));
  // EINVAL should cause RELEASE_ASSERT.
  EXPECT_DEATH(listener_->send(send_data), "Invalid argument passed in");
}

/**
 * Test that multiple stacked packets of the same size are properly segmented
 * when UDP GRO is enabled on the platform.
 */
#ifdef UDP_GRO
TEST_P(UdpListenerImplTest, UdpGroBasic) {
  // We send 4 packets (3 of equal length and 1 as a trail), which are concatenated together by
  // kernel supporting udp gro. Verify the concatenated packet is transformed back into individual
  // packets
  absl::FixedArray<std::string> client_data({"Equal!!!", "Length!!", "Messages", "trail"});

  for (const auto& i : client_data) {
    client_.write(i, *send_to_addr_);
  }

  // The concatenated payload received from kernel supporting udp_gro
  std::string stacked_message = absl::StrJoin(client_data, "");

  // Mock OsSysCalls to mimic kernel behavior for packet concatenation
  // based on udp_gro. supportsUdpGro should return true and recvmsg should
  // return the concatenated payload with the gso_size set appropriately.
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, supportsUdpGro).WillRepeatedly(Return(true));
  EXPECT_CALL(os_sys_calls, supportsMmsg).Times(0);

  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _))
      .WillOnce(Invoke([&](os_fd_t, msghdr* msg, int) {
        // Set msg_name and msg_namelen
        if (client_.localAddress()->ip()->version() == Address::IpVersion::v4) {
          sockaddr_storage ss;
          auto ipv4_addr = reinterpret_cast<sockaddr_in*>(&ss);
          memset(ipv4_addr, 0, sizeof(sockaddr_in));
          ipv4_addr->sin_family = AF_INET;
          ipv4_addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
          ipv4_addr->sin_port = client_.localAddress()->ip()->port();
          msg->msg_namelen = sizeof(sockaddr_in);
          *reinterpret_cast<sockaddr_in*>(msg->msg_name) = *ipv4_addr;
        } else if (client_.localAddress()->ip()->version() == Address::IpVersion::v6) {
          sockaddr_storage ss;
          auto ipv6_addr = reinterpret_cast<sockaddr_in6*>(&ss);
          memset(ipv6_addr, 0, sizeof(sockaddr_in6));
          ipv6_addr->sin6_family = AF_INET6;
          ipv6_addr->sin6_addr = in6addr_loopback;
          ipv6_addr->sin6_port = client_.localAddress()->ip()->port();
          *reinterpret_cast<sockaddr_in6*>(msg->msg_name) = *ipv6_addr;
          msg->msg_namelen = sizeof(sockaddr_in6);
        }

        // Set msg_iovec
        EXPECT_EQ(msg->msg_iovlen, 1);
        memcpy(msg->msg_iov[0].iov_base, stacked_message.data(), stacked_message.length());
        msg->msg_iov[0].iov_len = stacked_message.length();

        // Set control headers
        memset(msg->msg_control, 0, msg->msg_controllen);
        cmsghdr* cmsg = CMSG_FIRSTHDR(msg);
        if (send_to_addr_->ip()->version() == Address::IpVersion::v4) {
          cmsg->cmsg_level = IPPROTO_IP;
#ifndef IP_RECVDSTADDR
          cmsg->cmsg_type = IP_PKTINFO;
          cmsg->cmsg_len = CMSG_LEN(sizeof(in_pktinfo));
          reinterpret_cast<in_pktinfo*>(CMSG_DATA(cmsg))->ipi_addr.s_addr =
              send_to_addr_->ip()->ipv4()->address();
#else
          cmsg.cmsg_type = IP_RECVDSTADDR;
          cmsg->cmsg_len = CMSG_LEN(sizeof(in_addr));
          *reinterpret_cast<in_addr*>(CMSG_DATA(cmsg)) = send_to_addr_->ip()->ipv4()->address();
#endif
        } else if (send_to_addr_->ip()->version() == Address::IpVersion::v6) {
          cmsg->cmsg_len = CMSG_LEN(sizeof(in6_pktinfo));
          cmsg->cmsg_level = IPPROTO_IPV6;
          cmsg->cmsg_type = IPV6_PKTINFO;
          auto pktinfo = reinterpret_cast<in6_pktinfo*>(CMSG_DATA(cmsg));
          pktinfo->ipi6_ifindex = 0;
          *(reinterpret_cast<absl::uint128*>(pktinfo->ipi6_addr.s6_addr)) =
              send_to_addr_->ip()->ipv6()->address();
        }

        // Set gso_size
        cmsg = CMSG_NXTHDR(msg, cmsg);
        cmsg->cmsg_level = SOL_UDP;
        cmsg->cmsg_type = UDP_GRO;
        cmsg->cmsg_len = CMSG_LEN(sizeof(uint16_t));
        const uint16_t gso_size = 8;
        *reinterpret_cast<uint16_t*>(CMSG_DATA(cmsg)) = gso_size;

#ifdef SO_RXQ_OVFL
        // Set SO_RXQ_OVFL
        cmsg = CMSG_NXTHDR(msg, cmsg);
        EXPECT_NE(cmsg, nullptr);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SO_RXQ_OVFL;
        cmsg->cmsg_len = CMSG_LEN(sizeof(uint32_t));
        const uint32_t overflow = 0;
        *reinterpret_cast<uint32_t*>(CMSG_DATA(cmsg)) = overflow;
#endif
        return Api::SysCallSizeResult{static_cast<long>(stacked_message.length()), 0};
      }))
      .WillRepeatedly(Return(Api::SysCallSizeResult{-1, EAGAIN}));

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onData(_))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data, client_data.size());

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, client_data[num_packets_received_by_listener_ - 1]);
      }))
      .WillRepeatedly(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data, client_data.size());

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, client_data[num_packets_received_by_listener_ - 1]);
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady(_)).WillOnce(Invoke([&](const Socket& socket) {
    EXPECT_EQ(&socket.ioHandle(), &server_socket_->ioHandle());
    dispatcher_->exit();
  }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}
#endif

} // namespace
} // namespace Network
} // namespace Envoy

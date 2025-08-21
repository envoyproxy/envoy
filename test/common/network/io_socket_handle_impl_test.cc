#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/listen_socket_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Network {
namespace {

TEST(IoSocketHandleImpl, TestIoSocketError) {
  EXPECT_DEBUG_DEATH(IoSocketError::create(SOCKET_ERROR_AGAIN),
                     ".*assert failure: .* Details: Didn't use getIoSocketEagainError.*");
  EXPECT_EQ(IoSocketError::IoErrorCode::Again,
            IoSocketError::getIoSocketEagainError()->getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_AGAIN),
            IoSocketError::getIoSocketEagainError()->getErrorDetails());

  Api::IoErrorPtr error2 = IoSocketError::create(SOCKET_ERROR_NOT_SUP);
  EXPECT_EQ(IoSocketError::IoErrorCode::NoSupport, error2->getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_NOT_SUP), error2->getErrorDetails());

  Api::IoErrorPtr error3 = IoSocketError::create(SOCKET_ERROR_AF_NO_SUP);
  EXPECT_EQ(IoSocketError::IoErrorCode::AddressFamilyNoSupport, error3->getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_AF_NO_SUP), error3->getErrorDetails());

  Api::IoErrorPtr error4 = IoSocketError::create(SOCKET_ERROR_IN_PROGRESS);
  EXPECT_EQ(IoSocketError::IoErrorCode::InProgress, error4->getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_IN_PROGRESS), error4->getErrorDetails());

  Api::IoErrorPtr error5 = IoSocketError::create(SOCKET_ERROR_PERM);
  EXPECT_EQ(IoSocketError::IoErrorCode::Permission, error5->getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_PERM), error5->getErrorDetails());

  Api::IoErrorPtr error6 = IoSocketError::create(SOCKET_ERROR_MSG_SIZE);
  EXPECT_EQ(IoSocketError::IoErrorCode::MessageTooBig, error6->getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_MSG_SIZE), error6->getErrorDetails());

  Api::IoErrorPtr error7 = IoSocketError::create(SOCKET_ERROR_INTR);
  EXPECT_EQ(IoSocketError::IoErrorCode::Interrupt, error7->getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_INTR), error7->getErrorDetails());

  Api::IoErrorPtr error8 = IoSocketError::create(SOCKET_ERROR_ADDR_NOT_AVAIL);
  EXPECT_EQ(IoSocketError::IoErrorCode::AddressNotAvailable, error8->getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_ADDR_NOT_AVAIL), error8->getErrorDetails());

  Api::IoErrorPtr error9 = IoSocketError::create(SOCKET_ERROR_CONNRESET);
  EXPECT_EQ(IoSocketError::IoErrorCode::ConnectionReset, error9->getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_CONNRESET), error9->getErrorDetails());

  // Random unknown error
  Api::IoErrorPtr error10 = IoSocketError::create(123);
  EXPECT_EQ(IoSocketError::IoErrorCode::UnknownError, error10->getErrorCode());
  EXPECT_EQ(errorDetails(123), error10->getErrorDetails());
}

TEST(IoSocketHandleImpl, LastRoundTripTimeReturnsEmptyOptionalIfGetSocketFails) {
  NiceMock<Envoy::Api::MockOsSysCalls> os_sys_calls;
  auto os_calls =
      std::make_unique<Envoy::TestThreadsafeSingletonInjector<Envoy::Api::OsSysCallsImpl>>(
          &os_sys_calls);

  EXPECT_CALL(os_sys_calls, socketTcpInfo(_, _))
      .WillOnce(Return(Api::SysCallBoolResult{false, -1}));

  IoSocketHandleImpl io_handle;
  EXPECT_THAT(io_handle.lastRoundTripTime(), Eq(absl::optional<std::chrono::microseconds>{}));
}

TEST(IoSocketHandleImpl, LastRoundTripTimeReturnsRttIfSuccessful) {
  NiceMock<Envoy::Api::MockOsSysCalls> os_sys_calls;
  auto rtt = std::chrono::microseconds(35);
  auto os_calls =
      std::make_unique<Envoy::TestThreadsafeSingletonInjector<Envoy::Api::OsSysCallsImpl>>(
          &os_sys_calls);

  EXPECT_CALL(os_sys_calls, socketTcpInfo(_, _))
      .WillOnce(
          Invoke([rtt](os_fd_t /*sockfd*/, Api::EnvoyTcpInfo* tcp_info) -> Api::SysCallBoolResult {
            tcp_info->tcpi_rtt = rtt;
            return {true, 0};
          }));

  IoSocketHandleImpl io_handle;
  EXPECT_THAT(io_handle.lastRoundTripTime(),
              Eq(std::chrono::duration_cast<std::chrono::milliseconds>(rtt)));
}

TEST(IoSocketHandleImpl, InterfaceNameWithPipe) {
  std::string path = TestEnvironment::unixDomainSocketPath("foo.sock");

  const mode_t mode = 0777;
  Address::InstanceConstSharedPtr address = *Address::PipeInstance::create(path, mode);
  SocketImpl socket(Socket::Type::Stream, address, nullptr, {});

  EXPECT_TRUE(socket.ioHandle().isOpen()) << address->asString();

  Api::SysCallIntResult result = socket.bind(address);
  ASSERT_EQ(result.return_value_, 0);

  EXPECT_FALSE(socket.ioHandle().interfaceName().has_value());
}

TEST(IoSocketHandleImpl, ExplicitDoesNotSupportGetifaddrs) {

  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(Address::IpVersion::v4));

  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, supportsGetifaddrs()).WillOnce(Return(false));
  const auto maybe_interface_name = socket->ioHandle().interfaceName();
  EXPECT_FALSE(maybe_interface_name.has_value());
}

TEST(IoSocketHandleImpl, NullptrIfaddrs) {
  auto& os_syscalls_singleton = Api::OsSysCallsSingleton::get();
  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(Address::IpVersion::v4));

  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, supportsGetifaddrs()).WillRepeatedly(Return(true));
  EXPECT_CALL(os_sys_calls, getsockname(_, _, _))
      .WillOnce(
          Invoke([&](os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) -> Api::SysCallIntResult {
            os_syscalls_singleton.getsockname(sockfd, addr, addrlen);
            return {0, 0};
          }));
  EXPECT_CALL(os_sys_calls, getifaddrs(_))
      .WillOnce(
          Invoke([&](Api::InterfaceAddressVector&) -> Api::SysCallIntResult { return {0, 0}; }));

  const auto maybe_interface_name = socket->ioHandle().interfaceName();
  EXPECT_FALSE(maybe_interface_name.has_value());
}

TEST(IoSocketHandleImpl, ErrnoIfaddrs) {
  auto& os_syscalls_singleton = Api::OsSysCallsSingleton::get();
  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(Address::IpVersion::v4));

  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, supportsGetifaddrs()).WillRepeatedly(Return(true));
  EXPECT_CALL(os_sys_calls, getsockname(_, _, _))
      .WillOnce(
          Invoke([&](os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) -> Api::SysCallIntResult {
            os_syscalls_singleton.getsockname(sockfd, addr, addrlen);
            return {0, 0};
          }));
  EXPECT_CALL(os_sys_calls, getifaddrs(_))
      .WillOnce(Invoke([&](Api::InterfaceAddressVector&) -> Api::SysCallIntResult {
        return {/*return_value=*/-1, /*errno=*/19};
      }));

  const auto maybe_interface_name = socket->ioHandle().interfaceName();
  EXPECT_FALSE(maybe_interface_name.has_value());
}

TEST(IoSocketHandleImpl, DroppedUdpDatagramsMsg) {
  NiceMock<Envoy::Api::MockOsSysCalls> os_sys_calls;
  auto os_calls =
      std::make_unique<Envoy::TestThreadsafeSingletonInjector<Envoy::Api::OsSysCallsImpl>>(
          &os_sys_calls);

  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _))
      .WillRepeatedly(Invoke([](int /*fd*/, msghdr* msg_hdr, int /*flags*/) {
        msg_hdr->msg_iovlen = 1;
        msg_hdr->msg_flags = 0;

        sockaddr_in peer_address{};
        peer_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        peer_address.sin_family = AF_INET;
        peer_address.sin_port = htons(12345);
        memcpy(msg_hdr->msg_name, &peer_address, sizeof(peer_address));
        msg_hdr->msg_namelen = sizeof(peer_address);

        struct cmsghdr* cmsg = reinterpret_cast<struct cmsghdr*>(msg_hdr->msg_control);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SO_RXQ_OVFL;
        cmsg->cmsg_len = CMSG_LEN(sizeof(uint32_t));
        uint32_t dropped = 5;
        memcpy(CMSG_DATA(cmsg), &dropped, sizeof(uint32_t));
        msg_hdr->msg_controllen = cmsg->cmsg_len;

        return Api::SysCallSizeResult{1, 0};
      }));

  uint32_t dropped_packets = 0;

  char buffer[128] = {};
  Buffer::RawSlice slice{buffer, sizeof(buffer)};

  Network::IoHandle::RecvMsgOutput output(1, &dropped_packets);

  IoSocketHandleImpl io_handle;
  auto result = io_handle.recvmsg(&slice, 1, 1111, IoHandle::UdpSaveCmsgConfig(), output);
  EXPECT_EQ(result.return_value_, 1);
  EXPECT_EQ(dropped_packets, 5);

  result = io_handle.recvmsg(&slice, 1, 1111, IoHandle::UdpSaveCmsgConfig(), output);
  EXPECT_EQ(result.return_value_, 1);

  // Since the SO_RXQ_OVFL control message represents the number of dropped datagrams since socket
  // creation It's expected to see the same value after the second recvmsg call.
  EXPECT_EQ(dropped_packets, 5);
}

TEST(IoSocketHandleImpl, DroppedUdpDatagramsMmsg) {
  NiceMock<Envoy::Api::MockOsSysCalls> os_sys_calls;
  auto os_calls =
      std::make_unique<Envoy::TestThreadsafeSingletonInjector<Envoy::Api::OsSysCallsImpl>>(
          &os_sys_calls);

  EXPECT_CALL(os_sys_calls, recvmmsg(_, _, _, _, _))
      .WillRepeatedly(Invoke([](int /*fd*/, mmsghdr* mmsg_hdr, unsigned int /*num*/, int /*flags*/,
                                timespec* /*tp*/) {
        mmsg_hdr[0].msg_len = 100;
        mmsg_hdr[0].msg_hdr.msg_flags = 0;

        sockaddr_in peer_address{};
        peer_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        peer_address.sin_family = AF_INET;
        peer_address.sin_port = htons(12345);
        memcpy(mmsg_hdr[0].msg_hdr.msg_name, &peer_address, sizeof(peer_address));
        mmsg_hdr[0].msg_hdr.msg_namelen = sizeof(peer_address);

        struct cmsghdr* cmsg = reinterpret_cast<struct cmsghdr*>(mmsg_hdr[0].msg_hdr.msg_control);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SO_RXQ_OVFL;
        cmsg->cmsg_len = CMSG_LEN(sizeof(uint32_t));
        uint32_t dropped = 5;
        memcpy(CMSG_DATA(cmsg), &dropped, sizeof(uint32_t));
        mmsg_hdr[0].msg_hdr.msg_controllen = cmsg->cmsg_len;

        return Api::SysCallIntResult{1, 0};
      }));

  uint32_t dropped_packets = 0;

  char buffer[128] = {};
  Buffer::RawSlice slice{buffer, sizeof(buffer)};
  RawSliceArrays slices = {{slice}};

  Network::IoHandle::RecvMsgOutput output(slices.size(), &dropped_packets);

  IoSocketHandleImpl io_handle;
  auto result = io_handle.recvmmsg(slices, 1111, IoHandle::UdpSaveCmsgConfig(), output);
  EXPECT_EQ(result.return_value_, 1);
  EXPECT_EQ(output.msg_[0].msg_len_, 100);
  EXPECT_EQ(dropped_packets, 5);

  result = io_handle.recvmmsg(slices, 1111, IoHandle::UdpSaveCmsgConfig(), output);
  EXPECT_EQ(result.return_value_, 1);
  EXPECT_EQ(output.msg_[0].msg_len_, 100);

  // Since the SO_RXQ_OVFL control message represents the number of dropped datagrams since socket
  // creation It's expected to see the same value after the second recvmmsg call.
  EXPECT_EQ(dropped_packets, 5);
}

class IoSocketHandleImplTest : public testing::TestWithParam<Network::Address::IpVersion> {};
INSTANTIATE_TEST_SUITE_P(IpVersions, IoSocketHandleImplTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(IoSocketHandleImplTest, InterfaceNameForLoopback) {
  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(GetParam()));

  const auto maybe_interface_name = socket->ioHandle().interfaceName();

  if (Api::OsSysCallsSingleton::get().supportsGetifaddrs()) {
    EXPECT_TRUE(maybe_interface_name.has_value());
    EXPECT_TRUE(absl::StrContains(maybe_interface_name.value(), "lo"));
  } else {
    EXPECT_FALSE(maybe_interface_name.has_value());
  }
}

} // namespace

// This test wrapper is a friend class of IoSocketHandleImpl, so it has access to its private and
// protected methods.
class IoSocketHandleImplTestWrapper {
public:
  void runGetAddressTests(const int cache_size) {
    IoSocketHandleImpl io_handle(-1, false, absl::nullopt, cache_size);

    // New address.
    sockaddr_storage ss = Test::getV6SockAddr("2001:DB8::1234", 51234);
    EXPECT_EQ(io_handle.getOrCreateEnvoyAddressInstance(ss, Test::getSockAddrLen(ss))->asString(),
              "[2001:db8::1234]:51234");
    // New address.
    ss = Test::getV6SockAddr("2001:DB8::1235", 51235);
    EXPECT_EQ(io_handle.getOrCreateEnvoyAddressInstance(ss, Test::getSockAddrLen(ss))->asString(),
              "[2001:db8::1235]:51235");

    // Access the first entry to test moving recently used entries in the cache.
    ss = Test::getV6SockAddr("2001:DB8::1234", 51234);
    EXPECT_EQ(io_handle.getOrCreateEnvoyAddressInstance(ss, Test::getSockAddrLen(ss))->asString(),
              "[2001:db8::1234]:51234");
    // Access the last entry to test moving recently used entries in the cache.
    ss = Test::getV6SockAddr("2001:DB8::1234", 51234);
    EXPECT_EQ(io_handle.getOrCreateEnvoyAddressInstance(ss, Test::getSockAddrLen(ss))->asString(),
              "[2001:db8::1234]:51234");

    // New address.
    ss = Test::getV6SockAddr("2001:DB8::1236", 51236);
    EXPECT_EQ(io_handle.getOrCreateEnvoyAddressInstance(ss, Test::getSockAddrLen(ss))->asString(),
              "[2001:db8::1236]:51236");
    // New address.
    ss = Test::getV6SockAddr("2001:DB8::1237", 51237);
    EXPECT_EQ(io_handle.getOrCreateEnvoyAddressInstance(ss, Test::getSockAddrLen(ss))->asString(),
              "[2001:db8::1237]:51237");

    // Access the second entry to test moving recently used entries in the cache.
    ss = Test::getV6SockAddr("2001:DB8::1234", 51234);
    EXPECT_EQ(io_handle.getOrCreateEnvoyAddressInstance(ss, Test::getSockAddrLen(ss))->asString(),
              "[2001:db8::1234]:51234");

    // New address.
    ss = Test::getV6SockAddr("2001:DB8::1238", 51238);
    EXPECT_EQ(io_handle.getOrCreateEnvoyAddressInstance(ss, Test::getSockAddrLen(ss))->asString(),
              "[2001:db8::1238]:51238");
    // New address.
    ss = Test::getV4SockAddr("213.0.113.101", 50234);
    EXPECT_EQ(io_handle.getOrCreateEnvoyAddressInstance(ss, Test::getSockAddrLen(ss))->asString(),
              "213.0.113.101:50234");
    ss = Test::getV4SockAddr("213.0.113.102", 50235);
    EXPECT_EQ(io_handle.getOrCreateEnvoyAddressInstance(ss, Test::getSockAddrLen(ss))->asString(),
              "213.0.113.102:50235");
    ss = Test::getV4SockAddr("213.0.113.103", 50236);
    EXPECT_EQ(io_handle.getOrCreateEnvoyAddressInstance(ss, Test::getSockAddrLen(ss))->asString(),
              "213.0.113.103:50236");

    // Access a middle entry.
    ss = Test::getV4SockAddr("213.0.113.101", 50234);
    EXPECT_EQ(io_handle.getOrCreateEnvoyAddressInstance(ss, Test::getSockAddrLen(ss))->asString(),
              "213.0.113.101:50234");
  }
};

TEST(IoSocketHandleImpl, GetOrCreateEnvoyAddressInstance) {
  IoSocketHandleImplTestWrapper wrapper;

  // No cache.
  wrapper.runGetAddressTests(/*cache_size=*/0);

  // Cache size 1.
  wrapper.runGetAddressTests(/*cache_size=*/1);

  // Cache size 3.
  wrapper.runGetAddressTests(/*cache_size=*/3);

  // Cache size 4.
  wrapper.runGetAddressTests(/*cache_size=*/4);

  // Cache size 6.
  wrapper.runGetAddressTests(/*cache_size=*/6);

  // Cache size 10.
  wrapper.runGetAddressTests(/*cache_size=*/10);
}

} // namespace Network
} // namespace Envoy

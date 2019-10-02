#include <sys/types.h>

#include <memory>
#include <string>

#include "common/network/address_impl.h"
#include "common/network/io_socket_error_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_packet_writer.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Quic {

class EnvoyQuicWriterTest : public ::testing::Test {
public:
  EnvoyQuicWriterTest() : envoy_quic_writer_(socket_) {
    self_address_.FromString("::");
    quic::QuicIpAddress peer_ip;
    peer_ip.FromString("::1");
    peer_address_ = quic::QuicSocketAddress(peer_ip, /*port=*/123);
    ON_CALL(os_sys_calls_, socket(_, _, _)).WillByDefault(Return(Api::SysCallIntResult{3, 0}));
    ON_CALL(os_sys_calls_, close(3)).WillByDefault(Return(Api::SysCallIntResult{0, 0}));
  }

  void verifySendData(const std::string& content, const msghdr* message) {
    EXPECT_EQ(peer_address_.ToString(), Network::Address::addressFromSockAddr(
                                            *reinterpret_cast<sockaddr_storage*>(message->msg_name),
                                            message->msg_namelen, /*v6only=*/false)
                                            ->asString());
    cmsghdr* const cmsg = CMSG_FIRSTHDR(message);
    auto pktinfo = reinterpret_cast<in6_pktinfo*>(CMSG_DATA(cmsg));
    EXPECT_EQ(0, memcmp(self_address_.GetIPv6().s6_addr, pktinfo->ipi6_addr.s6_addr,
                        sizeof(pktinfo->ipi6_addr.s6_addr)));
    EXPECT_EQ(1, message->msg_iovlen);
    iovec iov = message->msg_iov[0];
    EXPECT_EQ(content, std::string(reinterpret_cast<char*>(iov.iov_base), iov.iov_len));
  }

protected:
  testing::NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
  testing::NiceMock<Network::MockListenSocket> socket_;
  quic::QuicIpAddress self_address_;
  quic::QuicSocketAddress peer_address_;
  EnvoyQuicPacketWriter envoy_quic_writer_;
};

TEST_F(EnvoyQuicWriterTest, AssertOnNonNullPacketOption) {
  std::string str("Hello World!");
  EXPECT_DEBUG_DEATH(envoy_quic_writer_.WritePacket(str.data(), str.length(), self_address_,
                                                    peer_address_,
                                                    reinterpret_cast<quic::PerPacketOptions*>(0x1)),
                     "");
}

TEST_F(EnvoyQuicWriterTest, SendSuccessfully) {
  std::string str("Hello World!");

  EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _))
      .WillOnce(testing::Invoke([this, str](int, const msghdr* message, int) {
        verifySendData(str, message);
        return Api::SysCallSizeResult{static_cast<ssize_t>(str.length()), 0};
      }));
  quic::WriteResult result = envoy_quic_writer_.WritePacket(str.data(), str.length(), self_address_,
                                                            peer_address_, nullptr);
  EXPECT_EQ(quic::WRITE_STATUS_OK, result.status);
  EXPECT_EQ(str.length(), result.bytes_written);
  EXPECT_FALSE(envoy_quic_writer_.IsWriteBlocked());
}

TEST_F(EnvoyQuicWriterTest, SendBlocked) {
  std::string str("Hello World!");
  EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _))
      .WillOnce(testing::Invoke([this, str](int, const msghdr* message, int) {
        verifySendData(str, message);
        return Api::SysCallSizeResult{-1, EAGAIN};
      }));
  quic::WriteResult result = envoy_quic_writer_.WritePacket(str.data(), str.length(), self_address_,
                                                            peer_address_, nullptr);
  EXPECT_EQ(quic::WRITE_STATUS_BLOCKED, result.status);
  EXPECT_EQ(static_cast<int>(Api::IoError::IoErrorCode::Again), result.error_code);
  EXPECT_TRUE(envoy_quic_writer_.IsWriteBlocked());
  // Writing while blocked is not allowed.
#ifdef NDEBUG
  EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _))
      .WillOnce(testing::Invoke([this, str](int, const msghdr* message, int) {
        verifySendData(str, message);
        return Api::SysCallSizeResult{-1, EAGAIN};
      }));
#endif
  EXPECT_DEBUG_DEATH(envoy_quic_writer_.WritePacket(str.data(), str.length(), self_address_,
                                                    peer_address_, nullptr),
                     "");
  envoy_quic_writer_.SetWritable();
  EXPECT_FALSE(envoy_quic_writer_.IsWriteBlocked());
}

TEST_F(EnvoyQuicWriterTest, SendFailure) {
  std::string str("Hello World!");
  EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _))
      .WillOnce(testing::Invoke([this, str](int, const msghdr* message, int) {
        verifySendData(str, message);
        return Api::SysCallSizeResult{-1, ENOTSUP};
      }));
  quic::WriteResult result = envoy_quic_writer_.WritePacket(str.data(), str.length(), self_address_,
                                                            peer_address_, nullptr);
  EXPECT_EQ(quic::WRITE_STATUS_ERROR, result.status);
  EXPECT_EQ(static_cast<int>(Api::IoError::IoErrorCode::NoSupport), result.error_code);
  EXPECT_FALSE(envoy_quic_writer_.IsWriteBlocked());
}

TEST_F(EnvoyQuicWriterTest, SendFailureMessageTooBig) {
  std::string str("Hello World!");
  EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _))
      .WillOnce(testing::Invoke([this, str](int, const msghdr* message, int) {
        verifySendData(str, message);
        return Api::SysCallSizeResult{-1, EMSGSIZE};
      }));
  quic::WriteResult result = envoy_quic_writer_.WritePacket(str.data(), str.length(), self_address_,
                                                            peer_address_, nullptr);
  // Currently MessageSize should be propagated through error_code. This test
  // would fail if QUICHE changes to propagate through status in the future.
  EXPECT_EQ(quic::WRITE_STATUS_ERROR, result.status);
  EXPECT_EQ(static_cast<int>(Api::IoError::IoErrorCode::MessageTooBig), result.error_code);
  EXPECT_FALSE(envoy_quic_writer_.IsWriteBlocked());
}

} // namespace Quic
} // namespace Envoy

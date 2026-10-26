#include "source/server/hot_restarting_base.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

using HotRestartMessage = envoy::HotRestartMessage;

class MockHotRestartingBase : public HotRestartingBase {
public:
  MockHotRestartingBase(const std::string& socket_path) : HotRestartingBase(0) {
    main_rpc_stream_.bindDomainSocket(1, "parent", socket_path, 0644);
  }
  bool sendMessage(sockaddr_un& address, const envoy::HotRestartMessage& message) {
    return main_rpc_stream_.sendHotRestartMessage(address, message);
  }
  std::unique_ptr<envoy::HotRestartMessage> receiveBlocking() {
    return main_rpc_stream_.receiveHotRestartMessage(RpcStream::Blocking::Yes);
  }
  bool socketIsNonBlocking() const {
    return (fcntl(main_rpc_stream_.domain_socket_, F_GETFL) & O_NONBLOCK) != 0;
  }
};

class HotRestartingBaseTest : public testing::Test {
public:
  HotRestartingBaseTest() : base_(path_) {}

  const std::string path_{"/tmp/source"};
  MockHotRestartingBase base_;
};

TEST_F(HotRestartingBaseTest, SendMsgRetryFailsAfterRetries) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _))
      .WillRepeatedly(Invoke(
          [&](os_fd_t, const msghdr*, int) { return Api::SysCallSizeResult{0, ECONNREFUSED}; }));

  std::string dst_path = "/tmp/dst";
  sockaddr_un sun;
  sun.sun_family = AF_UNIX;
  StringUtil::strlcpy(&sun.sun_path[1], dst_path.data(), dst_path.size());
  sun.sun_path[0] = '\0';

  HotRestartMessage message;
  message.mutable_request()->mutable_pass_listen_socket()->set_address("tcp://0.0.0.0:80");

  EXPECT_DEATH(base_.sendMessage(sun, message), "");
}

TEST_F(HotRestartingBaseTest, SendMsgRetrySucceedsEventually) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  bool retried = false;
  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _))
      .WillRepeatedly(Invoke([&](os_fd_t, const msghdr*, int) {
        if (!retried) {
          retried = true;
          return Api::SysCallSizeResult{0, ECONNREFUSED};
        }

        return Api::SysCallSizeResult{30, 0};
      }));

  std::string dst_path = "/tmp/dst";
  sockaddr_un sun;
  sun.sun_family = AF_UNIX;
  StringUtil::strlcpy(&sun.sun_path[1], dst_path.data(), dst_path.size());
  sun.sun_path[0] = '\0';

  HotRestartMessage message;
  message.mutable_request()->mutable_pass_listen_socket()->set_address("tcp://0.0.0.0:80");

  base_.sendMessage(sun, message);

  EXPECT_TRUE(retried);
}

// A blocking send that the socket's send timeout cuts short (the peer is not draining its
// socket) fails instead of asserting, and leaves the socket non-blocking.
TEST_F(HotRestartingBaseTest, SendMsgTimeoutFailsWithoutAsserting) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _)).WillOnce(Invoke([&](os_fd_t, const msghdr*, int) {
    return Api::SysCallSizeResult{-1, EAGAIN};
  }));

  std::string dst_path = "/tmp/dst";
  sockaddr_un sun;
  sun.sun_family = AF_UNIX;
  StringUtil::strlcpy(&sun.sun_path[1], dst_path.data(), dst_path.size());
  sun.sun_path[0] = '\0';

  HotRestartMessage request;
  request.mutable_request()->mutable_stats();

  EXPECT_LOG_CONTAINS("error", "hot restart sendmsg() timed out",
                      EXPECT_FALSE(base_.sendMessage(sun, request)));
  EXPECT_TRUE(base_.socketIsNonBlocking());
}

// A blocking receive that the socket's receive timeout cuts short returns no message instead of
// asserting, and leaves the socket non-blocking for the caller to come back later.
TEST_F(HotRestartingBaseTest, BlockingReceiveReturnsNothingOnTimeout) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _)).WillOnce(Invoke([&](os_fd_t, msghdr*, int) {
    return Api::SysCallSizeResult{-1, EAGAIN};
  }));

  EXPECT_EQ(base_.receiveBlocking(), nullptr);
  EXPECT_TRUE(base_.socketIsNonBlocking());
}

// With a send timeout set on the socket, a signal interrupts a blocking sendmsg() with EINTR
// even for handlers installed with SA_RESTART. The send is retried instead of asserting.
TEST_F(HotRestartingBaseTest, SendMsgRetriesWhenInterruptedBySignal) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _))
      .WillOnce(
          Invoke([&](os_fd_t, const msghdr*, int) { return Api::SysCallSizeResult{-1, EINTR}; }))
      .WillOnce(Invoke([&](os_fd_t, const msghdr* msg, int) {
        return Api::SysCallSizeResult{static_cast<ssize_t>(msg->msg_iov[0].iov_len), 0};
      }));

  std::string dst_path = "/tmp/dst";
  sockaddr_un sun;
  sun.sun_family = AF_UNIX;
  StringUtil::strlcpy(&sun.sun_path[1], dst_path.data(), dst_path.size());
  sun.sun_path[0] = '\0';

  HotRestartMessage request;
  request.mutable_request()->mutable_stats();

  EXPECT_TRUE(base_.sendMessage(sun, request));
  EXPECT_TRUE(base_.socketIsNonBlocking());
}

// Likewise, a blocking recvmsg() interrupted by a signal returns no message, as on the receive
// timeout, instead of asserting.
TEST_F(HotRestartingBaseTest, BlockingReceiveReturnsNothingWhenInterruptedBySignal) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _)).WillOnce(Invoke([&](os_fd_t, msghdr*, int) {
    return Api::SysCallSizeResult{-1, EINTR};
  }));

  EXPECT_EQ(base_.receiveBlocking(), nullptr);
  EXPECT_TRUE(base_.socketIsNonBlocking());
}

// A length prefix within sizeof(uint64_t) of UINT64_MAX would wrap the buffer size
// computation to a tiny value. The datagram must be dropped gracefully (no crash, no
// undersized allocation) — see #45872.
TEST_F(HotRestartingBaseTest, OversizedLengthPrefixDropped) {
  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, fds));

  RpcStream stream(0);
  stream.domain_socket_ = fds[0];

  const uint8_t network_len[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8};
  ASSERT_EQ(8, write(fds[1], network_len, 8));

  auto message = stream.receiveHotRestartMessage(RpcStream::Blocking::No);
  EXPECT_EQ(nullptr, message);

  close(fds[1]);
}

// The drop path must reset the stream state: after an oversized datagram is dropped, the
// next legitimate datagram still parses.
TEST_F(HotRestartingBaseTest, OversizedLengthPrefixDroppedThenNormalMessageParses) {
  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, fds));

  RpcStream stream(0);
  stream.domain_socket_ = fds[0];

  const uint8_t oversized[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8};
  ASSERT_EQ(8, write(fds[1], oversized, 8));

  auto dropped = stream.receiveHotRestartMessage(RpcStream::Blocking::No);
  EXPECT_EQ(nullptr, dropped);

  // A well-formed length prefix (0) followed by a zero-length protobuf payload.
  const uint8_t normal[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  ASSERT_EQ(8, write(fds[1], normal, 8));

  auto message = stream.receiveHotRestartMessage(RpcStream::Blocking::No);
  EXPECT_NE(nullptr, message);

  close(fds[1]);
}

// In blocking mode an oversized datagram must be skipped, not end the receive: the
// caller expects a reply, and the receive must restore O_NONBLOCK on its way out.
TEST_F(HotRestartingBaseTest, OversizedLengthPrefixDroppedBlockingReturnsNextMessage) {
  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, fds));

  RpcStream stream(0);
  stream.domain_socket_ = fds[0];

  const uint8_t oversized[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8};
  ASSERT_EQ(8, write(fds[1], oversized, 8));

  // A well-formed length prefix (0) followed by a zero-length protobuf payload.
  const uint8_t normal[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  ASSERT_EQ(8, write(fds[1], normal, 8));

  auto message = stream.receiveHotRestartMessage(RpcStream::Blocking::Yes);
  EXPECT_NE(nullptr, message);

  // The blocking receive must have restored O_NONBLOCK before returning.
  const int flags = fcntl(fds[0], F_GETFL);
  ASSERT_NE(-1, flags);
  EXPECT_TRUE(flags & O_NONBLOCK);

  close(fds[1]);
}

} // namespace
} // namespace Server
} // namespace Envoy

#include "source/server/hot_restarting_base.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
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
  void sendMessage(sockaddr_un& address, const envoy::HotRestartMessage& message) {
    main_rpc_stream_.sendHotRestartMessage(address, message);
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

} // namespace
} // namespace Server
} // namespace Envoy

// A length prefix within sizeof(uint64_t) of UINT64_MAX would wrap the buffer size
// computation to a tiny value. The datagram must be dropped gracefully (no crash, no
// undersized allocation) — see #45872.
TEST_F(HotRestartingBaseTest, OversizedLengthPrefixDropped) {
  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_DGRAM, 0, fds));

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
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_DGRAM, 0, fds));

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

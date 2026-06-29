#include "source/server/hot_restarting_base.h"

#include <endian.h>
#include <sys/socket.h>
#include <unistd.h>

#include <vector>

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

// Tests that receiveHotRestartMessage asserts when the wire length wraps uint64.
TEST_F(HotRestartingBaseTest, OverflowDeathUint64Max) {
  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_DGRAM, 0, fds));

  RpcStream stream(0);
  stream.domain_socket_ = fds[0];

  const uint64_t network_len = htobe64(0xFFFFFFFFFFFFFFFFULL);
  ASSERT_EQ(8, write(fds[1], &network_len, 8));

  EXPECT_DEATH(stream.receiveHotRestartMessage(RpcStream::Blocking::No), "");
  close(fds[1]);
}

// Tests that receiveHotRestartMessage asserts when 0xFFFFFFFFFFFFFFF8 + 8 = 0.
TEST_F(HotRestartingBaseTest, OverflowDeathMaxMinus7) {
  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_DGRAM, 0, fds));

  RpcStream stream(0);
  stream.domain_socket_ = fds[0];

  const uint64_t network_len = htobe64(0xFFFFFFFFFFFFFFF8ULL);
  ASSERT_EQ(8, write(fds[1], &network_len, 8));

  EXPECT_DEATH(stream.receiveHotRestartMessage(RpcStream::Blocking::No), "");
  close(fds[1]);
}

// Tests that a normal small message is received successfully (no assert).
TEST_F(HotRestartingBaseTest, NormalLengthSuccess) {
  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_DGRAM, 0, fds));

  RpcStream stream(0);
  stream.domain_socket_ = fds[0];

  HotRestartMessage msg;
  msg.mutable_request()->mutable_stats();
  std::string serialized = msg.SerializeAsString();
  const uint64_t total_size = sizeof(uint64_t) + serialized.size();

  std::vector<uint8_t> buf(total_size);
  const uint64_t network_len = htobe64(serialized.size());
  memcpy(buf.data(), &network_len, sizeof(uint64_t));
  memcpy(buf.data() + sizeof(uint64_t), serialized.data(), serialized.size());

  ASSERT_EQ(static_cast<ssize_t>(total_size), write(fds[1], buf.data(), buf.size()));

  auto result = stream.receiveHotRestartMessage(RpcStream::Blocking::No);
  EXPECT_NE(result, nullptr);
  close(fds[1]);
}
} // namespace
} // namespace Server
} // namespace Envoy

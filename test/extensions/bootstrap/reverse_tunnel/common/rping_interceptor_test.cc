#include <sys/socket.h>
#include <unistd.h>

#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/rping_interceptor.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class TestRpingInterceptor : public RpingInterceptor {
public:
  explicit TestRpingInterceptor(int fd) : IoSocketHandleImpl(fd) {}

  void onPingMessage() override { ++ping_messages_; }

  uint64_t pingMessages() const { return ping_messages_; }

private:
  uint64_t ping_messages_{0};
};

class RpingInterceptorTest : public testing::Test {
protected:
  std::unique_ptr<TestRpingInterceptor> makeInterceptor(int fd) {
    return std::make_unique<TestRpingInterceptor>(fd);
  }
};

TEST_F(RpingInterceptorTest, FullRpingConsumedAndCallbackInvoked) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));

  Buffer::OwnedImpl buffer;
  const auto result = interceptor->read(buffer, absl::nullopt);

  EXPECT_EQ(result.err_, nullptr);
  EXPECT_EQ(result.return_value_, rping.size());
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(interceptor->pingMessages(), 1);

  close(fds[1]);
}

TEST_F(RpingInterceptorTest, ChoppedRpingCompletesAndDrainsInSingleBuffer) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);

  const std::string prefix = rping.substr(0, 3);
  const std::string suffix = rping.substr(3);

  Buffer::OwnedImpl buffer;

  ASSERT_EQ(write(fds[1], prefix.data(), prefix.size()), static_cast<ssize_t>(prefix.size()));
  const auto first = interceptor->read(buffer, absl::nullopt);
  EXPECT_EQ(first.err_, nullptr);
  EXPECT_EQ(first.return_value_, prefix.size());
  EXPECT_EQ(buffer.toString(), prefix);
  EXPECT_EQ(interceptor->pingMessages(), 0);

  ASSERT_EQ(write(fds[1], suffix.data(), suffix.size()), static_cast<ssize_t>(suffix.size()));
  const auto second = interceptor->read(buffer, absl::nullopt);
  EXPECT_EQ(second.err_, nullptr);
  EXPECT_EQ(second.return_value_, rping.size());
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(interceptor->pingMessages(), 1);

  close(fds[1]);
}

TEST_F(RpingInterceptorTest, PingPlusDataConsumesPingAndReturnsPayload) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  const std::string payload = " value";
  const std::string combined = rping + payload;
  ASSERT_EQ(write(fds[1], combined.data(), combined.size()), static_cast<ssize_t>(combined.size()));

  Buffer::OwnedImpl buffer;
  const auto result = interceptor->read(buffer, absl::nullopt);

  EXPECT_EQ(result.err_, nullptr);
  EXPECT_EQ(result.return_value_, payload.size());
  EXPECT_EQ(buffer.toString(), payload);
  EXPECT_EQ(interceptor->pingMessages(), 1);

  close(fds[1]);
}

TEST_F(RpingInterceptorTest, DataAfterPingIsPassedThrough) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  const std::string data = "GET /";

  ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));
  Buffer::OwnedImpl first_read_buffer;
  const auto first = interceptor->read(first_read_buffer, absl::nullopt);
  EXPECT_EQ(first.err_, nullptr);
  EXPECT_EQ(first.return_value_, rping.size());
  EXPECT_EQ(first_read_buffer.length(), 0);
  EXPECT_EQ(interceptor->pingMessages(), 1);

  ASSERT_EQ(write(fds[1], data.data(), data.size()), static_cast<ssize_t>(data.size()));
  Buffer::OwnedImpl second_read_buffer;
  const auto second = interceptor->read(second_read_buffer, absl::nullopt);
  EXPECT_EQ(second.err_, nullptr);
  EXPECT_EQ(second.return_value_, data.size());
  EXPECT_EQ(second_read_buffer.toString(), data);
  EXPECT_EQ(interceptor->pingMessages(), 1);

  close(fds[1]);
}

TEST_F(RpingInterceptorTest, NonRpingFirstDisablesPingModeThenRpingPassesThrough) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string first_data = "HELLO";
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);

  ASSERT_EQ(write(fds[1], first_data.data(), first_data.size()),
            static_cast<ssize_t>(first_data.size()));
  Buffer::OwnedImpl first_read_buffer;
  const auto first = interceptor->read(first_read_buffer, absl::nullopt);
  EXPECT_EQ(first.err_, nullptr);
  EXPECT_EQ(first.return_value_, first_data.size());
  EXPECT_EQ(first_read_buffer.toString(), first_data);
  EXPECT_EQ(interceptor->pingMessages(), 0);

  ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));
  Buffer::OwnedImpl second_read_buffer;
  const auto second = interceptor->read(second_read_buffer, absl::nullopt);
  EXPECT_EQ(second.err_, nullptr);
  EXPECT_EQ(second.return_value_, rping.size());
  EXPECT_EQ(second_read_buffer.toString(), rping);
  EXPECT_EQ(interceptor->pingMessages(), 0);

  close(fds[1]);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

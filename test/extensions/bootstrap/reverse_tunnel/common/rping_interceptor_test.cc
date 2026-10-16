#include <fcntl.h>
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

  using RpingInterceptor::seedRetainedPingPrefix;

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

  // The readv() drain loop re-reads after consuming a keepalive, so the read end must be
  // non-blocking (as production sockets are) for the loop to terminate on EAGAIN.
  void setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    ASSERT_EQ(fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0);
  }

  // Reads through the interceptor's readv() into a caller buffer, mirroring the TLS BIO's
  // single-slice call.
  Api::IoCallUint64Result readvInto(TestRpingInterceptor& interceptor, char* buf, uint64_t cap) {
    Buffer::RawSlice slice;
    slice.mem_ = buf;
    slice.len_ = cap;
    return interceptor.readv(cap, &slice, 1);
  }
};

TEST_F(RpingInterceptorTest, FullRpingConsumedAndCallbackInvoked) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));

  Buffer::OwnedImpl buffer;
  const auto result = interceptor->read(buffer, std::nullopt);

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
  const auto first = interceptor->read(buffer, std::nullopt);
  EXPECT_EQ(first.err_, nullptr);
  EXPECT_EQ(first.return_value_, prefix.size());
  // The incomplete prefix is held internally, not delivered to the caller buffer.
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(interceptor->pingMessages(), 0);

  ASSERT_EQ(write(fds[1], suffix.data(), suffix.size()), static_cast<ssize_t>(suffix.size()));
  const auto second = interceptor->read(buffer, std::nullopt);
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
  const auto result = interceptor->read(buffer, std::nullopt);

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
  const auto first = interceptor->read(first_read_buffer, std::nullopt);
  EXPECT_EQ(first.err_, nullptr);
  EXPECT_EQ(first.return_value_, rping.size());
  EXPECT_EQ(first_read_buffer.length(), 0);
  EXPECT_EQ(interceptor->pingMessages(), 1);

  ASSERT_EQ(write(fds[1], data.data(), data.size()), static_cast<ssize_t>(data.size()));
  Buffer::OwnedImpl second_read_buffer;
  const auto second = interceptor->read(second_read_buffer, std::nullopt);
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
  const auto first = interceptor->read(first_read_buffer, std::nullopt);
  EXPECT_EQ(first.err_, nullptr);
  EXPECT_EQ(first.return_value_, first_data.size());
  EXPECT_EQ(first_read_buffer.toString(), first_data);
  EXPECT_EQ(interceptor->pingMessages(), 0);

  ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));
  Buffer::OwnedImpl second_read_buffer;
  const auto second = interceptor->read(second_read_buffer, std::nullopt);
  EXPECT_EQ(second.err_, nullptr);
  EXPECT_EQ(second.return_value_, rping.size());
  EXPECT_EQ(second_read_buffer.toString(), rping);
  EXPECT_EQ(interceptor->pingMessages(), 0);

  close(fds[1]);
}

// A full RPING read via readv() is consumed and echoed; the caller sees would-block.
TEST_F(RpingInterceptorTest, FullRpingConsumedViaReadv) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  setNonBlocking(fds[0]);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));

  char buf[64];
  const auto result = readvInto(*interceptor, buf, sizeof(buf));

  EXPECT_TRUE(result.wouldBlock());
  EXPECT_EQ(interceptor->pingMessages(), 1);

  close(fds[1]);
}

// A keepalive flood via readv() is bounded by the per-wake echo cap and terminated with a hard
// error rather than echoing an unbounded number of five byte windows on one wake.
TEST_F(RpingInterceptorTest, KeepaliveFloodViaReadvClosesConnection) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  setNonBlocking(fds[0]);

  // Enlarge the receive buffer so the whole flood is readable on a single wake, which is the case
  // the cap must bound.
  const int rcvbuf = 2 * 1024 * 1024;
  ASSERT_EQ(setsockopt(fds[0], SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)), 0);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);

  // The cap is 64 KiB of echoes. Write enough whole `RPING`s to exceed it on a single readv() call.
  std::string flood;
  flood.reserve(13200 * rping.size());
  for (int i = 0; i < 13200; i++) {
    flood += rping;
  }
  ASSERT_EQ(write(fds[1], flood.data(), flood.size()), static_cast<ssize_t>(flood.size()));

  char buf[64];
  const auto result = readvInto(*interceptor, buf, sizeof(buf));

  // The flood terminates with a hard error (not would-block), and the echoes are bounded at the
  // 64 KiB cap (13108 five byte `RPING`s).
  EXPECT_FALSE(result.wouldBlock());
  EXPECT_NE(result.err_, nullptr);
  EXPECT_EQ(interceptor->pingMessages(), 13108);

  close(fds[1]);
}

// A keepalive flood via the raw read() path is bounded by the cumulative echo cap and terminated
// with a hard error, mirroring the readv() path. RawBufferSocket::doRead re-reads until EAGAIN, so
// the cap must span read() calls.
TEST_F(RpingInterceptorTest, KeepaliveFloodViaReadClosesConnection) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  setNonBlocking(fds[0]);

  const int rcvbuf = 2 * 1024 * 1024;
  ASSERT_EQ(setsockopt(fds[0], SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)), 0);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  std::string flood;
  flood.reserve(13200 * rping.size());
  for (int i = 0; i < 13200; i++) {
    flood += rping;
  }
  ASSERT_EQ(write(fds[1], flood.data(), flood.size()), static_cast<ssize_t>(flood.size()));

  // Mirror RawBufferSocket::doRead: re-read until the socket errors or would-block.
  Api::IoCallUint64Result result{0, Api::IoError::none()};
  int reads = 0;
  constexpr int kMaxReads = 1000;
  do {
    Buffer::OwnedImpl buffer;
    result = interceptor->read(buffer, std::nullopt);
    ++reads;
  } while (result.err_ == nullptr && reads < kMaxReads);

  // The flood terminates with a hard error (not would-block), bounded by the 64 KiB echo cap.
  EXPECT_FALSE(result.wouldBlock());
  EXPECT_NE(result.err_, nullptr);
  EXPECT_LT(reads, kMaxReads);
  EXPECT_EQ(interceptor->pingMessages(), 13108);

  close(fds[1]);
}

// A slow, healthy keepalive stream over the connection lifetime is never torn down on the read()
// path: each keepalive arrives in its own read burst (followed by would-block) which resets the
// per-burst echo budget, so the cumulative echo count never trips the flood cap.
TEST_F(RpingInterceptorTest, SlowKeepaliveStreamViaReadStaysOpen) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  setNonBlocking(fds[0]);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);

  // Far more keepalives than the flood cap (13108), each in its own burst, must not close the
  // tunnel.
  constexpr int kKeepalives = 14000;
  for (int i = 0; i < kKeepalives; i++) {
    ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));
    Buffer::OwnedImpl buffer;
    const auto stripped = interceptor->read(buffer, std::nullopt);
    ASSERT_EQ(stripped.err_, nullptr);
    ASSERT_EQ(stripped.return_value_, rping.size());
    ASSERT_EQ(buffer.length(), 0);
    // The next read drains to would-block, the burst boundary that resets the echo budget.
    Buffer::OwnedImpl empty;
    const auto drained = interceptor->read(empty, std::nullopt);
    ASSERT_TRUE(drained.wouldBlock());
  }

  EXPECT_EQ(interceptor->pingMessages(), kKeepalives);
  close(fds[1]);
}

// The same slow keepalive stream is never torn down on the readv() path: the internal drain loop
// reaches would-block after each single keepalive, resetting the per-burst echo budget.
TEST_F(RpingInterceptorTest, SlowKeepaliveStreamViaReadvStaysOpen) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  setNonBlocking(fds[0]);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);

  constexpr int kKeepalives = 14000;
  char buf[64];
  for (int i = 0; i < kKeepalives; i++) {
    ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));
    const auto result = readvInto(*interceptor, buf, sizeof(buf));
    ASSERT_TRUE(result.wouldBlock());
  }

  EXPECT_EQ(interceptor->pingMessages(), kKeepalives);
  close(fds[1]);
}

// A RPING split across two readv() calls completes and is echoed once.
TEST_F(RpingInterceptorTest, ChoppedRpingViaReadv) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  setNonBlocking(fds[0]);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  const std::string prefix = rping.substr(0, 3);
  const std::string suffix = rping.substr(3);

  char buf[64];
  ASSERT_EQ(write(fds[1], prefix.data(), prefix.size()), static_cast<ssize_t>(prefix.size()));
  const auto first = readvInto(*interceptor, buf, sizeof(buf));
  EXPECT_TRUE(first.wouldBlock());
  EXPECT_EQ(interceptor->pingMessages(), 0);

  ASSERT_EQ(write(fds[1], suffix.data(), suffix.size()), static_cast<ssize_t>(suffix.size()));
  const auto second = readvInto(*interceptor, buf, sizeof(buf));
  EXPECT_TRUE(second.wouldBlock());
  EXPECT_EQ(interceptor->pingMessages(), 1);

  close(fds[1]);
}

// RPING immediately followed by application data: the RPING is stripped, the data passes through.
TEST_F(RpingInterceptorTest, PingPlusDataViaReadv) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  setNonBlocking(fds[0]);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  const std::string payload = " value";
  const std::string combined = rping + payload;
  ASSERT_EQ(write(fds[1], combined.data(), combined.size()), static_cast<ssize_t>(combined.size()));

  char buf[64];
  const auto result = readvInto(*interceptor, buf, sizeof(buf));

  EXPECT_EQ(result.err_, nullptr);
  EXPECT_EQ(result.return_value_, payload.size());
  EXPECT_EQ(absl::string_view(buf, result.return_value_), payload);
  EXPECT_EQ(interceptor->pingMessages(), 1);

  close(fds[1]);
}

// Application data after a consumed RPING passes through unchanged.
TEST_F(RpingInterceptorTest, DataAfterPingViaReadv) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  setNonBlocking(fds[0]);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  const std::string data = "GET /";

  char buf[64];
  ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));
  const auto first = readvInto(*interceptor, buf, sizeof(buf));
  EXPECT_TRUE(first.wouldBlock());
  EXPECT_EQ(interceptor->pingMessages(), 1);

  ASSERT_EQ(write(fds[1], data.data(), data.size()), static_cast<ssize_t>(data.size()));
  const auto second = readvInto(*interceptor, buf, sizeof(buf));
  EXPECT_EQ(second.err_, nullptr);
  EXPECT_EQ(second.return_value_, data.size());
  EXPECT_EQ(absl::string_view(buf, second.return_value_), data);
  EXPECT_EQ(interceptor->pingMessages(), 1);

  close(fds[1]);
}

// A non-RPING first read latches echo off; a later RPING then passes through verbatim.
TEST_F(RpingInterceptorTest, NonRpingFirstThenPassthroughViaReadv) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  setNonBlocking(fds[0]);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string first_data = "HELLO";
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);

  char buf[64];
  ASSERT_EQ(write(fds[1], first_data.data(), first_data.size()),
            static_cast<ssize_t>(first_data.size()));
  const auto first = readvInto(*interceptor, buf, sizeof(buf));
  EXPECT_EQ(first.err_, nullptr);
  EXPECT_EQ(first.return_value_, first_data.size());
  EXPECT_EQ(absl::string_view(buf, first.return_value_), first_data);
  EXPECT_EQ(interceptor->pingMessages(), 0);

  ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));
  const auto second = readvInto(*interceptor, buf, sizeof(buf));
  EXPECT_EQ(second.err_, nullptr);
  EXPECT_EQ(second.return_value_, rping.size());
  EXPECT_EQ(absl::string_view(buf, second.return_value_), rping);
  EXPECT_EQ(interceptor->pingMessages(), 0);

  close(fds[1]);
}

// The processing_read_ guard keeps read() from double-processing via the inner readv() dispatch.
TEST_F(RpingInterceptorTest, NoDoubleProcessing) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));

  Buffer::OwnedImpl buffer;
  const auto result = interceptor->read(buffer, std::nullopt);

  EXPECT_EQ(result.err_, nullptr);
  EXPECT_EQ(result.return_value_, rping.size());
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(interceptor->pingMessages(), 1);

  close(fds[1]);
}

// A RPING coalesced with trailing data in one segment must not strand the data behind a
// would-block; the drain loop reads past the consumed RPING and delivers the data.
TEST_F(RpingInterceptorTest, CoalescedRpingPlusDataDrainsViaReadv) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  setNonBlocking(fds[0]);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  const std::string data = "HELLO";
  const std::string combined = rping + data;
  ASSERT_EQ(write(fds[1], combined.data(), combined.size()), static_cast<ssize_t>(combined.size()));

  // Mirror the TLS BIO reading one record-header's worth at a time.
  char buf[5];
  const auto result = readvInto(*interceptor, buf, sizeof(buf));

  EXPECT_EQ(result.err_, nullptr);
  EXPECT_EQ(result.return_value_, data.size());
  EXPECT_EQ(absl::string_view(buf, result.return_value_), data);
  EXPECT_EQ(interceptor->pingMessages(), 1);

  close(fds[1]);
}

// Several keepalives coalesced ahead of application data are all stripped on the read() path.
TEST_F(RpingInterceptorTest, CoalescedRpingsStrippedViaRead) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  const std::string payload = " value";
  const std::string combined = rping + rping + payload;
  ASSERT_EQ(write(fds[1], combined.data(), combined.size()), static_cast<ssize_t>(combined.size()));

  Buffer::OwnedImpl buffer;
  const auto result = interceptor->read(buffer, std::nullopt);

  EXPECT_EQ(result.err_, nullptr);
  EXPECT_EQ(result.return_value_, payload.size());
  EXPECT_EQ(buffer.toString(), payload);
  EXPECT_EQ(interceptor->pingMessages(), 2);

  close(fds[1]);
}

// A prefix seeded at checkout completes with the arriving bytes and the RPING is stripped (read()).
TEST_F(RpingInterceptorTest, SeededPrefixCompletesRpingViaRead) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  interceptor->seedRetainedPingPrefix(rping.substr(0, 3));
  const std::string arriving = rping.substr(3) + " value";
  ASSERT_EQ(write(fds[1], arriving.data(), arriving.size()), static_cast<ssize_t>(arriving.size()));

  Buffer::OwnedImpl buffer;
  const auto result = interceptor->read(buffer, std::nullopt);

  EXPECT_EQ(result.err_, nullptr);
  EXPECT_EQ(buffer.toString(), " value");
  EXPECT_EQ(interceptor->pingMessages(), 1);

  close(fds[1]);
}

// A prefix seeded at checkout completes with the arriving bytes and the RPING is stripped
// (readv()).
TEST_F(RpingInterceptorTest, SeededPrefixCompletesRpingViaReadv) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  setNonBlocking(fds[0]);

  auto interceptor = makeInterceptor(fds[0]);
  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  interceptor->seedRetainedPingPrefix(rping.substr(0, 3));
  const std::string arriving = rping.substr(3) + " value";
  ASSERT_EQ(write(fds[1], arriving.data(), arriving.size()), static_cast<ssize_t>(arriving.size()));

  char buf[64];
  const auto result = readvInto(*interceptor, buf, sizeof(buf));

  EXPECT_EQ(result.err_, nullptr);
  EXPECT_EQ(absl::string_view(buf, result.return_value_), " value");
  EXPECT_EQ(interceptor->pingMessages(), 1);

  close(fds[1]);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

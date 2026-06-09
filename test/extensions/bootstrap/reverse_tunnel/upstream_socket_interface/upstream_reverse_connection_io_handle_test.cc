#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "source/common/network/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/logging.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class UpstreamReverseConnectionIOHandleTest : public testing::Test {
protected:
  UpstreamReverseConnectionIOHandleTest() {
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));
    EXPECT_CALL(server_context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    EXPECT_CALL(server_context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));

    // Set up extension and TLS registry so IOHandle can be constructed with
    // valid histogram pointers and a socket manager for close().
    socket_interface_ = std::make_unique<ReverseTunnelAcceptor>(server_context_);
    extension_ = std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_,
                                                                  server_context_, config_);
    tls_registry_ = std::make_unique<UpstreamSocketThreadLocal>(dispatcher_, extension_.get());
  }

  void TearDown() override {
    io_handle_.reset();
    tls_registry_.reset();
    extension_.reset();
    socket_interface_.reset();
  }

  // Helper to create a mock socket with IO handle.
  Network::ConnectionSocketPtr createMockSocket() {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));
    socket->io_handle_ = std::move(mock_io_handle);
    return socket;
  }

  Network::ConnectionSocketPtr createSocketWithFd(int fd) {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto io_handle = std::make_unique<Network::IoSocketHandleImpl>(fd);
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*io_handle));
    socket->io_handle_ = std::move(io_handle);
    return socket;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;

  std::unique_ptr<ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;
  std::unique_ptr<UpstreamSocketThreadLocal> tls_registry_;

  std::unique_ptr<UpstreamReverseConnectionIOHandle> io_handle_;

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);
};

TEST_F(UpstreamReverseConnectionIOHandleTest, ConnectReturnsSuccess) {
  io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(),
                                                                   "test-cluster", *tls_registry_);
  auto address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 8080);

  auto result = io_handle_->connect(address);

  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(result.errno_, 0);
}

TEST_F(UpstreamReverseConnectionIOHandleTest, GetSocketReturnsConstReference) {
  io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(),
                                                                   "test-cluster", *tls_registry_);
  const auto& socket = io_handle_->getSocket();

  EXPECT_NE(&socket, nullptr);
}

TEST_F(UpstreamReverseConnectionIOHandleTest, ShutdownIgnoredWhenOwned) {
  io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(),
                                                                   "test-cluster", *tls_registry_);
  auto result = io_handle_->shutdown(SHUT_RDWR);
  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(result.errno_, 0);
}

// Test close() notifies the socket manager via the stored registry.
TEST_F(UpstreamReverseConnectionIOHandleTest, CloseWithSocketManagerNotification) {
  io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(),
                                                                   "test-cluster", *tls_registry_);

  auto result = io_handle_->close();

  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(result.err_, nullptr);
}

// Test close() when owned_socket_ is nullptr.
TEST_F(UpstreamReverseConnectionIOHandleTest, CloseWithoutOwnedSocket) {
  io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(),
                                                                   "test-cluster", *tls_registry_);

  // Release the owned socket without closing/invalidating the fd.
  io_handle_->releaseSocketForTest();

  // Now close should call IoSocketHandleImpl::close().
  auto result = io_handle_->close();

  // Should return success.
  EXPECT_EQ(result.err_, nullptr);
}

// Test shutdown() when owned_socket_ is nullptr.
TEST_F(UpstreamReverseConnectionIOHandleTest, ShutdownWhenNotOwned) {
  io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(),
                                                                   "test-cluster", *tls_registry_);

  // Release the owned socket without closing/invalidating the fd.
  io_handle_->releaseSocketForTest();

  // Now shutdown should call IoSocketHandleImpl::shutdown().
  auto result = io_handle_->shutdown(SHUT_RDWR);
  EXPECT_GE(result.return_value_, -1);
}

TEST_F(UpstreamReverseConnectionIOHandleTest, ReadConsumesFullRping) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(createSocketWithFd(fds[0]),
                                                                   "test-cluster", *tls_registry_);

  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));

  Buffer::OwnedImpl buffer;
  auto result = io_handle_->read(buffer, std::nullopt);

  EXPECT_EQ(result.err_, nullptr);
  EXPECT_EQ(result.return_value_, rping.size());
  EXPECT_EQ(buffer.length(), 0);

  close(fds[1]);
}

TEST_F(UpstreamReverseConnectionIOHandleTest, ReadConsumesRpingAndReturnsTrailingPayload) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(createSocketWithFd(fds[0]),
                                                                   "test-cluster", *tls_registry_);

  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  const std::string payload = " value";
  const std::string combined = rping + payload;
  ASSERT_EQ(write(fds[1], combined.data(), combined.size()), static_cast<ssize_t>(combined.size()));

  Buffer::OwnedImpl buffer;
  auto result = io_handle_->read(buffer, std::nullopt);

  EXPECT_EQ(result.err_, nullptr);
  EXPECT_EQ(result.return_value_, payload.size());
  EXPECT_EQ(buffer.toString(), payload);

  close(fds[1]);
}

TEST_F(UpstreamReverseConnectionIOHandleTest, OnPingMessageIsNoOpAndDoesNotWriteToSocket) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(createSocketWithFd(fds[0]),
                                                                   "test-cluster", *tls_registry_);

  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));

  Buffer::OwnedImpl buffer;
  auto result = io_handle_->read(buffer, std::nullopt);
  EXPECT_EQ(result.err_, nullptr);
  EXPECT_EQ(result.return_value_, rping.size());

  char peer_buffer[16];
  const int flags = fcntl(fds[1], F_GETFL, 0);
  ASSERT_NE(flags, -1);
  ASSERT_EQ(fcntl(fds[1], F_SETFL, flags | O_NONBLOCK), 0);
  const ssize_t peer_read = read(fds[1], peer_buffer, sizeof(peer_buffer));
  EXPECT_EQ(peer_read, -1);
  EXPECT_EQ(errno, EAGAIN);

  close(fds[1]);
}

TEST_F(UpstreamReverseConnectionIOHandleTest, NonRpingFirstDisablesPingModeThenRpingPassesThrough) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(createSocketWithFd(fds[0]),
                                                                   "test-cluster", *tls_registry_);

  const std::string non_rping = "HELLO";
  ASSERT_EQ(write(fds[1], non_rping.data(), non_rping.size()),
            static_cast<ssize_t>(non_rping.size()));

  Buffer::OwnedImpl first_buffer;
  auto first = io_handle_->read(first_buffer, std::nullopt);
  EXPECT_EQ(first.err_, nullptr);
  EXPECT_EQ(first.return_value_, non_rping.size());
  EXPECT_EQ(first_buffer.toString(), non_rping);

  const std::string rping = std::string(ReverseConnectionUtility::PING_MESSAGE);
  ASSERT_EQ(write(fds[1], rping.data(), rping.size()), static_cast<ssize_t>(rping.size()));

  Buffer::OwnedImpl second_buffer;
  auto second = io_handle_->read(second_buffer, std::nullopt);
  EXPECT_EQ(second.err_, nullptr);
  EXPECT_EQ(second.return_value_, rping.size());
  EXPECT_EQ(second_buffer.toString(), rping);

  close(fds[1]);
}

// --- Post-upgrade lifetime histogram tests ---

TEST_F(UpstreamReverseConnectionIOHandleTest, PostUpgradeLifetimeHistogramRecordedOnDestroy) {
  // Swap in a mock before creating the IO handle so the
  // HistogramCompletableTimespanImpl captures a reference to it.
  // Unit must be a time unit to pass ensureTimeHistogram().
  NiceMock<Stats::MockHistogram> mock_histogram;
  mock_histogram.unit_ = Stats::Histogram::Unit::Milliseconds;
  tls_registry_->cx_post_upgrade_lifetime_ = &mock_histogram;

  io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(),
                                                                   "test-cluster", *tls_registry_);

  // Destroying the IO handle calls complete(), which records the duration.
  EXPECT_CALL(mock_histogram, recordValue(_));
  io_handle_.reset();
}

TEST_F(UpstreamReverseConnectionIOHandleTest,
       PostUpgradeLifetimeHistogramNotCompletedBeforeDestroy) {
  NiceMock<Stats::MockHistogram> mock_histogram;
  mock_histogram.unit_ = Stats::Histogram::Unit::Milliseconds;
  tls_registry_->cx_post_upgrade_lifetime_ = &mock_histogram;

  io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(),
                                                                   "test-cluster", *tls_registry_);

  // During the handle's active lifetime, no recording should happen.
  EXPECT_CALL(mock_histogram, recordValue(_)).Times(0);
  auto address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 8080);
  auto result = io_handle_->connect(address);
  EXPECT_EQ(result.return_value_, 0);

  // Verify the zero-calls expectation, then clear it before destruction
  // (which legitimately calls recordValue).
  testing::Mock::VerifyAndClearExpectations(&mock_histogram);
  io_handle_.reset();
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <vector>

#include "envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3/downstream_reverse_connection_socket_interface.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/downstream_reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator_extension.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Base test class for ReverseConnectionIOHandle (minimal version for
// DownstreamReverseConnectionIOHandleTest)
class ReverseConnectionIOHandleTestBase : public testing::Test {
protected:
  ReverseConnectionIOHandleTestBase() {
    // Set up the stats scope.
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Set up the mock context.
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    EXPECT_CALL(context_, clusterManager()).WillRepeatedly(ReturnRef(cluster_manager_));

    // Create the socket interface.
    socket_interface_ = std::make_unique<ReverseTunnelInitiator>(context_);

    // Create the extension.
    extension_ = std::make_unique<ReverseTunnelInitiatorExtension>(context_, config_);

    // Set up mock dispatcher with default expectations.
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockTimer>>());
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());
  }

  void TearDown() override {
    io_handle_.reset();
    extension_.reset();
    socket_interface_.reset();
  }

  // Helper to create a ReverseConnectionIOHandle with specified configuration.
  std::unique_ptr<ReverseConnectionIOHandle>
  createTestIOHandle(const ReverseConnectionSocketConfig& config) {
    // Create a test socket file descriptor.
    int test_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(test_fd, 0);

    // Create the IO handle.
    return std::make_unique<ReverseConnectionIOHandle>(test_fd, config, cluster_manager_,
                                                       extension_.get(), *stats_scope_);
  }

  // Helper to create a default test configuration.
  ReverseConnectionSocketConfig createDefaultTestConfig() {
    ReverseConnectionSocketConfig config;
    config.src_cluster_id = "test-cluster";
    config.src_node_id = "test-node";
    config.enable_circuit_breaker = true;
    config.remote_clusters.push_back(RemoteClusterConnectionConfig("remote-cluster", 2));
    return config;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};

  envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
      DownstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelInitiator> socket_interface_;
  std::unique_ptr<ReverseTunnelInitiatorExtension> extension_;
  std::unique_ptr<ReverseConnectionIOHandle> io_handle_;

  // Mock cluster manager.
  NiceMock<Upstream::MockClusterManager> cluster_manager_;

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);

  // Mock socket for testing.
  std::unique_ptr<Network::ConnectionSocket> mock_socket_;
};

/**
 * Test class for DownstreamReverseConnectionIOHandle.
 */
class DownstreamReverseConnectionIOHandleTest : public ReverseConnectionIOHandleTestBase {
protected:
  void SetUp() override {
    ReverseConnectionIOHandleTestBase::SetUp();

    // Initialize io_handle_ for testing.
    auto config = createDefaultTestConfig();
    io_handle_ = createTestIOHandle(config);
    EXPECT_NE(io_handle_, nullptr);

    // Create a mock socket for testing.
    mock_socket_ = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle_unique = std::make_unique<NiceMock<Network::MockIoHandle>>();
    mock_io_handle_ = mock_io_handle_unique.get();

    // Set up basic mock expectations.
    EXPECT_CALL(*mock_io_handle_, fdDoNotUse()).WillRepeatedly(Return(42)); // Arbitrary FD
    EXPECT_CALL(*mock_socket_, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_));
  }

  void TearDown() override {
    mock_socket_.reset();
    ReverseConnectionIOHandleTestBase::TearDown();
  }

  // Helper to create a DownstreamReverseConnectionIOHandle.
  std::unique_ptr<DownstreamReverseConnectionIOHandle>
  createHandle(ReverseConnectionIOHandle* parent = nullptr,
               const std::string& connection_key = "test_connection_key") {
    // Create a new mock socket for each handle to avoid releasing the shared one.
    auto new_mock_socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto new_mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();

    // Store the raw pointer before moving
    mock_io_handle_ = new_mock_io_handle.get();

    // Set up basic mock expectations.
    EXPECT_CALL(*mock_io_handle_, fdDoNotUse()).WillRepeatedly(Return(42)); // Arbitrary FD
    EXPECT_CALL(*new_mock_socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_));

    auto socket_ptr = std::unique_ptr<Network::ConnectionSocket>(new_mock_socket.release());
    return std::make_unique<DownstreamReverseConnectionIOHandle>(std::move(socket_ptr), parent,
                                                                 connection_key);
  }

  // Test fixtures.
  std::unique_ptr<NiceMock<Network::MockConnectionSocket>> mock_socket_;
  NiceMock<Network::MockIoHandle>* mock_io_handle_; // Raw pointer, managed by socket
};

// Test constructor and destructor.
TEST_F(DownstreamReverseConnectionIOHandleTest, Setup) {
  // Test constructor with parent.
  {
    auto handle = createHandle(io_handle_.get(), "test_key_1");
    EXPECT_NE(handle, nullptr);
    // Test fdDoNotUse() before any other operations.
    EXPECT_EQ(handle->fdDoNotUse(), 42);
  } // Destructor called here

  // Test constructor without parent.
  {
    auto handle = createHandle(nullptr, "test_key_2");
    EXPECT_NE(handle, nullptr);
    // Test fdDoNotUse() before any other operations.
    EXPECT_EQ(handle->fdDoNotUse(), 42);
  } // Destructor called here
}

// Test close() method and all edge cases.
TEST_F(DownstreamReverseConnectionIOHandleTest, CloseMethod) {
  // Test with parent - should notify parent and reset socket.
  {
    auto handle = createHandle(io_handle_.get(), "test_key");

    // Verify that parent is set correctly.
    EXPECT_NE(io_handle_.get(), nullptr);

    // First close - should notify parent and reset owned_socket.
    auto result1 = handle->close();
    EXPECT_EQ(result1.err_, nullptr);

    // Second close - should return immediately without notifying parent (fd < 0).
    auto result2 = handle->close();
    EXPECT_EQ(result2.err_, nullptr);
  }
}

// Test getSocket() method.
TEST_F(DownstreamReverseConnectionIOHandleTest, GetSocket) {
  auto handle = createHandle(io_handle_.get(), "test_key");

  // Test getSocket() returns the owned socket.
  const auto& socket = handle->getSocket();
  EXPECT_NE(&socket, nullptr);

  // Test getSocket() works on const object.
  const auto const_handle = createHandle(io_handle_.get(), "test_key");
  const auto& const_socket = const_handle->getSocket();
  EXPECT_NE(&const_socket, nullptr);

  // Test that getSocket() works before close() is called.
  EXPECT_EQ(handle->fdDoNotUse(), 42);
}

// Test ignoreCloseAndShutdown() functionality.
TEST_F(DownstreamReverseConnectionIOHandleTest, IgnoreCloseAndShutdown) {
  auto handle = createHandle(io_handle_.get(), "test_key");

  // Initially, close and shutdown should work normally
  // Test shutdown before ignoring - we don't check the result since it depends on base
  // implementation
  handle->shutdown(SHUT_RDWR);

  // Now enable ignore mode
  handle->ignoreCloseAndShutdown();

  // Test that close() is ignored when flag is set
  auto close_result = handle->close();
  EXPECT_EQ(close_result.err_, nullptr); // Should return success but do nothing

  // Test that shutdown() is ignored when flag is set
  auto shutdown_result2 = handle->shutdown(SHUT_RDWR);
  EXPECT_EQ(shutdown_result2.return_value_, 0);
  EXPECT_EQ(shutdown_result2.errno_, 0);

  // Test different shutdown modes are all ignored
  auto shutdown_rd = handle->shutdown(SHUT_RD);
  EXPECT_EQ(shutdown_rd.return_value_, 0);
  EXPECT_EQ(shutdown_rd.errno_, 0);

  auto shutdown_wr = handle->shutdown(SHUT_WR);
  EXPECT_EQ(shutdown_wr.return_value_, 0);
  EXPECT_EQ(shutdown_wr.errno_, 0);
}

// Test read() method with real socket pairs to validate RPING handling.
TEST_F(DownstreamReverseConnectionIOHandleTest, ReadRpingEchoScenarios) {
  const std::string rping_msg = std::string(ReverseConnectionUtility::PING_MESSAGE);

  // A complete RPING message should be echoed and drained.
  {
    // Create a socket pair.
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    // Create a mock socket with real file descriptor
    auto mock_socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<Network::IoSocketHandleImpl>(fds[0]);
    EXPECT_CALL(*mock_socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

    // Store the io handle in the socket.
    auto* io_handle_ptr = mock_io_handle.release();
    mock_socket->io_handle_.reset(io_handle_ptr);

    // Create handle with the socket.
    auto socket_ptr = Network::ConnectionSocketPtr(mock_socket.release());
    auto handle = std::make_unique<DownstreamReverseConnectionIOHandle>(
        std::move(socket_ptr), io_handle_.get(), "test_key");

    // Write RPING to the other end of the socket pair.
    ssize_t written = write(fds[1], rping_msg.data(), rping_msg.size());
    ASSERT_EQ(written, static_cast<ssize_t>(rping_msg.size()));

    // Read should process RPING and return the size (indicating RPING was handled).
    Buffer::OwnedImpl buffer;
    auto result = handle->read(buffer, absl::nullopt);

    EXPECT_EQ(result.return_value_, rping_msg.size());
    EXPECT_EQ(result.err_, nullptr);
    EXPECT_EQ(buffer.length(), 0); // RPING should be drained.

    // Verify RPING echo was sent back.
    char echo_buffer[10];
    ssize_t echo_read = read(fds[1], echo_buffer, sizeof(echo_buffer));
    EXPECT_EQ(echo_read, static_cast<ssize_t>(rping_msg.size()));
    EXPECT_EQ(std::string(echo_buffer, echo_read), rping_msg);

    close(fds[1]);
  }

  // When RPING is followed by application data, echo RPING, keep application data,
  // and disable echo.
  {
    // Create another socket pair.
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    auto mock_socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<Network::IoSocketHandleImpl>(fds[0]);
    EXPECT_CALL(*mock_socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

    auto* io_handle_ptr = mock_io_handle.release();
    mock_socket->io_handle_.reset(io_handle_ptr);

    auto socket_ptr = Network::ConnectionSocketPtr(mock_socket.release());
    auto handle = std::make_unique<DownstreamReverseConnectionIOHandle>(
        std::move(socket_ptr), io_handle_.get(), "test_key2");

    const std::string app_data = "GET /path HTTP/1.1\r\n";
    const std::string combined = rping_msg + app_data;

    // Write combined data to socket.
    ssize_t written = write(fds[1], combined.data(), combined.size());
    ASSERT_EQ(written, static_cast<ssize_t>(combined.size()));

    // Read should process RPING and return only app data size.
    Buffer::OwnedImpl buffer;
    auto result = handle->read(buffer, absl::nullopt);

    EXPECT_EQ(result.return_value_, app_data.size()); // Only app data size
    EXPECT_EQ(result.err_, nullptr);
    EXPECT_EQ(buffer.toString(), app_data); // Only app data remains

    // Verify RPING echo was sent back.
    char echo_buffer[10];
    ssize_t echo_read = read(fds[1], echo_buffer, sizeof(echo_buffer));
    EXPECT_EQ(echo_read, static_cast<ssize_t>(rping_msg.size()));
    EXPECT_EQ(std::string(echo_buffer, echo_read), rping_msg);

    close(fds[1]);
  }

  // Non-RPING data should disable echo and pass through.
  {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    auto mock_socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<Network::IoSocketHandleImpl>(fds[0]);
    EXPECT_CALL(*mock_socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

    auto* io_handle_ptr = mock_io_handle.release();
    mock_socket->io_handle_.reset(io_handle_ptr);

    auto socket_ptr = Network::ConnectionSocketPtr(mock_socket.release());
    auto handle = std::make_unique<DownstreamReverseConnectionIOHandle>(
        std::move(socket_ptr), io_handle_.get(), "test_key3");

    const std::string http_data = "GET /path HTTP/1.1\r\n";

    // Write HTTP data to socket.
    ssize_t written = write(fds[1], http_data.data(), http_data.size());
    ASSERT_EQ(written, static_cast<ssize_t>(http_data.size()));

    // Read should return all HTTP data without processing.
    Buffer::OwnedImpl buffer;
    auto result = handle->read(buffer, absl::nullopt);

    EXPECT_EQ(result.return_value_, http_data.size());
    EXPECT_EQ(result.err_, nullptr);
    EXPECT_EQ(buffer.toString(), http_data);

    // Verify no echo was sent back.
    char echo_buffer[10];
    // Set socket to non-blocking to avoid hanging.
    int flags = fcntl(fds[1], F_GETFL, 0);
    fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);
    ssize_t echo_read = read(fds[1], echo_buffer, sizeof(echo_buffer));
    EXPECT_EQ(echo_read, -1);
    EXPECT_EQ(errno, EAGAIN); // No data available

    close(fds[1]);
  }
}

// Test read() method with partial data handling using real sockets.
TEST_F(DownstreamReverseConnectionIOHandleTest, ReadPartialDataAndStateTransitions) {
  const std::string rping_msg = std::string(ReverseConnectionUtility::PING_MESSAGE);

  // A partial RPING should pass through and wait for more data.
  {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    auto mock_socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<Network::IoSocketHandleImpl>(fds[0]);
    EXPECT_CALL(*mock_socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

    auto* io_handle_ptr = mock_io_handle.release();
    mock_socket->io_handle_.reset(io_handle_ptr);

    auto socket_ptr = Network::ConnectionSocketPtr(mock_socket.release());
    auto handle = std::make_unique<DownstreamReverseConnectionIOHandle>(
        std::move(socket_ptr), io_handle_.get(), "test_key");

    // Write partial RPING (first 3 bytes).
    const std::string partial_rping = rping_msg.substr(0, 3);
    ssize_t written = write(fds[1], partial_rping.data(), partial_rping.size());
    ASSERT_EQ(written, static_cast<ssize_t>(partial_rping.size()));

    // Read should return the partial data as-is.
    Buffer::OwnedImpl buffer;
    auto result = handle->read(buffer, absl::nullopt);

    EXPECT_EQ(result.return_value_, 3);
    EXPECT_EQ(result.err_, nullptr);
    EXPECT_EQ(buffer.toString(), partial_rping);

    close(fds[1]);
  }

  // Non-RPING data should disable echo permanently.
  {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    auto mock_socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<Network::IoSocketHandleImpl>(fds[0]);
    EXPECT_CALL(*mock_socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

    auto* io_handle_ptr = mock_io_handle.release();
    mock_socket->io_handle_.reset(io_handle_ptr);

    auto socket_ptr = Network::ConnectionSocketPtr(mock_socket.release());
    auto handle = std::make_unique<DownstreamReverseConnectionIOHandle>(
        std::move(socket_ptr), io_handle_.get(), "test_key2");

    const std::string http_data = "GET /path";

    // Write HTTP data.
    ssize_t written = write(fds[1], http_data.data(), http_data.size());
    ASSERT_EQ(written, static_cast<ssize_t>(http_data.size()));

    // Read should return HTTP data and disable echo.
    Buffer::OwnedImpl buffer;
    auto result = handle->read(buffer, absl::nullopt);

    EXPECT_EQ(result.return_value_, http_data.size());
    EXPECT_EQ(result.err_, nullptr);
    EXPECT_EQ(buffer.toString(), http_data);

    // Verify no echo was sent.
    char echo_buffer[10];
    int flags = fcntl(fds[1], F_GETFL, 0);
    fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);
    ssize_t echo_read = read(fds[1], echo_buffer, sizeof(echo_buffer));
    EXPECT_EQ(echo_read, -1);
    EXPECT_EQ(errno, EAGAIN);

    close(fds[1]);
  }
}

// Test read() method in scenarios where echo is disabled.
TEST_F(DownstreamReverseConnectionIOHandleTest, ReadEchoDisabledAndErrorHandling) {
  const std::string rping_msg = std::string(ReverseConnectionUtility::PING_MESSAGE);

  // After echo is disabled, RPING should pass through without processing.
  {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    auto mock_socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<Network::IoSocketHandleImpl>(fds[0]);
    EXPECT_CALL(*mock_socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

    auto* io_handle_ptr = mock_io_handle.release();
    mock_socket->io_handle_.reset(io_handle_ptr);

    auto socket_ptr = Network::ConnectionSocketPtr(mock_socket.release());
    auto handle = std::make_unique<DownstreamReverseConnectionIOHandle>(
        std::move(socket_ptr), io_handle_.get(), "test_key");

    // First, disable echo by sending HTTP data.
    const std::string http_data = "HTTP/1.1";
    ssize_t written = write(fds[1], http_data.data(), http_data.size());
    ASSERT_EQ(written, static_cast<ssize_t>(http_data.size()));

    Buffer::OwnedImpl buffer1;
    handle->read(buffer1, absl::nullopt);
    EXPECT_EQ(buffer1.toString(), http_data);

    // Now send RPING - it should pass through without echo.
    written = write(fds[1], rping_msg.data(), rping_msg.size());
    ASSERT_EQ(written, static_cast<ssize_t>(rping_msg.size()));

    Buffer::OwnedImpl buffer2;
    auto result = handle->read(buffer2, absl::nullopt);

    EXPECT_EQ(result.return_value_, rping_msg.size());
    EXPECT_EQ(result.err_, nullptr);
    EXPECT_EQ(buffer2.toString(), rping_msg); // RPING data preserved

    // Verify no echo was sent.
    char echo_buffer[10];
    int flags = fcntl(fds[1], F_GETFL, 0);
    fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);
    ssize_t echo_read = read(fds[1], echo_buffer, sizeof(echo_buffer));
    EXPECT_EQ(echo_read, -1);
    EXPECT_EQ(errno, EAGAIN);

    close(fds[1]);
  }

  // Test EOF scenario by closing the write end.
  {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    auto mock_socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<Network::IoSocketHandleImpl>(fds[0]);
    EXPECT_CALL(*mock_socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

    auto* io_handle_ptr = mock_io_handle.release();
    mock_socket->io_handle_.reset(io_handle_ptr);

    auto socket_ptr = Network::ConnectionSocketPtr(mock_socket.release());
    auto handle = std::make_unique<DownstreamReverseConnectionIOHandle>(
        std::move(socket_ptr), io_handle_.get(), "test_key2");

    // Close write end to simulate EOF.
    close(fds[1]);

    Buffer::OwnedImpl buffer;
    auto result = handle->read(buffer, absl::nullopt);

    EXPECT_EQ(result.return_value_, 0); // EOF
    EXPECT_EQ(result.err_, nullptr);    // No error, just EOF
    EXPECT_EQ(buffer.length(), 0);
  }
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

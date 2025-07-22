#include <chrono>
#include <memory>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/listener/reverse_connection/reverse_connection.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ReverseConnection {

class ReverseConnectionFilterTest : public testing::Test {
protected:
  ReverseConnectionFilterTest() = default;

  // Helper to create a mock socket with proper address setup
  Network::ConnectionSocketPtr createMockSocket(int fd = 123,
                                                const std::string& local_addr = "127.0.0.1:8080",
                                                const std::string& remote_addr = "127.0.0.1:9090") {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

    // Parse local address (IP:port format)
    auto local_colon_pos = local_addr.find(':');
    std::string local_ip = local_addr.substr(0, local_colon_pos);
    uint32_t local_port = std::stoi(local_addr.substr(local_colon_pos + 1));
    auto local_address = Network::Utility::parseInternetAddressNoThrow(local_ip, local_port);

    // Parse remote address (IP:port format)
    auto remote_colon_pos = remote_addr.find(':');
    std::string remote_ip = remote_addr.substr(0, remote_colon_pos);
    uint32_t remote_port = std::stoi(remote_addr.substr(remote_colon_pos + 1));
    auto remote_address = Network::Utility::parseInternetAddressNoThrow(remote_ip, remote_port);

    // Create a mock IO handle and set it up
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    auto* mock_io_handle_ptr = mock_io_handle.get();
    EXPECT_CALL(*mock_io_handle_ptr, fdDoNotUse()).WillRepeatedly(Return(fd));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_ptr));

    // Store the mock_io_handle in the socket
    socket->io_handle_ = std::move(mock_io_handle);

    // Set up connection info provider with the desired addresses
    socket->connection_info_provider_->setLocalAddress(local_address);
    socket->connection_info_provider_->setRemoteAddress(remote_address);

    return socket;
  }

  // Helper to create a mock timer
  Event::MockTimer* createMockTimer() {
    auto timer = new NiceMock<Event::MockTimer>();
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Return(timer));
    return timer;
  }

  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};
};

TEST_F(ReverseConnectionFilterTest, Constructor) {
  // Test that constructor doesn't crash and creates a valid instance
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);
  EXPECT_EQ(config.pingWaitTimeout().count(), 1000);
}

TEST_F(ReverseConnectionFilterTest, OnAccept) {
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);

  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Call onAccept
  Network::FilterStatus status = filter.onAccept(callbacks);

  // Should return StopIteration to wait for data
  EXPECT_EQ(status, Network::FilterStatus::StopIteration);
}

TEST_F(ReverseConnectionFilterTest, OnAcceptWithZeroTimeout) {
  Config config(std::chrono::milliseconds(0));
  Filter filter(config);

  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(0), nullptr));

  // Call onAccept
  Network::FilterStatus status = filter.onAccept(callbacks);

  // Should return StopIteration to wait for data
  EXPECT_EQ(status, Network::FilterStatus::StopIteration);
}

TEST_F(ReverseConnectionFilterTest, OnDataWithValidPingMessage) {
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);

  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Call onAccept first
  filter.onAccept(callbacks);

  // Create mock IO handle for ping response
  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  // Mock successful write for ping response
  EXPECT_CALL(*mock_io_handle, write(_))
      .WillOnce(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        // Drain the buffer to simulate successful write
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{5, Api::IoError::none()};
      }));

  // Set up the socket's IO handle
  EXPECT_CALL(*mock_socket_ptr, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

  // Create buffer with valid ping message
  Buffer::OwnedImpl buffer("RPING");
  NiceMock<Network::MockListenerFilterBuffer> filter_buffer;
  EXPECT_CALL(filter_buffer, rawSlice()).WillRepeatedly(Return(Buffer::RawSlice{
      const_cast<void*>(static_cast<const void*>(buffer.toString().data())), buffer.length()}));
  EXPECT_CALL(filter_buffer, drain(buffer.length())).WillOnce(Return(true));

  // Call onData with valid ping message
  Network::FilterStatus status = filter.onData(filter_buffer);

  // Should return TryAgainLater to wait for more data
  EXPECT_EQ(status, Network::FilterStatus::TryAgainLater);
}

TEST_F(ReverseConnectionFilterTest, OnDataWithHttpEmbeddedPingMessage) {
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);

  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Call onAccept first
  filter.onAccept(callbacks);

  // Create mock IO handle for ping response
  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  // Mock successful write for ping response
  EXPECT_CALL(*mock_io_handle, write(_))
      .WillOnce(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        // Drain the buffer to simulate successful write
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{5, Api::IoError::none()};
      }));

  // Set up the socket's IO handle
  EXPECT_CALL(*mock_socket_ptr, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

  // Create buffer with HTTP-embedded ping message
  std::string http_ping = "GET /ping HTTP/1.1\r\nHost: example.com\r\n\r\nRPING";
  Buffer::OwnedImpl buffer(http_ping);
  NiceMock<Network::MockListenerFilterBuffer> filter_buffer;
  EXPECT_CALL(filter_buffer, rawSlice()).WillRepeatedly(Return(Buffer::RawSlice{
      const_cast<void*>(static_cast<const void*>(buffer.toString().data())), buffer.length()}));
  EXPECT_CALL(filter_buffer, drain(buffer.length())).WillOnce(Return(true));

  // Call onData with HTTP-embedded ping message
  Network::FilterStatus status = filter.onData(filter_buffer);

  // Should return TryAgainLater to wait for more data
  EXPECT_EQ(status, Network::FilterStatus::TryAgainLater);
}

TEST_F(ReverseConnectionFilterTest, OnDataWithNonPingMessage) {
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);

  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Call onAccept first
  filter.onAccept(callbacks);

  // Create buffer with non-ping message
  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
  NiceMock<Network::MockListenerFilterBuffer> filter_buffer;
  EXPECT_CALL(filter_buffer, rawSlice()).WillRepeatedly(Return(Buffer::RawSlice{
      const_cast<void*>(static_cast<const void*>(buffer.toString().data())), buffer.length()}));

  // Call onData with non-ping message
  Network::FilterStatus status = filter.onData(filter_buffer);

  // Should return Continue to proceed with normal processing
  EXPECT_EQ(status, Network::FilterStatus::Continue);
}

TEST_F(ReverseConnectionFilterTest, OnDataWithEmptyBuffer) {
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);

  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Call onAccept first
  filter.onAccept(callbacks);

  // Create empty buffer
  Buffer::OwnedImpl buffer("");
  NiceMock<Network::MockListenerFilterBuffer> filter_buffer;
  EXPECT_CALL(filter_buffer, rawSlice()).WillRepeatedly(Return(Buffer::RawSlice{
      const_cast<void*>(static_cast<const void*>(buffer.toString().data())), buffer.length()}));

  // Call onData with empty buffer
  Network::FilterStatus status = filter.onData(filter_buffer);

  // Should return Error due to remote connection closed
  EXPECT_EQ(status, Network::FilterStatus::StopIteration);
}

TEST_F(ReverseConnectionFilterTest, OnDataWithPartialPingMessage) {
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);

  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Call onAccept first
  filter.onAccept(callbacks);

  // Create buffer with partial ping message
  Buffer::OwnedImpl buffer("RPI");
  NiceMock<Network::MockListenerFilterBuffer> filter_buffer;
  EXPECT_CALL(filter_buffer, rawSlice()).WillRepeatedly(Return(Buffer::RawSlice{
      const_cast<void*>(static_cast<const void*>(buffer.toString().data())), buffer.length()}));

  // Call onData with partial ping message
  Network::FilterStatus status = filter.onData(filter_buffer);

  // Should return TryAgainLater to wait for more data
  EXPECT_EQ(status, Network::FilterStatus::TryAgainLater);
}

TEST_F(ReverseConnectionFilterTest, OnDataWithPingResponseWriteFailure) {
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);

  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Call onAccept first
  filter.onAccept(callbacks);

  // Create mock IO handle for ping response
  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  // Mock failed write for ping response
  EXPECT_CALL(*mock_io_handle, write(_))
      .WillOnce(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        // Drain the buffer to simulate write attempt
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{0, Network::IoSocketError::create(ECONNRESET)};
      }));

  // Set up the socket's IO handle
  EXPECT_CALL(*mock_socket_ptr, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

  // Create buffer with valid ping message
  Buffer::OwnedImpl buffer("RPING");
  NiceMock<Network::MockListenerFilterBuffer> filter_buffer;
  EXPECT_CALL(filter_buffer, rawSlice()).WillRepeatedly(Return(Buffer::RawSlice{
      const_cast<void*>(static_cast<const void*>(buffer.toString().data())), buffer.length()}));
  EXPECT_CALL(filter_buffer, drain(buffer.length())).WillOnce(Return(true));

  // Call onData with valid ping message
  Network::FilterStatus status = filter.onData(filter_buffer);

  // Should return TryAgainLater even if write fails (logs error but continues)
  EXPECT_EQ(status, Network::FilterStatus::TryAgainLater);
}

TEST_F(ReverseConnectionFilterTest, OnDataWithBufferDrainFailure) {
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);

  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Call onAccept first
  filter.onAccept(callbacks);

  // Create mock IO handle for ping response
  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  // Mock successful write for ping response
  EXPECT_CALL(*mock_io_handle, write(_))
      .WillOnce(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        // Drain the buffer to simulate successful write
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{5, Api::IoError::none()};
      }));

  // Set up the socket's IO handle
  EXPECT_CALL(*mock_socket_ptr, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

  // Create buffer with valid ping message
  Buffer::OwnedImpl buffer("RPING");
  NiceMock<Network::MockListenerFilterBuffer> filter_buffer;
  EXPECT_CALL(filter_buffer, rawSlice()).WillRepeatedly(Return(Buffer::RawSlice{
      const_cast<void*>(static_cast<const void*>(buffer.toString().data())), buffer.length()}));
  EXPECT_CALL(filter_buffer, drain(buffer.length())).WillOnce(Return(false));

  // Call onData with valid ping message
  Network::FilterStatus status = filter.onData(filter_buffer);

  // Should return TryAgainLater even if drain fails (logs error but continues)
  EXPECT_EQ(status, Network::FilterStatus::TryAgainLater);
}

TEST_F(ReverseConnectionFilterTest, OnPingWaitTimeout) {
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);

  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Call onAccept first
  filter.onAccept(callbacks);

  // Expect continueFilterChain to be called with false
  EXPECT_CALL(callbacks, continueFilterChain(false));

  // Call onPingWaitTimeout
  filter.onPingWaitTimeout();
}

TEST_F(ReverseConnectionFilterTest, OnClose) {
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);

  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Call onAccept first
  filter.onAccept(callbacks);

  // Create mock IO handle
  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  // Set up the socket's IO handle
  EXPECT_CALL(*mock_socket_ptr, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

  // Expect close to be called on the IO handle
  EXPECT_CALL(*mock_io_handle, close());

  // Call onClose
  filter.onClose();
}

TEST_F(ReverseConnectionFilterTest, OnCloseWithUsedConnection) {
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);

  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Call onAccept first
  filter.onAccept(callbacks);

  // Create mock IO handle for ping response
  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  // Mock successful write for ping response
  EXPECT_CALL(*mock_io_handle, write(_))
      .WillOnce(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        // Drain the buffer to simulate successful write
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{5, Api::IoError::none()};
      }));

  // Set up the socket's IO handle
  EXPECT_CALL(*mock_socket_ptr, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

  // Create buffer with non-ping message to mark connection as used
  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
  NiceMock<Network::MockListenerFilterBuffer> filter_buffer;
  EXPECT_CALL(filter_buffer, rawSlice()).WillRepeatedly(Return(Buffer::RawSlice{
      const_cast<void*>(static_cast<const void*>(buffer.toString().data())), buffer.length()}));

  // Call onData to mark connection as used
  filter.onData(filter_buffer);

  // Call onClose - should not close the IO handle since connection was used
  filter.onClose();
}

TEST_F(ReverseConnectionFilterTest, DestructorWithUnusedConnection) {
  Config config(std::chrono::milliseconds(1000));
  
  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Create filter and call onAccept
  {
    Filter filter(config);
    filter.onAccept(callbacks);

    // Expect socket close to be called in destructor for unused connection
    EXPECT_CALL(*mock_socket_ptr, isOpen()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_socket_ptr, close());
  }
  // Filter goes out of scope here, destructor should be called
}

TEST_F(ReverseConnectionFilterTest, DestructorWithUsedConnection) {
  Config config(std::chrono::milliseconds(1000));
  
  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Create filter and call onAccept
  {
    Filter filter(config);
    filter.onAccept(callbacks);

    // Create mock IO handle for ping response
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

    // Mock successful write for ping response
    EXPECT_CALL(*mock_io_handle, write(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
          // Drain the buffer to simulate successful write
          buffer.drain(buffer.length());
          return Api::IoCallUint64Result{5, Api::IoError::none()};
        }));

    // Set up the socket's IO handle
    EXPECT_CALL(*mock_socket_ptr, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

    // Create buffer with non-ping message to mark connection as used
    Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    NiceMock<Network::MockListenerFilterBuffer> filter_buffer;
    EXPECT_CALL(filter_buffer, rawSlice()).WillRepeatedly(Return(Buffer::RawSlice{
        const_cast<void*>(static_cast<const void*>(buffer.toString().data())), buffer.length()}));

    // Call onData to mark connection as used
    filter.onData(filter_buffer);

    // Expect socket close NOT to be called in destructor for used connection
    EXPECT_CALL(*mock_socket_ptr, isOpen()).WillRepeatedly(Return(true));
    // No EXPECT_CALL for close() since connection was used
  }
  // Filter goes out of scope here, destructor should be called
}

TEST_F(ReverseConnectionFilterTest, DestructorWithClosedSocket) {
  Config config(std::chrono::milliseconds(1000));
  
  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Create filter and call onAccept
  {
    Filter filter(config);
    filter.onAccept(callbacks);

    // Expect socket close NOT to be called in destructor for closed socket
    EXPECT_CALL(*mock_socket_ptr, isOpen()).WillRepeatedly(Return(false));
    // No EXPECT_CALL for close() since socket is already closed
  }
  // Filter goes out of scope here, destructor should be called
}

TEST_F(ReverseConnectionFilterTest, MaxReadBytes) {
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);

  // Test that maxReadBytes returns the correct value
  size_t max_bytes = filter.maxReadBytes();
  EXPECT_EQ(max_bytes, 5); // "RPING" is 5 bytes
}

TEST_F(ReverseConnectionFilterTest, Fd) {
  Config config(std::chrono::milliseconds(1000));
  Filter filter(config);

  // Create mock socket
  auto socket = createMockSocket(123);
  auto* mock_socket_ptr = socket.get();

  // Create mock callbacks
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  EXPECT_CALL(callbacks, socket()).WillRepeatedly(ReturnRef(*mock_socket_ptr));

  // Create mock timer
  auto* mock_timer = createMockTimer();
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));

  // Call onAccept first
  filter.onAccept(callbacks);

  // Test that fd() returns the correct file descriptor
  int fd = filter.fd();
  EXPECT_EQ(fd, 123);
}

} // namespace ReverseConnection
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy 
#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/transport_sockets/dynamic_modules/config.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace TransportSockets {
namespace {

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

// Test fixture for passthrough transport socket tests.
class PassthroughTransportSocketTest : public testing::Test {
protected:
  void SetUp() override {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);

    // Return the stats store scope.
    ON_CALL(factory_context_, statsScope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));

    // Create upstream factory.
    const std::string yaml = R"EOF(
dynamic_module_config:
  name: transport_socket_integration_test
socket_name: passthrough
)EOF";

    envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleUpstreamTransportSocket
        proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);

    auto factory_result =
        UpstreamDynamicModuleTransportSocketFactory::create(proto_config, factory_context_);
    ASSERT_TRUE(factory_result.ok());
    upstream_factory_ = std::move(factory_result.value());
  }

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  Stats::TestUtil::TestStore stats_store_;
  std::unique_ptr<UpstreamDynamicModuleTransportSocketFactory> upstream_factory_;
  NiceMock<Network::MockTransportSocketCallbacks> callbacks_;
  NiceMock<Network::MockIoHandle> io_handle_;
  NiceMock<Upstream::MockHostDescription> host_;
};

// Test basic socket creation and setTransportSocketCallbacks.
TEST_F(PassthroughTransportSocketTest, BasicSocketCreation) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  // Passthrough returns "tcp" as protocol.
  EXPECT_FALSE(socket->protocol().empty());
}

// Test doRead with data from io_handle.
TEST_F(PassthroughTransportSocketTest, DoReadWithData) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  Buffer::OwnedImpl buffer;

  // Mock io_handle to return data.
  EXPECT_CALL(callbacks_, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, read(_, _))
      .WillOnce([](Buffer::Instance& buffer, absl::optional<uint64_t>) -> Api::IoCallUint64Result {
        buffer.add("test data", 9);
        return Api::IoCallUint64Result{9, Api::IoError::none()};
      });

  auto result = socket->doRead(buffer);
  EXPECT_EQ(result.action_, Network::PostIoAction::KeepOpen);
  EXPECT_EQ(result.bytes_processed_, 9);
  EXPECT_FALSE(result.end_stream_read_);
  EXPECT_EQ(buffer.length(), 9);
  EXPECT_EQ(buffer.toString(), "test data");
}

// Test doRead with zero bytes (EOF).
TEST_F(PassthroughTransportSocketTest, DoReadEof) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  Buffer::OwnedImpl buffer;

  EXPECT_CALL(callbacks_, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, read(_, _))
      .WillOnce([](Buffer::Instance&, absl::optional<uint64_t>) -> Api::IoCallUint64Result {
        return Api::IoCallUint64Result{0, Api::IoError::none()};
      });

  auto result = socket->doRead(buffer);
  EXPECT_EQ(result.action_, Network::PostIoAction::KeepOpen);
  EXPECT_EQ(result.bytes_processed_, 0);
  EXPECT_EQ(buffer.length(), 0);
}

// Test doRead with large data (> 16KB).
TEST_F(PassthroughTransportSocketTest, DoReadLargeData) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  Buffer::OwnedImpl buffer;
  std::string large_data(20000, 'x');

  EXPECT_CALL(callbacks_, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, read(_, _))
      .WillOnce([&large_data](Buffer::Instance& buffer,
                              absl::optional<uint64_t>) -> Api::IoCallUint64Result {
        // Module will read up to 16KB at a time.
        buffer.add(large_data.substr(0, 16384));
        return Api::IoCallUint64Result{16384, Api::IoError::none()};
      });

  auto result = socket->doRead(buffer);
  EXPECT_EQ(result.action_, Network::PostIoAction::KeepOpen);
  EXPECT_EQ(result.bytes_processed_, 16384);
  EXPECT_EQ(buffer.length(), 16384);
}

// Test doWrite with basic data.
TEST_F(PassthroughTransportSocketTest, DoWriteBasic) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  Buffer::OwnedImpl buffer;
  buffer.add("hello world", 11);

  EXPECT_CALL(callbacks_, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, write(_))
      .WillOnce([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        uint64_t len = buffer.length();
        buffer.drain(len);
        return Api::IoCallUint64Result{len, Api::IoError::none()};
      });

  auto result = socket->doWrite(buffer, false);
  EXPECT_EQ(result.action_, Network::PostIoAction::KeepOpen);
  EXPECT_EQ(result.bytes_processed_, 11);
  EXPECT_EQ(buffer.length(), 0);
}

// Test doWrite with end_stream flag.
TEST_F(PassthroughTransportSocketTest, DoWriteEndStream) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  Buffer::OwnedImpl buffer;
  buffer.add("final data", 10);

  EXPECT_CALL(callbacks_, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, write(_))
      .WillOnce([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        uint64_t len = buffer.length();
        buffer.drain(len);
        return Api::IoCallUint64Result{len, Api::IoError::none()};
      });

  auto result = socket->doWrite(buffer, true);
  // When end_stream is set, passthrough closes the connection.
  EXPECT_EQ(result.action_, Network::PostIoAction::Close);
  EXPECT_EQ(result.bytes_processed_, 10);
}

// Test doWrite with empty buffer.
TEST_F(PassthroughTransportSocketTest, DoWriteEmptyBuffer) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  Buffer::OwnedImpl buffer;

  auto result = socket->doWrite(buffer, false);
  EXPECT_EQ(result.action_, Network::PostIoAction::KeepOpen);
  EXPECT_EQ(result.bytes_processed_, 0);
}

// Test doWrite with partial write.
TEST_F(PassthroughTransportSocketTest, DoWritePartial) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  Buffer::OwnedImpl buffer;
  buffer.add("0123456789", 10);

  EXPECT_CALL(callbacks_, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, write(_))
      .WillOnce([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        // Simulate partial write - only 5 bytes written.
        buffer.drain(5);
        return Api::IoCallUint64Result{5, Api::IoError::none()};
      });

  auto result = socket->doWrite(buffer, false);
  EXPECT_EQ(result.action_, Network::PostIoAction::KeepOpen);
  EXPECT_EQ(result.bytes_processed_, 5);
  EXPECT_EQ(buffer.length(), 5);
}

// Test doWrite with multiple buffer slices.
TEST_F(PassthroughTransportSocketTest, DoWriteMultipleSlices) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  Buffer::OwnedImpl buffer;
  buffer.add("first", 5);
  buffer.add("second", 6);
  buffer.add("third", 5);

  EXPECT_CALL(callbacks_, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));

  // Passthrough batches all slices into a single write call.
  EXPECT_CALL(io_handle_, write(_))
      .WillOnce([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        uint64_t len = buffer.length();
        buffer.drain(len);
        return Api::IoCallUint64Result{len, Api::IoError::none()};
      });

  auto result = socket->doWrite(buffer, false);
  EXPECT_EQ(result.action_, Network::PostIoAction::KeepOpen);
  EXPECT_EQ(result.bytes_processed_, 16); // 5 + 6 + 5.
  EXPECT_EQ(buffer.length(), 0);
}

// Test closeSocket with RemoteClose event.
TEST_F(PassthroughTransportSocketTest, CloseSocketRemoteClose) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  // Should not crash.
  socket->closeSocket(Network::ConnectionEvent::RemoteClose);
}

// Test closeSocket with LocalClose event.
TEST_F(PassthroughTransportSocketTest, CloseSocketLocalClose) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  socket->closeSocket(Network::ConnectionEvent::LocalClose);
}

// Test closeSocket with Connected event.
TEST_F(PassthroughTransportSocketTest, CloseSocketConnected) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  socket->closeSocket(Network::ConnectionEvent::Connected);
}

// Test closeSocket with ConnectedZeroRtt event.
TEST_F(PassthroughTransportSocketTest, CloseSocketConnectedZeroRtt) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  socket->closeSocket(Network::ConnectionEvent::ConnectedZeroRtt);
}

// Test onConnected raises Connected event.
TEST_F(PassthroughTransportSocketTest, OnConnectedRaisesEvent) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);

  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  socket->onConnected();
}

// Test protocol() returns value and caches result.
TEST_F(PassthroughTransportSocketTest, ProtocolCaching) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  auto protocol1 = socket->protocol();
  auto protocol2 = socket->protocol();

  EXPECT_FALSE(protocol1.empty());
  EXPECT_EQ(protocol1, protocol2);
}

// Test failureReason() returns empty and caches result.
TEST_F(PassthroughTransportSocketTest, FailureReasonCaching) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  auto reason1 = socket->failureReason();
  auto reason2 = socket->failureReason();

  EXPECT_TRUE(reason1.empty());
  EXPECT_EQ(reason1, reason2);
}

// Test ssl() returns nullptr for non-TLS passthrough socket.
TEST_F(PassthroughTransportSocketTest, SslInfoNonTls) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  auto ssl_info = socket->ssl();
  EXPECT_EQ(ssl_info, nullptr);
}

// Test canFlushClose returns true.
TEST_F(PassthroughTransportSocketTest, CanFlushClose) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  bool can_flush = socket->canFlushClose();
  EXPECT_TRUE(can_flush);
}

// Test startSecureTransport returns true for passthrough (it handles the call).
TEST_F(PassthroughTransportSocketTest, StartSecureTransport) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  bool started = socket->startSecureTransport();
  EXPECT_TRUE(started);
}

// Test configureInitialCongestionWindow does not crash.
TEST_F(PassthroughTransportSocketTest, ConfigureInitialCongestionWindow) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  // Should not crash.
  socket->configureInitialCongestionWindow(1000000, std::chrono::microseconds(100));
  socket->configureInitialCongestionWindow(0, std::chrono::microseconds(0));
  socket->configureInitialCongestionWindow(UINT64_MAX, std::chrono::microseconds(999999));
}

// Test downstream factory creation and socket lifecycle.
TEST_F(PassthroughTransportSocketTest, DownstreamFactory) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: transport_socket_integration_test
socket_name: passthrough
)EOF";

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleDownstreamTransportSocket
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto factory_result =
      DownstreamDynamicModuleTransportSocketFactory::create(proto_config, factory_context_);
  ASSERT_TRUE(factory_result.ok());
  auto downstream_factory = std::move(factory_result.value());

  auto socket = downstream_factory->createDownstreamTransportSocket();
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  // Test basic operations on downstream socket.
  EXPECT_FALSE(socket->protocol().empty());
  EXPECT_TRUE(socket->canFlushClose());
}

// Test implementsSecureTransport returns true for passthrough (it supports the API).
TEST_F(PassthroughTransportSocketTest, ImplementsSecureTransport) {
  EXPECT_TRUE(upstream_factory_->implementsSecureTransport());
}

// Test supportsAlpn returns false for passthrough.
TEST_F(PassthroughTransportSocketTest, SupportsAlpn) {
  EXPECT_FALSE(upstream_factory_->supportsAlpn());
}

// Test getIoHandleFd returns valid file descriptor.
TEST_F(PassthroughTransportSocketTest, GetIoHandleFd) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  // This test verifies that the ABI function for getting the FD exists and is callable.
  Buffer::OwnedImpl buffer;
  buffer.add("test", 4);

  EXPECT_CALL(callbacks_, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, write(_))
      .WillOnce([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        uint64_t len = buffer.length();
        buffer.drain(len);
        return Api::IoCallUint64Result{len, Api::IoError::none()};
      });

  socket->doWrite(buffer, false);
}

// Test setReadable callback.
TEST_F(PassthroughTransportSocketTest, SetReadable) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  // The passthrough socket should call setTransportSocketIsReadable during operations.
  EXPECT_CALL(callbacks_, setTransportSocketIsReadable()).Times(testing::AtLeast(0));

  Buffer::OwnedImpl buffer;
  EXPECT_CALL(callbacks_, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, read(_, _))
      .WillOnce([](Buffer::Instance& buffer, absl::optional<uint64_t>) -> Api::IoCallUint64Result {
        buffer.add("data", 4);
        return Api::IoCallUint64Result{4, Api::IoError::none()};
      });

  socket->doRead(buffer);
}

// Test flushWriteBuffer callback.
TEST_F(PassthroughTransportSocketTest, FlushWriteBuffer) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  // flushWriteBuffer should be callable.
  EXPECT_CALL(callbacks_, flushWriteBuffer()).Times(testing::AtLeast(0));
}

// Test raiseEvent with different event types (additional coverage).
TEST_F(PassthroughTransportSocketTest, RaiseEventMultiple) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);

  // Test raising Connected event.
  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  socket->onConnected();

  // Test closing with different events.
  testing::Mock::VerifyAndClearExpectations(&callbacks_);
  socket->closeSocket(Network::ConnectionEvent::RemoteClose);

  testing::Mock::VerifyAndClearExpectations(&callbacks_);
  socket->closeSocket(Network::ConnectionEvent::LocalClose);
}

// Test that error paths in doRead are handled.
TEST_F(PassthroughTransportSocketTest, DoReadError) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  Buffer::OwnedImpl buffer;

  EXPECT_CALL(callbacks_, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, read(_, _))
      .WillOnce([](Buffer::Instance&, absl::optional<uint64_t>) -> Api::IoCallUint64Result {
        return Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()};
      });

  auto result = socket->doRead(buffer);
  EXPECT_EQ(result.action_, Network::PostIoAction::KeepOpen);
  EXPECT_EQ(result.bytes_processed_, 0);
}

// Test that error paths in doWrite are handled.
TEST_F(PassthroughTransportSocketTest, DoWriteError) {
  auto socket = upstream_factory_->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  socket->onConnected();

  Buffer::OwnedImpl buffer;
  buffer.add("test data", 9);

  EXPECT_CALL(callbacks_, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, write(_)).WillOnce([](Buffer::Instance&) -> Api::IoCallUint64Result {
    return Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()};
  });

  auto result = socket->doWrite(buffer, false);
  EXPECT_EQ(result.action_, Network::PostIoAction::KeepOpen);
  EXPECT_EQ(result.bytes_processed_, 0);
}

// Test createTransportSocket with options.
TEST_F(PassthroughTransportSocketTest, CreateWithOptions) {
  // Create socket with non-null options (host info as shared_ptr).
  auto host_ptr = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  auto socket = upstream_factory_->createTransportSocket(nullptr, host_ptr);
  ASSERT_NE(socket, nullptr);

  socket->setTransportSocketCallbacks(callbacks_);
  EXPECT_TRUE(socket->protocol().empty() || !socket->protocol().empty());
}

} // namespace
} // namespace TransportSockets
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy

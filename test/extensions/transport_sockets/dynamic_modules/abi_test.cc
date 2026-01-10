#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/extensions/dynamic_modules/transport_socket_abi.h"
#include "source/extensions/transport_sockets/dynamic_modules/config.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {
namespace {

using UpstreamTransportSocketProto =
    envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleUpstreamTransportSocket;

// External C callback declarations from abi_impl.cc.
extern "C" {
void* envoy_dynamic_module_callback_transport_socket_get_io_handle(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

int64_t envoy_dynamic_module_callback_transport_socket_io_handle_read(void* io_handle, char* buffer,
                                                                      size_t length,
                                                                      size_t* bytes_read);

int64_t envoy_dynamic_module_callback_transport_socket_io_handle_write(void* io_handle,
                                                                       const char* buffer,
                                                                       size_t length,
                                                                       size_t* bytes_written);

void envoy_dynamic_module_callback_transport_socket_read_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr, size_t length);

void envoy_dynamic_module_callback_transport_socket_read_buffer_add(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    const char* data, size_t length);

size_t envoy_dynamic_module_callback_transport_socket_read_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

void envoy_dynamic_module_callback_transport_socket_write_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr, size_t length);

void envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* slices, size_t* slices_count);

size_t envoy_dynamic_module_callback_transport_socket_write_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

void envoy_dynamic_module_callback_transport_socket_raise_event(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_network_connection_event event);

bool envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

void envoy_dynamic_module_callback_transport_socket_set_is_readable(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

void envoy_dynamic_module_callback_transport_socket_flush_write_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);
}

// ============================================================================
// Tests for null pointer handling (edge cases).
// ============================================================================

class AbiCallbackNullTest : public testing::Test {};

TEST_F(AbiCallbackNullTest, GetIoHandleNullSocket) {
  auto* result = envoy_dynamic_module_callback_transport_socket_get_io_handle(nullptr);
  EXPECT_EQ(result, nullptr);
}

TEST_F(AbiCallbackNullTest, IoHandleReadNullPointers) {
  char buffer[10];
  size_t bytes_read;

  // All null.
  auto result =
      envoy_dynamic_module_callback_transport_socket_io_handle_read(nullptr, nullptr, 0, nullptr);
  EXPECT_EQ(result, -1);

  // Null io_handle.
  result = envoy_dynamic_module_callback_transport_socket_io_handle_read(nullptr, buffer, 10,
                                                                         &bytes_read);
  EXPECT_EQ(result, -1);

  // Null buffer (io_handle is fake but non-null for this test).
  int fake_handle = 1;
  result = envoy_dynamic_module_callback_transport_socket_io_handle_read(&fake_handle, nullptr, 10,
                                                                         &bytes_read);
  EXPECT_EQ(result, -1);

  // Null bytes_read.
  result =
      envoy_dynamic_module_callback_transport_socket_io_handle_read(nullptr, buffer, 10, nullptr);
  EXPECT_EQ(result, -1);
}

TEST_F(AbiCallbackNullTest, IoHandleWriteNullPointers) {
  const char buffer[] = "test";
  size_t bytes_written;

  // All null.
  auto result =
      envoy_dynamic_module_callback_transport_socket_io_handle_write(nullptr, nullptr, 0, nullptr);
  EXPECT_EQ(result, -1);

  // Null io_handle.
  result = envoy_dynamic_module_callback_transport_socket_io_handle_write(nullptr, buffer, 4,
                                                                          &bytes_written);
  EXPECT_EQ(result, -1);

  // Null buffer.
  int fake_handle = 1;
  result = envoy_dynamic_module_callback_transport_socket_io_handle_write(&fake_handle, nullptr, 10,
                                                                          &bytes_written);
  EXPECT_EQ(result, -1);

  // Null bytes_written.
  result =
      envoy_dynamic_module_callback_transport_socket_io_handle_write(nullptr, buffer, 4, nullptr);
  EXPECT_EQ(result, -1);
}

TEST_F(AbiCallbackNullTest, ReadBufferDrainNullSocket) {
  envoy_dynamic_module_callback_transport_socket_read_buffer_drain(nullptr, 10);
}

TEST_F(AbiCallbackNullTest, ReadBufferAddNullSocket) {
  envoy_dynamic_module_callback_transport_socket_read_buffer_add(nullptr, "test", 4);
}

TEST_F(AbiCallbackNullTest, ReadBufferLengthNullSocket) {
  auto result = envoy_dynamic_module_callback_transport_socket_read_buffer_length(nullptr);
  EXPECT_EQ(result, 0);
}

TEST_F(AbiCallbackNullTest, WriteBufferDrainNullSocket) {
  envoy_dynamic_module_callback_transport_socket_write_buffer_drain(nullptr, 10);
}

TEST_F(AbiCallbackNullTest, WriteBufferGetSlicesNullSocket) {
  envoy_dynamic_module_type_envoy_buffer slices[4];
  size_t slices_count = 4;
  envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(nullptr, slices,
                                                                         &slices_count);
  EXPECT_EQ(slices_count, 0);
}

TEST_F(AbiCallbackNullTest, WriteBufferGetSlicesNullCount) {
  envoy_dynamic_module_type_envoy_buffer slices[4];
  envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(nullptr, slices, nullptr);
}

TEST_F(AbiCallbackNullTest, WriteBufferLengthNullSocket) {
  auto result = envoy_dynamic_module_callback_transport_socket_write_buffer_length(nullptr);
  EXPECT_EQ(result, 0);
}

TEST_F(AbiCallbackNullTest, RaiseEventNullSocket) {
  envoy_dynamic_module_callback_transport_socket_raise_event(
      nullptr, envoy_dynamic_module_type_network_connection_event_Connected);
}

TEST_F(AbiCallbackNullTest, ShouldDrainReadBufferNullSocket) {
  auto result = envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(nullptr);
  EXPECT_FALSE(result);
}

TEST_F(AbiCallbackNullTest, SetIsReadableNullSocket) {
  envoy_dynamic_module_callback_transport_socket_set_is_readable(nullptr);
}

TEST_F(AbiCallbackNullTest, FlushWriteBufferNullSocket) {
  envoy_dynamic_module_callback_transport_socket_flush_write_buffer(nullptr);
}

// ============================================================================
// Tests with real DynamicModuleTransportSocket for full code coverage.
// ============================================================================

class AbiCallbackWithSocketTest : public testing::Test {
protected:
  void SetUp() override {
    // Set up the dynamic module search path.
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
  }

  Network::TransportSocketPtr createSocket() {
    const std::string yaml = R"EOF(
    dynamic_module_config:
      name: transport_socket_passthrough
    socket_name: passthrough
)EOF";

    UpstreamTransportSocketProto proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);

    UpstreamDynamicModuleTransportSocketConfigFactory factory;
    ON_CALL(context_, messageValidationVisitor())
        .WillByDefault(testing::ReturnRef(validation_visitor_));

    auto result = factory.createTransportSocketFactory(proto_config, context_);
    EXPECT_TRUE(result.ok()) << result.status().message();
    return result.value()->createTransportSocket(nullptr, nullptr);
  }

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

// Test get_io_handle with real socket and callbacks.
TEST_F(AbiCallbackWithSocketTest, GetIoHandleWithCallbacks) {
  auto socket = createSocket();
  ASSERT_NE(socket, nullptr);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  NiceMock<Network::MockIoHandle> io_handle;
  ON_CALL(callbacks, ioHandle()).WillByDefault(testing::ReturnRef(io_handle));

  socket->setTransportSocketCallbacks(callbacks);

  // Cast the socket to the internal type.
  auto* dm_socket = dynamic_cast<DynamicModuleTransportSocket*>(socket.get());
  ASSERT_NE(dm_socket, nullptr);

  auto* result = envoy_dynamic_module_callback_transport_socket_get_io_handle(dm_socket);
  EXPECT_NE(result, nullptr);
  EXPECT_EQ(result, &io_handle);
}

// Test get_io_handle with real socket but no callbacks.
TEST_F(AbiCallbackWithSocketTest, GetIoHandleNoCallbacks) {
  auto socket = createSocket();
  ASSERT_NE(socket, nullptr);

  auto* dm_socket = dynamic_cast<DynamicModuleTransportSocket*>(socket.get());
  ASSERT_NE(dm_socket, nullptr);

  // Callbacks not set, should return nullptr.
  auto* result = envoy_dynamic_module_callback_transport_socket_get_io_handle(dm_socket);
  EXPECT_EQ(result, nullptr);
}

// Test raise_event with real socket and all event types.
TEST_F(AbiCallbackWithSocketTest, RaiseEventAllTypes) {
  auto socket = createSocket();
  ASSERT_NE(socket, nullptr);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  socket->setTransportSocketCallbacks(callbacks);

  auto* dm_socket = dynamic_cast<DynamicModuleTransportSocket*>(socket.get());
  ASSERT_NE(dm_socket, nullptr);

  EXPECT_CALL(callbacks, raiseEvent(Network::ConnectionEvent::RemoteClose));
  envoy_dynamic_module_callback_transport_socket_raise_event(
      dm_socket, envoy_dynamic_module_type_network_connection_event_RemoteClose);

  EXPECT_CALL(callbacks, raiseEvent(Network::ConnectionEvent::LocalClose));
  envoy_dynamic_module_callback_transport_socket_raise_event(
      dm_socket, envoy_dynamic_module_type_network_connection_event_LocalClose);

  EXPECT_CALL(callbacks, raiseEvent(Network::ConnectionEvent::Connected));
  envoy_dynamic_module_callback_transport_socket_raise_event(
      dm_socket, envoy_dynamic_module_type_network_connection_event_Connected);

  EXPECT_CALL(callbacks, raiseEvent(Network::ConnectionEvent::ConnectedZeroRtt));
  envoy_dynamic_module_callback_transport_socket_raise_event(
      dm_socket, envoy_dynamic_module_type_network_connection_event_ConnectedZeroRtt);

  // Unknown event type defaults to LocalClose.
  EXPECT_CALL(callbacks, raiseEvent(Network::ConnectionEvent::LocalClose));
  envoy_dynamic_module_callback_transport_socket_raise_event(
      dm_socket, static_cast<envoy_dynamic_module_type_network_connection_event>(999));
}

// Test should_drain_read_buffer with real socket.
TEST_F(AbiCallbackWithSocketTest, ShouldDrainReadBuffer) {
  auto socket = createSocket();
  ASSERT_NE(socket, nullptr);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  socket->setTransportSocketCallbacks(callbacks);

  auto* dm_socket = dynamic_cast<DynamicModuleTransportSocket*>(socket.get());
  ASSERT_NE(dm_socket, nullptr);

  ON_CALL(callbacks, shouldDrainReadBuffer()).WillByDefault(testing::Return(true));
  EXPECT_TRUE(envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(dm_socket));

  ON_CALL(callbacks, shouldDrainReadBuffer()).WillByDefault(testing::Return(false));
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(dm_socket));
}

// Test set_is_readable with real socket.
TEST_F(AbiCallbackWithSocketTest, SetIsReadable) {
  auto socket = createSocket();
  ASSERT_NE(socket, nullptr);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  socket->setTransportSocketCallbacks(callbacks);

  auto* dm_socket = dynamic_cast<DynamicModuleTransportSocket*>(socket.get());
  ASSERT_NE(dm_socket, nullptr);

  EXPECT_CALL(callbacks, setTransportSocketIsReadable());
  envoy_dynamic_module_callback_transport_socket_set_is_readable(dm_socket);
}

// Test flush_write_buffer with real socket.
TEST_F(AbiCallbackWithSocketTest, FlushWriteBuffer) {
  auto socket = createSocket();
  ASSERT_NE(socket, nullptr);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  socket->setTransportSocketCallbacks(callbacks);

  auto* dm_socket = dynamic_cast<DynamicModuleTransportSocket*>(socket.get());
  ASSERT_NE(dm_socket, nullptr);

  EXPECT_CALL(callbacks, flushWriteBuffer());
  envoy_dynamic_module_callback_transport_socket_flush_write_buffer(dm_socket);
}

// Test read buffer operations during doRead.
TEST_F(AbiCallbackWithSocketTest, ReadBufferOperationsDuringDoRead) {
  auto socket = createSocket();
  ASSERT_NE(socket, nullptr);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  socket->setTransportSocketCallbacks(callbacks);

  auto* dm_socket = dynamic_cast<DynamicModuleTransportSocket*>(socket.get());
  ASSERT_NE(dm_socket, nullptr);

  // Before doRead, currentReadBuffer is null.
  EXPECT_EQ(dm_socket->currentReadBuffer(), nullptr);
  EXPECT_EQ(envoy_dynamic_module_callback_transport_socket_read_buffer_length(dm_socket), 0);

  // The callback operations with null buffer should not crash.
  envoy_dynamic_module_callback_transport_socket_read_buffer_add(dm_socket, "test", 4);
  envoy_dynamic_module_callback_transport_socket_read_buffer_drain(dm_socket, 2);
  EXPECT_EQ(envoy_dynamic_module_callback_transport_socket_read_buffer_length(dm_socket), 0);
}

// Test write buffer operations during doWrite.
TEST_F(AbiCallbackWithSocketTest, WriteBufferOperationsDuringDoWrite) {
  auto socket = createSocket();
  ASSERT_NE(socket, nullptr);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  socket->setTransportSocketCallbacks(callbacks);

  auto* dm_socket = dynamic_cast<DynamicModuleTransportSocket*>(socket.get());
  ASSERT_NE(dm_socket, nullptr);

  // Before doWrite, currentWriteBuffer is null.
  EXPECT_EQ(dm_socket->currentWriteBuffer(), nullptr);
  EXPECT_EQ(envoy_dynamic_module_callback_transport_socket_write_buffer_length(dm_socket), 0);

  // The callback operations with null buffer should not crash.
  envoy_dynamic_module_callback_transport_socket_write_buffer_drain(dm_socket, 2);

  envoy_dynamic_module_type_envoy_buffer slices[4];
  size_t slices_count = 4;
  envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(dm_socket, slices,
                                                                         &slices_count);
  EXPECT_EQ(slices_count, 0);
}

// Test io_handle operations with mock handles.
TEST_F(AbiCallbackWithSocketTest, IoHandleReadWithMock) {
  auto socket = createSocket();
  ASSERT_NE(socket, nullptr);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  NiceMock<Network::MockIoHandle> io_handle;
  ON_CALL(callbacks, ioHandle()).WillByDefault(testing::ReturnRef(io_handle));
  socket->setTransportSocketCallbacks(callbacks);

  auto* dm_socket = dynamic_cast<DynamicModuleTransportSocket*>(socket.get());
  ASSERT_NE(dm_socket, nullptr);

  // Get the io_handle.
  auto* handle = envoy_dynamic_module_callback_transport_socket_get_io_handle(dm_socket);
  ASSERT_NE(handle, nullptr);

  // Test read via the ABI callback.
  char buffer[100];
  size_t bytes_read = 0;

  // Mock recv to return data.
  EXPECT_CALL(io_handle, recv(testing::_, testing::_, testing::_))
      .WillOnce(testing::Invoke([](void* buf, size_t len, int) {
        const char* data = "hello";
        size_t copy_len = std::min(len, strlen(data));
        memcpy(buf, data, copy_len);
        return Api::IoCallUint64Result(copy_len, Api::IoError::none());
      }));

  auto result = envoy_dynamic_module_callback_transport_socket_io_handle_read(handle, buffer, 100,
                                                                              &bytes_read);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(bytes_read, 5);
  EXPECT_EQ(std::string(buffer, bytes_read), "hello");
}

// Test io_handle write with mock.
TEST_F(AbiCallbackWithSocketTest, IoHandleWriteWithMock) {
  auto socket = createSocket();
  ASSERT_NE(socket, nullptr);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  NiceMock<Network::MockIoHandle> io_handle;
  ON_CALL(callbacks, ioHandle()).WillByDefault(testing::ReturnRef(io_handle));
  socket->setTransportSocketCallbacks(callbacks);

  auto* dm_socket = dynamic_cast<DynamicModuleTransportSocket*>(socket.get());
  auto* handle = envoy_dynamic_module_callback_transport_socket_get_io_handle(dm_socket);
  ASSERT_NE(handle, nullptr);

  const char* data = "test data";
  size_t bytes_written = 0;

  // Mock writev to accept the data.
  EXPECT_CALL(io_handle, writev(testing::_, testing::_))
      .WillOnce(testing::Invoke([](const Buffer::RawSlice* slices, uint64_t num_slices) {
        uint64_t total = 0;
        for (uint64_t i = 0; i < num_slices; i++) {
          total += slices[i].len_;
        }
        return Api::IoCallUint64Result(total, Api::IoError::none());
      }));

  auto result = envoy_dynamic_module_callback_transport_socket_io_handle_write(
      handle, data, strlen(data), &bytes_written);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(bytes_written, 9);
}

// Test write buffer operations with actual buffers.
TEST_F(AbiCallbackWithSocketTest, WriteBufferWithActualData) {
  auto socket = createSocket();
  ASSERT_NE(socket, nullptr);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  NiceMock<Network::MockIoHandle> io_handle;
  ON_CALL(callbacks, ioHandle()).WillByDefault(testing::ReturnRef(io_handle));
  socket->setTransportSocketCallbacks(callbacks);

  auto* dm_socket = dynamic_cast<DynamicModuleTransportSocket*>(socket.get());
  ASSERT_NE(dm_socket, nullptr);

  // Set up write buffer by calling doWrite.
  Buffer::OwnedImpl write_buffer("test write data");
  dm_socket->setCurrentWriteBuffer(&write_buffer);

  // Now test buffer operations.
  EXPECT_EQ(envoy_dynamic_module_callback_transport_socket_write_buffer_length(dm_socket), 15);

  // Test get slices.
  envoy_dynamic_module_type_envoy_buffer slices[4];
  size_t slices_count = 4;
  envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(dm_socket, slices,
                                                                         &slices_count);
  EXPECT_GT(slices_count, 0);

  // Test drain.
  envoy_dynamic_module_callback_transport_socket_write_buffer_drain(dm_socket, 5);
  EXPECT_EQ(write_buffer.length(), 10);

  dm_socket->setCurrentWriteBuffer(nullptr);
}

// Test read buffer operations with actual buffers.
TEST_F(AbiCallbackWithSocketTest, ReadBufferWithActualData) {
  auto socket = createSocket();
  ASSERT_NE(socket, nullptr);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  socket->setTransportSocketCallbacks(callbacks);

  auto* dm_socket = dynamic_cast<DynamicModuleTransportSocket*>(socket.get());
  ASSERT_NE(dm_socket, nullptr);

  // Set up read buffer.
  Buffer::OwnedImpl read_buffer;
  dm_socket->setCurrentReadBuffer(&read_buffer);

  // Test add data to read buffer.
  envoy_dynamic_module_callback_transport_socket_read_buffer_add(dm_socket, "hello world", 11);
  EXPECT_EQ(read_buffer.length(), 11);
  EXPECT_EQ(envoy_dynamic_module_callback_transport_socket_read_buffer_length(dm_socket), 11);

  // Test drain.
  envoy_dynamic_module_callback_transport_socket_read_buffer_drain(dm_socket, 6);
  EXPECT_EQ(read_buffer.length(), 5);

  dm_socket->setCurrentReadBuffer(nullptr);
}

} // namespace
} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

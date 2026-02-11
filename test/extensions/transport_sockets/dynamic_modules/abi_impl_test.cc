#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/dynamic_modules/transport_socket_abi.h"
#include "source/extensions/transport_sockets/dynamic_modules/config.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

class DynamicModuleTransportSocketAbiImplTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
        Envoy::Extensions::DynamicModules::testSharedObjectPath("transport_socket_no_op", "c"),
        false);
    ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto factory_config_or_error = newDynamicModuleTransportSocketFactoryConfig(
        "test_socket", "", /*is_upstream=*/true, std::move(dynamic_module.value()));
    ASSERT_TRUE(factory_config_or_error.ok()) << factory_config_or_error.status().message();
    factory_config_ = std::move(factory_config_or_error.value());

    // Set up the mock IoHandle on transport callbacks.
    ON_CALL(transport_callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));

    socket_ = std::make_unique<DynamicModuleTransportSocket>(factory_config_);
    socket_->setTransportSocketCallbacks(transport_callbacks_);
  }

  void* socketPtr() { return static_cast<void*>(socket_.get()); }

  DynamicModuleTransportSocketFactoryConfigSharedPtr factory_config_;
  std::unique_ptr<DynamicModuleTransportSocket> socket_;
  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks_;
  NiceMock<Network::MockIoHandle> io_handle_;
};

// Test read buffer add and length.
TEST_F(DynamicModuleTransportSocketAbiImplTest, ReadBufferAddAndLength) {
  Buffer::OwnedImpl read_buffer;
  socket_->setCurrentReadBuffer(&read_buffer);

  const char* data = "hello world";
  envoy_dynamic_module_callback_transport_socket_read_buffer_add(socketPtr(), data, 11);
  EXPECT_EQ(11, envoy_dynamic_module_callback_transport_socket_read_buffer_length(socketPtr()));
  EXPECT_EQ("hello world", read_buffer.toString());

  socket_->setCurrentReadBuffer(nullptr);
}

// Test read buffer drain.
TEST_F(DynamicModuleTransportSocketAbiImplTest, ReadBufferDrain) {
  Buffer::OwnedImpl read_buffer;
  read_buffer.add("hello world");
  socket_->setCurrentReadBuffer(&read_buffer);

  envoy_dynamic_module_callback_transport_socket_read_buffer_drain(socketPtr(), 5);
  EXPECT_EQ(6, read_buffer.length());
  EXPECT_EQ(" world", read_buffer.toString());

  socket_->setCurrentReadBuffer(nullptr);
}

// Test write buffer length and get slices.
TEST_F(DynamicModuleTransportSocketAbiImplTest, WriteBufferLengthAndSlices) {
  Buffer::OwnedImpl write_buffer;
  write_buffer.add("hello");
  write_buffer.add(" world");
  socket_->setCurrentWriteBuffer(&write_buffer);

  EXPECT_EQ(11, envoy_dynamic_module_callback_transport_socket_write_buffer_length(socketPtr()));

  envoy_dynamic_module_type_envoy_buffer slices[8];
  size_t slices_count = 8;
  envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(socketPtr(), slices,
                                                                         &slices_count);
  EXPECT_GT(slices_count, 0);

  // Verify total data across slices.
  std::string total;
  for (size_t i = 0; i < slices_count; i++) {
    total.append(slices[i].ptr, slices[i].length);
  }
  EXPECT_EQ("hello world", total);

  socket_->setCurrentWriteBuffer(nullptr);
}

// Test write buffer drain.
TEST_F(DynamicModuleTransportSocketAbiImplTest, WriteBufferDrain) {
  Buffer::OwnedImpl write_buffer;
  write_buffer.add("hello world");
  socket_->setCurrentWriteBuffer(&write_buffer);

  envoy_dynamic_module_callback_transport_socket_write_buffer_drain(socketPtr(), 6);
  EXPECT_EQ(5, write_buffer.length());
  EXPECT_EQ("world", write_buffer.toString());

  socket_->setCurrentWriteBuffer(nullptr);
}

// Test raise event callback.
TEST_F(DynamicModuleTransportSocketAbiImplTest, RaiseEvent) {
  EXPECT_CALL(transport_callbacks_, raiseEvent(Network::ConnectionEvent::RemoteClose));
  envoy_dynamic_module_callback_transport_socket_raise_event(
      socketPtr(), envoy_dynamic_module_type_network_connection_event_RemoteClose);

  EXPECT_CALL(transport_callbacks_, raiseEvent(Network::ConnectionEvent::LocalClose));
  envoy_dynamic_module_callback_transport_socket_raise_event(
      socketPtr(), envoy_dynamic_module_type_network_connection_event_LocalClose);

  EXPECT_CALL(transport_callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  envoy_dynamic_module_callback_transport_socket_raise_event(
      socketPtr(), envoy_dynamic_module_type_network_connection_event_Connected);

  EXPECT_CALL(transport_callbacks_, raiseEvent(Network::ConnectionEvent::ConnectedZeroRtt));
  envoy_dynamic_module_callback_transport_socket_raise_event(
      socketPtr(), envoy_dynamic_module_type_network_connection_event_ConnectedZeroRtt);
}

// Test shouldDrainReadBuffer callback.
TEST_F(DynamicModuleTransportSocketAbiImplTest, ShouldDrainReadBuffer) {
  EXPECT_CALL(transport_callbacks_, shouldDrainReadBuffer()).WillOnce(Return(true));
  EXPECT_TRUE(envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(socketPtr()));

  EXPECT_CALL(transport_callbacks_, shouldDrainReadBuffer()).WillOnce(Return(false));
  EXPECT_FALSE(
      envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(socketPtr()));
}

// Test setTransportSocketIsReadable callback.
TEST_F(DynamicModuleTransportSocketAbiImplTest, SetIsReadable) {
  EXPECT_CALL(transport_callbacks_, setTransportSocketIsReadable());
  envoy_dynamic_module_callback_transport_socket_set_is_readable(socketPtr());
}

// Test flushWriteBuffer callback.
TEST_F(DynamicModuleTransportSocketAbiImplTest, FlushWriteBuffer) {
  EXPECT_CALL(transport_callbacks_, flushWriteBuffer());
  envoy_dynamic_module_callback_transport_socket_flush_write_buffer(socketPtr());
}

// Test null buffer safety.
TEST_F(DynamicModuleTransportSocketAbiImplTest, NullBufferSafety) {
  // Ensure callbacks handle null buffers gracefully when no buffer is set.
  EXPECT_EQ(0, envoy_dynamic_module_callback_transport_socket_read_buffer_length(socketPtr()));
  EXPECT_EQ(0, envoy_dynamic_module_callback_transport_socket_write_buffer_length(socketPtr()));

  size_t slices_count = 8;
  envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(socketPtr(), nullptr,
                                                                         &slices_count);
  EXPECT_EQ(0, slices_count);
}

// Test write buffer get slices query mode (null slices returns count).
TEST_F(DynamicModuleTransportSocketAbiImplTest, WriteBufferGetSlicesQueryMode) {
  Buffer::OwnedImpl write_buffer;
  write_buffer.add("hello");
  write_buffer.add(" world");
  socket_->setCurrentWriteBuffer(&write_buffer);

  // Query mode: null slices returns the total number of slices.
  size_t slices_count = 0;
  envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(socketPtr(), nullptr,
                                                                         &slices_count);
  EXPECT_GT(slices_count, 0);

  socket_->setCurrentWriteBuffer(nullptr);
}

// Test get_io_handle callback.
TEST_F(DynamicModuleTransportSocketAbiImplTest, GetIoHandle) {
  void* handle = envoy_dynamic_module_callback_transport_socket_get_io_handle(socketPtr());
  // Should return the address of the mock io handle.
  EXPECT_NE(nullptr, handle);
}

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

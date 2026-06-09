#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/transport_sockets/dynamic_modules/config.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::Invoke;
using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {
namespace {

constexpr char kRustModulesPath[] =
    "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust";
constexpr char kCModulesPath[] = "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c";
constexpr char kReferenceModule[] = "transport_socket_integration_test";
constexpr uint8_t kXorKey = 0x5a;

envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleTransportSocket
buildProtoConfig(const std::string& module_name, const std::string& socket_name) {
  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleTransportSocket config;
  config.mutable_dynamic_module_config()->set_name(module_name);
  config.set_transport_socket_name(socket_name);
  return config;
}

std::string xorString(absl::string_view in, uint8_t key) {
  std::string out;
  out.reserve(in.size());
  for (const char c : in) {
    out.push_back(static_cast<char>(static_cast<uint8_t>(c) ^ key));
  }
  return out;
}

// Stands in for a module whose on_new fails, for example after a panic, by returning null.
extern "C" envoy_dynamic_module_type_transport_socket_module_ptr
returnNullTransportSocket(envoy_dynamic_module_type_transport_socket_factory_config_module_ptr,
                          envoy_dynamic_module_type_transport_socket_envoy_ptr) {
  return nullptr;
}

// Tests for the upstream and downstream config factories.
class DynamicModuleTransportSocketConfigTest : public testing::Test {
public:
  void SetUp() override {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(kRustModulesPath), 1);
  }

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  UpstreamDynamicModuleTransportSocketConfigFactory upstream_factory_;
  DownstreamDynamicModuleTransportSocketConfigFactory downstream_factory_;
};

TEST_F(DynamicModuleTransportSocketConfigTest, FactoryName) {
  EXPECT_EQ("envoy.transport_sockets.dynamic_modules", upstream_factory_.name());
  EXPECT_EQ("envoy.transport_sockets.dynamic_modules", downstream_factory_.name());
}

TEST_F(DynamicModuleTransportSocketConfigTest, CreateEmptyConfigProto) {
  EXPECT_NE(nullptr, upstream_factory_.createEmptyConfigProto());
  EXPECT_NE(nullptr, downstream_factory_.createEmptyConfigProto());
}

TEST_F(DynamicModuleTransportSocketConfigTest, DownstreamValidConfig) {
  auto config = buildProtoConfig(kReferenceModule, "passthrough");
  auto factory_or_error = downstream_factory_.createTransportSocketFactory(config, context_, {});
  ASSERT_TRUE(factory_or_error.ok()) << factory_or_error.status().message();
  auto factory = std::move(factory_or_error.value());
  EXPECT_FALSE(factory->implementsSecureTransport());
  EXPECT_NE(nullptr, factory->createDownstreamTransportSocket());
}

TEST_F(DynamicModuleTransportSocketConfigTest, UpstreamValidConfig) {
  auto config = buildProtoConfig(kReferenceModule, "passthrough");
  auto factory_or_error = upstream_factory_.createTransportSocketFactory(config, context_);
  ASSERT_TRUE(factory_or_error.ok()) << factory_or_error.status().message();
  auto factory = std::move(factory_or_error.value());
  EXPECT_FALSE(factory->implementsSecureTransport());
  EXPECT_EQ("", factory->defaultServerNameIndication());
  EXPECT_NE(nullptr, factory->createTransportSocket(nullptr, nullptr));
}

TEST_F(DynamicModuleTransportSocketConfigTest, ImplementsSecureTransport) {
  auto config = buildProtoConfig(kReferenceModule, "passthrough");
  config.set_implements_secure_transport(true);
  auto factory_or_error = downstream_factory_.createTransportSocketFactory(config, context_, {});
  ASSERT_TRUE(factory_or_error.ok()) << factory_or_error.status().message();
  EXPECT_TRUE(factory_or_error.value()->implementsSecureTransport());
}

TEST_F(DynamicModuleTransportSocketConfigTest, WithTransportSocketConfig) {
  auto config = buildProtoConfig(kReferenceModule, "xor");
  config.mutable_transport_socket_config()->PackFrom(ValueUtil::stringValue("config_bytes"));
  auto factory_or_error = upstream_factory_.createTransportSocketFactory(config, context_);
  EXPECT_TRUE(factory_or_error.ok()) << factory_or_error.status().message();
}

TEST_F(DynamicModuleTransportSocketConfigTest, InvalidModuleName) {
  auto config = buildProtoConfig("nonexistent_module", "passthrough");
  auto factory_or_error = upstream_factory_.createTransportSocketFactory(config, context_);
  EXPECT_FALSE(factory_or_error.ok());
  EXPECT_THAT(factory_or_error.status().message(), HasSubstr("Failed to load dynamic module"));
}

TEST_F(DynamicModuleTransportSocketConfigTest, MissingTransportSocketSymbols) {
  // The C no_op module only implements HTTP filter symbols, so the transport socket entry points
  // cannot be resolved.
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                             TestEnvironment::substitute(kCModulesPath), 1);
  auto config = buildProtoConfig("no_op", "passthrough");
  auto factory_or_error = downstream_factory_.createTransportSocketFactory(config, context_, {});
  EXPECT_FALSE(factory_or_error.ok());
  EXPECT_THAT(factory_or_error.status().message(), HasSubstr("Failed to resolve symbol"));
}

TEST_F(DynamicModuleTransportSocketConfigTest, ConfigInitializationFailure) {
  // The reference module returns null for an unknown socket name.
  auto config = buildProtoConfig(kReferenceModule, "unknown_socket");
  auto factory_or_error = upstream_factory_.createTransportSocketFactory(config, context_);
  EXPECT_FALSE(factory_or_error.ok());
  EXPECT_THAT(factory_or_error.status().message(),
              HasSubstr("Failed to initialize dynamic module transport socket config"));
}

TEST_F(DynamicModuleTransportSocketConfigTest, DoNotCloseAndLoadGloballyOptions) {
  auto config = buildProtoConfig(kReferenceModule, "passthrough");
  config.mutable_dynamic_module_config()->set_do_not_close(true);
  config.mutable_dynamic_module_config()->set_load_globally(true);
  auto factory_or_error = upstream_factory_.createTransportSocketFactory(config, context_);
  EXPECT_TRUE(factory_or_error.ok()) << factory_or_error.status().message();
}

// Tests that exercise a transport socket instance against a mocked I/O handle.
class DynamicModuleTransportSocketTest : public testing::Test {
public:
  void SetUp() override {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(kRustModulesPath), 1);
    ON_CALL(callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));
  }

  Network::TransportSocketPtr
  createSocket(const std::string& socket_name,
               absl::optional<std::string> config_bytes = absl::nullopt) {
    auto config = buildProtoConfig(kReferenceModule, socket_name);
    if (config_bytes.has_value()) {
      Protobuf::BytesValue bytes_value;
      bytes_value.set_value(*config_bytes);
      config.mutable_transport_socket_config()->PackFrom(bytes_value);
    }
    auto factory_or_error = factory_.createTransportSocketFactory(config, context_, {});
    EXPECT_TRUE(factory_or_error.ok()) << factory_or_error.status().message();
    factory_ptr_ = std::move(factory_or_error.value());
    auto socket = factory_ptr_->createDownstreamTransportSocket();
    socket->setTransportSocketCallbacks(callbacks_);
    return socket;
  }

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  DownstreamDynamicModuleTransportSocketConfigFactory factory_;
  Network::DownstreamTransportSocketFactoryPtr factory_ptr_;
  NiceMock<Network::MockTransportSocketCallbacks> callbacks_;
  NiceMock<Network::MockIoHandle> io_handle_;
};

TEST_F(DynamicModuleTransportSocketTest, DoReadTransformsData) {
  auto socket = createSocket("xor");
  const std::string plaintext = "hello";
  EXPECT_CALL(io_handle_, recv(testing::_, testing::_, 0))
      .WillOnce(Invoke([&](void* buffer, size_t length, int) {
        EXPECT_GE(length, plaintext.size());
        memcpy(buffer, plaintext.data(), plaintext.size());
        return Api::IoCallUint64Result(plaintext.size(), Api::IoError::none());
      }))
      .WillRepeatedly(Invoke([](void*, size_t, int) {
        return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEagainError());
      }));

  Buffer::OwnedImpl buffer;
  Network::IoResult result = socket->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(plaintext.size(), result.bytes_processed_);
  EXPECT_FALSE(result.end_stream_read_);
  EXPECT_EQ(xorString(plaintext, kXorKey), buffer.toString());
}

TEST_F(DynamicModuleTransportSocketTest, DoReadYieldsWhenReadBufferShouldDrain) {
  auto socket = createSocket("xor");
  const std::string plaintext = "hello";
  EXPECT_CALL(io_handle_, recv(testing::_, testing::_, 0))
      .WillOnce(Invoke([&](void* buffer, size_t length, int) {
        EXPECT_GE(length, plaintext.size());
        memcpy(buffer, plaintext.data(), plaintext.size());
        return Api::IoCallUint64Result(plaintext.size(), Api::IoError::none());
      }));
  EXPECT_CALL(callbacks_, shouldDrainReadBuffer()).WillOnce(testing::Return(true));
  EXPECT_CALL(callbacks_, setTransportSocketIsReadable());

  Buffer::OwnedImpl buffer;
  Network::IoResult result = socket->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(plaintext.size(), result.bytes_processed_);
  EXPECT_FALSE(result.end_stream_read_);
  EXPECT_EQ(xorString(plaintext, kXorKey), buffer.toString());
}

TEST_F(DynamicModuleTransportSocketTest, DoReadEndStreamOnPeerClose) {
  auto socket = createSocket("passthrough");
  EXPECT_CALL(io_handle_, recv(testing::_, testing::_, 0)).WillOnce(Invoke([](void*, size_t, int) {
    return Api::IoCallUint64Result(0, Api::IoError::none());
  }));

  Buffer::OwnedImpl buffer;
  Network::IoResult result = socket->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(0, result.bytes_processed_);
  EXPECT_TRUE(result.end_stream_read_);
}

TEST_F(DynamicModuleTransportSocketTest, DoReadErrorClosesConnection) {
  auto socket = createSocket("passthrough");
  EXPECT_CALL(io_handle_, recv(testing::_, testing::_, 0)).WillOnce(Invoke([](void*, size_t, int) {
    return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEbadfError());
  }));

  Buffer::OwnedImpl buffer;
  Network::IoResult result = socket->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  // The module records the failure, which surfaces through failureReason.
  EXPECT_EQ("read error", socket->failureReason());
}

TEST_F(DynamicModuleTransportSocketTest, DoWriteTransformsData) {
  auto socket = createSocket("xor");
  std::string captured;
  EXPECT_CALL(io_handle_, writev(testing::_, testing::_))
      .WillOnce(Invoke([&](const Buffer::RawSlice* slices, uint64_t num_slice) {
        uint64_t total = 0;
        for (uint64_t i = 0; i < num_slice; i++) {
          captured.append(static_cast<const char*>(slices[i].mem_), slices[i].len_);
          total += slices[i].len_;
        }
        return Api::IoCallUint64Result(total, Api::IoError::none());
      }));

  Buffer::OwnedImpl buffer("world");
  Network::IoResult result = socket->doWrite(buffer, false);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(5, result.bytes_processed_);
  EXPECT_EQ(xorString("world", kXorKey), captured);
  EXPECT_EQ(0, buffer.length());
}

TEST_F(DynamicModuleTransportSocketTest, DoWriteEndStreamShutsDownWrite) {
  auto socket = createSocket("passthrough");
  EXPECT_CALL(io_handle_, shutdown(ENVOY_SHUT_WR))
      .WillOnce(testing::Return(Api::SysCallIntResult{0, 0}));

  Buffer::OwnedImpl buffer;
  Network::IoResult result = socket->doWrite(buffer, true);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(0, result.bytes_processed_);
}

TEST_F(DynamicModuleTransportSocketTest, DoWriteAgainKeepsConnectionOpen) {
  auto socket = createSocket("passthrough");
  EXPECT_CALL(io_handle_, writev(testing::_, testing::_))
      .WillOnce(Invoke([](const Buffer::RawSlice*, uint64_t) {
        return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEagainError());
      }));

  Buffer::OwnedImpl buffer("world");
  Network::IoResult result = socket->doWrite(buffer, false);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(0, result.bytes_processed_);
  EXPECT_EQ(5, buffer.length());
}

TEST_F(DynamicModuleTransportSocketTest, ProtocolAndFlushClose) {
  auto socket = createSocket("passthrough");
  EXPECT_EQ("", socket->protocol());
  EXPECT_EQ("", socket->failureReason());
  EXPECT_TRUE(socket->canFlushClose());
  EXPECT_EQ(nullptr, socket->ssl());
  EXPECT_FALSE(socket->startSecureTransport());
  socket->configureInitialCongestionWindow(100, std::chrono::microseconds(1));
}

TEST_F(DynamicModuleTransportSocketTest, OnConnectedRaisesConnectedEvent) {
  auto socket = createSocket("passthrough");
  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  socket->onConnected();
}

TEST_F(DynamicModuleTransportSocketTest, CloseSocketHandlesAllConnectionEvents) {
  auto socket = createSocket("passthrough");
  socket->closeSocket(Network::ConnectionEvent::RemoteClose, false);
  socket->closeSocket(Network::ConnectionEvent::LocalClose, true);
  socket->closeSocket(Network::ConnectionEvent::Connected, false);
  socket->closeSocket(Network::ConnectionEvent::ConnectedZeroRtt, false);
}

TEST_F(DynamicModuleTransportSocketTest, StartSecureTransportReflectsModuleDecision) {
  // The reference module reports secure transport and a protocol name when a non-zero transform key
  // is configured.
  auto socket = createSocket("xor");
  EXPECT_TRUE(socket->startSecureTransport());
  EXPECT_EQ("xor", socket->protocol());
}

TEST_F(DynamicModuleTransportSocketTest, GetRemoteAndLocalAddress) {
  auto socket = createSocket("passthrough");
  callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      *Network::Utility::resolveUrl("tcp://172.0.0.1:80"));
  callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      *Network::Utility::resolveUrl("tcp://174.2.2.222:50000"));
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());

  envoy_dynamic_module_type_envoy_buffer address{nullptr, 0};
  uint32_t port = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_transport_socket_get_remote_address(envoy_ptr, &address,
                                                                                &port));
  EXPECT_EQ("172.0.0.1", std::string(address.ptr, address.length));
  EXPECT_EQ(80, port);

  EXPECT_TRUE(
      envoy_dynamic_module_callback_transport_socket_get_local_address(envoy_ptr, &address, &port));
  EXPECT_EQ("174.2.2.222", std::string(address.ptr, address.length));
  EXPECT_EQ(50000, port);
}

TEST_F(DynamicModuleTransportSocketTest, GetAddressWithoutIpReturnsUnavailable) {
  auto socket = createSocket("passthrough");
  // A pipe address has no IP, so the address callbacks report it as unavailable.
  auto pipe_address = *Network::Utility::resolveUrl("unix:///tmp/dynamic_modules_test.sock");
  callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      pipe_address);
  callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      pipe_address);
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());

  envoy_dynamic_module_type_envoy_buffer address{nullptr, 0};
  uint32_t port = 7;
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_get_remote_address(envoy_ptr,
                                                                                 &address, &port));
  EXPECT_EQ(0, port);
  port = 7;
  EXPECT_FALSE(
      envoy_dynamic_module_callback_transport_socket_get_local_address(envoy_ptr, &address, &port));
  EXPECT_EQ(0, port);
}

TEST_F(DynamicModuleTransportSocketTest, FlushWriteBufferForwardsToCallbacks) {
  auto socket = createSocket("passthrough");
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());
  EXPECT_CALL(callbacks_, flushWriteBuffer());
  envoy_dynamic_module_callback_transport_socket_flush_write_buffer(envoy_ptr);
}

TEST_F(DynamicModuleTransportSocketTest, DoReadUsesConfiguredKey) {
  // The first byte of the transport socket config selects the XOR key, overriding the module
  // default.
  constexpr uint8_t configured_key = 0x01;
  auto socket = createSocket("xor", std::string(1, static_cast<char>(configured_key)));
  const std::string plaintext = "hello";
  EXPECT_CALL(io_handle_, recv(testing::_, testing::_, 0))
      .WillOnce(Invoke([&](void* buffer, size_t length, int) {
        EXPECT_GE(length, plaintext.size());
        memcpy(buffer, plaintext.data(), plaintext.size());
        return Api::IoCallUint64Result(plaintext.size(), Api::IoError::none());
      }))
      .WillRepeatedly(Invoke([](void*, size_t, int) {
        return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEagainError());
      }));

  Buffer::OwnedImpl buffer;
  socket->doRead(buffer);
  EXPECT_EQ(xorString(plaintext, configured_key), buffer.toString());
}

TEST_F(DynamicModuleTransportSocketTest, NullInModuleSocketDegradesSafely) {
  // Simulate a module whose on_new returns null. The socket must degrade safely instead of
  // dereferencing the null in-module pointer.
  auto config = std::make_shared<DynamicModuleTransportSocketConfig>(false, nullptr);
  config->on_new_ = returnNullTransportSocket;

  std::unique_ptr<DynamicModuleTransportSocket> socket;
  EXPECT_LOG_CONTAINS("error", "dynamic module failed to create transport socket",
                      { socket = std::make_unique<DynamicModuleTransportSocket>(config); });
  socket->setTransportSocketCallbacks(callbacks_);

  EXPECT_EQ("", socket->protocol());
  EXPECT_EQ("", socket->failureReason());
  EXPECT_TRUE(socket->canFlushClose());
  EXPECT_FALSE(socket->startSecureTransport());

  Buffer::OwnedImpl read_buffer;
  EXPECT_EQ(Network::PostIoAction::Close, socket->doRead(read_buffer).action_);
  Buffer::OwnedImpl write_buffer;
  EXPECT_EQ(Network::PostIoAction::Close, socket->doWrite(write_buffer, false).action_);

  socket->onConnected();
  socket->closeSocket(Network::ConnectionEvent::LocalClose, false);
}

TEST_F(DynamicModuleTransportSocketTest, ConnectionCallbacksWithoutCallbacksAreSafe) {
  auto config = buildProtoConfig(kReferenceModule, "passthrough");
  auto factory_or_error = factory_.createTransportSocketFactory(config, context_, {});
  ASSERT_TRUE(factory_or_error.ok()) << factory_or_error.status().message();
  factory_ptr_ = std::move(factory_or_error.value());
  auto socket = factory_ptr_->createDownstreamTransportSocket();
  // setTransportSocketCallbacks is intentionally not called, so the callbacks pointer is null.
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());

  char buffer[4];
  size_t io_bytes = 7;
  EXPECT_EQ(envoy_dynamic_module_type_transport_socket_io_status_Error,
            envoy_dynamic_module_callback_transport_socket_io_read(envoy_ptr, buffer,
                                                                   sizeof(buffer), &io_bytes));
  EXPECT_EQ(0, io_bytes);
  io_bytes = 7;
  EXPECT_EQ(envoy_dynamic_module_type_transport_socket_io_status_Error,
            envoy_dynamic_module_callback_transport_socket_io_write(envoy_ptr, buffer,
                                                                    sizeof(buffer), &io_bytes));
  EXPECT_EQ(0, io_bytes);
  envoy_dynamic_module_callback_transport_socket_io_shutdown_write(envoy_ptr);
  envoy_dynamic_module_callback_transport_socket_raise_event(
      envoy_ptr, envoy_dynamic_module_type_network_connection_event_RemoteClose);
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(envoy_ptr));
  envoy_dynamic_module_callback_transport_socket_set_is_readable(envoy_ptr);
  envoy_dynamic_module_callback_transport_socket_flush_write_buffer(envoy_ptr);

  envoy_dynamic_module_type_envoy_buffer address{nullptr, 0};
  uint32_t port = 7;
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_get_remote_address(envoy_ptr,
                                                                                 &address, &port));
  EXPECT_EQ(0, port);
  port = 7;
  EXPECT_FALSE(
      envoy_dynamic_module_callback_transport_socket_get_local_address(envoy_ptr, &address, &port));
  EXPECT_EQ(0, port);
}

TEST_F(DynamicModuleTransportSocketTest, IoReadReturnsStatusForEachOutcome) {
  auto socket = createSocket("passthrough");
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());
  char buffer[8];
  size_t bytes_read = 0;

  EXPECT_CALL(io_handle_, recv(testing::_, sizeof(buffer), 0))
      .WillOnce(Invoke([](void* buf, size_t, int) {
        memcpy(buf, "ab", 2);
        return Api::IoCallUint64Result(2, Api::IoError::none());
      }));
  EXPECT_EQ(envoy_dynamic_module_type_transport_socket_io_status_Success,
            envoy_dynamic_module_callback_transport_socket_io_read(envoy_ptr, buffer,
                                                                   sizeof(buffer), &bytes_read));
  EXPECT_EQ(2, bytes_read);

  EXPECT_CALL(io_handle_, recv(testing::_, sizeof(buffer), 0))
      .WillOnce(Invoke([](void*, size_t, int) {
        return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEagainError());
      }));
  EXPECT_EQ(envoy_dynamic_module_type_transport_socket_io_status_Again,
            envoy_dynamic_module_callback_transport_socket_io_read(envoy_ptr, buffer,
                                                                   sizeof(buffer), &bytes_read));
  EXPECT_EQ(0, bytes_read);

  EXPECT_CALL(io_handle_, recv(testing::_, sizeof(buffer), 0))
      .WillOnce(Invoke([](void*, size_t, int) {
        return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEbadfError());
      }));
  EXPECT_EQ(envoy_dynamic_module_type_transport_socket_io_status_Error,
            envoy_dynamic_module_callback_transport_socket_io_read(envoy_ptr, buffer,
                                                                   sizeof(buffer), &bytes_read));
}

TEST_F(DynamicModuleTransportSocketTest, IoWriteReturnsStatusForEachOutcome) {
  auto socket = createSocket("passthrough");
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());
  const std::string data = "abc";
  size_t bytes_written = 0;

  EXPECT_CALL(io_handle_, writev(testing::_, 1))
      .WillOnce(Invoke([](const Buffer::RawSlice* slices, uint64_t) {
        return Api::IoCallUint64Result(slices[0].len_, Api::IoError::none());
      }));
  EXPECT_EQ(envoy_dynamic_module_type_transport_socket_io_status_Success,
            envoy_dynamic_module_callback_transport_socket_io_write(envoy_ptr, data.data(),
                                                                    data.size(), &bytes_written));
  EXPECT_EQ(data.size(), bytes_written);

  EXPECT_CALL(io_handle_, writev(testing::_, 1))
      .WillOnce(Invoke([](const Buffer::RawSlice*, uint64_t) {
        return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEagainError());
      }));
  EXPECT_EQ(envoy_dynamic_module_type_transport_socket_io_status_Again,
            envoy_dynamic_module_callback_transport_socket_io_write(envoy_ptr, data.data(),
                                                                    data.size(), &bytes_written));
  EXPECT_EQ(0, bytes_written);

  EXPECT_CALL(io_handle_, writev(testing::_, 1))
      .WillOnce(Invoke([](const Buffer::RawSlice*, uint64_t) {
        return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEbadfError());
      }));
  EXPECT_EQ(envoy_dynamic_module_type_transport_socket_io_status_Error,
            envoy_dynamic_module_callback_transport_socket_io_write(envoy_ptr, data.data(),
                                                                    data.size(), &bytes_written));
}

TEST_F(DynamicModuleTransportSocketTest, RaiseEventForwardsAllConnectionEvents) {
  auto socket = createSocket("passthrough");
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());

  testing::InSequence sequence;
  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::RemoteClose));
  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::ConnectedZeroRtt));

  envoy_dynamic_module_callback_transport_socket_raise_event(
      envoy_ptr, envoy_dynamic_module_type_network_connection_event_RemoteClose);
  envoy_dynamic_module_callback_transport_socket_raise_event(
      envoy_ptr, envoy_dynamic_module_type_network_connection_event_LocalClose);
  envoy_dynamic_module_callback_transport_socket_raise_event(
      envoy_ptr, envoy_dynamic_module_type_network_connection_event_Connected);
  envoy_dynamic_module_callback_transport_socket_raise_event(
      envoy_ptr, envoy_dynamic_module_type_network_connection_event_ConnectedZeroRtt);
}

TEST_F(DynamicModuleTransportSocketTest, BufferCallbacksWithoutActiveBufferAreNoOps) {
  auto socket = createSocket("passthrough");
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());

  // Outside doRead and doWrite there is no active buffer, so the buffer callbacks are safe no-ops.
  envoy_dynamic_module_callback_transport_socket_read_buffer_add(envoy_ptr, "data", 4);
  envoy_dynamic_module_callback_transport_socket_write_buffer_drain(envoy_ptr, 4);

  size_t slices_count = 4;
  envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(envoy_ptr, nullptr,
                                                                         &slices_count);
  EXPECT_EQ(0, slices_count);
}

} // namespace
} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

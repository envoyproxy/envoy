#include <limits>

#include "envoy/event/file_event.h"
#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/transport_sockets/dynamic_modules/config.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::Envoy::StatusHelpers::HasStatusMessage;
using ::Envoy::StatusHelpers::IsOk;
using ::Envoy::StatusHelpers::IsOkAndHolds;
using testing::HasSubstr;
using testing::Invoke;
using testing::NiceMock;
using ::testing::Not;
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
constexpr char kSpliceModule[] = "transport_socket_splice";
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

// Hooks of a stand-in module whose do_read and do_write return the I/O result that its factory
// config points to.
extern "C" envoy_dynamic_module_type_transport_socket_module_ptr
newStubTransportSocket(envoy_dynamic_module_type_transport_socket_factory_config_module_ptr config,
                       envoy_dynamic_module_type_transport_socket_envoy_ptr) {
  return config;
}

extern "C" void destroyStubTransportSocket(envoy_dynamic_module_type_transport_socket_module_ptr) {}

// Reaches for the stream info, which a client connection constructs only after this returns.
extern "C" void
setStubTransportSocketCallbacks(envoy_dynamic_module_type_transport_socket_envoy_ptr envoy_ptr,
                                envoy_dynamic_module_type_transport_socket_module_ptr) {
  uint64_t upstream_connection_id = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_get_upstream_connection_id(
      envoy_ptr, &upstream_connection_id));
  envoy_dynamic_module_callback_transport_socket_add_wire_bytes(envoy_ptr, 1, 1);
}

extern "C" envoy_dynamic_module_type_transport_socket_io_result
stubTransportSocketDoRead(envoy_dynamic_module_type_transport_socket_envoy_ptr,
                          envoy_dynamic_module_type_transport_socket_module_ptr socket) {
  return *static_cast<const envoy_dynamic_module_type_transport_socket_io_result*>(socket);
}

extern "C" envoy_dynamic_module_type_transport_socket_io_result
stubTransportSocketDoWrite(envoy_dynamic_module_type_transport_socket_envoy_ptr,
                           envoy_dynamic_module_type_transport_socket_module_ptr socket, bool) {
  return *static_cast<const envoy_dynamic_module_type_transport_socket_io_result*>(socket);
}

// State of a stand-in module that tries to splice from inside its own read or write.
struct SplicingSocket {
  envoy_dynamic_module_type_transport_socket_envoy_ptr peer = nullptr;
  mutable bool started = true;
};

extern "C" envoy_dynamic_module_type_transport_socket_io_result
spliceFromDoRead(envoy_dynamic_module_type_transport_socket_envoy_ptr envoy_ptr,
                 envoy_dynamic_module_type_transport_socket_module_ptr socket) {
  const auto* splicing = static_cast<const SplicingSocket*>(socket);
  splicing->started =
      envoy_dynamic_module_callback_transport_socket_start_splice(envoy_ptr, splicing->peer);
  return {envoy_dynamic_module_type_transport_socket_post_io_action_KeepOpen, 0, false};
}

extern "C" envoy_dynamic_module_type_transport_socket_io_result
spliceFromDoWrite(envoy_dynamic_module_type_transport_socket_envoy_ptr envoy_ptr,
                  envoy_dynamic_module_type_transport_socket_module_ptr socket, bool) {
  return spliceFromDoRead(envoy_ptr, socket);
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

// Pull the shared dynamic-modules test helper into scope.
using ::Envoy::Extensions::DynamicModules::failureCounter;

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
  ASSERT_OK(factory_or_error);
  auto factory = std::move(factory_or_error.value());
  EXPECT_FALSE(factory->implementsSecureTransport());
  EXPECT_NE(nullptr, factory->createDownstreamTransportSocket());
}

TEST_F(DynamicModuleTransportSocketConfigTest, UpstreamValidConfig) {
  auto config = buildProtoConfig(kReferenceModule, "passthrough");
  auto factory_or_error = upstream_factory_.createTransportSocketFactory(config, context_);
  ASSERT_OK(factory_or_error);
  auto factory = std::move(factory_or_error.value());
  EXPECT_FALSE(factory->implementsSecureTransport());
  EXPECT_EQ("", factory->defaultServerNameIndication());
  EXPECT_NE(nullptr, factory->createTransportSocket(nullptr, nullptr));

  // The happy path emits no load-failure counters.
  auto& server_scope = context_.server_context_.serverScope();
  EXPECT_EQ(0U, failureCounter(server_scope, "module_load_error", "passthrough"));
  EXPECT_EQ(0U, failureCounter(server_scope, "config_init_error", "passthrough"));
}

// Load the module via the ``module.local.filename`` data source instead of by name.
TEST_F(DynamicModuleTransportSocketConfigTest, ValidConfigWithLocalFile) {
  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleTransportSocket config;
  config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
      TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust/"
          "libtransport_socket_integration_test.so"));
  config.set_transport_socket_name("passthrough");

  auto factory_or_error = upstream_factory_.createTransportSocketFactory(config, context_);
  ASSERT_THAT(factory_or_error, IsOkAndHolds(::testing::NotNull()));
}

TEST_F(DynamicModuleTransportSocketConfigTest, ImplementsSecureTransport) {
  auto config = buildProtoConfig(kReferenceModule, "passthrough");
  config.set_implements_secure_transport(true);
  auto factory_or_error = downstream_factory_.createTransportSocketFactory(config, context_, {});
  ASSERT_OK(factory_or_error);
  EXPECT_TRUE(factory_or_error.value()->implementsSecureTransport());
}

TEST_F(DynamicModuleTransportSocketConfigTest, WithTransportSocketConfig) {
  auto config = buildProtoConfig(kReferenceModule, "xor");
  std::ignore =
      config.mutable_transport_socket_config()->PackFrom(ValueUtil::stringValue("config_bytes"));
  auto factory_or_error = upstream_factory_.createTransportSocketFactory(config, context_);
  EXPECT_OK(factory_or_error);
}

TEST_F(DynamicModuleTransportSocketConfigTest, InvalidModuleName) {
  auto config = buildProtoConfig("nonexistent_module", "passthrough");
  auto factory_or_error = upstream_factory_.createTransportSocketFactory(config, context_);
  EXPECT_THAT(factory_or_error, HasStatusMessage(HasSubstr("Failed to load dynamic module")));

  EXPECT_EQ(1U, failureCounter(context_.server_context_.serverScope(), "module_load_error",
                               "passthrough"));
}

TEST_F(DynamicModuleTransportSocketConfigTest, MissingTransportSocketSymbols) {
  // The C no_op module only implements HTTP filter symbols, so the transport socket entry points
  // cannot be resolved.
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                             TestEnvironment::substitute(kCModulesPath), 1);
  auto config = buildProtoConfig("no_op", "passthrough");
  auto factory_or_error = downstream_factory_.createTransportSocketFactory(config, context_, {});
  EXPECT_THAT(factory_or_error, HasStatusMessage(HasSubstr("Failed to resolve symbol")));

  // The module loads fine but its config creation fails resolving a symbol, counted as
  // config_init_error.
  auto& server_scope = context_.server_context_.serverScope();
  EXPECT_EQ(1U, failureCounter(server_scope, "config_init_error", "passthrough"));
  EXPECT_EQ(0U, failureCounter(server_scope, "module_load_error", "passthrough"));
}

TEST_F(DynamicModuleTransportSocketConfigTest, ConfigInitializationFailure) {
  // The reference module returns null for an unknown socket name.
  auto config = buildProtoConfig(kReferenceModule, "unknown_socket");
  auto factory_or_error = upstream_factory_.createTransportSocketFactory(config, context_);
  EXPECT_THAT(
      factory_or_error,
      HasStatusMessage(HasSubstr("Failed to initialize dynamic module transport socket config")));

  EXPECT_EQ(1U, failureCounter(context_.server_context_.serverScope(), "config_init_error",
                               "unknown_socket"));
}

TEST_F(DynamicModuleTransportSocketConfigTest, MalformedTransportSocketConfig) {
  // The module loads fine but the transport_socket_config Any cannot be unpacked, counted as
  // config_init_error. A malformed Any must be built programmatically.
  auto config = buildProtoConfig(kReferenceModule, "passthrough");
  auto* any = config.mutable_transport_socket_config();
  any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  any->set_value("invalid_binary_data_that_cannot_be_unpacked_as_string_value");

  auto factory_or_error = upstream_factory_.createTransportSocketFactory(config, context_);
  EXPECT_THAT(factory_or_error, Not(IsOk()));

  auto& server_scope = context_.server_context_.serverScope();
  EXPECT_EQ(1U, failureCounter(server_scope, "config_init_error", "passthrough"));
  EXPECT_EQ(0U, failureCounter(server_scope, "module_load_error", "passthrough"));
}

TEST_F(DynamicModuleTransportSocketConfigTest, DoNotCloseAndLoadGloballyOptions) {
  auto config = buildProtoConfig(kReferenceModule, "passthrough");
  config.mutable_dynamic_module_config()->set_do_not_close(true);
  config.mutable_dynamic_module_config()->set_load_globally(true);
  auto factory_or_error = upstream_factory_.createTransportSocketFactory(config, context_);
  EXPECT_OK(factory_or_error);
}

// Tests that exercise a transport socket instance against a mocked I/O handle.
class DynamicModuleTransportSocketTest : public testing::Test {
public:
  void SetUp() override {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(kRustModulesPath), 1);
    ON_CALL(callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));
    ON_CALL(io_handle_, isOpen()).WillByDefault(testing::Return(true));
  }

  Network::TransportSocketPtr createSocket(const std::string& socket_name,
                                           std::optional<std::string> config_bytes = std::nullopt,
                                           const std::string& module_name = kReferenceModule) {
    auto config = buildProtoConfig(module_name, socket_name);
    if (config_bytes.has_value()) {
      Protobuf::BytesValue bytes_value;
      bytes_value.set_value(*config_bytes);
      std::ignore = config.mutable_transport_socket_config()->PackFrom(bytes_value);
    }
    auto factory_or_error = factory_.createTransportSocketFactory(config, context_, {});
    EXPECT_OK(factory_or_error);
    factory_ptr_ = std::move(factory_or_error.value());
    auto socket = factory_ptr_->createDownstreamTransportSocket();
    socket->setTransportSocketCallbacks(callbacks_);
    return socket;
  }

  // Creates an upstream passthrough socket without callbacks, a peer that a downstream socket can
  // splice with.
  Network::TransportSocketPtr createUpstreamSocket() {
    auto factory_or_error = upstream_factory_.createTransportSocketFactory(
        buildProtoConfig(kReferenceModule, "passthrough"), context_);
    EXPECT_OK(factory_or_error);
    upstream_factory_ptr_ = std::move(factory_or_error.value());
    return upstream_factory_ptr_->createTransportSocket(nullptr, nullptr);
  }

  std::unique_ptr<DynamicModuleTransportSocket>
  createStubSocket(const envoy_dynamic_module_type_transport_socket_io_result& io_result) {
    auto config = std::make_shared<DynamicModuleTransportSocketConfig>(false, nullptr);
    config->in_module_config_ = &io_result;
    config->on_new_ = newStubTransportSocket;
    config->on_destroy_ = destroyStubTransportSocket;
    config->on_set_callbacks_ = setStubTransportSocketCallbacks;
    config->on_do_read_ = stubTransportSocketDoRead;
    config->on_do_write_ = stubTransportSocketDoWrite;
    return std::make_unique<DynamicModuleTransportSocket>(config);
  }

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  DownstreamDynamicModuleTransportSocketConfigFactory factory_;
  Network::DownstreamTransportSocketFactoryPtr factory_ptr_;
  UpstreamDynamicModuleTransportSocketConfigFactory upstream_factory_;
  Network::UpstreamTransportSocketFactoryPtr upstream_factory_ptr_;
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
  ASSERT_OK(factory_or_error);
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
  EXPECT_EQ(-1, envoy_dynamic_module_callback_transport_socket_get_fd(envoy_ptr));
  envoy_dynamic_module_callback_transport_socket_raise_event(
      envoy_ptr, envoy_dynamic_module_type_network_connection_event_RemoteClose);
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(envoy_ptr));
  envoy_dynamic_module_callback_transport_socket_set_is_readable(envoy_ptr);
  envoy_dynamic_module_callback_transport_socket_set_is_writable(envoy_ptr);
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

  io_bytes = 7;
  EXPECT_EQ(envoy_dynamic_module_type_transport_socket_io_status_Error,
            envoy_dynamic_module_callback_transport_socket_io_read_to_read_buffer(envoy_ptr, 4,
                                                                                  &io_bytes));
  EXPECT_EQ(0, io_bytes);
  io_bytes = 7;
  EXPECT_EQ(envoy_dynamic_module_type_transport_socket_io_status_Error,
            envoy_dynamic_module_callback_transport_socket_io_write_from_write_buffer(envoy_ptr,
                                                                                      &io_bytes));
  EXPECT_EQ(0, io_bytes);
  uint64_t connection_id = 7;
  EXPECT_FALSE(
      envoy_dynamic_module_callback_transport_socket_get_connection_id(envoy_ptr, &connection_id));
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_get_upstream_connection_id(
      envoy_ptr, &connection_id));
  EXPECT_EQ(7, connection_id);
  envoy_dynamic_module_callback_transport_socket_add_wire_bytes(envoy_ptr, 1, 1);
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
  EXPECT_EQ(0, envoy_dynamic_module_callback_transport_socket_write_buffer_length(envoy_ptr));

  // The buffer I/O callbacks report an error instead.
  size_t io_bytes = 7;
  EXPECT_EQ(envoy_dynamic_module_type_transport_socket_io_status_Error,
            envoy_dynamic_module_callback_transport_socket_io_read_to_read_buffer(envoy_ptr, 4,
                                                                                  &io_bytes));
  EXPECT_EQ(0, io_bytes);
  io_bytes = 7;
  EXPECT_EQ(envoy_dynamic_module_type_transport_socket_io_status_Error,
            envoy_dynamic_module_callback_transport_socket_io_write_from_write_buffer(envoy_ptr,
                                                                                      &io_bytes));
  EXPECT_EQ(0, io_bytes);
}

TEST_F(DynamicModuleTransportSocketTest, GetFdReturnsNativeDescriptor) {
  auto socket = createSocket("passthrough");
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());
  EXPECT_CALL(io_handle_, fdDoNotUse()).WillOnce(testing::Return(42));
  EXPECT_EQ(42, envoy_dynamic_module_callback_transport_socket_get_fd(envoy_ptr));
}

TEST_F(DynamicModuleTransportSocketTest, SetIsWritableActivatesWriteEvent) {
  auto socket = createSocket("passthrough");
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());
  EXPECT_CALL(io_handle_, activateFileEvents(Event::FileReadyType::Write));
  envoy_dynamic_module_callback_transport_socket_set_is_writable(envoy_ptr);
}

TEST_F(DynamicModuleTransportSocketTest, OnConnectedRecordsFailureWhenFdUnavailable) {
  auto socket = createSocket("passthrough");
  // The reference module records a failure when the socket descriptor is unavailable.
  EXPECT_CALL(io_handle_, fdDoNotUse()).WillOnce(testing::Return(-1));
  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  socket->onConnected();
  EXPECT_EQ("missing socket fd", socket->failureReason());
}

TEST_F(DynamicModuleTransportSocketTest, WakeupsBeforeConnectAndAfterCloseAreNoOps) {
  auto socket = createSocket("passthrough");
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());
  EXPECT_CALL(callbacks_, setTransportSocketIsReadable()).Times(0);
  EXPECT_CALL(io_handle_, activateFileEvents(testing::_)).Times(0);

  // An event before the connection completes would be taken for its completion.
  EXPECT_CALL(callbacks_.connection_, connecting()).WillRepeatedly(testing::Return(true));
  envoy_dynamic_module_callback_transport_socket_set_is_readable(envoy_ptr);
  envoy_dynamic_module_callback_transport_socket_set_is_writable(envoy_ptr);

  // A closed connection has no file event left to activate.
  EXPECT_CALL(callbacks_.connection_, connecting()).WillRepeatedly(testing::Return(false));
  EXPECT_CALL(io_handle_, isOpen()).WillRepeatedly(testing::Return(false));
  envoy_dynamic_module_callback_transport_socket_set_is_readable(envoy_ptr);
  envoy_dynamic_module_callback_transport_socket_set_is_writable(envoy_ptr);
}

TEST_F(DynamicModuleTransportSocketTest, GetConnectionIds) {
  auto socket = createSocket("passthrough");
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());
  ON_CALL(callbacks_.connection_, id()).WillByDefault(testing::Return(1234));
  uint64_t connection_id = 0;
  EXPECT_TRUE(
      envoy_dynamic_module_callback_transport_socket_get_connection_id(envoy_ptr, &connection_id));
  EXPECT_EQ(1234, connection_id);

  // The TCP proxy has not recorded an upstream connection yet.
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_get_upstream_connection_id(
      envoy_ptr, &connection_id));
  callbacks_.connection_.stream_info_.upstream_info_->setUpstreamConnectionId(42);
  EXPECT_TRUE(envoy_dynamic_module_callback_transport_socket_get_upstream_connection_id(
      envoy_ptr, &connection_id));
  EXPECT_EQ(42, connection_id);

  // A connection without upstream info has no upstream connection either.
  callbacks_.connection_.stream_info_.upstream_info_ = nullptr;
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_get_upstream_connection_id(
      envoy_ptr, &connection_id));
}

TEST_F(DynamicModuleTransportSocketTest, AddWireBytesCountsStreamInfoBytes) {
  auto socket = createSocket("passthrough");
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());
  auto& stream_info = callbacks_.connection_.stream_info_;
  stream_info.upstream_bytes_meter_ = std::make_shared<StreamInfo::BytesMeter>();

  // Without a downstream bytes meter, as on an upstream connection, only the totals grow.
  envoy_dynamic_module_callback_transport_socket_add_wire_bytes(envoy_ptr, 3, 4);
  EXPECT_EQ(3, stream_info.bytesReceived());
  EXPECT_EQ(4, stream_info.bytesSent());
  EXPECT_EQ(0, stream_info.upstream_bytes_meter_->wireBytesSent());
  EXPECT_EQ(0, stream_info.upstream_bytes_meter_->wireBytesReceived());

  // Like the TCP proxy, bytes received downstream count as sent upstream and the other way round.
  stream_info.downstream_bytes_meter_ = std::make_shared<StreamInfo::BytesMeter>();
  envoy_dynamic_module_callback_transport_socket_add_wire_bytes(envoy_ptr, 5, 6);
  EXPECT_EQ(8, stream_info.bytesReceived());
  EXPECT_EQ(10, stream_info.bytesSent());
  EXPECT_EQ(5, stream_info.downstream_bytes_meter_->wireBytesReceived());
  EXPECT_EQ(6, stream_info.downstream_bytes_meter_->wireBytesSent());
  EXPECT_EQ(5, stream_info.upstream_bytes_meter_->wireBytesSent());
  EXPECT_EQ(6, stream_info.upstream_bytes_meter_->wireBytesReceived());
}

TEST_F(DynamicModuleTransportSocketTest, StreamInfoIsUnavailableWithinOnSetCallbacks) {
  const envoy_dynamic_module_type_transport_socket_io_result io_result = {};
  auto socket = createStubSocket(io_result);
  EXPECT_CALL(callbacks_.connection_, streamInfo()).Times(0);
  socket->setTransportSocketCallbacks(callbacks_);
}

TEST_F(DynamicModuleTransportSocketTest, IoReadToReadBufferReadsIntoConnectionBuffer) {
  auto socket = createSocket("splice", std::nullopt, kSpliceModule);
  EXPECT_CALL(io_handle_, read(testing::_, testing::_))
      .WillOnce(Invoke([](Buffer::Instance& buffer, std::optional<uint64_t> max_length) {
        EXPECT_EQ(std::numeric_limits<size_t>::max(), max_length);
        buffer.add("hello");
        return Api::IoCallUint64Result(5, Api::IoError::none());
      }))
      .WillOnce(Invoke([](Buffer::Instance&, std::optional<uint64_t>) {
        return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEagainError());
      }));

  Buffer::OwnedImpl buffer;
  Network::IoResult result = socket->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(5, result.bytes_processed_);
  EXPECT_FALSE(result.end_stream_read_);
  EXPECT_EQ("hello", buffer.toString());
}

TEST_F(DynamicModuleTransportSocketTest, IoWriteFromWriteBufferWritesConnectionBuffer) {
  auto socket = createSocket("splice", std::nullopt, kSpliceModule);
  // The module writes until the callback reports the buffer empty, which it does without touching
  // the socket.
  EXPECT_CALL(io_handle_, write(testing::_)).WillOnce(Invoke([](Buffer::Instance& buffer) {
    const uint64_t length = buffer.length();
    buffer.drain(length);
    return Api::IoCallUint64Result(length, Api::IoError::none());
  }));

  Buffer::OwnedImpl buffer("world");
  Network::IoResult result = socket->doWrite(buffer, false);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(5, result.bytes_processed_);
  EXPECT_EQ(0, buffer.length());
}

TEST_F(DynamicModuleTransportSocketTest, ResetActionClosesWithConnectionReset) {
  const envoy_dynamic_module_type_transport_socket_io_result io_result = {
      envoy_dynamic_module_type_transport_socket_post_io_action_Reset, 3, true};
  auto socket = createStubSocket(io_result);
  socket->setTransportSocketCallbacks(callbacks_);
  Buffer::OwnedImpl buffer;

  // Only a read reports the end of stream.
  Network::IoResult result = socket->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  EXPECT_EQ(3, result.bytes_processed_);
  EXPECT_TRUE(result.end_stream_read_);
  EXPECT_EQ(Api::IoError::IoErrorCode::ConnectionReset, result.err_code_);
  result = socket->doWrite(buffer, false);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  EXPECT_EQ(3, result.bytes_processed_);
  EXPECT_FALSE(result.end_stream_read_);
  EXPECT_EQ(Api::IoError::IoErrorCode::ConnectionReset, result.err_code_);
}

TEST_F(DynamicModuleTransportSocketTest, CloseAfterPeerResetClosesWithConnectionReset) {
  auto socket = createSocket("splice", std::nullopt, kSpliceModule);
  EXPECT_CALL(io_handle_, read(testing::_, testing::_))
      .Times(2)
      .WillRepeatedly(Invoke([](Buffer::Instance&, std::optional<uint64_t>) {
        return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEbadfError());
      }));
  EXPECT_CALL(io_handle_, write(testing::_)).WillOnce(Invoke([](Buffer::Instance&) {
    return Api::IoCallUint64Result(0, Network::IoSocketError::create(SOCKET_ERROR_CONNRESET));
  }));
  Buffer::OwnedImpl read_buffer;
  Buffer::OwnedImpl write_buffer("world");

  // The module only sees errors and closes. Envoy reports the close as a reset once a buffer I/O
  // callback saw the peer reset the connection.
  Network::IoResult result = socket->doRead(read_buffer);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  EXPECT_FALSE(result.err_code_.has_value());
  result = socket->doWrite(write_buffer, false);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  EXPECT_EQ(Api::IoError::IoErrorCode::ConnectionReset, result.err_code_);
  result = socket->doRead(read_buffer);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  EXPECT_EQ(Api::IoError::IoErrorCode::ConnectionReset, result.err_code_);
}

TEST_F(DynamicModuleTransportSocketTest, StartSpliceRejectsNullPeer) {
  auto socket = createSocket("passthrough");
  auto* self = static_cast<DynamicModuleTransportSocket*>(socket.get());
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_start_splice(self, nullptr));
}

TEST_F(DynamicModuleTransportSocketTest, StartSpliceRejectsPeerWithoutCallbacks) {
  auto self_socket = createSocket("passthrough");
  // A peer whose callbacks were never set has a null callbacks pointer.
  auto peer_socket = createUpstreamSocket();
  auto* self = static_cast<DynamicModuleTransportSocket*>(self_socket.get());
  auto* peer = static_cast<DynamicModuleTransportSocket*>(peer_socket.get());
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_start_splice(self, peer));
}

TEST_F(DynamicModuleTransportSocketTest, StartSpliceRejectsClosedHandle) {
  auto self_socket = createSocket("passthrough");
  auto peer_socket = createUpstreamSocket();
  peer_socket->setTransportSocketCallbacks(callbacks_);
  auto* self = static_cast<DynamicModuleTransportSocket*>(self_socket.get());
  auto* peer = static_cast<DynamicModuleTransportSocket*>(peer_socket.get());
  EXPECT_CALL(io_handle_, isOpen()).WillRepeatedly(testing::Return(false));
  EXPECT_CALL(io_handle_, fdDoNotUse()).Times(0);
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_start_splice(self, peer));
}

TEST_F(DynamicModuleTransportSocketTest, StartSpliceRejectsConnectingConnection) {
  auto self_socket = createSocket("passthrough");
  auto peer_socket = createUpstreamSocket();
  peer_socket->setTransportSocketCallbacks(callbacks_);
  auto* self = static_cast<DynamicModuleTransportSocket*>(self_socket.get());
  auto* peer = static_cast<DynamicModuleTransportSocket*>(peer_socket.get());
  ON_CALL(callbacks_.connection_, connecting()).WillByDefault(testing::Return(true));
  EXPECT_CALL(io_handle_, fdDoNotUse()).Times(0);
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_start_splice(self, peer));
}

TEST_F(DynamicModuleTransportSocketTest, StartSpliceRejectsItselfAsPeer) {
  auto socket = createSocket("passthrough");
  auto* self = static_cast<DynamicModuleTransportSocket*>(socket.get());
  EXPECT_CALL(io_handle_, fdDoNotUse()).Times(0);
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_start_splice(self, self));
}

TEST_F(DynamicModuleTransportSocketTest, StartSpliceRejectsPeerOnTheSameSide) {
  auto self_socket = createSocket("passthrough");
  auto peer_socket = factory_ptr_->createDownstreamTransportSocket();
  peer_socket->setTransportSocketCallbacks(callbacks_);
  auto* self = static_cast<DynamicModuleTransportSocket*>(self_socket.get());
  auto* peer = static_cast<DynamicModuleTransportSocket*>(peer_socket.get());
  EXPECT_CALL(io_handle_, fdDoNotUse()).Times(0);
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_start_splice(self, peer));
}

TEST_F(DynamicModuleTransportSocketTest, StartSpliceRejectsSplicedSocket) {
  auto self_socket = createSocket("passthrough");
  auto peer_socket = createUpstreamSocket();
  peer_socket->setTransportSocketCallbacks(callbacks_);
  auto* self = static_cast<DynamicModuleTransportSocket*>(self_socket.get());
  auto* peer = static_cast<DynamicModuleTransportSocket*>(peer_socket.get());
  peer->markSpliced();
  EXPECT_CALL(io_handle_, fdDoNotUse()).Times(0);
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_start_splice(self, peer));
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_start_splice(peer, self));
}

TEST_F(DynamicModuleTransportSocketTest, StartSpliceRejectsPeerOnAnotherDispatcher) {
  auto self_socket = createSocket("passthrough");
  // A separate mock connection has a dispatcher of its own.
  NiceMock<Network::MockTransportSocketCallbacks> peer_callbacks;
  ON_CALL(peer_callbacks, ioHandle()).WillByDefault(ReturnRef(io_handle_));
  auto peer_socket = createUpstreamSocket();
  peer_socket->setTransportSocketCallbacks(peer_callbacks);
  auto* self = static_cast<DynamicModuleTransportSocket*>(self_socket.get());
  auto* peer = static_cast<DynamicModuleTransportSocket*>(peer_socket.get());
  EXPECT_CALL(io_handle_, fdDoNotUse()).Times(0);
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_start_splice(self, peer));
}

// A secure downstream decrypts in the module, which the splice would bypass.
TEST_F(DynamicModuleTransportSocketTest, StartSpliceRejectsSecureDownstream) {
  auto config = buildProtoConfig(kReferenceModule, "passthrough");
  config.set_implements_secure_transport(true);
  auto factory_or_error = factory_.createTransportSocketFactory(config, context_, {});
  ASSERT_OK(factory_or_error);
  factory_ptr_ = std::move(factory_or_error.value());
  auto self_socket = factory_ptr_->createDownstreamTransportSocket();
  self_socket->setTransportSocketCallbacks(callbacks_);
  auto peer_socket = createUpstreamSocket();
  peer_socket->setTransportSocketCallbacks(callbacks_);
  auto* self = static_cast<DynamicModuleTransportSocket*>(self_socket.get());
  auto* peer = static_cast<DynamicModuleTransportSocket*>(peer_socket.get());
  EXPECT_CALL(io_handle_, fdDoNotUse()).Times(0);
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_start_splice(self, peer));
}

// Bytes still in the buffer of the read or write that splices would stay in Envoy.
TEST_F(DynamicModuleTransportSocketTest, StartSpliceRejectsPendingBytes) {
  SplicingSocket splicing;
  auto config = std::make_shared<DynamicModuleTransportSocketConfig>(false, nullptr);
  config->in_module_config_ = &splicing;
  config->on_new_ = newStubTransportSocket;
  config->on_destroy_ = destroyStubTransportSocket;
  config->on_set_callbacks_ = setStubTransportSocketCallbacks;
  config->on_do_read_ = spliceFromDoRead;
  config->on_do_write_ = spliceFromDoWrite;
  DynamicModuleTransportSocket self(config);
  self.setTransportSocketCallbacks(callbacks_);
  auto peer_socket = createUpstreamSocket();
  peer_socket->setTransportSocketCallbacks(callbacks_);
  splicing.peer = static_cast<DynamicModuleTransportSocket*>(peer_socket.get());
  EXPECT_CALL(io_handle_, fdDoNotUse()).Times(0);
  Buffer::OwnedImpl read_buffer("x");
  self.doRead(read_buffer);
  EXPECT_FALSE(splicing.started);
  splicing.started = true;
  Buffer::OwnedImpl write_buffer("x");
  self.doWrite(write_buffer, false);
  EXPECT_FALSE(splicing.started);
}

// A pair that passes every check but cannot be prepared stays on the buffered path untouched.
TEST_F(DynamicModuleTransportSocketTest, StartSpliceFallsBackWhenPrepareFails) {
  auto self_socket = createSocket("passthrough");
  auto peer_socket = createUpstreamSocket();
  peer_socket->setTransportSocketCallbacks(callbacks_);
  auto* self = static_cast<DynamicModuleTransportSocket*>(self_socket.get());
  auto* peer = static_cast<DynamicModuleTransportSocket*>(peer_socket.get());
  // Both fds are read, so every guard passed, and an invalid fd then fails the non-blocking check.
  // The session asks for its teardown callback first.
  EXPECT_CALL(io_handle_, fdDoNotUse()).Times(2).WillRepeatedly(testing::Return(-1));
  callbacks_.connection_.dispatcher_.allow_null_callback_ = true;
  EXPECT_CALL(io_handle_, resetFileEvents()).Times(0);
  EXPECT_CALL(callbacks_.connection_, addConnectionCallbacks(testing::_)).Times(0);
  EXPECT_FALSE(envoy_dynamic_module_callback_transport_socket_start_splice(self, peer));
  EXPECT_FALSE(self->spliced());
  EXPECT_FALSE(peer->spliced());
}

// Once the pump owns the fd, a late read, write, wakeup, or flush must not touch it.
TEST_F(DynamicModuleTransportSocketTest, IsInertOnceSpliced) {
  auto socket = createSocket("passthrough");
  auto* envoy_ptr = static_cast<DynamicModuleTransportSocket*>(socket.get());
  envoy_ptr->markSpliced();
  EXPECT_CALL(io_handle_, recv(testing::_, testing::_, testing::_)).Times(0);
  EXPECT_CALL(io_handle_, writev(testing::_, testing::_)).Times(0);
  EXPECT_CALL(io_handle_, shutdown(testing::_)).Times(0);
  EXPECT_CALL(callbacks_, setTransportSocketIsReadable()).Times(0);
  EXPECT_CALL(io_handle_, activateFileEvents(testing::_)).Times(0);
  EXPECT_CALL(callbacks_, flushWriteBuffer()).Times(0);
  Buffer::OwnedImpl read_buffer;
  Buffer::OwnedImpl write_buffer("data");
  Network::IoResult result = socket->doRead(read_buffer);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(0, result.bytes_processed_);
  result = socket->doWrite(write_buffer, true);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(0, result.bytes_processed_);
  envoy_dynamic_module_callback_transport_socket_set_is_readable(envoy_ptr);
  envoy_dynamic_module_callback_transport_socket_set_is_writable(envoy_ptr);
  envoy_dynamic_module_callback_transport_socket_flush_write_buffer(envoy_ptr);
}
} // namespace
} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

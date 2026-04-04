#include <cerrno>

#include "envoy/extensions/transport_sockets/rustls/v3/rustls.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/transport_sockets/rustls/rustls_impl.h"

#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Rustls {
namespace {

// ---------------------------------------------------------------------------
// Stub ABI functions for unit tests.
// ---------------------------------------------------------------------------
namespace stub {

envoy_dynamic_module_type_transport_socket_module_ptr
socketNewReturnNull(envoy_dynamic_module_type_transport_socket_factory_config_module_ptr,
                    envoy_dynamic_module_type_transport_socket_envoy_ptr) {
  return nullptr;
}

envoy_dynamic_module_type_transport_socket_module_ptr
socketNewReturnSentinel(envoy_dynamic_module_type_transport_socket_factory_config_module_ptr,
                        envoy_dynamic_module_type_transport_socket_envoy_ptr) {
  // Return a non-null sentinel to simulate a successfully created module socket.
  return reinterpret_cast<envoy_dynamic_module_type_transport_socket_module_ptr>(0xDEADBEEF);
}

void socketDestroy(envoy_dynamic_module_type_transport_socket_module_ptr) {}

void setCallbacks(envoy_dynamic_module_type_transport_socket_envoy_ptr,
                  envoy_dynamic_module_type_transport_socket_module_ptr) {}

void onConnected(envoy_dynamic_module_type_transport_socket_envoy_ptr,
                 envoy_dynamic_module_type_transport_socket_module_ptr) {}

envoy_dynamic_module_type_transport_socket_io_result
doReadKeepOpen(envoy_dynamic_module_type_transport_socket_envoy_ptr,
               envoy_dynamic_module_type_transport_socket_module_ptr) {
  return {envoy_dynamic_module_type_transport_socket_post_io_action_KeepOpen, 10, false};
}

envoy_dynamic_module_type_transport_socket_io_result
doReadClose(envoy_dynamic_module_type_transport_socket_envoy_ptr,
            envoy_dynamic_module_type_transport_socket_module_ptr) {
  return {envoy_dynamic_module_type_transport_socket_post_io_action_Close, 0, true};
}

envoy_dynamic_module_type_transport_socket_io_result
doWriteKeepOpen(envoy_dynamic_module_type_transport_socket_envoy_ptr,
                envoy_dynamic_module_type_transport_socket_module_ptr, size_t, bool) {
  return {envoy_dynamic_module_type_transport_socket_post_io_action_KeepOpen, 5, false};
}

envoy_dynamic_module_type_transport_socket_io_result
doWriteClose(envoy_dynamic_module_type_transport_socket_envoy_ptr,
             envoy_dynamic_module_type_transport_socket_module_ptr, size_t, bool) {
  return {envoy_dynamic_module_type_transport_socket_post_io_action_Close, 0, false};
}

void closeSocket(envoy_dynamic_module_type_transport_socket_envoy_ptr,
                 envoy_dynamic_module_type_transport_socket_module_ptr,
                 envoy_dynamic_module_type_network_connection_event) {}

void getProtocol(envoy_dynamic_module_type_transport_socket_envoy_ptr,
                 envoy_dynamic_module_type_transport_socket_module_ptr,
                 envoy_dynamic_module_type_module_buffer* out) {
  static const char kProtocol[] = "h2";
  out->ptr = kProtocol;
  out->length = 2;
}

void getProtocolEmpty(envoy_dynamic_module_type_transport_socket_envoy_ptr,
                      envoy_dynamic_module_type_transport_socket_module_ptr,
                      envoy_dynamic_module_type_module_buffer* out) {
  out->ptr = nullptr;
  out->length = 0;
}

void getFailureReason(envoy_dynamic_module_type_transport_socket_envoy_ptr,
                      envoy_dynamic_module_type_transport_socket_module_ptr,
                      envoy_dynamic_module_type_module_buffer* out) {
  static const char kReason[] = "handshake failed";
  out->ptr = kReason;
  out->length = 16;
}

void getFailureReasonEmpty(envoy_dynamic_module_type_transport_socket_envoy_ptr,
                           envoy_dynamic_module_type_transport_socket_module_ptr,
                           envoy_dynamic_module_type_module_buffer* out) {
  out->ptr = nullptr;
  out->length = 0;
}

bool canFlushCloseTrue(envoy_dynamic_module_type_transport_socket_envoy_ptr,
                       envoy_dynamic_module_type_transport_socket_module_ptr) {
  return true;
}

bool canFlushCloseFalse(envoy_dynamic_module_type_transport_socket_envoy_ptr,
                        envoy_dynamic_module_type_transport_socket_module_ptr) {
  return false;
}

void factoryConfigDestroy(envoy_dynamic_module_type_transport_socket_factory_config_module_ptr) {}

} // namespace stub

// Creates a config with stub function pointers for unit testing without loading a real module.
RustlsTransportSocketConfigSharedPtr createStubConfig(bool is_upstream) {
  auto config = std::make_shared<RustlsTransportSocketConfig>(is_upstream, "test_socket",
                                                              "test_config", nullptr);
  config->on_socket_new_ = stub::socketNewReturnSentinel;
  config->on_socket_destroy_ = stub::socketDestroy;
  config->on_set_callbacks_ = stub::setCallbacks;
  config->on_on_connected_ = stub::onConnected;
  config->on_do_read_ = stub::doReadKeepOpen;
  config->on_do_write_ = stub::doWriteKeepOpen;
  config->on_close_ = stub::closeSocket;
  config->on_get_protocol_ = stub::getProtocol;
  config->on_get_failure_reason_ = stub::getFailureReason;
  config->on_can_flush_close_ = stub::canFlushCloseTrue;
  config->on_factory_config_destroy_ = stub::factoryConfigDestroy;
  return config;
}

// Creates a config where on_socket_new returns nullptr to simulate module failure.
RustlsTransportSocketConfigSharedPtr createNullModuleConfig() {
  auto config =
      std::make_shared<RustlsTransportSocketConfig>(true, "test_socket", "test_config", nullptr);
  config->on_socket_new_ = stub::socketNewReturnNull;
  config->on_socket_destroy_ = stub::socketDestroy;
  return config;
}

// ===========================================================================
// Factory registration and config tests (using the real Rust module).
// ===========================================================================

class RustlsImplTest : public testing::Test {
public:
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
};

TEST_F(RustlsImplTest, UpstreamFactoryRegistration) {
  RustlsUpstreamTransportSocketConfigFactory factory;
  EXPECT_EQ(factory.name(), "envoy.transport_sockets.rustls");
  EXPECT_NE(factory.createEmptyConfigProto(), nullptr);
}

TEST_F(RustlsImplTest, DownstreamFactoryRegistration) {
  RustlsDownstreamTransportSocketConfigFactory factory;
  EXPECT_EQ(factory.name(), "envoy.transport_sockets.rustls");
  EXPECT_NE(factory.createEmptyConfigProto(), nullptr);
}

TEST_F(RustlsImplTest, UpstreamDefaultConfig) {
  RustlsUpstreamTransportSocketConfigFactory factory;

  envoy::extensions::transport_sockets::rustls::v3::RustlsUpstreamTlsContext config;
  auto result = factory.createTransportSocketFactory(config, context_);
  EXPECT_TRUE(result.ok()) << result.status().message();
}

TEST_F(RustlsImplTest, UpstreamWithSni) {
  RustlsUpstreamTransportSocketConfigFactory factory;

  envoy::extensions::transport_sockets::rustls::v3::RustlsUpstreamTlsContext config;
  config.set_sni("example.com");
  config.add_alpn_protocols("h2");
  config.add_alpn_protocols("http/1.1");

  auto result = factory.createTransportSocketFactory(config, context_);
  EXPECT_TRUE(result.ok()) << result.status().message();
}

TEST_F(RustlsImplTest, UpstreamWithClientCert) {
  RustlsUpstreamTransportSocketConfigFactory factory;

  const std::string cert_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_cert.pem");
  const std::string key_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_key.pem");

  envoy::extensions::transport_sockets::rustls::v3::RustlsUpstreamTlsContext config;
  config.set_cert_chain(cert_path);
  config.set_private_key(key_path);

  auto result = factory.createTransportSocketFactory(config, context_);
  EXPECT_TRUE(result.ok()) << result.status().message();
}

TEST_F(RustlsImplTest, UpstreamWithCustomTrustedCa) {
  RustlsUpstreamTransportSocketConfigFactory factory;

  const std::string ca_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/ca_cert.pem");

  envoy::extensions::transport_sockets::rustls::v3::RustlsUpstreamTlsContext config;
  config.set_trusted_ca(ca_path);
  config.set_sni("example.com");

  auto result = factory.createTransportSocketFactory(config, context_);
  EXPECT_TRUE(result.ok()) << result.status().message();
}

TEST_F(RustlsImplTest, DownstreamWithValidCerts) {
  RustlsDownstreamTransportSocketConfigFactory factory;

  const std::string cert_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_cert.pem");
  const std::string key_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_key.pem");

  envoy::extensions::transport_sockets::rustls::v3::RustlsDownstreamTlsContext config;
  config.set_cert_chain(cert_path);
  config.set_private_key(key_path);

  auto result = factory.createTransportSocketFactory(config, context_, {});
  EXPECT_TRUE(result.ok()) << result.status().message();
}

TEST_F(RustlsImplTest, DownstreamMissingCertsFailsInModule) {
  RustlsDownstreamTransportSocketConfigFactory factory;

  envoy::extensions::transport_sockets::rustls::v3::RustlsDownstreamTlsContext config;

  auto result = factory.createTransportSocketFactory(config, context_, {});
  EXPECT_FALSE(result.ok());
}

TEST_F(RustlsImplTest, DownstreamWithAlpn) {
  RustlsDownstreamTransportSocketConfigFactory factory;

  const std::string cert_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_cert.pem");
  const std::string key_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_key.pem");

  envoy::extensions::transport_sockets::rustls::v3::RustlsDownstreamTlsContext config;
  config.set_cert_chain(cert_path);
  config.set_private_key(key_path);
  config.add_alpn_protocols("h2");

  auto result = factory.createTransportSocketFactory(config, context_, {});
  EXPECT_TRUE(result.ok()) << result.status().message();
}

TEST_F(RustlsImplTest, DownstreamWithTrustedCa) {
  RustlsDownstreamTransportSocketConfigFactory factory;

  const std::string cert_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_cert.pem");
  const std::string key_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_key.pem");
  const std::string ca_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/ca_cert.pem");

  envoy::extensions::transport_sockets::rustls::v3::RustlsDownstreamTlsContext config;
  config.set_cert_chain(cert_path);
  config.set_private_key(key_path);
  config.set_trusted_ca(ca_path);

  auto result = factory.createTransportSocketFactory(config, context_, {});
  EXPECT_TRUE(result.ok()) << result.status().message();
}

TEST_F(RustlsImplTest, DownstreamWithKtlsEnabled) {
  RustlsDownstreamTransportSocketConfigFactory factory;

  const std::string cert_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_cert.pem");
  const std::string key_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_key.pem");

  envoy::extensions::transport_sockets::rustls::v3::RustlsDownstreamTlsContext config;
  config.set_cert_chain(cert_path);
  config.set_private_key(key_path);
  config.set_enable_ktls(true);

  auto result = factory.createTransportSocketFactory(config, context_, {});
  EXPECT_TRUE(result.ok()) << result.status().message();
}

TEST_F(RustlsImplTest, DownstreamWithKtlsTxOnly) {
  RustlsDownstreamTransportSocketConfigFactory factory;

  const std::string cert_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_cert.pem");
  const std::string key_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_key.pem");

  envoy::extensions::transport_sockets::rustls::v3::RustlsDownstreamTlsContext config;
  config.set_cert_chain(cert_path);
  config.set_private_key(key_path);
  config.set_enable_ktls(true);
  config.set_disable_ktls_rx(true);

  auto result = factory.createTransportSocketFactory(config, context_, {});
  EXPECT_TRUE(result.ok()) << result.status().message();
}

TEST_F(RustlsImplTest, UpstreamWithKtlsEnabled) {
  RustlsUpstreamTransportSocketConfigFactory factory;

  envoy::extensions::transport_sockets::rustls::v3::RustlsUpstreamTlsContext config;
  config.set_enable_ktls(true);

  auto result = factory.createTransportSocketFactory(config, context_);
  EXPECT_TRUE(result.ok()) << result.status().message();
}

TEST_F(RustlsImplTest, UpstreamWithKtlsTxOnly) {
  RustlsUpstreamTransportSocketConfigFactory factory;

  envoy::extensions::transport_sockets::rustls::v3::RustlsUpstreamTlsContext config;
  config.set_enable_ktls(true);
  config.set_disable_ktls_rx(true);

  auto result = factory.createTransportSocketFactory(config, context_);
  EXPECT_TRUE(result.ok()) << result.status().message();
}

TEST_F(RustlsImplTest, DisableKtlsRxWithoutEnableKtlsIsNoOp) {
  RustlsUpstreamTransportSocketConfigFactory factory;

  envoy::extensions::transport_sockets::rustls::v3::RustlsUpstreamTlsContext config;
  config.set_disable_ktls_rx(true);

  auto result = factory.createTransportSocketFactory(config, context_);
  EXPECT_TRUE(result.ok()) << result.status().message();
}

TEST_F(RustlsImplTest, UpstreamFactoryCreatesWorkingTransportSocket) {
  RustlsUpstreamTransportSocketConfigFactory factory;

  envoy::extensions::transport_sockets::rustls::v3::RustlsUpstreamTlsContext config;
  config.set_sni("example.com");
  config.add_alpn_protocols("h2");

  auto result = factory.createTransportSocketFactory(config, context_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto& upstream_factory = *result.value();
  EXPECT_TRUE(upstream_factory.implementsSecureTransport());

  auto socket = upstream_factory.createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);
  EXPECT_EQ(socket->ssl(), nullptr);
  EXPECT_FALSE(socket->startSecureTransport());
}

TEST_F(RustlsImplTest, UpstreamFactorySupportsAlpn) {
  RustlsUpstreamTransportSocketConfigFactory factory;

  envoy::extensions::transport_sockets::rustls::v3::RustlsUpstreamTlsContext config;
  config.set_sni("example.com");
  config.add_alpn_protocols("h2");

  auto result = factory.createTransportSocketFactory(config, context_);
  ASSERT_TRUE(result.ok());

  auto* upstream = dynamic_cast<RustlsUpstreamTransportSocketFactory*>(result.value().get());
  ASSERT_NE(upstream, nullptr);
  EXPECT_TRUE(upstream->supportsAlpn());
  EXPECT_EQ(upstream->defaultServerNameIndication(), "example.com");
}

TEST_F(RustlsImplTest, UpstreamFactoryNoAlpn) {
  RustlsUpstreamTransportSocketConfigFactory factory;

  envoy::extensions::transport_sockets::rustls::v3::RustlsUpstreamTlsContext config;

  auto result = factory.createTransportSocketFactory(config, context_);
  ASSERT_TRUE(result.ok());

  auto* upstream = dynamic_cast<RustlsUpstreamTransportSocketFactory*>(result.value().get());
  ASSERT_NE(upstream, nullptr);
  EXPECT_FALSE(upstream->supportsAlpn());
  EXPECT_TRUE(upstream->defaultServerNameIndication().empty());
}

TEST_F(RustlsImplTest, UpstreamFactoryHashKey) {
  RustlsUpstreamTransportSocketConfigFactory factory;

  envoy::extensions::transport_sockets::rustls::v3::RustlsUpstreamTlsContext config;
  config.set_sni("example.com");
  config.add_alpn_protocols("h2");

  auto result = factory.createTransportSocketFactory(config, context_);
  ASSERT_TRUE(result.ok());

  std::vector<uint8_t> key;
  result.value()->hashKey(key, nullptr);
  EXPECT_FALSE(key.empty());
}

TEST_F(RustlsImplTest, DownstreamFactoryCreatesWorkingTransportSocket) {
  RustlsDownstreamTransportSocketConfigFactory factory;

  const std::string cert_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_cert.pem");
  const std::string key_path =
      TestEnvironment::runfilesPath("test/common/tls/test_data/selfsigned_key.pem");

  envoy::extensions::transport_sockets::rustls::v3::RustlsDownstreamTlsContext config;
  config.set_cert_chain(cert_path);
  config.set_private_key(key_path);

  auto result = factory.createTransportSocketFactory(config, context_, {});
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_TRUE(result.value()->implementsSecureTransport());

  auto socket = result.value()->createDownstreamTransportSocket();
  ASSERT_NE(socket, nullptr);
  EXPECT_EQ(socket->ssl(), nullptr);
}

// ===========================================================================
// RustlsTransportSocketConfig tests.
// ===========================================================================

class RustlsTransportSocketConfigTest : public testing::Test {};

TEST_F(RustlsTransportSocketConfigTest, IsUpstream) {
  auto config = createStubConfig(/*is_upstream=*/true);
  EXPECT_TRUE(config->isUpstream());
}

TEST_F(RustlsTransportSocketConfigTest, IsDownstream) {
  auto config = createStubConfig(/*is_upstream=*/false);
  EXPECT_FALSE(config->isUpstream());
}

TEST_F(RustlsTransportSocketConfigTest, SocketNameAndConfigBytes) {
  auto config = createStubConfig(/*is_upstream=*/true);
  EXPECT_EQ(config->socketName(), "test_socket");
  EXPECT_EQ(config->socketConfigBytes(), "test_config");
}

// ===========================================================================
// RustlsTransportSocket tests with null socket_module_.
// ===========================================================================

class RustlsTransportSocketNullModuleTest : public testing::Test {
public:
  RustlsTransportSocketConfigSharedPtr config_ = createNullModuleConfig();
};

TEST_F(RustlsTransportSocketNullModuleTest, ConstructorLogsError) {
  EXPECT_LOG_CONTAINS("error",
                      "dynamic module transport socket: on_transport_socket_new returned nullptr",
                      { auto socket = std::make_unique<RustlsTransportSocket>(config_, true); });
}

TEST_F(RustlsTransportSocketNullModuleTest, DoReadReturnsCloseAction) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  Buffer::OwnedImpl buffer;
  auto result = socket->doRead(buffer);
  EXPECT_EQ(result.action_, Network::PostIoAction::Close);
  EXPECT_EQ(result.bytes_processed_, 0);
  EXPECT_FALSE(result.end_stream_read_);
}

TEST_F(RustlsTransportSocketNullModuleTest, DoWriteReturnsCloseAction) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  Buffer::OwnedImpl buffer;
  auto result = socket->doWrite(buffer, false);
  EXPECT_EQ(result.action_, Network::PostIoAction::Close);
  EXPECT_EQ(result.bytes_processed_, 0);
}

TEST_F(RustlsTransportSocketNullModuleTest, CloseSocketIsNoOp) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  socket->closeSocket(Network::ConnectionEvent::LocalClose);
}

TEST_F(RustlsTransportSocketNullModuleTest, OnConnectedIsNoOp) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  socket->onConnected();
}

TEST_F(RustlsTransportSocketNullModuleTest, SetCallbacksStoresCallbacks) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  socket->setTransportSocketCallbacks(callbacks);
  EXPECT_EQ(socket->transportCallbacks(), &callbacks);
}

TEST_F(RustlsTransportSocketNullModuleTest, ProtocolReturnsEmpty) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  EXPECT_TRUE(socket->protocol().empty());
}

TEST_F(RustlsTransportSocketNullModuleTest, FailureReasonReturnsEmpty) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  EXPECT_TRUE(socket->failureReason().empty());
}

TEST_F(RustlsTransportSocketNullModuleTest, CanFlushCloseReturnsTrue) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  EXPECT_TRUE(socket->canFlushClose());
}

TEST_F(RustlsTransportSocketNullModuleTest, SslReturnsNullptr) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  EXPECT_EQ(socket->ssl(), nullptr);
}

TEST_F(RustlsTransportSocketNullModuleTest, StartSecureTransportReturnsFalse) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  EXPECT_FALSE(socket->startSecureTransport());
}

TEST_F(RustlsTransportSocketNullModuleTest, ConfigureInitialCongestionWindowIsNoOp) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  socket->configureInitialCongestionWindow(100, std::chrono::microseconds(1000));
}

// ===========================================================================
// RustlsTransportSocket tests with valid stub module.
// ===========================================================================

class RustlsTransportSocketValidModuleTest : public testing::Test {
public:
  RustlsTransportSocketConfigSharedPtr config_ = createStubConfig(/*is_upstream=*/true);
};

TEST_F(RustlsTransportSocketValidModuleTest, DoReadKeepOpen) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  Buffer::OwnedImpl buffer;
  auto result = socket->doRead(buffer);
  EXPECT_EQ(result.action_, Network::PostIoAction::KeepOpen);
  EXPECT_EQ(result.bytes_processed_, 10);
  EXPECT_FALSE(result.end_stream_read_);
}

TEST_F(RustlsTransportSocketValidModuleTest, DoReadCloseAction) {
  config_->on_do_read_ = stub::doReadClose;
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  Buffer::OwnedImpl buffer;
  auto result = socket->doRead(buffer);
  EXPECT_EQ(result.action_, Network::PostIoAction::Close);
  EXPECT_EQ(result.bytes_processed_, 0);
  EXPECT_TRUE(result.end_stream_read_);
}

TEST_F(RustlsTransportSocketValidModuleTest, DoWriteKeepOpen) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  Buffer::OwnedImpl buffer("hello");
  auto result = socket->doWrite(buffer, false);
  EXPECT_EQ(result.action_, Network::PostIoAction::KeepOpen);
  EXPECT_EQ(result.bytes_processed_, 5);
}

TEST_F(RustlsTransportSocketValidModuleTest, DoWriteCloseAction) {
  config_->on_do_write_ = stub::doWriteClose;
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  Buffer::OwnedImpl buffer("hello");
  auto result = socket->doWrite(buffer, true);
  EXPECT_EQ(result.action_, Network::PostIoAction::Close);
  EXPECT_EQ(result.bytes_processed_, 0);
}

TEST_F(RustlsTransportSocketValidModuleTest, OnConnectedCallsModule) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  socket->onConnected();
}

TEST_F(RustlsTransportSocketValidModuleTest, CloseSocketCallsModule) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  socket->closeSocket(Network::ConnectionEvent::LocalClose);
}

TEST_F(RustlsTransportSocketValidModuleTest, ProtocolReturnsValue) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  EXPECT_EQ(socket->protocol(), "h2");
}

TEST_F(RustlsTransportSocketValidModuleTest, ProtocolReturnsEmptyWhenModuleReturnsEmpty) {
  config_->on_get_protocol_ = stub::getProtocolEmpty;
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  EXPECT_TRUE(socket->protocol().empty());
}

TEST_F(RustlsTransportSocketValidModuleTest, FailureReasonReturnsValue) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  EXPECT_EQ(socket->failureReason(), "handshake failed");
}

TEST_F(RustlsTransportSocketValidModuleTest, FailureReasonReturnsEmptyWhenModuleReturnsEmpty) {
  config_->on_get_failure_reason_ = stub::getFailureReasonEmpty;
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  EXPECT_TRUE(socket->failureReason().empty());
}

TEST_F(RustlsTransportSocketValidModuleTest, CanFlushCloseReturnsTrue) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  EXPECT_TRUE(socket->canFlushClose());
}

TEST_F(RustlsTransportSocketValidModuleTest, CanFlushCloseReturnsFalse) {
  config_->on_can_flush_close_ = stub::canFlushCloseFalse;
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  EXPECT_FALSE(socket->canFlushClose());
}

TEST_F(RustlsTransportSocketValidModuleTest, SetCallbacksCallsModule) {
  auto socket = std::make_unique<RustlsTransportSocket>(config_, true);
  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  socket->setTransportSocketCallbacks(callbacks);
  EXPECT_EQ(socket->transportCallbacks(), &callbacks);
}

// ===========================================================================
// RustlsUpstreamTransportSocketFactory tests.
// ===========================================================================

class RustlsUpstreamFactoryTest : public testing::Test {
public:
  RustlsTransportSocketConfigSharedPtr config_ = createStubConfig(/*is_upstream=*/true);
};

TEST_F(RustlsUpstreamFactoryTest, CreateTransportSocket) {
  RustlsUpstreamTransportSocketFactory factory(config_, "example.com", {"h2"}, true);
  auto socket = factory.createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);
}

TEST_F(RustlsUpstreamFactoryTest, ImplementsSecureTransportTrue) {
  RustlsUpstreamTransportSocketFactory factory(config_, "", {}, true);
  EXPECT_TRUE(factory.implementsSecureTransport());
}

TEST_F(RustlsUpstreamFactoryTest, ImplementsSecureTransportFalse) {
  RustlsUpstreamTransportSocketFactory factory(config_, "", {}, false);
  EXPECT_FALSE(factory.implementsSecureTransport());
}

TEST_F(RustlsUpstreamFactoryTest, SupportsAlpnWithProtocols) {
  RustlsUpstreamTransportSocketFactory factory(config_, "", {"h2"}, true);
  EXPECT_TRUE(factory.supportsAlpn());
}

TEST_F(RustlsUpstreamFactoryTest, SupportsAlpnEmpty) {
  RustlsUpstreamTransportSocketFactory factory(config_, "", {}, true);
  EXPECT_FALSE(factory.supportsAlpn());
}

TEST_F(RustlsUpstreamFactoryTest, DefaultServerNameIndication) {
  RustlsUpstreamTransportSocketFactory factory(config_, "example.com", {}, true);
  EXPECT_EQ(factory.defaultServerNameIndication(), "example.com");
}

TEST_F(RustlsUpstreamFactoryTest, HashKeyIsDeterministic) {
  RustlsUpstreamTransportSocketFactory factory(config_, "example.com", {"h2", "http/1.1"}, true);
  std::vector<uint8_t> key1;
  factory.hashKey(key1, nullptr);

  std::vector<uint8_t> key2;
  factory.hashKey(key2, nullptr);

  EXPECT_EQ(key1, key2);
  EXPECT_FALSE(key1.empty());
}

TEST_F(RustlsUpstreamFactoryTest, HashKeyDiffersWithDifferentConfig) {
  auto config2 = createStubConfig(/*is_upstream=*/true);
  RustlsUpstreamTransportSocketFactory factory1(config_, "a.com", {"h2"}, true);
  RustlsUpstreamTransportSocketFactory factory2(config2, "b.com", {"h2"}, true);

  std::vector<uint8_t> key1;
  factory1.hashKey(key1, nullptr);

  std::vector<uint8_t> key2;
  factory2.hashKey(key2, nullptr);

  EXPECT_NE(key1, key2);
}

// ===========================================================================
// RustlsDownstreamTransportSocketFactory tests.
// ===========================================================================

class RustlsDownstreamFactoryTest : public testing::Test {
public:
  RustlsTransportSocketConfigSharedPtr config_ = createStubConfig(/*is_upstream=*/false);
};

TEST_F(RustlsDownstreamFactoryTest, CreateDownstreamTransportSocket) {
  RustlsDownstreamTransportSocketFactory factory(config_, true);
  auto socket = factory.createDownstreamTransportSocket();
  ASSERT_NE(socket, nullptr);
}

TEST_F(RustlsDownstreamFactoryTest, ImplementsSecureTransportTrue) {
  RustlsDownstreamTransportSocketFactory factory(config_, true);
  EXPECT_TRUE(factory.implementsSecureTransport());
}

TEST_F(RustlsDownstreamFactoryTest, ImplementsSecureTransportFalse) {
  RustlsDownstreamTransportSocketFactory factory(config_, false);
  EXPECT_FALSE(factory.implementsSecureTransport());
}

// ===========================================================================
// ABI callback tests.
// ===========================================================================

class TransportSocketAbiCallbackTest : public testing::Test {
public:
  void SetUp() override {
    config_ = createStubConfig(/*is_upstream=*/true);
    socket_ = std::make_unique<RustlsTransportSocket>(config_, true);
  }

  envoy_dynamic_module_type_transport_socket_envoy_ptr socketPtr() {
    return socket_->thisAsEnvoyPtr();
  }

  RustlsTransportSocketConfigSharedPtr config_;
  std::unique_ptr<RustlsTransportSocket> socket_;
  NiceMock<Network::MockTransportSocketCallbacks> callbacks_;
  NiceMock<Network::MockIoHandle> io_handle_;
};

// -- get_io_handle -----------------------------------------------------------

TEST_F(TransportSocketAbiCallbackTest, GetIoHandleNullCallbacks) {
  auto* result = envoy_dynamic_module_callback_transport_socket_get_io_handle(socketPtr());
  EXPECT_EQ(result, nullptr);
}

TEST_F(TransportSocketAbiCallbackTest, GetIoHandleWithCallbacks) {
  ON_CALL(callbacks_, ioHandle()).WillByDefault(testing::ReturnRef(io_handle_));
  socket_->setTransportSocketCallbacks(callbacks_);
  auto* result = envoy_dynamic_module_callback_transport_socket_get_io_handle(socketPtr());
  EXPECT_NE(result, nullptr);
}

// -- io_handle_read ----------------------------------------------------------

TEST_F(TransportSocketAbiCallbackTest, IoHandleReadNullBytesRead) {
  char buf[16];
  EXPECT_EQ(-EINVAL, envoy_dynamic_module_callback_transport_socket_io_handle_read(
                         reinterpret_cast<void*>(&io_handle_), buf, sizeof(buf), nullptr));
}

TEST_F(TransportSocketAbiCallbackTest, IoHandleReadNullIoHandle) {
  char buf[16];
  size_t bytes_read = 99;
  EXPECT_EQ(-EINVAL, envoy_dynamic_module_callback_transport_socket_io_handle_read(
                         nullptr, buf, sizeof(buf), &bytes_read));
}

TEST_F(TransportSocketAbiCallbackTest, IoHandleReadSuccess) {
  EXPECT_CALL(io_handle_, readv(16, testing::_, 1))
      .WillOnce([](uint64_t, Buffer::RawSlice*, uint64_t) -> Api::IoCallUint64Result {
        return {10, Api::IoError::none()};
      });

  char buf[16];
  size_t bytes_read = 0;
  EXPECT_EQ(0, envoy_dynamic_module_callback_transport_socket_io_handle_read(
                   reinterpret_cast<void*>(&io_handle_), buf, sizeof(buf), &bytes_read));
  EXPECT_EQ(bytes_read, 10);
}

TEST_F(TransportSocketAbiCallbackTest, IoHandleReadError) {
  EXPECT_CALL(io_handle_, readv(16, testing::_, 1))
      .WillOnce([](uint64_t, Buffer::RawSlice*, uint64_t) -> Api::IoCallUint64Result {
        return {0, Network::IoSocketError::create(ECONNRESET)};
      });

  char buf[16];
  size_t bytes_read = 99;
  auto rc = envoy_dynamic_module_callback_transport_socket_io_handle_read(
      reinterpret_cast<void*>(&io_handle_), buf, sizeof(buf), &bytes_read);
  EXPECT_LT(rc, 0);
  EXPECT_EQ(bytes_read, 0);
}

// -- io_handle_write ---------------------------------------------------------

TEST_F(TransportSocketAbiCallbackTest, IoHandleWriteNullBytesWritten) {
  const char buf[] = "hello";
  EXPECT_EQ(-EINVAL, envoy_dynamic_module_callback_transport_socket_io_handle_write(
                         reinterpret_cast<void*>(&io_handle_), buf, sizeof(buf), nullptr));
}

TEST_F(TransportSocketAbiCallbackTest, IoHandleWriteNullIoHandle) {
  const char buf[] = "hello";
  size_t bytes_written = 99;
  EXPECT_EQ(-EINVAL, envoy_dynamic_module_callback_transport_socket_io_handle_write(
                         nullptr, buf, sizeof(buf), &bytes_written));
}

TEST_F(TransportSocketAbiCallbackTest, IoHandleWriteSuccess) {
  EXPECT_CALL(io_handle_, writev(testing::_, 1))
      .WillOnce([](const Buffer::RawSlice*, uint64_t) -> Api::IoCallUint64Result {
        return {5, Api::IoError::none()};
      });

  const char buf[] = "hello";
  size_t bytes_written = 0;
  EXPECT_EQ(0, envoy_dynamic_module_callback_transport_socket_io_handle_write(
                   reinterpret_cast<void*>(&io_handle_), buf, sizeof(buf), &bytes_written));
  EXPECT_EQ(bytes_written, 5);
}

TEST_F(TransportSocketAbiCallbackTest, IoHandleWriteError) {
  EXPECT_CALL(io_handle_, writev(testing::_, 1))
      .WillOnce([](const Buffer::RawSlice*, uint64_t) -> Api::IoCallUint64Result {
        return {0, Network::IoSocketError::create(ECONNRESET)};
      });

  const char buf[] = "hello";
  size_t bytes_written = 99;
  auto rc = envoy_dynamic_module_callback_transport_socket_io_handle_write(
      reinterpret_cast<void*>(&io_handle_), buf, sizeof(buf), &bytes_written);
  EXPECT_LT(rc, 0);
  EXPECT_EQ(bytes_written, 0);
}

// -- io_handle_fd ------------------------------------------------------------

TEST_F(TransportSocketAbiCallbackTest, IoHandleFdNullHandle) {
  EXPECT_EQ(-1, envoy_dynamic_module_callback_transport_socket_io_handle_fd(nullptr));
}

TEST_F(TransportSocketAbiCallbackTest, IoHandleFdReturnsValue) {
  EXPECT_CALL(io_handle_, fdDoNotUse()).WillOnce(testing::Return(42));
  EXPECT_EQ(42, envoy_dynamic_module_callback_transport_socket_io_handle_fd(
                    reinterpret_cast<void*>(&io_handle_)));
}

// -- read_buffer operations (null buffer, outside doRead) --------------------

TEST_F(TransportSocketAbiCallbackTest, ReadBufferDrainNullBuffer) {
  envoy_dynamic_module_callback_transport_socket_read_buffer_drain(socketPtr(), 10);
  EXPECT_EQ(socket_->activeReadBuffer(), nullptr);
}

TEST_F(TransportSocketAbiCallbackTest, ReadBufferAddNullBuffer) {
  const char data[] = "hello";
  envoy_dynamic_module_callback_transport_socket_read_buffer_add(socketPtr(), data, 5);
  EXPECT_EQ(socket_->activeReadBuffer(), nullptr);
}

TEST_F(TransportSocketAbiCallbackTest, ReadBufferLengthNullBuffer) {
  EXPECT_EQ(0, envoy_dynamic_module_callback_transport_socket_read_buffer_length(socketPtr()));
}

// -- write_buffer operations (null buffer, outside doWrite) ------------------

TEST_F(TransportSocketAbiCallbackTest, WriteBufferDrainNullBuffer) {
  envoy_dynamic_module_callback_transport_socket_write_buffer_drain(socketPtr(), 10);
  EXPECT_EQ(socket_->activeWriteBuffer(), nullptr);
}

TEST_F(TransportSocketAbiCallbackTest, WriteBufferGetSlicesNullSlicesCount) {
  envoy_dynamic_module_type_envoy_buffer slices[4];
  envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(socketPtr(), slices,
                                                                         nullptr);
}

TEST_F(TransportSocketAbiCallbackTest, WriteBufferGetSlicesNullBuffer) {
  size_t count = 4;
  envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(socketPtr(), nullptr,
                                                                         &count);
  EXPECT_EQ(count, 0);
}

TEST_F(TransportSocketAbiCallbackTest, WriteBufferLengthNullBuffer) {
  EXPECT_EQ(0, envoy_dynamic_module_callback_transport_socket_write_buffer_length(socketPtr()));
}

// -- raise_event -------------------------------------------------------------

TEST_F(TransportSocketAbiCallbackTest, RaiseEventNullCallbacks) {
  envoy_dynamic_module_callback_transport_socket_raise_event(
      socketPtr(), envoy_dynamic_module_type_network_connection_event_Connected);
}

TEST_F(TransportSocketAbiCallbackTest, RaiseEventWithCallbacks) {
  socket_->setTransportSocketCallbacks(callbacks_);
  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  envoy_dynamic_module_callback_transport_socket_raise_event(
      socketPtr(), static_cast<envoy_dynamic_module_type_network_connection_event>(
                       Network::ConnectionEvent::Connected));
}

// -- should_drain_read_buffer ------------------------------------------------

TEST_F(TransportSocketAbiCallbackTest, ShouldDrainReadBufferNullCallbacks) {
  EXPECT_FALSE(
      envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(socketPtr()));
}

TEST_F(TransportSocketAbiCallbackTest, ShouldDrainReadBufferWithCallbacks) {
  socket_->setTransportSocketCallbacks(callbacks_);
  EXPECT_CALL(callbacks_, shouldDrainReadBuffer()).WillOnce(testing::Return(true));
  EXPECT_TRUE(envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(socketPtr()));
}

// -- set_is_readable ---------------------------------------------------------

TEST_F(TransportSocketAbiCallbackTest, SetIsReadableNullCallbacks) {
  envoy_dynamic_module_callback_transport_socket_set_is_readable(socketPtr());
}

TEST_F(TransportSocketAbiCallbackTest, SetIsReadableWithCallbacks) {
  socket_->setTransportSocketCallbacks(callbacks_);
  EXPECT_CALL(callbacks_, setTransportSocketIsReadable());
  envoy_dynamic_module_callback_transport_socket_set_is_readable(socketPtr());
}

// -- flush_write_buffer ------------------------------------------------------

TEST_F(TransportSocketAbiCallbackTest, FlushWriteBufferNullCallbacks) {
  envoy_dynamic_module_callback_transport_socket_flush_write_buffer(socketPtr());
}

TEST_F(TransportSocketAbiCallbackTest, FlushWriteBufferWithCallbacks) {
  socket_->setTransportSocketCallbacks(callbacks_);
  EXPECT_CALL(callbacks_, flushWriteBuffer());
  envoy_dynamic_module_callback_transport_socket_flush_write_buffer(socketPtr());
}

} // namespace
} // namespace Rustls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

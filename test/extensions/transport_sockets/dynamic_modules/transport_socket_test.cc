#include "source/extensions/transport_sockets/dynamic_modules/factory.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

class DynamicModuleTransportSocketTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
        Envoy::Extensions::DynamicModules::testSharedObjectPath("transport_socket_no_op", "c"),
        false);
    EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto factory_config_or_error = newDynamicModuleTransportSocketFactoryConfig(
        "test_socket", "", /*is_upstream=*/true, std::move(dynamic_module.value()));
    EXPECT_TRUE(factory_config_or_error.ok()) << factory_config_or_error.status().message();
    factory_config_ = std::move(factory_config_or_error.value());
  }

  DynamicModuleTransportSocketFactoryConfigSharedPtr factory_config_;
  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks_;
};

// Test that a transport socket can be created and destroyed.
TEST_F(DynamicModuleTransportSocketTest, CreateAndDestroy) {
  auto socket = std::make_unique<DynamicModuleTransportSocket>(factory_config_);
  socket.reset();
}

// Test that transport socket callbacks can be set.
TEST_F(DynamicModuleTransportSocketTest, SetCallbacks) {
  auto socket = std::make_unique<DynamicModuleTransportSocket>(factory_config_);
  socket->setTransportSocketCallbacks(transport_callbacks_);
  EXPECT_EQ(&transport_callbacks_, socket->callbacks());
}

// Test the protocol() method with a no-op module (returns empty).
TEST_F(DynamicModuleTransportSocketTest, ProtocolEmpty) {
  auto socket = std::make_unique<DynamicModuleTransportSocket>(factory_config_);
  EXPECT_EQ("", socket->protocol());
}

// Test the failureReason() method with a no-op module (returns empty).
TEST_F(DynamicModuleTransportSocketTest, FailureReasonEmpty) {
  auto socket = std::make_unique<DynamicModuleTransportSocket>(factory_config_);
  EXPECT_EQ("", socket->failureReason());
}

// Test the canFlushClose() method with a no-op module (returns true).
TEST_F(DynamicModuleTransportSocketTest, CanFlushClose) {
  auto socket = std::make_unique<DynamicModuleTransportSocket>(factory_config_);
  EXPECT_TRUE(socket->canFlushClose());
}

// Test the closeSocket() method.
TEST_F(DynamicModuleTransportSocketTest, CloseSocket) {
  auto socket = std::make_unique<DynamicModuleTransportSocket>(factory_config_);
  socket->setTransportSocketCallbacks(transport_callbacks_);
  socket->closeSocket(Network::ConnectionEvent::LocalClose);
}

// Test the doRead() method with a no-op module (returns KeepOpen with 0 bytes).
TEST_F(DynamicModuleTransportSocketTest, DoReadNoOp) {
  auto socket = std::make_unique<DynamicModuleTransportSocket>(factory_config_);
  socket->setTransportSocketCallbacks(transport_callbacks_);
  Buffer::OwnedImpl buffer;
  auto result = socket->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(0, result.bytes_processed_);
  EXPECT_FALSE(result.end_stream_read_);
}

// Test the doWrite() method with a no-op module (returns KeepOpen with 0 bytes).
TEST_F(DynamicModuleTransportSocketTest, DoWriteNoOp) {
  auto socket = std::make_unique<DynamicModuleTransportSocket>(factory_config_);
  socket->setTransportSocketCallbacks(transport_callbacks_);
  Buffer::OwnedImpl buffer;
  auto result = socket->doWrite(buffer, false);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(0, result.bytes_processed_);
  EXPECT_FALSE(result.end_stream_read_);
}

// Test the onConnected() method.
TEST_F(DynamicModuleTransportSocketTest, OnConnected) {
  auto socket = std::make_unique<DynamicModuleTransportSocket>(factory_config_);
  socket->setTransportSocketCallbacks(transport_callbacks_);
  socket->onConnected();
}

// Test that ssl() returns nullptr.
TEST_F(DynamicModuleTransportSocketTest, SslReturnsNull) {
  auto socket = std::make_unique<DynamicModuleTransportSocket>(factory_config_);
  EXPECT_EQ(nullptr, socket->ssl());
}

// Test that startSecureTransport() returns false.
TEST_F(DynamicModuleTransportSocketTest, StartSecureTransportReturnsFalse) {
  auto socket = std::make_unique<DynamicModuleTransportSocket>(factory_config_);
  EXPECT_FALSE(socket->startSecureTransport());
}

// Test the factory config initialization.
TEST(DynamicModuleTransportSocketConfigTest, ConfigInitialization) {
  auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
      Envoy::Extensions::DynamicModules::testSharedObjectPath("transport_socket_no_op", "c"),
      false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto config_or_error = newDynamicModuleTransportSocketFactoryConfig(
      "test_socket", "some_config", true, std::move(dynamic_module.value()));
  EXPECT_TRUE(config_or_error.ok());

  auto config = config_or_error.value();
  EXPECT_NE(nullptr, config->in_module_config_);
  EXPECT_NE(nullptr, config->on_factory_config_destroy_);
  EXPECT_NE(nullptr, config->on_transport_socket_new_);
  EXPECT_NE(nullptr, config->on_transport_socket_destroy_);
  EXPECT_NE(nullptr, config->on_transport_socket_set_callbacks_);
  EXPECT_NE(nullptr, config->on_transport_socket_on_connected_);
  EXPECT_NE(nullptr, config->on_transport_socket_do_read_);
  EXPECT_NE(nullptr, config->on_transport_socket_do_write_);
  EXPECT_NE(nullptr, config->on_transport_socket_close_);
  EXPECT_NE(nullptr, config->on_transport_socket_get_protocol_);
  EXPECT_NE(nullptr, config->on_transport_socket_get_failure_reason_);
  EXPECT_NE(nullptr, config->on_transport_socket_can_flush_close_);
  EXPECT_TRUE(config->is_upstream());
  EXPECT_EQ("test_socket", config->socket_name());
}

// Test that missing symbols cause an error.
TEST(DynamicModuleTransportSocketConfigTest, MissingSymbols) {
  // Use the HTTP-only no_op module which lacks transport socket symbols.
  auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
      Envoy::Extensions::DynamicModules::testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto config_or_error = newDynamicModuleTransportSocketFactoryConfig(
      "test_socket", "", true, std::move(dynamic_module.value()));
  EXPECT_FALSE(config_or_error.ok());
}

// Test upstream transport socket factory.
TEST(DynamicModuleTransportSocketFactoryTest, UpstreamFactory) {
  auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
      Envoy::Extensions::DynamicModules::testSharedObjectPath("transport_socket_no_op", "c"),
      false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto config_or_error = newDynamicModuleTransportSocketFactoryConfig(
      "test_socket", "", true, std::move(dynamic_module.value()));
  EXPECT_TRUE(config_or_error.ok());

  std::vector<std::string> alpn{"h2", "http/1.1"};
  auto factory = std::make_unique<UpstreamDynamicModuleTransportSocketFactory>(
      config_or_error.value(), "example.com", alpn);

  EXPECT_TRUE(factory->implementsSecureTransport());
  EXPECT_TRUE(factory->supportsAlpn());
  EXPECT_EQ("example.com", factory->defaultServerNameIndication());

  // Test createTransportSocket.
  auto socket = factory->createTransportSocket(nullptr, nullptr);
  EXPECT_NE(nullptr, socket);

  // Test hashKey.
  std::vector<uint8_t> key;
  factory->hashKey(key, nullptr);
  EXPECT_FALSE(key.empty());
}

// Test downstream transport socket factory.
TEST(DynamicModuleTransportSocketFactoryTest, DownstreamFactory) {
  auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
      Envoy::Extensions::DynamicModules::testSharedObjectPath("transport_socket_no_op", "c"),
      false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto config_or_error = newDynamicModuleTransportSocketFactoryConfig(
      "test_socket", "", false, std::move(dynamic_module.value()));
  EXPECT_TRUE(config_or_error.ok());

  std::vector<std::string> alpn{"h2"};
  auto factory = std::make_unique<DownstreamDynamicModuleTransportSocketFactory>(
      config_or_error.value(), true, alpn);

  EXPECT_TRUE(factory->implementsSecureTransport());
  EXPECT_TRUE(factory->requireClientCertificate());
  EXPECT_EQ(std::vector<std::string>{"h2"}, factory->alpnProtocols());

  // Test createDownstreamTransportSocket.
  auto socket = factory->createDownstreamTransportSocket();
  EXPECT_NE(nullptr, socket);
}

// Test upstream factory with empty ALPN and SNI.
TEST(DynamicModuleTransportSocketFactoryTest, UpstreamFactoryNoAlpnNoSni) {
  auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
      Envoy::Extensions::DynamicModules::testSharedObjectPath("transport_socket_no_op", "c"),
      false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto config_or_error = newDynamicModuleTransportSocketFactoryConfig(
      "test_socket", "", true, std::move(dynamic_module.value()));
  EXPECT_TRUE(config_or_error.ok());

  std::vector<std::string> alpn;
  auto factory = std::make_unique<UpstreamDynamicModuleTransportSocketFactory>(
      config_or_error.value(), "", alpn);

  EXPECT_FALSE(factory->supportsAlpn());
  EXPECT_EQ("", factory->defaultServerNameIndication());
}

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

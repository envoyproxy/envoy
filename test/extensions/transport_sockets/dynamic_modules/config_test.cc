#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/network/transport_socket_options_impl.h"
#include "source/extensions/transport_sockets/dynamic_modules/config.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {
namespace {

class DynamicModuleTransportSocketConfigTest : public testing::Test {
protected:
  void SetUp() override {
    // Set up the dynamic module search path for the test modules.
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);

    // Set up the context mock.
    ON_CALL(context_, messageValidationVisitor())
        .WillByDefault(testing::ReturnRef(validation_visitor_));
  }

  Network::UpstreamTransportSocketFactoryPtr createUpstreamFactory(const std::string& yaml) {
    envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleUpstreamTransportSocket
        proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);

    UpstreamDynamicModuleTransportSocketConfigFactory factory;
    auto result = factory.createTransportSocketFactory(proto_config, context_);
    EXPECT_TRUE(result.ok()) << result.status().message();
    return std::move(result.value());
  }

  Network::DownstreamTransportSocketFactoryPtr createDownstreamFactory(const std::string& yaml) {
    envoy::extensions::transport_sockets::dynamic_modules::v3::
        DynamicModuleDownstreamTransportSocket proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);

    DownstreamDynamicModuleTransportSocketConfigFactory factory;
    std::vector<std::string> server_names;
    auto result = factory.createTransportSocketFactory(proto_config, context_, server_names);
    EXPECT_TRUE(result.ok()) << result.status().message();
    return std::move(result.value());
  }

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

// Test factory name and createEmptyConfigProto.
TEST_F(DynamicModuleTransportSocketConfigTest, UpstreamFactoryMetadata) {
  UpstreamDynamicModuleTransportSocketConfigFactory factory;
  EXPECT_EQ(factory.name(), "envoy.transport_sockets.dynamic_modules");
  auto proto = factory.createEmptyConfigProto();
  EXPECT_NE(proto, nullptr);
}

TEST_F(DynamicModuleTransportSocketConfigTest, DownstreamFactoryMetadata) {
  DownstreamDynamicModuleTransportSocketConfigFactory factory;
  EXPECT_EQ(factory.name(), "envoy.transport_sockets.dynamic_modules");
}

// Test upstream factory creation with all options.
TEST_F(DynamicModuleTransportSocketConfigTest, UpstreamFactoryCreateSuccess) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_passthrough
  socket_name: passthrough
  sni: example.com
  alpn_protocols:
    - h2
    - http/1.1
)EOF";

  auto factory_ptr = createUpstreamFactory(yaml);
  auto& upstream_factory = dynamic_cast<UpstreamDynamicModuleTransportSocketFactory&>(*factory_ptr);

  EXPECT_TRUE(upstream_factory.implementsSecureTransport());
  EXPECT_TRUE(upstream_factory.supportsAlpn());
  EXPECT_EQ(upstream_factory.defaultServerNameIndication(), "example.com");
  EXPECT_NE(upstream_factory.factoryConfig(), nullptr);
}

// Test hash key generation.
TEST_F(DynamicModuleTransportSocketConfigTest, UpstreamFactoryHashKey) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_passthrough
  socket_name: passthrough
  sni: example.com
  alpn_protocols:
    - h2
    - http/1.1
)EOF";

  auto factory_ptr = createUpstreamFactory(yaml);
  auto& upstream_factory = dynamic_cast<UpstreamDynamicModuleTransportSocketFactory&>(*factory_ptr);

  // Test hash key without options.
  std::vector<uint8_t> key1;
  upstream_factory.hashKey(key1, nullptr);
  EXPECT_FALSE(key1.empty());

  // Test hash key with options and SNI override.
  auto options = std::make_shared<Network::TransportSocketOptionsImpl>("override.example.com");
  std::vector<uint8_t> key2;
  upstream_factory.hashKey(key2, options);
  EXPECT_FALSE(key2.empty());
  EXPECT_NE(key1, key2);
}

// Test upstream factory without ALPN.
TEST_F(DynamicModuleTransportSocketConfigTest, UpstreamFactoryNoAlpn) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_passthrough
  socket_name: passthrough
)EOF";

  auto factory_ptr = createUpstreamFactory(yaml);
  auto& upstream_factory = dynamic_cast<UpstreamDynamicModuleTransportSocketFactory&>(*factory_ptr);

  EXPECT_FALSE(upstream_factory.supportsAlpn());
  EXPECT_TRUE(upstream_factory.defaultServerNameIndication().empty());
}

// Test downstream factory creation with all options.
TEST_F(DynamicModuleTransportSocketConfigTest, DownstreamFactoryCreateSuccess) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_passthrough
  socket_name: passthrough
  require_client_certificate:
    value: true
  alpn_protocols:
    - h2
)EOF";

  auto factory_ptr = createDownstreamFactory(yaml);
  auto& downstream_factory =
      dynamic_cast<DownstreamDynamicModuleTransportSocketFactory&>(*factory_ptr);

  EXPECT_TRUE(downstream_factory.implementsSecureTransport());
  EXPECT_TRUE(downstream_factory.requireClientCertificate());
  EXPECT_EQ(downstream_factory.alpnProtocols().size(), 1);
  EXPECT_EQ(downstream_factory.alpnProtocols()[0], "h2");
  EXPECT_NE(downstream_factory.factoryConfig(), nullptr);
}

// Test downstream factory without require_client_certificate.
TEST_F(DynamicModuleTransportSocketConfigTest, DownstreamFactoryNoClientCert) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_passthrough
  socket_name: passthrough
)EOF";

  auto factory_ptr = createDownstreamFactory(yaml);
  auto& downstream_factory =
      dynamic_cast<DownstreamDynamicModuleTransportSocketFactory&>(*factory_ptr);

  EXPECT_FALSE(downstream_factory.requireClientCertificate());
}

// Test transport socket creation and lifecycle.
TEST_F(DynamicModuleTransportSocketConfigTest, TransportSocketLifecycle) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_passthrough
  socket_name: passthrough
)EOF";

  auto factory_ptr = createUpstreamFactory(yaml);
  auto socket = factory_ptr->createTransportSocket(nullptr, nullptr);
  EXPECT_NE(socket, nullptr);

  // Set up mock callbacks.
  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  Network::MockIoHandle io_handle;
  ON_CALL(callbacks, ioHandle()).WillByDefault(testing::ReturnRef(io_handle));

  // Test setTransportSocketCallbacks.
  socket->setTransportSocketCallbacks(callbacks);

  // Test onConnected.
  socket->onConnected();

  // Test protocol - returns empty string for passthrough.
  EXPECT_TRUE(socket->protocol().empty());

  // Test failureReason - returns empty string for passthrough.
  EXPECT_TRUE(socket->failureReason().empty());

  // Test canFlushClose.
  EXPECT_TRUE(socket->canFlushClose());

  // Test ssl returns nullptr.
  EXPECT_EQ(socket->ssl(), nullptr);

  // Test startSecureTransport returns false.
  EXPECT_FALSE(socket->startSecureTransport());

  // Test configureInitialCongestionWindow does not throw.
  socket->configureInitialCongestionWindow(100, std::chrono::microseconds(1000));

  // Test closeSocket.
  socket->closeSocket(Network::ConnectionEvent::LocalClose);
}

// Test invalid module path.
TEST_F(DynamicModuleTransportSocketConfigTest, InvalidModulePath) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: nonexistent_module
  socket_name: passthrough
)EOF";

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleUpstreamTransportSocket
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  UpstreamDynamicModuleTransportSocketConfigFactory factory;
  auto result = factory.createTransportSocketFactory(proto_config, context_);
  EXPECT_FALSE(result.ok());
}

// Test with socket_config provided.
TEST_F(DynamicModuleTransportSocketConfigTest, WithSocketConfig) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_passthrough
  socket_name: passthrough
  socket_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: "some_config_value"
)EOF";

  auto factory_ptr = createUpstreamFactory(yaml);
  EXPECT_NE(factory_ptr, nullptr);
}

// Test downstream socket creation.
TEST_F(DynamicModuleTransportSocketConfigTest, DownstreamTransportSocketCreation) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_passthrough
  socket_name: passthrough
)EOF";

  auto factory_ptr = createDownstreamFactory(yaml);
  auto& downstream_factory =
      dynamic_cast<DownstreamDynamicModuleTransportSocketFactory&>(*factory_ptr);

  auto socket = downstream_factory.createDownstreamTransportSocket();
  EXPECT_NE(socket, nullptr);
}

// Test factory config accessors.
TEST_F(DynamicModuleTransportSocketConfigTest, FactoryConfigAccessors) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_passthrough
  socket_name: passthrough
)EOF";

  auto factory_ptr = createUpstreamFactory(yaml);
  auto& upstream_factory = dynamic_cast<UpstreamDynamicModuleTransportSocketFactory&>(*factory_ptr);

  auto& factory_config = upstream_factory.factoryConfig();
  EXPECT_NE(factory_config, nullptr);
  EXPECT_EQ(factory_config->socket_name(), "passthrough");
  EXPECT_TRUE(factory_config->is_upstream());
}

// Test close with different events.
TEST_F(DynamicModuleTransportSocketConfigTest, TransportSocketCloseEvents) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_passthrough
  socket_name: passthrough
)EOF";

  // Test RemoteClose.
  {
    auto factory_ptr = createUpstreamFactory(yaml);
    auto socket = factory_ptr->createTransportSocket(nullptr, nullptr);
    NiceMock<Network::MockTransportSocketCallbacks> callbacks;
    NiceMock<Network::MockIoHandle> io_handle;
    ON_CALL(callbacks, ioHandle()).WillByDefault(testing::ReturnRef(io_handle));
    socket->setTransportSocketCallbacks(callbacks);
    socket->closeSocket(Network::ConnectionEvent::RemoteClose);
  }

  // Test Connected.
  {
    auto factory_ptr = createUpstreamFactory(yaml);
    auto socket = factory_ptr->createTransportSocket(nullptr, nullptr);
    NiceMock<Network::MockTransportSocketCallbacks> callbacks;
    NiceMock<Network::MockIoHandle> io_handle;
    ON_CALL(callbacks, ioHandle()).WillByDefault(testing::ReturnRef(io_handle));
    socket->setTransportSocketCallbacks(callbacks);
    socket->closeSocket(Network::ConnectionEvent::Connected);
  }

  // Test ConnectedZeroRtt.
  {
    auto factory_ptr = createUpstreamFactory(yaml);
    auto socket = factory_ptr->createTransportSocket(nullptr, nullptr);
    NiceMock<Network::MockTransportSocketCallbacks> callbacks;
    NiceMock<Network::MockIoHandle> io_handle;
    ON_CALL(callbacks, ioHandle()).WillByDefault(testing::ReturnRef(io_handle));
    socket->setTransportSocketCallbacks(callbacks);
    socket->closeSocket(Network::ConnectionEvent::ConnectedZeroRtt);
  }
}

// Test doRead and doWrite with the noop module that doesn't do real I/O.
TEST_F(DynamicModuleTransportSocketConfigTest, DoReadAndDoWriteNoop) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_noop
  socket_name: noop
)EOF";

  auto factory_ptr = createUpstreamFactory(yaml);
  auto socket = factory_ptr->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  NiceMock<Network::MockIoHandle> io_handle;
  ON_CALL(callbacks, ioHandle()).WillByDefault(testing::ReturnRef(io_handle));
  socket->setTransportSocketCallbacks(callbacks);

  // Test doRead - the noop module returns immediately without I/O.
  Buffer::OwnedImpl read_buffer;
  auto read_result = socket->doRead(read_buffer);
  EXPECT_EQ(read_result.action_, Network::PostIoAction::KeepOpen);
  EXPECT_EQ(read_result.bytes_processed_, 0);
  EXPECT_FALSE(read_result.end_stream_read_);

  // Test doWrite - the noop module drains the buffer without I/O.
  Buffer::OwnedImpl write_buffer("test data");
  auto write_result = socket->doWrite(write_buffer, false);
  EXPECT_EQ(write_result.action_, Network::PostIoAction::KeepOpen);
  EXPECT_EQ(write_result.bytes_processed_, 9);
  EXPECT_FALSE(write_result.end_stream_read_);

  // Test doWrite with end_stream.
  Buffer::OwnedImpl write_buffer2("more data");
  auto write_result_end = socket->doWrite(write_buffer2, true);
  EXPECT_EQ(write_result_end.action_, Network::PostIoAction::KeepOpen);

  // Test protocol - noop module returns "noop-protocol".
  EXPECT_EQ(socket->protocol(), "noop-protocol");
}

// Test transport socket with the noop module for additional coverage.
TEST_F(DynamicModuleTransportSocketConfigTest, NoopSocketLifecycle) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_noop
  socket_name: noop
)EOF";

  auto factory_ptr = createUpstreamFactory(yaml);
  auto socket = factory_ptr->createTransportSocket(nullptr, nullptr);
  ASSERT_NE(socket, nullptr);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  NiceMock<Network::MockIoHandle> io_handle;
  ON_CALL(callbacks, ioHandle()).WillByDefault(testing::ReturnRef(io_handle));

  // Expect raiseEvent to be called during onConnected.
  EXPECT_CALL(callbacks, raiseEvent(Network::ConnectionEvent::Connected));

  socket->setTransportSocketCallbacks(callbacks);
  socket->onConnected();

  // Test canFlushClose.
  EXPECT_TRUE(socket->canFlushClose());

  // Test failureReason - noop module returns empty.
  EXPECT_TRUE(socket->failureReason().empty());

  // Test close with explicit event.
  socket->closeSocket(Network::ConnectionEvent::LocalClose);
}

// Test socket destruction via destroy() and destructor.
TEST_F(DynamicModuleTransportSocketConfigTest, SocketDestruction) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_passthrough
  socket_name: passthrough
)EOF";

  auto factory_ptr = createUpstreamFactory(yaml);
  {
    auto socket = factory_ptr->createTransportSocket(nullptr, nullptr);
    ASSERT_NE(socket, nullptr);

    NiceMock<Network::MockTransportSocketCallbacks> callbacks;
    NiceMock<Network::MockIoHandle> io_handle;
    ON_CALL(callbacks, ioHandle()).WillByDefault(testing::ReturnRef(io_handle));
    socket->setTransportSocketCallbacks(callbacks);

    // Socket goes out of scope and destructor is called.
  }

  // Creating another socket should work fine.
  auto socket2 = factory_ptr->createTransportSocket(nullptr, nullptr);
  EXPECT_NE(socket2, nullptr);
}

// Test factory config is_upstream.
TEST_F(DynamicModuleTransportSocketConfigTest, FactoryConfigIsUpstream) {
  // Test upstream factory sets is_upstream=true.
  {
    const std::string yaml = R"EOF(
    dynamic_module_config:
      name: transport_socket_passthrough
    socket_name: passthrough
)EOF";

    auto factory_ptr = createUpstreamFactory(yaml);
    auto& upstream_factory =
        dynamic_cast<UpstreamDynamicModuleTransportSocketFactory&>(*factory_ptr);
    EXPECT_TRUE(upstream_factory.factoryConfig()->is_upstream());
  }

  // Test downstream factory sets is_upstream=false.
  {
    const std::string yaml = R"EOF(
    dynamic_module_config:
      name: transport_socket_passthrough
    socket_name: passthrough
)EOF";

    auto factory_ptr = createDownstreamFactory(yaml);
    auto& downstream_factory =
        dynamic_cast<DownstreamDynamicModuleTransportSocketFactory&>(*factory_ptr);
    EXPECT_FALSE(downstream_factory.factoryConfig()->is_upstream());
  }
}

// Test hashKey with empty SNI.
TEST_F(DynamicModuleTransportSocketConfigTest, HashKeyEmptySni) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: transport_socket_passthrough
  socket_name: passthrough
)EOF";

  auto factory_ptr = createUpstreamFactory(yaml);
  auto& upstream_factory = dynamic_cast<UpstreamDynamicModuleTransportSocketFactory&>(*factory_ptr);

  std::vector<uint8_t> key;
  upstream_factory.hashKey(key, nullptr);
  // Hash should only contain socket_name since SNI is empty.
  EXPECT_FALSE(key.empty());
}

} // namespace
} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

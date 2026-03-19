#include "test/integration/tcp_proxy_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
void BaseTcpProxyIntegrationTest::initialize() {
  config_helper_.renameListener("tcp_proxy");
  BaseIntegrationTest::initialize();
}

void BaseTcpProxyIntegrationTest::setupByteMeterAccessLog() {
  useListenerAccessLog("DOWNSTREAM_WIRE_BYTES_SENT=%DOWNSTREAM_WIRE_BYTES_SENT% "
                       "DOWNSTREAM_WIRE_BYTES_RECEIVED=%DOWNSTREAM_WIRE_BYTES_RECEIVED% "
                       "UPSTREAM_WIRE_BYTES_SENT=%UPSTREAM_WIRE_BYTES_SENT% "
                       "UPSTREAM_WIRE_BYTES_RECEIVED=%UPSTREAM_WIRE_BYTES_RECEIVED%");
}

void BaseTcpProxySslIntegrationTest::initialize() {
  config_helper_.addSslConfig();
  BaseTcpProxyIntegrationTest::initialize();

  context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
      server_factory_context_);
  context_ = Ssl::createClientSslTransportSocketFactory(ssl_options_, *context_manager_, *api_);
}

BaseTcpProxySslIntegrationTest::ClientSslConnection::ClientSslConnection(
    BaseTcpProxySslIntegrationTest& parent)
    : parent_(parent),
      payload_reader_(std::make_shared<WaitForPayloadReader>(*parent.dispatcher_)) {
  // Set up the mock buffer factory so the newly created SSL client will have a mock write
  // buffer. This allows us to track the bytes actually written to the socket.
  EXPECT_CALL(*parent.mock_buffer_factory_, createBuffer_(_, _, _))
      .Times(::testing::AtLeast(1))
      .WillOnce(Invoke([&](std::function<void()> below_low, std::function<void()> above_high,
                           std::function<void()> above_overflow) -> Buffer::Instance* {
        client_write_buffer_ =
            new NiceMock<MockWatermarkBuffer>(below_low, above_high, above_overflow);
        ON_CALL(*client_write_buffer_, move(_))
            .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove));
        ON_CALL(*client_write_buffer_, drain(_))
            .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
        return client_write_buffer_;
      }));
  // Set up the SSL client.
  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(parent.version_, parent.lookupPort("tcp_proxy"));
  ssl_client_ = parent.dispatcher_->createClientConnection(
      address, Network::Address::InstanceConstSharedPtr(),
      parent.context_->createTransportSocket(nullptr, nullptr), nullptr, nullptr);

  // Start the SSL handshake. Loopback is allowlisted in tcp_proxy.json for the ssl_auth
  // filter so there will be no pause waiting on auth data.
  ssl_client_->addConnectionCallbacks(connect_callbacks_);
  ssl_client_->enableHalfClose(true);
  ssl_client_->addReadFilter(payload_reader_);
  ssl_client_->connect();
}

void BaseTcpProxySslIntegrationTest::ClientSslConnection::waitForUpstreamConnection() {
  while (!connect_callbacks_.connected()) {
    parent_.dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  AssertionResult result = parent_.dataStream()->waitForRawConnection(fake_upstream_connection_);
  RELEASE_ASSERT(result, result.message());
}

void BaseTcpProxySslIntegrationTest::ClientRawConnection::waitForUpstreamConnection() {
  AssertionResult result = parent_.dataStream()->waitForRawConnection(
      fake_upstream_connection_, TestUtility::DefaultTimeout, *parent_.dispatcher_);
  RELEASE_ASSERT(result, result.message());
}

// Test proxying data in both directions with envoy doing TCP and TLS
// termination.
void BaseTcpProxySslIntegrationTest::ClientSslConnection::sendAndReceiveTlsData(
    const std::string& data_to_send_upstream, const std::string& data_to_send_downstream) {
  // Ship some data upstream.
  Buffer::OwnedImpl buffer(data_to_send_upstream);
  ssl_client_->write(buffer, false);
  while (client_write_buffer_->bytesDrained() != data_to_send_upstream.size()) {
    parent_.dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection_->waitForData(data_to_send_upstream.size()));

  // Now send data downstream and make sure it arrives.
  ASSERT_TRUE(fake_upstream_connection_->write(data_to_send_downstream));
  payload_reader_->setDataToWaitFor(data_to_send_downstream);
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);

  // Clean up.
  Buffer::OwnedImpl empty_buffer;
  ssl_client_->write(empty_buffer, true);
  parent_.dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  ASSERT_TRUE(fake_upstream_connection_->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection_->write("", true));
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(payload_reader_->readLastByte());
  EXPECT_TRUE(connect_callbacks_.closed());
}

void BaseTcpProxySslIntegrationTest::ClientRawConnection::sendAndReceiveTlsData(
    const std::string& data_to_send_upstream, const std::string& data_to_send_downstream) {
  ASSERT_TRUE(tcp_client_.write(data_to_send_upstream));
  ASSERT_TRUE(fake_upstream_connection_->waitForData(data_to_send_upstream.size()));
  ASSERT_TRUE(fake_upstream_connection_->write(data_to_send_downstream));
  tcp_client_.waitForData(data_to_send_downstream);
  tcp_client_.close();
  ASSERT_TRUE(fake_upstream_connection_->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

void BaseTcpProxySslIntegrationTest::ClientSslConnection::close() {
  ssl_client_->close(Network::ConnectionCloseType::NoFlush);
}

void BaseTcpProxySslIntegrationTest::ClientRawConnection::close() { tcp_client_.close(); }

void BaseTcpProxySslIntegrationTest::ClientSslConnection::waitForDisconnect() {
  while (!connect_callbacks_.closed()) {
    parent_.dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

void BaseTcpProxySslIntegrationTest::ClientRawConnection::waitForDisconnect() {
  tcp_client_.waitForHalfClose();
  tcp_client_.close();
}

absl::optional<std::string>
BaseTcpProxySslIntegrationTest::ClientSslConnection::tlsSessionId() const {
  const Ssl::ConnectionInfoConstSharedPtr ssl_info =
      ssl_client_->connectionInfoProvider().sslConnection();
  return ssl_info ? absl::make_optional<std::string>(ssl_info->sessionId()) : absl::nullopt;
}

void BaseTcpProxySslIntegrationTest::setupConnections() {
  initialize();
  client_ = std::make_unique<ClientSslConnection>(*this);
  client_->waitForUpstreamConnection();
}

void BaseTcpProxySslIntegrationTest::sendAndReceiveTlsData(
    const std::string& data_to_send_upstream, const std::string& data_to_send_downstream) {
  client_->sendAndReceiveTlsData(data_to_send_upstream, data_to_send_downstream);
}
} // namespace Envoy

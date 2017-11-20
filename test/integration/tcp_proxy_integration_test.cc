#include "test/integration/tcp_proxy_integration_test.h"

#include "common/filesystem/filesystem_impl.h"
#include "common/network/utility.h"
#include "common/ssl/context_manager_impl.h"

#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/mocks/runtime/mocks.h"

#include "gtest/gtest.h"

using testing::Invoke;
using testing::MatchesRegex;
using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace {

INSTANTIATE_TEST_CASE_P(IpVersions, TcpProxyIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Test upstream writing before downstream downstream does.
TEST_P(TcpProxyIntegrationTest, TcpProxyUpstreamWritesFirst) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();

  fake_upstream_connection->write("hello");
  tcp_client->waitForData("hello");

  tcp_client->write("hello");
  fake_upstream_connection->waitForData(5);

  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();
  tcp_client->waitForDisconnect();
}

// Test proxying data in both directions, and that all data is flushed properly
// when there is an upstream disconnect.
TEST_P(TcpProxyIntegrationTest, TcpProxyUpstreamDisconnect) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  tcp_client->write("hello");
  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  fake_upstream_connection->waitForData(5);
  fake_upstream_connection->write("world");
  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();
  tcp_client->waitForDisconnect();

  EXPECT_EQ("world", tcp_client->data());
}

// Test proxying data in both directions, and that all data is flushed properly
// when the client disconnects.
TEST_P(TcpProxyIntegrationTest, TcpProxyDownstreamDisconnect) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  tcp_client->write("hello");
  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  fake_upstream_connection->waitForData(5);
  fake_upstream_connection->write("world");
  tcp_client->waitForData("world");
  tcp_client->write("hello");
  tcp_client->close();
  fake_upstream_connection->waitForData(10);
  fake_upstream_connection->waitForDisconnect();
}

TEST_P(TcpProxyIntegrationTest, TcpProxyLargeWrite) {
  std::string data(1024 * 16, 'a');
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy_with_write_limits"));
  tcp_client->write(data);
  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[1]->waitForRawConnection();
  fake_upstream_connection->waitForData(data.size());
  fake_upstream_connection->write(data);
  tcp_client->waitForData(data);
  tcp_client->close();
  fake_upstream_connection->waitForDisconnect();

  uint32_t upstream_pauses =
      test_server_
          ->counter("cluster.cluster_with_buffer_limits.upstream_flow_control_paused_reading_total")
          ->value();
  uint32_t upstream_resumes =
      test_server_
          ->counter(
              "cluster.cluster_with_buffer_limits.upstream_flow_control_resumed_reading_total")
          ->value();
  EXPECT_EQ(upstream_pauses, upstream_resumes);

  uint32_t downstream_pauses =
      test_server_
          ->counter("tcp.tcp_with_write_limits.downstream_flow_control_paused_reading_total")
          ->value();
  uint32_t downstream_resumes =
      test_server_
          ->counter("tcp.tcp_with_write_limits.downstream_flow_control_resumed_reading_total")
          ->value();
  EXPECT_EQ(downstream_pauses, downstream_resumes);
}

// Test proxying data in both directions with envoy doing TCP and TLS
// termination.
void TcpProxyIntegrationTest::sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                                                    const std::string& data_to_send_downstream) {
  Network::ClientConnectionPtr ssl_client;
  FakeRawConnectionPtr fake_upstream_connection;
  testing::NiceMock<Runtime::MockLoader> runtime;
  std::unique_ptr<Ssl::ContextManager> context_manager(new Ssl::ContextManagerImpl(runtime));
  Ssl::ClientContextPtr context;
  ConnectionStatusCallbacks connect_callbacks;
  MockWatermarkBuffer* client_write_buffer;
  // Set up the mock buffer factory so the newly created SSL client will have a mock write
  // buffer.  This allows us to track the bytes actually written to the socket.

  EXPECT_CALL(*mock_buffer_factory_, create_(_, _))
      .Times(1)
      .WillOnce(Invoke([&](std::function<void()> below_low,
                           std::function<void()> above_high) -> Buffer::Instance* {
        client_write_buffer = new NiceMock<MockWatermarkBuffer>(below_low, above_high);
        ON_CALL(*client_write_buffer, move(_))
            .WillByDefault(Invoke(client_write_buffer, &MockWatermarkBuffer::baseMove));
        ON_CALL(*client_write_buffer, drain(_))
            .WillByDefault(Invoke(client_write_buffer, &MockWatermarkBuffer::trackDrains));
        return client_write_buffer;
      }));
  // Set up the SSl client.
  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("tcp_proxy_with_tls_termination"));
  context = Ssl::createClientSslContext(false, false, *context_manager);
  ssl_client = dispatcher_->createSslClientConnection(*context, address,
                                                      Network::Address::InstanceConstSharedPtr());

  // Perform the SSL handshake.  Loopback is whitelisted in tcp_proxy.json for the ssl_auth
  // filter so there will be no pause waiting on auth data.

  ssl_client->addConnectionCallbacks(connect_callbacks);
  ssl_client->connect();
  while (!connect_callbacks.connected()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // Ship some data upstream.

  Buffer::OwnedImpl buffer(data_to_send_upstream);
  ssl_client->write(buffer);
  while (client_write_buffer->bytes_drained() != data_to_send_upstream.size()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // Make sure the data makes it upstream.
  fake_upstream_connection = fake_upstreams_[1]->waitForRawConnection();
  fake_upstream_connection->waitForData(data_to_send_upstream.size());
  // Now send data downstream and make sure it arrives.

  std::shared_ptr<WaitForPayloadReader> payload_reader(new WaitForPayloadReader(*dispatcher_));
  ssl_client->addReadFilter(payload_reader);
  fake_upstream_connection->write(data_to_send_downstream);
  payload_reader->set_data_to_wait_for(data_to_send_downstream);
  ssl_client->dispatcher().run(Event::Dispatcher::RunType::Block);
  // Clean up.
  ssl_client->close(Network::ConnectionCloseType::NoFlush);
  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();
}

TEST_P(TcpProxyIntegrationTest, SendTlsToTlsListener) { sendAndReceiveTlsData("hello", "world"); }

TEST_P(TcpProxyIntegrationTest, LargeBidirectionalTlsWrites) {
  std::string large_data(1024 * 8, 'a');
  sendAndReceiveTlsData(large_data, large_data);
}

TEST_P(TcpProxyIntegrationTest, AccessLog) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();

  fake_upstream_connection->write("hello");
  tcp_client->waitForData("hello");

  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();
  tcp_client->waitForDisconnect();

  const std::string path = TestEnvironment::temporaryPath(fmt::format(
      "tcp_{}.log", (GetParam() == Network::Address::IpVersion::v4) ? "127.0.0.1" : "[::1]"));
  std::string log_result;

  // Access logs only get flushed to disk periodically, so poll until the log is non-empty
  do {
    log_result = Filesystem::fileReadToEnd(path);
  } while (log_result.empty());

  // Regex matching localhost:port
  const std::string localhostIpPortRegex = (GetParam() == Network::Address::IpVersion::v4)
                                               ? R"EOF(127\.0\.0\.1:[0-9]+)EOF"
                                               : R"EOF(\[::1\]:[0-9]+)EOF";

  // Test that all three addresses were populated correctly.  Only check the first line of
  // log output for simplicity.
  EXPECT_THAT(log_result,
              MatchesRegex(fmt::format("upstreamlocal={0} upstreamhost={0} downstream={0}\n.*",
                                       localhostIpPortRegex)));
}

} // namespace
} // namespace Envoy

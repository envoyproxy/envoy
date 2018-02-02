#include "test/integration/tcp_proxy_integration_test.h"

#include "envoy/api/v2/filter/accesslog/accesslog.pb.h"

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
  initialize();
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
  initialize();
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
  initialize();
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
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  std::string data(1024 * 16, 'a');
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  tcp_client->write(data);
  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  fake_upstream_connection->waitForData(data.size());
  fake_upstream_connection->write(data);
  tcp_client->waitForData(data);
  tcp_client->close();
  fake_upstream_connection->waitForDisconnect();

  uint32_t upstream_pauses =
      test_server_->counter("cluster.cluster_0.upstream_flow_control_paused_reading_total")
          ->value();
  uint32_t upstream_resumes =
      test_server_->counter("cluster.cluster_0.upstream_flow_control_resumed_reading_total")
          ->value();
  EXPECT_EQ(upstream_pauses, upstream_resumes);

  uint32_t downstream_pauses =
      test_server_->counter("tcp.tcp_stats.downstream_flow_control_paused_reading_total")->value();
  uint32_t downstream_resumes =
      test_server_->counter("tcp.tcp_stats.downstream_flow_control_resumed_reading_total")->value();
  EXPECT_EQ(downstream_pauses, downstream_resumes);
}

// Test that an upstream flush works correctly (all data is flushed)
TEST_P(TcpProxyIntegrationTest, TcpProxyUpstreamFlush) {
  // Use a very large size to make sure it is larger than the kernel socket read buffer.
  const uint32_t size = 50 * 1024 * 1024;
  config_helper_.setBufferLimits(size, size);
  initialize();

  std::string data(size, 'a');
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  fake_upstream_connection->readDisable(true);
  tcp_client->write(data);
  tcp_client->close();

  test_server_->waitForGaugeEq("tcp.tcp_stats.upstream_flush_active", 1);
  fake_upstream_connection->readDisable(false);
  fake_upstream_connection->waitForData(data.size());
  fake_upstream_connection->waitForDisconnect();

  EXPECT_EQ(test_server_->counter("tcp.tcp_stats.upstream_flush_total")->value(), 1);
  EXPECT_EQ(test_server_->gauge("tcp.tcp_stats.upstream_flush_active")->value(), 0);
}

// Test that Envoy doesn't crash or assert when shutting down with an upstream flush active
TEST_P(TcpProxyIntegrationTest, TcpProxyUpstreamFlushEnvoyExit) {
  // Use a very large size to make sure it is larger than the kernel socket read buffer.
  const uint32_t size = 50 * 1024 * 1024;
  config_helper_.setBufferLimits(size, size);
  initialize();

  std::string data(size, 'a');
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  fake_upstream_connection->readDisable(true);
  tcp_client->write(data);
  tcp_client->close();

  test_server_->waitForGaugeEq("tcp.tcp_stats.upstream_flush_active", 1);
  test_server_.reset();
  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();
}

// Test proxying data in both directions with envoy doing TCP and TLS
// termination.
void TcpProxyIntegrationTest::sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                                                    const std::string& data_to_send_downstream) {
  config_helper_.addSslConfig();
  initialize();

  Network::ClientConnectionPtr ssl_client;
  FakeRawConnectionPtr fake_upstream_connection;
  testing::NiceMock<Runtime::MockLoader> runtime;
  std::unique_ptr<Ssl::ContextManager> context_manager(new Ssl::ContextManagerImpl(runtime));
  Network::TransportSocketFactoryPtr context;
  ConnectionStatusCallbacks connect_callbacks;
  MockWatermarkBuffer* client_write_buffer;
  // Set up the mock buffer factory so the newly created SSL client will have a mock write
  // buffer. This allows us to track the bytes actually written to the socket.

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
      Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));
  context = Ssl::createClientSslTransportSocketFactory(false, false, *context_manager);
  ssl_client =
      dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                          context->createTransportSocket(), nullptr);

  // Perform the SSL handshake. Loopback is whitelisted in tcp_proxy.json for the ssl_auth
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
  fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
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
  std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log{}.txt", GetParam() == Network::Address::IpVersion::v4 ? "v4" : "v6"));
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_config();

    envoy::api::v2::filter::network::TcpProxy tcp_proxy_config;
    MessageUtil::jsonConvert(*config_blob, tcp_proxy_config);

    auto* access_log = tcp_proxy_config.add_access_log();
    access_log->set_name("envoy.file_access_log");
    envoy::api::v2::filter::accesslog::FileAccessLog access_log_config;
    access_log_config.set_path(access_log_path);
    access_log_config.set_format("upstreamlocal=%UPSTREAM_LOCAL_ADDRESS% "
                                 "upstreamhost=%UPSTREAM_HOST% downstream=%DOWNSTREAM_ADDRESS%\n");
    MessageUtil::jsonConvert(access_log_config, *access_log->mutable_config());

    MessageUtil::jsonConvert(tcp_proxy_config, *config_blob);
  });
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();

  fake_upstream_connection->write("hello");
  tcp_client->waitForData("hello");

  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();
  tcp_client->waitForDisconnect();

  std::string log_result;
  // Access logs only get flushed to disk periodically, so poll until the log is non-empty
  do {
    log_result = Filesystem::fileReadToEnd(access_log_path);
  } while (log_result.empty());

  // Regex matching localhost:port
  const std::string ip_port_regex = (GetParam() == Network::Address::IpVersion::v4)
                                        ? R"EOF(127\.0\.0\.1:[0-9]+)EOF"
                                        : R"EOF(\[::1\]:[0-9]+)EOF";

  const std::string ip_regex =
      (GetParam() == Network::Address::IpVersion::v4) ? R"EOF(127\.0\.0\.1)EOF" : R"EOF(::1)EOF";

  // Test that all three addresses were populated correctly. Only check the first line of
  // log output for simplicity.
  EXPECT_THAT(log_result,
              MatchesRegex(fmt::format("upstreamlocal={0} upstreamhost={0} downstream={1}\n.*",
                                       ip_port_regex, ip_regex)));
}

} // namespace
} // namespace Envoy

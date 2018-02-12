#include "test/integration/tcp_proxy_integration_test.h"

#include "envoy/config/filter/accesslog/v2/accesslog.pb.h"

#include "common/filesystem/filesystem_impl.h"
#include "common/network/utility.h"
#include "common/ssl/context_manager_impl.h"

#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"

#include "gtest/gtest.h"

using testing::Invoke;
using testing::MatchesRegex;
using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace {

INSTANTIATE_TEST_CASE_P(IpVersions, TcpProxyIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

void TcpProxyIntegrationTest::initialize() {
  config_helper_.renameListener("tcp_proxy");
  BaseIntegrationTest::initialize();
}

// Test upstream writing before downstream downstream does.
TEST_P(TcpProxyIntegrationTest, TcpProxyUpstreamWritesFirst) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();

  fake_upstream_connection->write("hello");
  tcp_client->waitForData("hello");

  tcp_client->write("hello");
  fake_upstream_connection->waitForData(5);

  fake_upstream_connection->write("", true);
  tcp_client->waitForHalfClose();
  tcp_client->write("", true);
  fake_upstream_connection->waitForHalfClose();
  fake_upstream_connection->waitForDisconnect();
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
  tcp_client->waitForHalfClose();
  tcp_client->close();

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
  tcp_client->write("hello", true);
  fake_upstream_connection->waitForData(10);
  fake_upstream_connection->waitForHalfClose();
  fake_upstream_connection->write("", true);
  fake_upstream_connection->waitForDisconnect(true);
  tcp_client->waitForDisconnect();
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
  fake_upstream_connection->waitForHalfClose();
  fake_upstream_connection->close();
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

// Test that a downstream flush works correctly (all data is flushed)
TEST_P(TcpProxyIntegrationTest, TcpProxyDownstreamFlush) {
  // Use a very large size to make sure it is larger than the kernel socket read buffer.
  const uint32_t size = 50 * 1024 * 1024;
  config_helper_.setBufferLimits(size / 4, size / 4);
  initialize();

  std::string data(size, 'a');
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  tcp_client->readDisable(true);
  tcp_client->write("", true);

  // This ensures that readDisable(true) has been run on it's thread
  // before tcp_client starts writing.
  fake_upstream_connection->waitForHalfClose();

  fake_upstream_connection->write(data, true);

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_flow_control_paused_reading_total", 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_flow_control_resumed_reading_total")
                ->value(),
            0);
  tcp_client->readDisable(false);
  tcp_client->waitForData(data);
  tcp_client->waitForHalfClose();
  fake_upstream_connection->waitForHalfClose();

  uint32_t upstream_pauses =
      test_server_->counter("cluster.cluster_0.upstream_flow_control_paused_reading_total")
          ->value();
  uint32_t upstream_resumes =
      test_server_->counter("cluster.cluster_0.upstream_flow_control_resumed_reading_total")
          ->value();
  EXPECT_GE(upstream_pauses, upstream_resumes);
  EXPECT_GT(upstream_resumes, 0);
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
  fake_upstream_connection->write("", true);

  // This ensures that fake_upstream_connection->readDisable has been run on it's thread
  // before tcp_client starts writing.
  tcp_client->waitForHalfClose();

  tcp_client->write(data, true);

  test_server_->waitForGaugeEq("tcp.tcp_stats.upstream_flush_active", 1);
  fake_upstream_connection->readDisable(false);
  fake_upstream_connection->waitForData(data.size());
  fake_upstream_connection->waitForDisconnect();
  tcp_client->waitForHalfClose();

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
  fake_upstream_connection->write("", true);

  // This ensures that fake_upstream_connection->readDisable has been run on it's thread
  // before tcp_client starts writing.
  tcp_client->waitForHalfClose();

  tcp_client->write(data, true);

  test_server_->waitForGaugeEq("tcp.tcp_stats.upstream_flush_active", 1);
  test_server_.reset();
  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();

  // Success criteria is that no ASSERTs fire and there are no leaks.
}

TEST_P(TcpProxyIntegrationTest, AccessLog) {
  std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log{}.txt", GetParam() == Network::Address::IpVersion::v4 ? "v4" : "v6"));
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_config();

    envoy::config::filter::network::tcp_proxy::v2::TcpProxy tcp_proxy_config;
    MessageUtil::jsonConvert(*config_blob, tcp_proxy_config);

    auto* access_log = tcp_proxy_config.add_access_log();
    access_log->set_name("envoy.file_access_log");
    envoy::config::filter::accesslog::v2::FileAccessLog access_log_config;
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

  fake_upstream_connection->write("", true);
  tcp_client->waitForHalfClose();
  tcp_client->write("", true);
  fake_upstream_connection->waitForHalfClose();
  fake_upstream_connection->waitForDisconnect();

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

void TcpProxySslIntegrationTest::initialize() {
  config_helper_.addSslConfig();
  TcpProxyIntegrationTest::initialize();

  context_manager_.reset(new Ssl::ContextManagerImpl(runtime_));
  payload_reader_.reset(new WaitForPayloadReader(*dispatcher_));
}

void TcpProxySslIntegrationTest::setupConnections() {
  initialize();

  // Set up the mock buffer factory so the newly created SSL client will have a mock write
  // buffer. This allows us to track the bytes actually written to the socket.

  EXPECT_CALL(*mock_buffer_factory_, create_(_, _))
      .Times(1)
      .WillOnce(Invoke([&](std::function<void()> below_low,
                           std::function<void()> above_high) -> Buffer::Instance* {
        client_write_buffer_ = new NiceMock<MockWatermarkBuffer>(below_low, above_high);
        ON_CALL(*client_write_buffer_, move(_))
            .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove));
        ON_CALL(*client_write_buffer_, drain(_))
            .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
        return client_write_buffer_;
      }));
  // Set up the SSl client.
  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));
  context_ = Ssl::createClientSslTransportSocketFactory(false, false, *context_manager_);
  ssl_client_ =
      dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                          context_->createTransportSocket(), nullptr);

  // Perform the SSL handshake. Loopback is whitelisted in tcp_proxy.json for the ssl_auth
  // filter so there will be no pause waiting on auth data.
  ssl_client_->addConnectionCallbacks(connect_callbacks_);
  ssl_client_->enableHalfClose(true);
  ssl_client_->addReadFilter(payload_reader_);
  ssl_client_->connect();
  while (!connect_callbacks_.connected()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  fake_upstream_connection_ = fake_upstreams_[0]->waitForRawConnection();
}

// Test proxying data in both directions with envoy doing TCP and TLS
// termination.
void TcpProxySslIntegrationTest::sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                                                       const std::string& data_to_send_downstream) {
  // Ship some data upstream.
  Buffer::OwnedImpl buffer(data_to_send_upstream);
  ssl_client_->write(buffer, false);
  while (client_write_buffer_->bytes_drained() != data_to_send_upstream.size()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Make sure the data makes it upstream.
  fake_upstream_connection_->waitForData(data_to_send_upstream.size());

  // Now send data downstream and make sure it arrives.
  fake_upstream_connection_->write(data_to_send_downstream);
  payload_reader_->set_data_to_wait_for(data_to_send_downstream);
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);

  // Clean up.
  Buffer::OwnedImpl empty_buffer;
  ssl_client_->write(empty_buffer, true);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  fake_upstream_connection_->waitForHalfClose();
  fake_upstream_connection_->write("", true);
  fake_upstream_connection_->waitForDisconnect();
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(payload_reader_->readLastByte());
  EXPECT_TRUE(connect_callbacks_.closed());
}

TEST_P(TcpProxySslIntegrationTest, SendTlsToTlsListener) {
  setupConnections();
  sendAndReceiveTlsData("hello", "world");
}

TEST_P(TcpProxySslIntegrationTest, LargeBidirectionalTlsWrites) {
  setupConnections();
  std::string large_data(1024 * 8, 'a');
  sendAndReceiveTlsData(large_data, large_data);
}

// Test that a half-close on the downstream side is proxied correctly.
TEST_P(TcpProxySslIntegrationTest, DownstreamHalfClose) {
  setupConnections();

  Buffer::OwnedImpl empty_buffer;
  ssl_client_->write(empty_buffer, true);
  fake_upstream_connection_->waitForHalfClose();

  const std::string data("data");
  fake_upstream_connection_->write(data, false);
  payload_reader_->set_data_to_wait_for(data);
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_FALSE(payload_reader_->readLastByte());

  fake_upstream_connection_->write("", true);
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(payload_reader_->readLastByte());
}

// Test that a half-close on the upstream side is proxied correctly.
TEST_P(TcpProxySslIntegrationTest, UpstreamHalfClose) {
  setupConnections();

  fake_upstream_connection_->write("", true);
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(payload_reader_->readLastByte());
  EXPECT_FALSE(connect_callbacks_.closed());

  const std::string& val("data");
  Buffer::OwnedImpl buffer(val);
  ssl_client_->write(buffer, false);
  while (client_write_buffer_->bytes_drained() != val.size()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  fake_upstream_connection_->waitForData(val.size());

  Buffer::OwnedImpl empty_buffer;
  ssl_client_->write(empty_buffer, true);
  while (!connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  fake_upstream_connection_->waitForHalfClose();
}

} // namespace
} // namespace Envoy

#include "source/common/network/connection_impl.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"

#include "contrib/envoy/extensions/filters/network/postgres_proxy/v3alpha/postgres_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/postgres_proxy/v3alpha/postgres_proxy.pb.validate.h"
#include "contrib/postgres_proxy/filters/network/test/postgres_test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

class PostgresBaseIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public BaseIntegrationTest {
public:
  using UpstreamSSLConfig = std::tuple<const absl::string_view, const absl::string_view>;

  std::string postgresConfig(bool terminate_ssl, bool add_start_tls_transport_socket,
                             UpstreamSSLConfig upstream_ssl_config) {
    std::string main_config = fmt::format(
        TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
            "contrib/postgres_proxy/filters/network/test/postgres_test_config.yaml")),
        Platform::null_device_path, Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getLoopbackAddressString(GetParam()),
        std::get<1>(upstream_ssl_config), // upstream SSL transport socket
        Network::Test::getAnyAddressString(GetParam()), terminate_ssl ? "true" : "false",
        std::get<0>(upstream_ssl_config)); // postgres filter's upstream_ssl option.

    if (add_start_tls_transport_socket) {
      main_config +=
          fmt::format(R"EOF(
      transport_socket:
        name: "starttls"
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.starttls.v3.StartTlsConfig
          cleartext_socket_config:
          tls_socket_config:
            common_tls_context:
              tls_certificates:
                certificate_chain:
                  filename: {}
                private_key:
                  filename: {}
   )EOF",
                      TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"),
                      TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
    }

    return main_config;
  }

  PostgresBaseIntegrationTest(bool terminate_ssl, bool add_starttls_transport_socket,
                              UpstreamSSLConfig upstream_ssl_config)
      : BaseIntegrationTest(GetParam(), postgresConfig(terminate_ssl, add_starttls_transport_socket,
                                                       upstream_ssl_config)) {
    skip_tag_extraction_rule_check_ = true;
  };

  void SetUp() override { BaseIntegrationTest::initialize(); }

  static constexpr absl::string_view empty_config_string_{""};
  static constexpr UpstreamSSLConfig NoUpstreamSSL{empty_config_string_, empty_config_string_};
  FakeRawConnectionPtr fake_upstream_connection_;
};

// Base class for tests with `terminate_ssl` disabled and without
// `starttls` transport socket.
class BasicPostgresIntegrationTest : public PostgresBaseIntegrationTest {
public:
  BasicPostgresIntegrationTest() : PostgresBaseIntegrationTest(false, false, NoUpstreamSSL) {}
};

// Test that the filter is properly chained and reacts to successful login
// message.
TEST_P(BasicPostgresIntegrationTest, Login) {
  std::string str;
  std::string recv;

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send the startup message.
  Buffer::OwnedImpl data;
  std::string rcvd;
  char buf[32];

  memset(buf, 0, sizeof(buf));
  // Add length.
  data.writeBEInt<uint32_t>(12);
  // Add 8 bytes of some data.
  data.add(buf, 8);
  ASSERT_TRUE(tcp_client->write(data.toString()));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.toString().length(), &rcvd));
  data.drain(data.length());

  // TCP session is up. Just send the AuthenticationOK downstream.
  data.add("R");
  // Add length.
  data.writeBEInt<uint32_t>(8);
  uint32_t code = 0;
  data.add(&code, sizeof(code));

  rcvd.clear();
  ASSERT_TRUE(fake_upstream_connection->write(data.toString()));
  rcvd.append(data.toString());
  tcp_client->waitForData(rcvd, true);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  // Make sure that the successful login bumped up the number of sessions.
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions", 1);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, BasicPostgresIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Base class for tests with `terminate_ssl` enabled and `starttls` transport socket added.
class SSLPostgresIntegrationTest : public PostgresBaseIntegrationTest {
public:
  SSLPostgresIntegrationTest() : PostgresBaseIntegrationTest(true, true, NoUpstreamSSL) {}
};

// Test verifies that Postgres filter replies with correct code upon
// receiving request to terminate SSL.
TEST_P(SSLPostgresIntegrationTest, TerminateSSL) {
  Buffer::OwnedImpl data;

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send the startup message requesting SSL.
  // Message is 8 bytes long. The first 4 bytes contain length (8)
  // The next 8 bytes contain message code (RequestSSL=80877103)
  data.writeBEInt<uint32_t>(8);
  data.writeBEInt<uint32_t>(80877103);

  // Message will be processed by Postgres filter which
  // is configured to accept SSL termination and confirm it
  // by returning single byte 'S'.
  ASSERT_TRUE(tcp_client->write(data.toString()));
  data.drain(data.length());

  tcp_client->waitForData("S", true);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  // Make sure that the successful login bumped up the number of sessions.
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_terminated_ssl", 1);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, SSLPostgresIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

class SSLWrongConfigPostgresIntegrationTest : public PostgresBaseIntegrationTest {
public:
  SSLWrongConfigPostgresIntegrationTest()
      : PostgresBaseIntegrationTest(true, false, NoUpstreamSSL) {}
};

// Test verifies that Postgres filter closes connection when it is configured to
// terminate SSL, but underlying transport socket does not allow for such operation.
TEST_P(SSLWrongConfigPostgresIntegrationTest, TerminateSSLNoStartTlsTransportSocket) {
  Buffer::OwnedImpl data;

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send the startup message requesting SSL.
  // Message is 8 bytes long. The first 4 bytes contain length (8)
  // The next 8 bytes contain message code (RequestSSL=80877103)
  data.writeBEInt<uint32_t>(8);
  data.writeBEInt<uint32_t>(80877103);

  // Message will be processed by Postgres filter which
  // is configured to accept SSL termination and confirm it
  // by returning single byte 'S'.
  // The write can see disconnect upon completion so we do
  // not verify the result.
  ASSERT_TRUE(tcp_client->write(data.toString(), false, false));
  data.drain(data.length());

  tcp_client->waitForData("S", true);

  tcp_client->waitForDisconnect();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_terminated_ssl", 0);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, SSLWrongConfigPostgresIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Upstream SSL integration tests.
class UpstreamSSLBaseIntegrationTest : public PostgresBaseIntegrationTest {
public:
  UpstreamSSLBaseIntegrationTest(UpstreamSSLConfig upstream_ssl_config)
      // Disable downstream SSL.
      : PostgresBaseIntegrationTest(false, false, upstream_ssl_config) {}

  std::unique_ptr<Ssl::ContextManager> tls_context_manager_;
  Network::DownstreamTransportSocketFactoryPtr tls_context_;
  void setupUpstreamTLSCtx() {
    // Setup factory and context for tls transport socket.
    // The tls transport socket will be inserted into fake_upstream when
    // upstream starttls transport socket is converted to secure mode.
    tls_context_manager_ =
        std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());

    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext downstream_tls_context;

    std::string yaml_plain = R"EOF(
  common_tls_context:
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/config/integration/certs/upstreamcacert.pem"
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/config/integration/certs/upstreamcert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/config/integration/certs/upstreamkey.pem"
)EOF";

    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml_plain), downstream_tls_context);

    NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
    ON_CALL(mock_factory_ctx, api()).WillByDefault(testing::ReturnRef(*api_));
    auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
        downstream_tls_context, mock_factory_ctx);
    static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
    tls_context_ = Network::DownstreamTransportSocketFactoryPtr{
        new Extensions::TransportSockets::Tls::ServerSslSocketFactory(
            std::move(cfg), *tls_context_manager_, *client_stats_store, {})};
  }
  void enableTLSOnFakeUpstream() {
    // Create TLS transport socket and install it in fake_upstream.
    Network::TransportSocketPtr ts = tls_context_->createDownstreamTransportSocket();

    // Synchronization object used to suspend execution
    // until dispatcher completes transport socket conversion.
    absl::Notification notification;

    // Execute transport socket conversion to TLS on the same thread where received data
    // is dispatched. Otherwise conversion may collide with data processing.
    fake_upstreams_[0]->dispatcher()->post([&]() {
      auto connection =
          dynamic_cast<Envoy::Network::ConnectionImpl*>(&fake_upstream_connection_->connection());
      connection->transportSocket() = std::move(ts);
      connection->transportSocket()->setTransportSocketCallbacks(*connection);
      notification.Notify();
    });

    // Wait until the transport socket conversion completes.
    notification.WaitForNotification();
  }
};

// Base class for tests with disabled upstream SSL. It should behave exactly
// as without any upstream configuration specified and pass
// messages in clear-text.
class UpstreamSSLDisabledPostgresIntegrationTest : public UpstreamSSLBaseIntegrationTest {
public:
  // Disable downstream SSL and upstream SSL.
  UpstreamSSLDisabledPostgresIntegrationTest()
      : UpstreamSSLBaseIntegrationTest(std::make_tuple("upstream_ssl: DISABLE", "")) {}
};

// Verify that postgres filter does not send any additional messages when
// upstream SSL is disabled.
TEST_P(UpstreamSSLDisabledPostgresIntegrationTest, BasicConnectivityTest) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  // Send the startup message.
  Buffer::OwnedImpl data;
  std::string rcvd;
  createInitialPostgresRequest(data);
  ASSERT_TRUE(tcp_client->write(data.toString()));
  // Make sure that upstream receives startup message in clear-text (no SSL negotiation takes
  // place).
  ASSERT_TRUE(fake_upstream_connection_->waitForData(data.toString().length(), &rcvd));
  data.drain(data.length());

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_success", 0);
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_failed", 0);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, UpstreamSSLDisabledPostgresIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

class UpstreamSSLRequirePostgresIntegrationTest : public UpstreamSSLBaseIntegrationTest {
public:
  UpstreamSSLRequirePostgresIntegrationTest()
      : UpstreamSSLBaseIntegrationTest(std::make_tuple("upstream_ssl: REQUIRE",
                                                       R"EOF(transport_socket:
      name: "starttls"
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.starttls.v3.UpstreamStartTlsConfig
        tls_socket_config:
          common_tls_context: {}
)EOF")) {}
};

TEST_P(UpstreamSSLRequirePostgresIntegrationTest, ServerAgreesForSSLTest) {
  setupUpstreamTLSCtx();
  // START
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  // Send the startup message.
  Buffer::OwnedImpl data;
  Buffer::OwnedImpl upstream_data;
  std::string rcvd;
  createInitialPostgresRequest(data);
  ASSERT_TRUE(tcp_client->write(data.toString()));
  // Postgres filter should buffer the original message and negotiate SSL upstream.
  // The first 4 bytes should be length on the message (8 bytes).
  // The next 4 bytes should be SSL code.
  ASSERT_TRUE(fake_upstream_connection_->waitForData(8, &rcvd));
  upstream_data.add(rcvd);
  ASSERT_EQ(8, upstream_data.peekBEInt<uint32_t>(0));
  ASSERT_EQ(80877103, upstream_data.peekBEInt<uint32_t>(4));
  upstream_data.drain(upstream_data.length());

  // Reply to Envoy with 'S' and attach TLS socket to upstream.
  upstream_data.add("S");
  ASSERT_EQ(1, upstream_data.length());
  ASSERT_TRUE(fake_upstream_connection_->write(upstream_data.toString()));

  enableTLSOnFakeUpstream();

  fake_upstream_connection_->clearData();
  ASSERT_TRUE(fake_upstream_connection_->waitForData(data.length(), &rcvd));
  // Make sure that upstream received initial postgres request, which
  // triggered upstream SSL negotiation and TLS handshake.
  ASSERT_EQ(data.toString(), rcvd);

  data.drain(data.length());

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_success", 1);
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_failed", 0);
}

TEST_P(UpstreamSSLRequirePostgresIntegrationTest, ServerDeniesSSLTest) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  // Send the startup message.
  Buffer::OwnedImpl data;
  Buffer::OwnedImpl upstream_data;
  std::string rcvd;
  createInitialPostgresRequest(data);
  ASSERT_TRUE(tcp_client->write(data.toString()));
  // Postgres filter should buffer the original message and negotiate SSL upstream.
  // The first 4 bytes should be length on the message (8 bytes).
  // The next 4 bytes should be SSL code.
  ASSERT_TRUE(fake_upstream_connection_->waitForData(8, &rcvd));
  upstream_data.add(rcvd);
  ASSERT_EQ(8, upstream_data.peekBEInt<uint32_t>(0));
  ASSERT_EQ(80877103, upstream_data.peekBEInt<uint32_t>(4));
  upstream_data.drain(upstream_data.length());

  // Reply to Envoy with 'S' and attach TLS socket to upstream.
  upstream_data.add("E");
  ASSERT_EQ(1, upstream_data.length());
  ASSERT_TRUE(fake_upstream_connection_->write(upstream_data.toString()));

  data.drain(data.length());

  // Connection to client should be closed.
  tcp_client->waitForDisconnect();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_success", 0);
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_failed", 1);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, UpstreamSSLRequirePostgresIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

class UpstreamSSLPreferPostgresIntegrationTest : public UpstreamSSLBaseIntegrationTest {
public:
  UpstreamSSLPreferPostgresIntegrationTest()
      : UpstreamSSLBaseIntegrationTest(std::make_tuple("upstream_ssl: PREFER",
                                                       R"EOF(transport_socket:
      name: "starttls"
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.starttls.v3.UpstreamStartTlsConfig
        tls_socket_config:
          common_tls_context: {}
)EOF")) {}
  std::unique_ptr<Ssl::ContextManager> tls_context_manager_;
  Network::DownstreamTransportSocketFactoryPtr tls_context_;
};

TEST_P(UpstreamSSLPreferPostgresIntegrationTest, ServerAgreesForSSLTest) {
  setupUpstreamTLSCtx();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  // Send the startup message.
  Buffer::OwnedImpl data;
  Buffer::OwnedImpl upstream_data;
  std::string rcvd;
  createInitialPostgresRequest(data);
  ASSERT_TRUE(tcp_client->write(data.toString()));
  // Postgres filter should buffer the original message and negotiate SSL upstream.
  // The first 4 bytes should be length on the message (8 bytes).
  // The next 4 bytes should be SSL code.
  ASSERT_TRUE(fake_upstream_connection_->waitForData(8, &rcvd));
  upstream_data.add(rcvd);
  ASSERT_EQ(8, upstream_data.peekBEInt<uint32_t>(0));
  ASSERT_EQ(80877103, upstream_data.peekBEInt<uint32_t>(4));
  upstream_data.drain(upstream_data.length());

  // Reply to Envoy with 'S' and attach TLS socket to upstream.
  upstream_data.add("S");
  ASSERT_EQ(1, upstream_data.length());
  ASSERT_TRUE(fake_upstream_connection_->write(upstream_data.toString()));

  enableTLSOnFakeUpstream();

  fake_upstream_connection_->clearData();
  ASSERT_TRUE(fake_upstream_connection_->waitForData(data.length(), &rcvd));
  // Make sure that upstream received initial postgres request, which
  // triggered upstream SSL negotiation and TLS handshake.
  ASSERT_EQ(data.toString(), rcvd);

  data.drain(data.length());

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_success", 1);
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_failed", 0);
}

TEST_P(UpstreamSSLPreferPostgresIntegrationTest, ServerDeniesSSLTest) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  // Send the startup message.
  Buffer::OwnedImpl data;
  Buffer::OwnedImpl upstream_data;
  std::string rcvd;
  createInitialPostgresRequest(data);
  ASSERT_TRUE(tcp_client->write(data.toString()));
  // Postgres filter should buffer the original message and negotiate SSL upstream.
  // The first 4 bytes should be length on the message (8 bytes).
  // The next 4 bytes should be SSL code.
  ASSERT_TRUE(fake_upstream_connection_->waitForData(8, &rcvd));
  upstream_data.add(rcvd);
  ASSERT_EQ(8, upstream_data.peekBEInt<uint32_t>(0));
  ASSERT_EQ(80877103, upstream_data.peekBEInt<uint32_t>(4));
  upstream_data.drain(upstream_data.length());

  // Reply to Envoy with 'S' and attach TLS socket to upstream.
  upstream_data.add("E");
  ASSERT_EQ(1, upstream_data.length());
  ASSERT_TRUE(fake_upstream_connection_->write(upstream_data.toString()));

  fake_upstream_connection_->clearData();
  ASSERT_TRUE(fake_upstream_connection_->waitForData(data.length(), &rcvd));
  // Make sure that upstream received initial postgres request, which
  // triggered upstream SSL negotiation and TLS handshake.
  ASSERT_EQ(data.toString(), rcvd);

  data.drain(data.length());

  // Connection to client should be closed.
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_success", 0);
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_failed", 1);
}

TEST_P(UpstreamSSLPreferPostgresIntegrationTest, ServerSendsWrongReplySSLTest) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  // Send the startup message.
  Buffer::OwnedImpl data;
  Buffer::OwnedImpl upstream_data;
  std::string rcvd;
  createInitialPostgresRequest(data);
  ASSERT_TRUE(tcp_client->write(data.toString()));
  // Postgres filter should buffer the original message and negotiate SSL upstream.
  // The first 4 bytes should be length on the message (8 bytes).
  // The next 4 bytes should be SSL code.
  ASSERT_TRUE(fake_upstream_connection_->waitForData(8, &rcvd));
  upstream_data.add(rcvd);
  ASSERT_EQ(8, upstream_data.peekBEInt<uint32_t>(0));
  ASSERT_EQ(80877103, upstream_data.peekBEInt<uint32_t>(4));
  upstream_data.drain(upstream_data.length());

  // Reply to Envoy with 'S' and attach TLS socket to upstream.
  upstream_data.add("W");
  ASSERT_EQ(1, upstream_data.length());
  ASSERT_TRUE(fake_upstream_connection_->write(upstream_data.toString()));

  fake_upstream_connection_->clearData();
  ASSERT_TRUE(fake_upstream_connection_->waitForData(data.length(), &rcvd));
  // Make sure that upstream received initial postgres request, which
  // triggered upstream SSL negotiation and TLS handshake.
  ASSERT_EQ(data.toString(), rcvd);

  data.drain(data.length());

  // Connection to client should be closed.
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_success", 0);
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_failed", 1);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, UpstreamSSLPreferPostgresIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));
} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

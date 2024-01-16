#include "source/common/network/connection_impl.h"
#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"

#include "contrib/envoy/extensions/filters/network/postgres_proxy/v3alpha/postgres_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/postgres_proxy/v3alpha/postgres_proxy.pb.validate.h"
#include "contrib/postgres_proxy/filters/network/test/postgres_integration_test.pb.h"
#include "contrib/postgres_proxy/filters/network/test/postgres_integration_test.pb.validate.h"
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
  // Tuple to store upstream and downstream startTLS configuration.
  // The first string contains string to enable/disable SSL.
  // The second string contains transport socket configuration.
  using SSLConfig = std::tuple<const absl::string_view, const absl::string_view>;

  std::string postgresConfig(SSLConfig downstream_ssl_config, SSLConfig upstream_ssl_config,
                             std::string additional_filters) {
    std::string main_config = fmt::format(
        TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
            "contrib/postgres_proxy/filters/network/test/postgres_test_config.yaml-template")),
        Platform::null_device_path, Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getLoopbackAddressString(GetParam()),
        std::get<1>(upstream_ssl_config), // upstream SSL transport socket
        Network::Test::getAnyAddressString(GetParam()),
        std::get<0>(downstream_ssl_config),  // downstream SSL termination
        std::get<0>(upstream_ssl_config),    // upstream_SSL option
        additional_filters,                  // additional filters to insert after postgres
        std::get<1>(downstream_ssl_config)); // downstream SSL transport socket

    return main_config;
  }

  PostgresBaseIntegrationTest(SSLConfig downstream_ssl_config, SSLConfig upstream_ssl_config,
                              std::string additional_filters = "")
      : BaseIntegrationTest(GetParam(), postgresConfig(downstream_ssl_config, upstream_ssl_config,
                                                       additional_filters)) {
    skip_tag_extraction_rule_check_ = true;
  };

  void SetUp() override { BaseIntegrationTest::initialize(); }

  static constexpr absl::string_view empty_config_string_{""};
  static constexpr SSLConfig NoUpstreamSSL{empty_config_string_, empty_config_string_};
  static constexpr SSLConfig NoDownstreamSSL{empty_config_string_, empty_config_string_};
  FakeRawConnectionPtr fake_upstream_connection_;
};

// Base class for tests with `terminate_ssl` disabled and without
// `starttls` transport socket.
class BasicPostgresIntegrationTest : public PostgresBaseIntegrationTest {
public:
  BasicPostgresIntegrationTest() : PostgresBaseIntegrationTest(NoDownstreamSSL, NoUpstreamSSL) {}
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
class DownstreamSSLPostgresIntegrationTest : public PostgresBaseIntegrationTest {
public:
  DownstreamSSLPostgresIntegrationTest()
      : PostgresBaseIntegrationTest(
            std::make_tuple(
                "terminate_ssl: true",
                fmt::format(
                    R"EOF(transport_socket:
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
                    TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"))),
            NoUpstreamSSL) {}
};

// Test verifies that Postgres filter replies with correct code upon
// receiving request to terminate SSL.
TEST_P(DownstreamSSLPostgresIntegrationTest, TerminateSSL) {
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

INSTANTIATE_TEST_SUITE_P(IpVersions, DownstreamSSLPostgresIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

class DownstreamSSLWrongConfigPostgresIntegrationTest : public PostgresBaseIntegrationTest {
public:
  DownstreamSSLWrongConfigPostgresIntegrationTest()
      // Enable SSL termination but do not configure downstream transport socket.
      : PostgresBaseIntegrationTest(std::make_tuple("terminate_ssl: true", ""), NoUpstreamSSL) {}
};

// Test verifies that Postgres filter closes connection when it is configured to
// terminate SSL, but underlying transport socket does not allow for such operation.
TEST_P(DownstreamSSLWrongConfigPostgresIntegrationTest, TerminateSSLNoStartTlsTransportSocket) {
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

INSTANTIATE_TEST_SUITE_P(IpVersions, DownstreamSSLWrongConfigPostgresIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Upstream SSL integration tests.
// Tests do not use the real postgres server and concentrate only on initial exchange.
// The initial packet
// sent by the downstream client, SSL request sent to fake upstream and SSL response sent back by
// fake client  are valid postgres payloads, because they must be parsed by postgres filter.

class UpstreamSSLBaseIntegrationTest : public PostgresBaseIntegrationTest {
public:
  UpstreamSSLBaseIntegrationTest(SSLConfig upstream_ssl_config,
                                 SSLConfig downstream_ssl_config = NoDownstreamSSL)
      // Disable downstream SSL and attach synchronization filter.
      : PostgresBaseIntegrationTest(downstream_ssl_config, upstream_ssl_config, R"EOF(
      -  name: sync
         typed_config:
           "@type": type.googleapis.com/test.integration.postgres.SyncWriteFilterConfig
)EOF") {}

  // Helper synchronization filter which is injected between postgres filter and tcp proxy.
  // Its goal is to eliminate race conditions and synchronize operations between fake upstream and
  // postgres filter.
  struct SyncWriteFilter : public Network::WriteFilter {
    SyncWriteFilter(absl::Notification& proceed_sync, absl::Notification& recv_sync)
        : proceed_sync_(proceed_sync), recv_sync_(recv_sync) {}

    Network::FilterStatus onWrite(Buffer::Instance& data, bool) override {
      if (data.length() > 0) {
        // Notify fake upstream that payload has been received.
        recv_sync_.Notify();
        // Wait for signal to continue. This is to give fake upstream
        // some time to create and attach TLS transport socket.
        proceed_sync_.WaitForNotification();
      }
      return Network::FilterStatus::Continue;
    }

    void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
      read_callbacks_ = &callbacks;
    }

    Network::WriteFilterCallbacks* read_callbacks_{};
    // Synchronization object used to stop Envoy processing to allow fake upstream to
    // create and attach TLS transport socket.
    absl::Notification& proceed_sync_;
    // Synchronization object used to notify fake upstream that a message sent
    // by fake upstream was received by Envoy.
    absl::Notification& recv_sync_;
  };

  // Config factory for sync helper filter.
  class SyncWriteFilterConfigFactory : public Extensions::NetworkFilters::Common::FactoryBase<
                                           test::integration::postgres::SyncWriteFilterConfig> {
  public:
    explicit SyncWriteFilterConfigFactory(const std::string& name,
                                          Network::ConnectionCallbacks& /* upstream_callbacks*/)
        : FactoryBase(name) {}

    Network::FilterFactoryCb
    createFilterFactoryFromProtoTyped(const test::integration::postgres::SyncWriteFilterConfig&,
                                      Server::Configuration::FactoryContext&) override {
      return [&](Network::FilterManager& filter_manager) -> void {
        filter_manager.addWriteFilter(std::make_shared<SyncWriteFilter>(proceed_sync_, recv_sync_));
      };
    }

    std::string name() const override { return name_; }

    // See SyncWriteFilter for purpose and description of the following sync objects.
    absl::Notification proceed_sync_, recv_sync_;

  private:
    const std::string name_;
  };

  // Method prepares TLS context to be injected to fake upstream.
  // Method creates and attaches TLS transport socket to fake upstream.
  void enableTLSOnFakeUpstream() {
    // Setup factory and context for tls transport socket.
    // The tls transport socket will be inserted into fake_upstream when
    // Envoy's upstream starttls transport socket is converted to secure mode.
    std::unique_ptr<Ssl::ContextManager> tls_context_manager =
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
    ON_CALL(mock_factory_ctx.server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
    auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
        downstream_tls_context, mock_factory_ctx);
    static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
    Network::DownstreamTransportSocketFactoryPtr tls_context =
        Network::DownstreamTransportSocketFactoryPtr{
            new Extensions::TransportSockets::Tls::ServerSslSocketFactory(
                std::move(cfg), *tls_context_manager, *(client_stats_store->rootScope()), {})};

    Network::TransportSocketPtr ts = tls_context->createDownstreamTransportSocket();
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

  NiceMock<Network::MockConnectionCallbacks> upstream_callbacks_;
  SyncWriteFilterConfigFactory config_factory_{"sync", upstream_callbacks_};
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory>
      registered_config_factory_{config_factory_};
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
// upstream SSL is disabled. Fake upstream should receive only the initial
// postgres message.
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

// Base class for parameterized tests with REQUIRE option for upstream SSL.
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

// Test verifies that postgres filter starts upstream SSL negotiation with
// fake upstream upon receiving initial postgres packet. When server agrees
// to use SSL, TLS transport socket is attached to fake upstream and
// fake upstream receives initial postgres packet over encrypted connection.
TEST_P(UpstreamSSLRequirePostgresIntegrationTest, ServerAgreesForSSLTest) {
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
  fake_upstream_connection_->clearData();

  // Reply to Envoy with 'S' and attach TLS socket to upstream.
  upstream_data.add("S");
  ASSERT_TRUE(fake_upstream_connection_->write(upstream_data.toString()));

  config_factory_.recv_sync_.WaitForNotification();
  enableTLSOnFakeUpstream();
  config_factory_.proceed_sync_.Notify();

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

// Test verifies that postgres filter will not continue when upstream SSL
// is required and fake upstream does not agree for SSL.
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

  // Reply to Envoy with 'E' (SSL not allowed).
  upstream_data.add("E");
  ASSERT_TRUE(fake_upstream_connection_->write(upstream_data.toString()));
  config_factory_.proceed_sync_.Notify();

  data.drain(data.length());

  // Connection to client should be closed.
  tcp_client->waitForDisconnect();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_success", 0);
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_failed", 1);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, UpstreamSSLRequirePostgresIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Base class for parameterized tests when upstream and downstream SSL is enabled.
class UpstreamAndDownstreamSSLIntegrationTest : public UpstreamSSLBaseIntegrationTest {
public:
  UpstreamAndDownstreamSSLIntegrationTest()
      : UpstreamSSLBaseIntegrationTest(
            // Configure upstream SSL
            std::make_tuple("upstream_ssl: REQUIRE",
                            R"EOF(transport_socket:
      name: "starttls"
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.starttls.v3.UpstreamStartTlsConfig
        tls_socket_config:
          common_tls_context: {}
    )EOF"),
            // configure downstream SSL
            std::make_tuple(
                "terminate_ssl: true",
                fmt::format(
                    R"EOF(transport_socket:
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
                    TestEnvironment::runfilesPath(
                        "test/config/integration/certs/serverkey.pem")))) {}

  // Method changes IntegrationTcpClient's transport socket to TLS.
  // Sending any traffic to newly attached TLS transport socket will trigger
  // TLS handshake negotiation.
  void enableTLSonTCPClient(const IntegrationTcpClientPtr& tcp_client) {
    // Setup factory and context for tls transport socket.
    // The tls transport socket will be inserted into fake_upstream when
    // Envoy's upstream starttls transport socket is converted to secure mode.
    std::unique_ptr<Ssl::ContextManager> tls_context_manager =
        std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());

    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext upstream_tls_context;

    NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
    ON_CALL(mock_factory_ctx.server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
    auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ClientContextConfigImpl>(
        upstream_tls_context, mock_factory_ctx);
    static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
    Network::UpstreamTransportSocketFactoryPtr tls_context =
        Network::UpstreamTransportSocketFactoryPtr{
            new Extensions::TransportSockets::Tls::ClientSslSocketFactory(
                std::move(cfg), *tls_context_manager, *(client_stats_store->rootScope()))};

    Network::TransportSocketOptionsConstSharedPtr options;

    auto connection = dynamic_cast<Envoy::Network::ConnectionImpl*>(tcp_client->connection());
    Network::TransportSocketPtr ts = tls_context->createTransportSocket(
        options, connection->streamInfo().upstreamInfo()->upstreamHost());
    connection->transportSocket() = std::move(ts);
    connection->transportSocket()->setTransportSocketCallbacks(*connection);
  }
};

// Integration test when both downstream and upstream SSL is enabled.
// In this scenario test client establishes SSL connection to Envoy. Envoy de-crypts the traffic.
// The traffic is encrypted again when sent to upstream server.
// The test follows the following scenario:
//
// Test client                     Envoy                  Upstream
// ----- Can I use SSL? ------------>
// <------- Yes---------------------
// <------- TLS handshake ---------->
// ------ Initial postgres msg ----->
//                                    ------ Can I use SSL? --->
//                                    <------- Yes--------------
//                                    <------- TLS handshake--->
//                                    --Initial postgres msg--->
// ------ close connection --------->
//                                    ------ close connection--->
//
TEST_P(UpstreamAndDownstreamSSLIntegrationTest, ServerAgreesForSSL) {
  Buffer::OwnedImpl data;

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

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

  // Attach TLS to tcp_client.
  enableTLSonTCPClient(tcp_client);

  // Write initial postgres message. This should trigger SSL negotiation between test TCP client and
  // Envoy downstream transport socket. Postgres filter should ask the fake upstream server if it
  // will accept encrypted connection.
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
  ASSERT_TRUE(fake_upstream_connection_->write(upstream_data.toString()));
  fake_upstream_connection_->clearData();

  config_factory_.recv_sync_.WaitForNotification();
  enableTLSOnFakeUpstream();
  config_factory_.proceed_sync_.Notify();

  ASSERT_TRUE(fake_upstream_connection_->waitForData(data.length(), &rcvd));
  // Make sure that upstream received initial postgres request, which
  // triggered upstream SSL negotiation and TLS handshake.
  ASSERT_EQ(data.toString(), rcvd);

  data.drain(data.length());

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_terminated_ssl", 1);
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_success", 1);
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_failed", 0);
}

// Integration test when both downstream and upstream SSL is enabled.
// In this scenario test client establishes SSL connection to Envoy. Envoy de-crypts the traffic.
// The traffic is encrypted again when sent to upstream server, but the server
// rejects request for SSL.
// The test follows the following scenario:
//
// Test client                     Envoy                  Upstream
// ----- Can I use SSL? ------------>
// <------- Yes---------------------
// <------- TLS handshake ---------->
// ------ Initial postgres msg ----->
//                                    ------ Can I use SSL? --->
//                                    <------- No---------------
// <----- close connection ----------
//                                    ------ close connection--->
TEST_P(UpstreamAndDownstreamSSLIntegrationTest, ServerRejectsSSL) {
  Buffer::OwnedImpl data;

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

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

  // Attach TLS to tcp_client.
  enableTLSonTCPClient(tcp_client);

  // Write initial postgres message. This should trigger SSL negotiation between test TCP client and
  // Envoy downstream transport socket. Postgres filter should ask the fake upstream server if it
  // will accept encrypted connection.
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

  // Reply to Envoy with 'E' (SSL not allowed).
  upstream_data.add("E");
  ASSERT_TRUE(fake_upstream_connection_->write(upstream_data.toString()));
  config_factory_.proceed_sync_.Notify();

  data.drain(data.length());

  // Envoy should close the connection to test client.
  tcp_client->waitForDisconnect();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_terminated_ssl", 1);
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_success", 0);
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_upstream_ssl_failed", 1);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, UpstreamAndDownstreamSSLIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

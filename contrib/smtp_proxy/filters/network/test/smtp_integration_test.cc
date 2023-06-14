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

#include "contrib/envoy/extensions/filters/network/smtp_proxy/v3alpha/smtp_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/smtp_proxy/v3alpha/smtp_proxy.pb.validate.h"
#include "contrib/smtp_proxy/filters/network/test/smtp_integration_test.pb.h"
#include "contrib/smtp_proxy/filters/network/test/smtp_integration_test.pb.validate.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

class SmtpBaseIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public BaseIntegrationTest {
public:
  // Tuple to store upstream and downstream startTLS configuration.
  // The first string contains string to enable/disable SSL.
  // The second string contains transport socket configuration.
  using SSLConfig = std::tuple<const absl::string_view, const absl::string_view>;

  std::string smtpConfig(SSLConfig downstream_ssl_config, SSLConfig upstream_ssl_config,
                         std::string additional_filters) {
    std::string main_config =
        fmt::format(TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
                        "contrib/smtp_proxy/filters/network/test/smtp_test_config.yaml-template")),
                    Platform::null_device_path, Network::Test::getLoopbackAddressString(GetParam()),
                    Network::Test::getLoopbackAddressString(GetParam()),
                    std::get<1>(upstream_ssl_config), // upstream SSL transport socket
                    Network::Test::getAnyAddressString(GetParam()),
                    std::get<0>(downstream_ssl_config),  // downstream SSL termination
                    std::get<0>(upstream_ssl_config),    // upstream_SSL option
                    additional_filters,                  // additional filters to insert after smtp
                    std::get<1>(downstream_ssl_config)); // downstream SSL transport socket

    return main_config;
  }

  SmtpBaseIntegrationTest(SSLConfig downstream_ssl_config, SSLConfig upstream_ssl_config,
                          std::string additional_filters = "")
      : BaseIntegrationTest(GetParam(), smtpConfig(downstream_ssl_config, upstream_ssl_config,
                                                   additional_filters)) {
    skip_tag_extraction_rule_check_ = true;
  };

  void SetUp() override { BaseIntegrationTest::initialize(); }

  static constexpr absl::string_view empty_config_string_{""};
  static constexpr SSLConfig NoUpstreamSSL{empty_config_string_, empty_config_string_};
  static constexpr SSLConfig NoDownstreamSSL{empty_config_string_, empty_config_string_};
  FakeRawConnectionPtr fake_upstream_connection_;
};

// Base class for tests with upstream and downstream ssl termination disabled and without
// `starttls` transport socket.
class BasicSmtpIntegrationTest : public SmtpBaseIntegrationTest {
public:
  BasicSmtpIntegrationTest() : SmtpBaseIntegrationTest(NoDownstreamSSL, NoUpstreamSSL) {}
};

// Test that the filter is properly chained and handles capabilities
// negotiation with TLS termination disabled.
TEST_P(BasicSmtpIntegrationTest, NoTls) {
  std::string str;
  std::string recv;

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string greeting("200 upstream esmtp\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(greeting));
  tcp_client->waitForData(greeting, true);
  tcp_client->clearData();

  std::string ehlo("EHLO example.com\r\n");
  ASSERT_TRUE(tcp_client->write(ehlo));

  std::string rcvd;
  ASSERT_TRUE(fake_upstream_connection->waitForData(ehlo.size(), &rcvd));
  fake_upstream_connection->clearData();
  EXPECT_EQ(ehlo, rcvd);
  rcvd.clear();

  std::string ehlo_resp("200-upstream at your service\r\n"
                        "200-PIPELINING\r\n"
                        "200 STARTTLS\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(ehlo_resp));
  tcp_client->waitForData(ehlo_resp, true);
  tcp_client->clearData();

  std::string mail("MAIL FROM:<alice@example.com>\r\n");
  ASSERT_TRUE(tcp_client->write(mail));
  ASSERT_TRUE(fake_upstream_connection->waitForData(mail.size(), &rcvd));
  fake_upstream_connection->clearData();
  EXPECT_EQ(mail, rcvd);
  rcvd.clear();

  std::string mail_resp("200 ok\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(mail_resp));
  tcp_client->waitForData(mail_resp, true);
  tcp_client->clearData();

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  test_server_->waitForCounterEq("smtp.smtp_stats.sessions", 1);
  test_server_->waitForCounterEq("smtp.smtp_stats.sessions_esmtp_unencrypted", 1);
}
INSTANTIATE_TEST_SUITE_P(IpVersions, BasicSmtpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Base class for tests with `downstream_ssl` enabled and `starttls` transport socket added.
class DownstreamSSLSmtpIntegrationTest : public SmtpBaseIntegrationTest {
public:
  DownstreamSSLSmtpIntegrationTest()
      : SmtpBaseIntegrationTest(
            std::make_tuple(
                "downstream_ssl: ENABLE",
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

// Test verifies that the Smtp proxy filter replies with the correct code upon
// receiving request to terminate SSL.
TEST_P(DownstreamSSLSmtpIntegrationTest, TerminateSSL) {
  Buffer::OwnedImpl data;

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string greeting("200 upstream esmtp\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(greeting));
  tcp_client->waitForData(greeting, true);
  tcp_client->clearData();

  std::string ehlo("EHLO example.com\r\n");
  ASSERT_TRUE(tcp_client->write(ehlo));

  std::string rcvd;
  ASSERT_TRUE(fake_upstream_connection->waitForData(ehlo.size(), &rcvd));
  fake_upstream_connection->clearData();
  EXPECT_EQ(ehlo, rcvd);
  rcvd.clear();

  std::string ehlo_resp("200-upstream at your service\r\n"
                        "200 PIPELINING\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(ehlo_resp));
  // SmtpFilter appends STARTTLS to the list of capabilities returned
  // by the upstream
  std::string updated_ehlo_resp("200-upstream at your service\r\n"
                                "200-PIPELINING\r\n"
                                "200 STARTTLS\r\n");
  tcp_client->waitForData(updated_ehlo_resp, true);
  tcp_client->clearData();

  std::string starttls("STARTTLS\r\n");
  ASSERT_TRUE(tcp_client->write(starttls));

  // SmtpFilter responds to the STARTTLS command directly.
  std::string starttls_resp("220 envoy ready for tls\r\n");
  tcp_client->waitForData(starttls_resp, true);
  tcp_client->clearData();

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  test_server_->waitForCounterEq("smtp.smtp_stats.sessions", 1);
  test_server_->waitForCounterEq("smtp.smtp_stats.sessions_downstream_terminated_ssl", 1);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, DownstreamSSLSmtpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

class DownstreamSSLWrongConfigSmtpIntegrationTest : public SmtpBaseIntegrationTest {
public:
  DownstreamSSLWrongConfigSmtpIntegrationTest()
      // Enable SSL termination but do not configure downstream transport socket.
      : SmtpBaseIntegrationTest(std::make_tuple("downstream_ssl: ENABLE", ""), NoUpstreamSSL) {}
};

// Test verifies that Smtp filter closes connection when it is configured to
// terminate SSL, but underlying transport socket does not allow for such operation.
TEST_P(DownstreamSSLWrongConfigSmtpIntegrationTest, TerminateSSLNoStartTlsTransportSocket) {
  Buffer::OwnedImpl data;

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string greeting("200 upstream esmtp\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(greeting));
  tcp_client->waitForData(greeting, true);
  tcp_client->clearData();

  std::string ehlo("EHLO example.com\r\n");
  ASSERT_TRUE(tcp_client->write(ehlo));

  std::string rcvd;
  ASSERT_TRUE(fake_upstream_connection->waitForData(ehlo.size(), &rcvd));
  fake_upstream_connection->clearData();
  EXPECT_EQ(ehlo, rcvd);
  rcvd.clear();

  std::string ehlo_resp("200-upstream at your service\r\n"
                        "200 PIPELINING\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(ehlo_resp));
  std::string updated_ehlo_resp("200-upstream at your service\r\n"
                                "200-PIPELINING\r\n"
                                "200 STARTTLS\r\n");
  tcp_client->waitForData(updated_ehlo_resp, true);
  tcp_client->clearData();

  std::string starttls("STARTTLS\r\n");
  ASSERT_TRUE(tcp_client->write(starttls));

  tcp_client->waitForDisconnect();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  test_server_->waitForCounterEq("smtp.smtp_stats.sessions_downstream_terminated_ssl", 0);
  test_server_->waitForCounterEq("smtp.smtp_stats.sessions_downstream_ssl_err", 1);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, DownstreamSSLWrongConfigSmtpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Upstream SSL integration tests.
// Tests do not use the real smtp server and concentrate only on the SMTP capabilities negotiation
// and STARTTLS upgrade. The initial command sent by the downstream client, SSL request sent to fake
// upstream and SSL response sent back by fake client are valid SMTP because they must be parsed by
// smtp filter.

class UpstreamSSLBaseIntegrationTest : public SmtpBaseIntegrationTest {
public:
  UpstreamSSLBaseIntegrationTest(SSLConfig upstream_ssl_config)
      // Disable downstream SSL and attach synchronization filter.
      : SmtpBaseIntegrationTest(NoDownstreamSSL, upstream_ssl_config, R"EOF(
      -  name: sync
         typed_config:
           "@type": type.googleapis.com/test.integration.smtp.SyncWriteFilterConfig
)EOF") {}

  // Helper synchronization filter which is injected between smtp filter and tcp proxy.
  // Its goal is to eliminate race conditions and synchronize operations between fake upstream and
  // smtp filter.
  struct SyncWriteFilter : public Network::WriteFilter {
    SyncWriteFilter(absl::Notification& proceed_sync, absl::Notification& recv_sync)
        : proceed_sync_(proceed_sync), recv_sync_(recv_sync) {}

    Network::FilterStatus onWrite(Buffer::Instance& data, bool) override {
      // if (data.length() > 0) {
      if (data.toString() == "200 upstream ready for tls\r\n") {
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
                                           test::integration::smtp::SyncWriteFilterConfig> {
  public:
    explicit SyncWriteFilterConfigFactory(const std::string& name,
                                          Network::ConnectionCallbacks& /* upstream_callbacks*/)
        : FactoryBase(name) {}

    Network::FilterFactoryCb
    createFilterFactoryFromProtoTyped(const test::integration::smtp::SyncWriteFilterConfig&,
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

// Base class for tests with upstream_ssl: DISABLE. It should behave exactly
// as without any upstream configuration specified and pass
// messages in clear-text.
class UpstreamSSLDisabledSmtpIntegrationTest : public UpstreamSSLBaseIntegrationTest {
public:
  // Disable downstream SSL and upstream SSL.
  UpstreamSSLDisabledSmtpIntegrationTest()
      : UpstreamSSLBaseIntegrationTest(std::make_tuple("upstream_ssl: DISABLE", "")) {}
};

// Verify that smtp filter does not send any additional messages when
// upstream SSL is disabled. Fake upstream should receive only the initial
// smtp message.
TEST_P(UpstreamSSLDisabledSmtpIntegrationTest, BasicConnectivityTest) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  // Send the startup message.
  Buffer::OwnedImpl data;
  std::string rcvd;

  std::string greeting("200 upstream esmtp\r\n");
  ASSERT_TRUE(fake_upstream_connection_->write(greeting));
  tcp_client->waitForData(greeting, true);
  tcp_client->clearData();

  std::string ehlo("EHLO example.com\r\n");
  ASSERT_TRUE(tcp_client->write(ehlo));

  ASSERT_TRUE(fake_upstream_connection_->waitForData(ehlo.size(), &rcvd));
  fake_upstream_connection_->clearData();
  EXPECT_EQ(ehlo, rcvd);
  rcvd.clear();

  std::string ehlo_resp("200-upstream at your service\r\n"
                        "200-PIPELINING\r\n"
                        "200 STARTTLS\r\n");
  ASSERT_TRUE(fake_upstream_connection_->write(ehlo_resp));
  tcp_client->waitForData(ehlo_resp, true);
  tcp_client->clearData();

  std::string mail("MAIL FROM:<alice@example.com>\r\n");
  ASSERT_TRUE(tcp_client->write(mail));
  ASSERT_TRUE(fake_upstream_connection_->waitForData(mail.size(), &rcvd));
  fake_upstream_connection_->clearData();
  EXPECT_EQ(mail, rcvd);
  rcvd.clear();

  std::string mail_resp("200 ok\r\n");
  ASSERT_TRUE(fake_upstream_connection_->write(mail_resp));
  tcp_client->waitForData(mail_resp, true);
  tcp_client->clearData();

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  test_server_->waitForCounterEq("smtp.smtp_stats.sessions_upstream_terminated_ssl", 0);
  test_server_->waitForCounterEq("smtp.smtp_stats.sessions_upstream_ssl_command_err", 0);
  test_server_->waitForCounterEq("smtp.smtp_stats.sessions_upstream_ssl_err", 0);
  test_server_->waitForCounterEq("smtp.smtp_stats.sessions_esmtp_unencrypted", 1);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, UpstreamSSLDisabledSmtpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Base class for parameterized tests with REQUIRE option for upstream SSL.
class UpstreamSSLRequireSmtpIntegrationTest : public UpstreamSSLBaseIntegrationTest {
public:
  UpstreamSSLRequireSmtpIntegrationTest()
      : UpstreamSSLBaseIntegrationTest(std::make_tuple("upstream_ssl: REQUIRE",
                                                       R"EOF(transport_socket:
      name: "starttls"
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.starttls.v3.UpstreamStartTlsConfig
        tls_socket_config:
          common_tls_context: {}
)EOF")) {}
};

// Test verifies that smtp filter starts the upstream SSL negotiation
// with the fake upstream upon receiving EHLO from downstream. When
// the server returns a success response to the STARTTLS smtp command,
// the TLS transport socket is attached to the fake upstream and the
// fake upstream receives the client's EHLO command over the encrypted
// connection.
TEST_P(UpstreamSSLRequireSmtpIntegrationTest, StarttlsSuccess) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  std::string greeting("200 upstream esmtp\r\n");
  ASSERT_TRUE(fake_upstream_connection_->write(greeting));
  tcp_client->waitForData(greeting, true);
  tcp_client->clearData();

  std::string ehlo("EHLO example.com\r\n");
  ASSERT_TRUE(tcp_client->write(ehlo));

  std::string rcvd;
  ASSERT_TRUE(fake_upstream_connection_->waitForData(ehlo.size(), &rcvd));
  fake_upstream_connection_->clearData();
  EXPECT_EQ(ehlo, rcvd);
  rcvd.clear();

  std::string ehlo_resp("200-upstream at your service\r\n"
                        "200-PIPELINING\r\n"
                        "200 STARTTLS\r\n");
  ASSERT_TRUE(fake_upstream_connection_->write(ehlo_resp));

  std::string starttls("STARTTLS\r\n");
  ASSERT_TRUE(fake_upstream_connection_->waitForData(starttls.size(), &rcvd));
  fake_upstream_connection_->clearData();
  EXPECT_EQ(starttls, rcvd);
  rcvd.clear();

  std::string starttls_resp("200 upstream ready for tls\r\n");
  ASSERT_TRUE(fake_upstream_connection_->write(starttls_resp));

  config_factory_.recv_sync_.WaitForNotification();
  enableTLSOnFakeUpstream();
  config_factory_.proceed_sync_.Notify();

  // envoy resends initial client EHLO
  ASSERT_TRUE(fake_upstream_connection_->waitForData(ehlo.size(), &rcvd));
  fake_upstream_connection_->clearData();
  EXPECT_EQ(ehlo, rcvd);
  rcvd.clear();

  std::string ehlo2_resp("200-upstream at your service\r\n"
                         "200 PIPELINING\r\n");
  ASSERT_TRUE(fake_upstream_connection_->write(ehlo2_resp));
  tcp_client->waitForData(ehlo2_resp, true);
  tcp_client->clearData();

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  test_server_->waitForCounterEq("smtp.smtp_stats.sessions_upstream_terminated_ssl", 1);
}

// Test verifies that smtp filter will not continue when upstream SSL
// is required and fake upstream returns an error to the STARTTLS smtp command.
TEST_P(UpstreamSSLRequireSmtpIntegrationTest, StarttlsCommandErr) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  std::string greeting("200 upstream esmtp\r\n");
  ASSERT_TRUE(fake_upstream_connection_->write(greeting));
  tcp_client->waitForData(greeting, true);
  tcp_client->clearData();

  std::string ehlo("EHLO example.com\r\n");
  ASSERT_TRUE(tcp_client->write(ehlo));

  std::string rcvd;
  ASSERT_TRUE(fake_upstream_connection_->waitForData(ehlo.size(), &rcvd));
  fake_upstream_connection_->clearData();
  EXPECT_EQ(ehlo, rcvd);
  rcvd.clear();

  std::string ehlo_resp("200-upstream at your service\r\n"
                        "200-PIPELINING\r\n"
                        "200 STARTTLS\r\n");
  ASSERT_TRUE(fake_upstream_connection_->write(ehlo_resp));

  std::string starttls("STARTTLS\r\n");
  ASSERT_TRUE(fake_upstream_connection_->waitForData(starttls.size(), &rcvd));
  fake_upstream_connection_->clearData();
  EXPECT_EQ(starttls, rcvd);
  rcvd.clear();

  std::string starttls_resp("400 privkey.pem: ENOENT\r\n");
  ASSERT_TRUE(fake_upstream_connection_->write(starttls_resp));

  // client at this point is waiting for the response to EHLO, the
  // filter notifies them at this point that upstream tls failed
  std::string starttls_error_resp("400 upstream STARTTLS error\r\n");
  tcp_client->waitForData(starttls_error_resp, true);
  tcp_client->clearData();

  // Connection to client should be closed.
  tcp_client->waitForDisconnect();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  test_server_->waitForCounterEq("smtp.smtp_stats.sessions_upstream_terminated_ssl", 0);
  test_server_->waitForCounterEq("smtp.smtp_stats.sessions_upstream_ssl_command_err", 1);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, UpstreamSSLRequireSmtpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

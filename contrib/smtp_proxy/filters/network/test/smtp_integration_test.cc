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
                         std::string additional_filters, absl::string_view tracing_config_string) {
    std::string main_config =
        fmt::format(TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
                        "contrib/smtp_proxy/filters/network/test/smtp_test_config.yaml-template")),
                    Platform::null_device_path, Network::Test::getLoopbackAddressString(GetParam()),
                    Network::Test::getLoopbackAddressString(GetParam()),
                    std::get<1>(upstream_ssl_config), // upstream SSL transport socket
                    Network::Test::getAnyAddressString(GetParam()), tracing_config_string,
                    std::get<0>(downstream_ssl_config),  // downstream SSL termination
                    std::get<0>(upstream_ssl_config),    // upstream_SSL option
                    additional_filters,                  // additional filters to insert after smtp
                    std::get<1>(downstream_ssl_config)); // downstream SSL transport socket

    return main_config;
  }

  SmtpBaseIntegrationTest(SSLConfig downstream_ssl_config, SSLConfig upstream_ssl_config,
                          std::string additional_filters = "",
                          absl::string_view tracing_config_string = "")
      : BaseIntegrationTest(GetParam(), smtpConfig(downstream_ssl_config, upstream_ssl_config,
                                                   additional_filters, tracing_config_string)) {
    skip_tag_extraction_rule_check_ = true;
  };

  void SetUp() override { BaseIntegrationTest::initialize(); }

  static constexpr absl::string_view empty_config_string_{""};
  static constexpr absl::string_view tracing_config_string_{""};
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

  std::string greeting("220 Hi! This is upstream.com mail server\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(greeting));
  tcp_client->waitForData(greeting, true);
  tcp_client->clearData();

  std::string ehlo("EHLO foo.com\r\n");
  ASSERT_TRUE(tcp_client->write(ehlo));

  std::string rcvd;
  ASSERT_TRUE(fake_upstream_connection->waitForData(ehlo.size(), &rcvd));
  fake_upstream_connection->clearData();
  EXPECT_EQ(ehlo, rcvd);
  rcvd.clear();

  std::string ehlo_resp("250-upstream.com at your service\r\n"
                        "250-8BITMIME\r\n"
                        "250-ENHANCEDSTATUSCODES\r\n"
                        "250 STARTTLS\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(ehlo_resp));
  tcp_client->waitForData(ehlo_resp, true);
  tcp_client->clearData();

  std::string mail("MAIL FROM:<alice@foo.com>\r\n");
  ASSERT_TRUE(tcp_client->write(mail));
  ASSERT_TRUE(fake_upstream_connection->waitForData(mail.size(), &rcvd));
  fake_upstream_connection->clearData();
  EXPECT_EQ(mail, rcvd);
  rcvd.clear();

  std::string mail_resp("250 2.0.0 Roger, accepting mail from <alice@foo.com>\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(mail_resp));
  tcp_client->waitForData(mail_resp, true);
  tcp_client->clearData();

  std::string rcpt("RCPT TO:<bob@bar.com>\r\n");
  ASSERT_TRUE(tcp_client->write(rcpt));
  ASSERT_TRUE(fake_upstream_connection->waitForData(rcpt.size(), &rcvd));
  fake_upstream_connection->clearData();
  EXPECT_EQ(rcpt, rcvd);
  rcvd.clear();

  std::string rcpt_resp("250 2.0.0 I'll make sure <bob@bar.com> gets this\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(rcpt_resp));
  tcp_client->waitForData(rcpt_resp, true);
  tcp_client->clearData();

  std::string data("DATA\r\n");
  ASSERT_TRUE(tcp_client->write(data));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.size(), &rcvd));
  fake_upstream_connection->clearData();
  EXPECT_EQ(data, rcvd);
  rcvd.clear();

  std::string data_resp("354 2.0.0 Go ahead. End your data with <CR><LF>.<CR><LF>\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(data_resp));
  tcp_client->waitForData(data_resp, true);
  tcp_client->clearData();

  std::string mail_body("Hello World, bla bla!\r\n..\r\n.\r\n");
  ASSERT_TRUE(tcp_client->write(mail_body));
  ASSERT_TRUE(fake_upstream_connection->waitForData(mail_body.size(), &rcvd));
  fake_upstream_connection->clearData();
  EXPECT_EQ(mail_body, rcvd);
  rcvd.clear();

  std::string mail_body_resp("250 2.0.0 OK: queued\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(mail_body_resp));
  tcp_client->waitForData(mail_body_resp, true);
  tcp_client->clearData();

  std::string quit("QUIT\r\n");
  ASSERT_TRUE(tcp_client->write(quit));
  ASSERT_TRUE(fake_upstream_connection->waitForData(quit.size(), &rcvd));
  fake_upstream_connection->clearData();
  EXPECT_EQ(quit, rcvd);
  rcvd.clear();

  std::string quit_resp("221 2.0.0 Bye!\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(quit_resp));
  tcp_client->waitForData(quit_resp, true);
  tcp_client->clearData();

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  test_server_->waitForCounterEq("smtp.smtp_stats.session_requests", 1);
  test_server_->waitForCounterEq("smtp.smtp_stats.session_completed", 1);
  test_server_->waitForCounterEq("smtp.smtp_stats.transaction_completed", 1);
}
INSTANTIATE_TEST_SUITE_P(IpVersions, BasicSmtpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Base class for tests with `downstream_ssl` REQUIRE and `starttls` transport socket added.
class DownstreamSSLRequiredSmtpIntegrationTest : public SmtpBaseIntegrationTest {
public:
  DownstreamSSLRequiredSmtpIntegrationTest()
      : SmtpBaseIntegrationTest(
            std::make_tuple(
                "downstream_tls: REQUIRE",
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

// Test verifies that the filter responds with 5xx code when client tries to send MAIL FROM command
// without doing STARTTLS first.
TEST_P(DownstreamSSLRequiredSmtpIntegrationTest, StarttlsRequired) {

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string greeting("220 Hi! This is upstream.com mail server\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(greeting));
  tcp_client->waitForData(greeting, true);
  tcp_client->clearData();

  std::string ehlo("EHLO foo.com\r\n");
  ASSERT_TRUE(tcp_client->write(ehlo));

  std::string rcvd;
  ASSERT_TRUE(fake_upstream_connection->waitForData(ehlo.size(), &rcvd));
  fake_upstream_connection->clearData();
  EXPECT_EQ(ehlo, rcvd);
  rcvd.clear();

  std::string ehlo_resp("250-upstream.com at your service\r\n"
                        "250-8BITMIME\r\n"
                        "250-ENHANCEDSTATUSCODES\r\n"
                        "250 STARTTLS\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(ehlo_resp));
  tcp_client->waitForData(ehlo_resp, true);
  tcp_client->clearData();

  std::string mail("MAIL FROM:<alice@foo.com>\r\n");
  ASSERT_TRUE(tcp_client->write(mail));
  // ASSERT_TRUE(fake_upstream_connection->waitForData(mail.size(), &rcvd));
  // fake_upstream_connection->clearData();
  // EXPECT_EQ(mail, rcvd);
  // rcvd.clear();

  std::string mail_resp("530 5.7.10 plain-text connection is not allowed for this server. Please "
                        "upgrade the connection to TLS\r\n");
  // ASSERT_TRUE(fake_upstream_connection->write(mail_resp));
  tcp_client->waitForData(mail_resp, true);
  tcp_client->clearData();

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  test_server_->waitForCounterEq("smtp.smtp_stats.session_requests", 1);
  test_server_->waitForCounterEq("smtp.smtp_stats.mail_req_rejected_due_to_non_tls", 1);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, DownstreamSSLRequiredSmtpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Base class for tests with `downstream_ssl` enabled and `starttls` transport socket added.
class DownstreamSSLSmtpIntegrationTest : public SmtpBaseIntegrationTest {
public:
  DownstreamSSLSmtpIntegrationTest()
      : SmtpBaseIntegrationTest(
            std::make_tuple(
                "downstream_tls: ENABLE",
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

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string greeting("220 Hi! This is upstream.com mail server\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(greeting));
  tcp_client->waitForData(greeting, true);
  tcp_client->clearData();

  std::string ehlo("EHLO foo.com\r\n");
  ASSERT_TRUE(tcp_client->write(ehlo));

  std::string rcvd;
  ASSERT_TRUE(fake_upstream_connection->waitForData(ehlo.size(), &rcvd));
  fake_upstream_connection->clearData();
  EXPECT_EQ(ehlo, rcvd);
  rcvd.clear();

  std::string ehlo_resp("250-upstream.com at your service\r\n"
                        "250-8BITMIME\r\n"
                        "250-ENHANCEDSTATUSCODES\r\n"
                        "250 STARTTLS\r\n");
  ASSERT_TRUE(fake_upstream_connection->write(ehlo_resp));
  tcp_client->waitForData(ehlo_resp, true);
  tcp_client->clearData();

  std::string starttls("STARTTLS\r\n");
  ASSERT_TRUE(tcp_client->write(starttls));

  std::string starttls_resp("220 2.0.0 Ready to start TLS\r\n");
  tcp_client->waitForData(starttls_resp, true);
  tcp_client->clearData();

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  test_server_->waitForCounterEq("smtp.smtp_stats.session_requests", 1);
  test_server_->waitForCounterEq("smtp.smtp_stats.downstream_tls_termination_success", 1);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, DownstreamSSLSmtpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// class DownstreamSSLWrongConfigSmtpIntegrationTest : public SmtpBaseIntegrationTest {
// public:
//   DownstreamSSLWrongConfigSmtpIntegrationTest()
//       // Enable SSL termination but do not configure downstream transport socket.
//       : SmtpBaseIntegrationTest(std::make_tuple("downstream_ssl: ENABLE", ""), NoUpstreamSSL) {}
// };

// // Test verifies that Smtp filter closes connection when it is configured to
// // terminate SSL, but underlying transport socket does not allow for such operation.
// TEST_P(DownstreamSSLWrongConfigSmtpIntegrationTest, TerminateSSLNoStartTlsTransportSocket) {
//   Buffer::OwnedImpl data;

//   IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
//   FakeRawConnectionPtr fake_upstream_connection;
//   ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

//   std::string greeting("200 upstream esmtp\r\n");
//   ASSERT_TRUE(fake_upstream_connection->write(greeting));
//   tcp_client->waitForData(greeting, true);
//   tcp_client->clearData();

//   std::string ehlo("EHLO example.com\r\n");
//   ASSERT_TRUE(tcp_client->write(ehlo));

//   std::string rcvd;
//   ASSERT_TRUE(fake_upstream_connection->waitForData(ehlo.size(), &rcvd));
//   fake_upstream_connection->clearData();
//   EXPECT_EQ(ehlo, rcvd);
//   rcvd.clear();

//   std::string ehlo_resp("200-upstream at your service\r\n"
//                         "200 PIPELINING\r\n");
//   ASSERT_TRUE(fake_upstream_connection->write(ehlo_resp));
//   std::string updated_ehlo_resp("200-upstream at your service\r\n"
//                                 "200-PIPELINING\r\n"
//                                 "200 STARTTLS\r\n");
//   tcp_client->waitForData(updated_ehlo_resp, true);
//   tcp_client->clearData();

//   std::string starttls("STARTTLS\r\n");
//   ASSERT_TRUE(tcp_client->write(starttls));

//   tcp_client->waitForDisconnect();
//   ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

//   test_server_->waitForCounterEq("smtp.smtp_stats.sessions_downstream_terminated_ssl", 0);
//   test_server_->waitForCounterEq("smtp.smtp_stats.sessions_downstream_ssl_err", 1);
// }

// INSTANTIATE_TEST_SUITE_P(IpVersions, DownstreamSSLWrongConfigSmtpIntegrationTest,
//                          testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

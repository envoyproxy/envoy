#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"

#include "contrib/envoy/extensions/filters/network/postgres_proxy/v3alpha/postgres_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/postgres_proxy/v3alpha/postgres_proxy.pb.validate.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

class PostgresBaseIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public BaseIntegrationTest {

  std::string postgresConfig(bool terminate_ssl, bool add_start_tls_transport_socket) {
    std::string main_config = fmt::format(
        TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
            "contrib/postgres_proxy/filters/network/test/postgres_test_config.yaml")),
        Platform::null_device_path, Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getAnyAddressString(GetParam()), terminate_ssl ? "true" : "false");

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

public:
  PostgresBaseIntegrationTest(bool terminate_ssl, bool add_starttls_transport_socket)
      : BaseIntegrationTest(GetParam(),
                            postgresConfig(terminate_ssl, add_starttls_transport_socket)) {
    skip_tag_extraction_rule_check_ = true;
  };

  void SetUp() override { BaseIntegrationTest::initialize(); }
};

// Base class for tests with `terminate_ssl` disabled and without
// `starttls` transport socket.
class BasicPostgresIntegrationTest : public PostgresBaseIntegrationTest {
public:
  BasicPostgresIntegrationTest() : PostgresBaseIntegrationTest(false, false) {}
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
  SSLPostgresIntegrationTest() : PostgresBaseIntegrationTest(true, true) {}
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
  SSLWrongConfigPostgresIntegrationTest() : PostgresBaseIntegrationTest(true, false) {}
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

  // Make sure that the successful login bumped up the number of sessions.
  test_server_->waitForCounterEq("postgres.postgres_stats.sessions_terminated_ssl", 0);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, SSLWrongConfigPostgresIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

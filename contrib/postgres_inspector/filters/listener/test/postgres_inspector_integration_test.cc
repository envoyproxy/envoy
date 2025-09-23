#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/common/network/utility.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "contrib/postgres_inspector/filters/listener/test/postgres_test_utils.h"
#include "fmt/format.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class PostgresInspectorIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public BaseIntegrationTest {
public:
  PostgresInspectorIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

  void initialize() override {
    config_helper_.renameListener("tcp_proxy");
    BaseIntegrationTest::initialize();
  }

  void sendPostgresStartup(IntegrationTcpClient& tcp_client,
                           const std::map<std::string, std::string>& params) {
    using Extensions::ListenerFilters::PostgresInspector::PostgresTestUtils;
    auto buffer = PostgresTestUtils::createStartupMessage(params);
    ASSERT_TRUE(tcp_client.write(buffer.toString()));
  }

  void sendSslRequest(IntegrationTcpClient& tcp_client) {
    using Extensions::ListenerFilters::PostgresInspector::PostgresTestUtils;
    auto buffer = PostgresTestUtils::createSslRequest();
    ASSERT_TRUE(tcp_client.write(buffer.toString()));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, PostgresInspectorIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(PostgresInspectorIntegrationTest, PostgresProtocolDetection) {
  // TCP proxy base config is already present.
  setUpstreamCount(1);

  // Add Postgres Inspector listener filter.
  config_helper_.addListenerFilter(R"EOF(
    name: envoy.filters.listener.postgres_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector
      enable_metadata_extraction: true
  )EOF");

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send Postgres startup message.
  std::map<std::string, std::string> params = {{"user", "envoy"}, {"database", "test"}};
  using Extensions::ListenerFilters::PostgresInspector::PostgresTestUtils;
  auto startup = PostgresTestUtils::createStartupMessage(params);
  const std::string startup_str = startup.toString();
  ASSERT_TRUE(tcp_client->write(startup_str));

  // Verify connection to upstream.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Verify startup message passed through.
  std::string received_data;
  const size_t expected_size = startup_str.size();
  ASSERT_TRUE(fake_upstream_connection->waitForData(expected_size, &received_data));
  EXPECT_EQ(expected_size, received_data.length());

  // Verify protocol was detected (check stats).
  test_server_->waitForCounterGe("postgres_inspector.postgres_found", 1);
  test_server_->waitForCounterGe("postgres_inspector.ssl_not_requested", 1);

  // Verify bytes_processed histogram.
  test_server_->waitUntilHistogramHasSamples("postgres_inspector.bytes_processed");
  auto bytes_histogram = test_server_->histogram("postgres_inspector.bytes_processed");
  const auto sample_count = static_cast<int>(
      TestUtility::readSampleCount(test_server_->server().dispatcher(), *bytes_histogram));
  const auto sample_sum = static_cast<int>(
      TestUtility::readSampleSum(test_server_->server().dispatcher(), *bytes_histogram));
  EXPECT_EQ(1, sample_count);
  EXPECT_EQ(static_cast<int>(expected_size), sample_sum);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(PostgresInspectorIntegrationTest, PostgresWithSsl) {
  setUpstreamCount(1);

  // Add Postgres inspector then TLS inspector.
  config_helper_.addListenerFilter(R"EOF(
    name: envoy.filters.listener.postgres_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector
      enable_metadata_extraction: false
  )EOF");
  config_helper_.addListenerFilter(R"EOF(
    name: envoy.filters.listener.tls_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector
  )EOF");

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send SSL request.
  sendSslRequest(*tcp_client);

  // Verify stats.
  test_server_->waitForCounterGe("postgres_inspector.ssl_requested", 1);
  test_server_->waitForCounterGe("postgres_inspector.postgres_found", 1);

  // Verify bytes_processed histogram for SSLRequest (8 bytes).
  test_server_->waitUntilHistogramHasSamples("postgres_inspector.bytes_processed");
  auto bytes_histogram = test_server_->histogram("postgres_inspector.bytes_processed");
  const auto sample_count = static_cast<int>(
      TestUtility::readSampleCount(test_server_->server().dispatcher(), *bytes_histogram));
  const auto sample_sum = static_cast<int>(
      TestUtility::readSampleSum(test_server_->server().dispatcher(), *bytes_histogram));
  EXPECT_EQ(1, sample_count);
  EXPECT_EQ(8, sample_sum);

  tcp_client->close();
}

TEST_P(PostgresInspectorIntegrationTest, NonPostgresConnection) {
  setUpstreamCount(1);

  config_helper_.addListenerFilter(R"EOF(
    name: envoy.filters.listener.postgres_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector
  )EOF");

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send non-Postgres data.
  ASSERT_TRUE(tcp_client->write("GET / HTTP/1.1\r\n\r\n"));

  // Connection should be handled but not detected as Postgres.
  test_server_->waitForCounterGe("postgres_inspector.postgres_not_found", 1);
  test_server_->waitForCounterGe("postgres_inspector.protocol_error", 1);

  tcp_client->close();
}

TEST_P(PostgresInspectorIntegrationTest, MultipleFilterChains) {
  // Configure multiple filter chains based on transport protocol.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    // Clear existing filter chains.
    listener->clear_filter_chains();

    // Add Postgres filter chain.
    auto* postgres_chain = listener->add_filter_chains();
    postgres_chain->mutable_filter_chain_match()->set_transport_protocol("postgres");
    auto* postgres_filter = postgres_chain->add_filters();
    postgres_filter->set_name("envoy.filters.network.tcp_proxy");
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy postgres_config;
    postgres_config.set_stat_prefix("postgres_tcp");
    postgres_config.set_cluster("cluster_0");
    postgres_filter->mutable_typed_config()->PackFrom(postgres_config);

    // Add default filter chain for other protocols.
    auto* default_chain = listener->add_filter_chains();
    auto* default_filter = default_chain->add_filters();
    default_filter->set_name("envoy.filters.network.tcp_proxy");
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy default_config;
    default_config.set_stat_prefix("default_tcp");
    default_config.set_cluster("cluster_0");
    default_filter->mutable_typed_config()->PackFrom(default_config);
  });

  // Ensure Postgres inspector is present.
  setUpstreamCount(1);
  config_helper_.addListenerFilter(R"EOF(
    name: envoy.filters.listener.postgres_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector
  )EOF");

  initialize();

  // Test Postgres connection.
  {
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
    std::map<std::string, std::string> params = {{"user", "test"}};
    sendPostgresStartup(*tcp_client, params);

    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

    test_server_->waitForCounterGe("postgres_inspector.postgres_found", 1);
    test_server_->waitForCounterGe("tcp.postgres_tcp.downstream_cx_total", 1);

    tcp_client->close();
    ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  }

  // Test non-Postgres connection.
  {
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
    ASSERT_TRUE(tcp_client->write("HTTP data"));

    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

    test_server_->waitForCounterGe("postgres_inspector.postgres_not_found", 1);
    test_server_->waitForCounterGe("tcp.default_tcp.downstream_cx_total", 1);

    tcp_client->close();
    ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  }
}

TEST_P(PostgresInspectorIntegrationTest, ConfigurableTimeout) {
  // Configure a short timeout.
  setUpstreamCount(1);
  config_helper_.addListenerFilter(R"EOF(
    name: envoy.filters.listener.postgres_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector
      startup_timeout: 1s
  )EOF");

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Don't send any data, let it timeout.
  // Wait for timeout to trigger.
  test_server_->waitForCounterGe("postgres_inspector.startup_message_timeout", 1);

  // Connection should be closed.
  tcp_client->waitForDisconnect();
}

TEST_P(PostgresInspectorIntegrationTest, ConfigurableMaxMessageSize) {
  // Configure a small max message size (within proto limits [256, 10000]).
  setUpstreamCount(1);
  config_helper_.addListenerFilter(R"EOF(
    name: envoy.filters.listener.postgres_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector
      max_startup_message_size: 300
  )EOF");

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send an oversized message (400 bytes > 300 byte limit).
  using Extensions::ListenerFilters::PostgresInspector::PostgresTestUtils;
  auto buffer = PostgresTestUtils::createOversizedMessage(400);
  ASSERT_TRUE(tcp_client->write(buffer.toString()));

  // Should be rejected.
  test_server_->waitForCounterGe("postgres_inspector.startup_message_too_large", 1);

  // Connection should be closed by server (don't call close() first).
  tcp_client->waitForDisconnect();
}

} // namespace
} // namespace Envoy

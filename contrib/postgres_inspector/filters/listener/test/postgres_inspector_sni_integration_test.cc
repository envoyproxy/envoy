#include <chrono>
#include <string>
#include <thread>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/common/network/utility.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"

#include "contrib/envoy/extensions/filters/network/postgres_proxy/v3alpha/postgres_proxy.pb.h"
#include "contrib/postgres_inspector/filters/listener/test/postgres_test_utils.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace PostgresInspector {
namespace {

// Integration test demonstrating postgres_inspector working with postgres_proxy to handle SSL
// negotiation for both PostgreSQL < 17 and 17+.
class PostgresInspectorWithProxyIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  PostgresInspectorWithProxyIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void initializeWithProxySSLSupport() {
    // Add postgres_inspector to passively detect PostgreSQL protocol.
    config_helper_.addListenerFilter(R"EOF(
name: envoy.filters.listener.postgres_inspector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector
  enable_metadata_extraction: true
  startup_timeout: 5s
)EOF");

    const std::string ip = Network::Test::getLoopbackAddressString(version_);

    config_helper_.addConfigModifier([ip](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();

      // Create upstream clusters
      static_resources->mutable_clusters()->Clear();
      static_resources->add_clusters()->MergeFrom(
          ConfigHelper::buildStaticCluster("cluster_db1", 0, ip));
      static_resources->add_clusters()->MergeFrom(
          ConfigHelper::buildStaticCluster("cluster_db2", 0, ip));

      auto* listener = static_resources->mutable_listeners(0);
      listener->clear_filter_chains();
      listener->mutable_listener_filters_timeout()->set_seconds(5);
      listener->set_continue_on_listener_filters_timeout(true);

      auto* sa = listener->mutable_address()->mutable_socket_address();
      sa->set_address(ip);

      // Filter chain for PostgreSQL with postgres_proxy handling SSL.
      auto* pg_chain = listener->add_filter_chains();
      pg_chain->mutable_filter_chain_match()->set_transport_protocol("postgres");

      // Add postgres_proxy filter to actively handle SSL negotiation.
      auto* pg_filter = pg_chain->add_filters();
      pg_filter->set_name("envoy.filters.network.postgres_proxy");
      envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy pg_config;
      pg_config.set_stat_prefix("postgres_with_ssl");
      pg_config.set_terminate_ssl(false);
      pg_filter->mutable_typed_config()->PackFrom(pg_config);

      // Add TCP proxy for routing to backend.
      auto* tcp_filter = pg_chain->add_filters();
      tcp_filter->set_name("envoy.filters.network.tcp_proxy");
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_config;
      tcp_config.set_stat_prefix("tcp_postgres");
      tcp_config.set_cluster("cluster_db1");
      tcp_filter->mutable_typed_config()->PackFrom(tcp_config);

      // Default chain for non-PostgreSQL traffic.
      auto* default_chain = listener->add_filter_chains();

      auto* default_filter = default_chain->add_filters();
      default_filter->set_name("envoy.filters.network.tcp_proxy");
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy default_config;
      default_config.set_stat_prefix("tcp_default");
      default_config.set_cluster("cluster_db2");
      default_filter->mutable_typed_config()->PackFrom(default_config);
    });

    setUpstreamCount(2);
    BaseIntegrationTest::initialize();
  }

  // Send PostgreSQL < 17 style SSL request.
  std::unique_ptr<RawConnectionDriver> sendPostgresPre17SSLRequest() {
    Buffer::OwnedImpl request;

    // Create PostgreSQL SSLRequest.
    auto ssl_req = PostgresTestUtils::createSslRequest();
    request.add(ssl_req);

    auto driver = std::make_unique<RawConnectionDriver>(
        lookupPort("listener_0"), request,
        [](Network::ClientConnection&, const Buffer::Instance&) {}, version_, *dispatcher_);

    (void)driver->waitForConnection();
    (void)driver->run(Event::Dispatcher::RunType::NonBlock);

    return driver;
  }

  // Send PostgreSQL 17+ style SSLRequest with additional data immediately after.
  std::unique_ptr<RawConnectionDriver> sendPostgres17DirectSSL() {
    Buffer::OwnedImpl to_send;

    // PostgreSQL 17+ sends SSLRequest followed immediately by more data.
    auto ssl_req = PostgresTestUtils::createSslRequest();
    to_send.add(ssl_req);

    // Add dummy data simulating that ClientHello would follow immediately.
    to_send.add("SIMULATED_TLS_CLIENT_HELLO_DATA");

    auto driver = std::make_unique<RawConnectionDriver>(
        lookupPort("listener_0"), to_send,
        [](Network::ClientConnection&, const Buffer::Instance&) {}, version_, *dispatcher_);

    (void)driver->waitForConnection();

    // Process the messages.
    for (int i = 0; i < 5; ++i) {
      (void)driver->run(Event::Dispatcher::RunType::NonBlock);
    }

    return driver;
  }

  // Send plaintext PostgreSQL startup message.
  std::unique_ptr<RawConnectionDriver> sendPlaintextPostgres(const std::string& user,
                                                             const std::string& database) {
    std::map<std::string, std::string> params = {
        {"user", user}, {"database", database}, {"application_name", "integration_test"}};
    Buffer::OwnedImpl startup = PostgresTestUtils::createStartupMessage(params);

    auto driver = std::make_unique<RawConnectionDriver>(
        lookupPort("listener_0"), startup,
        [](Network::ClientConnection&, const Buffer::Instance&) {}, version_, *dispatcher_);

    (void)driver->waitForConnection();
    (void)driver->run(Event::Dispatcher::RunType::NonBlock);

    return driver;
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, PostgresInspectorWithProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Test PostgreSQL < 17 connection detected by postgres_inspector and routed correctly.
TEST_P(PostgresInspectorWithProxyIntegrationTest, PostgresPre17DetectedAndRouted) {
  initializeWithProxySSLSupport();

  auto driver = sendPostgresPre17SSLRequest();

  // Connection should be established to db1 upstream after detection.
  FakeRawConnectionPtr upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream));

  // Verify postgres_inspector detected the protocol.
  test_server_->waitForCounterGe("postgres_inspector.postgres_found", 1);
  test_server_->waitForCounterGe("postgres_inspector.ssl_requested", 1);

  // Verify postgres_proxy is in the filter chain.
  test_server_->waitForCounterGe("postgres.postgres_with_ssl.sessions", 1);

  driver->close();
}

// Test PostgreSQL 17+ direct SSL detected and routed correctly.
TEST_P(PostgresInspectorWithProxyIntegrationTest, Postgres17DirectSSLDetectedAndRouted) {
  initializeWithProxySSLSupport();

  auto driver = sendPostgres17DirectSSL();

  // Connection should be established to db1 upstream.
  FakeRawConnectionPtr upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream));

  // Verify postgres_inspector detected the protocol.
  test_server_->waitForCounterGe("postgres_inspector.postgres_found", 1);
  test_server_->waitForCounterGe("postgres_inspector.ssl_requested", 1);

  // Verify postgres_proxy is in the filter chain.
  test_server_->waitForCounterGe("postgres.postgres_with_ssl.sessions", 1);

  driver->close();
}

// Test plaintext PostgreSQL routes to postgres filter chain.
TEST_P(PostgresInspectorWithProxyIntegrationTest, PlaintextPostgresDetectedAndRouted) {
  initializeWithProxySSLSupport();

  auto driver = sendPlaintextPostgres("testuser", "testdb");

  // Should route to postgres chain (db1 upstream).
  FakeRawConnectionPtr upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream));

  // Verify postgres_inspector detected the protocol.
  test_server_->waitForCounterGe("postgres_inspector.postgres_found", 1);
  test_server_->waitForCounterGe("postgres_inspector.ssl_not_requested", 1);

  driver->close();
}

// Test that both version behaviors work correctly in sequence.
TEST_P(PostgresInspectorWithProxyIntegrationTest, MixedVersionConnectionsHandledCorrectly) {
  initializeWithProxySSLSupport();

  // Send different types of connections demonstrating support for both versions.
  auto driver1 = sendPostgresPre17SSLRequest();
  auto driver2 = sendPostgres17DirectSSL();
  auto driver3 = sendPlaintextPostgres("user1", "database1");

  // Verify routing. All should go to db1 via postgres filter chain.
  FakeRawConnectionPtr upstream1, upstream2, upstream3;

  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream1));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream2));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream3));

  // Verify stats demonstrate detection of all three types.
  test_server_->waitForCounterGe("postgres_inspector.postgres_found", 3);
  test_server_->waitForCounterGe("postgres_inspector.ssl_requested", 2);
  test_server_->waitForCounterGe("postgres_inspector.ssl_not_requested", 1);

  // Verify postgres_proxy handled at least the SSL sessions.
  test_server_->waitForCounterGe("postgres.postgres_with_ssl.sessions", 2);

  driver1->close();
  driver2->close();
  driver3->close();
}

} // namespace
} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy

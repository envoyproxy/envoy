#include <chrono>
#include <string>
#include <thread>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/common/network/utility.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"

#include "contrib/postgres_inspector/filters/listener/test/postgres_test_utils.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace PostgresInspector {
namespace {

// Integration test demonstrating postgres_inspector functionality.
class PostgresInspectorIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public BaseIntegrationTest {
public:
  PostgresInspectorIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig()) {}

  void initializeWithPostgresDetection() {
    // Configure postgres_inspector as passive listener filter.
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

      // Create upstream clusters.
      static_resources->mutable_clusters()->Clear();
      static_resources->add_clusters()->MergeFrom(
          ConfigHelper::buildStaticCluster("cluster_postgres", 0, ip));
      static_resources->add_clusters()->MergeFrom(
          ConfigHelper::buildStaticCluster("cluster_default", 0, ip));

      auto* listener = static_resources->mutable_listeners(0);
      listener->clear_filter_chains();
      listener->mutable_listener_filters_timeout()->set_seconds(2);
      listener->set_continue_on_listener_filters_timeout(true);

      auto* sa = listener->mutable_address()->mutable_socket_address();
      sa->set_address(ip);

      // Filter chain for PostgreSQL traffic detected by inspector.
      auto* pg_chain = listener->add_filter_chains();
      pg_chain->mutable_filter_chain_match()->set_transport_protocol("postgres");

      auto* pg_filter = pg_chain->add_filters();
      pg_filter->set_name("envoy.filters.network.tcp_proxy");
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy pg_config;
      pg_config.set_stat_prefix("tcp_postgres");
      pg_config.set_cluster("cluster_postgres");
      pg_filter->mutable_typed_config()->PackFrom(pg_config);

      // Default filter chain for non-PostgreSQL traffic.
      auto* default_chain = listener->add_filter_chains();

      auto* default_filter = default_chain->add_filters();
      default_filter->set_name("envoy.filters.network.tcp_proxy");
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy default_config;
      default_config.set_stat_prefix("tcp_default");
      default_config.set_cluster("cluster_default");
      default_filter->mutable_typed_config()->PackFrom(default_config);
    });

    setUpstreamCount(2);
    BaseIntegrationTest::initialize();
  }

  // Send PostgreSQL SSL request simulating version < 17.
  std::unique_ptr<RawConnectionDriver> sendPostgresSSLRequest() {
    Buffer::OwnedImpl to_send;

    // Send PostgreSQL SSLRequest.
    auto ssl_req = PostgresTestUtils::createSslRequest();
    to_send.add(ssl_req);

    auto driver = std::make_unique<RawConnectionDriver>(
        lookupPort("listener_0"), to_send,
        [](Network::ClientConnection&, const Buffer::Instance&) {}, version_, *dispatcher_);

    (void)driver->waitForConnection();
    (void)driver->run(Event::Dispatcher::RunType::NonBlock);

    return driver;
  }

  // Send PostgreSQL 17+ style. SSLRequest followed immediately by more data.
  std::unique_ptr<RawConnectionDriver> sendPostgres17DirectSSL() {
    Buffer::OwnedImpl to_send;

    // PostgreSQL 17+ sends SSLRequest and additional data together.
    auto ssl_req = PostgresTestUtils::createSslRequest();
    to_send.add(ssl_req);

    // Add some additional data simulating ClientHello would follow.
    to_send.add("SIMULATED_CLIENT_HELLO_DATA_FOR_POSTGRES_17");

    auto driver = std::make_unique<RawConnectionDriver>(
        lookupPort("listener_0"), to_send,
        [](Network::ClientConnection&, const Buffer::Instance&) {}, version_, *dispatcher_);

    (void)driver->waitForConnection();
    (void)driver->run(Event::Dispatcher::RunType::NonBlock);

    return driver;
  }

  // Send plaintext PostgreSQL startup message.
  std::unique_ptr<RawConnectionDriver> sendPlaintextPostgresStartup(const std::string& user,
                                                                    const std::string& database,
                                                                    const std::string& app_name) {
    std::map<std::string, std::string> params = {
        {"user", user}, {"database", database}, {"application_name", app_name}};
    Buffer::OwnedImpl startup = PostgresTestUtils::createStartupMessage(params);

    auto driver = std::make_unique<RawConnectionDriver>(
        lookupPort("listener_0"), startup,
        [](Network::ClientConnection&, const Buffer::Instance&) {}, version_, *dispatcher_);

    (void)driver->waitForConnection();
    (void)driver->run(Event::Dispatcher::RunType::NonBlock);

    return driver;
  }

  // Send non-PostgreSQL traffic.
  std::unique_ptr<RawConnectionDriver> sendNonPostgresTraffic() {
    Buffer::OwnedImpl http_req;
    http_req.add("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");

    auto driver = std::make_unique<RawConnectionDriver>(
        lookupPort("listener_0"), http_req,
        [](Network::ClientConnection&, const Buffer::Instance&) {}, version_, *dispatcher_);

    (void)driver->waitForConnection();
    (void)driver->run(Event::Dispatcher::RunType::NonBlock);

    return driver;
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, PostgresInspectorIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Test that postgres_inspector correctly detects PostgreSQL SSL request from version < 17.
TEST_P(PostgresInspectorIntegrationTest, DetectsPostgresPre17SSLRequest) {
  initializeWithPostgresDetection();

  auto driver = sendPostgresSSLRequest();

  // Should route to cluster_postgres (upstream 0) based on protocol detection.
  FakeRawConnectionPtr upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream));

  // Verify postgres_inspector detected the protocol.
  test_server_->waitForCounterGe("postgres_inspector.postgres_found", 1);
  test_server_->waitForCounterGe("postgres_inspector.ssl_requested", 1);

  driver->close();
}

// Test that postgres_inspector correctly detects PostgreSQL 17+ direct SSL.
TEST_P(PostgresInspectorIntegrationTest, DetectsPostgres17DirectSSL) {
  initializeWithPostgresDetection();

  auto driver = sendPostgres17DirectSSL();

  // Should route to cluster_postgres (upstream 0) based on protocol detection.
  FakeRawConnectionPtr upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream));

  // Verify postgres_inspector detected the protocol.
  test_server_->waitForCounterGe("postgres_inspector.postgres_found", 1);
  test_server_->waitForCounterGe("postgres_inspector.ssl_requested", 1);

  driver->close();
}

// Test that postgres_inspector correctly detects plaintext PostgreSQL startup.
TEST_P(PostgresInspectorIntegrationTest, DetectsPlaintextPostgresStartup) {
  initializeWithPostgresDetection();

  auto driver = sendPlaintextPostgresStartup("testuser", "testdb", "myapp");

  // Should route to cluster_postgres (upstream 0) based on protocol detection.
  FakeRawConnectionPtr upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream));

  // Verify postgres_inspector detected the protocol.
  test_server_->waitForCounterGe("postgres_inspector.postgres_found", 1);
  test_server_->waitForCounterGe("postgres_inspector.ssl_not_requested", 1);

  driver->close();
}

// Test that non-PostgreSQL traffic is not detected as PostgreSQL.
TEST_P(PostgresInspectorIntegrationTest, DoesNotDetectNonPostgres) {
  initializeWithPostgresDetection();

  auto driver = sendNonPostgresTraffic();

  // Should route to cluster_default (upstream 1) as not PostgreSQL.
  FakeRawConnectionPtr upstream;
  ASSERT_TRUE(fake_upstreams_[1]->waitForRawConnection(upstream));

  // Verify postgres_inspector did NOT detect PostgreSQL.
  test_server_->waitForCounterGe("postgres_inspector.postgres_not_found", 1);

  driver->close();
}

// Test multiple connections with different types work correctly.
TEST_P(PostgresInspectorIntegrationTest, MultipleMixedConnectionsDetectedCorrectly) {
  initializeWithPostgresDetection();

  // Send mix of PostgreSQL and non-PostgreSQL connections.
  auto driver1 = sendPostgresSSLRequest();
  auto driver2 = sendPlaintextPostgresStartup("user1", "db1", "app1");
  auto driver3 = sendNonPostgresTraffic();
  auto driver4 = sendPostgres17DirectSSL();

  // Verify routing based on protocol detection.
  FakeRawConnectionPtr upstream1, upstream2, upstream3, upstream4;

  // PostgreSQL SSL should go to upstream 0.
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream1));

  // PostgreSQL plaintext should go to upstream 0.
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream2));

  // Non-PostgreSQL should go to upstream 1.
  ASSERT_TRUE(fake_upstreams_[1]->waitForRawConnection(upstream3));

  // PostgreSQL 17 direct SSL should go to upstream 0.
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream4));

  // Verify stats.
  test_server_->waitForCounterGe("postgres_inspector.postgres_found", 3);
  test_server_->waitForCounterGe("postgres_inspector.postgres_not_found", 1);
  test_server_->waitForCounterGe("postgres_inspector.ssl_requested", 2);
  test_server_->waitForCounterGe("postgres_inspector.ssl_not_requested", 1);

  driver1->close();
  driver2->close();
  driver3->close();
  driver4->close();
}

} // namespace
} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy

#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/common/network/utility.h"

#include "test/extensions/filters/listener/tls_inspector/tls_utility.h"
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

class PostgresSniRoutingIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  PostgresSniRoutingIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig()) {}

  void initializeWithSniRouting() {
    // Inline minimal TLS inspector YAML to ensure type URL matches test deps.
    // NOTE: filters are prepended, so the last added will run first.
    // We add TLS first and then Postgres so that Postgres runs first at accept.
    config_helper_.addListenerFilter(R"EOF(
name: envoy.filters.listener.tls_inspector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector
)EOF");

    config_helper_.addListenerFilter(R"EOF(
name: envoy.filters.listener.postgres_inspector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector
  startup_timeout: 5s
)EOF");

    // Create a bunch of fake upstream clusters and point filter chains by SNI.
    const std::string ip = Network::Test::getLoopbackAddressString(version_);
    config_helper_.addConfigModifier([ip](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      // Create placeholder endpoints. The ports will be filled by finalize().
      static_resources->mutable_clusters()->Clear();
      static_resources->add_clusters()->MergeFrom(
          ConfigHelper::buildStaticCluster("cluster_db1", 0, ip));
      static_resources->add_clusters()->MergeFrom(
          ConfigHelper::buildStaticCluster("cluster_db2", 0, ip));
      static_resources->add_clusters()->MergeFrom(
          ConfigHelper::buildStaticCluster("cluster_db3", 0, ip));

      auto* listener = static_resources->mutable_listeners(0);
      listener->clear_filter_chains();
      listener->mutable_listener_filters_timeout()->set_seconds(1);
      listener->set_continue_on_listener_filters_timeout(true);
      // Ensure listener binds to the proper IP family
      auto* sa = listener->mutable_address()->mutable_socket_address();
      sa->set_address(ip);

      auto add_chain = [&](absl::string_view sni, absl::string_view cluster) {
        auto* chain = listener->add_filter_chains();
        chain->mutable_filter_chain_match()->add_server_names(std::string(sni));
        auto* nf = chain->add_filters();
        nf->set_name("tcp");
        envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_cfg;
        tcp_cfg.set_stat_prefix(std::string("tcp_") + std::string(sni));
        tcp_cfg.set_cluster(std::string(cluster));
        nf->mutable_typed_config()->PackFrom(tcp_cfg);
      };

      add_chain("db1.local", "cluster_db1");
      add_chain("db2.local", "cluster_db2");
      add_chain("db3.local", "cluster_db3");
    });

    // Stand up three TCP upstreams and initialize; BaseIntegrationTest will finalize ports.
    setUpstreamCount(3);
    BaseIntegrationTest::initialize();
  }

  std::unique_ptr<RawConnectionDriver> sendPgSslThenTlsClientHello(const std::string& sni) {
    Buffer::OwnedImpl to_send;
    // For IPv4, prepend Postgres SSLRequest so postgres_inspector sets transport_protocol.
    if (version_ == Network::Address::IpVersion::v4) {
      auto ssl_req = PostgresTestUtils::createSslRequest();
      to_send.add(ssl_req);
    }
    using Tls::Test::generateClientHello;
    std::vector<uint8_t> ch = generateClientHello(0x0303, 0x0303, sni, "");
    to_send.add(absl::string_view(reinterpret_cast<const char*>(ch.data()), ch.size()));

    auto driver = std::make_unique<RawConnectionDriver>(
        lookupPort("listener_0"), to_send,
        [&](Network::ClientConnection&, const Buffer::Instance&) {}, version_, *dispatcher_);
    // Wait for connection and drive dispatcher; return driver to keep connection alive.
    (void)driver->waitForConnection();
    for (int i = 0; i < 5; ++i) {
      (void)driver->run(Event::Dispatcher::RunType::NonBlock);
    }
    return driver;
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, PostgresSniRoutingIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(PostgresSniRoutingIntegrationTest, RoutesDb1BySni) {
  initializeWithSniRouting();
  auto driver = sendPgSslThenTlsClientHello("db1.local");
  FakeRawConnectionPtr c1;
  const bool ok = fake_upstreams_[0]->waitForRawConnection(c1);
  driver->close();
  EXPECT_TRUE(ok);
}

TEST_P(PostgresSniRoutingIntegrationTest, RoutesDb2BySni) {
  initializeWithSniRouting();
  auto driver = sendPgSslThenTlsClientHello("db2.local");
  FakeRawConnectionPtr c2;
  const bool ok = fake_upstreams_[1]->waitForRawConnection(c2);
  driver->close();
  EXPECT_TRUE(ok);
}

TEST_P(PostgresSniRoutingIntegrationTest, RoutesDb3BySni) {
  initializeWithSniRouting();
  auto driver = sendPgSslThenTlsClientHello("db3.local");
  FakeRawConnectionPtr c3;
  const bool ok = fake_upstreams_[2]->waitForRawConnection(c3);
  driver->close();
  EXPECT_TRUE(ok);
}

} // namespace
} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy

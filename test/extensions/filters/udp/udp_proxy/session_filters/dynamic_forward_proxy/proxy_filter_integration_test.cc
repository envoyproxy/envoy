#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy/config.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy/proxy_filter.h"

#include "test/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy/dfp_setter.h"
#include "test/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy/dfp_setter.pb.h"
#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicForwardProxy {
namespace {

class DynamicForwardProxyIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  DynamicForwardProxyIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseUdpListenerConfig()) {
    skip_tag_extraction_rule_check_ = true;
  }

  struct BufferConfig {
    uint32_t max_buffered_datagrams_;
    uint32_t max_buffered_bytes_;
  };

  void setup(std::string upsteam_host = "localhost",
             absl::optional<BufferConfig> buffer_config = absl::nullopt, uint32_t max_hosts = 1024,
             uint32_t max_pending_requests = 1024) {
    setUdpFakeUpstream(FakeUpstreamConfig::UdpConfig());

    config_helper_.addConfigModifier(
        [this, upsteam_host, buffer_config, max_hosts,
         max_pending_requests](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          // Switch predefined cluster_0 to CDS filesystem sourcing.
          bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_resource_api_version(
              envoy::config::core::v3::ApiVersion::V3);
          bootstrap.mutable_dynamic_resources()
              ->mutable_cds_config()
              ->mutable_path_config_source()
              ->set_path(cds_helper_.cdsPath());
          bootstrap.mutable_static_resources()->clear_clusters();

          std::string filter_config = fmt::format(
              R"EOF(
name: udp_proxy
typed_config:
  '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig
  stat_prefix: foo
  matcher:
    on_no_match:
      action:
        name: route
        typed_config:
          '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
          cluster: cluster_0
  session_filters:
  - name: setter
    typed_config:
      '@type': type.googleapis.com/test.extensions.filters.udp.udp_proxy.session_filters.DynamicForwardProxySetterFilterConfig
      host: {}
      port: {}
  - name: dfp
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.session.dynamic_forward_proxy.v3.FilterConfig
      stat_prefix: foo
      dns_cache_config:
        name: foo
        dns_lookup_family: {}
        max_hosts: {}
        dns_cache_circuit_breaker:
          max_pending_requests: {}
)EOF",
              upsteam_host, fake_upstreams_[0]->localAddress()->ip()->port(),
              Network::Test::ipVersionToDnsFamily(GetParam()), max_hosts, max_pending_requests);

          if (buffer_config.has_value()) {
            filter_config += fmt::format(R"EOF(
      buffer_options:
        max_buffered_datagrams: {}
        max_buffered_bytes: {}
)EOF",
                                         buffer_config.value().max_buffered_datagrams_,
                                         buffer_config.value().max_buffered_bytes_);
          }

          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto* filter = listener->add_listener_filters();
          TestUtility::loadFromYaml(filter_config, *filter);
        });

    // Setup the initial CDS cluster.
    cluster_.mutable_connect_timeout()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    cluster_.set_name("cluster_0");
    cluster_.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

    const std::string cluster_type_config = fmt::format(
        R"EOF(
name: envoy.clusters.dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
    dns_cache_circuit_breaker:
      max_pending_requests: {}
)EOF",
        Network::Test::ipVersionToDnsFamily(GetParam()), max_hosts, max_pending_requests);

    TestUtility::loadFromYaml(cluster_type_config, *cluster_.mutable_cluster_type());

    // Load the CDS cluster and wait for it to initialize.
    cds_helper_.setCds({cluster_});
    BaseIntegrationTest::initialize();
    test_server_->waitForCounterEq("cluster_manager.cluster_added", 1);
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  }

  CdsHelper cds_helper_;
  envoy::config::cluster::v3::Cluster cluster_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicForwardProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicForwardProxyIntegrationTest, BasicFlow) {
  setup();
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
  Network::Test::UdpSyncPeer client(version_);

  client.write("hello1", *listener_address);
  test_server_->waitForCounterEq("dns_cache.foo.dns_query_attempt", 1);
  test_server_->waitForCounterEq("dns_cache.foo.dns_query_success", 1);
  test_server_->waitForCounterEq("dns_cache.foo.host_added", 1);

  // There is no buffering in this test, so the first message was dropped. Send another message
  // to verify that it's able to go through after the DNS resolution completed.
  client.write("hello2", *listener_address);

  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ("hello2", request_datagram.buffer_->toString());
}

TEST_P(DynamicForwardProxyIntegrationTest, BasicFlowWithBuffering) {
  setup("localhost", BufferConfig{1, 1024});
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
  Network::Test::UdpSyncPeer client(version_);

  client.write("hello1", *listener_address);
  test_server_->waitForCounterEq("dns_cache.foo.dns_query_attempt", 1);
  test_server_->waitForCounterEq("dns_cache.foo.dns_query_success", 1);
  test_server_->waitForCounterEq("dns_cache.foo.host_added", 1);

  // Buffering is enabled so check that the first datagram is sent after the resolution completed.
  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ("hello1", request_datagram.buffer_->toString());
}

TEST_P(DynamicForwardProxyIntegrationTest, BufferOverflowDueToDatagramSize) {
  setup("localhost", BufferConfig{1, 2});
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
  Network::Test::UdpSyncPeer client(version_);

  client.write("hello1", *listener_address);
  test_server_->waitForCounterEq("dns_cache.foo.dns_query_attempt", 1);
  test_server_->waitForCounterEq("dns_cache.foo.dns_query_success", 1);
  test_server_->waitForCounterEq("dns_cache.foo.host_added", 1);
  test_server_->waitForCounterEq("udp.session.dynamic_forward_proxy.foo.buffer_overflow", 1);

  // The first datagram should be dropped because it exceeds the buffer size. Send another message
  // to verify that it's able to go through after the DNS resolution completed.
  client.write("hello2", *listener_address);

  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ("hello2", request_datagram.buffer_->toString());
}

TEST_P(DynamicForwardProxyIntegrationTest, EmptyDnsResponseDueToDummyHost) {
  setup("dummyhost");
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
  Network::Test::UdpSyncPeer client(version_);

  client.write("hello1", *listener_address);
  test_server_->waitForCounterEq("dns_cache.foo.dns_query_attempt", 1);

  // The DNS response is empty, so will not be found any valid host and session will be removed.
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_none_healthy", 1);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);

  // DNS cache hit but still no host found.
  client.write("hello2", *listener_address);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_none_healthy", 2);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);
}

} // namespace
} // namespace DynamicForwardProxy
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/api/api.h"

#include "common/network/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/upstream/static_cluster.h"
#include "common/upstream/strict_dns_cluster.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/options.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/health_checker.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Upstream {
namespace {

class ClusterInfoImplTest : public testing::Test {
public:
  ClusterInfoImplTest() : api_(Api::createApiForTest(stats_, random_)) {}

  std::unique_ptr<StrictDnsClusterImpl> makeCluster(const std::string& yaml,
                                                    bool avoid_boosting = true) {
    cluster_config_ = parseClusterFromV3Yaml(yaml, avoid_boosting);
    scope_ = stats_.createScope(fmt::format("cluster.{}.", cluster_config_.alt_stat_name().empty()
                                                               ? cluster_config_.name()
                                                               : cluster_config_.alt_stat_name()));
    factory_context_ = std::make_unique<Server::Configuration::TransportSocketFactoryContextImpl>(
        admin_, ssl_context_manager_, *scope_, cm_, local_info_, dispatcher_, stats_,
        singleton_manager_, tls_, validation_visitor_, *api_, options_);

    return std::make_unique<StrictDnsClusterImpl>(cluster_config_, runtime_, dns_resolver_,
                                                  *factory_context_, std::move(scope_), false);
  }

  class RetryBudgetTestClusterInfo : public ClusterInfoImpl {
  public:
    static std::pair<absl::optional<double>, absl::optional<uint32_t>> getRetryBudgetParams(
        const envoy::config::cluster::v3::CircuitBreakers::Thresholds& thresholds) {
      return ClusterInfoImpl::getRetryBudgetParams(thresholds);
    }
  };

  Stats::TestUtil::TestStore stats_;
  Ssl::MockContextManager ssl_context_manager_;
  std::shared_ptr<Network::MockDnsResolver> dns_resolver_{new NiceMock<Network::MockDnsResolver>()};
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<MockClusterManager> cm_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  ReadyWatcher initialized_;
  envoy::config::cluster::v3::Cluster cluster_config_;
  Envoy::Stats::ScopePtr scope_;
  std::unique_ptr<Server::Configuration::TransportSocketFactoryContextImpl> factory_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
  Server::MockOptions options_;
};

TEST_F(ClusterInfoImplTest, Http3) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: MAGLEV
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
    transport_socket:
      name: envoy.transport_sockets.quic
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicUpstreamTransport
        upstream_tls_context:
          common_tls_context:
            tls_certificates:
            - certificate_chain:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
              private_key:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
            validation_context:
              trusted_ca:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
              match_subject_alt_names:
              - exact: localhost
              - exact: 127.0.0.1
  )EOF",
                                                       Network::Address::IpVersion::v4);

  auto cluster1 = makeCluster(yaml);
  ASSERT_TRUE(cluster1->info()->idleTimeout().has_value());
  EXPECT_EQ(std::chrono::hours(1), cluster1->info()->idleTimeout().value());

  const std::string explicit_http3 = R"EOF(
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http3_protocol_options:
            quic_protocol_options:
              max_concurrent_streams: 2
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF";

  const std::string downstream_http3 = R"EOF(
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        use_downstream_protocol_config:
          http3_protocol_options: {}
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF";

  auto explicit_h3 = makeCluster(yaml + explicit_http3);
  EXPECT_EQ(Http::Protocol::Http3,
            explicit_h3->info()->upstreamHttpProtocol({Http::Protocol::Http10})[0]);
  EXPECT_EQ(
      explicit_h3->info()->http3Options().quic_protocol_options().max_concurrent_streams().value(),
      2);

  auto downstream_h3 = makeCluster(yaml + downstream_http3);
  EXPECT_EQ(Http::Protocol::Http3,
            downstream_h3->info()->upstreamHttpProtocol({Http::Protocol::Http3})[0]);
  EXPECT_FALSE(
      downstream_h3->info()->http3Options().quic_protocol_options().has_max_concurrent_streams());
}

TEST_F(ClusterInfoImplTest, Http3BadConfig) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: MAGLEV
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
    transport_socket:
      name: envoy.transport_sockets.not_quic
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicUpstreamTransport
        upstream_tls_context:
          common_tls_context:
            tls_certificates:
            - certificate_chain:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
              private_key:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
            validation_context:
              trusted_ca:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
              match_subject_alt_names:
              - exact: localhost
              - exact: 127.0.0.1
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        use_downstream_protocol_config:
          http3_protocol_options: {}
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_REGEX(makeCluster(yaml), EnvoyException,
                          "HTTP3 requires a QuicUpstreamTransport tranport socket: name.*");
}

} // namespace
} // namespace Upstream
} // namespace Envoy

#include <string>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"

#include "source/common/protobuf/utility.h"

#include "test/config/utility.h"
#include "test/integration/base_integration_test.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/integration_tcp_client.h"
#include "test/integration/server.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "contrib/envoy/extensions/filters/network/metadata_exchange/v3/metadata_exchange.pb.h"
#include "contrib/istio/filters/common/source/metadata_object.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Two-Envoy TCP fixture for the Istio sidecar stack.
//
//   raw TCP client --> CLIENT sidecar (OUTBOUND, reporter=source)
//                          | TLS + ALPN istio2, upstream metadata_exchange
//                          v
//                     SERVER sidecar (INBOUND, reporter=destination)
//                          | downstream metadata_exchange + istio.stats
//                          v
//                     app (raw TCP FakeUpstream)
//
// This is the topology the earlier single-Envoy self-chain TCP test could not
// drive (its connection stalled before close); two real proxies let the real MX
// filters do the istio2 framing and the connection close propagates naturally.
class IstioTcpTwoProxyIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public BaseIntegrationTest {
public:
  IstioTcpTwoProxyIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

protected:
  static envoy::config::listener::v3::Filter networkFilter(const std::string& yaml) {
    envoy::config::listener::v3::Filter filter;
    TestUtility::loadFromYaml(yaml, filter);
    return filter;
  }

  static std::string statsFilterYaml(const std::string& extra = "") {
    return absl::StrCat(R"EOF(
name: envoy.filters.network.istio_stats
typed_config:
  "@type": type.googleapis.com/stats.PluginConfig
  tcp_reporting_duration: 1s
)EOF",
                        extra);
  }

  static std::string tcpProxyFilterYaml() {
    return R"EOF(
name: tcp
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
  stat_prefix: tcpproxy_stats
  cluster: cluster_0
)EOF";
  }

  std::string metadataExchangeYaml(const std::string& name, const std::string& protocol) const {
    envoy::tcp::metadataexchange::config::MetadataExchange config;
    config.set_protocol(protocol);
    config.set_enable_discovery(enable_metadata_discovery_);
    for (const auto& label : mx_additional_labels_) {
      config.add_additional_labels(label);
    }

    envoy::config::listener::v3::Filter filter;
    filter.set_name(name);
    filter.mutable_typed_config()->PackFrom(config);
    return MessageUtil::getJsonStringFromMessageOrError(filter);
  }

  static void setNode(ConfigHelper& helper, const std::string& workload, const std::string& ns,
                      const std::string& cluster, const std::string& canonical_name,
                      const std::string& app) {
    helper.addConfigModifier([=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto& node = *bootstrap.mutable_node();
      node.set_id(absl::StrCat(workload, "-id"));
      node.set_cluster(cluster);
      auto& fields = *node.mutable_metadata()->mutable_fields();
      fields["WORKLOAD_NAME"].set_string_value(workload);
      fields["NAMESPACE"].set_string_value(ns);
      fields["CLUSTER_ID"].set_string_value(cluster);
      fields["ISTIO_VERSION"].set_string_value("1.20.0");
      auto& labels = *fields["LABELS"].mutable_struct_value()->mutable_fields();
      labels[std::string(Istio::Common::CanonicalNameLabel)].set_string_value(canonical_name);
      labels[std::string(Istio::Common::CanonicalRevisionLabel)].set_string_value("v1");
      labels[std::string(Istio::Common::AppNameLabel)].set_string_value(app);
      labels[std::string(Istio::Common::AppVersionLabel)].set_string_value("v1");
    });
  }

  void startSidecar(ConfigHelper& helper, const std::vector<uint32_t>& upstream_ports,
                    const std::string& listener_name, IntegrationTestServerPtr& out) {
    helper.finalize(upstream_ports);
    const std::string path = TestEnvironment::writeStringToFileForTest(
        absl::StrCat("tcp_two_proxy_", listener_name, ".pb"),
        TestUtility::getProtobufBinaryStringFromMessage(helper.bootstrap()));
    createGeneratedApiTestServer(path, {listener_name}, {false, true, false}, false, out);
  }

  void initializeTcpProxies() {
    const std::string certs = TestEnvironment::runfilesPath("test/config/integration/certs");
    const std::string downstream_tls = fmt::format(R"EOF(
name: envoy.transport_sockets.tls
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
  common_tls_context:
    alpn_protocols:
    - istio2
    tls_certificates:
    - certificate_chain:
        filename: "{}/servercert.pem"
      private_key:
        filename: "{}/serverkey.pem"
)EOF",
                                                   certs, certs);
    const std::string upstream_tls = R"EOF(
name: envoy.transport_sockets.tls
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
  common_tls_context:
    alpn_protocols:
    - istio2
)EOF";

    // 1. Raw TCP app the SERVER sidecar forwards to.
    setUpstreamCount(1);
    createUpstreams();
    const uint32_t app_port = fake_upstreams_[0]->localAddress()->ip()->port();

    // 2. SERVER sidecar (INBOUND): TLS+ALPN istio2 listener with
    //    [metadata_exchange(downstream), istio.stats, tcp_proxy] -> app.
    server_config_ = std::make_unique<ConfigHelper>(version_, config_helper_.bootstrap());
    setNode(*server_config_, "server-v1", "server-ns", "server-cluster", "server-svc",
            "server-app");
    const std::string downstream_mx =
        metadataExchangeYaml("envoy.filters.network.metadata_exchange", mx_protocol_);
    const std::string server_stats = statsFilterYaml(server_stats_extra_);
    server_config_->addConfigModifier([downstream_tls, downstream_mx, server_stats](
                                          envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->set_name("inbound");
      listener->set_traffic_direction(envoy::config::core::v3::INBOUND);
      auto* fc = listener->mutable_filter_chains(0);
      fc->clear_filters();
      *fc->add_filters() = networkFilter(downstream_mx);
      *fc->add_filters() = networkFilter(server_stats);
      *fc->add_filters() = networkFilter(tcpProxyFilterYaml());
      TestUtility::loadFromYaml(downstream_tls, *fc->mutable_transport_socket());
    });
    if (enable_metadata_discovery_) {
      // The workload-discovery bootstrap extension subscribes to the workload API
      // (here a file path_config_source -- the in-test analogue of istio/proxy's
      // go-control-plane LinearCache); metadata_exchange's enable_discovery falls
      // back to it to resolve the peer by source IP when ALPN MX is unavailable.
      const std::string workloads = workloads_path_;
      server_config_->addConfigModifier(
          [workloads](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
            TestUtility::loadFromYaml(absl::StrCat(R"EOF(
name: envoy.bootstrap.workload_discovery
typed_config:
  "@type": type.googleapis.com/istio.workload.BootstrapExtension
  config_source:
    path_config_source:
      path: ")EOF",
                                                   workloads, "\"\n"),
                                      *bootstrap.add_bootstrap_extensions());
          });
    }
    startSidecar(*server_config_, {app_port}, "inbound", server_sidecar_);
    const uint32_t server_port = lookupPort("inbound");

    // 3. CLIENT sidecar (OUTBOUND): plaintext listener with [istio.stats, tcp_proxy];
    //    cluster -> SERVER sidecar over TLS+ALPN istio2 with an upstream MX filter.
    setNode(config_helper_, "client-v1", "client-ns", "client-cluster", "client-svc", "client-app");
    if (!client_role_.empty()) {
      const std::string role = client_role_;
      config_helper_.addConfigModifier([role](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        auto& labels = *(*bootstrap.mutable_node()->mutable_metadata()->mutable_fields())["LABELS"]
                            .mutable_struct_value()
                            ->mutable_fields();
        labels["role"].set_string_value(role);
      });
    }
    const std::string upstream_mx =
        metadataExchangeYaml("envoy.filters.network.upstream.metadata_exchange", mx_protocol_);
    config_helper_.addConfigModifier(
        [upstream_tls, upstream_mx](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          listener->set_name("outbound");
          listener->set_traffic_direction(envoy::config::core::v3::OUTBOUND);
          auto* fc = listener->mutable_filter_chains(0);
          fc->clear_filters();
          *fc->add_filters() = networkFilter(statsFilterYaml());
          *fc->add_filters() = networkFilter(tcpProxyFilterYaml());

          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          TestUtility::loadFromYaml(upstream_tls, *cluster->mutable_transport_socket());
          envoy::config::cluster::v3::Filter mx;
          TestUtility::loadFromYaml(upstream_mx, mx);
          *cluster->add_filters() = mx;
        });
    startSidecar(config_helper_, {server_port}, "outbound", test_server_);
  }

  // Finds the single istiocustom counter for `metric` in `store`, polling until it
  // appears with a non-zero value (TCP stats emit on the report timer / on close).
  Stats::CounterSharedPtr waitForIstioCounter(Stats::Store& store, absl::string_view metric) {
    const std::string extracted = absl::StrCat("istiocustom.", metric);
    for (int i = 0; i < 100; ++i) {
      for (const auto& counter : store.counters()) {
        if (counter->tagExtractedName() == extracted && counter->value() > 0) {
          return counter;
        }
      }
      timeSystem().realSleepDoNotUseWithoutScrutiny(std::chrono::milliseconds(50));
    }
    return nullptr;
  }

  static std::optional<std::string> tagValue(const Stats::Counter& counter, absl::string_view tag) {
    for (const auto& t : counter.tags()) {
      if (t.name_ == tag) {
        return t.value_;
      }
    }
    return std::nullopt;
  }

  // Waits for a non-zero counter whose name contains `substr`, e.g. the
  // metadata_exchange filter's own telemetry `metadata_exchange.*` (the listener
  // scope may add a prefix, so match by substring).
  Stats::CounterSharedPtr waitForCounterContaining(Stats::Store& store, absl::string_view substr) {
    for (int i = 0; i < 100; ++i) {
      for (const auto& c : store.counters()) {
        if (absl::StrContains(c->name(), substr) && c->value() > 0) {
          return c;
        }
      }
      timeSystem().realSleepDoNotUseWithoutScrutiny(std::chrono::milliseconds(50));
    }
    return nullptr;
  }

  // Drives one TCP connection through both sidecars: write, echo back, close.
  void driveTcpConnection() {
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("outbound"));
    ASSERT_TRUE(tcp_client->write("hello"));
    FakeRawConnectionPtr app_conn;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(app_conn));
    ASSERT_TRUE(app_conn->waitForData(5));
    ASSERT_TRUE(app_conn->write("world"));
    tcp_client->waitForData("world");
    tcp_client->close();
    ASSERT_TRUE(app_conn->waitForDisconnect());
  }

  // The ALPN protocol the metadata_exchange filter matches on. The TLS hop always
  // negotiates ALPN "istio2"; setting this to something else makes the filter see
  // a mismatch (alpn_protocol_not_found, no peer exchanged).
  std::string mx_protocol_{"istio2"};
  // For TestTCPMXAdditionalLabels: node LABELS keys to carry as extra peer labels,
  // an extra istio.stats body (a `role` dimension) on the server, and the client's
  // `role` label value.
  std::vector<std::string> mx_additional_labels_;
  std::string server_stats_extra_;
  std::string client_role_;
  // For TestTCPMetadataExchange/true (WDS fallback): enable metadata_exchange
  // discovery and the workload_discovery bootstrap extension reading this file.
  bool enable_metadata_discovery_{false};
  std::string workloads_path_;
  std::unique_ptr<ConfigHelper> server_config_;
  IntegrationTestServerPtr server_sidecar_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, IstioTcpTwoProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Related upstream e2e: tcp_metadata_exchange/TestTCPMetadataExchange.
// One TCP connection through both sidecars; the istio2 metadata-exchange handshake
// runs over the TLS hop. Asserts istio_tcp_connections_opened_total on both
// sidecars with the reporter and (cross) peer identities each side should see.
TEST_P(IstioTcpTwoProxyIntegrationTest, TcpStatsBothSidecars) {
  initializeTcpProxies();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("outbound"));
  ASSERT_TRUE(tcp_client->write("hello"));

  FakeRawConnectionPtr app_conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(app_conn));
  ASSERT_TRUE(app_conn->waitForData(5));
  ASSERT_TRUE(app_conn->write("world"));
  tcp_client->waitForData("world");
  tcp_client->close();
  ASSERT_TRUE(app_conn->waitForDisconnect());

  // SERVER sidecar (INBOUND): reporter=destination, source identity from the
  // downstream (client) MX peer, destination identity from the server node.
  auto server =
      waitForIstioCounter(server_sidecar_->statStore(), "istio_tcp_connections_opened_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("destination", tagValue(*server, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*server, "source_workload").value_or(""));
  EXPECT_EQ("server-v1", tagValue(*server, "destination_workload").value_or(""));

  // CLIENT sidecar (OUTBOUND): reporter=source, destination identity from the
  // upstream (server) MX peer, source identity from the client node.
  auto client =
      waitForIstioCounter(test_server_->statStore(), "istio_tcp_connections_opened_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("source", tagValue(*client, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*client, "source_workload").value_or(""));
  EXPECT_EQ("server-v1", tagValue(*client, "destination_workload").value_or(""));

  // The connection closed cleanly through both sidecars (the exact teardown the
  // earlier single-Envoy self-chain could not drive), and bytes were counted on BOTH
  // sidecars (upstream TestTCPMetadataExchange validates byte counters on each side).
  EXPECT_NE(waitForIstioCounter(server_sidecar_->statStore(), "istio_tcp_connections_closed_total"),
            nullptr);
  EXPECT_NE(waitForIstioCounter(test_server_->statStore(), "istio_tcp_connections_closed_total"),
            nullptr);
  EXPECT_NE(waitForIstioCounter(server_sidecar_->statStore(), "istio_tcp_received_bytes_total"),
            nullptr);
  EXPECT_NE(waitForIstioCounter(server_sidecar_->statStore(), "istio_tcp_sent_bytes_total"),
            nullptr);
  EXPECT_NE(waitForIstioCounter(test_server_->statStore(), "istio_tcp_received_bytes_total"),
            nullptr);
  EXPECT_NE(waitForIstioCounter(test_server_->statStore(), "istio_tcp_sent_bytes_total"), nullptr);
}

// Related upstream e2e: tcp_metadata_exchange/TestTCPMetadataExchange (ALPN-found branch).
// migrates tcp_metadata_exchange: ALPN matches the MX protocol -> the network
// metadata_exchange filter exchanges peer metadata, incrementing its own
// alpn_protocol_found + metadata_added telemetry.
TEST_P(IstioTcpTwoProxyIntegrationTest, MetadataExchangeProtocolFound) {
  initializeTcpProxies();
  driveTcpConnection();

  EXPECT_NE(waitForCounterContaining(server_sidecar_->statStore(),
                                     "metadata_exchange.alpn_protocol_found"),
            nullptr);
  EXPECT_NE(
      waitForCounterContaining(server_sidecar_->statStore(), "metadata_exchange.metadata_added"),
      nullptr);
}

// Related upstream e2e: tcp_metadata_exchange/TestTCPMetadataExchangeNoAlpn and
// tcp_metadata_exchange/TestTCPMetadataNotFoundReporting.
// migrates tcp_metadata_exchange (no-ALPN/`some-protocol` case): when the MX
// filter's protocol does not match the negotiated ALPN (istio2), no metadata is
// exchanged -> alpn_protocol_not_found, and the TCP stats carry an unknown peer.
TEST_P(IstioTcpTwoProxyIntegrationTest, MetadataExchangeProtocolNotFound) {
  mx_protocol_ = "some-protocol"; // != the negotiated ALPN "istio2"
  initializeTcpProxies();
  driveTcpConnection();

  EXPECT_NE(waitForCounterContaining(server_sidecar_->statStore(),
                                     "metadata_exchange.alpn_protocol_not_found"),
            nullptr);
  auto server =
      waitForIstioCounter(server_sidecar_->statStore(), "istio_tcp_connections_opened_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("unknown", tagValue(*server, "source_workload").value_or(""));
  // migrates TestTCPMetadataNotFoundReporting: with no MX, the client/source side
  // also reports an unknown destination peer.
  auto client =
      waitForIstioCounter(test_server_->statStore(), "istio_tcp_connections_opened_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("source", tagValue(*client, "reporter").value_or(""));
  EXPECT_EQ("unknown", tagValue(*client, "destination_workload").value_or(""));
}

// Related upstream e2e: tcp_metadata_exchange/TestTCPMetricsDuringActiveConnection.
// migrates TestTCPMetricsDuringActiveConnection (regression istio/istio#59183): the
// peer object is set for TCP, so istio_tcp_* metrics are emitted on the report timer
// *while the connection is still open*, not only at close (end_stream).
TEST_P(IstioTcpTwoProxyIntegrationTest, TcpMetricsDuringActiveConnection) {
  initializeTcpProxies();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("outbound"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr app_conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(app_conn));
  ASSERT_TRUE(app_conn->waitForData(5));
  ASSERT_TRUE(app_conn->write("world"));
  tcp_client->waitForData("world");

  // Connection still OPEN (not closed): the 1s tcp_reporting_duration timer must
  // emit received-bytes with the peer identity mid-connection.
  auto server = waitForIstioCounter(server_sidecar_->statStore(), "istio_tcp_received_bytes_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("client-v1", tagValue(*server, "source_workload").value_or(""));

  tcp_client->close();
  ASSERT_TRUE(app_conn->close());
}

// Related upstream e2e: tcp_metadata_exchange/TestTCPMetadataExchangeWithConnectionTermination.
// migrates TestTCPMetadataExchangeWithConnectionTermination: the server app terminates
// the connection abruptly (right after accept, before echoing) while the istio2 MX
// handshake has completed. The server sidecar must still record the opened and closed
// connection counters with the (known) source peer -- i.e. an early server-side close
// does not lose the TCP stats.
TEST_P(IstioTcpTwoProxyIntegrationTest, ConnectionTerminationStillReportsStats) {
  initializeTcpProxies();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("outbound"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr app_conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(app_conn));
  ASSERT_TRUE(app_conn->waitForData(5));
  // Server side terminates the connection instead of echoing.
  ASSERT_TRUE(app_conn->close());
  tcp_client->waitForDisconnect();

  // MX completed over istio2, so the source identity is known, and both the opened and
  // closed counters are emitted despite the abrupt termination.
  auto opened =
      waitForIstioCounter(server_sidecar_->statStore(), "istio_tcp_connections_opened_total");
  ASSERT_NE(opened, nullptr);
  EXPECT_EQ("destination", tagValue(*opened, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*opened, "source_workload").value_or(""));
  EXPECT_NE(waitForIstioCounter(server_sidecar_->statStore(), "istio_tcp_connections_closed_total"),
            nullptr);
}

// Related upstream e2e: tcp_metadata_exchange/TestTCPMXAdditionalLabels.
// migrates TestTCPMXAdditionalLabels: with metadata_exchange `additional_labels`, an
// extra node label (`role`) is carried in the exchanged peer metadata and surfaced
// as a stats dimension on the server.
TEST_P(IstioTcpTwoProxyIntegrationTest, MetadataExchangeAdditionalLabels) {
  mx_additional_labels_ = {"role"};
  client_role_ = "ingress";
  server_stats_extra_ = R"EOF(  metrics:
  - dimensions:
      role: "filter_state.downstream_peer.labels['role']"
)EOF";
  initializeTcpProxies();
  driveTcpConnection();

  auto server =
      waitForIstioCounter(server_sidecar_->statStore(), "istio_tcp_connections_opened_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("ingress", tagValue(*server, "role").value_or(""));
}

// Related upstream e2e: tcp_metadata_exchange/TestTCPMetadataExchange (WDS-discovery branch).
// migrates TestTCPMetadataExchange/true (WDS discovery fallback): with ALPN MX
// unavailable but enable_discovery on, metadata_exchange resolves the peer from the
// workload-discovery provider by source IP. The provider is fed a Workload resource
// (client identity at the loopback source IP) via a file path_config_source.
TEST_P(IstioTcpTwoProxyIntegrationTest, MetadataExchangeWdsFallback) {
  if (version_ == Network::Address::IpVersion::v6) {
    // The lookup is by raw address bytes; only the IPv4 loopback (127.0.0.1) byte
    // form is pinned here.
    GTEST_SKIP() << "WDS fallback test covers IPv4 loopback";
  }
  // Client identity keyed by the loopback source IP 127.0.0.1 (addresses is bytes;
  // "fwAAAQ==" is base64 of {127,0,0,1}).
  workloads_path_ = TestEnvironment::writeStringToFileForTest("workloads.yaml", R"EOF(
resources:
- "@type": type.googleapis.com/istio.workload.Workload
  uid: client-uid
  name: client-pod
  workload_name: client-v1
  namespace: client-ns
  cluster_id: client-cluster
  canonical_name: client-svc
  canonical_revision: v1
  addresses:
  - "fwAAAQ=="
)EOF",
                                                              /*fully_qualified_path=*/false);
  enable_metadata_discovery_ = true;
  mx_protocol_ = "some-protocol"; // ALPN mismatch -> MX unavailable -> WDS fallback.
  initializeTcpProxies();
  driveTcpConnection();

  EXPECT_NE(waitForCounterContaining(server_sidecar_->statStore(),
                                     "metadata_exchange.alpn_protocol_not_found"),
            nullptr);
  // Source identity resolved from the workload provider (not ALPN MX).
  auto server =
      waitForIstioCounter(server_sidecar_->statStore(), "istio_tcp_connections_opened_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("client-v1", tagValue(*server, "source_workload").value_or(""));
}

} // namespace
} // namespace Envoy

#include <string>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/bootstrap/internal_listener/v3/internal_listener.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/common/common/base64.h"

#include "test/config/utility.h"
#include "test/integration/base_integration_test.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/integration_tcp_client.h"
#include "test/integration/server.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "contrib/istio/filters/common/source/metadata_object.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

using HttpConnectionManager =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;
using TcpProxy = envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy;

// Two-Envoy Istio ambient/HBONE fixture: a raw TCP client talks to a CLIENT
// sidecar, which originates an HTTP/2 CONNECT tunnel to a SERVER sidecar
// (waypoint), which terminates CONNECT and forwards the inner TCP to a raw app.
// Covers client-side istio_tcp_* over the tunnel, server-side L7 waypoint stats,
// and the server-side L4 waypoint stats over CONNECT (internal listener +
// shared_with_upstream peer crossing the internal boundary).
//
//   raw TCP client --> CLIENT sidecar (OUTBOUND)  tcp_proxy{tunneling_config: CONNECT}
//                          | HTTP/2 CONNECT
//                          v
//                     SERVER sidecar (INBOUND)    HCM CONNECT-terminate -> app cluster
//                          v
//                     app (raw TCP FakeUpstream)
//
// The SERVER is BaseIntegrationTest's `test_server_` (httpProxyConfig base gives
// an HCM with a route_config that setConnectConfig() rewrites for CONNECT). The
// CLIENT is a second IntegrationTestServer seeded from a copy of that bootstrap,
// with its listener rewritten to a tunneling tcp_proxy and its cluster set to
// HTTP/2.
class IstioHboneTwoProxyIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  IstioHboneTwoProxyIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::httpProxyConfig()) {
    enableHalfClose(true);
  }

protected:
  static void clusterToHttp2(envoy::config::cluster::v3::Cluster& cluster) {
    ConfigHelper::HttpProtocolOptions options;
    options.mutable_explicit_http_config()->mutable_http2_protocol_options();
    ConfigHelper::setProtocolOptions(cluster, options);
  }

  static void setNode(ConfigHelper& helper, const std::string& workload, const std::string& ns,
                      const std::string& cluster, const std::string& canonical,
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
      labels[std::string(Istio::Common::CanonicalNameLabel)].set_string_value(canonical);
      labels[std::string(Istio::Common::CanonicalRevisionLabel)].set_string_value("v1");
      labels[std::string(Istio::Common::AppNameLabel)].set_string_value(app);
      labels[std::string(Istio::Common::AppVersionLabel)].set_string_value("v1");
    });
  }

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

  // Encodes a destination workload as the x-envoy-peer-metadata wire value (base64 of a
  // deterministically serialized Struct) so the raw app can play the destination peer on
  // its HTTP response, letting the client attribute the destination over the tunnel.
  static std::string destPeerMetadataHeader() {
    const Istio::Common::WorkloadMetadataObject dest(
        "dest-pod", "dest-cluster", "dest-ns", "dest-v1", "dest-svc", "v1", "dest-app", "v1",
        Istio::Common::WorkloadType::Pod, "spiffe://dest", "", "");
    const Protobuf::Struct md = Istio::Common::convertWorkloadMetadataToStruct(dest);
    const std::string bytes = Istio::Common::serializeToStringDeterministic(md);
    return Base64::encode(bytes.data(), bytes.size());
  }

  void startSidecar(ConfigHelper& helper, const std::vector<uint32_t>& upstream_ports,
                    const std::string& listener_name, IntegrationTestServerPtr& out) {
    helper.finalize(upstream_ports);
    const std::string path = TestEnvironment::writeStringToFileForTest(
        absl::StrCat("hbone_two_proxy_", listener_name, ".pb"),
        TestUtility::getProtobufBinaryStringFromMessage(helper.bootstrap()));
    createGeneratedApiTestServer(path, {listener_name}, {false, true, false}, false, out);
  }

  void initializeHbone() {
    // Snapshot the pristine bootstrap for the CLIENT before the SERVER's modifiers
    // and finalize mutate config_helper_ (otherwise the client would inherit the
    // server's already-resolved cluster endpoint and tunnel straight to the app).
    const envoy::config::bootstrap::v3::Bootstrap pristine = config_helper_.bootstrap();

    // 1. Raw TCP app the SERVER sidecar forwards the tunneled bytes to.
    setUpstreamCount(1);
    createUpstreams();
    const uint32_t app_port = fake_upstreams_[0]->localAddress()->ip()->port();

    // 2. SERVER sidecar (INBOUND): HCM terminates CONNECT and routes to the app,
    //    advertising its identity as a baggage header on the CONNECT response.
    setNode(config_helper_, "server-v1", "server-ns", "server-cluster", "server-svc", "server-app");
    // istio.stats on the terminating HCM emits waypoint-reporter stats for the
    // CONNECT stream (the server terminating HBONE is the waypoint; reporter=waypoint
    // matches TestStatsServerWaypointProxyCONNECT). Prepended first so the chain is
    // [peer_metadata, istio.stats, router].
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.istio_stats
typed_config:
  "@type": type.googleapis.com/stats.PluginConfig
  reporter: SERVER_GATEWAY
)EOF");
    // peer_metadata both discovers the client identity from the CONNECT *request*
    // baggage (source for the server's stats) and advertises the server identity on
    // the CONNECT *response* baggage (consumed by the client, runs on the encode
    // path -- the authentic istio mechanism). When server_resolve_backend_ is set it
    // also resolves the destination (backend) workload by the upstream host IP via the
    // workload-discovery provider on the response (encode) path -- mirroring the real
    // waypoint's EnableMetadataDiscovery, so istio.stats can attribute the backend.
    const std::string server_upstream_discovery = server_resolve_backend_ ? R"EOF(
  upstream_discovery:
  - workload_discovery: {})EOF"
                                                                          : "";
    config_helper_.prependFilter(absl::StrCat(R"EOF(
name: envoy.filters.http.peer_metadata
typed_config:
  "@type": type.googleapis.com/io.istio.http.peer_metadata.Config
  downstream_discovery:
  - baggage: {}
  downstream_propagation:
  - baggage: {})EOF",
                                              server_upstream_discovery, "\n"));
    if (server_resolve_backend_) {
      // The workload-discovery bootstrap extension subscribes to the workload API (here a
      // file path_config_source, the in-test analogue of istio/proxy's go-control-plane
      // LinearCache). The HTTP peer_metadata XDSMethod looks up the upstream (backend) peer
      // in it by the upstream host IP.
      const std::string workloads = workloads_path_;
      config_helper_.addConfigModifier(
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
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->set_name("inbound");
      listener->set_traffic_direction(envoy::config::core::v3::INBOUND);
    });
    config_helper_.addConfigModifier([](HttpConnectionManager& hcm) {
      hcm.set_codec_type(HttpConnectionManager::HTTP2);
      ConfigHelper::setConnectConfig(hcm, /*terminate_connect=*/true, /*allow_post=*/false);
    });
    // cluster_0 -> app is set via finalize({app_port}); HCM CONNECT terminate sends
    // the upgraded raw stream to that cluster.
    startSidecar(config_helper_, {app_port}, "inbound", test_server_);
    const uint32_t server_port = lookupPort("inbound");

    // 3. CLIENT sidecar (OUTBOUND): the ambient/HBONE encap chain.
    buildClientSidecar(pristine, server_port);
  }

  // Builds the CLIENT sidecar (OUTBOUND) ambient/HBONE encap chain, seeded from the
  // pristine server bootstrap. Shared by every HBONE test -- the client topology is
  // identical regardless of what the server does with the terminated tunnel:
  //      outbound listener  [istio.stats(network), tcp_proxy → "encap"]
  //      "encap" cluster    → internal listener "connect_originate"
  //      "connect_originate" [peer_metadata, tcp_proxy{tunneling: CONNECT} → "hbone"]
  //      "hbone" cluster    → SERVER over HTTP/2
  void buildClientSidecar(const envoy::config::bootstrap::v3::Bootstrap& pristine,
                          uint32_t server_port) {
    const std::string loopback = Network::Test::getLoopbackAddressString(version_);
    // When the client advertises its identity, the tunneling tcp_proxy adds a
    // baggage header to the CONNECT request (read from peer_metadata filter state).
    // Suppressing it models the waypoint "empty metadata" case (unknown source).
    const std::string connect_request_baggage = client_sends_baggage_ ? R"EOF(
        headers_to_add:
        - header:
            key: baggage
            value: "%FILTER_STATE(baggage:PLAIN)%")EOF"
                                                                      : "";
    client_config_ = std::make_unique<ConfigHelper>(version_, pristine);
    setNode(*client_config_, "client-v1", "client-ns", "client-cluster", "client-svc",
            "client-app");
    client_config_->addConfigModifier([server_port, loopback, connect_request_baggage](
                                          envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* sr = bootstrap.mutable_static_resources();

      // Internal-listener bootstrap extension.
      envoy::extensions::bootstrap::internal_listener::v3::InternalListener il;
      auto* be = bootstrap.add_bootstrap_extensions();
      be->set_name("envoy.bootstrap.internal_listener");
      be->mutable_typed_config()->PackFrom(il);

      // outbound listener → [istio.stats(network, source), tcp_proxy → "encap"].
      auto* outbound = sr->mutable_listeners(0);
      outbound->set_name("outbound");
      outbound->set_traffic_direction(envoy::config::core::v3::OUTBOUND);
      auto* ofc = outbound->mutable_filter_chains(0);
      ofc->clear_filters();
      TestUtility::loadFromYaml(R"EOF(
name: envoy.filters.network.istio_stats
typed_config:
  "@type": type.googleapis.com/stats.PluginConfig
  tcp_reporting_duration: 1s
)EOF",
                                *ofc->add_filters());
      TcpProxy out_tcp;
      out_tcp.set_stat_prefix("outbound_tcp");
      out_tcp.set_cluster("encap");
      auto* of = ofc->add_filters();
      of->set_name("envoy.filters.network.tcp_proxy");
      of->mutable_typed_config()->PackFrom(out_tcp);

      // "connect_originate" internal listener: downstream peer_metadata computes the
      // local (client) baggage into filter state, which the tunneling tcp_proxy adds
      // to the CONNECT request; on the response it reads the peer (server) baggage
      // that tcp_proxy saved and injects it as the MX preamble downstream.
      TestUtility::loadFromYaml(absl::StrCat(R"EOF(
name: connect_originate
internal_listener: {}
filter_chains:
- filters:
  - name: envoy.filters.network.peer_metadata
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network_filters.peer_metadata.Config
      baggage_key: baggage
  - name: envoy.filters.network.tcp_proxy
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
      stat_prefix: connect_originate
      cluster: hbone
      tunneling_config:
        hostname: "app.default.svc.cluster.local:80"
        propagate_response_headers: true)EOF",
                                             connect_request_baggage, "\n"),
                                *sr->add_listeners());

      // cluster_0 becomes "encap" → the connect_originate internal listener, with the
      // upstream peer_metadata filter consuming the MX preamble into UpstreamPeer.
      auto* encap = sr->mutable_clusters(0);
      encap->set_name("encap");
      encap->clear_load_assignment();
      TestUtility::loadFromYaml(R"EOF(
name: encap
filters:
- name: envoy.filters.network.upstream.peer_metadata
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.network_filters.peer_metadata.UpstreamConfig
load_assignment:
  cluster_name: encap
  endpoints:
  - lb_endpoints:
    - endpoint:
        address:
          envoy_internal_address:
            server_listener_name: connect_originate
            endpoint_id: hbone
)EOF",
                                *encap);

      // "hbone" cluster → SERVER over HTTP/2.
      auto* hbone = sr->add_clusters();
      TestUtility::loadFromYaml(fmt::format(R"EOF(
name: hbone
connect_timeout: 5s
load_assignment:
  cluster_name: hbone
  endpoints:
  - lb_endpoints:
    - endpoint:
        address:
          socket_address:
            address: "{}"
            port_value: {}
)EOF",
                                            loopback, server_port),
                                *hbone);
      clusterToHttp2(*hbone);
    });
    startSidecar(*client_config_, {}, "outbound", client_sidecar_);
  }

  // Client-side L7 stats over HBONE (migrates TestStatsClientSidecarCONNECT). Unlike
  // buildClientSidecar (raw TCP over the tunnel), the client OUTBOUND listener here is an
  // HCM carrying the HTTP istio.stats filter, so an L7 request is recorded as
  // istio_requests_total{reporter=source} before being tunneled:
  //   client HTTP request --> OUTBOUND HCM [istio.stats(http), router] -> cluster "encap"
  //                            "encap" -> internal listener "connect_originate"
  //                            "connect_originate" tcp_proxy{tunneling: CONNECT} -> "hbone"
  //                            "hbone" -> SERVER over HTTP/2 (terminates CONNECT -> app)
  // No network peer_metadata on the internal listener (its MX preamble would corrupt the
  // tunneled HTTP bytes); without MX the destination peer is unknown, so this asserts the
  // source-side L7 recording over CONNECT -- the point of TestStatsClientSidecarCONNECT.
  void buildClientSidecarHttp(const envoy::config::bootstrap::v3::Bootstrap& pristine,
                              uint32_t server_port) {
    const std::string loopback = Network::Test::getLoopbackAddressString(version_);
    client_config_ = std::make_unique<ConfigHelper>(version_, pristine);
    setNode(*client_config_, "client-v1", "client-ns", "client-cluster", "client-svc",
            "client-app");
    // istio.stats on the client outbound HCM records istio_requests_total{reporter=source}.
    client_config_->prependFilter(R"EOF(
name: envoy.filters.http.istio_stats
typed_config:
  "@type": type.googleapis.com/stats.PluginConfig
)EOF");
    // peer_metadata learns the destination (server/backend) identity from the upstream
    // response's x-envoy-peer-metadata header, carried back through the tunnel. Prepended
    // after istio.stats so the final chain is [peer_metadata, istio.stats, router].
    client_config_->prependFilter(R"EOF(
name: envoy.filters.http.peer_metadata
typed_config:
  "@type": type.googleapis.com/io.istio.http.peer_metadata.Config
  upstream_discovery:
  - istio_headers: {}
)EOF");
    // Route the HCM to the "encap" cluster (the internal-listener HBONE encap path).
    client_config_->addConfigModifier([](HttpConnectionManager& hcm) {
      hcm.mutable_route_config()
          ->mutable_virtual_hosts(0)
          ->mutable_routes(0)
          ->mutable_route()
          ->set_cluster("encap");
    });
    client_config_->addConfigModifier(
        [server_port, loopback](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* sr = bootstrap.mutable_static_resources();

          auto* outbound = sr->mutable_listeners(0);
          outbound->set_name("outbound");
          outbound->set_traffic_direction(envoy::config::core::v3::OUTBOUND);

          // Internal-listener bootstrap extension.
          envoy::extensions::bootstrap::internal_listener::v3::InternalListener il;
          auto* be = bootstrap.add_bootstrap_extensions();
          be->set_name("envoy.bootstrap.internal_listener");
          be->mutable_typed_config()->PackFrom(il);

          // "connect_originate" internal listener: a tunneling tcp_proxy that wraps the
          // HCM's upstream HTTP bytes in an HTTP/2 CONNECT to the SERVER.
          TestUtility::loadFromYaml(R"EOF(
name: connect_originate
internal_listener: {}
filter_chains:
- filters:
  - name: envoy.filters.network.tcp_proxy
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
      stat_prefix: connect_originate
      cluster: hbone
      tunneling_config:
        hostname: "app.default.svc.cluster.local:80"
)EOF",
                                    *sr->add_listeners());

          // cluster_0 becomes "encap" -> the connect_originate internal listener.
          auto* encap = sr->mutable_clusters(0);
          encap->set_name("encap");
          encap->clear_load_assignment();
          TestUtility::loadFromYaml(R"EOF(
name: encap
load_assignment:
  cluster_name: encap
  endpoints:
  - lb_endpoints:
    - endpoint:
        address:
          envoy_internal_address:
            server_listener_name: connect_originate
            endpoint_id: hbone
)EOF",
                                    *encap);

          // "hbone" cluster -> SERVER over HTTP/2.
          auto* hbone = sr->add_clusters();
          TestUtility::loadFromYaml(fmt::format(R"EOF(
name: hbone
connect_timeout: 5s
load_assignment:
  cluster_name: hbone
  endpoints:
  - lb_endpoints:
    - endpoint:
        address:
          socket_address:
            address: "{}"
            port_value: {}
)EOF",
                                                loopback, server_port),
                                    *hbone);
          clusterToHttp2(*hbone);
        });
    startSidecar(*client_config_, {}, "outbound", client_sidecar_);
  }

  // Brings up a CONNECT-terminating SERVER (HCM -> app) and an HTTP-fronted CLIENT
  // (buildClientSidecarHttp) for the client-side L7-over-HBONE case.
  void initializeHboneClientHttp() {
    const envoy::config::bootstrap::v3::Bootstrap pristine = config_helper_.bootstrap();

    setUpstreamCount(1);
    createUpstreams();
    const uint32_t app_port = fake_upstreams_[0]->localAddress()->ip()->port();

    // SERVER (INBOUND): HCM terminates the CONNECT tunnel and forwards the inner stream
    // to the app cluster. No istio filters needed -- this case asserts only the client.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->set_name("inbound");
      listener->set_traffic_direction(envoy::config::core::v3::INBOUND);
    });
    config_helper_.addConfigModifier([](HttpConnectionManager& hcm) {
      hcm.set_codec_type(HttpConnectionManager::HTTP2);
      ConfigHelper::setConnectConfig(hcm, /*terminate_connect=*/true, /*allow_post=*/false);
    });
    startSidecar(config_helper_, {app_port}, "inbound", test_server_);
    const uint32_t server_port = lookupPort("inbound");

    buildClientSidecarHttp(pristine, server_port);
  }

  // Server-side waypoint over CONNECT, L4 variant (migrates
  // TestTCPStatsServerWaypointProxyCONNECT). The waypoint terminates the HBONE
  // tunnel with an HCM, learns the client identity from the CONNECT-request baggage
  // via the HTTP peer_metadata filter (shared_with_upstream), and routes the inner
  // upgraded stream to an internal listener that carries the *network* istio.stats
  // filter -- which reads the shared downstream peer and emits istio_tcp_* with
  // reporter=waypoint. This is the L4 counterpart of ServerSidecarStatsOverHbone
  // (which asserts the L7 istio_requests_total{reporter=waypoint}).
  //
  //   client CONNECT --> SERVER "inbound" HCM (terminate CONNECT)
  //                        [peer_metadata(baggage, shared_with_upstream), router]
  //                        route connect_matcher -> cluster "internal_inbound"
  //                          | internal_upstream transport carries the shared
  //                          | DownstreamPeerObj across the internal boundary
  //                          v
  //                      internal listener "internal_inbound"
  //                        [istio.stats(network, reporter=waypoint), tcp_proxy]
  //                          v
  //                      cluster_0 -> app (raw TCP FakeUpstream)
  void initializeHboneWaypointTcp() {
    const envoy::config::bootstrap::v3::Bootstrap pristine = config_helper_.bootstrap();

    setUpstreamCount(1);
    createUpstreams();
    const uint32_t app_port = fake_upstreams_[0]->localAddress()->ip()->port();

    setNode(config_helper_, "server-v1", "server-ns", "server-cluster", "server-svc", "server-app");
    // The waypoint reads the client identity from the CONNECT-request baggage and marks
    // the peer filter state shared_with_upstream so it crosses the internal boundary to
    // the network istio.stats filter. No HTTP istio.stats here -- the TCP variant reports
    // istio_tcp_* from the network filter on the internal listener.
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.peer_metadata
typed_config:
  "@type": type.googleapis.com/io.istio.http.peer_metadata.Config
  downstream_discovery:
  - baggage: {}
  downstream_propagation:
  - baggage: {}
  shared_with_upstream: true
)EOF");
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* sr = bootstrap.mutable_static_resources();

      // Internal-listener bootstrap extension (the waypoint routes the inner stream
      // through an internal listener).
      envoy::extensions::bootstrap::internal_listener::v3::InternalListener il;
      auto* be = bootstrap.add_bootstrap_extensions();
      be->set_name("envoy.bootstrap.internal_listener");
      be->mutable_typed_config()->PackFrom(il);

      auto* listener = sr->mutable_listeners(0);
      listener->set_name("inbound");
      listener->set_traffic_direction(envoy::config::core::v3::INBOUND);

      // "internal_inbound" cluster -> the internal listener, with the internal_upstream
      // transport so shared_with_upstream filter state (the discovered downstream peer)
      // is carried across the internal boundary.
      auto* ic = sr->add_clusters();
      TestUtility::loadFromYaml(R"EOF(
name: internal_inbound
connect_timeout: 5s
load_assignment:
  cluster_name: internal_inbound
  endpoints:
  - lb_endpoints:
    - endpoint:
        address:
          envoy_internal_address:
            server_listener_name: internal_inbound
transport_socket:
  name: envoy.transport_sockets.internal_upstream
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.internal_upstream.v3.InternalUpstreamTransport
    transport_socket:
      name: envoy.transport_sockets.raw_buffer
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
)EOF",
                                *ic);

      // Internal listener "internal_inbound": [istio.stats(network, waypoint), tcp_proxy].
      TestUtility::loadFromYaml(R"EOF(
name: internal_inbound
internal_listener: {}
traffic_direction: INBOUND
filter_chains:
- filters:
  - name: envoy.filters.network.istio_stats
    typed_config:
      "@type": type.googleapis.com/stats.PluginConfig
      reporter: SERVER_GATEWAY
      tcp_reporting_duration: 1s
  - name: envoy.filters.network.tcp_proxy
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
      stat_prefix: internal_inbound
      cluster: cluster_0
)EOF",
                                *sr->add_listeners());
    });
    config_helper_.addConfigModifier([](HttpConnectionManager& hcm) {
      hcm.set_codec_type(HttpConnectionManager::HTTP2);
      ConfigHelper::setConnectConfig(hcm, /*terminate_connect=*/true, /*allow_post=*/false);
      // Route the terminated CONNECT stream to the internal listener (not straight to
      // the app cluster), so it traverses the network istio.stats filter.
      hcm.mutable_route_config()
          ->mutable_virtual_hosts(0)
          ->mutable_routes(0)
          ->mutable_route()
          ->set_cluster("internal_inbound");
    });
    startSidecar(config_helper_, {app_port}, "inbound", test_server_);
    const uint32_t server_port = lookupPort("inbound");

    buildClientSidecar(pristine, server_port);
  }

  // When false, the client omits its baggage on the CONNECT request, modeling the
  // waypoint "empty metadata" case (the waypoint sees an unknown source).
  bool client_sends_baggage_{true};
  // When true, the SERVER (waypoint) resolves the destination (backend) workload by the
  // upstream host IP via the workload-discovery provider reading workloads_path_.
  bool server_resolve_backend_{false};
  std::string workloads_path_;
  std::unique_ptr<ConfigHelper> client_config_;
  IntegrationTestServerPtr client_sidecar_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, IstioHboneTwoProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Related upstream e2e: stats_plugin CONNECT/HBONE cases as shared tunnel scaffolding.
// Raw bytes flow through the CONNECT tunnel end to end.
TEST_P(IstioHboneTwoProxyIntegrationTest, ConnectTunnelBytesFlow) {
  initializeHbone();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("outbound"));
  ASSERT_TRUE(tcp_client->write("hello"));

  FakeRawConnectionPtr app_conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(app_conn));
  // The inner payload (not the HTTP/2 CONNECT frames) reaches the app, proving the
  // server terminated the tunnel; bytes flow back through it too.
  ASSERT_TRUE(app_conn->waitForData(5));
  ASSERT_TRUE(app_conn->write("world"));
  tcp_client->waitForData("world");

  tcp_client->close();
  ASSERT_TRUE(app_conn->close());
}

// Related upstream e2e: stats_plugin/TestTCPStatsServerWaypointProxyCONNECT (client/HBONE
// transport side of the CONNECT flow).
// The CLIENT sidecar emits istio_tcp_* over the full HBONE encap
// (internal listener "connect_originate" + tunneling tcp_proxy + the network
// peer_metadata filters), with reporter=source, the local (client) identity, and
// the destination (server) identity learned end to end over the tunnel:
//   server HTTP peer_metadata advertises its baggage on the CONNECT response ->
//   client tcp_proxy saves it as TunnelResponseHeaders -> downstream peer_metadata
//   injects the MX preamble -> upstream peer_metadata consumes it into the peer
//   filter state -> istio.stats reads it.
//
// Closing this required a one-line fix to the network peer_metadata UpstreamFilter
// (peer_metadata.cc): it now also stores the WorkloadMetadataObject under
// UpstreamPeerObj (mirroring the metadata_exchange filter). Previously it stored
// only a CelState whose struct keys are incompatible with istio.stats' CelState
// fallback, so the peer identity was lost over HBONE.
TEST_P(IstioHboneTwoProxyIntegrationTest, ClientSidecarTcpStatsOverHbone) {
  initializeHbone();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("outbound"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr app_conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(app_conn));
  ASSERT_TRUE(app_conn->waitForData(5));
  ASSERT_TRUE(app_conn->write("world"));
  tcp_client->waitForData("world");
  tcp_client->close();
  ASSERT_TRUE(app_conn->close());

  // istio.stats emits TCP counters over the HBONE tunnel with the source identity.
  auto client =
      waitForIstioCounter(client_sidecar_->statStore(), "istio_tcp_connections_opened_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("source", tagValue(*client, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*client, "source_workload").value_or(""));
  EXPECT_EQ("server-v1", tagValue(*client, "destination_workload").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsClientSidecarCONNECT.
// Migrates TestStatsClientSidecarCONNECT: the CLIENT sidecar records L7
// istio_requests_total{reporter=source} for an HTTP request that is encapsulated into an
// HBONE CONNECT tunnel. The outbound HCM (with the HTTP peer_metadata + istio.stats
// filters) handles the request, then the tunneling tcp_proxy on the internal listener
// wraps the upstream HTTP bytes in a CONNECT to the SERVER, which terminates the tunnel
// and forwards the inner HTTP stream to the raw app. The app plays the destination peer by
// returning an x-envoy-peer-metadata response header; it travels back through the tunnel
// and the client peer_metadata filter reads it, so the client attributes BOTH the source
// (its node) and the destination (dest-v1) for the request carried over CONNECT.
TEST_P(IstioHboneTwoProxyIntegrationTest, ClientSidecarHttpRequestsTotalOverHbone) {
  initializeHboneClientHttp();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("outbound"));
  ASSERT_TRUE(
      tcp_client->write("GET / HTTP/1.1\r\nhost: server-svc.server-ns.svc.cluster.local\r\n\r\n"));

  // The inner HTTP request reaches the app (the SERVER terminated CONNECT); the app
  // replies with a raw HTTP/1 response carrying its peer metadata, which flows back
  // through the tunnel to the client.
  FakeRawConnectionPtr app_conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(app_conn));
  // waitForData(uint64_t) matches an exact byte count; the inner request size is not
  // known up front, so wait for the request line via the inexact-match validator.
  ASSERT_TRUE(app_conn->waitForData(FakeRawConnection::waitForInexactMatch("GET /")));
  ASSERT_TRUE(app_conn->write(absl::StrCat("HTTP/1.1 200 OK\r\n"
                                           "x-envoy-peer-metadata-id: dest-id\r\n"
                                           "x-envoy-peer-metadata: ",
                                           destPeerMetadataHeader(),
                                           "\r\n"
                                           "content-length: 0\r\n\r\n")));
  tcp_client->waitForData("200", /*exact_match=*/false);
  tcp_client->close();

  // The client recorded the L7 request as a source-reporter stat over the CONNECT path,
  // with the destination attributed from the response peer metadata carried back through
  // the tunnel.
  auto client = waitForIstioCounter(client_sidecar_->statStore(), "istio_requests_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("source", tagValue(*client, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*client, "source_workload").value_or(""));
  EXPECT_EQ("dest-v1", tagValue(*client, "destination_workload").value_or(""));
  EXPECT_EQ("http", tagValue(*client, "request_protocol").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsServerWaypointProxyCONNECT (full-metadata case).
// Server-side waypoint over HBONE: the SERVER (INBOUND) terminates the CONNECT
// tunnel, reads the client identity from the CONNECT-request baggage via the HTTP
// peer_metadata filter, and emits waypoint-reporter stats for the stream (the
// server terminating HBONE is the waypoint, matching TestStatsServerWaypointProxy
// CONNECT). reporter=waypoint, source=client (from the CONNECT-request baggage).
// The destination (backend) is the raw-TCP app, which provides no identity here, so
// destination_workload resolves to "unknown" (a real waypoint's backend would
// supply it via endpoint/peer metadata).
TEST_P(IstioHboneTwoProxyIntegrationTest, ServerSidecarStatsOverHbone) {
  initializeHbone();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("outbound"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr app_conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(app_conn));
  ASSERT_TRUE(app_conn->waitForData(5));
  ASSERT_TRUE(app_conn->write("world"));
  tcp_client->waitForData("world");
  tcp_client->close();
  ASSERT_TRUE(app_conn->close());

  // The waypoint (test_server_) terminated the tunnel and recorded the request with
  // the client identity learned from the CONNECT-request baggage.
  auto server = waitForIstioCounter(test_server_->statStore(), "istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("waypoint", tagValue(*server, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*server, "source_workload").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsServerWaypointProxyCONNECT (full-metadata,
// backend-attribution side).
// Like ServerSidecarStatsOverHbone, but the waypoint additionally attributes the
// destination (backend) workload. Upstream's full-metadata case supplies the backend via
// UpdateWorkloadMetadata keyed by the backend IP and asserts the destination_* tags; here
// the workload-discovery provider is fed a Workload for the app's loopback IP, and the HTTP
// peer_metadata upstream workload_discovery resolves it on the CONNECT response (encode)
// path -- by then the upstream (app) connection is established, so the upstream host IP is
// known. istio.stats(SERVER_GATEWAY) then reads UpstreamPeerObj for the destination tags.
TEST_P(IstioHboneTwoProxyIntegrationTest, ServerWaypointBackendDestinationOverHbone) {
  // Backend (destination) workload keyed by the app's loopback IP (raw network-order
  // address bytes, as the workload-discovery provider reconstructs them: "fwAAAQ==" is
  // {127,0,0,1} and the long form is ::1 = 15 zero bytes + 0x01). canonical_name doubles
  // as app name and canonical_revision as version in the workload->metadata conversion.
  workloads_path_ = TestEnvironment::writeStringToFileForTest("hbone_backend_workloads.yaml",
                                                              R"EOF(
resources:
- "@type": type.googleapis.com/istio.workload.Workload
  uid: backend-uid
  name: ratings-pod
  workload_name: ratings-v1
  namespace: bookinfo
  cluster_id: backend-cluster
  canonical_name: ratings
  canonical_revision: v1
  addresses:
  - "fwAAAQ=="
  - "AAAAAAAAAAAAAAAAAAAAAQ=="
)EOF",
                                                              /*fully_qualified_path=*/false);
  server_resolve_backend_ = true;
  initializeHbone();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("outbound"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr app_conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(app_conn));
  ASSERT_TRUE(app_conn->waitForData(5));
  ASSERT_TRUE(app_conn->write("world"));
  tcp_client->waitForData("world");
  tcp_client->close();
  ASSERT_TRUE(app_conn->close());

  auto server = waitForIstioCounter(test_server_->statStore(), "istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("waypoint", tagValue(*server, "reporter").value_or(""));
  // Source (client) learned from the CONNECT-request baggage.
  EXPECT_EQ("client-v1", tagValue(*server, "source_workload").value_or(""));
  // Destination (backend) resolved from the workload-discovery provider by the upstream
  // host IP -- the attribution upstream's full-metadata case validates.
  EXPECT_EQ("ratings-v1", tagValue(*server, "destination_workload").value_or(""));
  EXPECT_EQ("bookinfo", tagValue(*server, "destination_workload_namespace").value_or(""));
  EXPECT_EQ("ratings", tagValue(*server, "destination_canonical_service").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsServerWaypointProxyCONNECT (empty-metadata case).
// migrates TestStatsServerWaypointProxyCONNECT/empty_metadata: when the client sends
// no baggage on the CONNECT request, the waypoint records the stream with an unknown
// source (it still reports reporter=waypoint).
TEST_P(IstioHboneTwoProxyIntegrationTest, ServerWaypointStatsEmptyMetadata) {
  client_sends_baggage_ = false;
  initializeHbone();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("outbound"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr app_conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(app_conn));
  ASSERT_TRUE(app_conn->waitForData(5));
  ASSERT_TRUE(app_conn->write("world"));
  tcp_client->waitForData("world");
  tcp_client->close();
  ASSERT_TRUE(app_conn->close());

  auto server = waitForIstioCounter(test_server_->statStore(), "istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("waypoint", tagValue(*server, "reporter").value_or(""));
  EXPECT_EQ("unknown", tagValue(*server, "source_workload").value_or(""));
}

// Related upstream e2e: stats_plugin/TestTCPStatsServerWaypointProxyCONNECT.
// Migrates TestTCPStatsServerWaypointProxyCONNECT: the waypoint emits istio_tcp_*
// (L4) for the CONNECT-tunneled stream, with reporter=waypoint, the source identity
// learned from the CONNECT-request baggage (crossing the internal-listener boundary
// via shared_with_upstream), and the destination being the waypoint's own identity.
TEST_P(IstioHboneTwoProxyIntegrationTest, ServerWaypointTcpStatsOverHbone) {
  initializeHboneWaypointTcp();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("outbound"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr app_conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(app_conn));
  ASSERT_TRUE(app_conn->waitForData(5));
  ASSERT_TRUE(app_conn->write("world"));
  tcp_client->waitForData("world");
  tcp_client->close();
  ASSERT_TRUE(app_conn->close());

  // The network istio.stats on the waypoint's internal listener recorded the TCP
  // connection with the client identity (shared across the internal boundary) and the
  // waypoint reporter -- the novel L4-over-CONNECT mechanism this test exists to prove.
  auto server =
      waitForIstioCounter(test_server_->statStore(), "istio_tcp_connections_opened_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("waypoint", tagValue(*server, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*server, "source_workload").value_or(""));
  EXPECT_EQ("tcp", tagValue(*server, "request_protocol").value_or(""));
  // For a waypoint (ServerGateway reporter) destination_workload is the backend the
  // waypoint fronts, read from UpstreamPeerObj on the internal listener's *downstream*
  // connection filter state (istio_stats.cc reads info.filterState(), not the upstream
  // filter state, for ServerGateway). Nothing can write it there in this fixture:
  //   - A downstream network metadata_exchange (enable_discovery) resolves the backend in
  //     onData, which runs BEFORE tcp_proxy establishes the upstream -- so upstreamInfo()
  //     is still empty and there is no upstream host IP to resolve (unlike the L7 path,
  //     where peer_metadata runs in encodeHeaders, after the upstream is connected; that is
  //     why ServerWaypointBackendDestinationOverHbone above CAN attribute the backend).
  //   - An upstream metadata_exchange writes UpstreamPeerObj to the *upstream* connection's
  //     filter state, which ServerGateway never reads.
  //   - istio/proxy's real path (original_dst + distinct backend IPs + the gRPC workload
  //     API resolving the destination on the upstream internal listener) can't be
  //     reproduced in this in-process fixture, and placing a network filter on the
  //     internal-listener paired connection segfaults during internal-connection creation.
  // So the destination stays unknown here; the source side -- the genuinely novel part
  // (downstream peer crossing the internal-listener boundary on an L4 waypoint over
  // CONNECT) -- is fully asserted above.
  EXPECT_EQ("unknown", tagValue(*server, "destination_workload").value_or(""));
}

} // namespace
} // namespace Envoy

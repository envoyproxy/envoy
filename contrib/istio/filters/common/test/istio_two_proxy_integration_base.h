#pragma once

#include <ranges>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/common/base64.h"
#include "source/common/protobuf/utility.h"

#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "contrib/envoy/extensions/filters/http/peer_metadata/v3/peer_metadata.pb.h"
#include "contrib/istio/filters/common/source/metadata_object.h"
#include "gtest/gtest.h"

namespace Envoy {

using Istio::Common::WorkloadMetadataObject;
using Istio::Common::WorkloadType;

// Two-Envoy integration fixture for the Istio sidecar stack.
//
// Topology (HTTP/1 throughout):
//
//   test client --> CLIENT sidecar (OUTBOUND, reporter=source)
//                        |  request carries x-envoy-peer-metadata (client identity)
//                        v
//                   SERVER sidecar (INBOUND, reporter=destination)
//                        |  response carries x-envoy-peer-metadata (server identity)
//                        v
//                   app (FakeUpstream = fake_upstreams_[0])
//
// The CLIENT sidecar is BaseIntegrationTest's `test_server_` (the traffic entry
// point, with the downstream codec client). The SERVER sidecar is a second
// IntegrationTestServer built from its own ConfigHelper and cross-wired so the
// CLIENT sidecar's cluster targets the SERVER sidecar's inbound listener.
class IstioTwoProxyIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  IstioTwoProxyIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

protected:
  // Sets the bootstrap node metadata the istio.stats Context and peer_metadata
  // propagation read (WORKLOAD_NAME, NAMESPACE, CLUSTER_ID, ISTIO_VERSION, LABELS).
  static void setSidecarNode(ConfigHelper& helper, const std::string& workload,
                             const std::string& ns, const std::string& cluster,
                             const std::string& canonical_name,
                             const std::string& canonical_revision, const std::string& app,
                             const std::string& version) {
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
      labels[std::string(Istio::Common::CanonicalRevisionLabel)].set_string_value(
          canonical_revision);
      labels[std::string(Istio::Common::AppNameLabel)].set_string_value(app);
      labels[std::string(Istio::Common::AppVersionLabel)].set_string_value(version);
    });
  }

  static void setTrafficDirection(ConfigHelper& helper,
                                  envoy::config::core::v3::TrafficDirection direction,
                                  const std::string& listener_name) {
    helper.addConfigModifier(
        [direction, listener_name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          listener->set_name(listener_name);
          listener->set_traffic_direction(direction);
        });
  }

  // Builds the per-side HCM filter chain by prepending (reverse order so the final
  // chain is [peer_metadata, extra..., istio.stats, router]):
  //  - istio.stats with the side's optional extra PluginConfig body,
  //  - any side-specific extra filters (e.g. set_filter_state, grpc_stats),
  //  - peer_metadata configured for the side's discovery/propagation.
  void prependSidecarFilters(ConfigHelper& helper, bool is_client) {
    // The istio.stats slot: a full filter YAML override (e.g. config_discovery /
    // ECDS) if provided for this side, else the inline PluginConfig + extra body.
    const std::string& override_yaml =
        is_client ? client_stats_filter_override_ : server_stats_filter_override_;
    if (!override_yaml.empty()) {
      helper.prependFilter(override_yaml);
    } else {
      const std::string& stats_body = is_client ? client_stats_body_ : server_stats_body_;
      helper.prependFilter(absl::StrCat(R"EOF(
name: envoy.filters.http.istio_stats
typed_config:
  "@type": type.googleapis.com/stats.PluginConfig
)EOF",
                                        stats_body));
    }

    const auto& extras = is_client ? client_extra_filters_ : server_extra_filters_;
    for (const auto& extra : std::ranges::reverse_view(extras)) {
      helper.prependFilter(extra);
    }

    helper.prependFilter(peerMetadataConfig(is_client));
  }

  // CLIENT: upstream discovery (read peer from response) + optional upstream
  // propagation (send our identity on the request).
  // SERVER: downstream discovery
  // (read peer from request, via istio_headers or baggage) + optional downstream
  // propagation (send our identity on the response).
  std::string peerMetadataConfig(bool is_client) const {
    io::istio::http::peer_metadata::Config config;

    const auto addDiscoveryMethod = [](auto* methods, absl::string_view mode) -> void {
      auto* method = methods->Add();
      if (mode == "istio_headers") {
        method->mutable_istio_headers();
      } else if (mode == "baggage") {
        method->mutable_baggage();
      } else {
        ADD_FAILURE() << "Unsupported peer metadata discovery mode: " << mode;
      }
    };
    const auto addIstioHeadersDiscovery = [&addDiscoveryMethod](auto* methods) {
      addDiscoveryMethod(methods, "istio_headers");
    };
    const auto addIstioHeadersPropagation = [](auto* methods) {
      methods->Add()->mutable_istio_headers();
    };

    if (is_client) {
      addIstioHeadersDiscovery(config.mutable_upstream_discovery());
      if (client_propagate_) {
        addIstioHeadersPropagation(config.mutable_upstream_propagation());
      }
    } else {
      addDiscoveryMethod(config.mutable_downstream_discovery(), server_downstream_discovery_);
      if (server_upstream_discovery_) {
        // Waypoint also reads the destination peer from the upstream (app) response.
        addIstioHeadersDiscovery(config.mutable_upstream_discovery());
      }
      if (server_propagate_) {
        addIstioHeadersPropagation(config.mutable_downstream_propagation());
      }
    }

    for (const auto& label : mx_additional_labels_) {
      config.add_additional_labels(label);
    }

    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter filter;
    filter.set_name("envoy.filters.http.peer_metadata");
    std::ignore = filter.mutable_typed_config()->PackFrom(config);
    return MessageUtil::getJsonStringFromMessageOrError(filter);
  }

  // Encodes a workload as the value of x-envoy-peer-metadata (base64 of a
  // deterministically serialized Struct), the wire format peer_metadata expects.
  // Used to have the app FakeUpstream play the upstream (destination) peer.
  static std::string peerMetadataHeader(const WorkloadMetadataObject& obj) {
    const Protobuf::Struct metadata = Istio::Common::convertWorkloadMetadataToStruct(obj);
    const std::string bytes = Istio::Common::serializeToStringDeterministic(metadata);
    return Base64::encode(bytes.data(), bytes.size());
  }

  // Finalizes `helper` against `upstream_ports`, writes the bootstrap, and starts
  // a server instance into `out`. `listener_name` is registered for lookupPort().
  // Reuses the base helper so listener readiness and port registration are handled.
  void startSidecar(ConfigHelper& helper, const std::vector<uint32_t>& upstream_ports,
                    const std::string& listener_name, IntegrationTestServerPtr& out) {
    helper.finalize(upstream_ports);
    const std::string path = TestEnvironment::writeStringToFileForTest(
        absl::StrCat("two_proxy_bootstrap_", listener_name, ".pb"),
        TestUtility::getProtobufBinaryStringFromMessage(helper.bootstrap()));
    createGeneratedApiTestServer(path, {listener_name}, {false, true, false}, false, out);
  }

  // Makes a sidecar's listener (HCM) and its single upstream cluster speak HTTP/2.
  static void applyHttp2(ConfigHelper& helper) {
    helper.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.set_codec_type(envoy::extensions::filters::network::http_connection_manager::v3::
                                 HttpConnectionManager::HTTP2);
        });
    helper.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      ConfigHelper::HttpProtocolOptions protocol_options;
      protocol_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
      ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                       protocol_options);
    });
  }

  // Brings up app upstream -> SERVER sidecar -> CLIENT sidecar in order, wiring each
  // stage's cluster to the next stage's port. After this returns, `test_server_` is
  // the CLIENT sidecar and `server_sidecar_` is the SERVER one.
  void initializeTwoProxies() {
    // For gRPC: HTTP/2 downstream and on the app FakeUpstream + client cluster.
    if (http2_hops_) {
      downstream_protocol_ = Http::CodecType::HTTP2;
      setUpstreamProtocol(Http::CodecType::HTTP2);
    }

    // 1. The app the SERVER sidecar forwards to.
    setUpstreamCount(1);
    createUpstreams();
    const uint32_t app_port = fake_upstreams_[0]->localAddress()->ip()->port();

    // 2. SERVER sidecar (INBOUND): [peer_metadata, istio.stats, router], cluster -> app.
    //    Seed its ConfigHelper from the base's still-pristine bootstrap proto (a
    //    copy, no YAML re-parse) so the two sidecars share the same starting shape.
    server_config_ = std::make_unique<ConfigHelper>(version_, config_helper_.bootstrap());
    setSidecarNode(*server_config_, "server-v1", "server-ns", "server-cluster", "server-svc", "v1",
                   "server-app", "v1");
    setTrafficDirection(*server_config_, envoy::config::core::v3::INBOUND, "inbound");
    prependSidecarFilters(*server_config_, /*is_client=*/false);
    if (http2_hops_) {
      applyHttp2(*server_config_);
    }
    for (const auto& modifier : server_modifiers_) {
      server_config_->addConfigModifier(modifier);
    }
    startSidecar(*server_config_, {app_port}, "inbound", server_sidecar_);
    server_inbound_port_ = lookupPort("inbound");

    // 3. CLIENT sidecar (OUTBOUND): [peer_metadata, istio.stats, router], cluster ->
    //    SERVER sidecar. This one is `test_server_`, the traffic entry point.
    setSidecarNode(config_helper_, "client-v1", "client-ns", "client-cluster", "client-svc", "v1",
                   "client-app", "v1");
    setTrafficDirection(config_helper_, envoy::config::core::v3::OUTBOUND, "outbound");
    prependSidecarFilters(config_helper_, /*is_client=*/true);
    if (http2_hops_) {
      applyHttp2(config_helper_);
    }
    for (const auto& modifier : client_modifiers_) {
      config_helper_.addConfigModifier(modifier);
    }
    startSidecar(config_helper_, {server_inbound_port_}, "outbound", test_server_);
  }

  // Drives one downstream request through both sidecars; the app FakeUpstream
  // answers with `status` (and any `resp_headers`, e.g. an MX header to play the
  // upstream peer). Returns the downstream response.
  IntegrationStreamDecoderPtr
  runRequest(const std::string& method, const std::string& path, const std::string& authority,
             const std::string& status = "200",
             const std::vector<std::pair<std::string, std::string>>& extra_req_headers = {},
             const std::vector<std::pair<std::string, std::string>>& resp_headers = {}) {
    codec_client_ = makeHttpConnection(lookupPort("outbound"));
    Http::TestRequestHeaderMapImpl request{
        {":method", method}, {":path", path}, {":scheme", "http"}, {":authority", authority}};
    for (const auto& h : extra_req_headers) {
      request.addCopy(h.first, h.second);
    }
    auto response = codec_client_->makeHeaderOnlyRequest(request);
    waitForNextUpstreamRequest();
    Http::TestResponseHeaderMapImpl response_headers{{":status", status}};
    for (const auto& h : resp_headers) {
      response_headers.addCopy(h.first, h.second);
    }
    upstream_request_->encodeHeaders(response_headers, true);
    EXPECT_TRUE(response->waitForEndStream());
    codec_client_->close();
    return response;
  }

  // The canonical distinct client/server/dest peers used across tests.
  static WorkloadMetadataObject clientPeer() {
    return WorkloadMetadataObject("client-pod-1", "client-cluster", "client-ns", "client-v1",
                                  "client-svc", "v1", "client-app", "v1", WorkloadType::Pod,
                                  "spiffe://client", "", "");
  }
  static WorkloadMetadataObject destPeer() {
    return WorkloadMetadataObject("dest-pod-1", "dest-cluster", "dest-ns", "dest-v1", "dest-svc",
                                  "v1", "dest-app", "v1", WorkloadType::Pod, "spiffe://dest", "",
                                  "");
  }

  Stats::CounterSharedPtr clientCounter(absl::string_view metric) {
    return istioCounter(test_server_->statStore(), metric);
  }
  Stats::CounterSharedPtr serverCounter(absl::string_view metric) {
    return istioCounter(server_sidecar_->statStore(), metric);
  }

  // Finds the single istiocustom counter for `metric` in `store`.
  static Stats::CounterSharedPtr istioCounter(Stats::Store& store, absl::string_view metric) {
    const std::string extracted = absl::StrCat("istiocustom.", metric);
    for (const auto& counter : store.counters()) {
      if (counter->tagExtractedName() == extracted) {
        return counter;
      }
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

  // --- Per-side configuration, set by tests before initializeTwoProxies(). ---
  // Extra stats.PluginConfig YAML appended to each side's istio.stats filter
  // (dimensions, tags_to_remove, definitions, disable_host_header_fallback,
  // `reporter: SERVER_GATEWAY`, ...).
  std::string client_stats_body_;
  std::string server_stats_body_;
  // Full istio.stats filter YAML override per side (e.g. an ECDS config_discovery
  // filter). When set, replaces the inline PluginConfig for that side.
  std::string client_stats_filter_override_;
  std::string server_stats_filter_override_;
  // When false, that side omits propagation, so the peer falls back to endpoint
  // metadata instead of an MX header.
  bool client_propagate_{true};
  bool server_propagate_{true};
  // When true, run HTTP/2 end to end (downstream, both sidecar listeners and
  // clusters, and the app FakeUpstream) -- required for gRPC.
  bool http2_hops_{false};
  // SERVER downstream discovery method: "istio_headers" (default) or "baggage".
  std::string server_downstream_discovery_{"istio_headers"};
  // Node LABELS keys to carry as additional peer labels (peer_metadata
  // additional_labels), e.g. {"role"} for TestAdditionalLabels.
  std::vector<std::string> mx_additional_labels_;
  // When true, the SERVER sidecar also reads the upstream (app response) peer --
  // used to model a waypoint that reads both source and destination peers.
  bool server_upstream_discovery_{false};
  // Extra HCM filters prepended ahead of istio.stats on each side.
  std::vector<std::string> client_extra_filters_;
  std::vector<std::string> server_extra_filters_;
  // Arbitrary bootstrap modifiers per side (cluster/endpoint istio metadata, ECDS).
  std::vector<ConfigHelper::ConfigModifierFunction> client_modifiers_;
  std::vector<ConfigHelper::ConfigModifierFunction> server_modifiers_;

  std::unique_ptr<ConfigHelper> server_config_;
  IntegrationTestServerPtr server_sidecar_;
  uint32_t server_inbound_port_{0};
};

} // namespace Envoy

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/common.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "contrib/istio/filters/common/test/istio_two_proxy_integration_base.h"
#include "gtest/gtest.h"

// Integration suite for the Istio `istio.stats` HTTP filter, migrating the
// istio/proxy `stats_plugin` e2e. Every case runs on the two-Envoy fixture
// (`IstioTwoProxyIntegrationTest`): a real CLIENT sidecar (OUTBOUND,
// reporter=source) and a real SERVER sidecar (INBOUND, reporter=destination)
// that perform the genuine client<->server metadata-exchange handshake over the
// wire -- the sidecars exchange their own node identities, not harness-injected
// headers (some cases additionally have the app upstream return an MX response
// header to play the destination peer). Assertions are made on each sidecar's own
// stat store, exactly as the Go e2e asserts golden metrics on both sidecars.
namespace Envoy {
namespace {

INSTANTIATE_TEST_SUITE_P(IpVersions, IstioTwoProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Related upstream e2e: stats_plugin/TestStatsPayload (default case).
// TestStatsPayload (Default), both sidecars. The client learns the destination
// identity from the server's response MX header and the server learns the source
// identity from the client's request MX header -- the real handshake, asserted on
// both stat stores.
TEST_P(IstioTwoProxyIntegrationTest, RequestsTotalBothSidecars) {
  initializeTwoProxies();
  runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");

  auto client = clientCounter("istio_requests_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ(1, client->value());
  EXPECT_EQ("source", tagValue(*client, "reporter").value_or(""));
  EXPECT_EQ("200", tagValue(*client, "response_code").value_or(""));
  EXPECT_EQ("http", tagValue(*client, "request_protocol").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*client, "source_workload").value_or(""));
  EXPECT_EQ("server-v1", tagValue(*client, "destination_workload").value_or(""));

  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ(1, server->value());
  EXPECT_EQ("destination", tagValue(*server, "reporter").value_or(""));
  EXPECT_EQ("200", tagValue(*server, "response_code").value_or(""));
  EXPECT_EQ("http", tagValue(*server, "request_protocol").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*server, "source_workload").value_or(""));
  EXPECT_EQ("client-ns", tagValue(*server, "source_workload_namespace").value_or(""));
  EXPECT_EQ("server-v1", tagValue(*server, "destination_workload").value_or(""));
  EXPECT_EQ("server-ns", tagValue(*server, "destination_workload_namespace").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsPayload (custom-dimension case).
// Custom dimension via a CEL expression over a request attribute, on the server.
TEST_P(IstioTwoProxyIntegrationTest, CustomCelDimension) {
  server_stats_body_ = R"EOF(  metrics:
  - dimensions:
      request_operation: "request.method"
)EOF";
  initializeTwoProxies();
  runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");

  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("GET", tagValue(*server, "request_operation").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsPayload (CEL-expression case).
// istio.stats CEL engine: string concatenation, equality, and a ternary.
TEST_P(IstioTwoProxyIntegrationTest, CelEngineDimensions) {
  server_stats_body_ = R"EOF(  metrics:
  - dimensions:
      request_operation: "request.method + ' ' + request.url_path"
      path_is_root: "request.url_path == '/' ? 'yes' : 'no'"
)EOF";
  initializeTwoProxies();
  runRequest("GET", "/api/orders", "server-svc.server-ns.svc.cluster.local");

  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("GET /api/orders", tagValue(*server, "request_operation").value_or(""));
  EXPECT_EQ("no", tagValue(*server, "path_is_root").value_or(""));
}

// Related upstream e2e: stats_plugin/TestAdditionalLabels.
// TestAdditionalLabels: a node LABELS key (`role`) is carried as an additional peer
// label via peer_metadata `additional_labels`, and surfaced as a stats dimension on
// BOTH sidecars (upstream asserts the labeled metric on both admin ports, not one
// direction). The client propagates its `role` on the request and the server reads it
// into the downstream peer; symmetrically the server propagates its `role` on the
// response and the client reads it into the upstream peer.
TEST_P(IstioTwoProxyIntegrationTest, AdditionalLabels) {
  mx_additional_labels_ = {"role"};
  // Each node carries a distinct `role` label to propagate.
  client_modifiers_.push_back([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto& labels = *(*bootstrap.mutable_node()->mutable_metadata()->mutable_fields())["LABELS"]
                        .mutable_struct_value()
                        ->mutable_fields();
    labels["role"].set_string_value("ingress");
  });
  server_modifiers_.push_back([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto& labels = *(*bootstrap.mutable_node()->mutable_metadata()->mutable_fields())["LABELS"]
                        .mutable_struct_value()
                        ->mutable_fields();
    labels["role"].set_string_value("gateway");
  });
  // SERVER reads the source (client) role from the downstream peer; CLIENT reads the
  // destination (server) role from the upstream peer learned via the response MX.
  server_stats_body_ = R"EOF(  metrics:
  - dimensions:
      role: "filter_state.downstream_peer.labels['role']"
)EOF";
  client_stats_body_ = R"EOF(  metrics:
  - dimensions:
      role: "filter_state.upstream_peer.labels['role']"
)EOF";
  initializeTwoProxies();
  runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");

  // Forward direction: the server learned the client's `role` from the request MX.
  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("ingress", tagValue(*server, "role").value_or(""));

  // Reverse direction: the client learned the server's `role` from the response MX.
  auto client = clientCounter("istio_requests_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("gateway", tagValue(*client, "role").value_or(""));
}

// Related upstream e2e: stats_plugin/TestAttributeGen.
// Filter-state dimension (migrates TestAttributeGen): a native set_filter_state
// filter mocks the AttributeGen plugin's classified output under the wasm.<name>
// key namespace, and istio.stats reads it via filter_state[...] as a dimension.
TEST_P(IstioTwoProxyIntegrationTest, RequestOperationFromFilterState) {
  server_extra_filters_.push_back(R"EOF(
name: envoy.filters.http.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_filter_state.v3.Config
  on_request_headers:
  - object_key: wasm.istio_operationId
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "GetMethod"
)EOF");
  server_stats_body_ = R"EOF(  metrics:
  - dimensions:
      request_operation: "filter_state['wasm.istio_operationId']"
)EOF";
  initializeTwoProxies();
  runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");

  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("GetMethod", tagValue(*server, "request_operation").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsPayload (host-header fallback case).
// Host-header fallback (default): destination_service is the request authority.
TEST_P(IstioTwoProxyIntegrationTest, DestinationServiceFromHostHeader) {
  initializeTwoProxies();
  runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");

  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("server-svc.server-ns.svc.cluster.local",
            tagValue(*server, "destination_service").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsPayload (disable_host_header_fallback case).
// Host-header fallback disabled: destination_service falls back to the canonical
// service name regardless of the request authority.
TEST_P(IstioTwoProxyIntegrationTest, DisableHostHeaderFallback) {
  server_stats_body_ = R"EOF(  disable_host_header_fallback: true
)EOF";
  initializeTwoProxies();
  runRequest("GET", "/", "ignored.example.com");

  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("server-svc", tagValue(*server, "destination_service").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsPayload (tags_to_remove case).
// tags_to_remove drops a standard dimension.
TEST_P(IstioTwoProxyIntegrationTest, TagsToRemove) {
  server_stats_body_ = R"EOF(  metrics:
  - tags_to_remove:
    - request_protocol
)EOF";
  initializeTwoProxies();
  runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");

  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ(1, server->value());
  EXPECT_FALSE(tagValue(*server, "request_protocol").has_value());
  EXPECT_EQ("destination", tagValue(*server, "reporter").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsParserRegression.
// A custom metric `definitions` entry creates a new istio_<name> counter
// (also exercises the customized-config parser path, TestStatsParserRegression).
TEST_P(IstioTwoProxyIntegrationTest, CustomMetricDefinition) {
  server_stats_body_ = R"EOF(  definitions:
  - name: my_count
    type: COUNTER
    value: "1"
)EOF";
  initializeTwoProxies();
  runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");

  auto server = serverCounter("istio_my_count");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ(1, server->value());
}

// Related upstream e2e: stats_plugin/TestStatsPayload (UseHostHeader, client side).
// On the CLIENT (source reporter) destination_service falls back to the request
// authority (Host header) when the upstream cluster carries no istio `services`
// metadata -- the source-reporter counterpart of DestinationServiceFromHostHeader.
TEST_P(IstioTwoProxyIntegrationTest, ClientDestinationServiceFromHostHeader) {
  initializeTwoProxies();
  runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");

  auto client = clientCounter("istio_requests_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("source", tagValue(*client, "reporter").value_or(""));
  EXPECT_EQ("server-svc.server-ns.svc.cluster.local",
            tagValue(*client, "destination_service").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsPayload (DisableHostHeader, client side).
// With host-header fallback disabled on the CLIENT and no upstream cluster
// `services` metadata, the source reporter has no destination service to report,
// so destination_service is "unknown" -- the source-reporter counterpart of
// DisableHostHeaderFallback.
TEST_P(IstioTwoProxyIntegrationTest, ClientDisableHostHeaderFallback) {
  client_stats_body_ = R"EOF(  disable_host_header_fallback: true
)EOF";
  initializeTwoProxies();
  runRequest("GET", "/", "ignored.example.com");

  auto client = clientCounter("istio_requests_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("source", tagValue(*client, "reporter").value_or(""));
  EXPECT_EQ("unknown", tagValue(*client, "destination_service").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsPayload (Customized, client side).
// Customized config on the CLIENT: a CEL custom dimension on istio_requests_total
// plus a custom metric `definitions` entry, both surfaced on the source reporter --
// the source-reporter counterpart of CustomCelDimension + CustomMetricDefinition.
TEST_P(IstioTwoProxyIntegrationTest, ClientCustomConfig) {
  client_stats_body_ = R"EOF(  metrics:
  - dimensions:
      request_operation: "request.method"
  definitions:
  - name: my_count
    type: COUNTER
    value: "1"
)EOF";
  initializeTwoProxies();
  runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");

  auto client = clientCounter("istio_requests_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("source", tagValue(*client, "reporter").value_or(""));
  EXPECT_EQ("GET", tagValue(*client, "request_operation").value_or(""));

  auto custom = clientCounter("istio_my_count");
  ASSERT_NE(custom, nullptr);
  EXPECT_EQ(1, custom->value());
}

// Related upstream e2e: stats_plugin/TestStatsPayload (upstream-403 response-code case).
// A non-2xx *upstream* response is reflected in response_code on both sidecars (the
// normal upstream-response encode path). The local-reply path is covered separately by
// RbacLocalReply403 below.
TEST_P(IstioTwoProxyIntegrationTest, ResponseCode403) {
  initializeTwoProxies();
  runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local", /*status=*/"403");

  auto client = clientCounter("istio_requests_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("403", tagValue(*client, "response_code").value_or(""));
  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("403", tagValue(*server, "response_code").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStats403Failure.
// Migrates TestStats403Failure: a server-side RBAC filter DENIES the request, producing
// a *local reply* (403 "RBAC: access denied") -- the request never reaches the app. This
// exercises the local-reply stats path (distinct from an upstream-origin 403): istio.stats
// runs on the encode path of the locally-generated response and must still record
// istio_requests_total with response_code=403 and the source peer discovered by the
// peer_metadata filter ahead of RBAC. The RBAC filter sits between peer_metadata and
// istio.stats, matching the upstream [mx, rbac, stats] chain.
TEST_P(IstioTwoProxyIntegrationTest, RbacLocalReply403) {
  server_extra_filters_.push_back(R"EOF(
name: envoy.filters.http.rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    action: DENY
    policies:
      deny-all:
        permissions:
        - any: true
        principals:
        - any: true
)EOF");
  initializeTwoProxies();

  // Drive the request directly -- RBAC denies at the server, so the app upstream is never
  // hit (runRequest's waitForNextUpstreamRequest would hang).
  codec_client_ = makeHttpConnection(lookupPort("outbound"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "server-svc.server-ns.svc.cluster.local"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("403", response->headers().getStatusValue());
  codec_client_->close();

  // The server recorded the locally-generated 403 with the source identity discovered
  // before RBAC ran -- the local-reply path TestStats403Failure exists to cover.
  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("403", tagValue(*server, "response_code").value_or(""));
  EXPECT_EQ("destination", tagValue(*server, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*server, "source_workload").value_or(""));

  // The client saw the 403 as its upstream response.
  auto client = clientCounter("istio_requests_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("403", tagValue(*client, "response_code").value_or(""));
  EXPECT_EQ("source", tagValue(*client, "reporter").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsPayload (cluster services metadata case).
// Istio `services` metadata on the destination cluster takes precedence over the
// Host header for destination_service*.
TEST_P(IstioTwoProxyIntegrationTest, DestinationServiceFromClusterMetadata) {
  server_modifiers_.push_back([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    Protobuf::Struct istio;
    auto* services = (*istio.mutable_fields())["services"].mutable_list_value();
    auto* svc = services->add_values()->mutable_struct_value();
    (*svc->mutable_fields())["host"].set_string_value("ratings.bookinfo.svc.cluster.local");
    (*svc->mutable_fields())["name"].set_string_value("ratings");
    (*svc->mutable_fields())["namespace"].set_string_value("bookinfo");
    (*cluster->mutable_metadata()->mutable_filter_metadata())["istio"] = istio;
  });
  initializeTwoProxies();
  runRequest("GET", "/", "different.example.com");

  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("ratings.bookinfo.svc.cluster.local",
            tagValue(*server, "destination_service").value_or(""));
  EXPECT_EQ("ratings", tagValue(*server, "destination_service_name").value_or(""));
  // destination_service/name come from the cluster `services` metadata (winning over
  // the "different.example.com" Host header) -- the precedence this test exists for.
  // destination_service_namespace tracks the destination workload (node) namespace.
  EXPECT_EQ("server-ns", tagValue(*server, "destination_service_namespace").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsDestinationServiceNamespacePrecedence.
// Migrates TestStatsDestinationServiceNamespacePrecedence: on the CLIENT (source
// reporter), destination_service_namespace is taken from the upstream cluster's istio
// `services[0].namespace` metadata, which takes precedence over the destination peer's
// own workload namespace (istio_stats.cc ClientSidecar branch). Here the service
// namespace ("server") is deliberately different from the peer/workload namespace
// ("default") so the precedence is observable.
TEST_P(IstioTwoProxyIntegrationTest, DestinationServiceNamespacePrecedence) {
  // The destination peer is derived from the upstream endpoint metadata (not MX
  // propagation), giving it a workload namespace distinct from the service namespace.
  server_propagate_ = false;
  client_modifiers_.push_back([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    // Upstream cluster service metadata: service "server" in namespace "server".
    Protobuf::Struct istio;
    auto* services = (*istio.mutable_fields())["services"].mutable_list_value();
    auto* svc = services->add_values()->mutable_struct_value();
    (*svc->mutable_fields())["host"].set_string_value("server.default.svc.cluster.local");
    (*svc->mutable_fields())["name"].set_string_value("server");
    (*svc->mutable_fields())["namespace"].set_string_value("server");
    (*cluster->mutable_metadata()->mutable_filter_metadata())["istio"] = istio;
    // Destination peer (endpoint) workload lives in namespace "default".
    auto* lb = cluster->mutable_load_assignment()->mutable_endpoints(0)->mutable_lb_endpoints(0);
    Protobuf::Struct ep;
    (*ep.mutable_fields())["workload"].set_string_value(
        "ratings-v1;default;ratings;v1;server-cluster");
    (*lb->mutable_metadata()->mutable_filter_metadata())["istio"] = ep;
  });
  initializeTwoProxies();
  runRequest("GET", "/", "server.default.svc.cluster.local");

  auto client = clientCounter("istio_requests_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("source", tagValue(*client, "reporter").value_or(""));
  EXPECT_EQ("ratings-v1", tagValue(*client, "destination_workload").value_or(""));
  // The peer/workload namespace is "default".
  EXPECT_EQ("default", tagValue(*client, "destination_workload_namespace").value_or(""));
  EXPECT_EQ("server.default.svc.cluster.local",
            tagValue(*client, "destination_service").value_or(""));
  EXPECT_EQ("server", tagValue(*client, "destination_service_name").value_or(""));
  // The service namespace ("server") wins over the peer namespace ("default").
  EXPECT_EQ("server", tagValue(*client, "destination_service_namespace").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsEndpointLabels.
// Client-side endpoint metadata fallback: with the server NOT propagating its
// identity, the client derives the destination from the upstream endpoint's
// istio/workload metadata (semicolon-encoded workload;ns;canonical;rev;cluster).
TEST_P(IstioTwoProxyIntegrationTest, ClientEndpointLabels) {
  server_propagate_ = false;
  client_modifiers_.push_back([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* lb = bootstrap.mutable_static_resources()
                   ->mutable_clusters(0)
                   ->mutable_load_assignment()
                   ->mutable_endpoints(0)
                   ->mutable_lb_endpoints(0);
    Protobuf::Struct istio;
    (*istio.mutable_fields())["workload"].set_string_value(
        "ratings-v1;bookinfo;ratings;v1;remote-cluster");
    (*lb->mutable_metadata()->mutable_filter_metadata())["istio"] = istio;
  });
  initializeTwoProxies();
  runRequest("GET", "/", "ratings");

  auto client = clientCounter("istio_requests_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("source", tagValue(*client, "reporter").value_or(""));
  EXPECT_EQ("ratings-v1", tagValue(*client, "destination_workload").value_or(""));
  EXPECT_EQ("bookinfo", tagValue(*client, "destination_workload_namespace").value_or(""));
  EXPECT_EQ("ratings", tagValue(*client, "destination_canonical_service").value_or(""));
  // Endpoint metadata supplies workload/namespace/canonical but NOT app/version, so
  // those resolve to "unknown" -- the signal TestStatsEndpointLabels asserts.
  EXPECT_EQ("unknown", tagValue(*client, "destination_app").value_or(""));
  EXPECT_EQ("unknown", tagValue(*client, "destination_version").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsServerWaypointProxy.
// Waypoint (TestStatsServerWaypointProxy): the SERVER sidecar runs as a waypoint
// (reporter=SERVER_GATEWAY), reading the source from the downstream (client) MX
// peer and the destination from the upstream (app) MX peer.
TEST_P(IstioTwoProxyIntegrationTest, WaypointReporter) {
  server_stats_body_ = R"EOF(  reporter: SERVER_GATEWAY
)EOF";
  server_upstream_discovery_ = true;
  initializeTwoProxies();
  runRequest("GET", "/", "dest-svc.dest-ns.svc.cluster.local", /*status=*/"200",
             /*extra_req_headers=*/{},
             /*resp_headers=*/
             {{"x-envoy-peer-metadata-id", "dest-id"},
              {"x-envoy-peer-metadata", peerMetadataHeader(destPeer())}});

  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ(1, server->value());
  EXPECT_EQ("waypoint", tagValue(*server, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*server, "source_workload").value_or(""));
  EXPECT_EQ("client-ns", tagValue(*server, "source_workload_namespace").value_or(""));
  EXPECT_EQ("dest-v1", tagValue(*server, "destination_workload").value_or(""));
  EXPECT_EQ("dest-ns", tagValue(*server, "destination_workload_namespace").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsWithBaggageWaypointProxy.
// Waypoint with a baggage-discovered source (TestStatsWithBaggageWaypointProxy):
// the source identity arrives in the W3C `baggage` request header (passed through
// the client sidecar), the destination from the upstream (app) MX peer.
TEST_P(IstioTwoProxyIntegrationTest, WaypointBaggagePeerDiscovery) {
  server_stats_body_ = R"EOF(  reporter: SERVER_GATEWAY
)EOF";
  server_downstream_discovery_ = "baggage";
  server_upstream_discovery_ = true;
  initializeTwoProxies();
  runRequest("GET", "/", "dest-svc.dest-ns.svc.cluster.local", /*status=*/"200",
             /*extra_req_headers=*/{{"baggage", clientPeer().baggage()}},
             /*resp_headers=*/
             {{"x-envoy-peer-metadata-id", "dest-id"},
              {"x-envoy-peer-metadata", peerMetadataHeader(destPeer())}});

  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ(1, server->value());
  EXPECT_EQ("waypoint", tagValue(*server, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*server, "source_workload").value_or(""));
  EXPECT_EQ("client-app", tagValue(*server, "source_app").value_or(""));
  EXPECT_EQ("dest-v1", tagValue(*server, "destination_workload").value_or(""));
}

// Builds one length-prefixed gRPC frame carrying `payload`.
Buffer::OwnedImpl grpcFrame(absl::string_view payload) {
  Buffer::OwnedImpl frame(payload.data(), payload.size());
  Grpc::Common::prependGrpcFrameHeader(frame);
  return frame;
}

Http::TestRequestHeaderMapImpl grpcRequestHeaders() {
  return Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/example.Service/Method"},
      {":scheme", "http"}, {":authority", "server-svc.server-ns.svc.cluster.local"},
      {"te", "trailers"},  {"content-type", "application/grpc"}};
}

const std::string kGrpcStatsFilter = R"EOF(
name: envoy.filters.http.grpc_stats
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_stats.v3.FilterConfig
  emit_filter_state: true
)EOF";

// Related upstream e2e: stats_plugin/TestStatsGrpc.
// gRPC is detected by content-type (request_protocol=grpc) and grpc_response_status
// comes from the grpc-status trailer -- asserted on both the source and destination
// sidecar.
TEST_P(IstioTwoProxyIntegrationTest, GrpcRequestBothSidecars) {
  http2_hops_ = true;
  server_extra_filters_.push_back(kGrpcStatsFilter);
  client_extra_filters_.push_back(kGrpcStatsFilter);
  initializeTwoProxies();

  codec_client_ = makeHttpConnection(lookupPort("outbound"));
  auto encoder_decoder = codec_client_->startRequest(grpcRequestHeaders());
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  Buffer::OwnedImpl request_message = grpcFrame("request-body");
  codec_client_->sendData(*request_encoder_, request_message, /*end_stream=*/true);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}},
      false);
  // Non-OK gRPC status (7 = PermissionDenied), as TestStatsGrpc asserts.
  upstream_request_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"grpc-status", "7"}});
  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();

  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("grpc", tagValue(*server, "request_protocol").value_or(""));
  EXPECT_EQ("7", tagValue(*server, "grpc_response_status").value_or(""));

  auto client = clientCounter("istio_requests_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("grpc", tagValue(*client, "request_protocol").value_or(""));
  EXPECT_EQ("7", tagValue(*client, "grpc_response_status").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsGrpcStream (unary-counting portion).
// Unary gRPC: one request message, one response message; message counters = 1/1 on
// both sidecars.
TEST_P(IstioTwoProxyIntegrationTest, UnaryGrpcBothSidecars) {
  http2_hops_ = true;
  server_extra_filters_.push_back(kGrpcStatsFilter);
  client_extra_filters_.push_back(kGrpcStatsFilter);
  initializeTwoProxies();

  codec_client_ = makeHttpConnection(lookupPort("outbound"));
  auto encoder_decoder = codec_client_->startRequest(grpcRequestHeaders());
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  Buffer::OwnedImpl request_message = grpcFrame("request-body");
  codec_client_->sendData(*request_encoder_, request_message, /*end_stream=*/true);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}},
      false);
  Buffer::OwnedImpl response_message = grpcFrame("response-body");
  upstream_request_->encodeData(response_message, false);
  upstream_request_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"grpc-status", "0"}});
  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();

  for (Stats::Store* store : {&server_sidecar_->statStore(), &test_server_->statStore()}) {
    auto req_msgs = istioCounter(*store, "istio_request_messages_total");
    ASSERT_NE(req_msgs, nullptr);
    EXPECT_EQ(1, req_msgs->value());
    auto resp_msgs = istioCounter(*store, "istio_response_messages_total");
    ASSERT_NE(resp_msgs, nullptr);
    EXPECT_EQ(1, resp_msgs->value());
  }
}

// Related upstream e2e: stats_plugin/TestStatsGrpcStream.
// Streaming gRPC: several request and response messages; counters accumulate on both
// sidecars.
TEST_P(IstioTwoProxyIntegrationTest, StreamingGrpcBothSidecars) {
  http2_hops_ = true;
  server_extra_filters_.push_back(kGrpcStatsFilter);
  client_extra_filters_.push_back(kGrpcStatsFilter);
  initializeTwoProxies();

  constexpr int kRequestMessages = 3;
  constexpr int kResponseMessages = 5;

  codec_client_ = makeHttpConnection(lookupPort("outbound"));
  auto encoder_decoder = codec_client_->startRequest(grpcRequestHeaders());
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  for (int i = 0; i < kRequestMessages; ++i) {
    Buffer::OwnedImpl message = grpcFrame(absl::StrCat("request-", i));
    codec_client_->sendData(*request_encoder_, message, /*end_stream=*/i == kRequestMessages - 1);
  }

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}},
      false);
  for (int i = 0; i < kResponseMessages; ++i) {
    Buffer::OwnedImpl message = grpcFrame(absl::StrCat("response-", i));
    upstream_request_->encodeData(message, false);
  }
  upstream_request_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"grpc-status", "0"}});
  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();

  for (Stats::Store* store : {&server_sidecar_->statStore(), &test_server_->statStore()}) {
    auto req_msgs = istioCounter(*store, "istio_request_messages_total");
    ASSERT_NE(req_msgs, nullptr);
    EXPECT_EQ(kRequestMessages, req_msgs->value());
    auto resp_msgs = istioCounter(*store, "istio_response_messages_total");
    ASSERT_NE(resp_msgs, nullptr);
    EXPECT_EQ(kResponseMessages, resp_msgs->value());
  }
}

// Related upstream e2e: stats_plugin/TestStatsGrpcStream.
// Migrates the mid-stream half of TestStatsGrpcStream: message counters must be reported
// *while the stream is still open*, not only at trailers/close. For gRPC HTTP streams the
// filter arms a periodic report timer (tcp_reporting_duration) whose onReportTimer ->
// reportHelper(false) emits the message-count delta from grpc_stats. With a short interval
// we send a first batch, then -- before closing -- poll the counters until they reflect
// the in-flight messages, then close and assert the final totals. A regression that
// deferred message accounting until trailers would fail the mid-stream assertion.
TEST_P(IstioTwoProxyIntegrationTest, StreamingGrpcMidStreamReporting) {
  http2_hops_ = true;
  server_extra_filters_.push_back(kGrpcStatsFilter);
  client_extra_filters_.push_back(kGrpcStatsFilter);
  // Short periodic report interval so the timer fires mid-stream.
  server_stats_body_ = R"EOF(  tcp_reporting_duration: 0.1s
)EOF";
  client_stats_body_ = R"EOF(  tcp_reporting_duration: 0.1s
)EOF";
  initializeTwoProxies();

  // Polls a store until the istio message counter reaches `want` (the periodic timer runs
  // on the sidecar's real-time worker dispatcher, so a real sleep lets it fire).
  auto waitForMsgCount = [this](Stats::Store& store, absl::string_view metric,
                                uint64_t want) -> Stats::CounterSharedPtr {
    for (int i = 0; i < 100; ++i) {
      auto c = istioCounter(store, metric);
      if (c != nullptr && c->value() >= want) {
        return c;
      }
      timeSystem().realSleepDoNotUseWithoutScrutiny(std::chrono::milliseconds(50));
    }
    return nullptr;
  };

  constexpr int kFirstRequestMessages = 3;
  constexpr int kFirstResponseMessages = 2;

  codec_client_ = makeHttpConnection(lookupPort("outbound"));
  auto encoder_decoder = codec_client_->startRequest(grpcRequestHeaders());
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  // First batch of request messages, stream left OPEN (end_stream=false).
  for (int i = 0; i < kFirstRequestMessages; ++i) {
    Buffer::OwnedImpl message = grpcFrame(absl::StrCat("request-", i));
    codec_client_->sendData(*request_encoder_, message, /*end_stream=*/false);
  }

  // Wait for the upstream stream's headers only -- the request is deliberately still open,
  // so the usual waitForNextUpstreamRequest() (which waits for end_stream) cannot be used.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}},
      false);
  for (int i = 0; i < kFirstResponseMessages; ++i) {
    Buffer::OwnedImpl message = grpcFrame(absl::StrCat("response-", i));
    upstream_request_->encodeData(message, false);
  }

  // Mid-stream (connection still open): the periodic timer must have published the
  // in-flight message counts on the server.
  auto mid_req = waitForMsgCount(server_sidecar_->statStore(), "istio_request_messages_total",
                                 kFirstRequestMessages);
  ASSERT_NE(mid_req, nullptr);
  EXPECT_GE(mid_req->value(), kFirstRequestMessages);
  auto mid_resp = waitForMsgCount(server_sidecar_->statStore(), "istio_response_messages_total",
                                  kFirstResponseMessages);
  ASSERT_NE(mid_resp, nullptr);
  EXPECT_GE(mid_resp->value(), kFirstResponseMessages);

  // ...and on the client (source) too: upstream TestStatsGrpcStream asserts the
  // in-flight message counters on BOTH admin ports, so a client-side periodic-reporting
  // regression must also fail here. The client sidecar carries the same grpc_stats +
  // short-interval istio.stats, so its timer publishes the same in-flight deltas.
  auto mid_req_client = waitForMsgCount(test_server_->statStore(), "istio_request_messages_total",
                                        kFirstRequestMessages);
  ASSERT_NE(mid_req_client, nullptr);
  EXPECT_GE(mid_req_client->value(), kFirstRequestMessages);
  auto mid_resp_client = waitForMsgCount(test_server_->statStore(), "istio_response_messages_total",
                                         kFirstResponseMessages);
  ASSERT_NE(mid_resp_client, nullptr);
  EXPECT_GE(mid_resp_client->value(), kFirstResponseMessages);

  // Close the stream: one final response message + trailers, end the request.
  Buffer::OwnedImpl last_response = grpcFrame("response-final");
  upstream_request_->encodeData(last_response, false);
  upstream_request_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"grpc-status", "0"}});
  Buffer::OwnedImpl empty;
  codec_client_->sendData(*request_encoder_, empty, /*end_stream=*/true);
  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();

  // Final totals after close, on both sidecars.
  auto final_req = serverCounter("istio_request_messages_total");
  ASSERT_NE(final_req, nullptr);
  EXPECT_EQ(kFirstRequestMessages, final_req->value());
  auto final_resp = serverCounter("istio_response_messages_total");
  ASSERT_NE(final_resp, nullptr);
  EXPECT_EQ(kFirstResponseMessages + 1, final_resp->value());

  auto final_req_client = clientCounter("istio_request_messages_total");
  ASSERT_NE(final_req_client, nullptr);
  EXPECT_EQ(kFirstRequestMessages, final_req_client->value());
  auto final_resp_client = clientCounter("istio_response_messages_total");
  ASSERT_NE(final_resp_client, nullptr);
  EXPECT_EQ(kFirstResponseMessages + 1, final_resp_client->value());
}

// --- ECDS (migrates TestStatsECDS) ---
//
// The SERVER sidecar's istio.stats filter is delivered via Extension Config
// Discovery (a file path_config_source) rather than inline typed_config, and the
// resulting istio_requests_total must still carry reporter=destination with the
// source identity from the real client-sidecar handshake.

// Related upstream e2e: stats_plugin/TestStatsECDS (warming path).
// Warming ECDS: the listener warms on the discovered config; after config_reload
// fires we drive traffic and assert the server metric.
TEST_P(IstioTwoProxyIntegrationTest, EcdsWarming) {
  TestEnvironment::writeStringToFileForTest("istio_stats_ecds.yaml", R"EOF(
resources:
- "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
  name: envoy.filters.http.istio_stats
  typed_config:
    "@type": type.googleapis.com/stats.PluginConfig
)EOF",
                                            /*fully_qualified_path=*/false);
  server_stats_filter_override_ = TestEnvironment::substitute(R"EOF(
name: envoy.filters.http.istio_stats
config_discovery:
  type_urls:
  - type.googleapis.com/stats.PluginConfig
  config_source:
    path_config_source:
      path: "{{ test_tmpdir }}/istio_stats_ecds.yaml"
)EOF");
  initializeTwoProxies();
  server_sidecar_->waitForCounter(
      "extension_config_discovery.http_filter.envoy.filters.http.istio_stats.config_reload",
      testing::Ge(1));
  runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");

  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ(1, server->value());
  EXPECT_EQ("destination", tagValue(*server, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*server, "source_workload").value_or(""));
  EXPECT_EQ("server-v1", tagValue(*server, "destination_workload").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsECDS (default_config_without_warming case).
// default_config + apply_default_config_without_warming: the filter is active
// immediately with no discovery round-trip.
TEST_P(IstioTwoProxyIntegrationTest, EcdsDefaultConfigWithoutWarming) {
  TestEnvironment::writeStringToFileForTest("istio_stats_ecds_empty.yaml", "resources: []",
                                            /*fully_qualified_path=*/false);
  server_stats_filter_override_ = TestEnvironment::substitute(R"EOF(
name: envoy.filters.http.istio_stats
config_discovery:
  type_urls:
  - type.googleapis.com/stats.PluginConfig
  apply_default_config_without_warming: true
  default_config:
    "@type": type.googleapis.com/stats.PluginConfig
  config_source:
    path_config_source:
      path: "{{ test_tmpdir }}/istio_stats_ecds_empty.yaml"
)EOF");
  initializeTwoProxies();
  runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");

  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ(1, server->value());
  EXPECT_EQ("destination", tagValue(*server, "reporter").value_or(""));
  EXPECT_EQ("server-v1", tagValue(*server, "destination_workload").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsECDS.
// TestStatsECDS, both sidecars: istio.stats is delivered via ECDS (config_discovery) on
// BOTH the client (outbound) and server (inbound), and istio_requests_total must carry the
// right reporter/identities on each. EcdsWarming only covered the server side; this closes
// the client-side ECDS path. Note: delivery is via a file path_config_source rather than
// gRPC/ADS, so the ECDS config-application and reload logic is exercised on both sides but
// not the xDS transport itself.
TEST_P(IstioTwoProxyIntegrationTest, EcdsBothSidecars) {
  TestEnvironment::writeStringToFileForTest("istio_stats_ecds_both.yaml", R"EOF(
resources:
- "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
  name: envoy.filters.http.istio_stats
  typed_config:
    "@type": type.googleapis.com/stats.PluginConfig
)EOF",
                                            /*fully_qualified_path=*/false);
  const std::string ecds_override = TestEnvironment::substitute(R"EOF(
name: envoy.filters.http.istio_stats
config_discovery:
  type_urls:
  - type.googleapis.com/stats.PluginConfig
  config_source:
    path_config_source:
      path: "{{ test_tmpdir }}/istio_stats_ecds_both.yaml"
)EOF");
  client_stats_filter_override_ = ecds_override;
  server_stats_filter_override_ = ecds_override;
  initializeTwoProxies();

  const std::string reload_counter =
      "extension_config_discovery.http_filter.envoy.filters.http.istio_stats.config_reload";
  test_server_->waitForCounter(reload_counter, testing::Ge(1));
  server_sidecar_->waitForCounter(reload_counter, testing::Ge(1));
  runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");

  // Client (outbound) filter delivered via ECDS records the source-reporter stat.
  auto client = clientCounter("istio_requests_total");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ("source", tagValue(*client, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*client, "source_workload").value_or(""));
  EXPECT_EQ("server-v1", tagValue(*client, "destination_workload").value_or(""));

  // Server (inbound) filter delivered via ECDS records the destination-reporter stat.
  auto server = serverCounter("istio_requests_total");
  ASSERT_NE(server, nullptr);
  EXPECT_EQ("destination", tagValue(*server, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*server, "source_workload").value_or(""));
  EXPECT_EQ("server-v1", tagValue(*server, "destination_workload").value_or(""));
}

// Related upstream e2e: stats_plugin/TestStatsParallel.
// TestStatsParallel: stats-config pushes *concurrent* with in-flight traffic, on BOTH
// sidecars (upstream pushes new listener versions to both the client and the server
// nodes during the run). Each sidecar's istio.stats is delivered via ECDS (a file
// path_config_source, which reloads when a new file is moved onto the watched path).
// Two background threads each rename a stream of pre-written resource files -- alternating
// between config v1 (standard metrics) and config v2 (adds a custom `my_count` definition)
// -- onto the client's and the server's watched paths respectively, while the main thread
// drives a steady stream of requests through both sidecars. This reproduces the
// istio/proxy Fork-based race (config reload on the main thread vs. request handling on a
// worker thread) on each side rather than a single serial swap; it must not crash and both
// configs must take effect on both sidecars.
TEST_P(IstioTwoProxyIntegrationTest, StatsConfigPushUnderTraffic) {
  const std::string v0 = R"EOF(
resources:
- "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
  name: envoy.filters.http.istio_stats
  typed_config:
    "@type": type.googleapis.com/stats.PluginConfig
)EOF";
  const std::string client_watched = TestEnvironment::writeStringToFileForTest(
      "istio_stats_parallel_client.yaml", v0, /*fully_qualified_path=*/false);
  const std::string server_watched = TestEnvironment::writeStringToFileForTest(
      "istio_stats_parallel_server.yaml", v0, /*fully_qualified_path=*/false);
  client_stats_filter_override_ = TestEnvironment::substitute(R"EOF(
name: envoy.filters.http.istio_stats
config_discovery:
  type_urls:
  - type.googleapis.com/stats.PluginConfig
  config_source:
    path_config_source:
      path: "{{ test_tmpdir }}/istio_stats_parallel_client.yaml"
)EOF");
  server_stats_filter_override_ = TestEnvironment::substitute(R"EOF(
name: envoy.filters.http.istio_stats
config_discovery:
  type_urls:
  - type.googleapis.com/stats.PluginConfig
  config_source:
    path_config_source:
      path: "{{ test_tmpdir }}/istio_stats_parallel_server.yaml"
)EOF");
  initializeTwoProxies();

  const std::string reload_counter =
      "extension_config_discovery.http_filter.envoy.filters.http.istio_stats.config_reload";
  test_server_->waitForCounter(reload_counter, testing::Ge(1));
  server_sidecar_->waitForCounter(reload_counter, testing::Ge(1));

  // The two config variants. v2 adds a custom `my_count` definition.
  const std::string& v1 = v0;
  const std::string v2 = R"EOF(
resources:
- "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
  name: envoy.filters.http.istio_stats
  typed_config:
    "@type": type.googleapis.com/stats.PluginConfig
    definitions:
    - name: my_count
      type: COUNTER
      value: "1"
)EOF";

  // Pre-write the swap source files on the main thread (TestEnvironment file helpers
  // are not thread-safe); the background threads only perform std::rename (an atomic
  // libc call) onto the watched paths. Distinct source files per side so the two
  // renamers never contend.
  constexpr int kSwaps = 40;
  std::vector<std::string> client_swaps;
  std::vector<std::string> server_swaps;
  client_swaps.reserve(kSwaps);
  server_swaps.reserve(kSwaps);
  for (int i = 0; i < kSwaps; ++i) {
    client_swaps.push_back(TestEnvironment::writeStringToFileForTest(
        absl::StrCat("istio_stats_parallel_client_src_", i, ".yaml"), (i % 2 == 0) ? v1 : v2,
        /*fully_qualified_path=*/false));
    server_swaps.push_back(TestEnvironment::writeStringToFileForTest(
        absl::StrCat("istio_stats_parallel_server_src_", i, ".yaml"), (i % 2 == 0) ? v1 : v2,
        /*fully_qualified_path=*/false));
  }

  std::atomic<bool> rename_failed{false};
  const auto rename_loop = [&rename_failed](const std::vector<std::string>& srcs,
                                            const std::string& dst) {
    for (const auto& f : srcs) {
      if (std::rename(f.c_str(), dst.c_str()) != 0) {
        rename_failed.store(true);
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(3)); // NO_CHECK_FORMAT(real_time)
    }
  };
  std::thread client_pusher([&]() { rename_loop(client_swaps, client_watched); });
  std::thread server_pusher([&]() { rename_loop(server_swaps, server_watched); });

  // Drive a steady stream of requests while the config churns underneath both sidecars.
  // Requests must keep succeeding across live ECDS reloads (runRequest asserts each
  // completes).
  constexpr int kRequests = 40;
  for (int i = 0; i < kRequests; ++i) {
    runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");
  }
  client_pusher.join();
  server_pusher.join();
  ASSERT_FALSE(rename_failed.load());

  // Settle both sides on a known-final config (v2) and confirm both the standard and the
  // custom metric are emitted on each sidecar after the storm -- proving config pushes
  // under traffic neither crashed nor wedged either filter. Poll because the file watcher
  // reload is async.
  const std::string client_final = TestEnvironment::writeStringToFileForTest(
      "istio_stats_parallel_client_final.yaml", v2, /*fully_qualified_path=*/false);
  const std::string server_final = TestEnvironment::writeStringToFileForTest(
      "istio_stats_parallel_server_final.yaml", v2, /*fully_qualified_path=*/false);
  ASSERT_EQ(0, std::rename(client_final.c_str(), client_watched.c_str()));
  ASSERT_EQ(0, std::rename(server_final.c_str(), server_watched.c_str()));

  Stats::CounterSharedPtr client_my_count;
  Stats::CounterSharedPtr server_my_count;
  for (int i = 0; i < 50 && (client_my_count == nullptr || server_my_count == nullptr); ++i) {
    runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");
    client_my_count = clientCounter("istio_my_count");
    server_my_count = serverCounter("istio_my_count");
  }
  ASSERT_NE(client_my_count, nullptr);
  EXPECT_GE(client_my_count->value(), 1);
  ASSERT_NE(server_my_count, nullptr);
  EXPECT_GE(server_my_count->value(), 1);

  auto client_requests = clientCounter("istio_requests_total");
  ASSERT_NE(client_requests, nullptr);
  EXPECT_GE(client_requests->value(), 1);
  auto server_requests = serverCounter("istio_requests_total");
  ASSERT_NE(server_requests, nullptr);
  EXPECT_GE(server_requests->value(), 1);
}

} // namespace
} // namespace Envoy

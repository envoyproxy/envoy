#include "test/test_common/utility.h"

#include "contrib/istio/filters/common/test/istio_two_proxy_integration_base.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

INSTANTIATE_TEST_SUITE_P(IpVersions, IstioTwoProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Related upstream e2e: stats_plugin/TestStatsPayload (default case).
// Proof of the fixture: one request through both real sidecars, with the Istio
// metadata-exchange handshake happening over the wire (no harness-injected
// headers). Asserts istio_requests_total on BOTH sidecars with the reporter and
// peer identities each side should see -- the same assertions the istio/proxy
// stats_plugin e2e makes on its two sidecars' admin ports.
TEST_P(IstioTwoProxyIntegrationTest, RequestsTotalOnBothSidecars) {
  initializeTwoProxies();

  codec_client_ = makeHttpConnection(lookupPort("outbound"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "server-svc.server-ns.svc.cluster.local"},
  });
  // The app upstream (behind the SERVER sidecar) receives the proxied request.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  codec_client_->close();

  // CLIENT sidecar (OUTBOUND): reporter=source, source identity is the client node,
  // destination identity learned from the SERVER sidecar's response MX header.
  auto client_counter = istioCounter(test_server_->statStore(), "istio_requests_total");
  ASSERT_NE(client_counter, nullptr);
  EXPECT_EQ(1, client_counter->value());
  EXPECT_EQ("source", tagValue(*client_counter, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*client_counter, "source_workload").value_or(""));
  EXPECT_EQ("client-ns", tagValue(*client_counter, "source_workload_namespace").value_or(""));
  EXPECT_EQ("server-v1", tagValue(*client_counter, "destination_workload").value_or(""));
  EXPECT_EQ("server-ns", tagValue(*client_counter, "destination_workload_namespace").value_or(""));

  // SERVER sidecar (INBOUND): reporter=destination, source identity learned from the
  // CLIENT sidecar's request MX header, destination identity is the server node.
  auto server_counter = istioCounter(server_sidecar_->statStore(), "istio_requests_total");
  ASSERT_NE(server_counter, nullptr);
  EXPECT_EQ(1, server_counter->value());
  EXPECT_EQ("destination", tagValue(*server_counter, "reporter").value_or(""));
  EXPECT_EQ("client-v1", tagValue(*server_counter, "source_workload").value_or(""));
  EXPECT_EQ("client-ns", tagValue(*server_counter, "source_workload_namespace").value_or(""));
  EXPECT_EQ("server-v1", tagValue(*server_counter, "destination_workload").value_or(""));
  EXPECT_EQ("server-ns", tagValue(*server_counter, "destination_workload_namespace").value_or(""));
}

// Both sidecars carry app.kubernetes.io/name and app.kubernetes.io/version labels
// that differ from their raw app/version labels. Each sidecar reports its own
// app/version from the raw labels, so the peer identity learned over metadata
// exchange must use the raw labels too, or the same workload shows up under two
// names. The raw version also differs from the canonical revision, so the version
// assertions show that it comes from the raw label.
TEST_P(IstioTwoProxyIntegrationTest, AppLabelPrecedenceOnBothSidecars) {
  client_version_ = "client-raw-version";
  server_version_ = "server-raw-version";
  client_app_qualified_ = "client-qualified-app";
  client_version_qualified_ = "client-qualified-version";
  server_app_qualified_ = "server-qualified-app";
  server_version_qualified_ = "server-qualified-version";
  initializeTwoProxies();

  auto response = runRequest("GET", "/", "server-svc.server-ns.svc.cluster.local");
  EXPECT_EQ("200", response->headers().getStatusValue());

  for (auto* store : {&test_server_->statStore(), &server_sidecar_->statStore()}) {
    auto counter = istioCounter(*store, "istio_requests_total");
    ASSERT_NE(counter, nullptr);
    SCOPED_TRACE(tagValue(*counter, "reporter").value_or(""));
    EXPECT_EQ(1, counter->value());
    EXPECT_EQ("client-app", tagValue(*counter, "source_app").value_or(""));
    EXPECT_EQ("client-raw-version", tagValue(*counter, "source_version").value_or(""));
    EXPECT_EQ("server-app", tagValue(*counter, "destination_app").value_or(""));
    EXPECT_EQ("server-raw-version", tagValue(*counter, "destination_version").value_or(""));
  }
}

} // namespace
} // namespace Envoy

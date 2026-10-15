#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "source/common/common/base64.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "contrib/istio/filters/common/source/metadata_object.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

using Istio::Common::WorkloadMetadataObject;
using Istio::Common::WorkloadType;

// Integration coverage for the HTTP peer_metadata filter, migrating the istio/proxy
// `test/envoye2e/http_metadata_exchange` suite (mx_native_inbound configures
// io.istio.http.peer_metadata.Config). A server-side (inbound) Envoy runs the
// filter with istio_headers downstream discovery + propagation; the test harness
// plays the client by sending crafted x-envoy-peer-metadata[-id] request headers
// and asserts the conditional response headers.
class PeerMetadataHttpIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public HttpIntegrationTest {
public:
  PeerMetadataHttpIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initializeFilter(const std::string& additional_labels = "", bool with_network_mx = false) {
    // Use the bootstrap node verbatim (otherwise the test framework overrides
    // node.id with its default "node_name").
    use_bootstrap_node_metadata_ = true;
    if (with_network_mx) {
      // TestNativeHTTPExchange wires a TCP metadata_exchange network filter ahead of
      // the HCM to prove "TCP MX must not break HTTP MX when there is no TCP prefix or
      // TCP MX ALPN". Over this plaintext HTTP/1 listener the ALPN protocol is never
      // "istio2", so the network MX finds no match, marks no-peer, and passes the bytes
      // through to the HCM unharmed -- the HTTP MX handshake must still succeed.
      config_helper_.addNetworkFilter(R"EOF(
name: envoy.filters.network.metadata_exchange
typed_config:
  "@type": type.googleapis.com/envoy.tcp.metadataexchange.config.MetadataExchange
  protocol: istio2
)EOF");
    }
    // Node identity: the filter propagates node().id() as x-envoy-peer-metadata-id
    // and the node metadata as the x-envoy-peer-metadata blob.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto& node = *bootstrap.mutable_node();
      node.set_id("server");
      node.set_cluster("server-cluster");
      auto& fields = *node.mutable_metadata()->mutable_fields();
      fields["WORKLOAD_NAME"].set_string_value("server-v1");
      fields["NAMESPACE"].set_string_value("server-ns");
      fields["CLUSTER_ID"].set_string_value("server-cluster");
    });
    config_helper_.prependFilter(absl::StrCat(R"EOF(
name: envoy.filters.http.peer_metadata
typed_config:
  "@type": type.googleapis.com/io.istio.http.peer_metadata.Config
  downstream_discovery:
  - istio_headers: {}
  downstream_propagation:
  - istio_headers: {}
)EOF",
                                              additional_labels));
    initialize();
  }

  // Encodes a workload as the x-envoy-peer-metadata wire value (base64 of a
  // deterministically serialized Struct).
  static std::string peerMetadata() {
    const WorkloadMetadataObject client("client-pod", "client-cluster", "client-ns", "client-v1",
                                        "client-svc", "v1", "client-app", "v1", WorkloadType::Pod,
                                        "spiffe://client", "", "");
    const Protobuf::Struct metadata = Istio::Common::convertWorkloadMetadataToStruct(client);
    const std::string bytes = Istio::Common::serializeToStringDeterministic(metadata);
    return Base64::encode(bytes.data(), bytes.size());
  }

  // Drives one request with the given extra request headers, returns the response.
  IntegrationStreamDecoderPtr
  exchange(const std::vector<std::pair<std::string, std::string>>& extra_request_headers) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
    for (const auto& h : extra_request_headers) {
      request.addCopy(h.first, h.second);
    }
    auto response = codec_client_->makeHeaderOnlyRequest(request);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    EXPECT_TRUE(response->waitForEndStream());
    codec_client_->close();
    return response;
  }

  static bool hasHeader(IntegrationStreamDecoder& response, const std::string& name) {
    return !response.headers().get(Http::LowerCaseString(name)).empty();
  }
  static std::string headerValue(IntegrationStreamDecoder& response, const std::string& name) {
    auto h = response.headers().get(Http::LowerCaseString(name));
    return h.empty() ? "" : std::string(h[0]->value().getStringView());
  }

  // Drives 1000 exchanges with unique peer IDs (matching upstream's N=1000, "high
  // enough to exercise cache eviction" -- above the MX peer cache size), asserting
  // a correct exchange each time and no Envoy bug-failures. `server.envoy_bug_failures`
  // is lazily created, so absent == zero bugs == pass; if present it must be 0.
  void runExchangeLoopAndCheckNoBugFailures() {
    constexpr int kRequests = 1000;
    for (int i = 0; i < kRequests; ++i) {
      auto response = exchange({{"x-envoy-peer-metadata-id", absl::StrCat("client", i)},
                                {"x-envoy-peer-metadata", peerMetadata()}});
      EXPECT_EQ("server", headerValue(*response, "x-envoy-peer-metadata-id"));
      EXPECT_TRUE(hasHeader(*response, "x-envoy-peer-metadata"));
    }
    auto bug_failures = test_server_->counter("server.envoy_bug_failures");
    if (bug_failures != nullptr) {
      EXPECT_EQ(0, bug_failures->value());
    }
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, PeerMetadataHttpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Related upstream e2e: http_metadata_exchange/TestHTTPExchange.
// TestHTTPExchange: the server emits its peer metadata on the response only in
// proportion to what the client sent (the conditional-injection contract).
TEST_P(PeerMetadataHttpIntegrationTest, ConditionalResponseEmission) {
  initializeFilter();

  // 1. No client headers -> server emits neither response header.
  {
    auto response = exchange({});
    EXPECT_FALSE(hasHeader(*response, "x-envoy-peer-metadata-id"));
    EXPECT_FALSE(hasHeader(*response, "x-envoy-peer-metadata"));
  }
  // 2. Only the ID -> server emits its ID but withholds the metadata blob.
  {
    auto response = exchange({{"x-envoy-peer-metadata-id", "client"}});
    EXPECT_EQ("server", headerValue(*response, "x-envoy-peer-metadata-id"));
    EXPECT_FALSE(hasHeader(*response, "x-envoy-peer-metadata"));
  }
  // 3. Full exchange -> server emits both its ID and metadata blob.
  {
    auto response = exchange(
        {{"x-envoy-peer-metadata-id", "client"}, {"x-envoy-peer-metadata", peerMetadata()}});
    EXPECT_EQ("server", headerValue(*response, "x-envoy-peer-metadata-id"));
    EXPECT_TRUE(hasHeader(*response, "x-envoy-peer-metadata"));
  }
}

// Related upstream e2e: http_metadata_exchange/TestNativeHTTPExchange.
// TestNativeHTTPExchange: a TCP metadata_exchange network filter sits ahead of the
// HCM (it must not break HTTP MX when no TCP MX ALPN is negotiated), and many requests
// with unique peer IDs exercise the MX cache (eviction); the exchange stays correct
// and no Envoy bug-failures occur.
TEST_P(PeerMetadataHttpIntegrationTest, CacheEvictionNoBugFailures) {
  initializeFilter(/*additional_labels=*/"", /*with_network_mx=*/true);
  runExchangeLoopAndCheckNoBugFailures();
}

// Related upstream e2e: http_metadata_exchange/TestHTTPExchangeAdditionalLabels.
// TestHTTPExchangeAdditionalLabels: structurally identical to TestNativeHTTPExchange
// (the TCP metadata_exchange network filter sits ahead of the HCM, exactly as upstream
// sets ServerNetworkFilters=server_mx_network_filter for this case) but with the
// additional_labels filter variant -- same eviction load + bug-failure guard, exercised
// under the labels config with network MX present. This guards the regression where
// network MX interferes specifically with *labeled* HTTP MX.
TEST_P(PeerMetadataHttpIntegrationTest, AdditionalLabels) {
  initializeFilter(R"EOF(  additional_labels:
  - role
)EOF",
                   /*with_network_mx=*/true);
  runExchangeLoopAndCheckNoBugFailures();
}

} // namespace
} // namespace Envoy

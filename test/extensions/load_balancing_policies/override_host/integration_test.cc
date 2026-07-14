#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/network/address.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace OverrideHost {
namespace {

using ::Envoy::TestEnvironment;
using ::Envoy::TestUtility;
using ::Envoy::Http::CodecType;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestResponseHeaderMapImpl;
using ::Envoy::Network::Address::IpVersion;

constexpr absl::string_view kSetMetadataFilterConfigPrimaryOnly = R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  metadata:
  - metadata_namespace: envoy.lb
    value:
      x-gateway-destination-endpoint: "{}"
)EOF";

constexpr absl::string_view kLbConfigWithMetadataOnlyEndpointSelection = R"EOF(
policies:
- typed_extension_config:
    name: envoy.load_balancing_policies.override_host
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.override_host.v3.OverrideHost
        override_host_sources:
        - metadata:
            key: "envoy.lb"
            path:
            - key: "x-gateway-destination-endpoint"
        fallback_policy:
          policies:
          - typed_extension_config:
              name: envoy.load_balancing_policies.round_robin
              typed_config:
                  "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin
)EOF";

// Use the default header names.
constexpr absl::string_view kLbConfigWithHeaderEndpointSelection = R"EOF(
policies:
- typed_extension_config:
    name: envoy.load_balancing_policies.override_host
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.override_host.v3.OverrideHost
        override_host_sources:
        - header: "x-gateway-destination-endpoint"
        - metadata:
            key: "envoy.lb"
            path:
            - key: "x-gateway-destination-endpoint"
        fallback_policy:
          policies:
          - typed_extension_config:
              name: envoy.load_balancing_policies.round_robin
              typed_config:
                  "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin
)EOF";

constexpr absl::string_view kLbConfigWithTestLb = R"EOF(
policies:
- typed_extension_config:
    name: envoy.load_balancing_policies.override_host
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.override_host.v3.OverrideHost
        override_host_sources:
        - header: "x-gateway-destination-endpoint"
        - metadata:
            key: "envoy.lb"
            path:
            - key: "x-gateway-destination-endpoint"
        fallback_policy:
          policies:
          - typed_extension_config:
              name: com.google.load_balancers.override_host.test
              typed_config:
                  "@type": type.googleapis.com/test.load_balancing_policies.override_host.Config
)EOF";

class OverrideHostIntegrationTest : public testing::TestWithParam<IpVersion>,
                                    public ::Envoy::HttpIntegrationTest {
public:
  OverrideHostIntegrationTest() : HttpIntegrationTest(CodecType::HTTP1, GetParam()) {
    setUpstreamCount(3);
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Metadata filter needs to be configured after upstreams are created since
    // it uses the upstream local address.
    if (set_metadata_filter_config_modifier_) {
      set_metadata_filter_config_modifier_();
    }
  }

  void
  initializeConfig(absl::string_view policy_yaml = kLbConfigWithMetadataOnlyEndpointSelection) {
    config_helper_.addConfigModifier([policy_yaml](
                                         ::envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
      ASSERT_EQ(cluster_0->name(), "cluster_0");
      auto* endpoint = cluster_0->mutable_load_assignment()->mutable_endpoints()->Mutable(0);

      constexpr absl::string_view endpoints_yaml = R"EOF(
          locality:
            region: "us-east1"
            zone: "us-east1-a"
          lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: 0
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: 0
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: 0
          )EOF";

      const std::string local_address = Envoy::Network::Test::getLoopbackAddressString(GetParam());
      TestUtility::loadFromYaml(
          fmt::format(endpoints_yaml, local_address, local_address, local_address), *endpoint);

      auto* policy = cluster_0->mutable_load_balancing_policy();
      TestUtility::loadFromYaml(std::string(policy_yaml), *policy);
    });

    HttpIntegrationTest::initialize();
  }

  void runLoadBalancing(std::function<void(const std::vector<uint64_t>&)> validate_indexes) {
    std::vector<uint64_t> indexs;

    for (uint64_t i = 0; i < 8; i++) {
      codec_client_ = makeHttpConnection(lookupPort("http"));

      auto response = codec_client_->makeRequestWithBody(request_headers_, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2});
      ASSERT_TRUE(upstream_index.has_value());
      indexs.push_back(upstream_index.value());

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      cleanupUpstreamAndDownstream();
    }

    validate_indexes(indexs);
  }

  std::function<void()> set_metadata_filter_config_modifier_;
  TestRequestHeaderMapImpl request_headers_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, OverrideHostIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Validate that without the selected endpoints metadata the fallback LB is used
TEST_P(OverrideHostIntegrationTest, WithoutMetadataOrHeaderFallbackLbIsUsed) {
  initializeConfig();
  runLoadBalancing([](absl::Span<const uint64_t> indexs) {
    // Fallback picking LB is Round Robin, so the indexes are expected to be
    // different.
    for (uint64_t i = 2; i < 8; i++) {
      EXPECT_NE(indexs[i], indexs[i - 1]);
      EXPECT_NE(indexs[i - 1], indexs[i - 2]);
    }
  });
}

// Set the first endpoint in the selected endpoints metadata and validate that
// no other endpoints were used in proxying requests.
TEST_P(OverrideHostIntegrationTest, UseFirstEndpoint) {
  set_metadata_filter_config_modifier_ = [this]() {
    config_helper_.prependFilter(fmt::format(kSetMetadataFilterConfigPrimaryOnly,
                                             fake_upstreams_[0]->localAddress()->asStringView()));
  };

  initializeConfig();
  runLoadBalancing([](absl::Span<const uint64_t> indexs) {
    // test forces to use the first endpoint, so all indexes should be equal.
    for (uint64_t i = 1; i < 8; i++) {
      EXPECT_EQ(indexs[i], indexs[i - 1]);
    }
  });
}

// TODO(yanavlasov): Add support for header and metadata with fallback endpoints
TEST_P(OverrideHostIntegrationTest, RetriesUseFallbackLb) {
  // Only set primary endpoint in the SelectedEndpoints metadata.
  set_metadata_filter_config_modifier_ = [this]() {
    config_helper_.prependFilter(fmt::format(kSetMetadataFilterConfigPrimaryOnly,
                                             fake_upstreams_[1]->localAddress()->asStringView()));
  };

  initializeConfig(kLbConfigWithTestLb);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                           {":path", "/"},
                                           {":scheme", "http"},
                                           {":authority", "example.com"},
                                           {"x-forwarded-for", "10.0.0.1"},
                                           {"x-envoy-retry-on", "retriable-4xx"}};

  auto response = codec_client_->makeRequestWithBody(request_headers, 0);

  waitForNextUpstreamRequest(1);
  TestResponseHeaderMapImpl error409_response_headers{{":status", "409"}};
  upstream_request_->encodeHeaders(error409_response_headers, true);

  std::vector<Envoy::FakeHttpConnectionPtr> upstream_connections;
  upstream_connections.push_back(std::move(fake_upstream_connection_));
  std::vector<Envoy::FakeStreamPtr> upstream_requests;
  upstream_requests.push_back(std::move(upstream_request_));

  // Without fallback endpoints, the fallback LB policy will be used. It is
  // test LB, which always returns endpoint at index 0.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(error409_response_headers, true);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  // Envoy is configured with only 1 retry attempt, so it will return 409 from
  // the second attempt.
  EXPECT_EQ(response->headers().getStatusValue(), "409");
}

// Set the second endpoint in the selected endpoint header and validate that
// no other endpoints were used in proxying requests.
TEST_P(OverrideHostIntegrationTest, UseFirstEndpointFromHeaders) {
  // Set the first endpoint in the metadata
  set_metadata_filter_config_modifier_ = [this]() {
    config_helper_.prependFilter(fmt::format(kSetMetadataFilterConfigPrimaryOnly,
                                             fake_upstreams_[0]->localAddress()->asStringView()));
  };

  initializeConfig(kLbConfigWithHeaderEndpointSelection);
  // Specify second endpoint in the header
  request_headers_.setCopy(Envoy::Http::LowerCaseString("x-gateway-destination-endpoint"),
                           fake_upstreams_[1]->localAddress()->asStringView());
  runLoadBalancing([](absl::Span<const uint64_t> indexes) {
    // Second endpoint specified in the header should be used.
    EXPECT_EQ(indexes[0], 1);
    for (uint64_t i = 1; i < 8; i++) {
      EXPECT_EQ(indexes[i], indexes[i - 1]);
    }
  });
}

} // namespace
} // namespace OverrideHost
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy

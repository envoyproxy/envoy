#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/thread_local_store.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class GrpcStatsDirectResponseIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  GrpcStatsDirectResponseIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void SetUp() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* route_config = hcm.mutable_route_config();
          auto* route = route_config->mutable_virtual_hosts(0)->mutable_routes(0);
          route->mutable_match()->set_prefix("/");
          auto* direct_response = route->mutable_direct_response();
          direct_response->set_status(404);
          direct_response->mutable_body()->set_inline_string("not found");
        });

    const std::string filter =
        R"yaml(
            name: envoy.filters.http.grpc_stats
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_stats.v3.FilterConfig
              stats_for_all_methods: true
              enable_upstream_stats: true
        )yaml";
    config_helper_.prependFilter(filter);
    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GrpcStatsDirectResponseIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GrpcStatsDirectResponseIntegrationTest, ConnectRequestToDirectResponse) {
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/anything"},
                                     {":scheme", "http"},
                                     {":authority", "127.0.0.1"},
                                     {"content-type", "application/connect+proto"}});

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
  EXPECT_EQ("not found", response->body());
}

class GrpcStatsTagsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  GrpcStatsTagsIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GrpcStatsTagsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// The gRPC request stats have the same flat names, tag-extracted names and tags whether the tags
// are derived by the legacy tag-extraction rules (IPv4) or supplied explicitly (IPv6).
TEST_P(GrpcStatsTagsIntegrationTest, StatsTagsAndNamesAreIdenticalInBothModes) {
  // Exercise both stats modes without doubling the test matrix: the two IP versions run the
  // server in different modes. The mode is read during server initialization, before the runtime
  // loader exists, so it has to be set directly rather than with addRuntimeOverride().
  const bool explicit_tags = GetParam() == Network::Address::IpVersion::v6;
  Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.enable_stats_explicit_tags",
                                explicit_tags);
  config_helper_.prependFilter(R"yaml(
            name: envoy.filters.http.grpc_stats
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_stats.v3.FilterConfig
              stats_for_all_methods: true
        )yaml");
  initialize();

  // Sanity check that the parameterized mode really took effect; otherwise both IP versions
  // would silently be exercising the same thing.
  auto* store = dynamic_cast<Stats::ThreadLocalStoreImpl*>(&test_server_->statStore());
  ASSERT_NE(store, nullptr);
  EXPECT_EQ(store->useExplicitTags(), explicit_tags);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/svcname/Method"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"content-type", "application/grpc"}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"content-type", "application/grpc"}, {"grpc-status", "0"}},
      true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Checks a counter's flat name, tag-extracted name and tags.
  auto expect_stat = [this](const std::string& name, const std::string& tag_extracted_name,
                            std::vector<std::pair<std::string, std::string>> expected_tags) {
    const Stats::CounterSharedPtr counter = test_server_->counter(name);
    ASSERT_NE(counter, nullptr) << "no counter named '" << name << "'";
    EXPECT_EQ(counter->tagExtractedName(), tag_extracted_name) << " for stat '" << name << "'";
    std::vector<std::pair<std::string, std::string>> actual_tags;
    for (const Stats::Tag& tag : counter->tags()) {
      actual_tags.emplace_back(tag.name_, tag.value_);
    }
    std::sort(actual_tags.begin(), actual_tags.end());
    std::sort(expected_tags.begin(), expected_tags.end());
    EXPECT_EQ(actual_tags, expected_tags) << " for stat '" << name << "'";
  };

  const std::vector<std::pair<std::string, std::string>> tags{
      {"envoy.cluster_name", "cluster_0"},
      {"envoy.grpc_bridge_service", "svcname"},
      {"envoy.grpc_bridge_method", "Method"}};
  expect_stat("cluster.cluster_0.grpc.svcname.Method.success", "cluster.grpc.success", tags);
  expect_stat("cluster.cluster_0.grpc.svcname.Method.total", "cluster.grpc.total", tags);
  expect_stat("cluster.cluster_0.grpc.svcname.Method.0", "cluster.grpc.0", tags);
}

} // namespace
} // namespace Envoy

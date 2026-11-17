#include <string>
#include <tuple>
#include <vector>

#include "envoy/extensions/router/cluster_specifiers/priority_group/v3/priority_group.pb.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace PriorityGroup {
namespace {

using PriorityGroupClusterSpecifierProto = envoy::extensions::router::cluster_specifiers::
    priority_group::v3::PriorityGroupClusterSpecifier;

// Reports the cluster of the last attempt, so that a test can tell which cluster produced the
// response that reached the downstream.
constexpr char ServedByHeader[] = "x-served-by";

// The clusters of the test configuration. Cluster ``cluster_<n>`` is served by the fake upstream
// with the index ``<n>``, because the ports of the fake upstreams are assigned to the endpoints of
// the static clusters in order.
constexpr char ClusterA[] = "cluster_a";
constexpr char ClusterB[] = "cluster_b";
constexpr char ClusterC[] = "cluster_c";

class PriorityGroupIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  PriorityGroupIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    // The upstreams answer on their own, so a test only states which response every cluster gives
    // and then asserts where the attempts of the request landed.
    autonomous_upstream_ = true;
  }

  // Declares the three clusters and gives every one of them its own fake upstream. The first
  // cluster of the default configuration is renamed and the other two are copies of it.
  void setupClusters() {
    setUpstreamCount(3);
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      static_resources->mutable_clusters(0)->set_name(ClusterA);
      for (const auto& name : {ClusterB, ClusterC}) {
        auto* cluster = static_resources->add_clusters();
        cluster->MergeFrom(static_resources->clusters(0));
        cluster->set_name(name);
      }
    });
  }

  // Installs the cluster specifier with the given configuration on the only route, together with
  // the given retry policy, and reports the cluster of the last attempt in a response header.
  void setupRoute(const std::string& specifier_yaml, const std::string& retry_policy_yaml) {
    config_helper_.addConfigModifier(
        [specifier_yaml, retry_policy_yaml](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          PriorityGroupClusterSpecifierProto specifier_config;
          TestUtility::loadFromYaml(specifier_yaml, specifier_config);

          auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
          auto* header = virtual_host->add_response_headers_to_add()->mutable_header();
          header->set_key(ServedByHeader);
          header->set_value("%UPSTREAM_CLUSTER%");

          auto* route_action = virtual_host->mutable_routes(0)->mutable_route();
          route_action->clear_cluster();
          TestUtility::loadFromYaml(retry_policy_yaml, *route_action->mutable_retry_policy());

          auto* plugin =
              route_action->mutable_inline_cluster_specifier_plugin()->mutable_extension();
          plugin->set_name("priority-group-specifier");
          std::ignore = plugin->mutable_typed_config()->PackFrom(specifier_config);
        });
  }

  // Sets the dynamic metadata that overrides the priority groups of every request, the way a
  // filter ahead of the router would. ``groups_lua`` is the Lua value of the group override list.
  //
  // The route, and with it the cluster of the initial attempt, is resolved before the filter chain
  // runs, so a filter that wants its groups to apply to the initial attempt has to clear the route
  // cache as well. ``clear_route_cache`` decides whether this filter does.
  void setupGroupOverrideMetadata(absl::string_view groups_lua, bool clear_route_cache = true) {
    config_helper_.prependFilter(absl::StrReplaceAll(
        R"EOF(
name: envoy.filters.http.lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        request_handle:streamInfo():dynamicMetadata():set("envoy.test", "priority_groups", GROUPS)
        CLEAR_ROUTE_CACHE
      end
)EOF",
        {{"GROUPS", groups_lua},
         {"CLEAR_ROUTE_CACHE", clear_route_cache ? "request_handle:clearRouteCache()" : ""}}));
  }

  // Sets the typed dynamic metadata that overrides the priority groups of every request.
  // ``groups_yaml`` is the YAML of the overriding ``PriorityGroupsOverride`` message. The Lua
  // filter is only there to clear the route cache, because the route, and with it the cluster of
  // the initial attempt, is resolved before the filter chain runs.
  void setupTypedGroupOverrideMetadata(absl::string_view groups_yaml) {
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        request_handle:clearRouteCache()
      end
)EOF");
    config_helper_.prependFilter(absl::StrReplaceAll(
        R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  metadata:
  - metadata_namespace: envoy.test
    typed_value:
      "@type": type.googleapis.com/envoy.extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupsOverride
      GROUPS
)EOF",
        {{"GROUPS", groups_yaml}}));
  }

  // Makes the cluster served by the given fake upstream answer every request with the given
  // status, so that a test can decide which attempts of a request fail.
  void setUpstreamStatus(uint64_t upstream_index, const std::string& status) {
    static_cast<AutonomousUpstream*>(fake_upstreams_[upstream_index].get())
        ->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
            Http::TestResponseHeaderMapImpl({{":status", status}})));
  }

  Http::TestRequestHeaderMapImpl
  requestHeaders(const std::vector<std::pair<std::string, std::string>>& extra_headers = {}) {
    Http::TestRequestHeaderMapImpl headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};
    for (const auto& [key, value] : extra_headers) {
      headers.setCopy(Http::LowerCaseString(key), value);
    }
    return headers;
  }

  std::string servedBy(const Http::ResponseHeaderMap& headers) {
    const auto values = headers.get(Http::LowerCaseString(ServedByHeader));
    return values.empty() ? "" : std::string(values[0]->value().getStringView());
  }

  uint64_t requestsTo(const std::string& cluster) {
    return test_server_->counter(absl::StrCat("cluster.", cluster, ".upstream_rq_total"))->value();
  }

  // The retry policy that every group based test needs: the failures of the upstreams are
  // retriable and the cluster is re-selected on every attempt.
  static std::string retryPolicy(uint32_t num_retries) {
    return absl::StrReplaceAll(R"EOF(
retry_on: 5xx
num_retries: NUM_RETRIES
refresh_cluster_on_retry: true
)EOF",
                               {{"NUM_RETRIES", absl::StrCat(num_retries)}});
  }

  // A group per cluster, in the given order.
  static std::string groupPerCluster(const std::vector<std::string>& clusters) {
    std::string yaml = "priority_groups:";
    for (const auto& cluster : clusters) {
      absl::StrAppend(&yaml, absl::StrReplaceAll(R"EOF(
- name: CLUSTER
  clusters:
  - cluster_name: CLUSTER
    weight: 100)EOF",
                                                 {{"CLUSTER", cluster}}));
    }
    return yaml;
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, PriorityGroupIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// The initial attempt uses the first group and every retry moves the request to the next group, so
// a request outlives the failure of the clusters ahead of the one that answers it.
TEST_P(PriorityGroupIntegrationTest, RetryMovesRequestToNextGroup) {
  setupClusters();
  setupRoute(groupPerCluster({ClusterA, ClusterB, ClusterC}), retryPolicy(2));
  initialize();

  setUpstreamStatus(0, "503");
  setUpstreamStatus(1, "503");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders());
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(ClusterC, servedBy(response->headers()));
  // Every cluster of the chain saw exactly one attempt.
  EXPECT_EQ(1, requestsTo(ClusterA));
  EXPECT_EQ(1, requestsTo(ClusterB));
  EXPECT_EQ(1, requestsTo(ClusterC));
}

// A group that is listed twice spends two attempts on its clusters before the request falls back,
// which is how a "try the primary twice, then move on" policy is configured.
TEST_P(PriorityGroupIntegrationTest, RepeatedGroupSpendsSeveralAttempts) {
  setupClusters();
  setupRoute(groupPerCluster({ClusterA, ClusterA, ClusterB}), retryPolicy(2));
  initialize();

  setUpstreamStatus(0, "503");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders());
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(ClusterB, servedBy(response->headers()));
  EXPECT_EQ(2, requestsTo(ClusterA));
  EXPECT_EQ(1, requestsTo(ClusterB));
}

// Once the attempts go past the end of the group list, the request stays on the last group rather
// than walking the chain again from its beginning.
TEST_P(PriorityGroupIntegrationTest, AttemptsStayOnTheLastGroup) {
  setupClusters();
  setupRoute(groupPerCluster({ClusterA, ClusterB}), retryPolicy(2));
  initialize();

  setUpstreamStatus(0, "503");
  setUpstreamStatus(1, "503");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders());
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  // Both groups failed, so the failure of the last attempt reaches the downstream.
  EXPECT_EQ("503", response->headers().getStatusValue());
  // The third attempt stayed on the last group.
  EXPECT_EQ(1, requestsTo(ClusterA));
  EXPECT_EQ(2, requestsTo(ClusterB));
  EXPECT_EQ(ClusterB, servedBy(response->headers()));
}

// Without refresh_cluster_on_retry the router keeps the cluster of the initial attempt.
TEST_P(PriorityGroupIntegrationTest, RetryStaysInFirstGroupWithoutClusterRefresh) {
  setupClusters();
  setupRoute(groupPerCluster({ClusterA, ClusterB}), R"EOF(
retry_on: 5xx
num_retries: 1
)EOF");
  initialize();

  setUpstreamStatus(0, "503");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders());
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(2, requestsTo(ClusterA));
  EXPECT_EQ(0, requestsTo(ClusterB));
}

// The target cluster of a group is picked from the cluster weights, and the random value that
// drives the pick can be supplied by a request header so that the choice is reproducible across
// proxy levels.
TEST_P(PriorityGroupIntegrationTest, WeightedClusterSelectionUsesHeaderRandomValue) {
  setupClusters();
  setupRoute(R"EOF(
header_name: x-random-value
priority_groups:
- name: main
  clusters:
  - cluster_name: cluster_a
    weight: 1
  - cluster_name: cluster_b
    weight: 1
)EOF",
             retryPolicy(1));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  {
    // The value falls in the weight interval of the first cluster.
    auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"x-random-value", "0"}}));
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(ClusterA, servedBy(response->headers()));
  }
  {
    // The value falls in the weight interval of the second cluster.
    auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({{"x-random-value", "1"}}));
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(ClusterB, servedBy(response->headers()));
  }
  EXPECT_EQ(1, requestsTo(ClusterA));
  EXPECT_EQ(1, requestsTo(ClusterB));
}

// A group override in the dynamic metadata replaces the configured order of the groups for the
// request, so a filter ahead of the router decides the fallback chain per request.
TEST_P(PriorityGroupIntegrationTest, MetadataOverridesTheOrderOfTheGroups) {
  setupClusters();
  setupGroupOverrideMetadata(R"({{name = "cluster_c"}, {name = "cluster_a"}})");
  setupRoute(absl::StrCat(R"EOF(
override_metadata_namespace: envoy.test
)EOF",
                          groupPerCluster({ClusterA, ClusterB, ClusterC})),
             retryPolicy(1));
  initialize();

  setUpstreamStatus(2, "503");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders());
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  // The metadata put cluster_c first and cluster_a second, so the configured order, which starts
  // with cluster_a, was not used.
  EXPECT_EQ(ClusterA, servedBy(response->headers()));
  EXPECT_EQ(1, requestsTo(ClusterC));
  EXPECT_EQ(1, requestsTo(ClusterA));
  EXPECT_EQ(0, requestsTo(ClusterB));
}

// A group override may also carry its own clusters and weights, which replace the ones of the
// configured group for the request.
TEST_P(PriorityGroupIntegrationTest, MetadataOverridesTheClustersOfAGroup) {
  setupClusters();
  setupGroupOverrideMetadata(
      R"({{name = "overridden", clusters = {{cluster_name = "cluster_c", weight = 100}}}})");
  setupRoute(absl::StrCat(R"EOF(
override_metadata_namespace: envoy.test
)EOF",
                          groupPerCluster({ClusterA, ClusterB})),
             retryPolicy(1));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders());
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  // The cluster of the metadata answered, although it is not part of any configured group.
  EXPECT_EQ(ClusterC, servedBy(response->headers()));
  EXPECT_EQ(0, requestsTo(ClusterA));
  EXPECT_EQ(1, requestsTo(ClusterC));
}

// A cluster that only exists in the group override metadata is not validated when the route
// configuration is loaded, so a name that resolves to nothing fails the request at runtime.
TEST_P(PriorityGroupIntegrationTest, UnknownClusterFromMetadataFailsRequest) {
  setupClusters();
  setupGroupOverrideMetadata(
      R"({{name = "overridden", clusters = {{cluster_name = "cluster_does_not_exist", )"
      R"(weight = 100}}}})");
  setupRoute(absl::StrCat(R"EOF(
override_metadata_namespace: envoy.test
)EOF",
                          groupPerCluster({ClusterA})),
             retryPolicy(1));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders());
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("503", response->headers().getStatusValue());
  // The request failed because the selected cluster is missing, not for some earlier reason.
  EXPECT_EQ(1, test_server_->counter("http.config_test.no_cluster")->value());
  EXPECT_EQ(0, requestsTo(ClusterA));
}

// A metadata value that is not a usable list of group overrides is ignored and the configured
// groups are used, so a broken filter degrades to the static chain rather than to a failed
// request.
TEST_P(PriorityGroupIntegrationTest, InvalidMetadataFallsBackToConfiguredGroups) {
  setupClusters();
  setupGroupOverrideMetadata(R"("not-a-list-of-groups")");
  setupRoute(absl::StrCat(R"EOF(
override_metadata_namespace: envoy.test
)EOF",
                          groupPerCluster({ClusterA, ClusterB})),
             retryPolicy(1));
  initialize();

  setUpstreamStatus(0, "503");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders());
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(ClusterB, servedBy(response->headers()));
  EXPECT_EQ(1, requestsTo(ClusterA));
  EXPECT_EQ(1, requestsTo(ClusterB));
}

// The group override may also be published as typed dynamic metadata, which carries the
// PriorityGroupsOverride message itself rather than a struct of the same shape.
TEST_P(PriorityGroupIntegrationTest, TypedMetadataOverridesTheOrderOfTheGroups) {
  setupClusters();
  setupTypedGroupOverrideMetadata(R"EOF(priority_groups:
      - name: cluster_c
      - name: cluster_a)EOF");
  setupRoute(absl::StrCat(R"EOF(
override_metadata_namespace: envoy.test
)EOF",
                          groupPerCluster({ClusterA, ClusterB, ClusterC})),
             retryPolicy(1));
  initialize();

  setUpstreamStatus(2, "503");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders());
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  // The typed metadata put cluster_c first and cluster_a second, so the configured order, which
  // starts with cluster_a, was not used.
  EXPECT_EQ(ClusterA, servedBy(response->headers()));
  EXPECT_EQ(1, requestsTo(ClusterC));
  EXPECT_EQ(1, requestsTo(ClusterA));
  EXPECT_EQ(0, requestsTo(ClusterB));
}

} // namespace
} // namespace PriorityGroup
} // namespace Router
} // namespace Extensions
} // namespace Envoy
